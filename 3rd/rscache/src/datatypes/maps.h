#ifndef RSCACHE_DATATYPES_MAPS_H
#define RSCACHE_DATATYPES_MAPS_H

#include "../rscache_profile.h"

#include <stdbool.h>
#include <stdint.h>

#define RSCACHE_MAP_TERRAIN_X 64
#define RSCACHE_MAP_TERRAIN_Z 64
#define RSCACHE_MAP_TERRAIN_LEVELS 4
#define RSCACHE_MAP_CHUNK_SIZE 64

#define RSCACHE_MAP_UNITS_LEVEL_HEIGHT 240
#define RSCACHE_MAP_UNITS_TILE_HEIGHT_BASIS 8

#define RSCACHE_MAPREGIONXZ(x, z) ((x) << 8 | (z))

static inline int
RSCache_MapTileCoordToChunkCoord(
    int x,
    int z,
    int level)
{
    return x + (z)*RSCACHE_MAP_TERRAIN_X +
           (level) * (RSCACHE_MAP_TERRAIN_X * RSCACHE_MAP_TERRAIN_Z);
}

#define RSCACHE_MAP_TILE_COORD(x, z, level) (RSCache_MapTileCoordToChunkCoord(x, z, level))

struct RSCache_MapLoc
{
    int loc_id;
    int shape_select;
    int orientation;
    int chunk_pos_x;
    int chunk_pos_z;
    int chunk_pos_level;
};

struct RSCache_MapLocs
{
    int chunk_mapx;
    int chunk_mapz;

    struct RSCache_MapLoc* locs;
    int locs_count;
};

enum RSCache_FloorFlags
{
    RSCACHE_FLOFLAG_BLOCK = 0x01,
    RSCACHE_FLOFLAG_LINK_BELOW = 0x02,
    RSCACHE_FLOFLAG_REMOVE_ROOF = 0x04,
    RSCACHE_FLOFLAG_VIS_BELOW = 0x08,
    RSCACHE_FLOFLAG_FORCE_HIGH_DETAIL = 0x10,
};

static inline int
RSCache_MapFloorVisBelowDrawLevel(
    uint8_t settings_at_cache_level,
    int cache_level,
    int column_has_link_below_l1)
{
    if( (settings_at_cache_level & RSCACHE_FLOFLAG_VIS_BELOW) != 0 )
        return 0;
    if( cache_level > 0 && column_has_link_below_l1 != 0 )
        return cache_level - 1;
    return cache_level;
}

enum RSCache_FloorShape
{
    RSCACHE_FLOOR_SHAPE_NONE = 0,
    RSCACHE_FLOOR_SHAPE_FLAT = 1,
    RSCACHE_FLOOR_SHAPE_DIAGONAL = 2,
    RSCACHE_FLOOR_SHAPE_LEFT_SEMI_DIAGONAL_SMALL = 3,
    RSCACHE_FLOOR_SHAPE_RIGHT_SEMI_DIAGONAL_SMALL = 4,
    RSCACHE_FLOOR_SHAPE_LEFT_SEMI_DIAGONAL_BIG = 5,
    RSCACHE_FLOOR_SHAPE_RIGHT_SEMI_DIAGONAL_BIG = 6,
    RSCACHE_FLOOR_SHAPE_HALF_SQUARE = 7,
    RSCACHE_FLOOR_SHAPE_CORNER_SMALL = 8,
    RSCACHE_FLOOR_SHAPE_CORNER_BIG = 9,
    RSCACHE_FLOOR_SHAPE_FAN_SMALL = 10,
    RSCACHE_FLOOR_SHAPE_FAN_BIG = 11,
    RSCACHE_FLOOR_SHAPE_TRAPEZIUM = 12,
};

struct RSCache_MapFloor
{
    uint16_t overlay_id;
    uint8_t underlay_id;
    int16_t height;
    uint8_t attr_opcode;
    uint8_t settings;
    uint8_t shape;
    uint8_t rotation;

    /*
     * Provenance, so the record can be written back.
     *
     * `height` alone cannot answer "did the source specify a height?" — the
     * post-decode fixup fills it in procedurally wherever it decoded as 0, so
     * "absent" and "authored" are the same state afterwards. An encoder reading
     * only `height` would emit an opcode-1 height for every tile and bake the
     * generated terrain into the file.
     *
     * `height_authored` records that opcode 1 was present, and `authored_height`
     * keeps the raw wire byte. Both are needed rather than just the flag: fixup
     * overwrites `height` when it is 0, so a tile that genuinely specified height 0
     * would otherwise still lose its value.
     *
     * These are set on decode whether or not fixup runs, so one encoder handles a
     * raw stream and a fixed-up one identically.
     */
    uint8_t height_authored;
    uint8_t authored_height;
};

struct RSCache_MapTerrain
{
    bool is_fixedup;
    int map_x;
    int map_z;
    struct RSCache_MapFloor
        tiles_xyz[RSCACHE_MAP_TERRAIN_X * RSCACHE_MAP_TERRAIN_Z * RSCACHE_MAP_TERRAIN_LEVELS];
};

#define RSCACHE_CHUNK_TILE_COUNT                                                                   \
    ((RSCACHE_MAP_TERRAIN_X * RSCACHE_MAP_TERRAIN_Z * RSCACHE_MAP_TERRAIN_LEVELS))

