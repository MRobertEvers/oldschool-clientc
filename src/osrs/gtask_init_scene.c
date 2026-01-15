#ifndef GTASK_INIT_SCENE_U_C
#define GTASK_INIT_SCENE_U_C
#include "gtask_init_scene.h"

#include "datastruct/list.h"
#include "datastruct/vec.h"
#include "graphics/dash.h"
#include "gtask.h"
#include "libg.h"
#include "osrs/configmap.h"
#include "osrs/dashlib.h"
#include "osrs/gio_assets.h"
#include "osrs/painters.h"
#include "osrs/scenebuilder.h"
#include "osrs/tables/config_floortype.h"
#include "osrs/tables/config_locs.h"
#include "osrs/tables/maps.h"
#include "osrs/tables/model.h"
#include "osrs/tables/sprites.h"
#include "osrs/tables/textures.h"
// #include "osrs/terrain.h"
#include "osrs/texture.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include "chunks.u.c"
#include "scene_builder.u.c"
// clang-format on

// static void
// load_model(
//     struct SceneModel* scene_model,
//     struct CacheConfigLocation* loc_config,
//     struct Cache* cache,
//     struct ModelCache* model_cache,
//     struct CacheConfigSequenceTable* sequence_table,
//     int shape_select,
//     int orientation,
//     int sw_height,
//     int se_height,
//     int ne_height,
//     int nw_height)
// {
//     struct CacheConfigSequence* sequence = NULL;
//     struct CacheFramemap* framemap = NULL;
//     struct CacheFrame* frame = NULL;
//     struct CacheArchive* frame_archive = NULL;
//     struct FileList* frame_filelist = NULL;
//     int* shapes = loc_config->shapes;
//     int** model_id_sets = loc_config->models;
//     int* lengths = loc_config->lengths;
//     int shapes_and_model_count = loc_config->shapes_and_model_count;

//     struct CacheModel** models = NULL;
//     int model_count = 0;
//     int* model_ids = NULL;

//     struct CacheModel* model = NULL;

//     if( !model_id_sets )
//         return;

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

//             model = model_cache_checkout(model_cache, cache, model_id);
//             assert(model);
//             models[model_count] = model;
//             model_ids[model_count] = model_id;
//             model_count++;
//         }
//     }
//     else
//     {
//         int count = shapes_and_model_count;

//         models = malloc(sizeof(struct CacheModel*) * count);
//         memset(models, 0, sizeof(struct CacheModel*) * count);
//         model_ids = malloc(sizeof(int) * count);
//         memset(model_ids, 0, sizeof(int) * count);

//         bool found = false;
//         for( int i = 0; i < count; i++ )
//         {
//             int count_inner = lengths[i];

//             int loc_type = shapes[i];
//             if( loc_type == shape_select )
//             {
//                 assert(count_inner <= count);
//                 for( int j = 0; j < count_inner; j++ )
//                 {
//                     int model_id = model_id_sets[i][j];
//                     assert(model_id);

//                     model = model_cache_checkout(model_cache, cache, model_id);
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

//     if( model->_id == 2255 && scene_model->yaw != 0 )
//     {
//         int i = 0;
//     }

//     // Sequences don't account for rotations, so models must be rotated AFTER the animation is
//     // applied.
//     if( loc_config->seq_id != -1 )
//     {
//         // TODO: account for transforms.
//         // Also, I believe this is the only overridden transform.
//         //         const isEntity =
//         // seqId !== -1 ||
//         // locType.transforms !== undefined ||
//         // locLoadType === LocLoadType.NO_MODELS;

//         scene_model->yaw = 512 * orientation;
//         scene_model->yaw %= 2048;
//         orientation = 0;
//     }

//     loc_apply_transforms(
//         loc_config, model, orientation, sw_height, se_height, ne_height, nw_height);

//     scene_model->model = model;
//     scene_model->model_id = model_ids[0];

//     scene_model->light_ambient = loc_config->ambient;
//     scene_model->light_contrast = loc_config->contrast;
//     scene_model->sharelight = loc_config->sharelight;

