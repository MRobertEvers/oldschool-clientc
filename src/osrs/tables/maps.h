#ifndef MAPS_H
#define MAPS_H

#define MAP_TERRAIN_X 64
#define MAP_TERRAIN_Y 64
#define MAP_TERRAIN_Z 4
#define MAP_CHUNK_SIZE 64

// From meteor-client deob and from rs map viewer
#define MAP_UNITS_LEVEL_HEIGHT 240
#define MAP_UNITS_TILE_HEIGHT_BASIS 8

#define MAP_TILE_COORD(x, y, z) (y + (x) * MAP_TERRAIN_X + (z) * MAP_TERRAIN_X * MAP_TERRAIN_Y)

struct MapLoc
{
    int id;
    int type;
    int orientation;
    int chunk_pos_x;
    int chunk_pos_y;
    int chunk_pos_level;
};

struct MapLocs
{
    struct MapLoc* locs;
    int locs_count;
};

struct MapTile
{
    int height;
    int attr_opcode;
    int settings;
    int overlay_id;
    int shape;
    int rotation;
    int underlay_id;
};

struct MapTerrain
{
    int map_x;
    int map_y;
    struct MapTile tiles_xyz[MAP_TERRAIN_X * MAP_TERRAIN_Y * MAP_TERRAIN_Z];
};

#define MAP_TILE_COUNT ((MAP_TERRAIN_X * MAP_TERRAIN_Y * MAP_TERRAIN_Z))

struct Cache;
struct MapTerrain* map_terrain_new_from_cache(struct Cache* cache, int map_x, int map_y);
struct MapTerrain* map_terrain_new_from_decode(char* data, int data_size);

struct MapLocs* map_locs_new_from_cache(struct Cache* cache, int map_x, int map_y);
struct MapLocs* map_locs_new_from_decode(char* data, int data_size);

void map_terrain_free(struct MapTerrain* map_terrain);
void map_locs_free(struct MapLocs* map_locs);

#endif