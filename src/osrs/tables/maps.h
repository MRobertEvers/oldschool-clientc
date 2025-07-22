#ifndef MAPS_H
#define MAPS_H

#include "osrs/cache.h"
#include "osrs/rsbuf.h"

#include <assert.h>

#define MAP_TERRAIN_X 64
#define MAP_TERRAIN_Y 64
#define MAP_TERRAIN_Z 4
#define MAP_CHUNK_SIZE 64

// From meteor-client deob and from rs map viewer
#define MAP_UNITS_LEVEL_HEIGHT 240
#define MAP_UNITS_TILE_HEIGHT_BASIS 8

static int
map_tile_coord_to_chunk_coord(int x, int y, int z)
{
    assert(x >= 0);
    assert(y >= 0);
    assert(z >= 0);
    assert(x < MAP_TERRAIN_X);
    assert(y < MAP_TERRAIN_Y);
    assert(z < MAP_TERRAIN_Z);

    return y + (x)*MAP_TERRAIN_X + (z)*MAP_TERRAIN_X * MAP_TERRAIN_Y;
}

// #define MAP_TILE_COORD(x, y, z) (y + (x) * MAP_TERRAIN_X + (z) * MAP_TERRAIN_X * MAP_TERRAIN_Y)
#define MAP_TILE_COORD(x, y, z) (map_tile_coord_to_chunk_coord(x, y, z))

struct CacheMapLoc
{
    int loc_id;
    /**
     * Some locs have multiple models associated with them.
     * (See op code 1), if that is the case, this field selects which model to use.
     *
     * For example, locs with walls usually have many models associated with them, e.g. diagonal,
     * horizontal etc. and the shape_select field selects which one to use.
     */
    int shape_select;
    int orientation;
    int chunk_pos_x;
    int chunk_pos_y;
    int chunk_pos_level;
};

struct CacheMapLocs
{
    struct CacheMapLoc* locs;
    int locs_count;
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
    int map_x;
    int map_y;
    struct CacheMapFloor tiles_xyz[MAP_TERRAIN_X * MAP_TERRAIN_Y * MAP_TERRAIN_Z];
};

#define MAP_TILE_COUNT ((MAP_TERRAIN_X * MAP_TERRAIN_Y * MAP_TERRAIN_Z))

struct CacheMapTerrain* map_terrain_new_from_cache(struct Cache* cache, int map_x, int map_y);
struct CacheMapTerrain* map_terrain_new_from_decode(char* data, int data_size);

struct CacheMapLocs* map_locs_new_from_cache(struct Cache* cache, int map_x, int map_y);
struct CacheMapLocs* map_locs_new_from_decode(char* data, int data_size);

void map_terrain_free(struct CacheMapTerrain* map_terrain);
void map_locs_free(struct CacheMapLocs* map_locs);

struct CacheMapLocsIter
{
    int is_populated;
    struct CacheMapLoc value;

    struct CacheArchive* _archive;
    struct RSBuffer _buffer;

    int _pos;
    int _id;
    int _state;
};

struct CacheMapLocsIter* map_locs_iter_new(struct Cache* cache, int map_x, int map_y);
void map_locs_iter_free(struct CacheMapLocsIter* iter);

struct CacheMapLoc* map_locs_iter_next(struct CacheMapLocsIter* iter);

#endif