//     scene_model->__loc_id = loc_config->_id;

//     if( model->vertex_bone_map )
//         scene_model->vertex_bones =
//             modelbones_new_decode(model->vertex_bone_map, model->vertex_count);
//     if( model->face_bone_map )
//         scene_model->face_bones = modelbones_new_decode(model->face_bone_map, model->face_count);

//     scene_model->sequence = NULL;
//     if( loc_config->seq_id != -1 )
//     {
//         scene_model_vertices_create_original(scene_model);

//         if( model->face_alphas )
//             scene_model_face_alphas_create_original(scene_model);

//         sequence = config_sequence_table_get_new(sequence_table, loc_config->seq_id);
//         assert(sequence);
//         assert(sequence->frame_lengths);
//         scene_model->sequence = sequence;

//         assert(scene_model->frames == NULL);
//         scene_model->frames = malloc(sizeof(struct CacheFrame*) * sequence->frame_count);
//         memset(scene_model->frames, 0, sizeof(struct CacheFrame*) * sequence->frame_count);

//         int frame_id = sequence->frame_ids[0];
//         int frame_archive_id = (frame_id >> 16) & 0xFFFF;
//         // Get the frame definition ID from the second 2 bytes of the sequence frame ID The
//         //     first 2 bytes are the sequence ID,
//         //     the second 2 bytes are the frame archive ID

//         frame_archive = cache_archive_new_load(cache, CACHE_ANIMATIONS, frame_archive_id);
//         frame_filelist = filelist_new_from_cache_archive(frame_archive);
//         for( int i = 0; i < sequence->frame_count; i++ )
//         {
//             assert(((sequence->frame_ids[i] >> 16) & 0xFFFF) == frame_archive_id);
//             // assert(i < frame_filelist->file_count);

//             int frame_id = sequence->frame_ids[i];
//             int frame_archive_id = (frame_id >> 16) & 0xFFFF;
//             int frame_file_id = frame_id & 0xFFFF;

//             assert(frame_file_id > 0);
//             assert(frame_file_id - 1 < frame_filelist->file_count);

//             char* frame_data = frame_filelist->files[frame_file_id - 1];
//             int frame_data_size = frame_filelist->file_sizes[frame_file_id - 1];
//             int framemap_id = framemap_id_from_frame_archive(frame_data, frame_data_size);

//             if( !scene_model->framemap )
//             {
//                 framemap = framemap_new_from_cache(cache, framemap_id);
//                 scene_model->framemap = framemap;
//             }

//             frame = frame_new_decode2(frame_id, scene_model->framemap, frame_data,
//             frame_data_size);

//             scene_model->frames[scene_model->frame_count++] = frame;
//         }

//         cache_archive_free(frame_archive);
//         frame_archive = NULL;
//         filelist_free(frame_filelist);
//         frame_filelist = NULL;
//     }

//     free(models);
//     free(model_ids);
// }

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

#define CHUNKS_COUNT 36
struct GTaskInitScene
{
    enum GTaskInitSceneStep step;
    struct GGame* game;
    struct GIOQueue* io;

    struct CacheMapLocsIter* scenery_iter;
    struct CacheMapTerrainIter* terrain_iter;

    struct CacheMapTerrain* terrain_definitions[CHUNKS_COUNT];
    struct CacheMapLocs* scenery_locs[CHUNKS_COUNT];
    int scenery_locs_count;
    int terrain_count;

    struct SceneBuilder* scene_builder;

    struct DashMap* scenery_configmap;
    struct DashMap* underlay_configmap;
    struct DashMap* overlay_configmap;
    struct DashMap* texture_definitions_configmap;
    struct DashMap* models_hmap;
    struct DashMap* spritepacks_hmap;
    struct DashMap* textures_hmap;
    struct Vec* queued_scenery_models_ids;
    struct Vec* queued_texture_ids;

    struct Chunk chunks[CHUNKS_COUNT];
    int chunks_count;
    int chunks_width;

    int world_x;
    int world_z;
    int scene_size;

