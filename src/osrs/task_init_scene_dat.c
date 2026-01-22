
#include "task_init_scene_dat.h"

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
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/frame.h"
#include "osrs/rscache/tables/framemap.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables/sprites.h"
#include "osrs/rscache/tables/textures.h"
#include "osrs/rscache/tables_dat/animframe.h"
#include "osrs/rscache/tables_dat/config_textures.h"
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

struct FlotypeEntry
{
    int id;
    struct CacheConfigOverlay* flotype;
};

struct ConfigLocEntry
{
    int id;
    int shape_select;
    struct CacheConfigLocation* config_loc;
};

struct AnimframeEntry
{
    int id;
    struct CacheAnimframe* animframe;
};

struct AnimbaseframesEntry
{
    int id;
    struct CacheDatAnimBaseFrames* animbaseframes;
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

struct SequenceEntry
{
    int id;
    struct CacheDatSequence* sequence;
};

struct Chunk
{
    int x;
    int z;
};

#define CHUNKS_COUNT 36

static void
vec_push_unique(
    struct Vec* vec,
    int* element)
{
    struct VecIter* iter = vec_iter_new(vec);
    int* element_iter = NULL;
    if( *element > 18000 )
    {
        printf("element: %d\n", *element);
    }
    while( (element_iter = (int*)vec_iter_next(iter)) )
    {
        if( *element_iter == *element )
            goto done;
    }
    vec_push(vec, element);

done:;
    vec_iter_free(iter);
}

struct TaskInitSceneDat
{
    enum TaskInitSceneDatStep step;
    struct GGame* game;
    struct GIOQueue* io;

    struct TaskStep task_steps[STEP_INIT_SCENE_DAT_STEP_COUNT];

    struct SceneBuilder* scene_builder;

    struct DashMap* flotype_hmap;
    struct DashMap* textures_hmap;
    struct DashMap* scenery_hmap;
    struct DashMap* models_hmap;
    struct DashMap* config_loc_hmap;
    struct DashMap* animframes_hmap;
    // For cleanup.
    struct DashMap* animbaseframes_hmap;
    struct DashMap* sequences_hmap;

    struct Vec* queued_texture_ids_vec;
    struct Vec* queued_scenery_models_vec;
    int animbaseframes_count;

    struct Chunk chunks[CHUNKS_COUNT];
    int chunks_count;
    int chunks_width;

    int world_x;
    int world_z;
    int scene_size;

    struct Vec* reqid_queue_vec;
    int reqid_queue_inflight_count;

    struct Painter* painter;
};

static void
queue_scenery_models(
    struct TaskInitSceneDat* task,
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
    // shapes 10 and 11 are used in old dat caches and they have one model.
    // TODO: Clean this up.
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
                // 629
                // 2280 is lumby flag in old cache
                // 2050 lumby church windows
                // 617 is the roofs in lumby
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

struct TaskInitSceneDat*
task_init_scene_dat_new(
    struct GGame* game,
    int map_sw_x,
    int map_sw_z,
    int map_ne_x,
    int map_ne_z)
{
    struct DashMapConfig config = { 0 };
    struct TaskInitSceneDat* task = malloc(sizeof(struct TaskInitSceneDat));
    memset(task, 0, sizeof(struct TaskInitSceneDat));
    task->step = STEP_INIT_SCENE_DAT_INITIAL;
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

    task->queued_texture_ids_vec = vec_new(sizeof(int), 512);
    task->queued_scenery_models_vec = vec_new(sizeof(int), 512);

    int buffer_size = 1024 * sizeof(struct TextureEntry) * 4;

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
        .entry_size = sizeof(struct FlotypeEntry),
    };
    task->flotype_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct SceneryEntry),
    };
    task->scenery_hmap = dashmap_new(&config, 0);

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
        .entry_size = sizeof(struct ConfigLocEntry),
    };
    task->config_loc_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size * 8),
        .buffer_size = buffer_size * 8,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct AnimframeEntry),
    };
    task->animframes_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct AnimbaseframesEntry),
    };
    task->animbaseframes_hmap = dashmap_new(&config, 0);

    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct SequenceEntry),
    };
    task->sequences_hmap = dashmap_new(&config, 0);

    return task;
}

