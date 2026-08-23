#include "graphics/raster/span_census.h"

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
