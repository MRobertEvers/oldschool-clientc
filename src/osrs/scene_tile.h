#ifndef SCENE_TILE_H
#define SCENE_TILE_H

#include "tables/maps.h"

struct SceneTile
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
    int chunk_pos_y;
    int chunk_pos_level;
};

struct CacheConfigOverlay;
struct CacheConfigUnderlay;
struct CacheMapTerrain;
struct Cache;

struct SceneTile* scene_tiles_new_from_map_terrain_cache(
    struct CacheMapTerrain* map_terrain, int* shade_map_nullable, struct Cache* cache);

struct SceneTile* scene_tiles_new_from_map_terrain(
    struct CacheMapTerrain* map_terrain,
    int* shade_map_nullable,
    struct CacheConfigOverlay* overlays,
    int* overlay_ids,
    int overlays_count,
    struct CacheConfigUnderlay* underlays,
    int* underlay_ids,
    int underlays_count);

void free_tiles(struct SceneTile* tiles, int tile_count);

#endif