#ifndef GAMECACHE_SCENERY_CONFIG_H
#define GAMECACHE_SCENERY_CONFIG_H

#include "osrs/rscache/rsbuf.h"

#include <stdbool.h>

struct CacheConfigLocation;

struct GameCache_SceneryConfig
{
    int _id;

    int* shapes;
    int** models;
    int* lengths;
    int shapes_and_model_count;

    char* name;
    char* desc;

    int size_x;
    int size_z;

    int blocks_walk;
    int blocks_projectiles;
    int wall_width;
    int is_interactive;
    int contoured_ground;
    int contour_ground_type;
    int contour_ground_param;
    int sharelight;
    int occlude;
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

    int map_function_id;
    int mirrored;
    int shadowed;
    int resize_x;
    int resize_height;
    int resize_z;
    int map_scene_id;
    int offset_x;
    int offset_y;
    int offset_z;
    int obstructs_ground;
    int break_routefinding;
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
    bool seq_random_start;
    int* random_seq_ids;
    int* random_seq_delays;
    int random_seq_id_count;
    int* campaign_ids;
    int campaign_id_count;

    struct Params param_values;
};

struct GameCache_SceneryConfig*
gamecache_scenery_config_new_from_cache_config_location(struct CacheConfigLocation* config_loc);

void
gamecache_scenery_config_free(struct GameCache_SceneryConfig* config);

#endif
