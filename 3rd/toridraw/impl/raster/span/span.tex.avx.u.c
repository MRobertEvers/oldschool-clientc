/* AVX2 intrinsics (requires __AVX2__); see tex.span.u.c ISA dispatch. */
#include "graphics/dash_restrict.h"
#include "graphics/int_wrap.h"
#include "graphics/shade.h"

#include <assert.h>
#include <immintrin.h>
#include <stdint.h>

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
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    const uint32_t* RESTRICT texels,
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
raster_linear_transparent_texshadeflat_lerp8(
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    const uint32_t* RESTRICT texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade)
{
    raster_linear_transparent_blend_lerp8(
        pixel_buffer,
        offset,
        texels,
        u_scan,
        v_scan,
        step_u,
        step_v,
        texture_shift,
        shade);
}

static inline void
raster_linear_opaque_blend_lerp8(
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    const uint32_t* RESTRICT texels,
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
raster_linear_opaque_texshadeflat_lerp8(
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    const uint32_t* RESTRICT texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade)
{
    raster_linear_opaque_blend_lerp8(
        pixel_buffer,
        offset,
        texels,
        u_scan,
        v_scan,
        step_u,
        step_v,
        texture_shift,
        shade);
}

static inline void
draw_texture_scanline_opaque_blend_branching_lerp8_ordered(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
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
    (void)stride;
    (void)screen_height;
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    int steps, adjust;

    int offset = pixel_offset;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

    int screen_x0 = (screen_x0_ish16 - 1) >> 16;
    int screen_x1 = screen_x1_ish16 >> 16;

    if( screen_x0 < 0 )
        screen_x0 = 0;
    if( screen_x1 >= screen_width )
        screen_x1 = screen_width - 1;

    if( screen_x0 >= screen_x1 )
        return;

    adjust = screen_x0 - (screen_width >> 1);
    au += (step_au_dx)*adjust;
    bv += (step_bv_dx)*adjust;
    cw += (step_cw_dx)*adjust;

    /* Kept because the per-pixel fallback needs the unscaled gradient; the
     * block loop below overwrites the parameters with the per-block step. */
    int step_au_px = step_au_dx;
    int step_bv_px = step_bv_dx;
    int step_cw_px = step_cw_dx;

    step_au_dx *= 8;
    step_bv_dx *= 8;
    step_cw_dx *= 8;

    /* Modular: the reference client relies on int wraparound here, and an edge-on
     * triangle reaches the overflow. See graphics/int_wrap.h. */
    shade8bit_ish8 = toridraw_wrap_add(
        shade8bit_ish8, toridraw_wrap_mul(step_shade8bit_dx_ish8, screen_x0));

    steps = screen_x1 - screen_x0;

    offset += screen_x0;

    int texture_shift = (texture_width & 0x80) ? 7 : 6;
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;

    int curr_u;
    int curr_v;
    int next_u;
    int next_v;

    int lerp8_steps = steps >> 3;
    int lerp8_last_steps = steps & 0x7;
    int lerp8_shade_step = toridraw_wrap_shl(step_shade8bit_dx_ish8, 3);
    int shade;

    /* Block k's end-of-block uv is block k+1's start-of-block uv, so carry it
     * forward rather than dividing for it twice (see the NEON twin).
     * have_cur == 0 means the carried pair is stale.
     *
     * This also repairs the block loop: the old `continue` on w == 0 skipped
     * the au/bv/cw advance and the `offset += 8` at the bottom, so one
     * degenerate block left every block after it writing 8 pixels to the left
     * of where it belonged - and if the first check tripped, cw never changed
     * and the whole span was dropped. The guard now covers only the draw. */
    int have_cur = 0;
    curr_u = 0;
    curr_v = 0;

    while( lerp8_steps-- > 0 )
    {
        int w = (cw) >> texture_shift;
        if( w != 0 )
        {
            if( !have_cur )
            {
                curr_u = clamp((au) / w, 0, texture_width - 1);
                curr_v = (bv) / w;
            }

            int w_n = (cw + step_cw_dx) >> texture_shift;
            if( w_n != 0 )
            {
                next_u = clamp((au + step_au_dx) / w_n, 0, texture_width - 1);
                next_v = (bv + step_bv_dx) / w_n;
            }
            else
            {
                next_u = curr_u;
                next_v = curr_v;
            }

            shade = shade8bit_ish8 >> 8;

            /* See tex.span_uv.h: past a tile per block the straight line is not
             * an approximation of the hyperbola, and w_n == 0 means there is no
             * far endpoint to fit to at all. */
            if( w_n != 0 && tex_span_lerp8_fits(curr_v, next_v, texture_width) )
            {
                int step_u = (next_u - curr_u) << (texture_shift - 3);
                int step_v = (next_v - curr_v) << (texture_shift - 3);

                raster_linear_opaque_blend_lerp8(
                    (uint32_t*)pixel_buffer,
                    offset,
                    (uint32_t*)texels,
                    curr_u << texture_shift,
                    tex_span_wrapped_scan_start(curr_v, texture_width, texture_shift),
                    step_u,
                    step_v,
                    texture_shift,
                    shade);
            }
            else
            {
                tex_span_exact_block(
                    pixel_buffer,
                    offset,
                    texels,
                    8,
                    au,
                    bv,
                    cw,
                    step_au_px,
                    step_bv_px,
                    step_cw_px,
                    shade,
                    texture_width,
                    texture_shift,
                    0);
            }

            curr_u = next_u;
            curr_v = next_v;
            have_cur = (w_n != 0);
        }
        else
        {
            have_cur = 0;
        }

        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;
        offset += 8;
        shade8bit_ish8 = toridraw_wrap_add(shade8bit_ish8, lerp8_shade_step);
    }

    if( lerp8_last_steps == 0 )
        return;

    int w = (cw) >> texture_shift;
    if( w == 0 )
        return;

    if( !have_cur )
    {
        curr_u = clamp((au) / w, 0, texture_width - 1);
        curr_v = (bv) / w;
    }

    int w_n = (cw + step_cw_dx) >> texture_shift;
    next_u = curr_u;
    next_v = curr_v;
    if( w_n != 0 )
    {
        next_u = clamp((au + step_au_dx) / w_n, 0, texture_width - 1);
        next_v = (bv + step_bv_dx) / w_n;
    }

    shade = shade8bit_ish8 >> 8;

    if( w_n == 0 || !tex_span_lerp8_fits(curr_v, next_v, texture_width) )
    {
        tex_span_exact_block(
            pixel_buffer,
            offset,
            texels,
            lerp8_last_steps,
            au,
            bv,
            cw,
            step_au_px,
            step_bv_px,
            step_cw_px,
            shade,
            texture_width,
            texture_shift,
            0);
        return;
    }

    int step_u = (next_u - curr_u) << (texture_shift - 3);
    int step_v = (next_v - curr_v) << (texture_shift - 3);

    int u_scan = curr_u << texture_shift;
    int v_scan = tex_span_wrapped_scan_start(curr_v, texture_width, texture_shift);

    for( int i = 0; i < lerp8_last_steps; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
        int texel = texels[u + v];
        pixel_buffer[offset] = shade_blend(texel, shade);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
}

static inline void
draw_texture_scanline_transparent_blend_branching_lerp8_ordered(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
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
    (void)stride;
    (void)screen_height;
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    int steps, adjust;

    int offset = pixel_offset;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

    int screen_x0 = (screen_x0_ish16 - 1) >> 16;
    int screen_x1 = screen_x1_ish16 >> 16;

    if( screen_x0 < 0 )
        screen_x0 = 0;
    if( screen_x1 >= screen_width )
        screen_x1 = screen_width - 1;

    if( screen_x0 >= screen_x1 )
        return;

    adjust = screen_x0 - (screen_width >> 1);
    au += (step_au_dx)*adjust;
    bv += (step_bv_dx)*adjust;
    cw += (step_cw_dx)*adjust;

    /* Kept because the per-pixel fallback needs the unscaled gradient; the
     * block loop below overwrites the parameters with the per-block step. */
    int step_au_px = step_au_dx;
    int step_bv_px = step_bv_dx;
    int step_cw_px = step_cw_dx;

    step_au_dx *= 8;
    step_bv_dx *= 8;
    step_cw_dx *= 8;

    shade8bit_ish8 = toridraw_wrap_add(
        shade8bit_ish8, toridraw_wrap_mul(step_shade8bit_dx_ish8, screen_x0));

    steps = screen_x1 - screen_x0;

    offset += screen_x0;

    int texture_shift = (texture_width & 0x80) ? 7 : 6;
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;

    int curr_u;
    int curr_v;
    int next_u;
    int next_v;

    int lerp8_steps = steps >> 3;
    int lerp8_last_steps = steps & 0x7;
    int lerp8_shade_step = toridraw_wrap_shl(step_shade8bit_dx_ish8, 3);
    int shade;

    /* Carry the block-end uv forward, and guard only the draw rather than the
     * whole loop body - see the opaque twin above for both. */
    int have_cur = 0;
    curr_u = 0;
    curr_v = 0;

    while( lerp8_steps-- > 0 )
    {
        int w = (cw) >> texture_shift;
        if( w != 0 )
        {
            if( !have_cur )
            {
                curr_u = clamp((au) / w, 0, texture_width - 1);
                curr_v = (bv) / w;
            }

            int w_n = (cw + step_cw_dx) >> texture_shift;
            if( w_n != 0 )
            {
                next_u = clamp((au + step_au_dx) / w_n, 0, texture_width - 1);
                next_v = (bv + step_bv_dx) / w_n;
            }
            else
            {
                next_u = curr_u;
                next_v = curr_v;
            }

            shade = shade8bit_ish8 >> 8;

            /* See tex.span_uv.h: past a tile per block the straight line is not
             * an approximation of the hyperbola, and w_n == 0 means there is no
             * far endpoint to fit to at all. */
            if( w_n != 0 && tex_span_lerp8_fits(curr_v, next_v, texture_width) )
            {
                int step_u = (next_u - curr_u) << (texture_shift - 3);
                int step_v = (next_v - curr_v) << (texture_shift - 3);

                raster_linear_transparent_blend_lerp8(
                    (uint32_t*)pixel_buffer,
                    offset,
                    (uint32_t*)texels,
                    curr_u << texture_shift,
                    tex_span_wrapped_scan_start(curr_v, texture_width, texture_shift),
                    step_u,
                    step_v,
                    texture_shift,
                    shade);
            }
            else
            {
                tex_span_exact_block(
                    pixel_buffer,
                    offset,
                    texels,
                    8,
                    au,
                    bv,
                    cw,
                    step_au_px,
                    step_bv_px,
                    step_cw_px,
                    shade,
                    texture_width,
                    texture_shift,
                    1);
            }

            curr_u = next_u;
            curr_v = next_v;
            have_cur = (w_n != 0);
        }
        else
        {
            have_cur = 0;
        }

        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;
        offset += 8;
        shade8bit_ish8 = toridraw_wrap_add(shade8bit_ish8, lerp8_shade_step);
    }

    if( lerp8_last_steps == 0 )
        return;

    int w = (cw) >> texture_shift;
    if( w == 0 )
        return;

    if( !have_cur )
    {
        curr_u = clamp((au) / w, 0, texture_width - 1);
        curr_v = (bv) / w;
    }

    int w_n = (cw + step_cw_dx) >> texture_shift;
    next_u = curr_u;
    next_v = curr_v;
    if( w_n != 0 )
    {
        next_u = clamp((au + step_au_dx) / w_n, 0, texture_width - 1);
        next_v = (bv + step_bv_dx) / w_n;
    }

    shade = shade8bit_ish8 >> 8;

    if( w_n == 0 || !tex_span_lerp8_fits(curr_v, next_v, texture_width) )
    {
        tex_span_exact_block(
            pixel_buffer,
            offset,
            texels,
            lerp8_last_steps,
            au,
            bv,
            cw,
            step_au_px,
            step_bv_px,
            step_cw_px,
            shade,
            texture_width,
            texture_shift,
            1);
        return;
    }

    int step_u = (next_u - curr_u) << (texture_shift - 3);
    int step_v = (next_v - curr_v) << (texture_shift - 3);

    int u_scan = curr_u << texture_shift;
    int v_scan = tex_span_wrapped_scan_start(curr_v, texture_width, texture_shift);

    for( int i = 0; i < lerp8_last_steps; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
        int texel = texels[u + v];
        if( texel != 0 )
            pixel_buffer[offset] = shade_blend(texel, shade);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
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
draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered(
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

    int steps = x1 - x0;
    int offset = pixel_offset + x0;
    shade8bit_ish8 = toridraw_wrap_add(
        shade8bit_ish8, toridraw_wrap_mul(step_shade8bit_dx_ish8, x0));

    int blocks = steps >> 3;
    int remaining = steps & 7;

    int step_au8 = step_au_dx << 3;
    int step_bv8 = step_bv_dx << 3;
    int step_cw8 = step_cw_dx << 3;

    /* Block k's end uv is block k+1's start uv - carry it forward instead of
     * dividing for it twice. This variant clamps u (unlike the NEON opaque
     * twin, which relies on u_mask), so the carried value is the CLAMPED one,
     * matching the endpoint this block actually used. */
    int cur_u = 0;
    int cur_v = 0;
    int have_cur = 0;

    while( blocks-- )
    {
        int w = cw >> texture_shift;
        if( w != 0 )
        {
            if( !have_cur )
            {
                float inv_w = 1.0f / (float)w;
                cur_u = tex_span_u_quotient(au, inv_w, texture_width);
                cur_v = tex_span_wrapped_quotient(bv, w, inv_w);
            }

            int w_n = (cw + step_cw8) >> texture_shift;
            int nxt_u;
            int nxt_v;
            if( w_n != 0 )
            {
                float inv_w_n = 1.0f / (float)w_n;
                nxt_u = tex_span_u_quotient(au + step_au8, inv_w_n, texture_width);
                nxt_v = tex_span_wrapped_quotient(bv + step_bv8, w_n, inv_w_n);
            }
            else
            {
                nxt_u = cur_u;
                nxt_v = cur_v;
            }

            /* w_n == 0 leaves nxt_* fabricated from cur_*, so the fit test sees
             * a flat gradient and believes it. The block has no far endpoint;
             * draw it per pixel rather than smearing one row across eight. */
            if( w_n != 0 && tex_span_lerp8_fits(cur_v, nxt_v, texture_width) )
            {
                int s_u = (nxt_u - cur_u) << (texture_shift - 3);
                int s_v = (nxt_v - cur_v) << (texture_shift - 3);

                raster_linear_opaque_blend_lerp8_v3(
                    (uint32_t*)&pixel_buffer[offset],
                    (uint32_t*)texels,
                    cur_u << texture_shift,
                    tex_span_wrapped_scan_start(cur_v, texture_width, texture_shift),
                    s_u,
                    s_v,
                    texture_shift,
                    u_mask,
                    v_mask,
                    shade8bit_ish8 >> 8);
            }
            else
            {
                tex_span_exact_block(
                    pixel_buffer,
                    offset,
                    texels,
                    8,
                    au,
                    bv,
                    cw,
                    step_au_dx,
                    step_bv_dx,
                    step_cw_dx,
                    shade8bit_ish8 >> 8,
                    texture_width,
                    texture_shift,
                    0);
            }

            cur_u = nxt_u;
            cur_v = nxt_v;
            have_cur = (w_n != 0);
        }
        else
        {
            have_cur = 0;
        }
        au += step_au8;
        bv += step_bv8;
        cw += step_cw8;
        offset += 8;
        shade8bit_ish8 =
            toridraw_wrap_add(shade8bit_ish8, toridraw_wrap_shl(step_shade8bit_dx_ish8, 3));
    }

    if( remaining > 0 && (cw >> texture_shift) != 0 )
    {
        int w = cw >> texture_shift;
        if( !have_cur )
        {
            float inv_w = 1.0f / (float)w;
            cur_u = tex_span_u_quotient(au, inv_w, texture_width);
            cur_v = tex_span_wrapped_quotient(bv, w, inv_w);
        }

        int w_n = (cw + step_cw8) >> texture_shift;
        int nxt_u = cur_u;
        int nxt_v = cur_v;
        if( w_n != 0 )
        {
            float inv_w_n = 1.0f / (float)w_n;
            nxt_u = tex_span_u_quotient(au + step_au8, inv_w_n, texture_width);
            nxt_v = tex_span_wrapped_quotient(bv + step_bv8, w_n, inv_w_n);
        }

        int shade = shade8bit_ish8 >> 8;

        if( w_n == 0 || !tex_span_lerp8_fits(cur_v, nxt_v, texture_width) )
        {
            tex_span_exact_block(
                pixel_buffer,
                offset,
                texels,
                remaining,
                au,
                bv,
                cw,
                step_au_dx,
                step_bv_dx,
                step_cw_dx,
                shade,
                texture_width,
                texture_shift,
                0);
            return;
        }

        int s_u = (nxt_u - cur_u) << (texture_shift - 3);
        int s_v = (nxt_v - cur_v) << (texture_shift - 3);

        int u_scan = cur_u << texture_shift;
        int v_scan = tex_span_wrapped_scan_start(cur_v, texture_width, texture_shift);

        for( int i = 0; i < remaining; i++ )
        {
            int u = (u_scan >> texture_shift) & u_mask;
            int v = v_scan & v_mask;
            pixel_buffer[offset] = shade_blend(texels[u + v], shade);
            offset++;
            u_scan += s_u;
            v_scan += s_v;
        }
    }
}

static inline void
draw_texture_scanline_transparent_blend_branching_lerp8_v3_ordered(
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

    int steps = x1 - x0;
    int offset = pixel_offset + x0;
    shade8bit_ish8 = toridraw_wrap_add(
        shade8bit_ish8, toridraw_wrap_mul(step_shade8bit_dx_ish8, x0));

    int blocks = steps >> 3;
    int remaining = steps & 7;

    int step_au8 = step_au_dx << 3;
    int step_bv8 = step_bv_dx << 3;
    int step_cw8 = step_cw_dx << 3;

    /* As in the opaque twin: block k's end uv is block k+1's start uv. The
     * carried u is the CLAMPED one, matching what this block actually used as
     * its span endpoint. */
    int cur_u = 0;
    int cur_v = 0;
    int have_cur = 0;

    while( blocks-- )
    {
        int w = cw >> texture_shift;
        if( w != 0 )
        {
            if( !have_cur )
            {
                float inv_w = 1.0f / (float)w;
                cur_u = tex_span_u_quotient(au, inv_w, texture_width);
                cur_v = tex_span_wrapped_quotient(bv, w, inv_w);
            }

            int w_n = (cw + step_cw8) >> texture_shift;
            int nxt_u;
            int nxt_v;
            if( w_n != 0 )
            {
                float inv_w_n = 1.0f / (float)w_n;
                nxt_u = tex_span_u_quotient(au + step_au8, inv_w_n, texture_width);
                nxt_v = tex_span_wrapped_quotient(bv + step_bv8, w_n, inv_w_n);
            }
            else
            {
                nxt_u = cur_u;
                nxt_v = cur_v;
            }

            /* w_n == 0 leaves nxt_* fabricated from cur_*, so the fit test sees
             * a flat gradient and believes it. The block has no far endpoint;
             * draw it per pixel rather than smearing one row across eight. */
            if( w_n != 0 && tex_span_lerp8_fits(cur_v, nxt_v, texture_width) )
            {
                int s_u = (nxt_u - cur_u) << (texture_shift - 3);
                int s_v = (nxt_v - cur_v) << (texture_shift - 3);

                raster_linear_transparent_blend_lerp8_v3(
                    (uint32_t*)&pixel_buffer[offset],
                    (uint32_t*)texels,
                    cur_u << texture_shift,
                    tex_span_wrapped_scan_start(cur_v, texture_width, texture_shift),
                    s_u,
                    s_v,
                    texture_shift,
                    u_mask,
                    v_mask,
                    shade8bit_ish8 >> 8);
            }
            else
            {
                tex_span_exact_block(
                    pixel_buffer,
                    offset,
                    texels,
                    8,
                    au,
                    bv,
                    cw,
                    step_au_dx,
                    step_bv_dx,
                    step_cw_dx,
                    shade8bit_ish8 >> 8,
                    texture_width,
                    texture_shift,
                    1);
            }

            cur_u = nxt_u;
            cur_v = nxt_v;
            have_cur = (w_n != 0);
        }
        else
        {
            have_cur = 0;
        }
        au += step_au8;
        bv += step_bv8;
        cw += step_cw8;
        offset += 8;
        shade8bit_ish8 =
            toridraw_wrap_add(shade8bit_ish8, toridraw_wrap_shl(step_shade8bit_dx_ish8, 3));
    }

    if( remaining > 0 && (cw >> texture_shift) != 0 )
    {
        int w = cw >> texture_shift;
        if( !have_cur )
        {
            float inv_w = 1.0f / (float)w;
            cur_u = tex_span_u_quotient(au, inv_w, texture_width);
            cur_v = tex_span_wrapped_quotient(bv, w, inv_w);
        }

        int w_n = (cw + step_cw8) >> texture_shift;
        int nxt_u = cur_u;
        int nxt_v = cur_v;
        if( w_n != 0 )
        {
            float inv_w_n = 1.0f / (float)w_n;
            nxt_u = tex_span_u_quotient(au + step_au8, inv_w_n, texture_width);
            nxt_v = tex_span_wrapped_quotient(bv + step_bv8, w_n, inv_w_n);
        }

        int shade = shade8bit_ish8 >> 8;

        if( w_n == 0 || !tex_span_lerp8_fits(cur_v, nxt_v, texture_width) )
        {
            tex_span_exact_block(
                pixel_buffer,
                offset,
                texels,
                remaining,
                au,
                bv,
                cw,
                step_au_dx,
                step_bv_dx,
                step_cw_dx,
                shade,
                texture_width,
                texture_shift,
                1);
            return;
        }

        int s_u = (nxt_u - cur_u) << (texture_shift - 3);
        int s_v = (nxt_v - cur_v) << (texture_shift - 3);

        int u_scan = cur_u << texture_shift;
        int v_scan = tex_span_wrapped_scan_start(cur_v, texture_width, texture_shift);

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
}

static inline void
draw_texture_scanline_opaque_blend_affine_branching_lerp8_ordered(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x_start,
    int x_end,
    int pixel_offset,
    int shade_ish8,
    int shade_step_ish8,
    int u,
    int v,
    int w,
    int step_u_dx,
    int step_v_dx,
    int step_w_dx,
    int* RESTRICT texels,
    int origin_x)
{
    if( x_end > screen_width )
    {
        x_end = screen_width;
    }
    if( x_start < 0 )
    {
        x_start = 0;
    }

    if( x_start >= x_end )
    {
        return;
    }

    int offset = pixel_offset + x_start;
    int shade_accum = x_start * shade_step_ish8 + shade_ish8;
    int width = x_end - x_start;
    int dx = x_start - origin_x;
    int u_start = step_u_dx * dx + u;
    int v_start = step_v_dx * dx + v;
    int w_start = step_w_dx * dx + w;

    int w_div = w_start >> 14;
    int u_coord = 0;
    int v_coord = 0;
    if( w_div == 0 )
    {
        u_coord = 0;
        v_coord = 0;
    }
    else
    {
        u_coord = u_start / w_div;
        v_coord = v_start / w_div;
    }

    int u_end = step_u_dx * width + u_start;
    int v_end = step_v_dx * width + v_start;
    int w_end = step_w_dx * width + w_start;

    int w_div_end = w_end >> 14;
    int u_coord_end = 0;
    int v_coord_end = 0;
    if( w_div_end == 0 )
    {
        u_coord_end = 0;
        v_coord_end = 0;
    }
    else
    {
        u_coord_end = u_end / w_div_end;
        v_coord_end = v_end / w_div_end;
    }

    int uv_packed = (u_coord << 18) + v_coord;
    int uv_step = ((u_coord_end - u_coord) / width << 18) + (v_coord_end - v_coord) / width;

    int steps_8 = width >> 3;
    int shade_step_8 = shade_step_ish8 << 3;
    int shade = shade_accum >> 8;

    if( steps_8 > 0 )
    {
        do
        {
            int texel = texels[((uint32_t)uv_packed >> 25) + (uv_packed & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            int uv_next = uv_packed + uv_step;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_packed = uv_step + uv_next;
            shade_accum += shade_step_8;
            shade = shade_accum >> 8;
            steps_8--;
        } while( steps_8 > 0 );
    }

    int remaining = (x_end - x_start) & 0x7;
    if( remaining > 0 )
    {
        do
        {
            int texel = texels[((uint32_t)uv_packed >> 25) + (uv_packed & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_packed += uv_step;
            remaining--;
        } while( remaining > 0 );
    }
}

static inline void
draw_texture_scanline_transparent_blend_affine_branching_lerp8_ordered(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x_start,
    int x_end,
    int pixel_offset,
    int shade_ish8,
    int shade_step_ish8,
    int u,
    int v,
    int w,
    int step_u_dx,
    int step_v_dx,
    int step_w_dx,
    int* RESTRICT texels,
    int origin_x)
{
    if( x_end > screen_width )
    {
        x_end = screen_width;
    }
    if( x_start < 0 )
    {
        x_start = 0;
    }

    if( x_start >= x_end )
    {
        return;
    }

    int offset = pixel_offset + x_start;
    int shade_accum = x_start * shade_step_ish8 + shade_ish8;
    int width = x_end - x_start;
    int dx = x_start - origin_x;
    int u_start = step_u_dx * dx + u;
    int v_start = step_v_dx * dx + v;
    int w_start = step_w_dx * dx + w;

    int w_div = w_start >> 14;
    int u_coord = 0;
    int v_coord = 0;
    if( w_div == 0 )
    {
        u_coord = 0;
        v_coord = 0;
    }
    else
    {
        u_coord = u_start / w_div;
        v_coord = v_start / w_div;
    }

    int u_end = step_u_dx * width + u_start;
    int v_end = step_v_dx * width + v_start;
    int w_end = step_w_dx * width + w_start;

    int w_div_end = w_end >> 14;
    int u_coord_end = 0;
    int v_coord_end = 0;
    if( w_div_end == 0 )
    {
        u_coord_end = 0;
        v_coord_end = 0;
    }
    else
    {
        u_coord_end = u_end / w_div_end;
        v_coord_end = v_end / w_div_end;
    }

    int uv_packed = (u_coord << 18) + v_coord;
    int uv_step = ((u_coord_end - u_coord) / width << 18) + (v_coord_end - v_coord) / width;

    int steps_8 = width >> 3;
    int shade_step_8 = shade_step_ish8 << 3;
    int shade = shade_accum >> 8;

    if( steps_8 > 0 )
    {
        do
        {
            int texel = texels[((uint32_t)uv_packed >> 25) + (uv_packed & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;
            int uv_next = uv_packed + uv_step;

            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_packed = uv_step + uv_next;
            shade_accum += shade_step_8;
            shade = shade_accum >> 8;
            steps_8--;
        } while( steps_8 > 0 );
    }

    int remaining = (x_end - x_start) & 0x7;
    if( remaining > 0 )
    {
        do
        {
            int texel = texels[((uint32_t)uv_packed >> 25) + (uv_packed & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;
            uv_packed += uv_step;
            remaining--;
        } while( remaining > 0 );
    }
}


static inline void
draw_texture_scanline_opaque_blend_affine_branching_lerp8_ish16_ordered(
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
    shade8bit_ish8 = toridraw_wrap_add(
        shade8bit_ish8, toridraw_wrap_mul(step_shade8bit_dx_ish8, x0));

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

    int au_end = toridraw_add_mul32(au, step_au_dx, width);
    int bv_end = toridraw_add_mul32(bv, step_bv_dx, width);
    int cw_end = toridraw_add_mul32(cw, step_cw_dx, width);
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
        shade8bit_ish8 =
            toridraw_wrap_add(shade8bit_ish8, toridraw_wrap_shl(step_shade8bit_dx_ish8, 3));
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
draw_texture_scanline_transparent_blend_affine_branching_lerp8_ish16_ordered(
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
    shade8bit_ish8 = toridraw_wrap_add(
        shade8bit_ish8, toridraw_wrap_mul(step_shade8bit_dx_ish8, x0));

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

    int au_end = toridraw_add_mul32(au, step_au_dx, width);
    int bv_end = toridraw_add_mul32(bv, step_bv_dx, width);
    int cw_end = toridraw_add_mul32(cw, step_cw_dx, width);
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
        shade8bit_ish8 =
            toridraw_wrap_add(shade8bit_ish8, toridraw_wrap_shl(step_shade8bit_dx_ish8, 3));
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
