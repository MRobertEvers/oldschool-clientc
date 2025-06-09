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

    // deprecated
    int* face_color_hsl;

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
};

struct SceneTile* parse_tiles_data(const char* filename, int* tile_count);

struct Overlay;
struct Underlay;
struct MapTerrain;

struct SceneTile* scene_tiles_new_from_map_terrain(
    struct MapTerrain* map_terrain,
    struct Overlay* overlays,
    int* overlay_ids,
    int overlays_count,
    struct Underlay* underlays,
    int* underlay_ids,
    int underlays_count);

void free_tiles(struct SceneTile* tiles, int tile_count);

#endif