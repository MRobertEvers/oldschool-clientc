#ifndef GTASK_INIT_SCENE_U_C
#define GTASK_INIT_SCENE_U_C
#include "task_init_scene.h"

#include "datastruct/list.h"
#include "datastruct/vec.h"
#include "game.h"
#include "gametask.h"
#include "graphics/dash.h"
#include "osrs/cache_utils.h"
#include "osrs/configmap.h"
#include "osrs/dashlib.h"
#include "osrs/gio_assets.h"
#include "osrs/painters.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/frame.h"
#include "osrs/rscache/tables/framemap.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables/sprites.h"
#include "osrs/rscache/tables/textures.h"
#include "osrs/scenebuilder.h"
// #include "osrs/terrain.h"
#include "osrs/texture.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum TaskStepKind
{
    TS_GATHER,
    TS_POLL,
    TS_PROCESS,
    TS_DONE,
};

struct TaskStep
{
    enum TaskStepKind step;
};

struct SceneryEntry
{
    int id;
    int mapx;
    int mapz;
    struct CacheMapLocs* locs;
};

struct ModelEntry
{
    int id;
    struct CacheModel* model;
};

struct TextureEntry
{
    int id;
    struct DashTexture* texture;
};

struct FrameEntry
{
    int id;
    struct FileList* frame;
};

struct FrameAnimEntry
{
    // anim_file
    int id;
    struct CacheFrame* frame;
};

struct FramemapEntry
{
    int id;
    struct CacheFramemap* framemap;
};

struct Chunk
{
    int x;
    int z;
};

#define CHUNKS_COUNT 36

struct FramePack
{
    int animation_id_aka_archive_id;
    struct FramePack* next;

    void* data;
    int data_size;
};

struct FramePack*
framepack_new(
    void* data,
    int data_size)
{
    struct FramePack* framepack = malloc(sizeof(struct FramePack));
    memset(framepack, 0, sizeof(struct FramePack));

    void* copied_data = malloc(data_size);
    memcpy(copied_data, data, data_size);
    framepack->data = copied_data;
    framepack->data_size = data_size;

    return framepack;
}

void
framepack_free(struct FramePack* framepack)
{
    free(framepack->data);
    free(framepack);
}

struct TaskInitScene
{
    enum TaskInitSceneStep step;
    struct GGame* game;
    struct GIOQueue* io;

    struct TaskStep task_steps[STEP_INIT_SCENE_STEP_COUNT];

    struct SceneBuilder* scene_builder;

    struct FramePack* framepacks_list;

    struct DashMap* overlay_configmap;
    struct DashMap* scenery_configmap;
    struct DashMap* sequences_configmap;
    struct DashMap* texture_definitions_configmap;
    struct DashMap* models_hmap;
    struct DashMap* spritepacks_hmap;
    struct DashMap* textures_hmap;
    struct DashMap* frame_blob_hmap;
    struct DashMap* frame_anim_hmap;
    struct DashMap* framemaps_hmap;

    struct DashMap* scenery_hmap;
    struct Vec* scenery_ids_vec;

    struct Vec* queued_texture_ids_vec;
    struct Vec* queued_frame_ids_vec;
    struct Vec* queued_framemap_ids_vec;

    struct Chunk chunks[CHUNKS_COUNT];
    int chunks_count;
    int chunks_width;

    int world_x;
    int world_z;
    int scene_size;

    struct Vec* queued_sequences_vec;
    struct Vec* queued_scenery_models_vec;
    struct Vec* reqid_queue_vec;
    int reqid_queue_inflight_count;

    struct Painter* painter;
};

static void
framepack_push_buffer(
    struct TaskInitScene* task,
    int id,
    void* data,
    int data_size)
{
    struct FramePack* framepack = framepack_new(data, data_size);
    framepack->animation_id_aka_archive_id = id;

    if( task->framepacks_list == NULL )
    {
        task->framepacks_list = framepack;
        return;
    }

    struct FramePack* current = task->framepacks_list;
    while( current->next != NULL )
        current = current->next;
    current->next = framepack;
}

// static struct DashModel*
// load_model(
//     struct CacheConfigLocation* loc_config,
//     struct HMap* models_hmap,
//     int shape_select,
//     int orientation,
//     struct TileHeights* tile_heights)
// {
//     struct ModelEntry* model_entry = NULL;
//     int* shapes = loc_config->shapes;
//     int** model_id_sets = loc_config->models;
//     int* lengths = loc_config->lengths;
//     int shapes_and_model_count = loc_config->shapes_and_model_count;

//     struct CacheModel** models = NULL;
//     int model_count = 0;
//     int* model_ids = NULL;

//     struct CacheModel* model = NULL;

//     if( !model_id_sets )
//         return NULL;

//     if( !shapes )
//     {
//         int count = lengths[0];

//         models = malloc(sizeof(struct CacheModel) * count);
//         memset(models, 0, sizeof(struct CacheModel) * count);
//         model_ids = malloc(sizeof(int) * count);
//         memset(model_ids, 0, sizeof(int) * count);

