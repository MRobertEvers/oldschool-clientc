#ifndef SCENE_OCCLUDERS_H
#define SCENE_OCCLUDERS_H

/**
 * Planar occluder system — Client-TS Occlude / World.calcOcclude / World.occluded.
 *
 * A big flat opaque surface (wall, floor between levels, roof) casts a shadow
 * away from the camera. Anything inside that shadow cannot be seen, so it does
 * not have to be drawn. Occluders are built once at scene load (see
 * occluder_buildmap.h) and selected per frame for the current eye.
 *
 * See docs/OCCLUDER_SYSTEM.md.
 */

#include "painters.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include <stdbool.h>
#include <stdint.h>

/** A wall running north-south: world X never varies across the plane. */
#define OCCLUDER_PLANE_CONSTANT_X 1
/** A wall running east-west: world Z never varies across the plane. */
#define OCCLUDER_PLANE_CONSTANT_Z 2
/** A floor or roof: world Y never varies across the plane. */
#define OCCLUDER_PLANE_CONSTANT_Y 4

/**
 * Which half-space this surface hides — i.e. which side of it the camera is
 * NOT on. Recomputed every frame; the reference calls this `mode` (1..5).
 */
enum OccluderHides
{
    OCCLUDER_HIDES_NOTHING = 0,
    OCCLUDER_HIDES_WEST_OF_PLANE = 1,  /* mode 1: camera east of an X-plane */
    OCCLUDER_HIDES_EAST_OF_PLANE = 2,  /* mode 2: camera west of an X-plane */
    OCCLUDER_HIDES_SOUTH_OF_PLANE = 3, /* mode 3: camera north of a Z-plane */
    OCCLUDER_HIDES_NORTH_OF_PLANE = 4, /* mode 4: camera south of a Z-plane */
    OCCLUDER_HIDES_BELOW_PLANE = 5,    /* mode 5: camera above a Y-plane (y is negative-up) */
};

/*
 * How small a merged surface is still worth keeping as an occluder.
 *
 * Primarily a cost/benefit threshold. Each occluder is tested as its own
 * projected shadow rectangle (scene_occluders_point_hidden_ref), so a smaller
 * occluder casts a proportionally smaller shadow and can only ever ADD correct
 * culling -- it cannot hide something a larger one would have left visible.
 * What a too-small occluder costs is a per-frame test that rarely pays for
 * itself, plus a slot out of the OCCLUDER_MAX_PER_LEVEL budget below.
 *
 * The one place it is not purely a cost question: a ground tile is judged by
 * sampling its four CORNERS, so a tile whose corners are all shadowed but whose
 * middle is not can be culled when it should not be. That approximation comes
 * from the reference and exists at any gate; halving the gates admits more
 * occluders and therefore widens the exposure to it.
 *
 * Measured on the bench suite (lumbridge-ground, soft3d, 765x503): -10.8%
 * frame time, winning all six paired runs against an interleaved baseline,
 * painter commands 2929 -> 2636. A two-sample image comparison (four frames
 * per binary, every within- and between-group pair) put the between-group
 * difference inside the spread that two runs of the SAME binary already
 * produce, so no image change was detectable -- but that scene animates
 * entities independently of the camera, so this bounds the difference rather
 * than proving equivalence. docs/xpbench/OCCLUDER_GATES.md has the runs.
 *
 * This is a deliberate divergence from the reference client, which uses 8 and
 * 4. Restore those two numbers to get reference behaviour back.
 */
/** Wall merge must cover at least this many (levels × tiles). */
#define OCCLUDER_WALL_MIN_TILE_AREA 4
/** Floor merge must cover at least this many tiles. */
#define OCCLUDER_FLOOR_MIN_TILE_AREA 2
/** Wall planes extend this many scene units below the top-level ground. */
#define OCCLUDER_WALL_HEIGHT 240
/** Occluders closer than this to the eye are skipped (degenerate projection). */
#define OCCLUDER_CAMERA_DEAD_ZONE 32
/** Floor/roof planes only activate when the eye is this far below them. */
#define OCCLUDER_ROOF_MIN_CLEARANCE 128
/** Cap per top-level, matching Client-TS World.levelOccluders capacity. */
#define OCCLUDER_MAX_PER_LEVEL 500
/** Draw-distance clamp — rev-239 Scene preference (class112.method3959). */
#define OCCLUDER_DRAW_DISTANCE_MIN 25
#define OCCLUDER_DRAW_DISTANCE_MAX 90
/** Far clip scene units per draw-distance tile (deob class243.method4457). */
#define OCCLUDER_FAR_CLIP_PER_TILE 210

