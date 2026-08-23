#ifndef TORIDRAW_GOURAUD_SPAN_FILL_H
#define TORIDRAW_GOURAUD_SPAN_FILL_H

/**
 * The gouraud span fill, after the prologue has clamped the ends and stepped
 * the colour accumulator to x_start. Every variant here draws the same pixels
 * -- bit-exact, checked by toridraw_span_bench in compare mode -- so the only
 * thing that separates them is speed.
 *
 * What the reference does, and why it constrains the rest:
 *
 *   The span is walked in blocks of FOUR pixels. One palette lookup is done
 *   per block and its result is stored to all four. The colour is therefore
 *   quantized to 4-pixel steps, and that quantization is VISIBLE OUTPUT, not
 *   an implementation detail -- toridraw_scanline_parity_test pins it against
 *   the reference client. A variant may not re-block to 8, may not shift the
 *   phase, and may not interpolate per pixel. The blocking is tied to
 *   x_start, so it also may not be aligned to the frame buffer.
 *
 * That kills the obvious vectorization before it starts. There is no per-pixel
 * interpolation left to put in a `paddd` -- the reference already amortizes
 * one lookup over four pixels -- so a 4-wide body can only merge four dword
 * stores into one 16-byte store. Against the measured distribution (46% of
 * spans are ONE pixel, 73% are four or fewer, and only a quarter start
 * 16-byte aligned) that is worth a fraction of a percent, and on P4 an
 * unaligned 16-byte store is not obviously cheaper than the four dwords it
 * replaces.
 *
 * The cost that IS worth attacking is the lookup. g_hsl16_to_rgb_table is
 * 65,536 entries -- 256 KB, sixteen times the P4's L1D -- and the reference
 * reads it once per block whether or not the colour changed. The accumulator
 * is 8.8 fixed point, so the palette index only moves when the accumulator
 * crosses a 256 boundary: at a small per-pixel step, dozens of consecutive
 * blocks resolve to the same index and the reference looks all of them up.
 *
 * `run` tests the INDEX instead (shift, compare) and looks the table up once
 * per run of equal indices, then fills the whole run at once. It cannot
 * assume the index sequence is monotone -- the accumulator wraps on purpose
 * here, an edge-on triangle reaches the overflow (graphics/int_wrap.h) -- so
 * it compares consecutive indices rather than the two endpoints.
 */

#include "graphics/int_wrap.h"
#include "graphics/tori_compat.h"
#include "graphics/dash_restrict.h"
#include "graphics/shared_tables.h"

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define TORIDRAW_GOURAUD_SPAN_HAS_SSE2 1
#endif

/** The palette index the reference resolves, clamp and all, without the load. */
static inline int
toridraw_gouraud_span_index(int color_hsl16_ish8)
{
    int hsl16 = color_hsl16_ish8 >> 8;

    if( (unsigned)hsl16 > 0xFFFFu )
        hsl16 = hsl16 < 0 ? 0 : 0xFFFF;
    return hsl16;
}

/**
 * The reference twin: today's shipping loop, verbatim, kept as its own
 * function so a candidate has something to be compared against that is not
 * "the rasterizer, rebuilt".
 */
static inline void
toridraw_gouraud_span_fill_ref(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int stride,
    int color_hsl16_ish8,
    int color_step_hsl16_ish8)
{
    int steps = stride >> 2;
    int rgb_color;

    color_step_hsl16_ish8 = toridraw_wrap_shl(color_step_hsl16_ish8, 2);

    while( steps-- > 0 )
    {
        rgb_color = ToriDraw_Hsl16Ish8ToRgb(color_hsl16_ish8);

        pixel_buffer[offset + 0] = rgb_color;
        pixel_buffer[offset + 1] = rgb_color;
        pixel_buffer[offset + 2] = rgb_color;
        pixel_buffer[offset + 3] = rgb_color;
        offset += 4;

        color_hsl16_ish8 = toridraw_wrap_add(color_hsl16_ish8, color_step_hsl16_ish8);
    }

    rgb_color = ToriDraw_Hsl16Ish8ToRgb(color_hsl16_ish8);
    switch( stride & 0x3 )
    {
    case 3:
        pixel_buffer[offset] = rgb_color;
        offset += 1;
        /* fallthrough */
    case 2:
        pixel_buffer[offset] = rgb_color;
        offset += 1;
        /* fallthrough */
    case 1:
        pixel_buffer[offset] = rgb_color;
    }
}

