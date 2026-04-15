#include <arm_neon.h>

#include <assert.h>

// shade_blend for 4 pixels at a time
static inline uint32x4_t
shade_blend4_neon(
    uint32x4_t texel,
    int shade)
{
    // Expand 8-bit channels to 16-bit
    uint8x16_t texel_u8 = vreinterpretq_u8_u32(texel);
    uint16x8_t lo = vmovl_u8(vget_low_u8(texel_u8));
    uint16x8_t hi = vmovl_u8(vget_high_u8(texel_u8));

    // Multiply by shade
    lo = vmulq_n_u16(lo, shade);
    hi = vmulq_n_u16(hi, shade);

    // >> 8 (same as scalar shade_blend)
    lo = vshrq_n_u16(lo, 8);
    hi = vshrq_n_u16(hi, 8);

    // Narrow back to u8
    uint8x16_t shaded_u8 = vcombine_u8(vqmovn_u16(lo), vqmovn_u16(hi));

    return vreinterpretq_u32_u8(shaded_u8);
}

static inline void
raster_linear_transparent_blend_lerp8(
    uint32_t* pixel_buffer,
    int offset,
    const uint32_t* texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade)
{
    int idx[8];
    assert(texture_shift == 7 || texture_shift == 6);
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    // Load 8 texels (scalar gather)
    uint32x4_t t0 = { texels[idx[0]], texels[idx[1]], texels[idx[2]], texels[idx[3]] };
    uint32x4_t t1 = { texels[idx[4]], texels[idx[5]], texels[idx[6]], texels[idx[7]] };

    // Shade blend in SIMD
    uint32x4_t r0 = shade_blend4_neon(t0, shade);
    uint32x4_t r1 = shade_blend4_neon(t1, shade);

    // Handle transparency: preserve existing pixel buffer where texel is 0
    uint32x4_t zero = vdupq_n_u32(0);
    uint32x4_t existing0 = vld1q_u32(&pixel_buffer[offset]);
    uint32x4_t existing1 = vld1q_u32(&pixel_buffer[offset + 4]);

    // Create masks for non-zero texels
    uint32x4_t mask0 = vceqq_u32(t0, zero); // true where texel is 0
    uint32x4_t mask1 = vceqq_u32(t1, zero); // true where texel is 0
    // Select existing pixels where texel is 0, shaded result where texel is not 0
    r0 = vbslq_u32(mask0, existing0, r0);
    r1 = vbslq_u32(mask1, existing1, r1);

    // Store results
    vst1q_u32(&pixel_buffer[offset], r0);
    vst1q_u32(&pixel_buffer[offset + 4], r1);
}

static inline void
raster_linear_opaque_blend_lerp8(
    uint32_t* pixel_buffer,
    int offset,
    const uint32_t* texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade)
{
    int idx[8];
    assert(texture_shift == 7 || texture_shift == 6);
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    // Load 8 texels (scalar gather)
    uint32x4_t t0 = { texels[idx[0]], texels[idx[1]], texels[idx[2]], texels[idx[3]] };
    uint32x4_t t1 = { texels[idx[4]], texels[idx[5]], texels[idx[6]], texels[idx[7]] };

    // Shade blend in SIMD
    uint32x4_t r0 = shade_blend4_neon(t0, shade);
    uint32x4_t r1 = shade_blend4_neon(t1, shade);

    // Store results directly (no transparency masking for opaque rendering)
    vst1q_u32(&pixel_buffer[offset], r0);
    vst1q_u32(&pixel_buffer[offset + 4], r1);
}

