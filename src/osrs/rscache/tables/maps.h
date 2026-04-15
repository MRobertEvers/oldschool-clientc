#ifndef MAPS_H
#define MAPS_H

#include "../cache.h"
#include "../cache_dat.h"
#include "../rsbuf.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#define MAP_TERRAIN_X 64
#define MAP_TERRAIN_Z 64
#define MAP_TERRAIN_LEVELS 4
#define MAP_CHUNK_SIZE 64

// From meteor-client deob and from rs map viewer
#define MAP_UNITS_LEVEL_HEIGHT 240
#define MAP_UNITS_TILE_HEIGHT_BASIS 8

#define MAPREGIONXZ(x, z) ((x) << 8 | (z))

static inline int
map_tile_coord_to_chunk_coord(
    int x,
    int z,
    int level)
{
    // assert(x >= 0);
    // assert(y >= 0);
    // assert(z >= 0);
    // assert(x < MAP_TERRAIN_X);
    // assert(y < MAP_TERRAIN_Y);
    // assert(z < MAP_TERRAIN_Z);

    return x + (z)*MAP_TERRAIN_X + (level) * (MAP_TERRAIN_X * MAP_TERRAIN_Z);
}

// #define MAP_TILE_COORD(x, y, z) (y + (x) * MAP_TERRAIN_X + (z) * MAP_TERRAIN_X * MAP_TERRAIN_Y)
#define MAP_TILE_COORD(x, z, level) (map_tile_coord_to_chunk_coord(x, z, level))

struct CacheMapLoc
{
    // This is the config id.
    int loc_id;
    /**
     * Some locs have multiple models associated with them.
     * (See op code 1), if that is the case, this field selects which model to use.
     *
     * For example, locs with walls usually have many models associated with them, e.g. diagonal,
     * horizontal etc. and the shape_select field selects which one to use.
     */
    int shape_select;

    /**
     * For models with no animations (or other transforms), this causes the model to be
     * pre-rotated before rendering, i.e. during the creation of the scene.
     *
     * For models that have a sequence, the animation is applied and the model is rotated
     * during rendering.
     *
     * This is why the render functions always take an additional "yaw" parameter.
     */
    int orientation;
    int chunk_pos_x;
    int chunk_pos_z;
    int chunk_pos_level;
};

struct CacheMapLocs
{
    int _chunk_mapx;
    int _chunk_mapz;

    struct CacheMapLoc* locs;
    int locs_count;
};

/**
 * Per-tile flags from cache terrain (CacheMapFloor.settings).
 * Names and semantics align with Client-TS MapFlag (Client-TS/src/dash3d/MapFlag.ts) and
 * ClientBuild.getVisBelowLevel() (Client-TS/src/dash3d/ClientBuild.ts).
 *
 * Build-time use in 3draster:
 *   - Collision: FLOFLAG_BLOCK.
 *   - Bridge push-down: FLOFLAG_LINK_BELOW on cache level 1 (same column).
 *   - Painter draw level (slevel in packed_meta): map_floor_vis_below_draw_level() from the
 *     cache floor that *contributed* content to each painter grid slot after push-down.
 *
 * The painter does not read these flags at draw time; only pre-computed slevel matters.
 */
enum FloorFlags
{
    /**
     * FLOFLAG_BLOCK (0x01) -- Client-TS MapFlag.Block
     *
     * Ground tile blocks walking (collision map marks the tile impassable).
     * No effect on painter draw level.
     */
    FLOFLAG_BLOCK = 0x01,

    /**
     * FLOFLAG_LINK_BELOW (0x02) -- Client-TS MapFlag.LinkBelow
     *
     * When set on cache level 1 at (x,z), the entire column is push-down'd in the painter:
     *   cache 1 -> grid 0, cache 2 -> grid 1, cache 3 -> grid 2, old grid 0 -> bridge slot
     *   (grid 3) with PAINTERS_TILE_FLAG_BRIDGE, drawn via bridge_tile before the surface tile.
     *
     * For draw-level math (before/after push), if this bit is set on level 1, every cache
     * level L > 0 is treated as one draw level lower unless FLOFLAG_VIS_BELOW overrides on
     * that specific cache tile (see map_floor_vis_below_draw_level).
     */
    FLOFLAG_LINK_BELOW = 0x02,

    /**
     * FLOFLAG_REMOVE_ROOF (0x04) -- Client-TS MapFlag.RemoveRoof
     *
     * Client uses this for roof culling (hide upper roofs when camera/player is under a
     * "remove roof" tile). Does not affect painter slevel or tile inclusion in 3draster.
     */
    FLOFLAG_REMOVE_ROOF = 0x04,

    /**
     * FLOFLAG_VIS_BELOW (0x08) -- Client-TS MapFlag.VisBelow
     *
     * On this cache tile's own level, forces painter draw level (slevel) to 0 so the tile
     * and all attached locs participate when max draw level is 0. Overrides the LinkBelow
     * "level - 1" reduction for that tile only.
     *
     * Matches: if ((mapl[level][x][z] & VisBelow) !== 0) return 0; in getVisBelowLevel().
     */
    FLOFLAG_VIS_BELOW = 0x08,

    /**
     * FLOFLAG_FORCE_HIGH_DETAIL (0x10) -- Client-TS MapFlag.ForceHighDetail
     *
     * In client low-memory mode, tiles with this flag skip ground/loc loading. 3draster has
     * no low-memory path; flag is unused at runtime here.
     */
    FLOFLAG_FORCE_HIGH_DETAIL = 0x10,
};

