#ifndef TEX_VPENTIUM4_SPAN_NEON_U_C
#define TEX_VPENTIUM4_SPAN_NEON_U_C

#include "graphics/dash_restrict.h"
#include "graphics/shade.h"
#include "graphics/shared_tables.h"
#include <arm_neon.h>

#include <assert.h>
#include <stdint.h>

static inline uint32x4_t
p4_shade_blend4_neon(
    uint32x4_t texel,
    int shade)
{
    uint8x16_t texel_u8 = vreinterpretq_u8_u32(texel);
    uint16x8_t lo = vmovl_u8(vget_low_u8(texel_u8));
    uint16x8_t hi = vmovl_u8(vget_high_u8(texel_u8));
    lo = vmulq_n_u16(lo, (uint16_t)shade);
    hi = vmulq_n_u16(hi, (uint16_t)shade);
    lo = vshrq_n_u16(lo, 8);
    hi = vshrq_n_u16(hi, 8);
    uint8x16_t shaded_u8 = vcombine_u8(vqmovn_u16(lo), vqmovn_u16(hi));
    return vreinterpretq_u32_u8(shaded_u8);
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
    uint32x4_t t0, t1;

#define P4_GET_TEXEL_ADDR &texels[((u_scan >> texture_shift) & u_mask) + (v_scan & v_mask)]

    assert(texture_shift == 7 || texture_shift == 6);
    t0 = vld1q_lane_u32(P4_GET_TEXEL_ADDR, t0, 0);
    u_scan += step_u;
    v_scan += step_v;
    t0 = vld1q_lane_u32(P4_GET_TEXEL_ADDR, t0, 1);
    u_scan += step_u;
    v_scan += step_v;
    t0 = vld1q_lane_u32(P4_GET_TEXEL_ADDR, t0, 2);
    u_scan += step_u;
    v_scan += step_v;
    t0 = vld1q_lane_u32(P4_GET_TEXEL_ADDR, t0, 3);
    u_scan += step_u;
    v_scan += step_v;

    t1 = vld1q_lane_u32(P4_GET_TEXEL_ADDR, t1, 0);
    u_scan += step_u;
    v_scan += step_v;
    t1 = vld1q_lane_u32(P4_GET_TEXEL_ADDR, t1, 1);
    u_scan += step_u;
    v_scan += step_v;
    t1 = vld1q_lane_u32(P4_GET_TEXEL_ADDR, t1, 2);
    u_scan += step_u;
    v_scan += step_v;
    t1 = vld1q_lane_u32(P4_GET_TEXEL_ADDR, t1, 3);

    uint32x4_t r0 = p4_shade_blend4_neon(t0, shade);
    uint32x4_t r1 = p4_shade_blend4_neon(t1, shade);

    vst1q_u32(pixel_buffer, r0);
    vst1q_u32(pixel_buffer + 4, r1);
#undef P4_GET_TEXEL_ADDR
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

#endif /* TEX_VPENTIUM4_SPAN_NEON_U_C */