struct SceneOccluder
{
    /* Build time: the world-space box the surface covers. One axis is collapsed
     * (min == max) because the surface is a plane, not a volume. */
    int32_t min_x;
    int32_t max_x;
    int32_t min_y;
    int32_t max_y;
    int32_t min_z;
    int32_t max_z;
    int16_t tile_min_x;
    int16_t tile_max_x;
    int16_t tile_min_z;
    int16_t tile_max_z;
    uint8_t plane;

    /* Per frame: how the surface's shadow widens with distance from the eye,
     * in 8.8 fixed point per unit of depth. Reference calls these minDelta*. */
    uint8_t hides;
    int32_t spread_min_x;
    int32_t spread_max_x;
    int32_t spread_min_y;
    int32_t spread_max_y;
    int32_t spread_min_z;
    int32_t spread_max_z;
};

/**
 * One active occluder, in the form the point test actually reads it.
 *
 * `key` is the occluder's plane coordinate with the gate's sign folded in:
 * negated for the three modes whose gate is `q - plane` rather than
 * `plane - q`, so every run gates on `key - q > 0` and every run sorts
 * descending. Axis A is the one the original tested first and is the one
 * that rejects 88.7% of everything that gets past the gate.
 */
struct SceneOccluderPacked
{
    int32_t key;
    int32_t lo_a;
    int32_t hi_a;
    int32_t spread_lo_a;
    int32_t spread_hi_a;
    int32_t lo_b;
    int32_t hi_b;
    int32_t spread_lo_b;
    int32_t spread_hi_b;
};

struct SceneOccluders
{
    int width;
    int height;
    int levels;

    struct SceneOccluder* level_occluders[4];
    int level_occluder_count[4];

    /** Active set for the current frame (pointers into level_occluders). */
    struct SceneOccluder* active[OCCLUDER_MAX_PER_LEVEL];
    int active_count;

    /**
     * Contiguous runs of active[] grouped by hides, so the point test can
     * walk each mode without a per-plane branch. Index is OccluderHides 1..5;
     * run_start[h] is the first active index with that hides, run_end[h] is
     * one past the last. Unused modes have start == end.
     */
    int run_start[6];
    int run_end[6];

    /**
     * active[] rewritten for the point test: same order, same runs, but
     * each run sorted by `key` descending and every field the test reads
     * copied in. Built once per frame by occluder_partition_active.
     */
    struct SceneOccluderPacked packed[OCCLUDER_MAX_PER_LEVEL];

    /** Copy of ground heights at build time (scene units). Indexed like
     * heightmap: x + z * width + level * width * height. Size is
     * (width+1)*(height+1)*levels so corner samples are in range. */
    int16_t* ground_heights;
    int ground_width;
    int ground_height;

    /**
     * Per-tile cache of ground-occlusion verdicts for the current frame.
     * +frame_no = hidden, -frame_no = visible, anything else = not yet
     * computed. Sized width * height * levels.
     */
    int32_t* tile_cycle;
    int32_t frame_no;

    int camera_sx;
    int camera_sz;
    int eye_x;
    int eye_y;
    int eye_z;
};

struct SceneOccluders*
scene_occluders_new(
    int width,
    int height,
    int levels);

void
scene_occluders_free(struct SceneOccluders* occ);

void
scene_occluders_clear(struct SceneOccluders* occ);

/** Append one occluder under top_level. Returns 0 on success, -1 if full. */
int
scene_occluders_add(
    struct SceneOccluders* occ,
    int top_level,
    int plane,
    int min_x,
    int min_y,
    int min_z,
    int max_x,
    int max_y,
    int max_z);

/** Copy ground heights from a heightmap-shaped int16 array (size
 * ground_width * ground_height * levels). Called once at scene build. */
void
scene_occluders_set_ground_heights(
    struct SceneOccluders* occ,
    const int16_t* heights,
    int ground_width,
    int ground_height,
    int levels);

int
scene_occluders_ground_height(
    const struct SceneOccluders* occ,
    int x,
    int z,
    int level);

/**
 * Select occluders for the current camera (Client-TS calcOcclude /
 * deob updateActiveOccluders).
 * top_level is the roof-capped max draw level (0..3).
 * draw_distance is the painter's runtime radius (Scene.drawDistance, 25..90);
 * the footprint gate indexes tiles in [0, 2*draw_distance].
 * span may be NULL; when provided (and non-empty), it gates the footprint
 * against the frustum the way the reference uses visibilityMap. When NULL
 * and cullmap is also NULL, every footprint tile is treated as visible.
 * cullmap + camera_pitch/yaw are the baked-map fallback (unused today).
 */
