#ifndef TORIDRAW_RASTER_SPAN_CENSUS_H
#define TORIDRAW_RASTER_SPAN_CENSUS_H

/**
 * How long is a span, actually?
 *
 * Every question about hand-vectorizing a scanline fill is really a question
 * about this distribution and nothing else. A 16-byte store kernel that needs
 * six instructions of setup is a win over a 40-pixel span and a loss over a
 * 5-pixel one, and "models average 19 faces so spans are short" is a guess
 * about triangle COUNT, not about how many pixels each one covers -- a single
 * wall or floor tile can be half the viewport.
 *
 * Two gates, both compile-time and both off in a shipping build. Compile-time
 * rather than env-gated because this sits in the innermost loop of the
 * rasterizer, and a runtime branch there would tax the measurement it exists
 * to take.
 *
 *   -DTORIDRAW_SPAN_CENSUS=1  the length/alignment histogram, dumped at exit.
 *                             TORIDRAW_SPAN_CENSUS_FILE=<path> redirects it.
 *
 *   -DTORIDRAW_SPAN_TRACE=1   every Nth span's full argument tuple, dumped as
 *                             a binary file the span bench replays. This is
 *                             what makes a kernel A/B a measurement instead of
 *                             an argument: frame times on this suite vary ~2x
 *                             run to run (docs/PERF_HARNESS.md), which is an
 *                             order of magnitude wider than any span kernel's
 *                             whole contribution, so no number taken through a
 *                             live frame can score one.
 *                             TORIDRAW_SPAN_TRACE_FILE=<path> to write it.
 *
 *   -DTORIDRAW_TEXSPAN_CENSUS=1  the perspective texture span's block mix --
 *                             how many eight-pixel blocks a span actually runs
 *                             and how many of them take the per-pixel fallback.
 *                             A different question; see that section below.
 */

#if defined(TORIDRAW_SPAN_CENSUS) && TORIDRAW_SPAN_CENSUS

#include <stdio.h>
#include <stdlib.h>

/** Buckets are exact for 0..63, then power-of-two up to >= 4096. */
#define TORIDRAW_SPAN_CENSUS_EXACT 64
#define TORIDRAW_SPAN_CENSUS_BUCKETS (TORIDRAW_SPAN_CENSUS_EXACT + 8)

struct ToriDraw_SpanCensus
{
    /** Spans of each length, indexed by toridraw_span_census_bucket. */
    unsigned int count[TORIDRAW_SPAN_CENSUS_BUCKETS];
    /** Spans, and pixels, split by whether the first pixel is 16-byte aligned
     *  -- what decides between movdqa and movdqu for a vector store. */
    unsigned int spans_aligned16;
    unsigned int spans_total;
    double pixels_total;
    /** Spans that took the clamped path rather than the proven-inside one. */
    unsigned int spans_clipped;
};

extern struct ToriDraw_SpanCensus g_toridraw_span_census;

static inline int
toridraw_span_census_bucket(int len)
{
    int bucket = TORIDRAW_SPAN_CENSUS_EXACT;
    int edge = TORIDRAW_SPAN_CENSUS_EXACT;

    if( len < 0 )
        len = 0;
    if( len < TORIDRAW_SPAN_CENSUS_EXACT )
        return len;
    while( bucket < TORIDRAW_SPAN_CENSUS_BUCKETS - 1 && len >= edge * 2 )
    {
        bucket++;
        edge *= 2;
    }
    return bucket;
}

void
ToriDraw_SpanCensusDump(void);

/**
 * `first` is the pixel index of the span's left end within the buffer, which
 * is what the alignment question is about -- not the x coordinate.
 */
static inline void
toridraw_span_census_record(int len, int first, int clipped)
{
    if( len <= 0 )
        return;
    if( g_toridraw_span_census.spans_total == 0 )
        atexit(ToriDraw_SpanCensusDump);
    g_toridraw_span_census.count[toridraw_span_census_bucket(len)]++;
    g_toridraw_span_census.spans_total++;
    g_toridraw_span_census.pixels_total += (double)len;
    if( ((unsigned)first & 3u) == 0u )
        g_toridraw_span_census.spans_aligned16++;
    if( clipped )
        g_toridraw_span_census.spans_clipped++;
}

#define TORIDRAW_SPAN_CENSUS_RECORD_HIST(len, first, clipped)                             \
    toridraw_span_census_record((len), (first), (clipped))

#else

#define TORIDRAW_SPAN_CENSUS_RECORD_HIST(len, first, clipped) ((void)0)

#endif

#if defined(TORIDRAW_SPAN_TRACE) && TORIDRAW_SPAN_TRACE

#include <assert.h>
#include <stdlib.h>

/**
 * One span, as the fill sees it after the prologue has clamped and stepped.
 *
 * `align` is the left end's dword index mod 4 rather than the offset itself:
 * replaying into the bench's own buffer has to reproduce where the span sits
 * inside a 16-byte line, and nothing else about the address matters.
 */
struct ToriDraw_SpanTraceRec
{
    int stride;
    int color_hsl16_ish8;
    int color_step_hsl16_ish8;
    short align;
    short clipped;
};

/** Every Nth span is kept. 300 frames of a bench scene is ~10^7 spans and the
 *  distribution is what is wanted, not the individual frames. */
#define TORIDRAW_SPAN_TRACE_STRIDE 8
#define TORIDRAW_SPAN_TRACE_MAX (1 << 21)