/**
 * ClientBuild.getVisBelowLevel(level, stx, stz): draw level stored on each Square before
 * pushDown. We apply the same formula to cache *source* level when assigning painter slevel
 * after bridge layout is known.
 *
 * @param settings_at_cache_level  CacheMapFloor.settings for the source cache level.
 * @param cache_level                Source terrain level (0..MAP_TERRAIN_LEVELS-1).
 * @param column_has_link_below_l1  Non-zero if (mapl[1][x][z] & LinkBelow) at this column.
 */
static inline int
map_floor_vis_below_draw_level(
    uint8_t settings_at_cache_level,
    int cache_level,
    int column_has_link_below_l1)
{
    if( (settings_at_cache_level & FLOFLAG_VIS_BELOW) != 0 )
        return 0;
    if( cache_level > 0 && column_has_link_below_l1 != 0 )
        return cache_level - 1;
    return cache_level;
}

// { 1, 3, 5, 7 },
// { 1, 3, 5, 7 }, // PLAIN_SHAPE
// { 1, 3, 5, 7 }, // DIAGONAL_SHAPE
// { 1, 3, 5, 7, 6 }, // LEFT_SEMI_DIAGONAL_SMALL_SHAPE
// { 1, 3, 5, 7, 6 }, // RIGHT_SEMI_DIAGONAL_SMALL_SHAPE
// { 1, 3, 5, 7, 6 }, // LEFT_SEMI_DIAGONAL_BIG_SHAPE
// { 1, 3, 5, 7, 6 }, // RIGHT_SEMI_DIAGONAL_BIG_SHAPE
// { 1, 3, 5, 7, 2, 6 }, // HALF_SQUARE_SHAPE
// { 1, 3, 5, 7, 2, 8 }, // CORNER_SMALL_SHAPE
// { 1, 3, 5, 7, 2, 8 }, // CORNER_BIG_SHAPE
// { 1, 3, 5, 7, 11, 12 }, // FAN_SMALL_SHAPE
// { 1, 3, 5, 7, 11, 12 }, // FAN_BIG_SHAPE
// { 1, 3, 5, 7, 13, 14 } // TRAPEZIUM_SHAPE

enum FloorShape
{
    FLOOR_SHAPE_NONE = 0,
    FLOOR_SHAPE_FLAT = 1,
    FLOOR_SHAPE_DIAGONAL = 2,
    FLOOR_SHAPE_LEFT_SEMI_DIAGONAL_SMALL = 3,
    FLOOR_SHAPE_RIGHT_SEMI_DIAGONAL_SMALL = 4,
    FLOOR_SHAPE_LEFT_SEMI_DIAGONAL_BIG = 5,
    FLOOR_SHAPE_RIGHT_SEMI_DIAGONAL_BIG = 6,
    FLOOR_SHAPE_HALF_SQUARE = 7,
    FLOOR_SHAPE_CORNER_SMALL = 8,
    FLOOR_SHAPE_CORNER_BIG = 9,
    FLOOR_SHAPE_FAN_SMALL = 10,
    FLOOR_SHAPE_FAN_BIG = 11,
    FLOOR_SHAPE_TRAPEZIUM = 12,
};

struct CacheMapFloor
{
    uint16_t overlay_id;
    uint8_t underlay_id;
    int16_t height;
    uint8_t attr_opcode;
    // enum FloorFlags
    uint8_t settings;
    // enum FloorShape
    uint8_t shape;
    uint8_t rotation;
};

struct CacheMapTerrain
{
    bool _is_fixedup;
    int map_x;
    int map_z;
    struct CacheMapFloor tiles_xyz[MAP_TERRAIN_X * MAP_TERRAIN_Z * MAP_TERRAIN_LEVELS];
};

#define CHUNK_TILE_COUNT ((MAP_TERRAIN_X * MAP_TERRAIN_Z * MAP_TERRAIN_LEVELS))

struct CacheMapTerrain* //
map_terrain_new_from_cache( //
    struct Cache* cache, int map_x, int map_y);

struct CacheArchive* //
map_terrain_archive_new_load( //
    struct Cache* cache, int map_x, int map_y);

struct CacheMapTerrain*
map_terrain_new_from_archive( //
    struct CacheArchive* archive, int map_x, int map_y);

#define MAP_TERRAIN_DECODE_U16 0
#define MAP_TERRAIN_DECODE_U8 1

struct CacheMapTerrain* //
map_terrain_new_from_decode_flags( //
    char* data, int data_size, int map_x, int map_z, int flags);

struct CacheMapTerrain* //
map_terrain_new_from_decode( //
    char* data, int data_size, int map_x, int map_z);

struct CacheMapLocs* //
map_locs_new_from_cache(
    struct Cache* cache,
    int map_x,
    int map_z);

struct CacheMapLocs* //
map_locs_new_from_decode(
    char* data,
    int data_size);

void //
map_terrain_free(struct CacheMapTerrain* map_terrain);

void //
map_locs_free(struct CacheMapLocs* map_locs);

struct CacheArchive* //
map_locs_archive_new_load(
    struct Cache* cache, //
    int map_x,
    int map_z);

struct ChunkOffset
{
    int x;
    int z;
};

#endif
