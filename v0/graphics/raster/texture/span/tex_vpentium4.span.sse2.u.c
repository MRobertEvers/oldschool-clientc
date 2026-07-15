#ifndef TEX_VPENTIUM4_SPAN_SSE2_U_C
#define TEX_VPENTIUM4_SPAN_SSE2_U_C

#include "graphics/dash_restrict.h"
#include "graphics/shared_tables.h"
#include "graphics/shade.h"

#include <assert.h>
#include <emmintrin.h>
#include <stdint.h>

/* Pentium 4 / NetBurst-oriented span (SSE2). Names prefixed so this TU can coexist with
 * tex.span.sse2.u.c. */

static inline __m128i
p4_shade_blend4_sse(
    __m128i texel,
    int shade)
{
    __m128i texel_lo = _mm_unpacklo_epi8(texel, _mm_setzero_si128());
    __m128i texel_hi = _mm_unpackhi_epi8(texel, _mm_setzero_si128());
    __m128i shade_16 = _mm_set1_epi16(shade);
    texel_lo = _mm_mullo_epi16(texel_lo, shade_16);
    texel_hi = _mm_mullo_epi16(texel_hi, shade_16);
    texel_lo = _mm_srli_epi16(texel_lo, 8);
    texel_hi = _mm_srli_epi16(texel_hi, 8);
    return _mm_packus_epi16(texel_lo, texel_hi);
}

/* Normalized reciprocal on |w|; sign of w restored (negative w supported). */
static inline int
p4_persp_coord(
    int a,
    int w)
{
    if( w == 0 )
        return 0;
    int32_t mask = w >> 31;
    uint32_t w_abs = (uint32_t)((w ^ mask) - mask);
    int lz = __builtin_clz(w_abs);
    int shift = lz - 16;
    uint32_t w_norm = (shift >= 0) ? (w_abs << shift) : (w_abs >> -shift);
    uint32_t inv_w = g_reciprocal_norm30[w_norm & 0x7FFF];
    int64_t full = (int64_t)a * (int64_t)inv_w;
    int32_t result = (int32_t)(full >> (30 - shift));
    return (int32_t)(((int64_t)result ^ mask) - mask);
}

static inline void
raster_linear_opaque_blend_lerp8_vpentium4(
    uint32_t* __restrict pixel_buffer,
    const uint32_t* __restrict texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int u_mask,
    int v_mask,
    int shade)
{
    int idx[8];
    assert(texture_shift == 7 || texture_shift == 6);
    for( int i = 0; i < 8; i++ )
    {
        int u = (u_scan >> texture_shift) & u_mask;
        int v = v_scan & v_mask;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    __m128i t0 = _mm_set_epi32(texels[idx[3]], texels[idx[2]], texels[idx[1]], texels[idx[0]]);
    __m128i t1 = _mm_set_epi32(texels[idx[7]], texels[idx[6]], texels[idx[5]], texels[idx[4]]);
    __m128i r0 = p4_shade_blend4_sse(t0, shade);
    __m128i r1 = p4_shade_blend4_sse(t1, shade);

    _mm_storeu_si128((__m128i*)pixel_buffer, r0);
    _mm_storeu_si128((__m128i*)(pixel_buffer + 4), r1);
}

static inline void
draw_texture_scanline_opaque_blend_branching_lerp8_vpentium4_ordered(
    int* RESTRICT pixel_buffer,
    int screen_width,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* RESTRICT texels,
    int texture_width)
{
    int x0 = (screen_x0_ish16 - 1) >> 16;
    if( x0 < 0 )
        x0 = 0;
    int x1 = screen_x1_ish16 >> 16;
    if( x1 >= screen_width )
        x1 = screen_width - 1;
    if( x0 >= x1 )
        return;

    int adjust = x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    int texture_shift = (texture_width == 128) ? 7 : 6;
    int v_mask = (texture_width == 128) ? 0x3F80 : 0x0FC0;
    int u_mask = texture_width - 1;

    if( (cw >> texture_shift) == 0 )
        return;

    int steps = x1 - x0;
    int offset = pixel_offset + x0;
    shade8bit_ish8 += step_shade8bit_dx_ish8 * x0;

    int blocks = steps >> 3;
    int remaining = steps & 7;

    while( blocks-- > 0 )
    {
        int w0 = cw >> texture_shift;
        int w1 = (cw + (step_cw_dx << 3)) >> texture_shift;
        int cur_u = p4_persp_coord(au, w0);
        int cur_v = p4_persp_coord(bv, w0);
        int nxt_u = p4_persp_coord(au + (step_au_dx << 3), w1 != 0 ? w1 : 1);
        int nxt_v = p4_persp_coord(bv + (step_bv_dx << 3), w1 != 0 ? w1 : 1);
        int s_u = (nxt_u - cur_u) << (texture_shift - 3);
        int s_v = (nxt_v - cur_v) << (texture_shift - 3);

        raster_linear_opaque_blend_lerp8_vpentium4(
            (uint32_t*)&pixel_buffer[offset],
            (uint32_t*)texels,
            cur_u << texture_shift,
            cur_v << texture_shift,
            s_u,
            s_v,
            texture_shift,
            u_mask,
            v_mask,
            shade8bit_ish8 >> 8);

        au += (step_au_dx << 3);
        bv += (step_bv_dx << 3);
        cw += (step_cw_dx << 3);
        offset += 8;
        shade8bit_ish8 += (step_shade8bit_dx_ish8 << 3);
    }

    if( remaining > 0 )
    {
        int w0 = cw >> texture_shift;
        int w1 = (cw + (step_cw_dx << 3)) >> texture_shift;
        int cur_u = p4_persp_coord(au, w0);
        int cur_v = p4_persp_coord(bv, w0);
        int nxt_u = p4_persp_coord(au + (step_au_dx << 3), w1 != 0 ? w1 : 1);
        int nxt_v = p4_persp_coord(bv + (step_bv_dx << 3), w1 != 0 ? w1 : 1);
        int s_u = (nxt_u - cur_u) << (texture_shift - 3);
        int s_v = (nxt_v - cur_v) << (texture_shift - 3);
        int u_scan = cur_u << texture_shift;
        int v_scan = cur_v << texture_shift;
        int shade = shade8bit_ish8 >> 8;

        for( int i = 0; i < remaining; i++ )
        {
            int u = (u_scan >> texture_shift) & u_mask;
            int v = v_scan & v_mask;
            pixel_buffer[offset++] = shade_blend(texels[u + v], shade);
            u_scan += s_u;
            v_scan += s_v;
        }
    }
}

#endif /* TEX_VPENTIUM4_SPAN_SSE2_U_C */
