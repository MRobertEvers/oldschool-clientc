#ifndef SRC_EDITOR_EDITOR_DERIVE_H
#define SRC_EDITOR_EDITOR_DERIVE_H

/**
 * Authored document -> what the renderer draws.
 *
 * Its own translation unit, and the only part of the editor that links the
 * cache codec. The document layer beside it is plain data — tiles, a loc list,
 * dirty flags — and stays testable without a cache; this is the one edge that
 * needs one, so it is the one file that pays for it.
 */

#include "editor_types.h"

struct ToriRS_MapTerrain;
struct ToriRS_MapFloor;
struct ToriRS_MapLocs;
struct RSCache;

/**
 * Authored tiles -> the renderer's terrain, with heights resolved.
 *
 * Runs the authored record through the cache's own encoder and decoder rather
 * than reimplementing the height fixup here. That fixup is not a formatting
 * detail — it generates a height from Perlin noise for every tile the file
 * left blank, folds in the level below above level 0, and scales authored
 * values by the tile height basis. Reimplementing it would put a second copy
 * of that arithmetic in the tree, and the editor would slowly stop agreeing
 * with the game about what its own map looks like.
 *
 * Cost is one encode and one decode of a 64x64x4 square, which happens per
 * square per commit of a terrain edit, not per frame.
 *
 * `profile` supplies the tile-attribute widths, which are an era difference
 * (u8 before OldSchool 209, u16 after) — deriving with the wrong one shifts
 * every tile.
 *
 * Returns 1 on success, 0 when the authored record holds a value the format
 * cannot express.
 */
int
Editor_SquareDeriveTerrain(
    const struct Editor_Square* square,
    const struct RSCache* profile,
    struct ToriRS_MapTerrain* out_terrain);

/**
 * One tile of a square, derived exactly as Editor_SquareDeriveTerrain derives
 * it -- the per-tile form the EDIT world's tile_set takes.
 *
 * A tile's resolved height depends on nothing but its own column: Perlin noise
 * at its absolute coordinates on level 0, the level below plus its own
 * authored step above that. So one tile costs a walk up its column, not a
 * square's fixup, which is what lets a brush stroke derive only the tiles it
 * touched. The levels above the one edited move with it (a height edit on
 * level 0 lifts every floor stacked on that column), which is why
 * Editor_TileDeriveColumn exists beside it.
 *
 * `x`, `z` are square-local (0..63), `level` 0..3. Agreement with the square
 * derive is checked tile for tile by edit_world_test.
 */
void
Editor_TileDerive(
    const struct Editor_Square* square,
    int x,
    int z,
    int level,
    struct ToriRS_MapFloor* out_floor);

/** All four levels of one column into `out_floors[0..3]`. */
void
Editor_TileDeriveColumn(
    const struct Editor_Square* square,
    int x,
    int z,
    struct ToriRS_MapFloor* out_floors);

/**
 * Authored locs -> the renderer's scenery list.
 *
 * A straight copy: unlike heights, loc placements mean on screen exactly what
 * the file says. `out_locs->locs` is allocated here and owned by the caller.
 */
int
Editor_SquareDeriveLocs(
    const struct Editor_Square* square,
    struct ToriRS_MapLocs* out_locs);

#endif
