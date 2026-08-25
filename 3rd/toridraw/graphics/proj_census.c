#include "graphics/proj_census.h"

#if defined(TORIDRAW_PROJ_CENSUS) && TORIDRAW_PROJ_CENSUS

struct ToriDraw_ProjCensus g_toridraw_proj_census;

static char const* const g_proj_kind_name[TORIDRAW_PROJ_KIND_COUNT] = {
    "6dof     tex", "6dof   notex", "pitchyaw tex", "pitchyaw ntx",
    "yaw      tex", "yaw    notex",
};

/**
 * Report shares of VERTICES, not of models. A kernel that runs on 2% of models
 * but every one of them is a 400-vertex npc is not 2% of the work, and the
 * model share is the number that would say it was.
 */
void
ToriDraw_ProjCensusDump(void)
{
    struct ToriDraw_ProjCensus const* c = &g_toridraw_proj_census;
    char const* path = getenv("TORIDRAW_PROJ_CENSUS_FILE");
    FILE* f = stderr;
    double verts_total = 0.0;
    unsigned int models_total = 0;
    int i;

    for( i = 0; i < TORIDRAW_PROJ_KIND_COUNT; i++ )
    {
        verts_total += c->vertices[i][0] + c->vertices[i][1];
        models_total += c->models[i][0] + c->models[i][1];
    }
    if( models_total == 0 )
        return;

    if( path )
    {
        f = fopen(path, "w");
        if( !f )
            f = stderr;
    }

    fprintf(f, "proj-census: %u models, %.0f vertices\n", models_total, verts_total);
    fprintf(f, "  kernel        noclip models   clip models    vertices   %% verts\n");
    for( i = 0; i < TORIDRAW_PROJ_KIND_COUNT; i++ )
    {
        double v = c->vertices[i][0] + c->vertices[i][1];

        if( c->models[i][0] + c->models[i][1] == 0 )
            continue;
        fprintf(
            f,
            "  %-12s  %13u  %12u  %10.0f  %7.2f\n",
            g_proj_kind_name[i],
            c->models[i][0],
            c->models[i][1],
            v,
            100.0 * v / verts_total);
    }

    fprintf(f, "  vertices in the 4-wide scalar tail: %.0f (%.2f%%)\n",
        c->vertices_tail, 100.0 * c->vertices_tail / verts_total);

    fprintf(f, "  vertex-count histogram (models):\n");
    for( i = 0; i < TORIDRAW_PROJ_CENSUS_BUCKETS; i++ )
    {
        int lo;
        int hi;

        if( c->hist[i] == 0 )
            continue;
        if( i < TORIDRAW_PROJ_CENSUS_EXACT )
        {
            lo = i;
            hi = i;
        }
        else
        {
            lo = TORIDRAW_PROJ_CENSUS_EXACT << (i - TORIDRAW_PROJ_CENSUS_EXACT);
            hi = (lo * 2) - 1;
        }
        if( lo == hi )
            fprintf(f, "    %6d       %8u\n", lo, c->hist[i]);
        else
            fprintf(f, "    %6d-%-6d %8u\n", lo, hi, c->hist[i]);
    }

    if( f != stderr )
        fclose(f);
}

#endif /* TORIDRAW_PROJ_CENSUS */
