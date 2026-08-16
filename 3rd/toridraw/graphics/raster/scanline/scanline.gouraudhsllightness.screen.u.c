#ifndef TORIDRAW_RASTER_SCANLINE_GOURAUDHSLLIGHTNESSHSLLIGHTNESS_SCREEN_U_C
#define TORIDRAW_RASTER_SCANLINE_GOURAUDHSLLIGHTNESSHSLLIGHTNESS_SCREEN_U_C

/**
 * Gouraud triangles, `scanline` walk, barycentric colour plane.
 *
 * Variant IDs: `gouraud.screen.opaque.bary.scanline.s4`,
 *              `gouraud.screen.alpha.bary.scanline.s4`.
 *
 * Drop-in replacements for `raster_gouraudhsllightness_screen_{opaque,alpha}_bary_branching_s4`.
 *
 * The colour gradient is a plane, so it is invariant under the y-sort: it is
 * derived from the *unsorted* vertices and the sorted signed area is recovered
 * from the permutation parity rather than recomputed. Only the plane's base
 * value needs the sorted top vertex.
 *
 * Beyond the shared `scanline` wins (single sort, arithmetic y-clip,
 * per-segment horizontal clip classification) this adds the flat-span
 * degeneration: when the colour step across x is zero the run is a single
 * colour and drops into the flat fill.
 */

#include "graphics/dash_restrict.h"
#include "graphics/int_wrap.h"
#include "graphics/raster/scanline/scanline_common.h"
#include "graphics/raster/scanline/span/scanline.span.solid.u.c"
#include "graphics/shared_tables.h"

struct ScanlineGouraudPlane
{
    int step_x_ish8;
    int step_y_ish8;
    int base_ish8;
};

/**
 * Colour plane for a sorted triangle.
 *
 * `base_ish8` is the plane value at (x = 0, y = tri->ya), carrying the same
 * one-step nudge the `bary` kernels apply so the two families agree pixel for
 * pixel.
 */
static inline void
scanline_gouraud_plane(
    struct ScanlineGouraudPlane* RESTRICT out,
    const struct ScanlineTri* RESTRICT tri,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16)
{
    /* Signed area of the *unsorted* triangle: same magnitude as the sorted
     * one, sign flipped by an odd permutation. */
    int sarea = tri->parity ? -tri->sarea : tri->sarea;

    int dx_ab = x1 - x0;
    int dy_ab = y1 - y0;
    int dx_ac = x2 - x0;
    int dy_ac = y2 - y0;

    int d_hsl_ab = color1_hsl16 - color0_hsl16;
    int d_hsl_ac = color2_hsl16 - color0_hsl16;

    out->step_x_ish8 = scanline_bary_step_ish8(d_hsl_ab * dy_ac - d_hsl_ac * dy_ab, sarea);
    out->step_y_ish8 = scanline_bary_step_ish8(d_hsl_ac * dx_ab - d_hsl_ab * dx_ac, sarea);

    int colors[3];
    colors[0] = color0_hsl16;
    colors[1] = color1_hsl16;
    colors[2] = color2_hsl16;

    int color_top = colors[tri->perm[0]];
    /* Modular: the reference client relies on int wraparound here, and an edge-on
     * triangle reaches the overflow. See graphics/int_wrap.h. */
    out->base_ish8 = toridraw_wrap_sub(
        toridraw_wrap_add(out->step_x_ish8, color_top << 8),
        toridraw_wrap_mul(tri->xa, out->step_x_ish8));
}

