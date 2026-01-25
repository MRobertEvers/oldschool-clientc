
#include "task_init_scene_dat.h"

#include "bmp.h"
#include "datastruct/list.h"
#include "datastruct/vec.h"
#include "game.h"
#include "gametask.h"
#include "graphics/dash.h"
#include "graphics/lighting.h"
#include "osrs/buildcachedat.h"
#include "osrs/cache_utils.h"
#include "osrs/configmap.h"
#include "osrs/dash_utils.h"
#include "osrs/dashlib.h"
#include "osrs/gio_assets.h"
#include "osrs/minimap.h"
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
#include "osrs/rscache/tables_dat/config_idk.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "osrs/rscache/tables_dat/config_textures.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"
#include "osrs/rscache/tables_dat/pixfont.h"
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

    struct BuildCacheDat* buildcachedat;

    struct DashPix8* pix8;
    struct DashPixPalette* palette;

    struct DashSprite* sprite;

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
    game->sys_minimap = minimap_new(map_sw_x, map_sw_z, map_ne_x, map_ne_z, MAP_TERRAIN_LEVELS);

    task->scene_builder = scenebuilder_new_painter(
        task->painter, game->sys_minimap, map_sw_x, map_sw_z, map_ne_x, map_ne_z);

    game->scenebuilder = task->scene_builder;

    task->reqid_queue_vec = vec_new(sizeof(int), 64);
    task->reqid_queue_inflight_count = 0;

    task->queued_texture_ids_vec = vec_new(sizeof(int), 512);
    task->queued_scenery_models_vec = vec_new(sizeof(int), 512);

    game->buildcachedat = buildcachedat_new();
    task->buildcachedat = game->buildcachedat;

    return task;
}

static enum GameTaskStatus
step_configs_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_0_LOAD_CONFIGS];

    static int versionlist_reqid = 0;
    static int configs_reqid = 0;
    struct GIOMessage message = { 0 };

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);
        configs_reqid = gio_assets_dat_config_configs_load(task->io);
        vec_push(task->reqid_queue_vec, &configs_reqid);

        versionlist_reqid = gio_assets_dat_config_version_list_load(task->io);
        vec_push(task->reqid_queue_vec, &versionlist_reqid);

        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            if( message.message_id == configs_reqid )
            {
                buildcachedat_set_config_jagfile(
                    task->buildcachedat,
                    filelist_dat_new_from_decode(message.data, message.data_size));
            }
            else if( message.message_id == versionlist_reqid )
            {
                buildcachedat_set_versionlist_jagfile(
                    task->buildcachedat,
                    filelist_dat_new_from_decode(message.data, message.data_size));
            }

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_DONE;
    }

    return GAMETASK_STATUS_COMPLETED;
}

static void
step_terrain_load_gather(struct TaskInitSceneDat* task)
{
    int reqid = 0;
    vec_clear(task->reqid_queue_vec);
    for( int i = 0; i < task->chunks_count; i++ )
    {
        reqid = gio_assets_dat_map_terrain_load(task->io, task->chunks[i].x, task->chunks[i].z);

        vec_push(task->reqid_queue_vec, &reqid);
    }
}

static void
step_terrain_load_poll(
    struct TaskInitSceneDat* task,
    struct GIOMessage* message)
{
    struct CacheMapTerrain* terrain = NULL;

    int map_x = (message->param_b >> 16) & 0xFFFF;
    int map_z = message->param_b & 0xFFFF;

    terrain = map_terrain_new_from_decode_flags(
        message->data, message->data_size, map_x, map_z, MAP_TERRAIN_DECODE_U8);

    scenebuilder_cache_map_terrain(task->scene_builder, map_x, map_z, terrain);
}

