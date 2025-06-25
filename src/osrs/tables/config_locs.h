#ifndef CONFIG_LOCS_H
#define CONFIG_LOCS_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum LocParamType
{
    LOC_PARAM_TYPE_INT = 0,
    LOC_PARAM_TYPE_STRING = 1,
};

struct LocParam
{
    int type;
    union
    {
        int int_value;
        char* string_value;
    };
};

struct Loc
{
    int* types;
    int** models;

    char* name;
    char* desc;

    int size_x;
    int size_y;

    int clip_type;
    int blocks_projectiles;

    int is_interactive;
    int contoured_ground;
    int contour_ground_type;
    int contour_ground_param;

    int merge_normals;

    int model_clipped;

    int seq_id;

    int ambient;
    int contrast;

    // Menu operations - null terminated strings
    char* actions[10];

    int* recolors_from;
    int* recolors_to;
    int recolor_count;

    int* retextures_from;
    int* retextures_to;
    int retexture_count;

    int map_function_id;
    int rotated;
    int clipped;

    int model_size_x;
    int model_size_y;
    int model_size_height;

    int map_scene_id;
    int offset_x;
    int offset_y;
    int offset_height;

    int obstructs_ground;
    int is_hollow;
    int support_items;

    int transform_varbit;
    int transform_varp;
    int* transforms;
    int transform_count;

    int ambient_sound_id;
    int ambient_sound_distance;
    int ambient_sound_retain;

    int ambient_sound_ticks_min;
    int ambient_sound_ticks_max;

    int* ambient_sound_ids;
    int ambient_sound_id_count;

    int seq_random_start;

    int* random_seq_ids;
    int* random_seq_delays;
    int random_seq_id_count;

    int* campaign_ids;
    int campaign_id_count;

    int param_count;
    int* param_keys;
    struct LocParam* param_values;
};

struct Loc* config_locs_new_decode(char* buffer, int buffer_size);
void config_locs_free(struct Loc* loc);

void decode_loc(struct Loc* loc, char* buffer, int buffer_size);
void free_loc(struct Loc* loc);

#endif