    uint32_t reqid_scenery[CHUNKS_COUNT];
    uint32_t reqid_scenery_count;
    uint32_t reqid_scenery_inflight;

    uint32_t reqid_terrain[CHUNKS_COUNT];
    uint32_t reqid_terrain_count;
    uint32_t reqid_terrain_inflight;

    uint32_t reqid_scenery_config;

    uint32_t reqid_model_count;
    uint32_t reqid_model_inflight;
    uint32_t* reqid_models;

    uint32_t reqid_texture_count;
    uint32_t reqid_texture_inflight;
    uint32_t* reqid_textures;

    uint32_t reqid_spritepack_count;
    uint32_t reqid_spritepack_inflight;
    uint32_t* reqid_spritepacks;

    uint32_t reqid_texture_definitions;
    uint32_t reqid_underlay;
    uint32_t reqid_overlay;

    struct Painter* painter;
};

static int
compare_terrain_by_z_x(
    const void* a,
    const void* b)
{
    struct CacheMapTerrain* terrain_a = *(struct CacheMapTerrain**)a;
    struct CacheMapTerrain* terrain_b = *(struct CacheMapTerrain**)b;

    // First compare by z coordinate (map_z)
    if( terrain_a->map_z != terrain_b->map_z )
    {
        return terrain_a->map_z - terrain_b->map_z;
    }
    // Then compare by x coordinate (map_x)
    return terrain_a->map_x - terrain_b->map_x;
}

static void
sort_terrain_definitions(
    struct CacheMapTerrain** terrain_definitions,
    int size)
{
    // Sort by lowest z, then lowest x.
    qsort(terrain_definitions, size, sizeof(struct CacheMapTerrain*), compare_terrain_by_z_x);
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

//     // Sequences don't account for rotations, so models must be rotated AFTER the animation is
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
//     //     memset(scene_model->frames, 0, sizeof(struct CacheFrame*) * sequence->frame_count);

//     //     int frame_id = sequence->frame_ids[0];
//     //     int frame_archive_id = (frame_id >> 16) & 0xFFFF;
//     //     // Get the frame definition ID from the second 2 bytes of the sequence frame ID The
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
queue_model_unique(
    struct GTaskInitScene* task,
    int model_id)
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
    struct GTaskInitScene* task,
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
            // Ignore shape select because some locs don't use the shape select stored.
            // if( loc_type == shape_select )
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
    map_locs_iter_begin(iter);
    while( (loc = map_locs_iter_next(iter, NULL)) )
    {
        vec_push(vec, &loc->loc_id);
    }
    return vec;
}

struct GTaskInitScene*
gtask_init_scene_new(
    struct GGame* game,
    int world_x,
    int world_z,
    int scene_size)
{
    struct ChunksInView chunks = { 0 };
    struct DashMapConfig config = { 0 };
    struct GTaskInitScene* task = malloc(sizeof(struct GTaskInitScene));
    memset(task, 0, sizeof(struct GTaskInitScene));
    task->step = STEP_INIT_SCENE_INITIAL;
    task->game = game;
    task->io = game->io;

    task->world_x = world_x;
    task->world_z = world_z;
    task->scene_size = scene_size;

    chunks_inview(task->chunks, world_x, world_z, scene_size, &chunks);
    task->chunks_count = chunks.count;
    task->chunks_width = chunks.width;

    game->sys_painter = painter_new(
        chunks.width * CHUNK_TILE_SIZE, chunks.width * CHUNK_TILE_SIZE, MAP_TERRAIN_LEVELS);
    game->sys_painter_buffer = painter_buffer_new();

    task->painter = game->sys_painter;

    int minz = 1000;
    int maxz = 0;
    int minx = 1000;
    int maxx = 0;
    for( int i = 0; i < task->chunks_count; i++ )
    {
        if( task->chunks[i].x < minx )
            minx = task->chunks[i].x;
        if( task->chunks[i].x > maxx )
            maxx = task->chunks[i].x;
        if( task->chunks[i].z < minz )
            minz = task->chunks[i].z;
        if( task->chunks[i].z > maxz )
            maxz = task->chunks[i].z;
    }

    task->scene_builder = scenebuilder_new_painter(task->painter, minx, minz, maxx, maxz);

    task->chunks_count = chunks.count;
    task->chunks_width = chunks.width;

    task->queued_scenery_models_ids = vec_new(sizeof(int), 512);
    task->queued_texture_ids = vec_new(sizeof(int), 512);
    int buffer_size = 1024 * 32;
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

    return task;
}