static inline void
raster_linear_opaque_blend_lerp8_v3(
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

// We mask U and V at every pixel to prevent texture bleeding/crashes
#define GET_TEXEL_ADDR &texels[((u_scan >> texture_shift) & u_mask) + (v_scan & v_mask)]

    t0 = vld1q_lane_u32(GET_TEXEL_ADDR, t0, 0);
    u_scan += step_u;
    v_scan += step_v;
    t0 = vld1q_lane_u32(GET_TEXEL_ADDR, t0, 1);
    u_scan += step_u;
    v_scan += step_v;
    t0 = vld1q_lane_u32(GET_TEXEL_ADDR, t0, 2);
    u_scan += step_u;
    v_scan += step_v;
    t0 = vld1q_lane_u32(GET_TEXEL_ADDR, t0, 3);
    u_scan += step_u;
    v_scan += step_v;

    t1 = vld1q_lane_u32(GET_TEXEL_ADDR, t1, 0);
    u_scan += step_u;
    v_scan += step_v;
    t1 = vld1q_lane_u32(GET_TEXEL_ADDR, t1, 1);
    u_scan += step_u;
    v_scan += step_v;
    t1 = vld1q_lane_u32(GET_TEXEL_ADDR, t1, 2);
    u_scan += step_u;
    v_scan += step_v;
    t1 = vld1q_lane_u32(GET_TEXEL_ADDR, t1, 3);

    uint32x4_t r0 = shade_blend4_neon(t0, shade);
    uint32x4_t r1 = shade_blend4_neon(t1, shade);

    vst1q_u32(pixel_buffer, r0);
    vst1q_u32(pixel_buffer + 4, r1);
#undef GET_TEXEL_ADDR
}

static inline void
raster_linear_transparent_blend_lerp8_v3(
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

#define GET_TEXEL_ADDR &texels[((u_scan >> texture_shift) & u_mask) + (v_scan & v_mask)]

    t0 = vld1q_lane_u32(GET_TEXEL_ADDR, t0, 0);
    u_scan += step_u;
    v_scan += step_v;
    t0 = vld1q_lane_u32(GET_TEXEL_ADDR, t0, 1);
    u_scan += step_u;
    v_scan += step_v;
    t0 = vld1q_lane_u32(GET_TEXEL_ADDR, t0, 2);
    u_scan += step_u;
    v_scan += step_v;
    t0 = vld1q_lane_u32(GET_TEXEL_ADDR, t0, 3);
    u_scan += step_u;
    v_scan += step_v;

    t1 = vld1q_lane_u32(GET_TEXEL_ADDR, t1, 0);
    u_scan += step_u;
    v_scan += step_v;
    t1 = vld1q_lane_u32(GET_TEXEL_ADDR, t1, 1);
    u_scan += step_u;
    v_scan += step_v;
    t1 = vld1q_lane_u32(GET_TEXEL_ADDR, t1, 2);
    u_scan += step_u;
    v_scan += step_v;
    t1 = vld1q_lane_u32(GET_TEXEL_ADDR, t1, 3);

    uint32x4_t r0 = shade_blend4_neon(t0, shade);
    uint32x4_t r1 = shade_blend4_neon(t1, shade);

    uint32x4_t zero = vdupq_n_u32(0);
    uint32x4_t existing0 = vld1q_u32(pixel_buffer);
    uint32x4_t existing1 = vld1q_u32(pixel_buffer + 4);
    uint32x4_t m0 = vceqq_u32(t0, zero);
    uint32x4_t m1 = vceqq_u32(t1, zero);
    r0 = vbslq_u32(m0, existing0, r0);
    r1 = vbslq_u32(m1, existing1, r1);

    vst1q_u32(pixel_buffer, r0);
    vst1q_u32(pixel_buffer + 4, r1);
#undef GET_TEXEL_ADDR
}