static enum GameTaskStatus
step_terrain_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_1_LOAD_TERRAIN];

    struct CacheMapLocs* locs = NULL;
    struct DashMapIter* iter = NULL;
    struct GIOMessage message = { 0 };
    struct CacheMapTerrain* terrain = NULL;
    int reqid = 0;
    struct SceneryEntry* scenery_entry = NULL;
    int mapxz = 0;

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);
        for( int i = 0; i < task->chunks_count; i++ )
        {
            reqid = gio_assets_dat_map_terrain_load(task->io, task->chunks[i].x, task->chunks[i].z);

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

            terrain = map_terrain_new_from_decode_flags(
                message.data, message.data_size, map_x, map_z, MAP_TERRAIN_DECODE_U8);

            scenebuilder_cache_map_terrain(task->scene_builder, map_x, map_z, terrain);

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_PROCESS;
    case TS_PROCESS:

        step_stage->step = TS_DONE;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_flotype_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_2_LOAD_FLOORTYPE];

    int reqid = 0;
    struct GIOMessage message = { 0 };
    struct ConfigMap* configmap = NULL;

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);
        reqid = gio_assets_dat_config_flotype_file_load(task->io);
        vec_push(task->reqid_queue_vec, &reqid);

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;
            assert(task->reqid_queue_inflight_count == 0);

            struct RSBuffer buffer = {
                .data = message.data,
                .size = message.data_size,
                .position = 0,
            };
            struct CacheConfigOverlay* flotype = NULL;

            int count = g2(&buffer);
            for( int i = 0; i < count; i++ )
            {
                flotype = malloc(sizeof(struct CacheConfigOverlay));
                memset(flotype, 0, sizeof(struct CacheConfigOverlay));
                flotype->_id = i;

                buffer.position += config_floortype_overlay_decode_inplace(
                    flotype, buffer.data + buffer.position, buffer.size - buffer.position);

                struct FlotypeEntry* flotype_entry =
                    (struct FlotypeEntry*)dashmap_search(task->flotype_hmap, &i, DASHMAP_INSERT);
                assert(flotype_entry && "Flotype must be inserted into hmap");
                flotype_entry->id = i;
                flotype_entry->flotype = flotype;

                scenebuilder_cache_flotype(task->scene_builder, i, flotype);
            }

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
step_scenery_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_3_LOAD_SCENERY];

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
            reqid = gio_assets_dat_map_scenery_load(task->io, task->chunks[i].x, task->chunks[i].z);
            vec_push(task->reqid_queue_vec, &reqid);
        }
        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            struct CacheMapLocs* locs = map_locs_new_from_decode(message.data, message.data_size);
            locs->_chunk_mapx = message.param_b >> 16;
            locs->_chunk_mapz = message.param_b & 0xFFFF;

            mapxz = MAPXZ(locs->_chunk_mapx, locs->_chunk_mapz);

            struct SceneryEntry* scenery_entry =
                (struct SceneryEntry*)dashmap_search(task->scenery_hmap, &mapxz, DASHMAP_INSERT);
            assert(scenery_entry && "Scenery entry must be inserted into hmap");
            scenery_entry->id = mapxz;
            scenery_entry->mapx = locs->_chunk_mapx;
            scenery_entry->mapz = locs->_chunk_mapz;
            scenery_entry->locs = locs;

            scenebuilder_cache_map_locs(
                task->scene_builder, locs->_chunk_mapx, locs->_chunk_mapz, locs);

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_PROCESS;
    case TS_PROCESS:

        step_stage->step = TS_DONE;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_scenery_config_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_4_LOAD_SCENERY_CONFIG];

    struct CacheMapLocs* locs = NULL;
    struct CacheMapLoc* loc = NULL;
    struct DashMapIter* iter = NULL;
    struct GIOMessage message = { 0 };
    int reqid = 0;
    struct SceneryEntry* scenery_entry = NULL;
    int mapxz = 0;

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);
        reqid = gio_assets_dat_config_scenery_fileidx_load(task->io);
        vec_push(task->reqid_queue_vec, &reqid);
        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;
            assert(task->reqid_queue_inflight_count == 0);

            struct FileListDatIndexed* filelist_indexed =
                cu_filelist_dat_indexed_new_from_filepack(message.data, message.data_size);

            iter = dashmap_iter_new(task->scenery_hmap);
            while( (scenery_entry = (struct SceneryEntry*)dashmap_iter_next(iter)) )
            {
                locs = scenery_entry->locs;
                for( int i = 0; i < locs->locs_count; i++ )
                {
                    struct CacheConfigLocation* config_loc =
                        malloc(sizeof(struct CacheConfigLocation));
                    memset(config_loc, 0, sizeof(struct CacheConfigLocation));

                    loc = &locs->locs[i];
                    int offset = filelist_indexed->offsets[loc->loc_id];
                    if( loc->loc_id == 187 )
                    {
                        printf("loc_id: %d\n", loc->loc_id);
                    }

                    config_locs_decode_inplace(
                        config_loc,
                        filelist_indexed->data + offset,
                        filelist_indexed->data_size - offset,
                        CONFIG_LOC_DECODE_DAT);

                    struct ConfigLocEntry* config_loc_entry =
                        (struct ConfigLocEntry*)dashmap_search(
                            task->config_loc_hmap, &loc->loc_id, DASHMAP_INSERT);
                    assert(config_loc_entry && "Config loc entry must be inserted into hmap");
                    assert(config_loc_entry->id == loc->loc_id);
                    config_loc_entry->id = loc->loc_id;
                    config_loc_entry->shape_select = loc->shape_select;
                    config_loc_entry->config_loc = config_loc;
                    config_loc->_id = loc->loc_id;
                    assert(
                        config_loc_entry->id == loc->loc_id &&
                        config_loc_entry->config_loc->_id == loc->loc_id);

                    scenebuilder_cache_config_loc(task->scene_builder, loc->loc_id, config_loc);
                }
            }
            dashmap_iter_free(iter);

            gioq_release(task->io, &message);
        }

        iter = dashmap_iter_new(task->config_loc_hmap);
        struct ConfigLocEntry* config_loc_entry = NULL;
        while( (config_loc_entry = (struct ConfigLocEntry*)dashmap_iter_next(iter)) )
        {
            assert(config_loc_entry->id == config_loc_entry->config_loc->_id);
        }
        dashmap_iter_free(iter);

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_PROCESS;
    case TS_PROCESS:

        step_stage->step = TS_DONE;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_models_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_5_LOAD_MODELS];

    int reqid = 0;
    struct GIOMessage message = { 0 };
    struct DashMapIter* iter = NULL;
    struct ConfigLocEntry* config_loc_entry = NULL;

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);

        iter = dashmap_iter_new(task->config_loc_hmap);
        while( (config_loc_entry = (struct ConfigLocEntry*)dashmap_iter_next(iter)) )
        {
            queue_scenery_models(
                task, config_loc_entry->config_loc, config_loc_entry->shape_select);
        }
        dashmap_iter_free(iter);

        int count = vec_size(task->queued_scenery_models_vec);
        for( int i = 0; i < count; i++ )
        {
            int model_id = *(int*)vec_get(task->queued_scenery_models_vec, i);

            reqid = gio_assets_dat_models_load(task->io, model_id);
            vec_push(task->reqid_queue_vec, &reqid);
        }

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);
        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            struct ModelEntry* model_entry = (struct ModelEntry*)dashmap_search(
                task->models_hmap, &message.param_b, DASHMAP_INSERT);
            assert(model_entry && "Model must be inserted into hmap");
            model_entry->id = message.param_b;

            if( message.param_b == 2085 )
            {
                printf("IIII %d\n", message.param_b);
            }
            model_entry->model = model_new_decode(message.data, message.data_size);
            model_entry->model->_id = message.param_b;
            scenebuilder_cache_model(task->scene_builder, message.param_b, model_entry->model);

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
step_textures_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_6_LOAD_TEXTURES];

    struct GIOMessage message = { 0 };
    int reqid = 0;

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);

        reqid = gio_assets_dat_config_texture_sprites_load(task->io);
        vec_push(task->reqid_queue_vec, &reqid);

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;
            assert(task->reqid_queue_inflight_count == 0);

            struct FileListDat* filelist =
                filelist_dat_new_from_decode(message.data, message.data_size);

            // Hardcoded to 50 in the deob. Not sure why.
            for( int i = 0; i < 50; i++ )
            {
                if( i == 14 )
                {
                    printf("IIII %d\n", i);
                }
                struct CacheDatTexture* texture =
                    cache_dat_texture_new_from_filelist_dat(filelist, i, 0);

                struct DashTexture* dash_texture = texture_new_from_texture_sprite(texture);
                assert(dash_texture != NULL);

                struct TextureEntry* texture_entry =
                    (struct TextureEntry*)dashmap_search(task->textures_hmap, &i, DASHMAP_INSERT);
                assert(texture_entry && "Texture must be inserted into hmap");

                texture_entry->id = i;
                texture_entry->texture = dash_texture;

                dash3d_add_texture(task->game->sys_dash, i, dash_texture);
            }
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
step_sequences_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_7_LOAD_SEQUENCES];

    struct ConfigLocEntry* config_loc_entry = NULL;
    struct DashMapIter* iter = NULL;
    struct GIOMessage message = { 0 };

    int reqid = 0;
    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);

        reqid = gio_assets_dat_config_seq_file_load(task->io);
        vec_push(task->reqid_queue_vec, &reqid);

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        struct CacheAnimframe* animframe = NULL;
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;
            assert(task->reqid_queue_inflight_count == 0);

            struct RSBuffer buffer = { .data = message.data,
                                       .size = message.data_size,
                                       .position = 0 };
            int count = g2(&buffer);
            for( int i = 0; i < count; i++ )
            {
                struct CacheDatSequence* sequence = malloc(sizeof(struct CacheDatSequence));
                memset(sequence, 0, sizeof(struct CacheDatSequence));

                buffer.position += config_dat_sequence_decode_inplace(
                    sequence, buffer.data + buffer.position, buffer.size - buffer.position);

                struct SequenceEntry* sequence_entry =
                    (struct SequenceEntry*)dashmap_search(task->sequences_hmap, &i, DASHMAP_INSERT);
                assert(sequence_entry && "Sequence must be inserted into hmap");
                sequence_entry->id = i;
                sequence_entry->sequence = sequence;

                scenebuilder_cache_dat_sequence(task->scene_builder, i, sequence);
            }

            // animframe = cache_dat_animframe_new_decode(message.data, message.data_size);
            // struct AnimframeEntry* animframe_entry = (struct AnimframeEntry*)dashmap_search(
            //     task->animframes_hmap, &message.param_b, DASHMAP_INSERT);
            // assert(animframe_entry && "Animframe must be inserted into hmap");
            // animframe_entry->id = message.param_b;
            // animframe_entry->animframe = animframe;

            // scenebuilder_cache_animframe(
            //     task->scene_builder, message.param_b, animframe_entry->animframe);

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
step_animframes_index_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_8_LOAD_ANIMFRAMES_INDEX];

    struct GIOMessage message = { 0 };
    int reqid = 0;
    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);
        reqid = gio_assets_dat_config_versionlist_animindex_load(task->io);
        vec_push(task->reqid_queue_vec, &reqid);
        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;
            assert(task->reqid_queue_inflight_count == 0);

            task->animbaseframes_count = message.data_size / 2;

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
step_animbaseframes_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_9_LOAD_ANIMBASEFRAMES];

    struct GIOMessage message = { 0 };
    int reqid = 0;
    struct ConfigLocEntry* config_loc_entry = NULL;
    struct DashMapIter* iter = NULL;

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);

        for( int i = 0; i < task->animbaseframes_count; i++ )
        {
            reqid = gio_assets_dat_animbaseframes_load(task->io, i);
            vec_push(task->reqid_queue_vec, &reqid);
        }

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
    {
        struct CacheDatAnimBaseFrames* animbaseframes = NULL;
        struct AnimbaseframesEntry* animbaseframes_entry = NULL;
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            if( message.data == NULL )
                goto release;

            animbaseframes = cache_dat_animbaseframes_new_decode(message.data, message.data_size);

            animbaseframes_entry = (struct AnimbaseframesEntry*)dashmap_search(
                task->animbaseframes_hmap, &message.param_b, DASHMAP_INSERT);
            assert(animbaseframes_entry && "Animbaseframes must be inserted into hmap");
            animbaseframes_entry->id = message.param_b;
            animbaseframes_entry->animbaseframes = animbaseframes;

            for( int i = 0; i < animbaseframes->frame_count; i++ )
            {
                struct CacheAnimframe* animframe = &animbaseframes->frames[i];
                struct AnimframeEntry* animframe_entry = (struct AnimframeEntry*)dashmap_search(
                    task->animframes_hmap, &animframe->id, DASHMAP_INSERT);
                assert(animframe_entry && "Animframe must be inserted into hmap");
                animframe_entry->id = animframe->id;
                animframe_entry->animframe = animframe;

                scenebuilder_cache_animframe(task->scene_builder, animframe->id, animframe);
            }

        release:;
            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_PROCESS;
    }
    case TS_PROCESS:
        break;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_sounds_load(struct TaskInitSceneDat* task)
{
    return GAMETASK_STATUS_COMPLETED;
}

