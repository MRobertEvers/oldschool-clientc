#ifndef MAPS_H
#define MAPS_H

#include "../cache.h"
#include "../rsbuf.h"

#include <assert.h>

#define MAP_TERRAIN_X 64
#define MAP_TERRAIN_Z 64
#define MAP_TERRAIN_LEVELS 4
#define MAP_CHUNK_SIZE 64

// From meteor-client deob and from rs map viewer
#define MAP_UNITS_LEVEL_HEIGHT 240
#define MAP_UNITS_TILE_HEIGHT_BASIS 8

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

enum FloorFlags
{
    FLOFLAG_SOLID_UNKNWON = 0 << 1,
    FLOFLAG_BRIDGE = 1 << 1
};

struct CacheMapFloor
{
    int height;
    int attr_opcode;
    int settings;
    int overlay_id;
    int shape;
    int rotation;
    int underlay_id;
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

struct CacheMapTerrain* //
map_terrain_new_from_decode( //
    char* data, int data_size, int map_x, int map_y);

struct CacheMapLocs* //
map_locs_new_from_cache(
    struct Cache* cache,
    int map_x,
    int map_y);

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

struct CacheMapLoc* //
map_locs_iter_next( //
    struct CacheMapLocsIter* iter, //
    struct ChunkOffset* out_offset_nullable);

#endif