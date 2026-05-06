#ifndef GAMECACHE_LOC_H
#define GAMECACHE_LOC_H

/** Standalone GameCache loc-config type -- only fields consumed by world/entity build. */
struct GameCacheLoc
{
    char* name;
    char* desc;

    int size_x;
    int size_z;

    int blocks_walk;
    int blocks_projectiles;

    int wall_width;

    int is_interactive;

    int contour_ground_type;
    int contour_ground_param;

    int sharelight;

    int seq_id;

    int ambient;
    int contrast;

    char* actions[10];

    int* recolors_from;
    int* recolors_to;
    int recolor_count;

    int* retextures_from;
    int* retextures_to;
    int retexture_count;

    int mirrored;
    int shadowed;

    int resize_x;
    int resize_height;
    int resize_z;

    int map_scene_id;
    int offset_x;
    int offset_y;
    int offset_z;

    int* shapes;
    int** models;
    int* lengths;
    int shapes_and_model_count;
};

#endif
