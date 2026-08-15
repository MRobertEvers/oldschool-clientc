#ifndef TEX_SPAN_FACEALPHA_U_C
#define TEX_SPAN_FACEALPHA_U_C

/*
 * Perspective texture spans that blend the shaded texel into the destination
 * with a per-face alpha, rather than overwriting it.
 *
 * ## Why this is a separate file, and scalar
 *
 * The plain spans live in tex.span.{scalar,sse2,sse41,avx,neon}.u.c: one symbol
 * set, five implementations, chosen at compile time. They can be wide because
 * they only ever *write* the frame buffer - eight shaded texels go out and
 * nothing comes back. A face-alpha span has to read the destination, blend, and
 * write, and that read-modify-write is what the SIMD chain is not shaped for.
 * The `scanline` family reached the same conclusion for its own facealpha
 * variants (see scanline.texture_tmpl.inc), which stay scalar while their
 * opaque siblings call straight into the per-ISA kernels.
 *
 * So these live outside the ISA rotation entirely: one scalar definition, no
 * per-backend twin to keep in step, and no risk of an ISA file silently missing
 * a symbol the dispatcher expects.
 *
 * ## Alpha convention
 *
 * `face_alpha` is the SOURCE weight - 0xFF draws the texel outright, 0 leaves
 * the destination untouched. That is alpha_blend()'s convention and the one the
 * gouraud alpha kernels use. It is the inverse of the raw `face_alphas` byte on
 * a model, which the caller flips (`0xFF - a`) before it gets here; the raster
 * dispatcher already does exactly that for untextured faces.
 *
 * Everything else - the uv quotient rules, the 8-pixel linear fit and its
 * fallback to a per-pixel exact block - is shared with the opaque spans through
 * tex.span_uv.h, so a face that switches between the two gates keeps the same
 * texture mapping and only its compositing changes.
 */

#include "graphics/alpha.h"
#include "graphics/dash_restrict.h"
#include "graphics/shade.h"
#include "graphics/raster/texture/span/tex.span_uv.h"

#include <stdint.h>

/**
 * The per-pixel path, blending. Twin of tex_span_exact_block; only blocks that
 * fail tex_span_lerp8_v_fits come here, so this is cold.
 */
static inline void
tex_span_exact_block_facealpha(
    int* RESTRICT pixel_buffer,
    int offset,
    const int* RESTRICT texels,
    int count,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade,
    int texture_width,
    int texture_shift,
    int transparent,
    int face_alpha)
{
    for( int i = 0; i < count; i++ )
    {
        int w = cw >> texture_shift;
        if( w != 0 )
        {
            int u = clamp(au / w, 0, texture_width - 1);
            int v = (bv / w) & (texture_width - 1);
            int texel = texels[u + (v << texture_shift)];
            if( !transparent || texel != 0 )
                pixel_buffer[offset + i] = alpha_blend(
                    face_alpha, pixel_buffer[offset + i], shade_blend(texel, shade));
        }
        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;
    }
}

/** Eight pixels along a fitted uv line, every texel drawn. */
static inline void
raster_linear_opaque_blend_facealpha_lerp8_v3(
    uint32_t* RESTRICT pixel_buffer,
    const uint32_t* RESTRICT texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int u_mask,
    int v_mask,
    int shade,
    int face_alpha)
{
    for( int i = 0; i < 8; i++ )
    {
        int u = (u_scan >> texture_shift) & u_mask;
        int v = v_scan & v_mask;
        int t = texels[u + v];
        pixel_buffer[i] =
            (uint32_t)alpha_blend(face_alpha, (int)pixel_buffer[i], shade_blend(t, shade));

        u_scan += step_u;
        v_scan += step_v;
    }
}

/** Eight pixels along a fitted uv line, texel 0 skipped. */
static inline void
raster_linear_transparent_blend_facealpha_lerp8_v3(
    uint32_t* RESTRICT pixel_buffer,
    const uint32_t* RESTRICT texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int u_mask,
    int v_mask,
    int shade,
    int face_alpha)
{
    for( int i = 0; i < 8; i++ )
    {
        int u = (u_scan >> texture_shift) & u_mask;
        int v = v_scan & v_mask;
        int t = texels[u + v];
        if( t != 0 )
            pixel_buffer[i] =
                (uint32_t)alpha_blend(face_alpha, (int)pixel_buffer[i], shade_blend(t, shade));

        u_scan += step_u;
        v_scan += step_v;
    }
}

/* ------------------------------------------------------------------ rows */

/*
 * One already-y-clipped scanline. Generated twice from the template below, once
 * per texel gate, so the uv walk cannot drift between the two.
 */

#define TSFA_SPAN_FN draw_texture_scanline_opaque_blend_facealpha_branching_lerp8_v3_ordered
#define TSFA_SPAN_BLOCK8 raster_linear_opaque_blend_facealpha_lerp8_v3
#define TSFA_SPAN_TRANS 0
#include "graphics/raster/texture/span/tex.span.facealpha_tmpl.inc"

#define TSFA_SPAN_FN draw_texture_scanline_transparent_blend_facealpha_branching_lerp8_v3_ordered
#define TSFA_SPAN_BLOCK8 raster_linear_transparent_blend_facealpha_lerp8_v3
#define TSFA_SPAN_TRANS 1
#include "graphics/raster/texture/span/tex.span.facealpha_tmpl.inc"

#endif