extern struct ToriDraw_SpanTraceRec* g_toridraw_span_trace;
extern unsigned int g_toridraw_span_trace_count;
extern unsigned int g_toridraw_span_trace_seen;

void
ToriDraw_SpanTraceDump(void);

static inline void
toridraw_span_trace_record(int len, int first, int clipped, int color, int step)
{
    struct ToriDraw_SpanTraceRec* rec;

    if( len <= 0 )
        return;
    if( (g_toridraw_span_trace_seen++ % TORIDRAW_SPAN_TRACE_STRIDE) != 0 )
        return;
    if( g_toridraw_span_trace_count >= TORIDRAW_SPAN_TRACE_MAX )
        return;
    if( !g_toridraw_span_trace )
    {
        g_toridraw_span_trace =
            malloc(TORIDRAW_SPAN_TRACE_MAX * sizeof(*g_toridraw_span_trace));
        assert(g_toridraw_span_trace);
        atexit(ToriDraw_SpanTraceDump);
    }

    rec = &g_toridraw_span_trace[g_toridraw_span_trace_count++];
    rec->stride = len;
    rec->color_hsl16_ish8 = color;
    rec->color_step_hsl16_ish8 = step;
    rec->align = (short)((unsigned)first & 3u);
    rec->clipped = (short)clipped;
}

#define TORIDRAW_SPAN_TRACE_RECORD(len, first, clipped, color, step)                      \
    toridraw_span_trace_record((len), (first), (clipped), (color), (step))

#else

#define TORIDRAW_SPAN_TRACE_RECORD(len, first, clipped, color, step) ((void)0)

#endif

#if defined(TORIDRAW_TEXSPAN_CENSUS) && TORIDRAW_TEXSPAN_CENSUS

#include <stdio.h>
#include <stdlib.h>

/**
 * The texture span's own census, which asks a different question than the
 * gouraud one above.
 *
 * A perspective texture span is drawn as `steps >> 3` eight-pixel blocks plus a
 * `steps & 7` scalar tail, and each block goes one of two ways: the SSE2 linear
 * kernel, or tex_span_exact_block -- a divide and an indirect fetch PER PIXEL,
 * documented as cold. Whether that is still true decides where the time is:
 *
 *   - many blocks per span, nearly all vector  -> the inner kernel is the cost
 *   - few blocks per span                       -> the per-span prologue is
 *   - exact blocks not rare                     -> the fallback is
 *
 * Only the first of those three is a case a wider kernel can help, so this has
 * to be counted before any kernel is written.
 */
struct ToriDraw_TexSpanCensus
{
    unsigned int spans;
    double pixels;
    /** Eight-pixel blocks that took the SSE2 linear-fit kernel. */
    double blocks_lerp8;
    /** Blocks that fell back to the per-pixel exact path, and their pixels. */
    double blocks_exact;
    double pixels_exact;
    /** Pixels drawn by the `steps & 7` scalar tail. */
    double pixels_tail;
    /** Spans that never ran a single eight-pixel block. */
    unsigned int spans_no_block;
};

extern struct ToriDraw_TexSpanCensus g_toridraw_texspan_census;

void
ToriDraw_TexSpanCensusDump(void);

static inline void
toridraw_texspan_census_span(int steps)
{
    if( g_toridraw_texspan_census.spans == 0 )
        atexit(ToriDraw_TexSpanCensusDump);
    g_toridraw_texspan_census.spans++;
    g_toridraw_texspan_census.pixels += (double)steps;
    if( (steps >> 3) == 0 )
        g_toridraw_texspan_census.spans_no_block++;
}

/**
 * The tail is only counted where it draws linearly. A tail that falls back
 * reports through _EXACT instead, so no pixel lands in two buckets and the
 * three shares can be read against each other.
 */
static inline void
toridraw_texspan_census_tail(int count)
{
    g_toridraw_texspan_census.pixels_tail += (double)count;
}

static inline void
toridraw_texspan_census_lerp8(void)
{
    g_toridraw_texspan_census.blocks_lerp8 += 1.0;
}

static inline void
toridraw_texspan_census_exact(int count)
{
    g_toridraw_texspan_census.blocks_exact += 1.0;
    g_toridraw_texspan_census.pixels_exact += (double)count;
}

#define TORIDRAW_TEXSPAN_CENSUS_SPAN(steps) toridraw_texspan_census_span((steps))
#define TORIDRAW_TEXSPAN_CENSUS_LERP8() toridraw_texspan_census_lerp8()
#define TORIDRAW_TEXSPAN_CENSUS_EXACT(count) toridraw_texspan_census_exact((count))
#define TORIDRAW_TEXSPAN_CENSUS_TAIL(count) toridraw_texspan_census_tail((count))

#else

#define TORIDRAW_TEXSPAN_CENSUS_SPAN(steps) ((void)0)
#define TORIDRAW_TEXSPAN_CENSUS_LERP8() ((void)0)
#define TORIDRAW_TEXSPAN_CENSUS_EXACT(count) ((void)0)
#define TORIDRAW_TEXSPAN_CENSUS_TAIL(count) ((void)0)

#endif

/** The one call site macro: whichever of the two probes is compiled in. */
#define TORIDRAW_SPAN_CENSUS_RECORD(len, first, clipped, color, step)                     \
    do                                                                                    \
    {                                                                                     \
        TORIDRAW_SPAN_CENSUS_RECORD_HIST((len), (first), (clipped));                       \
        TORIDRAW_SPAN_TRACE_RECORD((len), (first), (clipped), (color), (step));            \
    } while( 0 )

#endif
