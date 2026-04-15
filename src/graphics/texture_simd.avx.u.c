/* AVX2 intrinsics (requires __AVX2__); filename kept as texture_simd.avx for consistency with
 * project naming. */
#include <assert.h>
#include <immintrin.h>

// shade_blend for 8 pixels at a time using AVX2
static inline __m256i
shade_blend8_avx2(
    __m256i texel,
    int shade)
{
    // Expand 8-bit channels to 16-bit (similar to NEON vmovl_u8)
    __m256i texel_lo = _mm256_unpacklo_epi8(texel, _mm256_setzero_si256());
    __m256i texel_hi = _mm256_unpackhi_epi8(texel, _mm256_setzero_si256());

    // Multiply by shade (similar to NEON vmulq_n_u16)
    __m256i shade_16 = _mm256_set1_epi16(shade);
    texel_lo = _mm256_mullo_epi16(texel_lo, shade_16);
    texel_hi = _mm256_mullo_epi16(texel_hi, shade_16);

    // >> 8 (same as scalar shade_blend and NEON vshrq_n_u16)
    texel_lo = _mm256_srli_epi16(texel_lo, 8);
    texel_hi = _mm256_srli_epi16(texel_hi, 8);

    // Pack back to 8-bit (similar to NEON vqmovn_u16)
    __m256i shaded = _mm256_packus_epi16(texel_lo, texel_hi);

    return shaded;
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
    int vm = texture_shift == 7 ? 0x3f80 : 0x0fc0;
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & vm;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    // Load 8 texels (scalar gather)
    __m256i t = _mm256_set_epi32(
        texels[idx[7]],
        texels[idx[6]],
        texels[idx[5]],
        texels[idx[4]],
        texels[idx[3]],
        texels[idx[2]],
        texels[idx[1]],
        texels[idx[0]]);

    // Shade blend in SIMD
    __m256i r = shade_blend8_avx2(t, shade);

    // Handle transparency: preserve existing pixel buffer where texel is 0
    __m256i zero = _mm256_setzero_si256();
    __m256i existing = _mm256_loadu_si256((__m256i*)&pixel_buffer[offset]);

    // Create mask for non-zero texels (true where texel is 0)
    __m256i mask = _mm256_cmpeq_epi32(t, zero);

    // Select existing pixels where texel is 0, shaded result where texel is not 0
    r = _mm256_blendv_epi8(r, existing, mask);

    // Store results
    _mm256_storeu_si256((__m256i*)&pixel_buffer[offset], r);
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
    int vm = texture_shift == 7 ? 0x3f80 : 0x0fc0;
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & vm;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    // Load 8 texels (scalar gather)
    __m256i t = _mm256_set_epi32(
        texels[idx[7]],
        texels[idx[6]],
        texels[idx[5]],
        texels[idx[4]],
        texels[idx[3]],
        texels[idx[2]],
        texels[idx[1]],
        texels[idx[0]]);

    // Shade blend in SIMD
    __m256i r = shade_blend8_avx2(t, shade);

    // Store results directly (no transparency masking for opaque rendering)
    _mm256_storeu_si256((__m256i*)&pixel_buffer[offset], r);
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

    __m256i t = _mm256_set_epi32(
        texels[idx[7]],
        texels[idx[6]],
        texels[idx[5]],
        texels[idx[4]],
        texels[idx[3]],
        texels[idx[2]],
        texels[idx[1]],
        texels[idx[0]]);

    __m256i r = shade_blend8_avx2(t, shade);
    _mm256_storeu_si256((__m256i*)pixel_buffer, r);
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

    __m256i t = _mm256_set_epi32(
        texels[idx[7]],
        texels[idx[6]],
        texels[idx[5]],
        texels[idx[4]],
        texels[idx[3]],
        texels[idx[2]],
        texels[idx[1]],
        texels[idx[0]]);

    __m256i r = shade_blend8_avx2(t, shade);
    __m256i zero = _mm256_setzero_si256();
    __m256i existing = _mm256_loadu_si256((__m256i*)pixel_buffer);
    __m256i texel_eq0 = _mm256_cmpeq_epi32(t, zero);
    r = _mm256_blendv_epi8(r, existing, texel_eq0);
    _mm256_storeu_si256((__m256i*)pixel_buffer, r);
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
            cur_u = clamp(cur_u, 0, texture_width - 1);
            nxt_u = clamp(nxt_u, 0, texture_width - 1);
            s_u = (nxt_u - cur_u) << (texture_shift - 3);
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
        cur_u = clamp(cur_u, 0, texture_width - 1);
        nxt_u = clamp(nxt_u, 0, texture_width - 1);
        s_u = (nxt_u - cur_u) << (texture_shift - 3);
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
