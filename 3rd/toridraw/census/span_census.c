#include "census/span_census.h"

#if defined(TORIDRAW_SPAN_TRACE) && TORIDRAW_SPAN_TRACE

#include <stdio.h>

struct ToriDraw_SpanTraceRec* g_toridraw_span_trace;
unsigned int g_toridraw_span_trace_count;
unsigned int g_toridraw_span_trace_seen;

/**
 * Raw records, no header: the bench sizes the array from the file length, so
 * a truncated write is a short trace rather than a misparsed one.
 */
void
ToriDraw_SpanTraceDump(void)
{
    char const* path = getenv("TORIDRAW_SPAN_TRACE_FILE");
    FILE* f;

    if( !path )
        path = "span_trace.bin";
    if( g_toridraw_span_trace_count == 0 )
        return;

    f = fopen(path, "wb");
    if( !f )
        return;
    fwrite(
        g_toridraw_span_trace,
        sizeof(*g_toridraw_span_trace),
        g_toridraw_span_trace_count,
        f);
    fclose(f);
    fprintf(
        stderr,
        "span-trace: %u of %u spans -> %s\n",
        g_toridraw_span_trace_count,
        g_toridraw_span_trace_seen,
        path);
}

#endif

#if defined(TORIDRAW_TEXSPAN_CENSUS) && TORIDRAW_TEXSPAN_CENSUS

struct ToriDraw_TexSpanCensus g_toridraw_texspan_census;

/**
 * Report the block mix as shares of pixels, not of blocks. A fallback that is
 * 1% of blocks but draws eight pixels each at a divide apiece is not 1% of the
 * cost, and the block share is the number that would say it was.
 */
void
ToriDraw_TexSpanCensusDump(void)
{
    struct ToriDraw_TexSpanCensus const* c = &g_toridraw_texspan_census;
    double blocks;

    if( c->spans == 0 )
        return;
    blocks = c->blocks_lerp8 + c->blocks_exact;

    fprintf(
        stderr,
        "texspan-census: %u spans, %.0f px, %.2f px/span\n"
        "  blocks/span %.2f  (%.1f%% of spans run none at all)\n"
        "  lerp8 blocks %.0f (%.2f%%)  exact blocks %.0f (%.2f%%)\n"
        "  pixels: lerp8 %.1f%%  exact %.1f%%  scalar tail %.1f%%  undrawn %.1f%%\n",
        c->spans,
        c->pixels,
        c->pixels / (double)c->spans,
        blocks / (double)c->spans,
        100.0 * (double)c->spans_no_block / (double)c->spans,
        c->blocks_lerp8,
        blocks > 0.0 ? 100.0 * c->blocks_lerp8 / blocks : 0.0,
        c->blocks_exact,
        blocks > 0.0 ? 100.0 * c->blocks_exact / blocks : 0.0,
        100.0 * (c->blocks_lerp8 * 8.0) / c->pixels,
        100.0 * c->pixels_exact / c->pixels,
        100.0 * c->pixels_tail / c->pixels,
        /* The residual is the blocks whose w was zero -- behind the eye, so
         * nothing was drawn for them at all. It is not a fourth kernel. */
        100.0 *
            (c->pixels - c->blocks_lerp8 * 8.0 - c->pixels_exact - c->pixels_tail) /
            c->pixels);
    fflush(stderr);
}

#endif

#if defined(TORIDRAW_SPAN_CENSUS) && TORIDRAW_SPAN_CENSUS

struct ToriDraw_SpanCensus g_toridraw_span_census;

/**
 * Print the distribution, as counts and as the two cumulative shares that
 * actually decide a kernel: what fraction of SPANS are at most this long, and
 * what fraction of PIXELS is drawn by spans at most this long. They answer
 * different questions -- the first says how often a fixed setup cost is paid,
 * the second says how much of the fill a vector body could ever reach -- and
 * quoting one for the other is how "short spans" becomes a false conclusion.
 */
void
ToriDraw_SpanCensusDump(void)
{
    struct ToriDraw_SpanCensus const* c = &g_toridraw_span_census;
    char const* path = getenv("TORIDRAW_SPAN_CENSUS_FILE");
    FILE* out = stderr;
    double span_run = 0.0;
    double pixel_run = 0.0;
    int edge = TORIDRAW_SPAN_CENSUS_EXACT;

    if( c->spans_total == 0 )
        return;
    if( path )
    {
        FILE* f = fopen(path, "wb");
        if( f )
            out = f;
    }

    fprintf(
        out,
        "span-census: %u spans, %.0f pixels, %.2f px/span, %.1f%% 16B-aligned, "
        "%.1f%% clipped\n",
        c->spans_total,
        c->pixels_total,
        c->pixels_total / (double)c->spans_total,
        100.0 * (double)c->spans_aligned16 / (double)c->spans_total,
        100.0 * (double)c->spans_clipped / (double)c->spans_total);
    fprintf(out, "len,spans,span_cum_pct,pixel_cum_pct\n");
    for( int i = 0; i < TORIDRAW_SPAN_CENSUS_BUCKETS; i++ )
    {
        double len;
        if( i < TORIDRAW_SPAN_CENSUS_EXACT )
            len = (double)i;
        else
        {
            len = (double)edge * 1.5; /* bucket midpoint, [edge, 2*edge) */
            edge *= 2;
        }
        if( c->count[i] == 0 )
            continue;
        span_run += (double)c->count[i];
        pixel_run += (double)c->count[i] * len;
        fprintf(
            out,
            "%.0f,%u,%.2f,%.2f\n",
            len,
            c->count[i],
            100.0 * span_run / (double)c->spans_total,
            100.0 * pixel_run / c->pixels_total);
    }
    fflush(out);
    if( out != stderr )
        fclose(out);
}

#endif