static inline void
raster_gouraudhsllightness_screen_opaque_bary_scanline_s4(
    toripixel_t* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16)
{
    struct ScanlineTri tri;
    struct ScanlineEdges edges;

    if( !scanline_tri_build(&tri, x0, x1, x2, y0, y1, y2) )
        return;
    if( !scanline_edges_build(&edges, &tri, screen_height) )
        return;

    struct ScanlineGouraudPlane plane;
    scanline_gouraud_plane(
        &plane, &tri, x0, x1, x2, y0, y1, y2, color0_hsl16, color1_hsl16, color2_hsl16);

    int step_x_ish8 = plane.step_x_ish8;
    int step_y_ish8 = plane.step_y_ish8;

    for( int seg = 0; seg < 2; seg++ )
    {
        int rows = edges.rows[seg];
        if( rows <= 0 )
            continue;

        int l_ish16 = edges.l_ish16[seg];
        int l_step = edges.l_step[seg];
        int r_ish16 = edges.r_ish16[seg];
        int r_step = edges.r_step[seg];
        int offset = edges.y0[seg] * stride;
        int hsl_ish8 = toridraw_wrap_add(
            plane.base_ish8, toridraw_wrap_mul(step_y_ish8, edges.y0[seg] - tri.ya));

        if( scanline_segment_no_hclip(l_ish16, l_step, r_ish16, r_step, rows, screen_width) )
        {
            while( rows-- > 0 )
            {
                int x_start = l_ish16 >> 16;
                int count = (r_ish16 >> 16) - x_start;
                if( count > 0 )
                    scanline_span_gouraudhsllightness_opaque(
                        pixel_buffer,
                        offset + x_start,
                        count,
                        hsl_ish8 + x_start * step_x_ish8,
                        step_x_ish8);

                l_ish16 += l_step;
                r_ish16 += r_step;
                hsl_ish8 = toridraw_wrap_add(hsl_ish8, step_y_ish8);
                offset += stride;
            }
        }
        else
        {
            while( rows-- > 0 )
            {
                int x_start;
                int count = scanline_row_clip(l_ish16, r_ish16, screen_width, &x_start);
                if( count > 0 )
                    scanline_span_gouraudhsllightness_opaque(
                        pixel_buffer,
                        offset + x_start,
                        count,
                        hsl_ish8 + x_start * step_x_ish8,
                        step_x_ish8);

                l_ish16 += l_step;
                r_ish16 += r_step;
                hsl_ish8 = toridraw_wrap_add(hsl_ish8, step_y_ish8);
                offset += stride;
            }
        }
    }
}

static inline void
raster_gouraudhsllightness_screen_alpha_bary_scanline_s4(
    toripixel_t* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16,
    int alpha)
{
    struct ScanlineTri tri;
    struct ScanlineEdges edges;

    if( !scanline_tri_build(&tri, x0, x1, x2, y0, y1, y2) )
        return;
    if( !scanline_edges_build(&edges, &tri, screen_height) )
        return;

    struct ScanlineGouraudPlane plane;
    scanline_gouraud_plane(
        &plane, &tri, x0, x1, x2, y0, y1, y2, color0_hsl16, color1_hsl16, color2_hsl16);

    int step_x_ish8 = plane.step_x_ish8;
    int step_y_ish8 = plane.step_y_ish8;

    for( int seg = 0; seg < 2; seg++ )
    {
        int rows = edges.rows[seg];
        if( rows <= 0 )
            continue;

        int l_ish16 = edges.l_ish16[seg];
        int l_step = edges.l_step[seg];
        int r_ish16 = edges.r_ish16[seg];
        int r_step = edges.r_step[seg];
        int offset = edges.y0[seg] * stride;
        int hsl_ish8 = toridraw_wrap_add(
            plane.base_ish8, toridraw_wrap_mul(step_y_ish8, edges.y0[seg] - tri.ya));

        if( scanline_segment_no_hclip(l_ish16, l_step, r_ish16, r_step, rows, screen_width) )
        {
            while( rows-- > 0 )
            {
                int x_start = l_ish16 >> 16;
                int count = (r_ish16 >> 16) - x_start;
                if( count > 0 )
                    scanline_span_gouraudhsllightness_alpha(
                        pixel_buffer,
                        offset + x_start,
                        count,
                        hsl_ish8 + x_start * step_x_ish8,
                        step_x_ish8,
                        alpha);

                l_ish16 += l_step;
                r_ish16 += r_step;
                hsl_ish8 = toridraw_wrap_add(hsl_ish8, step_y_ish8);
                offset += stride;
            }
        }
        else
        {
            while( rows-- > 0 )
            {
                int x_start;
                int count = scanline_row_clip(l_ish16, r_ish16, screen_width, &x_start);
                if( count > 0 )
                    scanline_span_gouraudhsllightness_alpha(
                        pixel_buffer,
                        offset + x_start,
                        count,
                        hsl_ish8 + x_start * step_x_ish8,
                        step_x_ish8,
                        alpha);

                l_ish16 += l_step;
                r_ish16 += r_step;
                hsl_ish8 = toridraw_wrap_add(hsl_ish8, step_y_ish8);
                offset += stride;
            }
        }
    }
}

#endif
