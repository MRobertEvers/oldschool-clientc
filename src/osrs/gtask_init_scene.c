#ifndef GTASK_INIT_SCENE_U_C
#define GTASK_INIT_SCENE_U_C
#include "gtask_init_scene.h"

#include "datastruct/hmap.h"
#include "datastruct/list.h"
#include "datastruct/vec.h"
#include "gtask.h"
#include "osrs/configmap.h"
#include "osrs/gio_assets.h"
#include "osrs/painters.h"
#include "osrs/tables/config_locs.h"
#include "osrs/tables/maps.h"
#include "osrs/tables/model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include "chunks.u.c"
// clang-format on
struct GTaskInitScene
{
    enum GTaskInitSceneStep step;
    struct GIOQueue* io;

    struct CacheMapLocsIter* scenery_iter;
    struct CacheMapLocs scenery_locs[9];
    int scenery_locs_count;

    struct ConfigMap* scenery_configmap;
    struct HMap* models_hmap;
    struct Vec* queued_scenery_models_ids;

    struct Chunk chunks[9];
    int chunks_count;
    int world_x;
    int world_z;
    int scene_size;

    uint32_t reqid_scenery[9];
    uint32_t reqid_scenery_count;
    uint32_t reqid_scenery_inflight;

    uint32_t reqid_terrain[9];
    uint32_t reqid_terrain_count;
    uint32_t reqid_terrain_inflight;

    uint32_t reqid_scenery_config;

    uint32_t reqid_model_count;
    uint32_t reqid_model_inflight;
    uint32_t* reqid_models;

    struct Painter* painter;
};

static void
build_world(struct GTaskInitScene* task)
{
    task->step = STEP_INIT_SCENE_FOUR_BUILD_WORLD;
}

static void
queue_model_unique(struct GTaskInitScene* task, int model_id)
{
    struct VecIter* iter = vec_iter_new(task->queued_scenery_models_ids);
    int* element = NULL;
    while( (element = (int*)vec_iter_next(iter)) )
    {
        if( *element == model_id )
            goto done;
    }

    vec_push(task->queued_scenery_models_ids, &model_id);

done:;
    vec_iter_free(iter);
}

static void
queue_scenery_models(
    struct GTaskInitScene* task, struct CacheConfigLocation* scenery_config, int shape_select)
{
    int* shapes = scenery_config->shapes;
    int** model_id_sets = scenery_config->models;
    int* lengths = scenery_config->lengths;
    int shapes_and_model_count = scenery_config->shapes_and_model_count;

    if( !model_id_sets )
        return;

    // Collect all model IDs that need to be loaded
    if( !shapes )
    {
        int count = lengths[0];
        for( int i = 0; i < count; i++ )
        {
            int model_id = model_id_sets[0][i];
            if( model_id )
            {
                queue_model_unique(task, model_id);
            }
        }
    }
    else
    {
        for( int i = 0; i < shapes_and_model_count; i++ )
        {
            int count_inner = lengths[i];
            int loc_type = shapes[i];
            if( loc_type == shape_select )
            {
                for( int j = 0; j < count_inner; j++ )
                {
                    int model_id = model_id_sets[i][j];
                    if( model_id )
                    {
                        queue_model_unique(task, model_id);
                    }
                }
            }
        }
    }
}

static struct Vec*
gather_scenery_ids_vec_new(struct CacheMapLocsIter* iter)
{
    struct CacheMapLoc* loc;
    struct Vec* vec = vec_new(sizeof(int), 128);
    while( loc = map_locs_iter_next(iter) )
    {
        vec_push(vec, &loc->loc_id);
    }
    return vec;
}

struct GTaskInitScene*
gtask_init_scene_new(struct GIOQueue* io, int world_x, int world_z, int scene_size)
{
    struct GTaskInitScene* task = malloc(sizeof(struct GTaskInitScene));
    memset(task, 0, sizeof(struct GTaskInitScene));
    task->step = STEP_INIT_SCENE_INITIAL;
    task->io = io;
    task->world_x = world_x;
    task->world_z = world_z;
    task->scene_size = scene_size;
    task->chunks_count = chunks_inview(task->chunks, world_x, world_z, scene_size);

    task->queued_scenery_models_ids = vec_new(sizeof(int), 512);
    struct HashConfig config = {
        .buffer = NULL,
        .buffer_size = 0,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct CacheModel*),
    };
    task->models_hmap = hmap_new(&config, 0);
    return task;
}

void
gtask_init_scene_free(struct GTaskInitScene* task)
{
    vec_free(task->queued_scenery_models_ids);
    configmap_free(task->scenery_configmap);
    map_locs_iter_free(task->scenery_iter);
    free(task);
}