//         for( int i = 0; i < count; i++ )
//         {
//             int model_id = model_id_sets[0][i];
//             assert(model_id);

//             model_entry = (struct ModelEntry*)hmap_search(models_hmap, &model_id, HMAP_FIND);
//             assert(model_entry);
//             model = model_entry->model;

//             assert(model);
//             models[model_count] = model;
//             model_ids[model_count] = model_id;
//             model_count++;
//         }
//     }
//     else
//     {
//         models = malloc(sizeof(struct CacheModel*) * shapes_and_model_count);
//         memset(models, 0, sizeof(struct CacheModel*) * shapes_and_model_count);
//         model_ids = malloc(sizeof(int) * shapes_and_model_count);
//         memset(model_ids, 0, sizeof(int) * shapes_and_model_count);

//         bool found = false;
//         for( int i = 0; i < shapes_and_model_count; i++ )
//         {
//             int count_inner = lengths[i];

//             int loc_type = shapes[i];
//             if( loc_type == shape_select )
//             {
//                 for( int j = 0; j < count_inner; j++ )
//                 {
//                     int model_id = model_id_sets[i][j];
//                     assert(model_id);

//                     model_entry =
//                         (struct ModelEntry*)hmap_search(models_hmap, &model_id, HMAP_FIND);
//                     assert(model_entry);
//                     model = model_entry->model;

//                     assert(model);
//                     models[model_count] = model;
//                     model_ids[model_count] = model_id;
//                     model_count++;
//                     found = true;
//                 }
//             }
//         }
//         assert(found);
//     }

//     assert(model_count > 0);

//     if( model_count > 1 )
//     {
//         model = model_new_merge(models, model_count);
//     }
//     else
//     {
//         model = model_new_copy(models[0]);
//     }

//     // printf("face textures: ");
//     // if( model->face_textures )
//     //     for( int f = 0; f < model->face_count; f++ )
//     //     {
//     //         printf("%d; ", model->face_textures[f]);
//     //     }
//     // printf("\n");

//     apply_transforms(
//         loc_config,
//         model,
//         orientation,
//         tile_heights->sw_height,
//         tile_heights->se_height,
//         tile_heights->ne_height,
//         tile_heights->nw_height);

//     struct DashModel* dash_model = NULL;
//     dash_model = dashmodel_new_from_cache_model(model);
//     model_free(model);

//     //     scene_model->light_ambient = loc_config->ambient;
//     //     scene_model->light_contrast = loc_config->contrast;

//     light_model_default(dash_model, loc_config->contrast, loc_config->ambient);

//     // Sequences don't account for rotations, so models must be rotated AFTER the animation
//     is
//     // applied.
//     if( loc_config->seq_id != -1 )
//     {
//         // TODO: account for transforms.
//         // Also, I believe this is the only overridden transform.
//         //         const isEntity =
//         // seqId !== -1 ||
//         // locType.transforms !== undefined ||
//         // locLoadType === LocLoadType.NO_MODELS;

//         // scene_model->yaw = 512 * orientation;
//         // scene_model->yaw %= 2048;
//         // orientation = 0;
//     }

//     // scene_model->model = model;
//     // scene_model->model_id = model_ids[0];

//     // scene_model->light_ambient = loc_config->ambient;
//     // scene_model->light_contrast = loc_config->contrast;
//     // scene_model->sharelight = loc_config->sharelight;

//     // scene_model->__loc_id = loc_config->_id;

//     // if( model->vertex_bone_map )
//     //     scene_model->vertex_bones =
//     //         modelbones_new_decode(model->vertex_bone_map, model->vertex_count);
//     // if( model->face_bone_map )
//     //     scene_model->face_bones = modelbones_new_decode(model->face_bone_map,
//     model->face_count);

//     // scene_model->sequence = NULL;
//     // if( loc_config->seq_id != -1 )
//     // {
//     //     scene_model_vertices_create_original(scene_model);

//     //     if( model->face_alphas )
//     //         scene_model_face_alphas_create_original(scene_model);

//     //     sequence = config_sequence_table_get_new(sequence_table, loc_config->seq_id);
//     //     assert(sequence);
//     //     assert(sequence->frame_lengths);
//     //     scene_model->sequence = sequence;

//     //     assert(scene_model->frames == NULL);
//     //     scene_model->frames = malloc(sizeof(struct CacheFrame*) * sequence->frame_count);
//     //     memset(scene_model->frames, 0, sizeof(struct CacheFrame*) *
//     sequence->frame_count);

//     //     int frame_id = sequence->frame_ids[0];
//     //     int frame_archive_id = (frame_id >> 16) & 0xFFFF;
//     //     // Get the frame definition ID from the second 2 bytes of the sequence frame ID
//     The
//     //     //     first 2 bytes are the sequence ID,
//     //     //     the second 2 bytes are the frame archive ID

//     //     frame_archive = cache_archive_new_load(cache, CACHE_ANIMATIONS, frame_archive_id);
//     //     frame_filelist = filelist_new_from_cache_archive(frame_archive);
//     //     for( int i = 0; i < sequence->frame_count; i++ )
//     //     {
//     //         assert(((sequence->frame_ids[i] >> 16) & 0xFFFF) == frame_archive_id);
//     //         // assert(i < frame_filelist->file_count);

