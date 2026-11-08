#ifndef EDITOR_REGION_H
#define EDITOR_REGION_H

/*
 * Region painting: an overlay whose border does not follow the grid.
 *
 * A tool never picks shapes. It says which part of the ground is overlay, as
 * COVERAGE: each tile sampled on an 8 x 8 grid, one bit a sample. The fitter
 * gives each tile the client's overlay shape and rotation whose overlay faces
 * match those samples best, and a settle pass lets a tile trade its own best
 * fit for one that carries its neighbours' edge on, so a border reads as a
 * line rather than as tiles fitted one by one. The shapes are the client's
 * (engine/world_builder/world_decode_tile.c): the mask of each is built from
 * the same vertex and face tables the mesh builder turns into triangles.
 *
 * The producers:
 *
 *   capsule   a disc swept from one point to the next (the Region brush; a
 *             click is a capsule of no length);
 *   polygon   a closed loop, filled even-odd (the Lasso);
 *   read back the coverage of tiles that already hold the overlay (Tidy, and
 *             every plan, which joins what is there);
 *   smooth    a box blur at sample resolution, thresholded (Tidy).
 *
 * The path painter (editor/editor_path.h) is one more producer: it builds a
 * grid along its line and plans it here with ADD at hardness 50.
 *
 * HARDNESS weighs the two ways a shape can be wrong: a sample it covers that
 * the region does not (the overlay spills), against a sample of the region it
 * leaves bare. At 50 the two weigh the same (the path's fit). Higher, spills
 * cost more, so a rim tile takes a thinner shape or none; lower, the rim
 * fattens and a grazed tile is kept.
 */

#include <stdint.h>

/** Samples per tile side; a tile's coverage is bit (row * 8 + column), row 0
 *  the south row, column 0 the west column. */
#define EDITOR_REGION_MASK 8
/** The client's overlay shapes, 0 (full) .. 11 (the trapezium). */
#define EDITOR_REGION_SHAPES 12
/** Largest square of tiles one plan may cover (its bounding box). */
#define EDITOR_REGION_SPAN_MAX 256
/** A tile whose best fit still disagrees with its coverage on more samples
 *  than this is a POOR fit: the preview hatches it. */
#define EDITOR_REGION_POOR_FIT 8
/** A subtract takes off a tile's remainder smaller than this (a quarter
 *  tile): what is left of an add's rim past the line the painter drew, not
 *  a shape anyone drew. */
#define EDITOR_REGION_SLIVER 16
/** Hardness bounds, percent. */
#define EDITOR_REGION_HARDNESS_MIN 10
#define EDITOR_REGION_HARDNESS_MAX 90
#define EDITOR_REGION_HARDNESS_DEFAULT 50

/** The shapes a plan may write, in tie order. */
enum Editor_RegionSet
{
    /** Every shape the content's dirt tracks use; not the trapezium. */
    EDITOR_REGION_NATURAL = 0,
    /** Full, half diagonal, half square and the corner pair: straight runs
     *  and clean 45-degree bends. */
    EDITOR_REGION_PAVED,
    /** All twelve. */
    EDITOR_REGION_ALL,
    EDITOR_REGION_SET_COUNT
};

/** What a plan does with the grid's coverage against what the tiles hold. */
enum Editor_RegionOp
{
    /** The overlay where the grid is, joined to what the tiles hold. */
    EDITOR_REGION_ADD = 0,
    /** The overlay taken off where the grid is; the rest refitted. */
    EDITOR_REGION_SUBTRACT,
    /** The grid IS the overlay over the tiles it spans (Tidy): a tile that
     *  held the overlay and is bare in the grid loses it. */
    EDITOR_REGION_REPLACE,
    EDITOR_REGION_OP_COUNT
};

/** What a tile holds, as the plan's callback answers. */
enum Editor_RegionHas
{
    /** No overlay (or one the plan may treat as bare). */
    EDITOR_REGION_HAS_NONE = 0,
    /** The overlay being painted, at *out_shape / *out_rotation. */
    EDITOR_REGION_HAS_SAME = 1,
    /** Another overlay: a tile the plan replaces only where the region covers
     *  it whole (two overlays cannot share a tile). */
    EDITOR_REGION_HAS_OTHER = 2,
};

/** What world tile (x, z) holds (enum Editor_RegionHas); with HAS_SAME its
 *  shape and rotation. NULL: every tile bare. */
typedef int (*Editor_RegionTileFn)(void* user, int world_x, int world_z, int* out_shape, int* out_rotation);

/** Coverage over a box of world tiles. Allocated by Editor_RegionGridInit. */
struct Editor_RegionGrid
{
    int x0;
    int z0;
    int w;
    int h;
    uint64_t* tiles;
};

struct Editor_RegionParams
{
    enum Editor_RegionSet set;
    enum Editor_RegionOp op;
    /** EDITOR_REGION_HARDNESS_MIN .. MAX. */
    int hardness;
    /** The settle pass may move a tile to a shape that disagrees with its
     *  coverage on at most this many samples (or on as many as its own best
     *  fit does, when that is more): a border carried on, never bent off the
     *  region. 0: no limit (the path's settle). The tools pass
     *  EDITOR_REGION_POOR_FIT. */
    int misfit_cap;
};

