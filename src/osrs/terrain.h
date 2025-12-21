#ifndef TERRAIN_H
#define TERRAIN_H

#include "configmap.h"
#include "datastruct/hmap.h"
#include "tables/maps.h"

struct TerrainTileModel
{
    int vertex_count;
    int* vertex_x;
    int* vertex_y;
    int* vertex_z;

    int face_count;
    int* faces_a;
    int* faces_b;
    int* faces_c;
    int* valid_faces;

    int* face_texture_ids;
    int* face_texture_u_a;
    int* face_texture_v_a;
    int* face_texture_u_b;
    int* face_texture_v_b;
    int* face_texture_u_c;
    int* face_texture_v_c;

    int* face_color_hsl_a;
    int* face_color_hsl_b;
    int* face_color_hsl_c;

    int chunk_pos_x;
    int chunk_pos_z;
    int chunk_pos_level;
};

struct Terrain
{
    struct TerrainTileModel* tiles;
    int tile_count;

    int side;
};

struct TileHeights
{
    int sw_height;
    int se_height;
    int ne_height;
    int nw_height;
    int height_center;
};

struct TileHeights
terrain_tile_heights_at(struct CacheMapTerrainIter* terrain, int sx, int sz, int level);

struct CacheMapTerrain;
struct Terrain* terrain_new_from_map_terrain(
    struct CacheMapTerrainIter* terrain,
    int* shade_map_nullable,
    struct ConfigMap* config_overlay_map,
    struct ConfigMap* config_underlay_map);

void terrain_free(struct Terrain* terrain);

#endif