//     //         int frame_id = sequence->frame_ids[i];
//     //         int frame_archive_id = (frame_id >> 16) & 0xFFFF;
//     //         int frame_file_id = frame_id & 0xFFFF;

//     //         assert(frame_file_id > 0);
//     //         assert(frame_file_id - 1 < frame_filelist->file_count);

//     //         char* frame_data = frame_filelist->files[frame_file_id - 1];
//     //         int frame_data_size = frame_filelist->file_sizes[frame_file_id - 1];
//     //         int framemap_id = framemap_id_from_frame_archive(frame_data, frame_data_size);

//     //         if( !scene_model->framemap )
//     //         {
//     //             framemap = framemap_new_from_cache(cache, framemap_id);
//     //             scene_model->framemap = framemap;
//     //         }

//     //         frame = frame_new_decode2(frame_id, scene_model->framemap, frame_data,
//     //         frame_data_size);

//     //         scene_model->frames[scene_model->frame_count++] = frame;
//     //     }

//     //     cache_archive_free(frame_archive);
//     //     frame_archive = NULL;
//     //     filelist_free(frame_filelist);
//     //     frame_filelist = NULL;
//     // }

//     free(models);
//     free(model_ids);

//     return dash_model;
// }

// static int
// game_add_scene_element(
//     struct GGame* game,
//     struct DashModel* model,
//     struct DashPosition* position)
// {
//     struct SceneElement scene_element = {
//         .model = model,
//         .position = position,
//     };
//     vec_push(game->scene_elements, &scene_element);

//     int idx = vec_size(game->scene_elements) - 1;

//     return idx;
// }

static void
vec_push_unique(
    struct Vec* vec,
    int* element)
{
    struct VecIter* iter = vec_iter_new(vec);
    int* element_iter = NULL;
    while( (element_iter = (int*)vec_iter_next(iter)) )
    {
        if( *element_iter == *element )
            goto done;
    }
    vec_push(vec, element);

done:;
    vec_iter_free(iter);
}

static void
queue_sequence(
    struct TaskInitScene* task,
    struct CacheConfigLocation* scenery_config)
{
    int seq_id = scenery_config->seq_id;
    if( seq_id > 0 )
    {
        vec_push_unique(task->queued_sequences_vec, &seq_id);
    }
}

static void
queue_scenery_models(
    struct TaskInitScene* task,
    struct CacheConfigLocation* scenery_config,
    int shape_select)
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
                vec_push_unique(task->queued_scenery_models_vec, &model_id);
            }
        }
    }
    else
    {
        for( int i = 0; i < shapes_and_model_count; i++ )
        {
            int count_inner = lengths[i];
            int loc_type = shapes[i];
            // Ignore shape select because some locs don't use the shape select stored.
            // if( loc_type == shape_select )
            {
                for( int j = 0; j < count_inner; j++ )
                {
                    int model_id = model_id_sets[i][j];
                    if( model_id )
                    {
                        vec_push_unique(task->queued_scenery_models_vec, &model_id);
                    }
                }
            }
        }
    }
}

struct TaskInitScene*
task_init_scene_new(
    struct GGame* game,
    int map_sw_x,
    int map_sw_z,
    int map_ne_x,
    int map_ne_z)
{
    struct DashMapConfig config = { 0 };
    struct TaskInitScene* task = malloc(sizeof(struct TaskInitScene));
    memset(task, 0, sizeof(struct TaskInitScene));
    task->step = STEP_INIT_SCENE_INITIAL;
    task->game = game;
    task->io = game->io;

    task->world_x = map_sw_x;
    task->world_z = map_sw_z;
    task->scene_size = map_ne_x - map_sw_x;

    int chunk_idx = 0;
    for( int i = map_sw_x; i <= map_ne_x; i++ )
    {
        for( int j = map_sw_z; j <= map_ne_z; j++ )
        {
            task->chunks[chunk_idx].x = i;
            task->chunks[chunk_idx].z = j;
            chunk_idx++;
        }
    }

    task->chunks_width = (map_ne_x - map_sw_x + 1);
    task->chunks_count = (map_ne_z - map_sw_z + 1) * (map_ne_x - map_sw_x + 1);

    game->sys_painter =
        painter_new(task->chunks_width * 64, task->chunks_width * 64, MAP_TERRAIN_LEVELS);
    game->sys_painter_buffer = painter_buffer_new();

    task->painter = game->sys_painter;

    task->scene_builder =
        scenebuilder_new_painter(task->painter, map_sw_x, map_sw_z, map_ne_x, map_ne_z);

    task->reqid_queue_vec = vec_new(sizeof(int), 64);
    task->reqid_queue_inflight_count = 0;
    task->queued_scenery_models_vec = vec_new(sizeof(int), 512);
    task->queued_sequences_vec = vec_new(sizeof(int), 512);

    task->scenery_ids_vec = vec_new(sizeof(int), 512);

    task->queued_texture_ids_vec = vec_new(sizeof(int), 512);
    task->queued_frame_ids_vec = vec_new(sizeof(int), 512);
    task->queued_framemap_ids_vec = vec_new(sizeof(int), 512);

    int buffer_size = 1024 * sizeof(struct ModelEntry) * 4;
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ModelEntry),
    };
    task->models_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct TextureEntry),
    };
    task->textures_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct SpritePackEntry),
    };
    task->spritepacks_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct FramemapEntry),
    };
    task->framemaps_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct FrameEntry),
    };
    task->frame_blob_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct FrameAnimEntry),
    };
    task->frame_anim_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct SceneryEntry),
    };
    task->scenery_hmap = dashmap_new(&config, 0);

    return task;
}