enum GameTaskStatus
task_init_scene_dat_step(struct TaskInitSceneDat* task)
{
    struct GIOMessage message;
    struct DashMapIter* iter = NULL;
    struct CacheConfigSequence* sequence = NULL;

    switch( task->step )
    {
    case STEP_INIT_SCENE_DAT_INITIAL:
    {
        task->step = STEP_INIT_SCENE_DAT_1_LOAD_TERRAIN;
    }
    case STEP_INIT_SCENE_DAT_1_LOAD_TERRAIN:
    {
        if( step_terrain_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_DAT_2_LOAD_FLOORTYPE;
    }
    case STEP_INIT_SCENE_DAT_2_LOAD_FLOORTYPE:
    {
        if( step_flotype_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_DAT_3_LOAD_SCENERY;
    }
    case STEP_INIT_SCENE_DAT_3_LOAD_SCENERY:
    {
        if( step_scenery_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_DAT_4_LOAD_SCENERY_CONFIG;
    }
    case STEP_INIT_SCENE_DAT_4_LOAD_SCENERY_CONFIG:
    {
        if( step_scenery_config_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_DAT_5_LOAD_MODELS;
    }
    case STEP_INIT_SCENE_DAT_5_LOAD_MODELS:
    {
        if( step_models_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_DAT_6_LOAD_TEXTURES;
    }
    case STEP_INIT_SCENE_DAT_6_LOAD_TEXTURES:
    {
        if( step_textures_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_DAT_7_LOAD_SEQUENCES;
    }
    case STEP_INIT_SCENE_DAT_7_LOAD_SEQUENCES:
    {
        if( step_sequences_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_DAT_8_LOAD_ANIMFRAMES_INDEX;
    }
    case STEP_INIT_SCENE_DAT_8_LOAD_ANIMFRAMES_INDEX:
    {
        if( step_animframes_index_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_DAT_9_LOAD_ANIMBASEFRAMES;
    }
    case STEP_INIT_SCENE_DAT_9_LOAD_ANIMBASEFRAMES:
    {
        if( step_animbaseframes_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_DAT_10_LOAD_SOUNDS;
    }
    case STEP_INIT_SCENE_DAT_10_LOAD_SOUNDS:
    {
        if( step_sounds_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_DAT_DONE;
    }
    case STEP_INIT_SCENE_DAT_DONE:
    {
        task->game->scene = scenebuilder_load(task->scene_builder);

        return GAMETASK_STATUS_COMPLETED;
    }
    default:
        goto bad_step;
    }

bad_step:;
    assert(false && "Bad step in task_init_scene_step");
    return GAMETASK_STATUS_FAILED;
}

void
task_init_scene_dat_free(struct TaskInitSceneDat* task)
{
    free(task);
}