/** One planned tile. */
struct Editor_RegionTile
{
    int world_x;
    int world_z;
    /** The overlay shape and rotation to write; shape -1: take the overlay
     *  off (SUBTRACT and REPLACE only). */
    int shape;
    int rotation;
    /** The tile's target coverage, 0..64. */
    int coverage;
    /** Samples the written shape disagrees with the target on, 0..64. */
    int misfit;
};

/** The coverage of overlay `shape` (0..11) at `rotation` (0..3). */
uint64_t
Editor_RegionShapeMask(
    int shape,
    int rotation);

int
Editor_RegionPopcount(uint64_t v);

/** Most overlay triangles one shape has. */
#define EDITOR_REGION_SHAPE_TRIANGLES 6

/**
 * The overlay faces of `shape` at `rotation` as triangles in tile units
 * (0..1, x east, z north) -- the client's own faces, for a preview drawn
 * the way the mesh builder will draw it. Returns the count (at most
 * EDITOR_REGION_SHAPE_TRIANGLES).
 */
int
Editor_RegionShapeTriangles(
    int shape,
    int rotation,
    double out[EDITOR_REGION_SHAPE_TRIANGLES][3][2]);

/**
 * The shape and rotation of `set` whose coverage best matches `mask` at
 * `hardness`; 0 (and the outputs untouched) when leaving the tile bare is
 * closer than any of them.
 */
int
Editor_RegionFit(
    enum Editor_RegionSet set,
    int hardness,
    uint64_t mask,
    int* out_shape,
    int* out_rotation);

/**
 * A zeroed grid over world tiles x0 .. x0+w-1, z0 .. z0+h-1. Returns 0 (and
 * allocates nothing) when the box is wider or taller than
 * EDITOR_REGION_SPAN_MAX -- a drag that long is a runtime state, said to the
 * user; the grid must be freed when it returns 1.
 */
int
Editor_RegionGridInit(
    struct Editor_RegionGrid* grid,
    int x0,
    int z0,
    int w,
    int h);

void
Editor_RegionGridFree(struct Editor_RegionGrid* grid);

/** Sets every sample within `radius` tiles of the segment a..b (world tiles,
 *  continuous: a tile's centre is x + 0.5). a == b is a disc. */
void
Editor_RegionCapsule(
    struct Editor_RegionGrid* grid,
    double ax,
    double az,
    double bx,
    double bz,
    double radius);

/** Sets every sample inside the closed loop (even-odd), `count` >= 3 points
 *  in world tiles, continuous. */
void
Editor_RegionPolygon(
    struct Editor_RegionGrid* grid,
    const double* xs,
    const double* zs,
    int count);

/** Chaikin's corner cutting on a closed loop, `passes` times: `*count`
 *  points in, twice as many out each pass, at most `max`. */
void
Editor_RegionLoopSmooth(
    double* xs,
    double* zs,
    int* count,
    int max,
    int passes);

/** ORs into every grid tile the coverage of what it holds when that is the
 *  overlay being painted (`existing` answers HAS_SAME). */
void
Editor_RegionReadBack(
    struct Editor_RegionGrid* grid,
    Editor_RegionTileFn existing,
    void* user);

/**
 * A box blur `radius` samples each way over the whole grid (outside it is
 * bare), each sample kept when the share of the box it reads covered is at
 * least `hardness` percent. Radius 0 changes nothing.
 */
void
Editor_RegionSmooth(
    struct Editor_RegionGrid* grid,
    int radius,
    int hardness);

/**
 * The tiles a plan writes: the grid's coverage set against what the tiles
 * hold (`existing`, may be NULL) by `params->op`, fitted, settled. A tile
 * left as it was is still listed when it holds the overlay and the region
 * touches it (the caller drops a write that changes nothing). Returns the
 * count, at most `max`.
 */
int
Editor_RegionPlan(
    const struct Editor_RegionParams* params,
    const struct Editor_RegionGrid* grid,
    Editor_RegionTileFn existing,
    void* user,
    struct Editor_RegionTile* out,
    int max);

/**
 * Underlay painting over the grid's region: underlays have no shapes (the
 * client blends them between tile centres), so the rim is a dither. A tile's
 * share is its coverage, averaged over the tiles within `feather` tiles when
 * that is > 0; a whole tile is always painted, a rim tile when a hash of
 * `seed` and the tile falls under its share. Writes the tiles to paint,
 * absolute, at most `max`; returns the count. The grid wants `feather` tiles
 * of margin round the region.
 */
int
Editor_RegionDither(
    const struct Editor_RegionGrid* grid,
    int feather,
    uint32_t seed,
    int* out_x,
    int* out_z,
    int max);

/**
 * The run of tiles holding the overlay (`existing` answers HAS_SAME) that
 * world tile (x, z) belongs to, by edge: up to `max` tiles, within
 * EDITOR_REGION_SPAN_MAX / 2 of the start each way. *out_clipped is set when
 * the run reached that window or `max` (a run bigger than Tidy takes).
 * Returns the count; 0 when the start does not hold the overlay.
 */
int
Editor_RegionFlood(
    Editor_RegionTileFn existing,
    void* user,
    int world_x,
    int world_z,
    int* out_x,
    int* out_z,
    int max,
    int* out_clipped);

#endif
