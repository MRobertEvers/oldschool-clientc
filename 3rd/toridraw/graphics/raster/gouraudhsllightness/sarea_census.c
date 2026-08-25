#include "graphics/raster/gouraudhsllightness/sarea_census.h"

#if defined(TORIDRAW_SAREA_CENSUS) && TORIDRAW_SAREA_CENSUS

struct ToriDraw_SareaCensus g_toridraw_sarea_census;

void
ToriDraw_SareaCensusDump(void)
{
    /* Same convention as the face census: a named file, because the client
     * writes enough to stderr that a dump landing there has to be dug out of
     * it, and a runner that greps for the table is a runner that can miss it
     * silently. Falls back to stderr so the dump is never simply lost. */
    const char* path = getenv("TORIDRAW_SAREA_CENSUS_FILE");
    FILE* out = stderr;
    double running = 0.0;
    int k;

    if( g_toridraw_sarea_census.total <= 0.0 )
        return;

    if( path )
    {
        FILE* f = fopen(path, "w");
        if( f )
            out = f;
    }

    fprintf(out, "\n=== gouraud |sarea| census ===\n");
    fprintf(out, "triangles reaching the prologue: %.0f\n",
            g_toridraw_sarea_census.total);
    fprintf(out, "%12s %14s %8s %9s\n", "|sarea| <", "count", "share", "cumul");
    for( k = 0; k <= 32; k++ )
    {
        if( g_toridraw_sarea_census.buckets[k] == 0.0 )
            continue;
        running += g_toridraw_sarea_census.buckets[k];
        fprintf(out, "%12.0f %14.0f %7.3f%% %8.3f%%\n",
                (k >= 31) ? 2147483648.0 : (double)(1u << k),
                g_toridraw_sarea_census.buckets[k],
                100.0 * g_toridraw_sarea_census.buckets[k] /
                    g_toridraw_sarea_census.total,
                100.0 * running / g_toridraw_sarea_census.total);
    }
    fflush(out);
    if( out != stderr )
        fclose(out);
}

#endif
