#include "graphics/int_wrap.h"
#include "graphics/tori_compat.h"

#ifndef TORIDRAW_RASTER_ZBUF_SCREEN_U_C
#define TORIDRAW_RASTER_ZBUF_SCREEN_U_C

/*
 * Depth-tested raster kernels.
 *
 * These are the depth-aware twins of the flat, gouraud and perspective-texture
 * kernels. A model tagged TORIDRAW_MODEL_FLAG_ZBUFFER draws through these, so
 * its faces resolve per pixel rather than by face order alone. Nothing else
 * changes: the same faces in the same order reach the same trapezoid walk, and
 * because the model resets the buffer before it draws, the test only ever
 * resolves that model against itself (see graphics/zdepth.h).
 *
 * Three things are deliberate.
 *
 * 1. ONE walker, three shading modes, instead of a depth-tested copy of each of
 *    the eight existing kernels. The mode is a constant for every pixel of a
 *    model, so the per-pixel branch predicts perfectly, and the alternative was
 *    eight files that could drift apart in their coverage rules. Coverage is
 *    exactly what must NOT drift: a z-buffered model has to keep the silhouette
 *    and the seams it had without depth, or the feature changes pictures it was
 *    only supposed to reorder. The span entry/exit rules below are therefore
 *    copied per mode from the family each mode replaces, differences included.
 *
 * 2. The spans are per-pixel. The stock kernels amortise work over blocks —
 *    gouraud recolours every fourth pixel, the texture spans fit a line through
 *    eight — and a depth test cannot be amortised that way, since neighbouring
 *    pixels can fall on opposite sides of it. The texture mode consequently
 *    takes the exact per-pixel divide the reference rasterizer takes for every
 *    pixel (docs/raster_scanlines_thedaneeffect.txt) rather than the 8-pixel
 *    linear fit. This path is opt-in and rare; correctness is worth more here
 *    than throughput.
 *
 * 3. Only opaque faces write depth. A translucent face (face alpha < 255, or a
 *    per-texel-alpha material) tests against the buffer but leaves it alone, so
 *    the back-to-front blend the depth sort set up still composites in order. A
 *    colour-keyed texel that is skipped writes nothing either, because the depth
 *    write sits behind the same test that decides to draw the pixel.
 */

#include "graphics/alpha.h"
#include "graphics/clamp.h"
#include "graphics/dash_restrict.h"
#include "impl/projection/projection.scalar_reference.u.c"
#include "graphics/raster/gouraudhsllightness/gouraudhsllightness_barycentric_steps.h"
#include "graphics/shade.h"
#include "graphics/shared_tables.h"
#include "graphics/zdepth.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum ToriDraw_ZbufMode
{
    TORIDRAW_ZBUF_MODE_FLAT,
    TORIDRAW_ZBUF_MODE_GOURAUD,
    TORIDRAW_ZBUF_MODE_TEXTURE,
};

enum ToriDraw_ZbufTexKind
{
    /** Every texel is drawn. */
    TORIDRAW_ZBUF_TEX_OPAQUE,
    /** Pure black is the colour key and is skipped. */
    TORIDRAW_ZBUF_TEX_TRANSPARENT,
    /** Each texel carries its own coverage in bits 24-31. */
    TORIDRAW_ZBUF_TEX_ALPHA,
};

/**
 * One triangle, as the face-level wrappers hand it over.
 *
 * x/y/key/shade are per screen corner and are permuted together by the y sort.
 * The uv basis is not: it belongs to the face's p/m/n texture vertices, which
 * are a different triple from a/b/c and must stay where the model put them.
 */
struct ToriDraw_ZbufTriangle
{
    int mode;

    int x[3];
    int y[3];
    /** Depth key per corner — toridraw_zdepth_key of that corner's camera z. */
    float key[3];

    /**
     * FLAT: shade[0] is the face's hsl16 colour, the rest unused.
     * GOURAUD: the three hsl16 corner colours.
     * TEXTURE: the three 0..127 corner lightnesses.
     */
    int shade[3];

    /** Face coverage 0..255. 255 draws opaque and is the only case that writes
     *  depth. Ignored in TEXTURE mode, which is what the stock textured kernels
     *  do with face_alphas as well. */
    int alpha;