void
scene_occluders_select_for_camera(
    struct SceneOccluders* occ,
    int eye_x,
    int eye_y,
    int eye_z,
    int top_level,
    int draw_distance,
    const struct PaintersCullSpan* span,
    const struct PaintersCullMap* cullmap,
    int camera_pitch,
    int camera_yaw);

/**
 * Is the world-space point (x,y,z) behind any active occluder?
 * Hot path — early-outs on active_count == 0.
 */
#if defined(TORIDRAW_OCC_CENSUS) && TORIDRAW_OCC_CENSUS
/**
 * Two numbers decide what to do about the occlusion cull, and the sampling
 * profile can supply neither: how often the point predicate runs, and how many
 * occluders it walks when it does. Both are functions of the scene and the
 * camera alone, so they are counted here rather than on the target box.
 */
struct SceneOccluderCensus
{
    unsigned long long point_calls;
    unsigned long long point_hits;
    /** active_count summed over calls -- the loop-iteration bill, since a
     *  call that returns false visits every active occluder. */
    unsigned long long point_active_sum;
    /** Loop iterations actually executed. Before the sorted rewrite this
     *  equalled point_active_sum by construction; the gap between them
     *  now IS the saving. */
    unsigned long long point_visits;
    /** Visits that got past the `d > 0` sign gate, i.e. paid the four
     *  imuls. The gap up to point_active_sum is what a run sorted by
     *  plane coordinate could break out of instead of walking. */
    unsigned long long point_dpos;
    /** Of those, how many the FIRST range axis alone rejected. */
    unsigned long long point_axis_a_reject;
    unsigned long long tile_calls;
    unsigned long long tile_cached;
    unsigned long long wall_calls;
    unsigned long long frames;
    unsigned long long active_sum;
    unsigned int active_max;
};
extern struct SceneOccluderCensus g_scene_occluder_census;
void scene_occluders_census_arm(void);
#endif

/**
 * One run of the point test: is (ca, cb) inside the shadow of any occluder
 * whose plane the point is on the far side of?
 *
 * The run is sorted by key descending, so `key - q <= 0` for one entry means
 * it holds for every entry after it and the walk is over. The four bounds
 * are produced one at a time because the first one usually settles it.
 */
static inline bool
occluder_run_hidden(
    const struct SceneOccluderPacked* p,
    int i,
    int n,
    int q,
    int ca,
    int cb)
{
    for( ; i < n; i++ )
    {
        int d = p[i].key - q;
        int bound;

#if defined(TORIDRAW_OCC_CENSUS) && TORIDRAW_OCC_CENSUS
        g_scene_occluder_census.point_visits++;
#endif
        if( d <= 0 )
            return false;

#if defined(TORIDRAW_OCC_CENSUS) && TORIDRAW_OCC_CENSUS
        g_scene_occluder_census.point_dpos++;
#endif
        bound = p[i].lo_a + ((p[i].spread_lo_a * d) >> 8);
        if( ca < bound )
        {
#if defined(TORIDRAW_OCC_CENSUS) && TORIDRAW_OCC_CENSUS
            g_scene_occluder_census.point_axis_a_reject++;
#endif
            continue;
        }
        bound = p[i].hi_a + ((p[i].spread_hi_a * d) >> 8);
        if( ca > bound )
        {
#if defined(TORIDRAW_OCC_CENSUS) && TORIDRAW_OCC_CENSUS
            g_scene_occluder_census.point_axis_a_reject++;
#endif
            continue;
        }
        bound = p[i].lo_b + ((p[i].spread_lo_b * d) >> 8);
        if( cb < bound )
            continue;
        bound = p[i].hi_b + ((p[i].spread_hi_b * d) >> 8);
        if( cb <= bound )
            return true;
    }
    return false;
}

/**
 * Is the world-space point (x,y,z) behind any active occluder?
 *
 * The negated q for EAST/NORTH/BELOW is the sign the packed key already
 * carries; see struct SceneOccluderPacked.
 */
