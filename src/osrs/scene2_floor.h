#ifndef SCENE2_FLOOR_H
#define SCENE2_FLOOR_H

#include "tables/maps.h"
#include "world.h"

struct Scene2Floor
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

struct Scene2Floor* scene2_floor_new_from_map_terrain(struct Floor* floors, struct Cache* cache);

void scene2_floor_free(struct Scene2Floor* floor);

#endif