/**
 * Store `count` copies of one dword. `count` is a whole number of 4-pixel
 * blocks, so the vector body needs no per-pixel tail -- only an alignment
 * one, and only when the span did not start 16-byte aligned.
 */
static inline void
toridraw_gouraud_span_store(toripixel_t* RESTRICT dst, int count, int rgb_color)
{
#if defined(TORIDRAW_GOURAUD_SPAN_HAS_SSE2)
    __m128i const v = _mm_set1_epi32(rgb_color);

    while( count > 0 && (((uintptr_t)dst) & 15u) != 0 )
    {
        *dst++ = rgb_color;
        count--;
    }
    while( count >= 16 )
    {
        _mm_store_si128((__m128i*)(dst + 0), v);
        _mm_store_si128((__m128i*)(dst + 4), v);
        _mm_store_si128((__m128i*)(dst + 8), v);
        _mm_store_si128((__m128i*)(dst + 12), v);
        dst += 16;
        count -= 16;
    }
    while( count >= 4 )
    {
        _mm_store_si128((__m128i*)dst, v);
        dst += 4;
        count -= 4;
    }
#endif
    while( count-- > 0 )
        *dst++ = rgb_color;
}

/**
 * One palette load per run of equal indices instead of one per block.
 *
 * The inner test is `(acc >> 8)` clamped and compared -- three ALU ops against
 * a 256 KB table read. The accumulator advances exactly as many times as the
 * reference's does, so the colour the tail sees is the same one.
 */
static inline void
toridraw_gouraud_span_fill_run(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int stride,
    int color_hsl16_ish8,
    int color_step_hsl16_ish8)
{
    int steps = stride >> 2;
    int step4 = toridraw_wrap_shl(color_step_hsl16_ish8, 2);
    int rgb_color;

    while( steps > 0 )
    {
        int const index = toridraw_gouraud_span_index(color_hsl16_ish8);
        int blocks = 0;

        do
        {
            blocks++;
            steps--;
            color_hsl16_ish8 = toridraw_wrap_add(color_hsl16_ish8, step4);
        } while( steps > 0 && toridraw_gouraud_span_index(color_hsl16_ish8) == index );

        toridraw_gouraud_span_store(
            pixel_buffer + offset, blocks << 2, g_hsl16_to_rgb_table[index]);
        offset += blocks << 2;
    }

    rgb_color = ToriDraw_Hsl16Ish8ToRgb(color_hsl16_ish8);
    switch( stride & 0x3 )
    {
    case 3:
        pixel_buffer[offset] = rgb_color;
        offset += 1;
        /* fallthrough */
    case 2:
        pixel_buffer[offset] = rgb_color;
        offset += 1;
        /* fallthrough */
    case 1:
        pixel_buffer[offset] = rgb_color;
    }
}

/**
 * The literal reading of the Tier 3 plan: same one-lookup-per-block schedule,
 * four dword stores replaced by one unaligned 16-byte store. Kept because
 * "the obvious kernel is not worth it" is a claim that needs a number, not an
 * argument -- and because on a machine with cheap unaligned stores it may
 * still win where `run` cannot (a steep colour ramp, where every block really
 * is a different colour).
 */
static inline void
toridraw_gouraud_span_fill_sse2(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int stride,
    int color_hsl16_ish8,
    int color_step_hsl16_ish8)
{
#if defined(TORIDRAW_GOURAUD_SPAN_HAS_SSE2)
    int steps = stride >> 2;
    int rgb_color;

    color_step_hsl16_ish8 = toridraw_wrap_shl(color_step_hsl16_ish8, 2);

    while( steps-- > 0 )
    {
        rgb_color = ToriDraw_Hsl16Ish8ToRgb(color_hsl16_ish8);
        _mm_storeu_si128((__m128i*)(pixel_buffer + offset), _mm_set1_epi32(rgb_color));
        offset += 4;
        color_hsl16_ish8 = toridraw_wrap_add(color_hsl16_ish8, color_step_hsl16_ish8);
    }

    rgb_color = ToriDraw_Hsl16Ish8ToRgb(color_hsl16_ish8);
    switch( stride & 0x3 )
    {
    case 3:
        pixel_buffer[offset] = rgb_color;
        offset += 1;
        /* fallthrough */
    case 2:
        pixel_buffer[offset] = rgb_color;
        offset += 1;
        /* fallthrough */
    case 1:
        pixel_buffer[offset] = rgb_color;
    }
#else
    toridraw_gouraud_span_fill_ref(
        pixel_buffer, offset, stride, color_hsl16_ish8, color_step_hsl16_ish8);
#endif
}

#endif