static inline void
draw_texture_scanline_opaque_blend_ordered_blerp8_v3(
    int* pixel_buffer,
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
    int* texels,
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

    int steps = x1 - x0;
    int offset = pixel_offset + x0;
    shade8bit_ish8 += step_shade8bit_dx_ish8 * x0;

    int blocks = steps >> 3;
    int remaining = steps & 7;

// Re-useable UV calculation logic
#define CALC_BLOCK_PARAMS(W_VAL, AU_VAL, BV_VAL)                                                   \
    float inv_w = 1.0f / (float)((W_VAL) >> texture_shift);                                        \
    int cur_u = (int)((AU_VAL) * inv_w);                                                           \
    int cur_v = (int)((BV_VAL) * inv_w);                                                           \
    float inv_w_n = 1.0f / (float)(((W_VAL) + (step_cw_dx << 3)) >> texture_shift);                \
    int nxt_u = (int)(((AU_VAL) + (step_au_dx << 3)) * inv_w_n);                                   \
    int nxt_v = (int)(((BV_VAL) + (step_bv_dx << 3)) * inv_w_n);                                   \
    int s_u = (nxt_u - cur_u) << (texture_shift - 3);                                              \
    int s_v = (nxt_v - cur_v) << (texture_shift - 3);

    while( blocks-- )
    {
        if( (cw >> texture_shift) != 0 )
        {
            CALC_BLOCK_PARAMS(cw, au, bv);
            raster_linear_opaque_blend_lerp8_v3(
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
        }
        au += (step_au_dx << 3);
        bv += (step_bv_dx << 3);
        cw += (step_cw_dx << 3);
        offset += 8;
        shade8bit_ish8 += (step_shade8bit_dx_ish8 << 3);
    }

    // --- FIX: Proper Tail Cleanup ---
    if( remaining > 0 && (cw >> texture_shift) != 0 )
    {
        CALC_BLOCK_PARAMS(cw, au, bv);
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
#undef CALC_BLOCK_PARAMS
}

static inline void
draw_texture_scanline_transparent_blend_ordered_blerp8_v3(
    int* pixel_buffer,
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
    int* texels,
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

    int steps = x1 - x0;
    int offset = pixel_offset + x0;
    shade8bit_ish8 += step_shade8bit_dx_ish8 * x0;

    int blocks = steps >> 3;
    int remaining = steps & 7;

#define CALC_BLOCK_PARAMS(W_VAL, AU_VAL, BV_VAL)                                                   \
    float inv_w = 1.0f / (float)((W_VAL) >> texture_shift);                                        \
    int cur_u = (int)((AU_VAL) * inv_w);                                                           \
    int cur_v = (int)((BV_VAL) * inv_w);                                                           \
    float inv_w_n = 1.0f / (float)(((W_VAL) + (step_cw_dx << 3)) >> texture_shift);                \
    int nxt_u = (int)(((AU_VAL) + (step_au_dx << 3)) * inv_w_n);                                   \
    int nxt_v = (int)(((BV_VAL) + (step_bv_dx << 3)) * inv_w_n);                                   \
    int s_u = (nxt_u - cur_u) << (texture_shift - 3);                                              \
    int s_v = (nxt_v - cur_v) << (texture_shift - 3);

    while( blocks-- )
    {
        if( (cw >> texture_shift) != 0 )
        {
            CALC_BLOCK_PARAMS(cw, au, bv);
            raster_linear_transparent_blend_lerp8_v3(
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
        }
        au += (step_au_dx << 3);
        bv += (step_bv_dx << 3);
        cw += (step_cw_dx << 3);
        offset += 8;
        shade8bit_ish8 += (step_shade8bit_dx_ish8 << 3);
    }

    if( remaining > 0 && (cw >> texture_shift) != 0 )
    {
        CALC_BLOCK_PARAMS(cw, au, bv);
        int u_scan = cur_u << texture_shift;
        int v_scan = cur_v << texture_shift;
        int shade = shade8bit_ish8 >> 8;

        for( int i = 0; i < remaining; i++ )
        {
            int u = (u_scan >> texture_shift) & u_mask;
            int v = v_scan & v_mask;
            int t = texels[u + v];
            if( t != 0 )
                pixel_buffer[offset] = shade_blend(t, shade);
            offset++;
            u_scan += s_u;
            v_scan += s_v;
        }
    }
#undef CALC_BLOCK_PARAMS
}

static inline void
draw_texture_scanline_opaque_blend_affine_ordered_ish16(
    int* pixel_buffer,
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
    int* texels,
    int texture_width)
{
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

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

    int offset = pixel_offset + x0;
    shade8bit_ish8 += step_shade8bit_dx_ish8 * x0;

    int width = x1 - x0;

    int texture_shift = (texture_width == 128) ? 7 : 6;
    int v_mask = (texture_width == 128) ? 0x3F80 : 0x0FC0;
    int u_mask = texture_width - 1;

    int w_div = cw >> texture_shift;
    int u_coord = 0;
    int v_coord = 0;
    if( w_div != 0 )
    {
        u_coord = au / w_div;
        v_coord = bv / w_div;
    }
    u_coord = clamp(u_coord, 0, texture_width - 1);

    int au_end = au + step_au_dx * width;
    int bv_end = bv + step_bv_dx * width;
    int cw_end = cw + step_cw_dx * width;
    int w_div_end = cw_end >> texture_shift;
    int u_coord_end = 0;
    int v_coord_end = 0;
    if( w_div_end != 0 )
    {
        u_coord_end = au_end / w_div_end;
        v_coord_end = bv_end / w_div_end;
    }
    u_coord_end = clamp(u_coord_end, 0, texture_width - 1);

    int u_scan = u_coord << texture_shift;
    int v_scan = v_coord << texture_shift;
    int step_u = ((u_coord_end - u_coord) << texture_shift) / width;
    int step_v = ((v_coord_end - v_coord) << texture_shift) / width;

    int blocks = width >> 3;
    int remaining = width & 7;

    while( blocks-- )
    {
        raster_linear_opaque_blend_lerp8_v3(
            (uint32_t*)&pixel_buffer[offset],
            (uint32_t*)texels,
            u_scan,
            v_scan,
            step_u,
            step_v,
            texture_shift,
            u_mask,
            v_mask,
            shade8bit_ish8 >> 8);
        u_scan += step_u * 8;
        v_scan += step_v * 8;
        offset += 8;
        shade8bit_ish8 += (step_shade8bit_dx_ish8 << 3);
    }

    if( remaining > 0 )
    {
        int shade = shade8bit_ish8 >> 8;
        for( int i = 0; i < remaining; i++ )
        {
            int u = (u_scan >> texture_shift) & u_mask;
            int v = v_scan & v_mask;
            pixel_buffer[offset++] = shade_blend(texels[u + v], shade);
            u_scan += step_u;
            v_scan += step_v;
        }
    }
}

static inline void
draw_texture_scanline_transparent_blend_affine_ordered_ish16(
    int* pixel_buffer,
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
    int* texels,
    int texture_width)
{
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

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

    int offset = pixel_offset + x0;
    shade8bit_ish8 += step_shade8bit_dx_ish8 * x0;

    int width = x1 - x0;

    int texture_shift = (texture_width == 128) ? 7 : 6;
    int v_mask = (texture_width == 128) ? 0x3F80 : 0x0FC0;
    int u_mask = texture_width - 1;

    int w_div = cw >> texture_shift;
    int u_coord = 0;
    int v_coord = 0;
    if( w_div != 0 )
    {
        u_coord = au / w_div;
        v_coord = bv / w_div;
    }
    u_coord = clamp(u_coord, 0, texture_width - 1);

    int au_end = au + step_au_dx * width;
    int bv_end = bv + step_bv_dx * width;
    int cw_end = cw + step_cw_dx * width;
    int w_div_end = cw_end >> texture_shift;
    int u_coord_end = 0;
    int v_coord_end = 0;
    if( w_div_end != 0 )
    {
        u_coord_end = au_end / w_div_end;
        v_coord_end = bv_end / w_div_end;
    }
    u_coord_end = clamp(u_coord_end, 0, texture_width - 1);

    int u_scan = u_coord << texture_shift;
    int v_scan = v_coord << texture_shift;
    int step_u = ((u_coord_end - u_coord) << texture_shift) / width;
    int step_v = ((v_coord_end - v_coord) << texture_shift) / width;

    int blocks = width >> 3;
    int remaining = width & 7;

    while( blocks-- )
    {
        raster_linear_transparent_blend_lerp8_v3(
            (uint32_t*)&pixel_buffer[offset],
            (uint32_t*)texels,
            u_scan,
            v_scan,
            step_u,
            step_v,
            texture_shift,
            u_mask,
            v_mask,
            shade8bit_ish8 >> 8);
        u_scan += step_u * 8;
        v_scan += step_v * 8;
        offset += 8;
        shade8bit_ish8 += (step_shade8bit_dx_ish8 << 3);
    }

    if( remaining > 0 )
    {
        int shade = shade8bit_ish8 >> 8;
        for( int i = 0; i < remaining; i++ )
        {
            int u = (u_scan >> texture_shift) & u_mask;
            int v = v_scan & v_mask;
            int t = texels[u + v];
            if( t != 0 )
                pixel_buffer[offset] = shade_blend(t, shade);
            offset++;
            u_scan += step_u;
            v_scan += step_v;
        }
    }
}
