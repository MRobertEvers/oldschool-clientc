#include "gamecache_scenery_config.h"

#include "osrs/rscache/tables/config_locs.h"

#include <stdlib.h>
#include <string.h>

struct GameCache_SceneryConfig*
gamecache_scenery_config_new_from_cache_config_location(struct CacheConfigLocation* loc)
{
    if( !loc )
        return NULL;

    struct GameCache_SceneryConfig* config = malloc(sizeof(struct GameCache_SceneryConfig));
    if( !config )
        return NULL;

    memset(config, 0, sizeof(struct GameCache_SceneryConfig));

    config->_id = loc->_id;
    config->shapes = loc->shapes;
    config->models = loc->models;
    config->lengths = loc->lengths;
    config->shapes_and_model_count = loc->shapes_and_model_count;
    config->name = loc->name;
    config->desc = loc->desc;
    config->size_x = loc->size_x;
    config->size_z = loc->size_z;
    config->blocks_walk = loc->blocks_walk;
    config->blocks_projectiles = loc->blocks_projectiles;
    config->wall_width = loc->wall_width;
    config->is_interactive = loc->is_interactive;
    config->contoured_ground = loc->contoured_ground;
    config->contour_ground_type = loc->contour_ground_type;
    config->contour_ground_param = loc->contour_ground_param;
    config->sharelight = loc->sharelight;
    config->occlude = loc->occlude;
    config->seq_id = loc->seq_id;
    config->ambient = loc->ambient;
    config->contrast = loc->contrast;
    memcpy(config->actions, loc->actions, sizeof(config->actions));
    config->recolors_from = loc->recolors_from;
    config->recolors_to = loc->recolors_to;
    config->recolor_count = loc->recolor_count;
    config->retextures_from = loc->retextures_from;
    config->retextures_to = loc->retextures_to;
    config->retexture_count = loc->retexture_count;
    config->map_function_id = loc->map_function_id;
    config->mirrored = loc->mirrored;
    config->shadowed = loc->shadowed;
    config->resize_x = loc->resize_x;
    config->resize_height = loc->resize_height;
    config->resize_z = loc->resize_z;
    config->map_scene_id = loc->map_scene_id;
    config->offset_x = loc->offset_x;
    config->offset_y = loc->offset_y;
    config->offset_z = loc->offset_z;
    config->obstructs_ground = loc->obstructs_ground;
    config->break_routefinding = loc->break_routefinding;
    config->support_items = loc->support_items;
    config->transform_varbit = loc->transform_varbit;
    config->transform_varp = loc->transform_varp;
    config->transforms = loc->transforms;
    config->transform_count = loc->transform_count;
    config->ambient_sound_id = loc->ambient_sound_id;
    config->ambient_sound_distance = loc->ambient_sound_distance;
    config->ambient_sound_retain = loc->ambient_sound_retain;
    config->ambient_sound_ticks_min = loc->ambient_sound_ticks_min;
    config->ambient_sound_ticks_max = loc->ambient_sound_ticks_max;
    config->ambient_sound_ids = loc->ambient_sound_ids;
    config->ambient_sound_id_count = loc->ambient_sound_id_count;
    config->seq_random_start = loc->seq_random_start;
    config->random_seq_ids = loc->random_seq_ids;
    config->random_seq_delays = loc->random_seq_delays;
    config->random_seq_id_count = loc->random_seq_id_count;
    config->campaign_ids = loc->campaign_ids;
    config->campaign_id_count = loc->campaign_id_count;
    config->param_values = loc->param_values;

    memset(loc, 0, sizeof(struct CacheConfigLocation));
    free(loc);

    return config;
}

void
gamecache_scenery_config_free(struct GameCache_SceneryConfig* config)
{
    if( !config )
        return;

    for( int i = 0; i < 10; i++ )
        free(config->actions[i]);
    free(config->name);
    free(config->desc);

    free(config->shapes);
    if( config->models )
    {
        for( int i = 0; i < config->shapes_and_model_count; i++ )
            free(config->models[i]);
        free(config->models);
    }

    free(config->lengths);
    free(config->recolors_from);
    free(config->recolors_to);
    free(config->retextures_from);
    free(config->retextures_to);
    free(config->transforms);
    free(config->ambient_sound_ids);
    free(config->random_seq_ids);
    free(config->random_seq_delays);
    free(config->campaign_ids);

    if( config->param_values.values )
    {
        for( int i = 0; i < config->param_values.count; i++ )
            free(config->param_values.values[i]);
        free(config->param_values.values);
    }
    free(config->param_values.keys);
    free(config->param_values.is_string);

    free(config);
}