    /* TEXTURE only. */
    int uv_x[3]; /* origin, u end, v end — camera space */
    int uv_y[3];
    int uv_z[3];
    int* texels;
    int texture_width;
    int tex_kind;
};

/**
 * Everything a single scanline needs. The row-varying members are advanced by
 * the walker between scanlines; the span function only reads.
 */
struct ToriDraw_ZbufSpanState
{
    int mode;
    int tex_kind;
    int alpha;
    bool depth_write;

    /** Depth key at x == 0 of this row, and its per-pixel step. */
    float key_at_x0;
    float key_step_dx;

    /** FLAT: the resolved rgb. */
    toripixel_t rgb;

    /** GOURAUD: 8.8 hsl16 at x == 0. */
    int hsl_ish8;
    int hsl_step_dx_ish8;

    /** TEXTURE: the three homogeneous numerators, stated at the screen's centre
     *  column — the origin the perspective texture kernels anchor to. */
    int au;
    int bv;
    int cw;
    int step_au_dx;
    int step_bv_dx;
    int step_cw_dx;
    /** TEXTURE: 8.8 lightness at x == 0. */
    int shade8_ish8;
    int step_shade8_dx_ish8;
    const int* texels;
    int texture_width;
    int texture_shift;
};

/** Per-scanline deltas for the members above that vary down the triangle. */
struct ToriDraw_ZbufRowStep
{
    float key_dy;
    int hsl_dy_ish8;
    int au_dy;
    int bv_dy;
    int cw_dy;
    int shade8_dy_ish8;
};

static inline void
zbuf_row_advance(
    struct ToriDraw_ZbufSpanState* s,
    const struct ToriDraw_ZbufRowStep* d)
{
    s->key_at_x0 += d->key_dy;
    /* Modular: the reference client relies on int wraparound here, and an edge-on
     * triangle reaches the overflow. See graphics/int_wrap.h. */
    s->hsl_ish8 = toridraw_wrap_add(s->hsl_ish8, d->hsl_dy_ish8);
    s->au += d->au_dy;
    s->bv += d->bv_dy;
    s->cw += d->cw_dy;
    s->shade8_ish8 += d->shade8_dy_ish8;
}

/**
 * One depth-tested scanline.
 *
 * `offset` is the row's first element in BOTH buffers: the z-buffer is
 * allocated at the frame buffer's stride precisely so one index walks both.
 */
