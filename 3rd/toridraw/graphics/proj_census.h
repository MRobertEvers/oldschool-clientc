#ifndef TORIDRAW_GRAPHICS_PROJ_CENSUS_H
#define TORIDRAW_GRAPHICS_PROJ_CENSUS_H

/**
 * Which projection kernel actually runs, and over how many vertices?
 *
 * The perspective dispatch picks one of eight kernels per model -- three
 * rotation shapes (6DOF, pitch+yaw, yaw-only) crossed with textured/untextured
 * and clip/noclip -- and the SSE2 file behind them is 2400 lines. Any decision
 * about hand-writing one of those is a decision about which of the eight a real
 * scene reaches, and how long its vertex arrays are. Guessing gets this wrong
 * the same way "models average 19 faces" got span length wrong: the dispatch
 * key is a property of the MODEL, and a scene is not a uniform sample of models.
 *
 * Compile-time gated, like the span census next door, and for the same reason:
 * this sits per-model on the render path, and a runtime branch there would tax
 * the measurement it exists to take.
 *
 *   -DTORIDRAW_PROJ_CENSUS=1   per-kernel model and vertex counts, plus the
 *                              vertex-count histogram that says whether a
 *                              4-wide kernel ever gets a full second block.
 *                              TORIDRAW_PROJ_CENSUS_FILE=<path> redirects it.
 */

#if defined(TORIDRAW_PROJ_CENSUS) && TORIDRAW_PROJ_CENSUS

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    TORIDRAW_PROJ_K_6DOF_TEX,
    TORIDRAW_PROJ_K_6DOF_NOTEX,
    TORIDRAW_PROJ_K_PITCHYAW_TEX,
    TORIDRAW_PROJ_K_PITCHYAW_NOTEX,
    TORIDRAW_PROJ_K_YAW_TEX,
    TORIDRAW_PROJ_K_YAW_NOTEX,
    TORIDRAW_PROJ_KIND_COUNT
};

/** Exact for 0..63 vertices, then power-of-two up to >= 4096. */
#define TORIDRAW_PROJ_CENSUS_EXACT 64
#define TORIDRAW_PROJ_CENSUS_BUCKETS (TORIDRAW_PROJ_CENSUS_EXACT + 8)

struct ToriDraw_ProjCensus
{
    /** Models projected by each kernel, split by whether it clips. */
    unsigned int models[TORIDRAW_PROJ_KIND_COUNT][2];
    /** Vertices those models carried -- the quantity the kernel loops over. */
    double vertices[TORIDRAW_PROJ_KIND_COUNT][2];
    /** Vertex counts, so the 4-wide block/tail split is a number not a guess. */
    unsigned int hist[TORIDRAW_PROJ_CENSUS_BUCKETS];
    /** Vertices landing in the scalar tail of a 4-wide kernel (count & 3). */
    double vertices_tail;
    /** Where ToriDraw_Project stopped: fast (cylinder) cull, 8-point AABB
     * cull, or went on to project. The 8-point box costs eight projected
     * corners per model, so its reject count is what says whether it earns
     * its keep. */
    unsigned int cull_fast;
    unsigned int cull_aabb;
    unsigned int cull_error;
    unsigned int projected;
    /** Set once, by the first record, so the dump is registered exactly once. */
    int installed;
};

extern struct ToriDraw_ProjCensus g_toridraw_proj_census;

static inline int
toridraw_proj_census_bucket(int n)
{
    int bucket = TORIDRAW_PROJ_CENSUS_EXACT;
    int edge = TORIDRAW_PROJ_CENSUS_EXACT;

    if( n < 0 )
        n = 0;
    if( n < TORIDRAW_PROJ_CENSUS_EXACT )
        return n;
    while( bucket < TORIDRAW_PROJ_CENSUS_BUCKETS - 1 && n >= edge * 2 )
    {
        bucket++;
        edge *= 2;
    }
    return bucket;
}

void
ToriDraw_ProjCensusDump(void);

static inline void
toridraw_proj_census_record(int kind, int clipped, int num_vertices)
{
    struct ToriDraw_ProjCensus* c = &g_toridraw_proj_census;

    assert(kind >= 0);
    assert(kind < TORIDRAW_PROJ_KIND_COUNT);
    assert(num_vertices >= 0);

    /* An empty model is a legitimate runtime state, not a contract violation. */
    if( num_vertices == 0 )
        return;
    if( !c->installed )
    {
        c->installed = 1;
        atexit(ToriDraw_ProjCensusDump);
    }
    c->models[kind][clipped ? 1 : 0] += 1;
    c->vertices[kind][clipped ? 1 : 0] += (double)num_vertices;
    c->hist[toridraw_proj_census_bucket(num_vertices)] += 1;
    c->vertices_tail += (double)(num_vertices & 3);
}

#define TORIDRAW_PROJ_CENSUS_RECORD(kind, clipped, n) \
    toridraw_proj_census_record((kind), (clipped), (n))
#define TORIDRAW_PROJ_CENSUS_COUNT(field) (g_toridraw_proj_census.field += 1)

#else

#define TORIDRAW_PROJ_CENSUS_RECORD(kind, clipped, n) ((void)0)
#define TORIDRAW_PROJ_CENSUS_COUNT(field) ((void)0)

#endif /* TORIDRAW_PROJ_CENSUS */

#endif /* TORIDRAW_GRAPHICS_PROJ_CENSUS_H */
