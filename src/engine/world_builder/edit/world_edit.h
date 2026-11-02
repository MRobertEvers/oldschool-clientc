#ifndef WORLD_EDIT_H
#define WORLD_EDIT_H

/*
 * The EDIT world layout (docs/MAPED_DESIGN.html #world): the map editor's
 * builder.
 *
 * Same bake as the game's STATIC layout, three differences:
 *
 *   - The terrain build maps stay resident after the bake (blend, overlay,
 *     shape, light, shade, flags), so a tile edit re-blends, relights and
 *     remeshes from them instead of baking the window again.
 *   - Every placement has a handle -- pool index plus generation -- that
 *     survives flushes, and EDIT remembers what each placement did to the build
 *     maps (its shadows, its wall offsets, its decor, what lights it) so a
 *     neighbourhood can be re-derived when one loc goes.
 *   - A change is a local invalidation, applied by one flush per frame:
 *
 *       corner height   the 4 tiles sharing the corner remesh, the light at
 *                       the 4 corners around it is recomputed, every
 *                       placement standing on or contoured over the tiles is
 *                       re-placed (position, contour, lighting)
 *       underlay        the +-5 blend window re-blends; tiles whose colour
 *                       moved remesh
 *       overlay/shape   the tile remeshes
 *       flags           BLOCK restamps collision, VIS_BELOW and REMOVE_ROOF
 *                       their column; LINK_BELOW (a bridge) asks the caller to
 *                       rebuild the window (WORLD_BUILDER_FLUSH_REBUILD)
 *       loc add/remove  the loc and every share-lit neighbour re-place and
 *                       relight, its shadows are replayed, collision and the
 *                       minimap walls of its tiles are restamped
 *
 * Contract with the caller: every tile_set / loc_place / loc_remove mirrors a
 * change the caller has also made in the CacheProvider's map records, so that a
 * rebuild of the window (pan, LINK_BELOW, the first bake) reads what EDIT
 * shows. The map editor keeps that contract in app_map_editor.c.
 *
 * What a flush does NOT yet redo locally (it is correct again at the next
 * window bake): planar occluders are dropped on the first edit rather than
 * re-merged, and a placed or removed loc's minimap mapscene icon and
 * raise-object stack height are not updated.
 *
 * The identity test (engine/world_builder/test/edit_world_test.c) holds the
 * layout to its one promise: random edits through EDIT, then a STATIC bake of
 * the same squares, give the same element models, vertex for vertex, at the
 * same positions.
 */

#include "engine/world_builder/world_builder.h"

#include <stdint.h>

/** What the last flush did. */
struct WorldEditFlushStats
{
    /** Flushes that had work, since the bake. */
    int flushes;
    /** This flush: */
    int tiles_set;
    int corners_relit;
    int tiles_reblended;
    int tiles_remeshed;
    int placements_rebuilt;
    int placements_removed;
    int placements_added;
    int collision_tiles;
    /** Wall time of the flush, ms. */
    double ms;
};

/** A square window is baked and resident: tile_set and the loc calls apply
 *  locally. 0 before the first bake, and after an instance (zone-copy) bake,
 *  which EDIT does not edit -- every change then answers
 *  WORLD_BUILDER_FLUSH_REBUILD. */
int
WorldEdit_Ready(const struct WorldBuilder* builder);

/** Bumps at every bake: handles from an older serial name nothing. */
uint32_t
WorldEdit_BakeSerial(const struct WorldBuilder* builder);

/**
 * The handle of the `index`th loc of square (map_x, map_z) as the last bake
 * placed it -- the provider's loc list order, every entry counted (one whose
 * multiloc hides it, or whose config is missing, too). NONE when the square
 * is not in the window, the index is past its list, or that placement has been
 * removed since.
 */
int
WorldEdit_SquarePlacement(
    const struct WorldBuilder* builder,
    int map_x,
    int map_z,
    int index);

/** Square (map_x, map_z) is one the last bake placed: tile_set and the loc
 *  calls may name its in-scene tiles. */
int
WorldEdit_SquareInWindow(
    const struct WorldBuilder* builder,
    int map_x,
    int map_z);

/** The first live placement, in map order, at this tile/level with this shape:
 *  the one Editor_SquareLocFind names for the same key. NONE when none. */
int
WorldEdit_FindPlacement(
    const struct WorldBuilder* builder,
    int scene_x,
    int scene_z,
    int level,
    int shape);

/** A live placement's record. Returns 0 when the handle names nothing live. */
int
WorldEdit_PlacementGet(
    const struct WorldBuilder* builder,
    int handle,
    struct World_LocPlacement* out);

void
WorldEdit_LastFlushStats(
    const struct WorldBuilder* builder,
    struct WorldEditFlushStats* out);

#endif
