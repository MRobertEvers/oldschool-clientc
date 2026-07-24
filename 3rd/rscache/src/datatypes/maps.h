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

/** Terrain tile attribute / overlay id width. Purely a container difference:
 *  jagfile-era map squares store these as u8, js5-era ones as u16. */
#define RSCACHE_MAP_TERRAIN_DECODE_U16 0
#define RSCACHE_MAP_TERRAIN_DECODE_U8 1

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

struct RSCache_MapTerrain*
RSCache_MapTerrainNewFromDecodeFlags(
    char* data,
    int data_size,
    int map_x,
    int map_z,
    int flags);

struct RSCache_MapTerrain*
RSCache_MapTerrainNewDecode(
    char* data,
    int data_size,
    int map_x,
    int map_z);

void
RSCache_MapTerrainFree(struct RSCache_MapTerrain* map_terrain);

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