static inline bool
scene_occluders_point_hidden_impl(
    const struct SceneOccluders* occ,
    int x,
    int y,
    int z)
{
    const struct SceneOccluderPacked* p = occ->packed;
    const int* rs = occ->run_start;
    const int* re = occ->run_end;

    assert(occ);
    if( occ->active_count == 0 )
        return false;

    if( occluder_run_hidden(p, rs[OCCLUDER_HIDES_WEST_OF_PLANE],
                            re[OCCLUDER_HIDES_WEST_OF_PLANE], x, z, y) )
        return true;
    if( occluder_run_hidden(p, rs[OCCLUDER_HIDES_EAST_OF_PLANE],
                            re[OCCLUDER_HIDES_EAST_OF_PLANE], -x, z, y) )
        return true;
    if( occluder_run_hidden(p, rs[OCCLUDER_HIDES_SOUTH_OF_PLANE],
                            re[OCCLUDER_HIDES_SOUTH_OF_PLANE], z, x, y) )
        return true;
    if( occluder_run_hidden(p, rs[OCCLUDER_HIDES_NORTH_OF_PLANE],
                            re[OCCLUDER_HIDES_NORTH_OF_PLANE], -z, x, y) )
        return true;
    if( occluder_run_hidden(p, rs[OCCLUDER_HIDES_BELOW_PLANE],
                            re[OCCLUDER_HIDES_BELOW_PLANE], -y, x, z) )
        return true;
    return false;
}

#if (defined(TORIDRAW_OCC_VERIFY) && TORIDRAW_OCC_VERIFY) \
 || (defined(TORIDRAW_OCC_REF) && TORIDRAW_OCC_REF)
/**
 * The predicate as it stood before the packed rewrite, walking active[] in
 * whatever order the counting sort left it. Kept so the rewrite can be
 * checked against it rather than argued about: a disagreement here is a
 * building that stops being drawn.
 */
static inline bool
scene_occluders_point_hidden_ref(
    const struct SceneOccluders* occ,
    int x,
    int y,
    int z)
{
    int i;
    int n;

    assert(occ);
    if( occ->active_count == 0 )
        return false;

    for( i = occ->run_start[OCCLUDER_HIDES_WEST_OF_PLANE],
         n = occ->run_end[OCCLUDER_HIDES_WEST_OF_PLANE];
         i < n;
         i++ )
    {
        const struct SceneOccluder* o = occ->active[i];
        int dx = o->min_x - x;
        if( dx > 0 )
        {
            int min_z = o->min_z + ((o->spread_min_z * dx) >> 8);
            int max_z = o->max_z + ((o->spread_max_z * dx) >> 8);
            int min_y = o->min_y + ((o->spread_min_y * dx) >> 8);
            int max_y = o->max_y + ((o->spread_max_y * dx) >> 8);
            if( z >= min_z && z <= max_z && y >= min_y && y <= max_y )
                return true;
        }
    }
    for( i = occ->run_start[OCCLUDER_HIDES_EAST_OF_PLANE],
         n = occ->run_end[OCCLUDER_HIDES_EAST_OF_PLANE];
         i < n;
         i++ )
    {
        const struct SceneOccluder* o = occ->active[i];
        int dx = x - o->min_x;
        if( dx > 0 )
        {
            int min_z = o->min_z + ((o->spread_min_z * dx) >> 8);
            int max_z = o->max_z + ((o->spread_max_z * dx) >> 8);
            int min_y = o->min_y + ((o->spread_min_y * dx) >> 8);
            int max_y = o->max_y + ((o->spread_max_y * dx) >> 8);
            if( z >= min_z && z <= max_z && y >= min_y && y <= max_y )
                return true;
        }
    }
    for( i = occ->run_start[OCCLUDER_HIDES_SOUTH_OF_PLANE],
         n = occ->run_end[OCCLUDER_HIDES_SOUTH_OF_PLANE];
         i < n;
         i++ )
    {
        const struct SceneOccluder* o = occ->active[i];
        int dz = o->min_z - z;
        if( dz > 0 )
        {
            int min_x = o->min_x + ((o->spread_min_x * dz) >> 8);
            int max_x = o->max_x + ((o->spread_max_x * dz) >> 8);
            int min_y = o->min_y + ((o->spread_min_y * dz) >> 8);
            int max_y = o->max_y + ((o->spread_max_y * dz) >> 8);
            if( x >= min_x && x <= max_x && y >= min_y && y <= max_y )
                return true;
        }
    }
    for( i = occ->run_start[OCCLUDER_HIDES_NORTH_OF_PLANE],
         n = occ->run_end[OCCLUDER_HIDES_NORTH_OF_PLANE];
         i < n;
         i++ )
    {
        const struct SceneOccluder* o = occ->active[i];
        int dz = z - o->min_z;
        if( dz > 0 )
        {
            int min_x = o->min_x + ((o->spread_min_x * dz) >> 8);
            int max_x = o->max_x + ((o->spread_max_x * dz) >> 8);
            int min_y = o->min_y + ((o->spread_min_y * dz) >> 8);
            int max_y = o->max_y + ((o->spread_max_y * dz) >> 8);
            if( x >= min_x && x <= max_x && y >= min_y && y <= max_y )
                return true;
        }
    }
    for( i = occ->run_start[OCCLUDER_HIDES_BELOW_PLANE],
         n = occ->run_end[OCCLUDER_HIDES_BELOW_PLANE];
         i < n;
         i++ )
    {
        const struct SceneOccluder* o = occ->active[i];
        int dy = y - o->min_y;
        if( dy > 0 )
        {
            int min_x = o->min_x + ((o->spread_min_x * dy) >> 8);
            int max_x = o->max_x + ((o->spread_max_x * dy) >> 8);
            int min_z = o->min_z + ((o->spread_min_z * dy) >> 8);
            int max_z = o->max_z + ((o->spread_max_z * dy) >> 8);
            if( x >= min_x && x <= max_x && z >= min_z && z <= max_z )
                return true;
        }
    }
    return false;
}
#endif

