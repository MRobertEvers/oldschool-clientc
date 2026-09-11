#ifndef SRC_EDITOR_EDITOR_TYPES_H
#define SRC_EDITOR_EDITOR_TYPES_H

/**
 * The map editor's document types: what a map square looks like in memory
 * while it is being edited.
 *
 * The one idea this file exists to enforce is the split between what the file
 * SAYS and what the renderer USES. They are not the same values, and keeping
 * only one of them loses the other irrecoverably:
 *
 *   - A tile with no `h` token gets a height from Perlin noise at level 0, or
 *     from the level below above that (see fixup_terrain in rscache's maps.c).
 *     Every tile comes out of that with a height, so "absent" and "authored"
 *     are indistinguishable afterwards.
 *   - Even an authored height is rewritten: the fixup scales it by the tile
 *     height basis and, above level 0, adds the level below. So the renderer's
 *     `height` field holds nothing that can be written back.
 *
 * `struct Editor_Tile` is therefore the AUTHORED record — the tokens as the
 * file spells them, absence included. The derived values live where they
 * always did, in the `ToriRS_MapTerrain` the provider hands the world builder;
 * the editor regenerates that from the authored record whenever it changes
 * (Editor_SquareDerive), so the two can never drift.
 *
 * Rule for callers: display derived, edit authored, save authored.
 */

#include <stddef.h>
#include <stdint.h>

/* One map square. Matches RSCACHE_MAP_TERRAIN_* / TORIRS_MAP_TERRAIN_*; spelled
 * here so the document types do not drag either header into every consumer. */
#define EDITOR_SQUARE_X 64
#define EDITOR_SQUARE_Z 64
#define EDITOR_SQUARE_LEVELS 4
#define EDITOR_SQUARE_TILES (EDITOR_SQUARE_X * EDITOR_SQUARE_Z * EDITOR_SQUARE_LEVELS)

/** Tile index. Level-major, then x, then z — the order the file lists them and
 *  the order RSCACHE_MAP_TILE_COORD uses, so a re-encode is byte-exact. */
static inline int
Editor_TileIndex(
    int x,
    int z,
    int level)
{
    return (level * EDITOR_SQUARE_Z + z) * EDITOR_SQUARE_X + x;
}

/**
 * The authored record for one tile: exactly the tokens on its `.jm2` line.
 *
 * A tile with no tokens at all has no line in the file, and every field here
 * zero. That is the common case — an all-default tile is absent from the
 * source, and emitting one back would grow the file.
 */
struct Editor_Tile
{
    /** The `h` token was present. Distinct from `height != 0`: the format
     *  spells an explicit zero as `h1` (the fixup maps 1 back to 0), so a raw
     *  zero here with the flag set is not a state the file can hold. */
    uint8_t has_height;
    /** The raw byte after `h`, unscaled. Meaningless unless has_height. */
    uint8_t height;

    /** The `o` token was present, i.e. the tile carries an overlay. Separate
     *  from `overlay_id != 0`: overlay id 0 with a shape is legal and means
     *  "shape cut out of the underlay", which is not the same as no overlay. */
    uint8_t has_overlay;
    uint16_t overlay_id;
    uint8_t shape;
    uint8_t rotation;

    /** The `f` token: the tile settings bitfield (1 block, 2 link-below,
     *  4 remove-roof, 8 vis-below). 0 = no token. */
    uint8_t settings;

    /** The `u` token. 0 = no token. */
    uint8_t underlay_id;
};

/** One scenery placement, as a `.jl2` line: `level x z: id shape [rotation]`. */
struct Editor_Loc
{
    int loc_id;
    int shape;
    int rotation;
    int level;
    int x;
    int z;
};

/**
 * A loaded map square, authored form.
 *
 * `trailing` and `foreign` are the two pieces the editor carries but never
 * interprets, so that a square it saves differs from the one it loaded only
 * where the user edited:
 *
 *   - `trailing` is the bytes past the last tile in the encoded stream. Nothing
 *     reads them, but 1,612 of 2,934 OldSchool 239 squares have a non-zero one
 *     and a re-encode without it is short.
 *   - `foreign` is every `.jm2` section after `==== MAP ====` — the server's
 *     `==== NPC ====` and `==== OBJ ====` spawns. Those belong to the content
 *     layer, not the cache, and the editor is not their editor.
 */
struct Editor_Square
{
    int map_x;
    int map_z;
    /** 1 once the square's text has been parsed into this record. */
    int loaded;

    struct Editor_Tile tiles[EDITOR_SQUARE_TILES];

    struct Editor_Loc* locs;
    int loc_count;
    int loc_capacity;

    /** Owned. Hex-decoded from the `trailing=` line. */
    uint8_t* trailing;
    int trailing_size;

    /** Owned, NUL-terminated. The `.jm2` text from the first foreign section
     *  header to end of file, verbatim. NULL when the file had none. */
    char* foreign;

    /** Set by any edit, cleared by a successful save. Tracked separately
     *  because the two halves are two files: painting a tile must not rewrite
     *  the square's `.jl2` and re-order its locs. */
    int dirty_map;
    int dirty_loc;
};

#endif