void
task_init_scene_free(struct TaskInitScene* task)
{
    vec_free(task->queued_scenery_models_vec);
    vec_free(task->queued_texture_ids_vec);
    vec_free(task->queued_frame_ids_vec);
    vec_free(task->queued_framemap_ids_vec);

    free(dashmap_buffer_ptr(task->models_hmap));
    free(dashmap_buffer_ptr(task->textures_hmap));
    free(dashmap_buffer_ptr(task->spritepacks_hmap));

    dashmap_free(task->models_hmap);
    dashmap_free(task->textures_hmap);
    dashmap_free(task->spritepacks_hmap);

    configmap_free(task->scenery_configmap);
    configmap_free(task->texture_definitions_configmap);

    free(task);
}

static enum GameTaskStatus
step_scenery_load(struct TaskInitScene* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_1_LOAD_SCENERY];

    struct CacheMapLocs* locs = NULL;
    struct DashMapIter* iter = NULL;
    struct GIOMessage message = { 0 };
    int reqid = 0;
    struct SceneryEntry* scenery_entry = NULL;
    int mapxz = 0;

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);
        for( int i = 0; i < task->chunks_count; i++ )
        {
            reqid = gio_assets_map_scenery_load(task->io, task->chunks[i].x, task->chunks[i].z);

            vec_push(task->reqid_queue_vec, &reqid);
        }
        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            locs = map_locs_new_from_decode(message.data, message.data_size);
            locs->_chunk_mapx = message.param_b >> 16;
            locs->_chunk_mapz = message.param_b & 0xFFFF;

            mapxz = MAPXZ(locs->_chunk_mapx, locs->_chunk_mapz);
            scenery_entry =
                (struct SceneryEntry*)dashmap_search(task->scenery_hmap, &mapxz, DASHMAP_INSERT);
            assert(scenery_entry && "Scenery entry must be inserted into hmap");
            scenery_entry->id = mapxz;
            scenery_entry->mapx = locs->_chunk_mapx;
            scenery_entry->mapz = locs->_chunk_mapz;
            scenery_entry->locs = locs;

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_PROCESS;
    case TS_PROCESS:
        iter = dashmap_iter_new(task->scenery_hmap);
        while( (scenery_entry = (struct SceneryEntry*)dashmap_iter_next(iter)) )
        {
            locs = scenery_entry->locs;

            scenebuilder_cache_map_locs(
                task->scene_builder, locs->_chunk_mapx, locs->_chunk_mapz, locs);

            for( int i = 0; i < locs->locs_count; i++ )
                vec_push_unique(task->scenery_ids_vec, &locs->locs[i].loc_id);
        }
        dashmap_iter_free(iter);
        step_stage->step = TS_DONE;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_scenery_config_load(struct TaskInitScene* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_2_LOAD_SCENERY_CONFIG];

    int reqid = 0;
    struct GIOMessage message = { 0 };
    struct DashMapIter* iter = NULL;
    struct CacheConfigLocation* config_loc = NULL;
    struct CacheMapLoc* loc = NULL;
    struct CacheMapLocs* locs = NULL;
    struct SceneryEntry* scenery_entry = NULL;

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);

        reqid = gio_assets_config_scenery_load(task->io);
        vec_push(task->reqid_queue_vec, &reqid);

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            assert(task->reqid_queue_inflight_count > 0);
            task->reqid_queue_inflight_count--;
            assert(task->reqid_queue_inflight_count == 0);

            task->scenery_configmap = configmap_new_from_filepack(
                message.data,
                message.data_size,
                (int*)vec_data(task->scenery_ids_vec),
                vec_size(task->scenery_ids_vec));

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_PROCESS;
    case TS_PROCESS:

        scenebuilder_cache_configmap_locs(task->scene_builder, task->scenery_configmap);

        iter = dashmap_iter_new(task->scenery_hmap);
        while( (scenery_entry = (struct SceneryEntry*)dashmap_iter_next(iter)) )
        {
            locs = scenery_entry->locs;

            for( int i = 0; i < locs->locs_count; i++ )
            {
                loc = &locs->locs[i];

                config_loc = configmap_get(task->scenery_configmap, loc->loc_id);
                assert(config_loc && "Config loc must be found");

                queue_scenery_models(task, config_loc, loc->shape_select);
                queue_sequence(task, config_loc);
            }
        }
        dashmap_iter_free(iter);

        step_stage->step = TS_DONE;
    case TS_DONE:
        break;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_scenery_models_load(struct TaskInitScene* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_3_LOAD_SCENERY_MODELS];

    int reqid = 0;
    struct GIOMessage message = { 0 };
    struct ModelEntry* model_entry = NULL;
    struct CacheModel* model = NULL;

    switch( step_stage->step )
    {
    case TS_GATHER:

        vec_clear(task->reqid_queue_vec);
        int count = vec_size(task->queued_scenery_models_vec);

        for( int i = 0; i < count; i++ )
        {
            int model_id = *(int*)vec_get(task->queued_scenery_models_vec, i);

            reqid = gio_assets_model_load(task->io, model_id);
            vec_push(task->reqid_queue_vec, &reqid);
        }

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            model = model_new_decode(message.data, message.data_size);
            model_entry = (struct ModelEntry*)dashmap_search(
                task->models_hmap, &message.param_b, DASHMAP_INSERT);
            assert(model_entry && "Model must be inserted into hmap");
            model_entry->id = message.param_b;
            model_entry->model = model;

            scenebuilder_cache_model(task->scene_builder, message.param_b, model);

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_DONE;
    case TS_DONE:
        break;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_terrain_load(struct TaskInitScene* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_4_LOAD_TERRAIN];

    int reqid = 0;
    struct GIOMessage message = { 0 };
    struct CacheMapTerrain* terrain = NULL;

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);
        for( int i = 0; i < task->chunks_count; i++ )
        {
            reqid = gio_assets_map_terrain_load(task->io, task->chunks[i].x, task->chunks[i].z);
            vec_push(task->reqid_queue_vec, &reqid);
        }
        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            int map_x = (message.param_b >> 16) & 0xFFFF;
            int map_z = message.param_b & 0xFFFF;

            terrain = map_terrain_new_from_decode(message.data, message.data_size, map_x, map_z);

            scenebuilder_cache_map_terrain(task->scene_builder, map_x, map_z, terrain);

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_DONE;
    case TS_DONE:
        break;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_underlay_load(struct TaskInitScene* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_5_LOAD_UNDERLAY];

    int reqid = 0;
    struct GIOMessage message = { 0 };
    struct CacheConfigUnderlay* underlay = NULL;
    struct ConfigMap* configmap = NULL;

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);
        reqid = gio_assets_config_underlay_load(task->io);
        vec_push(task->reqid_queue_vec, &reqid);

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;
            assert(task->reqid_queue_inflight_count == 0);

            configmap = configmap_new_from_filepack(message.data, message.data_size, NULL, 0);
            scenebuilder_cache_configmap_underlay(task->scene_builder, configmap);

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_DONE;
    case TS_DONE:
        break;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_overlay_load(struct TaskInitScene* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_6_LOAD_OVERLAY];

    int reqid = 0;
    struct GIOMessage message = { 0 };
    struct CacheConfigOverlay* overlay = NULL;
    struct ConfigMap* configmap = NULL;

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);
        reqid = gio_assets_config_overlay_load(task->io);
        vec_push(task->reqid_queue_vec, &reqid);

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;
            assert(task->reqid_queue_inflight_count == 0);

            configmap = configmap_new_from_filepack(message.data, message.data_size, NULL, 0);

            task->overlay_configmap = configmap;
            scenebuilder_cache_configmap_overlay(task->scene_builder, configmap);

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_DONE;
    case TS_DONE:
        break;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_textures_load(struct TaskInitScene* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_7_LOAD_TEXTURES];

    struct CacheConfigOverlay* config_overlay = NULL;
    struct DashMapIter* iter = NULL;
    struct SceneryEntry* scenery_entry = NULL;
    struct ModelEntry* model_entry = NULL;
    struct CacheModel* model = NULL;
    struct CacheMapLoc* loc = NULL;
    struct CacheMapLocs* locs = NULL;
    struct CacheConfigLocation* config_loc = NULL;
    int reqid = 0;
    struct GIOMessage message = { 0 };

    switch( step_stage->step )
    {
    case TS_GATHER:
        /**
         * Queue Textures
         */

        iter = dashmap_iter_new(task->overlay_configmap);
        while( (config_overlay = configmap_iter_next(iter)) )
            if( config_overlay->texture > 0 )
            {
                int texture_id = config_overlay->texture;
                vec_push_unique(task->queued_texture_ids_vec, &texture_id);
            }

        dashmap_iter_free(iter);

        iter = dashmap_iter_new(task->models_hmap);
        while( (model_entry = (struct ModelEntry*)dashmap_iter_next(iter)) )
        {
            // There is a face texture that is not getting loaded.
            model = model_entry->model;
            if( !model->face_textures )
                continue;

            for( int i = 0; i < model->face_count; i++ )
            {
                int face_texture = model->face_textures[i];
                if( face_texture != -1 )
                {
                    vec_push_unique(task->queued_texture_ids_vec, &face_texture);
                }
            }
        }
        dashmap_iter_free(iter);

        iter = dashmap_iter_new(task->scenery_hmap);
        while( (scenery_entry = (struct SceneryEntry*)dashmap_iter_next(iter)) )
        {
            locs = scenery_entry->locs;

            for( int i = 0; i < locs->locs_count; i++ )
            {
                loc = &locs->locs[i];

                config_loc = configmap_get(task->scenery_configmap, loc->loc_id);
                assert(config_loc && "Config loc must be found");

                if( config_loc->retexture_count && config_loc->retextures_to )
                {
                    for( int i = 0; i < config_loc->retexture_count; i++ )
                        vec_push_unique(
                            task->queued_texture_ids_vec, &config_loc->retextures_to[i]);
                }
            }
        }
        dashmap_iter_free(iter);

        vec_clear(task->reqid_queue_vec);
        reqid = gio_assets_texture_definitions_load(task->io);
        vec_push(task->reqid_queue_vec, &reqid);

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;
            assert(task->reqid_queue_inflight_count == 0);

            task->texture_definitions_configmap = configmap_new_from_filepack(
                message.data,
                message.data_size,
                (int*)vec_data(task->queued_texture_ids_vec),
                vec_size(task->queued_texture_ids_vec));

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_DONE;
    case TS_DONE:
        break;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_spritepacks_load(struct TaskInitScene* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_8_LOAD_SPRITEPACKS];
    int reqid = 0;
    struct GIOMessage message = { 0 };
    struct CacheTexture* texture_definition = NULL;
    struct DashMapIter* iter = NULL;
    struct Vec* sprite_pack_ids = NULL;
    struct SpritePackEntry* spritepack_entry = NULL;
    struct CacheSpritePack* spritepack = NULL;

    struct Vec* queued_sprite_pack_ids = NULL;

    switch( step_stage->step )
    {
    case TS_GATHER:
        queued_sprite_pack_ids = vec_new(sizeof(int), 512);

        iter = dashmap_iter_new(task->texture_definitions_configmap);
        while( (texture_definition = (struct CacheTexture*)configmap_iter_next(iter)) )
        {
            for( int i = 0; i < texture_definition->sprite_ids_count; i++ )
                vec_push_unique(queued_sprite_pack_ids, &texture_definition->sprite_ids[i]);
        }
        dashmap_iter_free(iter);

        vec_clear(task->reqid_queue_vec);
        for( int i = 0; i < vec_size(queued_sprite_pack_ids); i++ )
        {
            int sprite_pack_id = *(int*)vec_get(queued_sprite_pack_ids, i);
            reqid = gio_assets_spritepack_load(task->io, sprite_pack_id);
            vec_push(task->reqid_queue_vec, &reqid);
        }

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        vec_free(queued_sprite_pack_ids);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            spritepack =
                sprite_pack_new_decode(message.data, message.data_size, SPRITELOAD_FLAG_NORMALIZE);
            assert(spritepack && "Spritepack must be decoded");
            spritepack_entry =
                dashmap_search(task->spritepacks_hmap, &message.param_b, DASHMAP_INSERT);
            assert(spritepack_entry && "Spritepack must be inserted into hmap");
            spritepack_entry->id = message.param_b;
            spritepack_entry->spritepack = spritepack;

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_PROCESS;
    case TS_PROCESS:
        step_stage->step = TS_DONE;
    case TS_DONE:
        break;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_textures_build(struct TaskInitScene* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_9_BUILD_TEXTURES];

    struct DashMapIter* iter = NULL;
    struct CacheTexture* texture_definition = NULL;
    struct DashTexture* texture = NULL;
    struct TextureEntry* texture_entry = NULL;

    iter = dashmap_iter_new(task->texture_definitions_configmap);
    while( (texture_definition = (struct CacheTexture*)configmap_iter_next(iter)) )
    {
        texture = texture_new_from_definition(texture_definition, task->spritepacks_hmap);
        assert(texture);

        texture_entry = (struct TextureEntry*)dashmap_search(
            task->textures_hmap, &texture_definition->_id, DASHMAP_INSERT);
        assert(texture_entry && "Texture must be inserted into hmap");

        texture_entry->id = texture_definition->_id;
        texture_entry->texture = texture;

        dash3d_add_texture(task->game->sys_dash, texture_definition->_id, texture);
    }
    dashmap_iter_free(iter);

    step_stage->step = TS_DONE;

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_sequences_load(struct TaskInitScene* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_10_LOAD_SEQUENCES];

    int reqid = 0;
    struct GIOMessage message = { 0 };
    struct CacheConfigSequence* sequence = NULL;
    struct DashMapIter* iter = NULL;

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);
        reqid = gio_assets_config_sequences_load(task->io);
        vec_push(task->reqid_queue_vec, &reqid);

        task->reqid_queue_inflight_count = 1;

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;
            assert(task->reqid_queue_inflight_count == 0);

            task->sequences_configmap = configmap_new_from_filepack(
                message.data,
                message.data_size,
                (int*)vec_data(task->queued_sequences_vec),
                vec_size(task->queued_sequences_vec));

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_PROCESS;
    case TS_PROCESS:
        step_stage->step = TS_DONE;
    case TS_DONE:
        break;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_frames_load(struct TaskInitScene* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_11_LOAD_FRAMES];

    struct DashMapIter* iter = NULL;
    struct CacheConfigSequence* sequence = NULL;
    struct FileList* frame = NULL;
    struct FrameEntry* frame_entry = NULL;
    int reqid = 0;
    struct Vec* queued_frame_ids_vec = NULL;
    struct GIOMessage message = { 0 };

    switch( step_stage->step )
    {
    case TS_GATHER:
        queued_frame_ids_vec = vec_new(sizeof(int), 512);
        vec_clear(task->reqid_queue_vec);
        iter = dashmap_iter_new(task->sequences_configmap);
        while( (sequence = (struct CacheConfigSequence*)configmap_iter_next(iter)) )
        {
            for( int i = 0; i < sequence->frame_count; i++ )
            {
                int frame_id = sequence->frame_ids[i];
                int frame_archive_id = (frame_id >> 16) & 0xFFFF;
                int frame_file_id = frame_id & 0xFFFF;
                if( frame_id == 811597825 )
                {
                    printf("frame_id: %d\n", frame_id);
                }
                vec_push_unique(queued_frame_ids_vec, &frame_archive_id);
            }
        }

        for( int i = 0; i < vec_size(queued_frame_ids_vec); i++ )
        {
            int frame_id = *(int*)vec_get(queued_frame_ids_vec, i);
            reqid = gio_assets_animation_load(task->io, frame_id);
            vec_push(task->reqid_queue_vec, &reqid);
        }
        vec_free(queued_frame_ids_vec);

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            frame_entry = (struct FrameEntry*)dashmap_search(
                task->frame_blob_hmap, &message.param_b, DASHMAP_INSERT);
            assert(frame_entry && "Frame must be inserted into hmap");
            frame_entry->id = message.param_b;
            frame_entry->frame = cu_filelist_new_from_filepack(message.data, message.data_size);

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_PROCESS;
    case TS_PROCESS:
        break;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_framemaps_load(struct TaskInitScene* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_12_LOAD_FRAMEMAPS];

    struct DashMapIter* iter = NULL;
    struct FrameEntry* frame_entry = NULL;
    struct FramemapEntry* framemap_entry = NULL;
    struct Vec* queued_framemap_ids_vec = NULL;
    struct CacheConfigSequence* sequence = NULL;
    struct FrameAnimEntry* frame_anim_entry = NULL;
    struct FileList* frame_anim = NULL;
    struct CacheFrame* cache_frame = NULL;
    int reqid = 0;
    struct GIOMessage message = { 0 };

    switch( step_stage->step )
    {
    case TS_GATHER:
        queued_framemap_ids_vec = vec_new(sizeof(int), 512);
        vec_clear(task->reqid_queue_vec);
        iter = dashmap_iter_new(task->sequences_configmap);
        while( (sequence = (struct CacheConfigSequence*)configmap_iter_next(iter)) )
        {
            for( int i = 0; i < sequence->frame_count; i++ )
            {
                int frame_id = sequence->frame_ids[i];
                int frame_archive_id = (frame_id >> 16) & 0xFFFF;
                int frame_file_id = frame_id & 0xFFFF;

                frame_entry = (struct FrameEntry*)dashmap_search(
                    task->frame_blob_hmap, &frame_archive_id, DASHMAP_FIND);
                assert(frame_entry && "Frame must be found");

                assert(frame_file_id > 0);
                assert(frame_file_id - 1 < frame_entry->frame->file_count);

                char* file_data = frame_entry->frame->files[frame_file_id - 1];
                int file_data_size = frame_entry->frame->file_sizes[frame_file_id - 1];

                int framemap_id = frame_framemap_id_from_file(file_data, file_data_size);

                vec_push_unique(queued_framemap_ids_vec, &framemap_id);
            }
        }

        for( int i = 0; i < vec_size(queued_framemap_ids_vec); i++ )
        {
            int framemap_id = *(int*)vec_get(queued_framemap_ids_vec, i);
            reqid = gio_assets_framemap_load(task->io, framemap_id);
            vec_push(task->reqid_queue_vec, &reqid);
        }
        vec_free(queued_framemap_ids_vec);

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            framemap_entry = (struct FramemapEntry*)dashmap_search(
                task->framemaps_hmap, &message.param_b, DASHMAP_INSERT);
            assert(framemap_entry && "Framemap must be inserted into hmap");

            framemap_entry->id = message.param_b;
            framemap_entry->framemap =
                framemap_new_decode2(message.param_b, message.data, message.data_size);

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_PROCESS;
    case TS_PROCESS:
        iter = dashmap_iter_new(task->sequences_configmap);
        while( (sequence = (struct CacheConfigSequence*)configmap_iter_next(iter)) )
        {
            for( int i = 0; i < sequence->frame_count; i++ )
            {
                int frame_id = sequence->frame_ids[i];
                int frame_archive_id = (frame_id >> 16) & 0xFFFF;
                int frame_file_id = frame_id & 0xFFFF;

                frame_entry = (struct FrameEntry*)dashmap_search(
                    task->frame_blob_hmap, &frame_archive_id, DASHMAP_FIND);
                assert(frame_entry && "Frame must be found");

                assert(frame_file_id > 0);
                assert(frame_file_id - 1 < frame_entry->frame->file_count);

                char* file_data = frame_entry->frame->files[frame_file_id - 1];
                int file_data_size = frame_entry->frame->file_sizes[frame_file_id - 1];

                int framemap_id = frame_framemap_id_from_file(file_data, file_data_size);

                framemap_entry = (struct FramemapEntry*)dashmap_search(
                    task->framemaps_hmap, &framemap_id, DASHMAP_FIND);
                assert(framemap_entry && "Framemap must be found");

                frame_anim_entry = (struct FrameAnimEntry*)dashmap_search(
                    task->frame_anim_hmap, &frame_id, DASHMAP_INSERT);
                assert(frame_anim_entry && "Frame anim must be inserted into hmap");

                cache_frame = frame_new_decode2(
                    frame_id, framemap_entry->framemap, file_data, file_data_size);

                frame_anim_entry->id = frame_id;
                frame_anim_entry->frame = cache_frame;

                scenebuilder_cache_frame(task->scene_builder, frame_id, cache_frame);
            }
        }

        step_stage->step = TS_DONE;
        break;
    case TS_DONE:
        break;
    }

    return GAMETASK_STATUS_COMPLETED;
}

enum GameTaskStatus
task_init_scene_step(struct TaskInitScene* task)
{
    struct GIOMessage message;
    struct DashMapIter* iter = NULL;
    struct CacheConfigSequence* sequence = NULL;

    switch( task->step )
    {
    case STEP_INIT_SCENE_INITIAL:
    {
        task->step = STEP_INIT_SCENE_1_LOAD_SCENERY;
    }
    case STEP_INIT_SCENE_1_LOAD_SCENERY:
    {
        if( step_scenery_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_2_LOAD_SCENERY_CONFIG;
    }
    case STEP_INIT_SCENE_2_LOAD_SCENERY_CONFIG:
    {
        if( step_scenery_config_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_3_LOAD_SCENERY_MODELS;
    }
    case STEP_INIT_SCENE_3_LOAD_SCENERY_MODELS:
    {
        if( step_scenery_models_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_4_LOAD_TERRAIN;
    }
    case STEP_INIT_SCENE_4_LOAD_TERRAIN:
    {
        if( step_terrain_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_5_LOAD_UNDERLAY;
    }
    case STEP_INIT_SCENE_5_LOAD_UNDERLAY:
    {
        if( step_underlay_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_6_LOAD_OVERLAY;
    }
    case STEP_INIT_SCENE_6_LOAD_OVERLAY:
    {
        if( step_overlay_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_7_LOAD_TEXTURES;
    }
    case STEP_INIT_SCENE_7_LOAD_TEXTURES:
    {
        if( step_textures_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_8_LOAD_SPRITEPACKS;
    }
    case STEP_INIT_SCENE_8_LOAD_SPRITEPACKS:
    {
        if( step_spritepacks_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_9_BUILD_TEXTURES;
    }
    case STEP_INIT_SCENE_9_BUILD_TEXTURES:
    {
        if( step_textures_build(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_10_LOAD_SEQUENCES;
    }
    case STEP_INIT_SCENE_10_LOAD_SEQUENCES:
    {
        /**
         * Create a list of all the sequence ids used in models.
         *
         * Parse the [animation, frame] pairs from the sequences.
         * Load each animation.
         * For each animation, pass a list of the frame ids and parse those.
         *      Put those into a dashmap with [animation, frame] pairs as keys.
         * Get all the framemaps from the frames.
         * Load each framemap.
         */
        if( step_sequences_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_11_LOAD_FRAMES;
    }
    case STEP_INIT_SCENE_11_LOAD_FRAMES:
    {
        if( step_frames_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_12_LOAD_FRAMEMAPS;
    }
    case STEP_INIT_SCENE_12_LOAD_FRAMEMAPS:
    {
        if( step_framemaps_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_13_BUILD_WORLD3D;
    }
    case STEP_INIT_SCENE_13_BUILD_WORLD3D:
    {
        scenebuilder_cache_configmap_locs(task->scene_builder, task->scenery_configmap);
        scenebuilder_cache_configmap_sequences(task->scene_builder, task->sequences_configmap);

        task->game->scene = scenebuilder_load(task->scene_builder);

        task->step = STEP_INIT_SCENE_14_BUILD_TERRAIN3D;
    }
    case STEP_INIT_SCENE_DONE:
    {
        return GAMETASK_STATUS_COMPLETED;
    }
    default:
        goto bad_step;
    }

bad_step:;
    assert(false && "Bad step in task_init_scene_step");
    return GAMETASK_STATUS_FAILED;
}

struct CacheModel*
task_init_scene_value(struct TaskInitScene* task)
{
    assert(task->step == STEP_INIT_SCENE_DONE);
    return NULL;
}

#endif