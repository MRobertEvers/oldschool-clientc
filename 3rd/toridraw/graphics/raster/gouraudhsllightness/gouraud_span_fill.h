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
 * stores into one 16-byte store.
 *
 * WHERE THE TIME ACTUALLY WENT
 *
 * Two guesses were wrong, and a 2,097,152-span trace of lumbridge-ground said
 * so. Both are kept below, because a measured loss is worth more than an
 * untried idea.
 *
 *   `sse2` -- four dword stores merged into one movdqu. Measured 0.0% on x64
 *   and -0.2% on i686, and the disassembly says why: gcc had already turned
 *   the reference's own four stores into pshufd + movups. It was never four
 *   stores in the shipping binary.
 *
 *   `run` -- one palette load per run of equal indices, on the theory that a
 *   256 KB table read per block was the cost. Measured 58% SLOWER on x64,
 *   50% on i686. The premise fails on the real distribution: the mean span is
 *   9 pixels, so there are barely two blocks to amortize a run over, and the
 *   index test costs more than the load it skips. The load was never the
 *   problem either -- a triangle varies only in lightness, so its indices are
 *   consecutive and its slice of the table is a few cache lines.
 *
 * What the trace actually shows is that this is not a fill loop. 37.3% of
 * spans are ONE pixel, 59.5% are under four -- `steps` is zero and the block
 * loop never executes at all. For those spans the entire kernel is one palette
 * lookup and the tail `switch`, and that switch is an indirect jump split
 * 48 / 24 / 16 / 13 across its three cases. It is mispredicted most of the way
 * through a frame, and a P4 pays twenty pipeline stages each time.
 *
 * `edge` replaces it with three unconditional stores and two conditional
 * moves; `short` additionally lifts the sub-four-pixel span clear of the loop.
 * Measured -16.8% / -18.8% on x64 and -21.4% / -23.6% on i686, bit-exact.
 * Net of the harness's own floor, the kernel itself is about a third faster on
 * the XP lane.
 *
 * `short` is what the rasterizer calls. `ref` is the loop it replaced, kept
 * so a candidate has something to be compared against that is not "the
 * rasterizer, rebuilt".
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
 * The loop this replaced, verbatim, as the thing every candidate is scored
 * against. `toridraw_scanline_parity_test` pins the rasterizer against the
 * reference client; this pins the kernel against the loop that passed it.
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
 * Write the 1..3 pixel tail without a jump table.
 *
 * `count` is stride & 3 and is never zero here. Against the recorded trace it
 * is 1 / 2 / 3 in a 48 / 24 / 16 split, which is as close to unpredictable as
 * a three-way branch gets -- and the reference spells it as a switch, so the
 * compiler makes it an indirect jump. A P4 mispredicts that nearly every span
 * and pays its whole twenty-stage pipeline for it.
 *
 * Three stores and two conditional moves instead. The duplicates are
 * deliberate: a repeated store of the same dword to the same address is a
 * store-buffer hit, and it is unconditionally cheaper than being wrong about
 * where to jump. Nothing outside [0, count) is ever addressed.
 */
static inline void
toridraw_gouraud_span_tail(toripixel_t* RESTRICT dst, int count, int rgb_color)
{
    int const i1 = count > 1 ? 1 : 0;
    int const i2 = count > 2 ? 2 : 0;

    dst[0] = rgb_color;
    dst[i1] = rgb_color;
    dst[i2] = rgb_color;
}

/**
 * The reference schedule with that tail, and nothing else changed.
 *
 * This is the whole bet, and the trace is why. 59.5% of spans are one to three
 * pixels -- `steps` is zero, the block loop never runs, and the ONLY work is
 * one palette lookup and the switch. For those spans the reference is not a
 * fill loop at all; it is a mispredicted indirect branch with a store attached.
 * 37.3% are a single pixel.
 *
 * So there is nothing to gain in the block body, and the sse2 variant measuring
 * dead even with ref is that fact stated from the other side. The tail is where
 * the time is because the tail is where the spans are.
 */
static inline void
toridraw_gouraud_span_fill_edge(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int stride,
    int color_hsl16_ish8,
    int color_step_hsl16_ish8)
{
    toripixel_t* RESTRICT dst = pixel_buffer + offset;
    int steps = stride >> 2;
    int tail = stride & 3;
    int rgb_color;

    color_step_hsl16_ish8 = toridraw_wrap_shl(color_step_hsl16_ish8, 2);

    while( steps-- > 0 )
    {
        rgb_color = ToriDraw_Hsl16Ish8ToRgb(color_hsl16_ish8);

        dst[0] = rgb_color;
        dst[1] = rgb_color;
        dst[2] = rgb_color;
        dst[3] = rgb_color;
        dst += 4;

        color_hsl16_ish8 = toridraw_wrap_add(color_hsl16_ish8, color_step_hsl16_ish8);
    }

    if( tail )
        toridraw_gouraud_span_tail(
            dst, tail, ToriDraw_Hsl16Ish8ToRgb(color_hsl16_ish8));
}

/**
 * The same, plus the short span lifted clear of the block loop entirely.
 *
 * A stride under four takes zero trips through `while( steps-- > 0 )`, and the
 * test that decides so splits 59.5 / 40.5 -- barely more predictable than the
 * switch it just replaced. Hoisting it puts the majority case in a straight
 * line with no loop-carried anything: one lookup, three stores, return.
 *
 * Whether that beats `edge` is a question about this machine's branch
 * predictor and not one that can be answered by reading the code, which is why
 * both are here.
 */
static inline void
toridraw_gouraud_span_fill_short(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int stride,
    int color_hsl16_ish8,
    int color_step_hsl16_ish8)
{
    toripixel_t* RESTRICT dst = pixel_buffer + offset;
    int steps;
    int tail;
    int rgb_color;

    if( stride < 4 )
    {
        toridraw_gouraud_span_tail(
            dst, stride, ToriDraw_Hsl16Ish8ToRgb(color_hsl16_ish8));
        return;
    }

    steps = stride >> 2;
    tail = stride & 3;
    color_step_hsl16_ish8 = toridraw_wrap_shl(color_step_hsl16_ish8, 2);

    while( steps-- > 0 )
    {
        rgb_color = ToriDraw_Hsl16Ish8ToRgb(color_hsl16_ish8);

        dst[0] = rgb_color;
        dst[1] = rgb_color;
        dst[2] = rgb_color;
        dst[3] = rgb_color;
        dst += 4;

        color_hsl16_ish8 = toridraw_wrap_add(color_hsl16_ish8, color_step_hsl16_ish8);
    }

    if( tail )
        toridraw_gouraud_span_tail(
            dst, tail, ToriDraw_Hsl16Ish8ToRgb(color_hsl16_ish8));
}

/**
 * The literal reading of the Tier 3 plan: same one-lookup-per-block schedule,
 * four dword stores replaced by one unaligned 16-byte store.
 *
 * Kept as the record of a measured non-result. It ties `ref` to two decimal
 * places on both lanes, which is the strongest possible evidence that gcc was
 * already emitting this and that the four stores in the source were never four
 * stores in the binary.
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