static void
zbuf_span(
    toripixel_t* RESTRICT pixel_buffer,
    torizdepth_t* RESTRICT zbuffer,
    int offset,
    int screen_width,
    int x_start_ish16,
    int x_end_ish16,
    const struct ToriDraw_ZbufSpanState* s)
{
    int x_start;
    int x_end;

    if( s->mode == TORIDRAW_ZBUF_MODE_TEXTURE )
    {
        /* The perspective texture spans open the run one subpixel early; the
         * flat and gouraud ones do not. Kept per mode so a z-buffered face
         * covers the same pixels its stock twin covered. */
        x_start = (x_start_ish16 - 1) >> 16;
        x_end = x_end_ish16 >> 16;
    }
    else
    {
        if( x_start_ish16 == x_end_ish16 )
            return;
        x_start = x_start_ish16 >> 16;
        x_end = x_end_ish16 >> 16;
    }

    if( x_start < 0 )
        x_start = 0;
    if( x_end >= screen_width )
        x_end = screen_width - 1;
    if( x_start >= x_end )
        return;

    float key = s->key_at_x0 + s->key_step_dx * (float)x_start;
    int hsl_ish8 = toridraw_wrap_add(s->hsl_ish8, toridraw_wrap_mul(s->hsl_step_dx_ish8, x_start));
    int shade8_ish8 = s->shade8_ish8 + s->step_shade8_dx_ish8 * x_start;

    /* The texture numerators are stated at the centre column, so walk them from
     * there to the first pixel actually drawn. */
    int const adjust = x_start - (screen_width >> 1);
    int au = s->au + s->step_au_dx * adjust;
    int bv = s->bv + s->step_bv_dx * adjust;
    int cw = s->cw + s->step_cw_dx * adjust;

    for( int x = x_start; x < x_end; x++ )
    {
        int const at = offset + x;

        /* Strictly nearer wins. Equal depth keeps what is already there, which
         * is what stops the shared edge between two faces of one model from
         * fighting. */
        if( key > (float)zbuffer[at] )
        {
            toripixel_t color = 0;
            bool drew = true;

            switch( s->mode )
            {
            case TORIDRAW_ZBUF_MODE_FLAT:
                color = s->rgb;
                break;

            case TORIDRAW_ZBUF_MODE_GOURAUD:
                color = ToriDraw_Hsl16Ish8ToPixel(hsl_ish8);
                break;

            default:
            {
                int const w = cw >> s->texture_shift;

                if( w == 0 )
                {
                    drew = false;
                    break;
                }

                int const u = clamp(au / w, 0, s->texture_width - 1);
                int const v = (bv / w) & (s->texture_width - 1);
                uint32_t const texel = (uint32_t)s->texels[u + (v << s->texture_shift)];
                int const shade = shade8_ish8 >> 8;

                if( s->tex_kind == TORIDRAW_ZBUF_TEX_ALPHA )
                {
                    int const texel_alpha = (int)(texel >> 24);
                    toripixel_t lit;

                    if( texel_alpha == 0 )
                    {
                        drew = false;
                        break;
                    }
                    lit = toritexel_to_pixel(
                        toritexel_shade_blend(texel & TORITEXEL_COLOR_MASK, shade));
                    color = texel_alpha == 0xFF
                                ? lit
                                : alpha_blend(texel_alpha, pixel_buffer[at], lit);
                }
                else if( s->tex_kind == TORIDRAW_ZBUF_TEX_TRANSPARENT && texel == 0 )
                {
                    drew = false;
                }
                else
                {
                    color = toritexel_to_pixel(toritexel_shade_blend(texel, shade));
                }
                break;
            }
            }

            if( drew )
            {
                /* Face alpha belongs to the untextured kernels only — the stock
                 * textured ones never consult face_alphas either. */
                if( s->mode != TORIDRAW_ZBUF_MODE_TEXTURE && s->alpha != 0xFF )
                    color = alpha_blend(s->alpha, pixel_buffer[at], color);

                pixel_buffer[at] = color;
                if( s->depth_write )
                    zbuffer[at] = (torizdepth_t)key;
            }
        }

        key += s->key_step_dx;
        hsl_ish8 = toridraw_wrap_add(hsl_ish8, s->hsl_step_dx_ish8);
        shade8_ish8 += s->step_shade8_dx_ish8;
        au += s->step_au_dx;
        bv += s->step_bv_dx;
        cw += s->step_cw_dx;
    }
}

/**
 * The trapezoid walk, with corners already sorted so y0 <= y1 <= y2.
 *
 * Structurally the gouraud kernel's walk, because that is the shape every
 * family here shares: two edge pairs, a per-row advance, and a segment split at
 * the middle vertex.
 */