static enum GameTaskStatus
step_terrain_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_1_LOAD_TERRAIN];

    struct GIOMessage message = { 0 };

    switch( step_stage->step )
    {
    case TS_GATHER:
        step_terrain_load_gather(task);
        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            step_terrain_load_poll(task, &message);

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
step_flotype_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_2_LOAD_FLOORTYPE];

    int reqid = 0;
    struct ConfigMap* configmap = NULL;

    struct FileListDat* config_jagfile = buildcachedat_config_jagfile(task->buildcachedat);
    assert(config_jagfile != NULL && "Config jagfile must be loaded");
    int data_file_idx = filelist_dat_find_file_by_name(config_jagfile, "flo.dat");

    assert(data_file_idx != -1 && "Data file must be found");

    struct RSBuffer buffer = {
        .data = config_jagfile->files[data_file_idx],
        .size = config_jagfile->file_sizes[data_file_idx],
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

        buildcachedat_add_flotype(task->buildcachedat, i, flotype);

        scenebuilder_cache_flotype(task->scene_builder, i, flotype);
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

            buildcachedat_add_scenery(
                task->buildcachedat, locs->_chunk_mapx, locs->_chunk_mapz, locs);

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
    struct FileListDatIndexed* filelist_indexed = NULL;
    int mapxz = 0;

    struct FileListDat* config_jagfile = buildcachedat_config_jagfile(task->buildcachedat);
    assert(config_jagfile != NULL && "Config jagfile must be loaded");

    int data_file_idx = filelist_dat_find_file_by_name(config_jagfile, "loc.dat");
    int index_file_idx = filelist_dat_find_file_by_name(config_jagfile, "loc.idx");

    assert(data_file_idx != -1 && "Data file must be found");
    assert(index_file_idx != -1 && "Index file must be found");

    filelist_indexed = filelist_dat_indexed_new_from_decode(
        config_jagfile->files[index_file_idx],
        config_jagfile->file_sizes[index_file_idx],
        config_jagfile->files[data_file_idx],
        config_jagfile->file_sizes[data_file_idx]);

    iter = buildcachedat_iter_new_scenery(task->buildcachedat);
    while( (locs = buildcachedat_iter_next_scenery(iter)) )
    {
        for( int i = 0; i < locs->locs_count; i++ )
        {
            struct CacheConfigLocation* config_loc = malloc(sizeof(struct CacheConfigLocation));
            memset(config_loc, 0, sizeof(struct CacheConfigLocation));

            loc = &locs->locs[i];
            int offset = filelist_indexed->offsets[loc->loc_id];

            config_locs_decode_inplace(
                config_loc,
                filelist_indexed->data + offset,
                filelist_indexed->data_size - offset,
                CONFIG_LOC_DECODE_DAT);

            config_loc->_id = loc->loc_id;

            buildcachedat_add_config_loc(
                task->buildcachedat, loc->loc_id, loc->shape_select, config_loc);

            scenebuilder_cache_config_loc(task->scene_builder, loc->loc_id, config_loc);
        }
    }
    dashmap_iter_free(iter);

    return GAMETASK_STATUS_COMPLETED;
}

static const int g_guard_models[] = { 235, 246, 301, 151, 176, 254, 185, 519, 541 };

#include <math.h>