/*
 * Terrain decode flags. A bitmask — test with `&`, not `==`.
 *
 * The width bit is an **era** difference, not a container one: the tile attribute
 * opcode and overlay id are u8 everywhere until OldSchool revision 209 widens them
 * to u16. dat1 is narrow, and so is a pre-209 dat2 cache — including the whole
 * 643/RS2 branch, whose revision number is larger than 209 but is not on the
 * OldSchool line. Ask RSCache_MapTerrainFlags rather than inferring it.
 */
/** u16 attribute/overlay widths (OldSchool rev 209+). Zero, so it is the default. */
#define RSCACHE_MAP_TERRAIN_DECODE_U16 0
/** u8 attribute/overlay widths (dat1, and any pre-209 dat2 cache). */
#define RSCACHE_MAP_TERRAIN_DECODE_U8 1
/**
 * Skip the post-decode fixup.
 *
 * The fixup generates a height for every tile that decoded without one — from
 * Perlin noise at level 0, derived from the level below above that — which is what
 * the renderer wants but means the terrain it produces is not what the file said.
 * Pass this to get the stream exactly as stored.
 *
 * Not required for encoding: the decode records `height_authored` either way. This
 * exists for callers that want the untouched values, e.g. to compare two caches.
 */
#define RSCACHE_MAP_TERRAIN_DECODE_NO_FIXUP 2

/** Terrain decode flags for this cache. Replaces callers hardcoding the width
 *  from whichever provider they happen to live in. */
int
RSCache_MapTerrainFlags(const struct RSCache* cache);

struct RSCache_Dat2Disk;
struct RSCache_Dat2DiskArchive;

struct RSCache_MapTerrain*
RSCache_MapTerrainNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int map_x,
    int map_z);

struct RSCache_Dat2DiskArchive*
RSCache_MapTerrainArchiveNewLoad(
    struct RSCache_Dat2Disk* cache,
    int map_x,
    int map_z);

struct RSCache_MapTerrain*
RSCache_MapTerrainNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int map_x,
    int map_z);

/** As above, but takes the tile widths from the cache profile instead of assuming
 *  the modern OSRS ones. Prefer this: a pre-209 dat2 square decodes as void under
 *  the wide layout (see RSCache_MapTerrainFlags). `cache` may be NULL, which keeps
 *  the legacy assumption. */
struct RSCache_MapTerrain*
RSCache_MapTerrainNewFromArchiveProfile(
    struct RSCache_Dat2DiskArchive* archive,
    int map_x,
    int map_z,
    const struct RSCache* cache);

struct RSCache_MapTerrain*
RSCache_MapTerrainNewFromDecodeFlags(
    char* data,
    int data_size,
    int map_x,
    int map_z,
    int flags);

/**
 * Encode map-square terrain.
 *
 * Works on a terrain decoded either way — raw (RSCACHE_MAP_TERRAIN_DECODE_NO_FIXUP)
 * or fixed up — because the decode records height provenance in
 * `height_authored` / `authored_height` rather than leaving the encoder to guess
 * from `height`. Guessing is not possible: the fixup rewrites `height` for *every*
 * tile, procedurally where the file gave none and scaled by the tile-height basis
 * where it did, so nothing writable survives in that field.
 *
 * Pass the same width flag the decode used — the tile attribute and overlay id are
 * u8 in dat1 and u16 in dat2, and using the wrong width shifts every tile.
 *
 * `shape` and `rotation` are not written because both derive from `attr_opcode`,
 * which is stored verbatim.
 *
 * Byte-exactness depends only on the order the source listed a tile's attributes in:
 * this writes overlay, settings, underlay, then the terminator, and the decoder
 * accepts them in any order.
 *
 * Returns bytes written, or 0 on failure.
 */
uint32_t
RSCache_MapTerrainEncode(
    const struct RSCache_MapTerrain* map_terrain,
    int flags,
    uint8_t* out,
    uint32_t out_capacity);

/** Worst-case output size for RSCache_MapTerrainEncode at the given width flag. */
uint32_t
RSCache_MapTerrainEncodeBound(int flags);

struct RSCache_MapTerrain*
RSCache_MapTerrainNewDecode(
    char* data,
    int data_size,
    int map_x,
    int map_z);

void
RSCache_MapTerrainFree(struct RSCache_MapTerrain* map_terrain);

/**
 * Are this cache's map (lX_Z) archives XTEA-encrypted?
 *
 * - OldSchool: encrypted below revision 237; plain from then on (no keys shipped).
 * - RS2 dat2: encrypted from revision 414 (when that lineage adopted map XTEA;
 *   the container itself moved to dat2 shortly after ~377). 643 is well past.
 * - Dat1: no dat2 map table — returns false.
 *
 * Not answerable from the key file: an absent key on a keyed cache is a missing
 * square; an absent key on an unencrypted cache is ordinary. Applying a key to
 * plain data corrupts as thoroughly as omitting one from encrypted data.
 */
bool
RSCache_MapLocsEncrypted(const struct RSCache* cache);

struct RSCache_MapLocs*
RSCache_MapLocsNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int map_x,
    int map_z);

struct RSCache_MapLocs*
RSCache_MapLocsNewDecode(
    char* data,
    int data_size);

void
RSCache_MapLocsFree(struct RSCache_MapLocs* map_locs);

struct RSCache_Dat2DiskArchive*
RSCache_MapLocsArchiveNewLoad(
    struct RSCache_Dat2Disk* cache,
    int map_x,
    int map_z);

#endif
