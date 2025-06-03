#ifndef SCENE_TILE_H
#define SCENE_TILE_H

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

    int* face_color_hsl;
};

struct SceneTile* parse_tiles_data(const char* filename, int* tile_count);

void free_tiles(struct SceneTile* tiles, int tile_count);

#endif