static void
raster_zbuf_screen_ordered(
    toripixel_t* RESTRICT pixel_buffer,
    torizdepth_t* RESTRICT zbuffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    const struct ToriDraw_ZbufTriangle* tri)
{
    int const x0 = tri->x[0];
    int const x1 = tri->x[1];
    int const x2 = tri->x[2];
    int y0 = tri->y[0];
    int y1 = tri->y[1];
    int y2 = tri->y[2];

    if( y2 - y0 == 0 )
        return;

    int const dx_AC = x2 - x0;
    int const dy_AC = y2 - y0;
    int const dx_AB = x1 - x0;
    int const dy_AB = y1 - y0;

    /* Degeneracy is decided by the same integer area the stock kernel of this
     * mode decides it by, so a triangle either family drops is dropped by both.
     * The texture family spells it with the opposite sign; it is zero in exactly
     * the same cases. */
    int const sarea = dx_AB * dy_AC - dx_AC * dy_AB;
    if( sarea == 0 )
        return;

    struct ToriDraw_ZbufSpanState s;
    struct ToriDraw_ZbufRowStep d;
    struct ToriDraw_TexturePlane32 texture_plane;

    /* Read once. The branch that fills texture_plane and the branch that reads
     * it back are the same test, but through `tri->` they are two loads the
     * compiler cannot prove equal across the writes in between -- so it treats
     * the read as reachable without the write and reports texture_plane as
     * maybe-uninitialised. One local settles it, and saves the reloads. */
    int const mode = tri->mode;

    memset(&s, 0, sizeof(s));
    memset(&d, 0, sizeof(d));

    s.mode = mode;
    s.tex_kind = tri->tex_kind;
    s.alpha = tri->alpha;
    /* A translucent surface must not occlude what is drawn after it, and a
     * per-texel-alpha material is translucent texel by texel. Both test,
     * neither writes. */
    s.depth_write = tri->alpha == 0xFF && !(mode == TORIDRAW_ZBUF_MODE_TEXTURE &&
                                            tri->tex_kind == TORIDRAW_ZBUF_TEX_ALPHA);

    /*
     * Depth plane. The key is linear in screen space by construction (see
     * graphics/zdepth.h), so two gradients and a base describe it exactly.
     * Solved in double: the corner coordinates are ints that a near-clip lerp
     * can push far outside the viewport, and their cross products are the one
     * quantity here that would overflow.
     */
    {
        double const dsarea = (double)dx_AB * (double)dy_AC - (double)dx_AC * (double)dy_AB;
        double const dk_AB = (double)tri->key[1] - (double)tri->key[0];
        double const dk_AC = (double)tri->key[2] - (double)tri->key[0];

        s.key_step_dx = (float)((dk_AB * (double)dy_AC - dk_AC * (double)dy_AB) / dsarea);
        d.key_dy = (float)((dk_AC * (double)dx_AB - dk_AB * (double)dx_AC) / dsarea);
        s.key_at_x0 = (float)((double)tri->key[0] - (double)s.key_step_dx * (double)x0);
    }

    if( mode == TORIDRAW_ZBUF_MODE_GOURAUD )
    {
        int const d_hsl_AB = tri->shade[1] - tri->shade[0];
        int const d_hsl_AC = tri->shade[2] - tri->shade[0];

        gouraudhsllightness_recip_t recip_sarea = gouraudhsllightness_barycentric_recip(sarea);
        s.hsl_step_dx_ish8 = gouraudhsllightness_barycentric_hsl_step_ish8(
            d_hsl_AB * dy_AC - d_hsl_AC * dy_AB, recip_sarea);
        d.hsl_dy_ish8 = gouraudhsllightness_barycentric_hsl_step_ish8(
            d_hsl_AC * dx_AB - d_hsl_AB * dx_AC, recip_sarea);
        s.hsl_ish8 = toridraw_wrap_sub(
            toridraw_wrap_add(s.hsl_step_dx_ish8, tri->shade[0] << 8),
            toridraw_wrap_mul(x0, s.hsl_step_dx_ish8));
    }
    else if( mode == TORIDRAW_ZBUF_MODE_FLAT )
    {
        s.rgb = g_hsl16_to_pixel_table[tri->shade[0] & 0xFFFF];
    }
    /* Spelled as the positive test rather than a bare `else`, so it is the same
     * condition that gates the read of texture_plane further down. `mode` is a
     * plain int, so under an `else` the compiler cannot tell the two apart and
     * reports texture_plane as maybe-uninitialised. */
    else if( mode == TORIDRAW_ZBUF_MODE_TEXTURE )
    {
        /* Perspective uv basis, straight out of the texture kernels: three
         * homogeneous numerator planes whose ratios are u and v. */
        int const vU_x = tri->uv_x[1] - tri->uv_x[0];
        int const vU_y = tri->uv_y[1] - tri->uv_y[0];
        int const vU_z = tri->uv_z[1] - tri->uv_z[0];
        int const vV_x = tri->uv_x[2] - tri->uv_x[0];
        int const vV_y = tri->uv_y[2] - tri->uv_y[0];
        int const vV_z = tri->uv_z[2] - tri->uv_z[0];

        /* The shade gradients divide by the texture family's spelling of the
         * area, which is this one's negation. */
        int const sarea_abc = -sarea;
        int const dshade_ab = tri->shade[1] - tri->shade[0];
        int const dshade_ac = tri->shade[2] - tri->shade[0];

        struct ToriDraw_TexturePlane32 const built = {
            .term = {
                { tri->uv_y[0] * vV_z - tri->uv_z[0] * vV_y,
                  tri->uv_z[0] * vV_x - tri->uv_x[0] * vV_z,
                  tri->uv_x[0] * vV_y - tri->uv_y[0] * vV_x,
                  0 },
                { vU_y * tri->uv_z[0] - vU_z * tri->uv_y[0],
                  vU_z * tri->uv_x[0] - vU_x * tri->uv_z[0],
                  vU_x * tri->uv_y[0] - vU_y * tri->uv_x[0],
                  0 },
                { vU_z * vV_y - vU_y * vV_z,
                  vU_x * vV_z - vU_z * vV_x,
                  vU_y * vV_x - vU_x * vV_y,
                  0 },
            },
        };

        texture_plane = built;
        if( !ToriDraw_TexturePlanePrepare32(
                &texture_plane, screen_width, screen_height, camera_cot16) )
            return;

        s.step_au_dx = texture_plane.term[0].x;
        s.step_bv_dx = texture_plane.term[1].x;
        s.step_cw_dx = texture_plane.term[2].x;
        d.au_dy = texture_plane.term[0].y;
        d.bv_dy = texture_plane.term[1].y;
        d.cw_dy = texture_plane.term[2].y;
        /* Take the bases here too, where the plane is known to be built, rather
         * than reading texture_plane again after the clipping below. Nothing
         * touches s.au/bv/cw in between, so the row walk further down only has
         * to add its dy term. Keeping the one read here is also what stops the
         * compiler reporting texture_plane as maybe-uninitialised: it cannot
         * carry `mode == TEXTURE` across that much intervening control flow. */
        s.au = texture_plane.term[0].base;
        s.bv = texture_plane.term[1].base;
        s.cw = texture_plane.term[2].base;

        d.shade8_dy_ish8 = ((dx_AC * dshade_ab - dx_AB * dshade_ac) << 9) / sarea_abc;
        s.step_shade8_dx_ish8 = ((dy_AB * dshade_ac - dy_AC * dshade_ab) << 9) / sarea_abc;
        s.shade8_ish8 = (tri->shade[0] << 9) - s.step_shade8_dx_ish8 * x0 + s.step_shade8_dx_ish8;

        s.texels = tri->texels;
        s.texture_width = tri->texture_width;
        s.texture_shift = (tri->texture_width == 128) ? 7 : 6;
    }

    int step_edge_x_AC_ish16 = 0;
    int step_edge_x_AB_ish16 = 0;
    int step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( y2 != y1 )
        step_edge_x_BC_ish16 = ((x2 - x1) << 16) / (y2 - y1);

    int edge_x_AC_ish16 = x0 << 16;
    int edge_x_AB_ish16 = x0 << 16;
    int edge_x_BC_ish16 = x1 << 16;

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        /*
         * Only pre-step A->B if that edge is ever walked.
         *
         * Rows [y0, y1) use A->B, so when the middle vertex is at or above the
         * top of the viewport the span is empty (`rows_seg1` clamps to 0 below)
         * and this value feeds nothing. Skipping it is not an optimisation: a
         * near-horizontal AB has a tiny dy and therefore a slope of hundreds of
         * pixels per row, and multiplying that by a y0 far above the screen
         * overflows the 16.16 product -- for a result that is then discarded.
         * UBSan caught exactly that on the Queen Black Dragon:
         *
         *   zbuf.screen.u.c:474: signed integer overflow: 19595264 * -127
         *
         * A LIVE edge cannot overflow here, which is why this is a guard and
         * not a widening: the edge spans y0..y1 with y1 > 0, so dy >= |y0| and
         * the product is bounded by dx << 16 -- reaching INT_MAX needs a
         * projected dx over 32768px, which is geometry outside the raster's
         * coordinate domain (TORIDRAW_PROJECTED_COORD_LIMIT) rather than an
         * arithmetic problem. Same argument covers A->C (y2 >= 0 is guaranteed
         * by the caller's reject) and B->C (guarded by its own y1 < 0 block).
         */
        if( y1 > 0 )
            edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;
        s.key_at_x0 -= d.key_dy * (float)y0;
        s.hsl_ish8 = toridraw_wrap_sub(s.hsl_ish8, toridraw_wrap_mul(d.hsl_dy_ish8, y0));
        s.shade8_ish8 -= d.shade8_dy_ish8 * y0;

        y0 = 0;
    }

    if( y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * y1;

        y1 = 0;
    }

    if( mode == TORIDRAW_ZBUF_MODE_TEXTURE )
    {
        /* The numerators are stated at the screen centre in both axes; walk the
         * vertical one down to the first row drawn. */
        int const dy = y0 - (screen_height >> 1);

        s.au += d.au_dy * dy;
        s.bv += d.bv_dy * dy;
        s.cw += d.cw_dy * dy;
    }

    int offset = y0 * stride;

    if( y1 > screen_height )
    {
        y1 = screen_height;
        y2 = screen_height;
    }
    else if( y2 > screen_height )
    {
        y2 = screen_height;
    }

    /* Which of the two edges bounds the span on the right; the stock kernels
     * decide it with exactly this test. */
    bool const ac_is_right = (y0 == y1 && step_edge_x_AC_ish16 <= step_edge_x_BC_ish16) ||
                             (y0 != y1 && step_edge_x_AC_ish16 >= step_edge_x_AB_ish16);

    int rows_seg1 = y1 - y0;
    int rows_seg2 = y2 - y1;
    if( rows_seg1 < 0 )
        rows_seg1 = 0;
    if( rows_seg2 < 0 )
        rows_seg2 = 0;

    while( rows_seg1-- > 0 )
    {
        if( ac_is_right )
            zbuf_span(
                pixel_buffer, zbuffer, offset, screen_width, edge_x_AB_ish16, edge_x_AC_ish16, &s);
        else
            zbuf_span(
                pixel_buffer, zbuffer, offset, screen_width, edge_x_AC_ish16, edge_x_AB_ish16, &s);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;
        zbuf_row_advance(&s, &d);
        offset += stride;
    }

    while( rows_seg2-- > 0 )
    {
        if( ac_is_right )
            zbuf_span(
                pixel_buffer, zbuffer, offset, screen_width, edge_x_BC_ish16, edge_x_AC_ish16, &s);
        else
            zbuf_span(
                pixel_buffer, zbuffer, offset, screen_width, edge_x_AC_ish16, edge_x_BC_ish16, &s);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;
        zbuf_row_advance(&s, &d);
        offset += stride;
    }
}