static void
light_model_default(
    struct DashModel* dash_model,
    int model_contrast,
    int model_ambient)
{
    int light_ambient = 64;
    int light_attenuation = 768;
    int lightsrc_x = -50;
    int lightsrc_y = -10;
    int lightsrc_z = -50;

    light_ambient += model_ambient;
    // This is what 2004Scape does. Later revs do not.
    light_attenuation += (model_contrast & 0xff) * 5;

    int light_magnitude =
        (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
    int attenuation = (light_attenuation * light_magnitude) >> 8;

    calculate_vertex_normals(
        dash_model->normals->lighting_vertex_normals,
        dash_model->normals->lighting_face_normals,
        dash_model->vertex_count,
        dash_model->face_indices_a,
        dash_model->face_indices_b,
        dash_model->face_indices_c,
        dash_model->vertices_x,
        dash_model->vertices_y,
        dash_model->vertices_z,
        dash_model->face_count);

    apply_lighting(
        dash_model->lighting->face_colors_hsl_a,
        dash_model->lighting->face_colors_hsl_b,
        dash_model->lighting->face_colors_hsl_c,
        dash_model->normals->lighting_vertex_normals,
        dash_model->normals->lighting_face_normals,
        dash_model->face_indices_a,
        dash_model->face_indices_b,
        dash_model->face_indices_c,
        dash_model->face_count,
        dash_model->face_colors,
        dash_model->face_alphas,
        dash_model->face_textures,
        dash_model->face_infos,
        light_ambient,
        attenuation,
        lightsrc_x,
        lightsrc_y,
        lightsrc_z);
}

static enum GameTaskStatus
step_models_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_5_LOAD_MODELS];

    int reqid = 0;
    struct GIOMessage message = { 0 };
    struct DashMapIter* iter = NULL;
    struct CacheConfigLocation* config_loc = NULL;
    int shape_select = 0;

    switch( step_stage->step )
    {
    case TS_GATHER:
        vec_clear(task->reqid_queue_vec);

        iter = buildcachedat_iter_new_config_locs(task->buildcachedat);
        while( (config_loc = buildcachedat_iter_next_config_loc(iter, &shape_select)) )
        {
            queue_scenery_models(task, config_loc, shape_select);
        }
        dashmap_iter_free(iter);

        for( int i = 0; i < sizeof(g_guard_models) / sizeof(g_guard_models[0]); i++ )
        {
            vec_push_unique(task->queued_scenery_models_vec, &g_guard_models[i]);
        }

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

            struct CacheModel* model = model_new_decode(message.data, message.data_size);

            buildcachedat_add_model(task->buildcachedat, message.param_b, model);

            scenebuilder_cache_model(task->scene_builder, message.param_b, model);

            gioq_release(task->io, &message);
        }

        if( task->reqid_queue_inflight_count != 0 )
            return GAMETASK_STATUS_PENDING;

        step_stage->step = TS_DONE;
    case TS_DONE:
    {
        struct CacheModel* guard_models[sizeof(g_guard_models) / sizeof(g_guard_models[0])] = {};
        for( int i = 0; i < sizeof(g_guard_models) / sizeof(g_guard_models[0]); i++ )
        {
            guard_models[i] = buildcachedat_get_model(task->buildcachedat, g_guard_models[i]);
        }

        struct CacheModel* merged_model =
            model_new_merge(guard_models, sizeof(g_guard_models) / sizeof(g_guard_models[0]));

        task->game->model = dashmodel_new_from_cache_model(merged_model);
        model_free(merged_model);
        light_model_default(task->game->model, 0, 0);
        break;
    }
    }

    return GAMETASK_STATUS_COMPLETED;
}

static void
step_textures_load_gather(struct TaskInitSceneDat* task)
{
    vec_clear(task->reqid_queue_vec);

    int reqid = gio_assets_dat_config_texture_sprites_load(task->io);
    vec_push(task->reqid_queue_vec, &reqid);
}

static void
step_textures_load_poll(
    struct TaskInitSceneDat* task,
    struct GIOMessage* message)
{
    struct FileListDat* filelist = filelist_dat_new_from_decode(message->data, message->data_size);
    struct CacheDatTexture* texture = cache_dat_texture_new_from_filelist_dat(filelist, 0, 0);

    // Hardcoded to 50 in the deob. Not sure why.
    for( int i = 0; i < 50; i++ )
    {
        struct CacheDatTexture* texture = cache_dat_texture_new_from_filelist_dat(filelist, i, 0);

        int animation_direction = TEXANIM_DIRECTION_NONE;
        int animation_speed = 0;

        /**
         * In old revisions (e.g. 245.2) the animated textures were hardcoded.
         */
        if( i == 17 )
        {
            animation_direction = TEXANIM_DIRECTION_V_DOWN;
            animation_speed = 2;
        }

        if( i == 24 )
        {
            animation_direction = TEXANIM_DIRECTION_V_DOWN;
            animation_speed = 2;
        }

        struct DashTexture* dash_texture =
            texture_new_from_texture_sprite(texture, animation_direction, animation_speed);
        assert(dash_texture != NULL);

        buildcachedat_add_texture(task->buildcachedat, i, dash_texture);

        scenebuilder_cache_texture(task->scene_builder, i, dash_texture);

        dash3d_add_texture(task->game->sys_dash, i, dash_texture);
    }
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
        step_textures_load_gather(task);
        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            step_textures_load_poll(task, &message);

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

    struct FileListDat* config_jagfile = buildcachedat_config_jagfile(task->buildcachedat);
    assert(config_jagfile != NULL && "Config jagfile must be loaded");