#if defined(TORIDRAW_OCC_REF) && TORIDRAW_OCC_REF
/* A/B arm: run the pre-rewrite predicate, so the only difference between the
 * two timed binaries is which of the two walks the active set. */
#define scene_occluders_point_hidden_impl scene_occluders_point_hidden_ref
#endif

#if defined(TORIDRAW_OCC_CENSUS) && TORIDRAW_OCC_CENSUS
/** Counting wrapper; see struct SceneOccluderCensus. */
static inline bool
scene_occluders_point_hidden_counted(
    const struct SceneOccluders* occ,
    int x,
    int y,
    int z)
{
    bool hit;
    g_scene_occluder_census.point_calls++;
    g_scene_occluder_census.point_active_sum += (unsigned)occ->active_count;
    hit = scene_occluders_point_hidden_impl(occ, x, y, z);
    if( hit )
        g_scene_occluder_census.point_hits++;
    return hit;
}
#else
#define scene_occluders_point_hidden_counted scene_occluders_point_hidden_impl
#endif

#if defined(TORIDRAW_OCC_VERIFY) && TORIDRAW_OCC_VERIFY
/**
 * Run the packed predicate and the pre-rewrite one on every query and stop
 * the program at the first disagreement.
 *
 * Not an assert: the lane this needs to run in defines NDEBUG, and a check
 * that disappears under the build it is checking is not a check. Occlusion
 * mistakes present as scenery that quietly stops being drawn, which is the
 * kind of bug that survives a play-test, so this one aborts.
 */
static inline bool
scene_occluders_point_hidden(
    const struct SceneOccluders* occ,
    int x,
    int y,
    int z)
{
    bool fast = scene_occluders_point_hidden_counted(occ, x, y, z);
    bool ref = scene_occluders_point_hidden_ref(occ, x, y, z);
    if( fast != ref )
    {
        fprintf(stderr,
                "occluder verify: point (%d,%d,%d) fast=%d ref=%d, "
                "active=%d\n",
                x, y, z, (int)fast, (int)ref, occ->active_count);
        fflush(stderr);
        abort();
    }
    return fast;
}
#else
#define scene_occluders_point_hidden scene_occluders_point_hidden_counted
#endif

/** All four ground corners of tile (x,z) at `level` are hidden. Cached. */
bool
scene_occluders_ground_tile_hidden(
    struct SceneOccluders* occ,
    int level,
    int x,
    int z);

/**
 * Wall face at tile (x,z) is hidden. `wall_side` is an enum WallSide bit
 * (1/2/4/8 straight, 16/32/64/128 corner). Requires the ground tile hidden.
 */
bool
scene_occluders_wall_hidden(
    struct SceneOccluders* occ,
    int level,
    int x,
    int z,
    int wall_side);

/**
 * Column (decor / ground object) of height `model_min_y` above the tile is
 * hidden. Requires the ground tile hidden.
 */
bool
scene_occluders_column_hidden(
    struct SceneOccluders* occ,
    int level,
    int x,
    int z,
    int model_min_y);

/**
 * Scenery footprint is hidden (Client-TS spriteOccluded2 / deob isAreaOccluded).
 * Takes the painter's (sx, sz, size_x, size_z) and derives max_x/max_z inside —
 * callers must not pass min/max tile extents (easy to swap with size_*).
 * model_min_y is the model's extent above the ground (Model.bottomY).
 */
bool
scene_occluders_footprint_hidden(
    struct SceneOccluders* occ,
    int level,
    int sx,
    int sz,
    int size_x,
    int size_z,
    int model_min_y);

#endif
