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
#include "osrs/tables/config_floortype.h"
#include "osrs/tables/config_locs.h"
#include "osrs/tables/maps.h"
#include "osrs/tables/model.h"
#include "osrs/tables/sprites.h"
#include "osrs/tables/textures.h"
#include "osrs/terrain.h"
#include "osrs/texture.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include "chunks.u.c"
// clang-format on

struct ModelEntry
{
    int id;
    struct CacheModel* model;
};

struct TextureEntry
{
    int id;
    struct Texture* texture;
};

struct GTaskInitScene
{
    enum GTaskInitSceneStep step;
    struct GIOQueue* io;

    struct CacheMapLocsIter* scenery_iter;
    struct CacheMapTerrainIter* terrain_iter;

    struct CacheMapLocs* scenery_locs[9];
    struct CacheMapTerrain* terrain_definitions[9];
    int scenery_locs_count;
    int terrain_count;

    struct ConfigMap* scenery_configmap;
    struct ConfigMap* underlay_configmap;
    struct ConfigMap* overlay_configmap;
    struct ConfigMap* texture_definitions_hmap;
    struct HMap* models_hmap;
    struct HMap* spritepacks_hmap;
    struct HMap* textures_hmap;
    struct Vec* queued_scenery_models_ids;
    struct Vec* queued_texture_ids;

    struct Chunk chunks[9];
    int chunks_count;
    int chunks_width;

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
    struct Terrain* terrain;
};

static void
vec_push_unique(struct Vec* vec, int* element)
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
    map_locs_iter_begin(iter);
    while( (loc = map_locs_iter_next(iter)) )
    {
        vec_push(vec, &loc->loc_id);
    }
    return vec;
}

struct GTaskInitScene*
gtask_init_scene_new(struct GIOQueue* io, int world_x, int world_z, int scene_size)
{
    struct ChunksInView chunks = { 0 };
    struct HashConfig config = { 0 };
    struct GTaskInitScene* task = malloc(sizeof(struct GTaskInitScene));
    memset(task, 0, sizeof(struct GTaskInitScene));
    task->step = STEP_INIT_SCENE_INITIAL;
    task->io = io;
    task->world_x = world_x;
    task->world_z = world_z;
    task->scene_size = scene_size;

    chunks_inview(task->chunks, world_x, world_z, scene_size, &chunks);

    task->chunks_count = chunks.count;
    task->chunks_width = chunks.width;

    task->queued_scenery_models_ids = vec_new(sizeof(int), 512);
    task->queued_texture_ids = vec_new(sizeof(int), 512);
    int buffer_size = 1024 * 32;
    config = (struct HashConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ModelEntry),
    };
    task->models_hmap = hmap_new(&config, 0);

    config = (struct HashConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct TextureEntry),
    };
    task->textures_hmap = hmap_new(&config, 0);

    config = (struct HashConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct SpritePackEntry),
    };
    task->spritepacks_hmap = hmap_new(&config, 0);

    return task;
}

void
gtask_init_scene_free(struct GTaskInitScene* task)
{
    vec_free(task->queued_scenery_models_ids);
    vec_free(task->queued_texture_ids);

    free(hmap_buffer_ptr(task->models_hmap));
    free(hmap_buffer_ptr(task->textures_hmap));
    free(hmap_buffer_ptr(task->spritepacks_hmap));

    hmap_free(task->models_hmap);
    hmap_free(task->textures_hmap);
    hmap_free(task->spritepacks_hmap);

    configmap_free(task->scenery_configmap);
    configmap_free(task->underlay_configmap);
    configmap_free(task->overlay_configmap);
    configmap_free(task->texture_definitions_hmap);

    map_locs_iter_free(task->scenery_iter);
    map_terrain_iter_free(task->terrain_iter);

    free(task);
}

enum GTaskStatus
gtask_init_scene_step(struct GTaskInitScene* task)
{
    struct GIOMessage message;
    struct ConfigMapIter* iter = NULL;
    struct CacheMapLocsIter* map_locs_iter;
    struct CacheMapLoc* loc;
    struct CacheMapLocs* locs;
    struct CacheConfigLocation* config_loc;
    struct CacheModel* model = NULL;
    struct CacheModel** model_ptr = NULL;
    struct CacheSpritePack* spritepack = NULL;
    struct CacheTexture* texture_definition = NULL;
    struct Texture* texture = NULL;
    struct TextureEntry* texture_entry = NULL;
    struct SpritePackEntry* spritepack_entry = NULL;
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
        iter = configmap_iter_new(task->scenery_configmap);

        while( (config_loc = configmap_iter_next(iter)) )
            queue_scenery_models(task, config_loc, loc->shape_select);

        configmap_iter_free(iter);

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
            assert(model_ptr && "Model must be inserted into hmap");
            *model_ptr = model;

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
            task->reqid_terrain_inflight--;
            task->terrain_definitions[task->terrain_count++] =
                map_terrain_new_from_decode(message.data, message.data_size);
            gioq_release(task->io, &message);
        }

        if( task->reqid_terrain_inflight != 0 )
            return GTASK_STATUS_PENDING;

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
        iter = configmap_iter_new(task->overlay_configmap);
        while( (config_overlay = configmap_iter_next(iter)) )
            if( config_overlay->texture != -1 )
                vec_push_unique(task->queued_texture_ids, &config_overlay->texture);

        configmap_iter_free(iter);

        struct HMapIter* iter = hmap_iter_new(task->models_hmap);
        while( (model_ptr = (struct CacheModel**)hmap_iter_next(iter)) )
        {
            model = *model_ptr;
            for( int i = 0; i < model->face_count; i++ )
            {
                if( !model->face_textures )
                    continue;
                int face_texture = model->face_textures[i];
                if( face_texture != -1 )
                    vec_push_unique(task->queued_texture_ids, &face_texture);
            }
        }
        hmap_iter_free(iter);

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

        task->texture_definitions_hmap = configmap_new_from_packed(
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
            struct ConfigMapIter* iter = configmap_iter_new(task->texture_definitions_hmap);
            while( (texture_definition = (struct CacheTexture*)configmap_iter_next(iter)) )
            {
                for( int i = 0; i < texture_definition->sprite_ids_count; i++ )
                    vec_push_unique(sprite_pack_ids, &texture_definition->sprite_ids[i]);
            }
            configmap_iter_free(iter);

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
            spritepack_entry = hmap_search(task->spritepacks_hmap, &message.param_b, HMAP_INSERT);
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
        struct ConfigMapIter* iter = configmap_iter_new(task->texture_definitions_hmap);
        while( (texture_definition = (struct CacheTexture*)configmap_iter_next(iter)) )
        {
            texture = texture_new_from_definition(texture_definition, task->spritepacks_hmap);
            texture_entry = (struct TextureEntry*)hmap_search(
                task->textures_hmap, &texture_definition->_id, HMAP_INSERT);
            assert(texture_entry && "Texture must be inserted into hmap");
            texture_entry->id = texture_definition->_id;
            texture_entry->texture = texture;
        }
        configmap_iter_free(iter);
        task->step = STEP_INIT_SCENE_10_BUILD_WORLD3D;
    }
    case STEP_INIT_SCENE_10_BUILD_WORLD3D:
    {
        task->terrain = terrain_new_from_map_terrain(
            task->terrain_iter, NULL, task->underlay_configmap, task->overlay_configmap);
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