    int data_file_idx = filelist_dat_find_file_by_name(config_jagfile, "seq.dat");
    assert(data_file_idx != -1 && "Data file must be found");

    struct RSBuffer buffer = { .data = config_jagfile->files[data_file_idx],
                               .size = config_jagfile->file_sizes[data_file_idx],
                               .position = 0 };
    int count = g2(&buffer);
    for( int i = 0; i < count; i++ )
    {
        struct CacheDatSequence* sequence = malloc(sizeof(struct CacheDatSequence));
        memset(sequence, 0, sizeof(struct CacheDatSequence));

        buffer.position += config_dat_sequence_decode_inplace(
            sequence, buffer.data + buffer.position, buffer.size - buffer.position);

        buildcachedat_add_sequence(task->buildcachedat, i, sequence);

        scenebuilder_cache_dat_sequence(task->scene_builder, i, sequence);
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_animframes_index_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_8_LOAD_ANIMFRAMES_INDEX];

    struct FileListDat* versionlist_jagfile =
        buildcachedat_versionlist_jagfile(task->buildcachedat);
    assert(versionlist_jagfile != NULL && "Versionlist jagfile must be loaded");

    int index_file_idx = filelist_dat_find_file_by_name(versionlist_jagfile, "anim_index");
    assert(index_file_idx != -1 && "Index file must be found");

