#ifndef EDITOR_PATH_H
#define EDITOR_PATH_H

/*
 * The path painter: a dragged line of tiles becomes an overlay path whose
 * edges follow the line instead of stepping tile by tile.
 *
 * The content's own paths were studied for it (the Windows run, 2026-11-06:
 * every surface square's level 0, the overlays that form long runs one to
 * six tiles across, rendered with the client's tile geometry). Two kinds:
 *
 *   dirt     brown tracks (e.g. jm2 overlays 14, 22, 80): edges that wander,
 *            the width breathing along the track; about a third of their
 *            tiles are edge shapes, the half-diagonal the most common.
 *   pavement the stone road (e.g. 10, Lumbridge): straight runs and clean
 *            45-degree bends, a steady width.
 *
 * Both are drawn the same way. The path is a region in continuous tile space
 * (a centreline and a width); each tile it touches is sampled on an 8 x 8
 * grid and given the overlay shape and rotation whose coverage matches those
 * samples best (EDITOR_PATH_MASK). A tile the stroke only grazes stays as it
 * was; a tile that already carries the same overlay is joined, not replaced:
 * its shape's coverage and the new coverage are fitted together, so a second
 * stroke or a fork meets the first without a seam.
 *
 * The shapes are the client's (engine/world_builder/world_decode_tile.c,
 * the overlay faces of each shape and how a rotation turns them); overlay
 * shape 11 (the trapezium) is never chosen, as the content almost never uses
 * it on a path.
 */

#include <stdint.h>

enum Editor_PathStyle
{
    EDITOR_PATH_DIRT = 0,
    EDITOR_PATH_PAVED,
    /** A stream or a river: the dirt track's way, but the bed meanders
     *  slowly across a width or more and both banks swell together; the
     *  curve stays smooth, the edges lean rather than step. With the Path
     *  tool's height on "lower by", the bed is dug as it is laid. */
    EDITOR_PATH_RIVER,
    EDITOR_PATH_STYLE_COUNT
};

/** Coverage samples per tile side: a tile's coverage is 64 bits, bit
 *  (row * 8 + column), row 0 the south row, column 0 the west column. */
#define EDITOR_PATH_MASK 8

/** Most points a stroke keeps (a drag adds one per new tile). */
#define EDITOR_PATH_POINTS_MAX 2048
/** Widest path, in tiles. */
#define EDITOR_PATH_WIDTH_MAX 8.0
/** Largest square of tiles one path may touch (its bounding box). */
#define EDITOR_PATH_SPAN_MAX 256

struct Editor_PathParams
{
    enum Editor_PathStyle style;
    /** Across the path, in tiles: 0.5 .. EDITOR_PATH_WIDTH_MAX. */
    double width;
    /** Dirt and river: how much the width and the line wander, 0 .. 1
     *  (pavement: unused). */
    double roughness;
    /** Dirt and river: the wander's seed, so a stroke redrawn draws the same
     *  edges. */
    uint32_t seed;
};

/** One planned tile. */
struct Editor_PathTile
{
    int world_x;
    int world_z;
    /** The overlay shape (0 full .. 10) and rotation (0..3) to write. */
    int shape;
    int rotation;
    /** Samples covered, 1..64 (the fit's input). */
    int coverage;
};

/**
 * What a tile already holds: 1 with its shape and rotation when it carries
 * the overlay being painted (the plan joins it), else 0.
 */
typedef int (*Editor_PathExistingFn)(void* user, int world_x, int world_z, int* out_shape, int* out_rotation);

/** The coverage of overlay `shape` (0..11) at `rotation` (0..3). */
uint64_t
Editor_PathShapeMask(
    int shape,
    int rotation);

/**
 * The shape and rotation that best cover `mask` among the style's shapes;
 * 0 when leaving the tile bare is closer than any of them.
 */
int
Editor_PathFit(
    enum Editor_PathStyle style,
    uint64_t mask,
    int* out_shape,
    int* out_rotation);

/**
 * The tiles the path through the `count` world tiles (`xs`, `zs`, in drag
 * order; repeats are dropped) covers, with their shapes. `existing` may be
 * NULL (nothing is joined). Returns the tile count, at most `max`; 0 for
 * no points or a path wider than EDITOR_PATH_SPAN_MAX tiles.
 */
int
Editor_PathPlan(
    const struct Editor_PathParams* params,
    const int* xs,
    const int* zs,
    int count,
    Editor_PathExistingFn existing,
    void* user,
    struct Editor_PathTile* out,
    int max);

/**
 * The tile corners the path covers: world corner (x, z) -- tile (x, z)'s
 * south-west corner, which its `h` token sets -- is in when at least two of
 * the four samples touching it are. The same region Editor_PathPlan fits
 * shapes to, so a height laid with the path follows its banks. Writes up
 * to `max` corners; returns the count (0 for no points or a path wider than
 * EDITOR_PATH_SPAN_MAX tiles).
 */
int
Editor_PathCorners(
    const struct Editor_PathParams* params,
    const int* xs,
    const int* zs,
    int count,
    int* out_x,
    int* out_z,
    int max);

#endif