void
gtask_init_scene_free(struct GTaskInitScene* task)
{
    vec_free(task->queued_scenery_models_ids);
    vec_free(task->queued_texture_ids);

    free(dashmap_buffer_ptr(task->models_hmap));
    free(dashmap_buffer_ptr(task->textures_hmap));
    free(dashmap_buffer_ptr(task->spritepacks_hmap));

    dashmap_free(task->models_hmap);
    dashmap_free(task->textures_hmap);
    dashmap_free(task->spritepacks_hmap);

    configmap_free(task->scenery_configmap);
    configmap_free(task->underlay_configmap);
    configmap_free(task->overlay_configmap);
    configmap_free(task->texture_definitions_configmap);

    map_locs_iter_free(task->scenery_iter);
    map_terrain_iter_free(task->terrain_iter);

    free(task);
}

enum GTaskStatus
gtask_init_scene_step(struct GTaskInitScene* task)
{
    struct GIOMessage message;
    struct DashMapIter* iter = NULL;
    struct CacheMapLocsIter* map_locs_iter;
    struct CacheMapLoc* loc;
    struct CacheMapLocs* locs;
    struct CacheConfigLocation* config_loc;
    struct CacheModel* model = NULL;
    struct ModelEntry* model_entry = NULL;
    struct CacheSpritePack* spritepack = NULL;
    struct CacheTexture* texture_definition = NULL;
    struct DashTexture* texture = NULL;
    struct TextureEntry* texture_entry = NULL;
    struct SpritePackEntry* spritepack_entry = NULL;
    struct ChunkOffset chunk_offset;
    bool status = false;

    switch( task->step )
    {
    case STEP_INIT_SCENE_INITIAL:
    {
        task->step = STEP_INIT_SCENE_1_LOAD_SCENERY;
    }
    case STEP_INIT_SCENE_1_LOAD_SCENERY:
    {
        if( task->reqid_scenery_count == 0 )
        {
            for( int i = 0; i < task->chunks_count; i++ )
            {
                task->reqid_scenery[i] =
                    gio_assets_map_scenery_load(task->io, task->chunks[i].x, task->chunks[i].z);
            }
            task->reqid_scenery_count = task->chunks_count;
            task->reqid_scenery_inflight = task->chunks_count;
        }

        while( gioq_poll(task->io, &message) )
        {
            task->reqid_scenery_inflight--;

            locs = map_locs_new_from_decode(message.data, message.data_size);
            task->scenery_locs[task->scenery_locs_count++] = locs;
            locs->_chunk_mapx = message.param_b >> 16;
            locs->_chunk_mapz = message.param_b & 0xFFFF;

            scenebuilder_cache_map_locs(
                task->scene_builder, locs->_chunk_mapx, locs->_chunk_mapz, locs);

            gioq_release(task->io, &message);
        }
        if( task->reqid_scenery_inflight != 0 )
            return GTASK_STATUS_PENDING;

        task->scenery_iter = map_locs_iter_new_from_ptrs(
            task->scenery_locs, task->scenery_locs_count, task->chunks_width);

        task->step = STEP_INIT_SCENE_2_LOAD_SCENERY_CONFIG;
    }
    case STEP_INIT_SCENE_2_LOAD_SCENERY_CONFIG:
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
        map_locs_iter_begin(task->scenery_iter);
        while( (loc = map_locs_iter_next(task->scenery_iter, &chunk_offset)) )
        {
            config_loc = configmap_get(task->scenery_configmap, loc->loc_id);

            queue_scenery_models(task, config_loc, loc->shape_select);
        }

        task->step = STEP_INIT_SCENE_3_LOAD_SCENERY_MODELS;
    }
    case STEP_INIT_SCENE_3_LOAD_SCENERY_MODELS:
    {
        if( task->reqid_model_count == 0 )
        {
            int count = vec_size(task->queued_scenery_models_ids);
            int* reqids = (int*)malloc(sizeof(int) * count);

            for( int i = 0; i < count; i++ )
            {
                int model_id = *(int*)vec_get(task->queued_scenery_models_ids, i);

                reqids[i] = gio_assets_model_load(task->io, model_id);
            }

            task->reqid_model_count = count;
            task->reqid_model_inflight = count;
            task->reqid_models = reqids;
        }

        while( gioq_poll(task->io, &message) )
        {
            task->reqid_model_inflight--;

            model = model_new_decode(message.data, message.data_size);
            model_entry = (struct ModelEntry*)dashmap_search(
                task->models_hmap, &message.param_b, DASHMAP_INSERT);
            assert(model_entry && "Model must be inserted into hmap");
            model_entry->id = message.param_b;
            model_entry->model = model;

            scenebuilder_cache_model(task->scene_builder, message.param_b, model);

            gioq_release(task->io, &message);
        }

        if( task->reqid_model_inflight != 0 )
            return GTASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_4_LOAD_TERRAIN;
    }
    case STEP_INIT_SCENE_4_LOAD_TERRAIN:
    {
        if( task->reqid_terrain_count == 0 )
        {
            for( int i = 0; i < task->chunks_count; i++ )
            {
                task->reqid_terrain[i] =
                    gio_assets_map_terrain_load(task->io, task->chunks[i].x, task->chunks[i].z);
            }
            task->reqid_terrain_count = task->chunks_count;
            task->reqid_terrain_inflight = task->chunks_count;
        }

        while( gioq_poll(task->io, &message) )
        {
            int map_x = (message.param_b >> 16) & 0xFFFF;
            int map_z = message.param_b & 0xFFFF;
            task->reqid_terrain_inflight--;
            task->terrain_definitions[task->terrain_count++] =
                map_terrain_new_from_decode(message.data, message.data_size, map_x, map_z);

            scenebuilder_cache_map_terrain(
                task->scene_builder,
                map_x,
                map_z,
                task->terrain_definitions[task->terrain_count - 1]);

            gioq_release(task->io, &message);
        }

        if( task->reqid_terrain_inflight != 0 )
            return GTASK_STATUS_PENDING;

        sort_terrain_definitions(task->terrain_definitions, task->terrain_count);

        task->terrain_iter = map_terrain_iter_new_from_ptrs(
            task->terrain_definitions, task->terrain_count, task->chunks_width);

        task->step = STEP_INIT_SCENE_5_LOAD_UNDERLAY;
    }
    case STEP_INIT_SCENE_5_LOAD_UNDERLAY:
    {
        if( task->reqid_underlay == 0 )
            task->reqid_underlay = gio_assets_config_underlay_load(task->io);

        if( !gioq_poll(task->io, &message) )
            return GTASK_STATUS_PENDING;
        assert(message.message_id == task->reqid_underlay);

        task->underlay_configmap =
            configmap_new_from_packed(message.data, message.data_size, NULL, 0);

        gioq_release(task->io, &message);

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

        task->step = STEP_INIT_SCENE_6_LOAD_OVERLAY;
    }
    case STEP_INIT_SCENE_6_LOAD_OVERLAY:
    {
        if( task->reqid_overlay == 0 )
            task->reqid_overlay = gio_assets_config_overlay_load(task->io);

        if( !gioq_poll(task->io, &message) )
            return GTASK_STATUS_PENDING;
        assert(message.message_id == task->reqid_overlay);

        task->overlay_configmap =
            configmap_new_from_packed(message.data, message.data_size, NULL, 0);

        gioq_release(task->io, &message);

        /**
         * Queue Textures
         */

        struct CacheConfigOverlay* config_overlay = NULL;
        iter = dashmap_iter_new(task->overlay_configmap);
        while( (config_overlay = configmap_iter_next(iter)) )
            if( config_overlay->texture != -1 )
                vec_push_unique(task->queued_texture_ids, &config_overlay->texture);

        dashmap_iter_free(iter);

        struct DashMapIter* iter = dashmap_iter_new(task->models_hmap);
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
                    vec_push_unique(task->queued_texture_ids, &face_texture);
                }
            }
        }
        dashmap_iter_free(iter);

        map_locs_iter_begin(task->scenery_iter);
        while( (loc = map_locs_iter_next(task->scenery_iter, &chunk_offset)) )
        {
            config_loc = configmap_get(task->scenery_configmap, loc->loc_id);
            assert(config_loc && "Scenery configuration must be loaded");

            if( config_loc->retexture_count && config_loc->retextures_to )
            {
                for( int i = 0; i < config_loc->retexture_count; i++ )
                    vec_push_unique(task->queued_texture_ids, &config_loc->retextures_to[i]);
            }
        }

        task->step = STEP_INIT_SCENE_7_LOAD_TEXTURES;
    }
    case STEP_INIT_SCENE_7_LOAD_TEXTURES:
    {
        if( task->reqid_texture_definitions == 0 )
        {
            task->reqid_texture_definitions = gio_assets_texture_definitions_load(task->io);
        }
        if( !gioq_poll(task->io, &message) )
            return GTASK_STATUS_PENDING;
        assert(message.message_id == task->reqid_texture_definitions);

        task->texture_definitions_configmap = configmap_new_from_packed(
            message.data,
            message.data_size,
            (int*)vec_data(task->queued_texture_ids),
            vec_size(task->queued_texture_ids));

        gioq_release(task->io, &message);

        task->step = STEP_INIT_SCENE_8_LOAD_SPRITEPACKS;
    }
    case STEP_INIT_SCENE_8_LOAD_SPRITEPACKS:
    {
        if( task->reqid_spritepack_count == 0 )
        {
            struct Vec* sprite_pack_ids = vec_new(sizeof(int), 512);
            struct DashMapIter* iter = dashmap_iter_new(task->texture_definitions_configmap);
            while( (texture_definition = (struct CacheTexture*)configmap_iter_next(iter)) )
            {
                for( int i = 0; i < texture_definition->sprite_ids_count; i++ )
                    vec_push_unique(sprite_pack_ids, &texture_definition->sprite_ids[i]);
            }
            dashmap_iter_free(iter);

            int count = vec_size(sprite_pack_ids);
            int* reqids = (int*)malloc(sizeof(int) * count);
            int* ids = (int*)vec_data(sprite_pack_ids);

            for( int i = 0; i < count; i++ )
            {
                reqids[i] = gio_assets_spritepack_load(task->io, ids[i]);
            }

            task->reqid_spritepack_count = count;
            task->reqid_spritepack_inflight = count;

            vec_free(sprite_pack_ids);
        }

        while( gioq_poll(task->io, &message) )
        {
            task->reqid_spritepack_inflight--;
            spritepack =
                sprite_pack_new_decode(message.data, message.data_size, SPRITELOAD_FLAG_NORMALIZE);
            spritepack_entry =
                dashmap_search(task->spritepacks_hmap, &message.param_b, DASHMAP_INSERT);
            assert(spritepack_entry && "Spritepack must be inserted into hmap");
            spritepack_entry->id = message.param_b;
            spritepack_entry->spritepack = spritepack;

            gioq_release(task->io, &message);
        }

        if( task->reqid_spritepack_inflight != 0 )
            return GTASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_9_BUILD_TEXTURES;
    }
    case STEP_INIT_SCENE_9_BUILD_TEXTURES:
    {
        struct DashMapIter* iter = dashmap_iter_new(task->texture_definitions_configmap);
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
        task->step = STEP_INIT_SCENE_10_BUILD_WORLD3D;
    }
    case STEP_INIT_SCENE_10_BUILD_WORLD3D:
    {
        // struct TileHeights tile_heights;
        struct DashModel* dash_model = NULL;
        loc = NULL;
        // map_locs_iter_begin(task->scenery_iter);
        // while( (loc = map_locs_iter_next(task->scenery_iter, &chunk_offset)) )
        // {
        //     int sx = chunk_offset.x + loc->chunk_pos_x;
        //     int sz = chunk_offset.z + loc->chunk_pos_z;
        //     int slevel = loc->chunk_pos_level;

        //     config_loc = configmap_get(task->scenery_configmap, loc->loc_id);
        //     assert(config_loc && "Scenery configuration must be loaded");

        //     tile_heights = terrain_tile_heights_at(task->terrain_iter, sx, sz, slevel);

        //     // int entity = entity_new(task->game);

        //     switch( loc->shape_select )
        //     {
        //     case LOC_SHAPE_WALL_SINGLE_SIDE:
        //     {
        //         // Create the model.
        //         // Add model to game entity
        //         // Get the model number
        //         // Add to painter
        //         // Add painter to game entity
        //         dash_model = load_model(
        //             config_loc,
        //             task->models_hmap,
        //             loc->shape_select,
        //             loc->orientation,
        //             &tile_heights);
        //         // TODO: Load animations.

        //         struct DashPosition* position = malloc(sizeof(struct DashPosition));
        //         memset(position, 0, sizeof(struct DashPosition));
        //         position->x = sx * TILE_SIZE + config_loc->size_x * 64;
        //         position->z = sz * TILE_SIZE + config_loc->size_z * 64;
        //         position->y = tile_heights.height_center;

        //         int model_index = game_add_scene_element(task->game, dash_model, position);
        //         // model->region_x = tile_x * TILE_SIZE + size_x * 64;
        //         // model->region_z = tile_y * TILE_SIZE + size_y * 64;
        //         // model->region_height = height_center;

        //         // Push to game.
        //         //

        //         painter_add_wall(
        //             task->game->sys_painter,
        //             sx,
        //             sz,
        //             slevel,
        //             model_index,
        //             WALL_A,
        //             ROTATION_WALL_TYPE[loc->orientation]);

        //         // Update shademap
        //         // Update collisionmap
        //     }
        //     break;
        //     case LOC_SHAPE_WALL_TRI_CORNER:
        //     {
        //         // Create the model.
        //         // Add model to game entity
        //         // Get the model number
        //         // Add to painter
        //         // Add painter to game entity
        //         dash_model = load_model(
        //             config_loc,
        //             task->models_hmap,
        //             loc->shape_select,
        //             loc->orientation,
        //             &tile_heights);
        //         // TODO: Load animations.

        //         struct DashPosition* position = malloc(sizeof(struct DashPosition));
        //         memset(position, 0, sizeof(struct DashPosition));
        //         position->x = sx * TILE_SIZE + config_loc->size_x * 64;
        //         position->z = sz * TILE_SIZE + config_loc->size_z * 64;
        //         position->y = tile_heights.height_center;

        //         int model_index = game_add_scene_element(task->game, dash_model, position);
        //         // model->region_x = tile_x * TILE_SIZE + size_x * 64;
        //         // model->region_z = tile_y * TILE_SIZE + size_y * 64;
        //         // model->region_height = height_center;

        //         // Push to game.
        //         //

        //         painter_add_wall(
        //             task->game->sys_painter,
        //             sx,
        //             sz,
        //             slevel,
        //             model_index,
        //             WALL_A,
        //             ROTATION_WALL_TYPE[loc->orientation]);

        //         // Update shademap
        //         // Update collisionmap
        //     }
        //     break;
        //     }
        // }

        scenebuilder_cache_configmap_locs(task->scene_builder, task->scenery_configmap);
        scenebuilder_cache_configmap_underlay(task->scene_builder, task->underlay_configmap);
        scenebuilder_cache_configmap_overlay(task->scene_builder, task->overlay_configmap);
        task->game->scene = scenebuilder_load(task->scene_builder);

        task->step = STEP_INIT_SCENE_11_BUILD_TERRAIN3D;
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