    task->animbaseframes_count = versionlist_jagfile->file_sizes[index_file_idx] / 2;

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_animbaseframes_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_9_LOAD_ANIMBASEFRAMES];

    struct GIOMessage message = { 0 };
    int reqid = 0;

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
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            if( message.data == NULL )
                goto release;

            animbaseframes = cache_dat_animbaseframes_new_decode(message.data, message.data_size);

            buildcachedat_add_animbaseframes(task->buildcachedat, message.param_b, animbaseframes);

            for( int i = 0; i < animbaseframes->frame_count; i++ )
            {
                struct CacheAnimframe* animframe = &animbaseframes->frames[i];

                buildcachedat_add_animframe(task->buildcachedat, animframe->id, animframe);

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

static void
step_media_load_gather(struct TaskInitSceneDat* task)
{
    int reqid = 0;

    vec_clear(task->reqid_queue_vec);

    reqid = gio_assets_dat_config_media_load(task->io);
    vec_push(task->reqid_queue_vec, &reqid);
}

static struct DashSprite*
step_media_load_sprite_pix8(
    struct FileListDat* filelist,
    const char* filename,
    int index_file_idx,
    int sprite_idx)
{
    struct CacheDatPix8Palette* pix8_palette = NULL;
    struct DashSprite* sprite = NULL;
    int data_file_idx = filelist_dat_find_file_by_name(filelist, filename);
    if( data_file_idx == -1 )
    {
        printf("Failed to find %s in filelist\n", filename);
        assert(false && "Failed to find %s in filelist");
        return NULL;
    }

    pix8_palette = cache_dat_pix8_palette_new(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);

    sprite = dashsprite_new_from_cache_pix8_palette(pix8_palette);
    cache_dat_pix8_palette_free(pix8_palette);

    return sprite;
}

static struct DashSprite*
step_media_load_sprite_pix32(
    struct FileListDat* filelist,
    const char* filename,
    int index_file_idx,
    int sprite_idx)
{
    struct CacheDatPix32* pix32 = NULL;
    struct DashSprite* sprite = NULL;
    int data_file_idx = filelist_dat_find_file_by_name(filelist, filename);
    if( data_file_idx == -1 )
    {
        printf("Failed to find %s in filelist\n", filename);
        assert(false && "Failed to find %s in filelist");
        return NULL;
    }

    pix32 = cache_dat_pix32_new(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);
    if( !pix32 )
    {
        return NULL;
    }
    sprite = dashsprite_new_from_cache_pix32(pix32);
    cache_dat_pix32_free(pix32);
    return sprite;
}

static void
step_media_load_poll(
    struct TaskInitSceneDat* task,
    struct GIOMessage* message)
{
    int reqid = 0;
    struct CacheDatPix8Palette* pix8 = NULL;
    struct CacheDatPix32* pix32 = NULL;
    struct FileListDat* filelist = filelist_dat_new_from_decode(message->data, message->data_size);

    // char name[16] = { 0 };
    // snprintf(name, sizeof(name), "%d.dat", texture_id);

    int index_file_idx = filelist_dat_find_file_by_name(filelist, "index.dat");
    assert(index_file_idx != -1 && "Failed to find invback.dat or index.dat in filelist");

    task->game->sprite_invback =
        step_media_load_sprite_pix8(filelist, "invback.dat", index_file_idx, 0);

    task->game->sprite_chatback =
        step_media_load_sprite_pix8(filelist, "chatback.dat", index_file_idx, 0);

    task->game->sprite_mapback =
        step_media_load_sprite_pix8(filelist, "mapback.dat", index_file_idx, 0);

    task->game->sprite_backbase1 =
        step_media_load_sprite_pix8(filelist, "backbase1.dat", index_file_idx, 0);

    task->game->sprite_backbase2 =
        step_media_load_sprite_pix8(filelist, "backbase2.dat", index_file_idx, 0);

    task->game->sprite_backhmid1 =
        step_media_load_sprite_pix8(filelist, "backhmid1.dat", index_file_idx, 0);

    for( int i = 0; i < 13; i++ )
    {
        task->game->sprite_sideicons[i] =
            step_media_load_sprite_pix8(filelist, "sideicons.dat", index_file_idx, i);
    }

    task->game->sprite_compass =
        step_media_load_sprite_pix32(filelist, "compass.dat", index_file_idx, 0);

    task->game->sprite_mapedge =
        step_media_load_sprite_pix32(filelist, "mapedge.dat", index_file_idx, 0);

    for( int i = 0; i < 50; i++ )
    {
        task->game->sprite_mapscene[i] =
            step_media_load_sprite_pix8(filelist, "mapscene.dat", index_file_idx, i);
    }

    for( int i = 0; i < 50; i++ )
    {
        task->game->sprite_mapfunction[i] =
            step_media_load_sprite_pix32(filelist, "mapfunction.dat", index_file_idx, i);
    }

    for( int i = 0; i < 20; i++ )
    {
        task->game->sprite_hitmarks[i] =
            step_media_load_sprite_pix32(filelist, "hitmarks.dat", index_file_idx, i);
    }

    for( int i = 0; i < 20; i++ )
    {
        task->game->sprite_headicons[i] =
            step_media_load_sprite_pix32(filelist, "headicons.dat", index_file_idx, i);
    }

    task->game->sprite_mapmarker0 =
        step_media_load_sprite_pix32(filelist, "mapmarker.dat", index_file_idx, 0);

    task->game->sprite_mapmarker1 =
        step_media_load_sprite_pix32(filelist, "mapmarker.dat", index_file_idx, 1);

    for( int i = 0; i < 8; i++ )
    {
        task->game->sprite_cross[i] =
            step_media_load_sprite_pix32(filelist, "cross.dat", index_file_idx, i);
    }

    task->game->sprite_mapdot0 =
        step_media_load_sprite_pix32(filelist, "mapdots.dat", index_file_idx, 0);

    task->game->sprite_mapdot1 =
        step_media_load_sprite_pix32(filelist, "mapdots.dat", index_file_idx, 1);

    task->game->sprite_mapdot2 =
        step_media_load_sprite_pix32(filelist, "mapdots.dat", index_file_idx, 2);

    task->game->sprite_mapdot3 =
        step_media_load_sprite_pix32(filelist, "mapdots.dat", index_file_idx, 3);

    task->game->sprite_scrollbar0 =
        step_media_load_sprite_pix8(filelist, "scrollbar.dat", index_file_idx, 0);

    task->game->sprite_scrollbar1 =
        step_media_load_sprite_pix8(filelist, "scrollbar.dat", index_file_idx, 1);

    task->game->sprite_redstone1 =
        step_media_load_sprite_pix8(filelist, "redstone1.dat", index_file_idx, 0);

    task->game->sprite_redstone2 =
        step_media_load_sprite_pix8(filelist, "redstone2.dat", index_file_idx, 0);

    task->game->sprite_redstone3 =
        step_media_load_sprite_pix8(filelist, "redstone3.dat", index_file_idx, 0);

    task->game->sprite_redstone1h =
        step_media_load_sprite_pix8(filelist, "redstone1.dat", index_file_idx, 0);

    task->game->sprite_redstone2h =
        step_media_load_sprite_pix8(filelist, "redstone2.dat", index_file_idx, 0);

    task->game->sprite_redstone1v =
        step_media_load_sprite_pix8(filelist, "redstone1.dat", index_file_idx, 0);

    task->game->sprite_redstone2v =
        step_media_load_sprite_pix8(filelist, "redstone2.dat", index_file_idx, 0);

    task->game->sprite_redstone3v =
        step_media_load_sprite_pix8(filelist, "redstone3.dat", index_file_idx, 0);

    task->game->sprite_redstone1hv =
        step_media_load_sprite_pix8(filelist, "redstone1.dat", index_file_idx, 0);

    task->game->sprite_redstone2hv =
        step_media_load_sprite_pix8(filelist, "redstone2.dat", index_file_idx, 0);

    for( int i = 0; i < 2; i++ )
    {
        task->game->sprite_modicons[i] =
            step_media_load_sprite_pix8(filelist, "mod_icons.dat", index_file_idx, i);
    }

    task->game->sprite_backleft1 =
        step_media_load_sprite_pix32(filelist, "backleft1.dat", index_file_idx, 0);

    task->game->sprite_backleft2 =
        step_media_load_sprite_pix8(filelist, "backleft2.dat", index_file_idx, 0);

    task->game->sprite_backright1 =
        step_media_load_sprite_pix8(filelist, "backright1.dat", index_file_idx, 0);

    task->game->sprite_backright2 =
        step_media_load_sprite_pix8(filelist, "backright2.dat", index_file_idx, 0);

    task->game->sprite_backtop1 =
        step_media_load_sprite_pix8(filelist, "backtop1.dat", index_file_idx, 0);

    task->game->sprite_backvmid1 =
        step_media_load_sprite_pix8(filelist, "backvmid1.dat", index_file_idx, 0);

    task->game->sprite_backvmid2 =
        step_media_load_sprite_pix8(filelist, "backvmid2.dat", index_file_idx, 0);

    task->game->sprite_backvmid3 =
        step_media_load_sprite_pix8(filelist, "backvmid3.dat", index_file_idx, 0);

    task->game->sprite_backhmid2 =
        step_media_load_sprite_pix8(filelist, "backhmid2.dat", index_file_idx, 0);

    filelist_dat_free(filelist);
}

static enum GameTaskStatus
step_media_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_11_LOAD_MEDIA];

    struct GIOMessage message = { 0 };

    switch( step_stage->step )
    {
    case TS_GATHER:
        step_media_load_gather(task);
        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;

            step_media_load_poll(task, &message);

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

static void
step_title_load_gather(struct TaskInitSceneDat* task)
{
    int reqid = 0;
    vec_clear(task->reqid_queue_vec);
    reqid = gio_assets_dat_config_title_load(task->io);
    vec_push(task->reqid_queue_vec, &reqid);
}

static void
step_title_load_poll(
    struct TaskInitSceneDat* task,
    struct GIOMessage* message)
{
    struct FileListDat* filelist = filelist_dat_new_from_decode(message->data, message->data_size);

    int data_file_idx = filelist_dat_find_file_by_name(filelist, "b12.dat");
    int index_file_idx = filelist_dat_find_file_by_name(filelist, "index.dat");
    assert(
        data_file_idx != -1 && index_file_idx != -1 &&
        "Failed to find b12.dat or index.dat in filelist");
    struct CacheDatPixfont* pixfont = cache_dat_pixfont_new_decode(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);
    task->game->pixfont_b12 = dashpixfont_new_from_cache_dat_pixfont_move(pixfont);
    cache_dat_pixfont_free(pixfont);

    data_file_idx = filelist_dat_find_file_by_name(filelist, "p12.dat");
    assert(
        data_file_idx != -1 && index_file_idx != -1 &&
        "Failed to find p12.dat or index.dat in filelist");
    pixfont = cache_dat_pixfont_new_decode(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);
    task->game->pixfont_p12 = dashpixfont_new_from_cache_dat_pixfont_move(pixfont);
    cache_dat_pixfont_free(pixfont);

    data_file_idx = filelist_dat_find_file_by_name(filelist, "p11.dat");
    assert(
        data_file_idx != -1 && index_file_idx != -1 &&
        "Failed to find p11.dat or index.dat in filelist");
    pixfont = cache_dat_pixfont_new_decode(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);
    task->game->pixfont_p11 = dashpixfont_new_from_cache_dat_pixfont_move(pixfont);
    cache_dat_pixfont_free(pixfont);

    data_file_idx = filelist_dat_find_file_by_name(filelist, "q8.dat");
    assert(
        data_file_idx != -1 && index_file_idx != -1 &&
        "Failed to find q8.dat or index.dat in filelist");
    pixfont = cache_dat_pixfont_new_decode(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);
    task->game->pixfont_q8 = dashpixfont_new_from_cache_dat_pixfont_move(pixfont);
    cache_dat_pixfont_free(pixfont);

    filelist_dat_free(filelist);
}

static enum GameTaskStatus
step_title_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_12_LOAD_TITLE];

    struct GIOMessage message = { 0 };

    switch( step_stage->step )
    {
    case TS_GATHER:
        step_title_load_gather(task);
        task->reqid_queue_inflight_count = vec_size(task->reqid_queue_vec);

        step_stage->step = TS_POLL;
    case TS_POLL:
        while( gioq_poll(task->io, &message) )
        {
            task->reqid_queue_inflight_count--;
            step_title_load_poll(task, &message);

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
step_idkits_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_13_LOAD_IDKITS];

    struct FileListDat* filelist = buildcachedat_config_jagfile(task->buildcachedat);

    int data_file_idx = filelist_dat_find_file_by_name(filelist, "idk.dat");
    assert(data_file_idx != -1 && "Failed to find idk.dat in filelist");

    struct CacheDatConfigIdkList* idk_list = cache_dat_config_idk_list_new_decode(
        filelist->files[data_file_idx], filelist->file_sizes[data_file_idx]);

    for( int i = 0; i < idk_list->idks_count; i++ )
    {
        buildcachedat_add_idk(task->buildcachedat, i, idk_list->idks[i]);
    }

    return GAMETASK_STATUS_COMPLETED;
}