/**
 * Entry point: sort the corners by y and hand the triangle to the walker.
 *
 * The stock kernels spell this as a six-way branch that re-calls the ordered
 * function with permuted arguments. Here the per-corner data is already an
 * array, so the permutation is a three-element sort — and it has to move x, y,
 * key and shade together, which the six-way form would spell out four times in
 * every arm.
 */
static void
raster_zbuf_screen(
    toripixel_t* RESTRICT pixel_buffer,
    torizdepth_t* RESTRICT zbuffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    const struct ToriDraw_ZbufTriangle* tri)
{
    struct ToriDraw_ZbufTriangle sorted = *tri;
    int order[3] = { 0, 1, 2 };

    if( tri->y[order[0]] > tri->y[order[1]] )
    {
        int const t = order[0];
        order[0] = order[1];
        order[1] = t;
    }
    if( tri->y[order[1]] > tri->y[order[2]] )
    {
        int const t = order[1];
        order[1] = order[2];
        order[2] = t;
    }
    if( tri->y[order[0]] > tri->y[order[1]] )
    {
        int const t = order[0];
        order[0] = order[1];
        order[1] = t;
    }

    for( int i = 0; i < 3; i++ )
    {
        sorted.x[i] = tri->x[order[i]];
        sorted.y[i] = tri->y[order[i]];
        sorted.key[i] = tri->key[order[i]];
        sorted.shade[i] = tri->shade[order[i]];
    }

    /* The stock kernels reject on the sorted extremes before doing any setup;
     * the same two tests, so an off-screen triangle costs the same here. */
    if( sorted.y[0] > screen_height || sorted.y[2] < 0 )
        return;

    raster_zbuf_screen_ordered(
        pixel_buffer, zbuffer, stride, screen_width, screen_height, camera_cot16, &sorted);
}

#endif
