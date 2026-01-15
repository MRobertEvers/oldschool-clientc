#ifndef CONFIG_LOCS_H
#define CONFIG_LOCS_H

#include "osrs/cache.h"
#include "osrs/filelist.h"
#include "osrs/tables/configs.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum LocShape
{
    LOC_SHAPE_WALL_SINGLE_SIDE = 0,
    LOC_SHAPE_WALL_TRI_CORNER = 1,
    LOC_SHAPE_WALL_TWO_SIDES = 2,
    LOC_SHAPE_WALL_RECT_CORNER = 3,

    // Inside decor is not moved by wall offset.
    LOC_SHAPE_WALL_DECOR_INSIDE = 4,
    // Outside decor is moved by wall offset.
    LOC_SHAPE_WALL_DECOR_OUTSIDE = 5,
    LOC_SHAPE_WALL_DECOR_DIAGONAL_OUTSIDE = 6,
    LOC_SHAPE_WALL_DECOR_DIAGONAL_INSIDE = 7,
    LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE = 8,

    LOC_SHAPE_WALL_DIAGONAL = 9,

    LOC_SHAPE_SCENERY = 10,
    LOC_SHAPE_SCENERY_DIAGIONAL = 11,

    LOC_SHAPE_ROOF_SLOPED = 12,
    LOC_SHAPE_ROOF_SLOPED_OUTER_CORNER = 13,
    LOC_SHAPE_ROOF_SLOPED_INNER_CORNER = 14,
    LOC_SHAPE_ROOF_SLOPED_HARD_INNER_CORNER = 15,
    LOC_SHAPE_ROOF_SLOPED_HARD_OUTER_CORNER = 16,
    LOC_SHAPE_ROOF_FLAT = 17,
    LOC_SHAPE_ROOF_SLOPED_OVERHANG = 18,
    LOC_SHAPE_ROOF_SLOPED_OVERHANG_OUTER_CORNER = 19,
    LOC_SHAPE_ROOF_SLOPED_OVERHANG_INNER_CORNER = 20,
    LOC_SHAPE_ROOF_SLOPED_OVERHANG_HARD_OUTER_CORNER = 21,

    LOC_SHAPE_FLOOR_DECORATION = 22,
};

enum LocParamType
{
    LOC_PARAM_TYPE_INT = 0,
    LOC_PARAM_TYPE_STRING = 1,
};

struct CacheConfigLocationParam
{
    int type;
    union
    {
        int int_value;
        char* string_value;
    };
};

struct CacheConfigLocation
{
    // Added after loading.
    int _id;

    /**
     * Sometimes multiple models are specified in a single loc config,
     * and the shape_select field of the map loc selects which one to use.
     * E.g. Walls will have multiple angles.
     */
    int* shapes;
    int** models;
    int* lengths;
    int shapes_and_model_count;

    // Null terminated strings
    char* name;
    char* desc;

    int size_x;
    int size_z;

    // TODO: This is blockwalk.
    int clip_type;
    int blocks_projectiles;

    // Both x and z.
    // LostCity/2004Scape/Pazaz-Gang call this wallwidth.
    int wall_width;

    int is_interactive;
    int contoured_ground;
    int contour_ground_type;
    int contour_ground_param;

    // This is merge_normals in rs map viewer
    int sharelight;

    int occlude;

    // Animation
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
    int mirrored;
    int shadowed;

    int resize_x;
    int resize_y;
    int resize_z;

    int map_scene_id;
    int offset_x;
    int offset_y;
    int offset_z;

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
    struct CacheConfigLocationParam* param_values;
};

struct CacheConfigLocation*
config_locs_new_decode(
    char* buffer,
    int buffer_size);
void
config_locs_free(struct CacheConfigLocation* loc);
void
config_locs_free_inplace(struct CacheConfigLocation* loc);

void
config_locs_decode_inplace(
    struct CacheConfigLocation* loc,
    char* data,
    int data_size);

#endif