static enum GameTaskStatus
step_objects_load(struct TaskInitSceneDat* task)
{
    struct TaskStep* step_stage = &task->task_steps[STEP_INIT_SCENE_DAT_14_LOAD_OBJECTS];

    struct FileListDat* filelist = buildcachedat_config_jagfile(task->buildcachedat);

    int data_file_idx = filelist_dat_find_file_by_name(filelist, "obj.dat");
    assert(data_file_idx != -1 && "Failed to find obj.dat in filelist");
    int index_file_idx = filelist_dat_find_file_by_name(filelist, "obj.idx");
    assert(index_file_idx != -1 && "Failed to find obj.idx in filelist");

    struct CacheDatConfigObjList* obj_list = cache_dat_config_obj_list_new_decode(
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx]);

    for( int i = 0; i < obj_list->objs_count; i++ )
    {
        buildcachedat_add_obj(task->buildcachedat, i, obj_list->objs[i]);
    }

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
        task->step = STEP_INIT_SCENE_DAT_0_LOAD_CONFIGS;
    }
    case STEP_INIT_SCENE_DAT_0_LOAD_CONFIGS:
    {
        if( step_configs_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

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

        task->step = STEP_INIT_SCENE_DAT_11_LOAD_MEDIA;
    }
    case STEP_INIT_SCENE_DAT_11_LOAD_MEDIA:
    {
        if( step_media_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_DAT_12_LOAD_TITLE;
    }
    case STEP_INIT_SCENE_DAT_12_LOAD_TITLE:
    {
        if( step_title_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_DAT_13_LOAD_IDKITS;
    }
    case STEP_INIT_SCENE_DAT_13_LOAD_IDKITS:
    {
        if( step_idkits_load(task) != GAMETASK_STATUS_COMPLETED )
            return GAMETASK_STATUS_PENDING;

        task->step = STEP_INIT_SCENE_DAT_14_LOAD_OBJECTS;
    }
    case STEP_INIT_SCENE_DAT_14_LOAD_OBJECTS:
    {
        if( step_objects_load(task) != GAMETASK_STATUS_COMPLETED )
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