enum GTaskStatus
gtask_init_scene_step(struct GTaskInitScene* task)
{
    struct GIOMessage message;
    struct ConfigMapIter* iter = NULL;
    struct CacheMapLocsIter* map_locs_iter;
    struct CacheMapLoc* loc;
    struct CacheConfigLocation* config_loc;
    struct CacheModel* model = NULL;
    struct CacheModel** model_ptr = NULL;
    bool status = false;

    switch( task->step )
    {
    case STEP_INIT_SCENE_INITIAL:
    {
        task->step = STEP_INIT_SCENE_ONE_LOAD_SCENERY;
    }
    case STEP_INIT_SCENE_ONE_LOAD_SCENERY:
    {
        if( task->reqid_scenery_count == 0 )
        {
            for( int i = 0; i < task->chunks_count; i++ )
            {
                task->reqid_scenery[i] =
                    gio_assets_map_scenery_load(task->io, task->chunks[i].x, task->chunks[i].z);
            }
            task->reqid_scenery_count = task->chunks_count;
        }

        while( gioq_poll(task->io, &message) )
        {
            task->reqid_scenery_inflight--;
            task->scenery_iter = map_locs_iter_new_decode(message.data, message.data_size);
            gioq_release(task->io, &message);
        }
        if( task->reqid_scenery_inflight != 0 )
            return GTASK_STATUS_PENDING;

        task->scenery_iter = map_locs_iter_new_decode(message.data, message.data_size);

        gioq_release(task->io, &message);

        task->step = STEP_INIT_SCENE_TWO_LOAD_SCENERY_CONFIG;
    }
    case STEP_INIT_SCENE_TWO_LOAD_SCENERY_CONFIG:
    {
        struct Vec* scenery_ids = NULL;
        if( task->reqid_scenery_config == 0 )
            task->reqid_scenery_config = gio_assets_config_scenery_load(task->io);

        if( !gioq_poll(task->io, &message) )
            return GTASK_STATUS_PENDING;
        assert(message.message_id == task->reqid_scenery_config);

        scenery_ids = gather_scenery_ids_vec_new(task->scenery_iter);

        task->scenery_configmap = configmap_new_from_packed(
            message.data, message.data_size, (int*)vec_data(scenery_ids), vec_size(scenery_ids));

        vec_free(scenery_ids);
        gioq_release(task->io, &message);

        /**
         * Process Scenery, Queue Model Ids
         */

        iter = configmap_iter_new(task->scenery_configmap);

        while( (config_loc = configmap_iter_next(iter)) )
        {
            queue_scenery_models(task, config_loc, loc->shape_select);
        }

        configmap_iter_free(iter);

        task->step = STEP_INIT_SCENE_THREE_LOAD_SCENERY_MODELS;
    }
    case STEP_INIT_SCENE_THREE_LOAD_SCENERY_MODELS:
    {
        if( task->reqid_model_count == 0 )
        {
            int count = vec_size(task->queued_scenery_models_ids);
            int* reqids = (int*)malloc(sizeof(int) * count);

            for( int i = 0; i < count; i++ )
            {
                reqids[i] = gio_assets_model_load(
                    task->io, ((int*)vec_data(task->queued_scenery_models_ids))[i]);
            }

            task->reqid_model_count = count;
            task->reqid_model_inflight = count;
            task->reqid_models = reqids;
        }

        while( gioq_poll(task->io, &message) )
        {
            task->reqid_model_inflight--;

            model = model_new_decode(message.data, message.data_size);
            model_ptr = hmap_search(task->models_hmap, &model->_id, HMAP_INSERT);
            *model_ptr = model;

            gioq_release(task->io, &message);
        }
        if( task->reqid_model_inflight != 0 )
            return GTASK_STATUS_PENDING;
    }
    case STEP_INIT_SCENE_FOUR_LOAD_TERRAIN:
    {
        if( task->reqid_terrain == 0 )
            task->reqid_terrain =
                gio_assets_map_terrain_load(task->io, task->chunk_x, task->chunk_y);

        if( !gioq_poll(task->io, &message) )
            return GTASK_STATUS_PENDING;
        assert(message.message_id == task->reqid_terrain);

        task->terrain = terrain_new_decode(message.data, message.data_size);
        gioq_release(task->io, &message);
    }
    break;
    case STEP_INIT_SCENE_FOUR_BUILD_WORLD:
    {
        // loc = NULL;
        // config_loc = NULL;
        // map_locs_iter_begin(task->scenery_iter);
        // while( (loc = map_locs_iter_next(task->scenery_iter)) )
        // {
        //     config_loc = configmap_get(task->scenery_configmap, loc->loc_id);
        //     assert(config_loc && "Scenery configuration must be loaded");

        //     // Create entity (ties the different systems together)
        //     // 1. Create entity
        //     // 2. Update painter with entity idx
        //     // 3. Gather Wall Thicknesses
        //     //
        //     // Later: Build visual models (requires terrain, wall thicknesses)
        //     // Later: Build shade map (requires models)

        //     switch( loc->shape_select )
        //     {
        //     case LOC_SHAPE_WALL_SINGLE_SIDE:

        //         painter_add_wall(
        //             task->painter,
        //             loc->chunk_pos_x,
        //             loc->chunk_pos_y,
        //             loc->chunk_pos_level,
        //             entity,
        //             WALL_A,
        //             loc->wall_side);
        //         break;

        //     default:
        //         break;
        //     }
        // }

        task->step = STEP_INIT_SCENE_DONE;
    }
    case STEP_INIT_SCENE_DONE:
    {
        return GTASK_STATUS_COMPLETED;
    }
    default:
        goto bad_step;
    }

    // initial:;
    //     static const int MODEL_ID = 0;
    //     if( task->reqid_model == 0 )
    //         task->reqid_model = gio_assets_model_load(task->io, MODEL_ID);

    //     if( !gioq_poll(task->io, &message) )
    //         return GTASK_STATUS_PENDING;
    //     assert(message.message_id == task->reqid_model);

    //     task->step = STEP_INIT_SCENE_LOAD_MODEL;
    // step_two_load_model:;

    //     model = model_new_decode(message.data, message.data_size);
    //     model->_id = MODEL_ID;

    //     task->model_nullable = model;

    //     gioq_release(task->io, &message);

    //     task->step = STEP_INIT_SCENE_DONE;
    //     return GTASK_STATUS_COMPLETED;

bad_step:;
    assert(false && "Bad step in gtask_init_scene_step");
    return GTASK_STATUS_FAILED;
}

struct CacheModel*
gtask_init_scene_value(struct GTaskInitScene* task)
{
    assert(task->step == STEP_INIT_SCENE_DONE);
    return NULL;
}

#endif