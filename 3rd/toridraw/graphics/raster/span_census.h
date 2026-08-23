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
 * Compile-time gated, not env-gated: this sits in the innermost loop of the
 * rasterizer, and a runtime branch there would tax the measurement it exists
 * to take. Build with -DTORIDRAW_SPAN_CENSUS=1 to get a census binary; the
 * shipping build does not contain any of it.
 *
 *   TORIDRAW_SPAN_CENSUS_FILE=<path> to redirect the dump (default stderr).
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

#define TORIDRAW_SPAN_CENSUS_RECORD(len, first, clipped)                                  \
    toridraw_span_census_record((len), (first), (clipped))

#else

#define TORIDRAW_SPAN_CENSUS_RECORD(len, first, clipped) ((void)0)

#endif

#endif
