#include "gametask_scene_load.h"

#include "datastruct/ht.h"
#include "graphics/lighting.h"
#include "osrs/cache.h"
#include "osrs/filelist.h"
#include "osrs/model_transforms.h"
#include "osrs/scene.h"
#include "osrs/tables/config_idk.h"
#include "osrs/tables/config_locs.h"
#include "osrs/tables/config_object.h"
#include "osrs/tables/configs.h"
#include "osrs/tables/maps.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TILE_SIZE 128

static const int ROTATION_WALL_TYPE[] = {
    1, 2, 4, 8 // WALL_SIDE_WEST, WALL_SIDE_NORTH, WALL_SIDE_EAST, WALL_SIDE_SOUTH
};
static const int ROTATION_WALL_CORNER_TYPE[] = {
    1,
    2,
    4,
    8 // WALL_CORNER_NORTHWEST, WALL_CORNER_NORTHEAST, WALL_CORNER_SOUTHEAST, WALL_CORNER_SOUTHWEST
};
static const int WALL_DECOR_ROTATION_OFFSET_X[] = { 1, 0, -1, 0 };
static const int WALL_DECOR_ROTATION_OFFSET_Z[] = { 0, -1, 0, 1 };
static const int WALL_DECOR_ROTATION_DIAGONAL_OFFSET_X[] = { 1, -1, -1, 1 };
static const int WALL_DECOR_ROTATION_DIAGONAL_OFFSET_Z[] = { -1, -1, 1, 1 };

enum SceneLoadStep
{
    E_SCENE_LOAD_STEP_INITIAL = 0,
    E_SCENE_LOAD_STEP_MAP_TERRAIN,
    E_SCENE_LOAD_STEP_MAP_LOCS,
    E_SCENE_LOAD_STEP_LOAD_CONFIG_SEQUENCE,
    E_SCENE_LOAD_STEP_LOAD_CONFIG_OBJECT,
    E_SCENE_LOAD_STEP_LOAD_CONFIG_IDK,
    E_SCENE_LOAD_STEP_LOAD_CONFIG_LOCS,
    E_SCENE_LOAD_STEP_PROCESS_LOCS,
    E_SCENE_LOAD_STEP_LOAD_MODELS,
    E_SCENE_LOAD_STEP_COMPLETE_MODELS,
    E_SCENE_LOAD_STEP_BUILD_LIGHTING,
    E_SCENE_LOAD_STEP_DONE,
};

struct GameTaskSceneLoad
{
    enum SceneLoadStep step;
    struct GameIORequest* request;
    struct GameIO* io;
    struct Cache* cache;
    struct Scene* scene;
    int chunk_x;
    int chunk_y;

    // Loading state
    struct CacheMapTerrain* map_terrain;
    struct CacheMapLocsIter* map_locs_iter;
    struct CacheMapLoc* current_map_loc;
    struct ModelCache* model_cache;
    struct CacheConfigLocationTable* config_locs_table;
    struct CacheConfigObjectTable* config_object_table;
    struct CacheConfigSequenceTable* config_sequence_table;
    struct CacheConfigIdkTable* config_idk_table;
    int* shade_map;
    struct CacheArchiveTuple locs_archive_tuple;
    struct CacheArchive* locs_archive;

    // Async model loading
    struct HashTable loaded_models; // Maps model_id -> CacheModel*
    int* queued_model_ids;          // Array of model IDs to load
    int queued_model_count;
    int queued_model_capacity;

    int queue_index;
    bool models_requested; // Whether queued models have been requested
};

// Forward declarations for async model functions
static void io_queue_model(struct GameTaskSceneLoad* task, int model_id);
static struct CacheModel* get_model_async(struct HashTable* loaded_models, int model_id);

static struct CacheModel*
get_model_async(struct HashTable* loaded_models, int model_id)
{
    struct CacheModel** model = ht_lookup(loaded_models, (char const*)&model_id, sizeof(int));
    if( model )
        return *model;

    assert(false);
    return NULL;
}

static void
init_wall_default(struct Wall* wall)
{
    memset(wall, 0, sizeof(struct Wall));
    wall->model_a = -1;
    wall->model_b = -1;
}

static int
vec_loc_push(struct Scene* scene)
{
    if( scene->locs_length >= scene->locs_capacity )
    {
        scene->locs_capacity *= 2;
        scene->locs = realloc(scene->locs, sizeof(struct Loc) * scene->locs_capacity);
    }

    memset(scene->locs + scene->locs_length, 0, sizeof(struct Loc));

    scene->locs[scene->locs_length].entity = -1;

    return scene->locs_length++;
}

static struct Loc*
vec_loc_back(struct Scene* scene)
{
    return &scene->locs[scene->locs_length - 1];
}

static int
vec_model_push(struct Scene* scene)
{
    if( scene->models_length >= scene->models_capacity )
    {
        scene->models_capacity *= 2;
        scene->models = realloc(scene->models, sizeof(struct SceneModel) * scene->models_capacity);
    }

    memset(scene->models + scene->models_length, 0, sizeof(struct SceneModel));

    scene->models[scene->models_length].light_ambient = -1;
    scene->models[scene->models_length].light_contrast = -1;

    return scene->models_length++;
}

static struct SceneModel*
vec_model_back(struct Scene* scene)
{
    struct SceneModel* model = &scene->models[scene->models_length - 1];
    model->scene_model_idx = scene->models_length - 1;
    return model;
}

static void
init_scene_model_wxh(
    struct SceneModel* model, int tile_x, int tile_y, int height_center, int size_x, int size_y)
{
    model->region_x = tile_x * TILE_SIZE + size_x * 64;
    model->region_z = tile_y * TILE_SIZE + size_y * 64;
    model->region_height = height_center;

    model->_size_x = size_x;
    model->_size_y = size_y;

    model->_chunk_pos_x = tile_x;
    model->_chunk_pos_y = tile_y;
}

static void
init_scene_model_1x1(struct SceneModel* model, int tile_x, int tile_y, int height_center)
{
    init_scene_model_wxh(model, tile_x, tile_y, height_center, 1, 1);
}

static void
init_loc_1x1(struct Loc* loc, int tile_x, int tile_y, int tile_z)
{
    loc->size_x = 1;
    loc->size_y = 1;
    loc->chunk_pos_x = tile_x;
    loc->chunk_pos_y = tile_y;
    loc->chunk_pos_level = tile_z;
}

static void
compute_normal_scenery_spans(
    struct GridTile* grid, int loc_x, int loc_y, int loc_z, int size_x, int size_y, int loc_index)
{
    struct GridTile* grid_tile = NULL;

    int min_tile_x = loc_x;
    int min_tile_y = loc_y;

    // The max tile is not actually included in the span
    int max_tile_exclusive_x = loc_x + size_x - 1;
    int max_tile_exclusive_y = loc_y + size_y - 1;
    if( max_tile_exclusive_x > MAP_TERRAIN_X - 1 )
        max_tile_exclusive_x = MAP_TERRAIN_X - 1;
    if( max_tile_exclusive_y > MAP_TERRAIN_Y - 1 )
        max_tile_exclusive_y = MAP_TERRAIN_Y - 1;

    for( int x = min_tile_x; x <= max_tile_exclusive_x; x++ )
    {
        for( int y = min_tile_y; y <= max_tile_exclusive_y; y++ )
        {
            int span_flags = 0;
            if( x > min_tile_x )
            {
                // Block until the west tile underlay is drawn.
                span_flags |= SPAN_FLAG_WEST;
            }

            if( x < max_tile_exclusive_x )
            {
                // Block until the east tile underlay is drawn.
                span_flags |= SPAN_FLAG_EAST;
            }

            if( y > min_tile_y )
            {
                // Block until the south tile underlay is drawn.
                span_flags |= SPAN_FLAG_SOUTH;
            }

            if( y < max_tile_exclusive_y )
            {
                // Block until the north tile underlay is drawn.
                span_flags |= SPAN_FLAG_NORTH;
            }

            grid_tile = &grid[MAP_TILE_COORD(x, y, loc_z)];
            grid_tile->spans |= span_flags;
            grid_tile->locs[grid_tile->locs_length++] = loc_index;
        }
    }
}

static void
loc_apply_transforms(
    struct CacheConfigLocation* loc,
    struct CacheModel* model,
    int orientation,
    int sw_height,
    int se_height,
    int ne_height,
    int nw_height)
{
    // This should never be called on a shared model.
    assert((model->_flags & CMODEL_FLAG_SHARED) == 0);

    for( int i = 0; i < loc->recolor_count; i++ )
    {
        model_transform_recolor(model, loc->recolors_from[i], loc->recolors_to[i]);
    }

    for( int i = 0; i < loc->retexture_count; i++ )
    {
        model_transform_retexture(model, loc->retextures_from[i], loc->retextures_to[i]);
    }

    bool mirrored = (loc->mirrored != (orientation > 3));
    bool oriented = orientation != 0;
    bool scaled = loc->resize_x != 128 || loc->resize_y != 128 || loc->resize_z != 128;
    bool translated = loc->offset_x != 0 || loc->offset_y != 0 || loc->offset_z != 0;
    // TODO: handle the other contoured ground types.
    bool hillskewed = loc->contour_ground_type == 1;

    if( mirrored )
        model_transform_mirror(model);

    if( oriented )
        model_transform_orient(model, orientation);

    if( scaled )
        model_transform_scale(model, loc->resize_x, loc->resize_y, loc->resize_z);

    if( translated )
        model_transform_translate(model, loc->offset_x, loc->offset_y, loc->offset_z);

    if( hillskewed )
        model_transform_hillskew(model, sw_height, se_height, ne_height, nw_height);
}

// Async loc_load_model - requests models asynchronously instead of loading synchronously
static void
loc_load_model_async(
    struct GameTaskSceneLoad* task,
    struct SceneModel* scene_model,
    struct CacheConfigLocation* loc_config,
    int shape_select,
    int orientation)
{
    int* shapes = loc_config->shapes;
    int** model_id_sets = loc_config->models;
    int* lengths = loc_config->lengths;
    int shapes_and_model_count = loc_config->shapes_and_model_count;

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
                io_queue_model(task, model_id);
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
                        io_queue_model(task, model_id);
                    }
                }
            }
        }
    }

    // Store metadata for later when models are actually loaded
    scene_model->model = NULL;  // Will be set when models are loaded
    scene_model->model_id = -1; // Will be set later
    scene_model->light_ambient = loc_config->ambient;
    scene_model->light_contrast = loc_config->contrast;
    scene_model->sharelight = loc_config->sharelight;
    scene_model->__loc_id = loc_config->_id;
    // Store transform parameters for later
    scene_model->_pending_transform_orientation = orientation;
    scene_model->_pending_loc_config = loc_config;
    scene_model->_pending_shape_select = shape_select;
}

// Called when all models are loaded to complete the scene model setup
static void
complete_scene_model_loading(
    struct GameTaskSceneLoad* task,
    struct SceneModel* scene_model,
    int sw_height,
    int se_height,
    int ne_height,
    int nw_height)
{
    // Load the actual models based on the loc config
    struct CacheConfigLocation* loc_config = scene_model->_pending_loc_config;
    int shape_select = scene_model->_pending_shape_select;

    if( !loc_config )
        return;

    int* shapes = loc_config->shapes;
    int** model_id_sets = loc_config->models;
    int* lengths = loc_config->lengths;
    int shapes_and_model_count = loc_config->shapes_and_model_count;

    struct CacheModel** models = NULL;
    int model_count = 0;
    int* model_ids = NULL;

    if( !model_id_sets )
        return;

    if( !shapes )
    {
        int count = lengths[0];
        models = malloc(sizeof(struct CacheModel*) * count);
        memset(models, 0, sizeof(struct CacheModel*) * count);
        model_ids = malloc(sizeof(int) * count);
        memset(model_ids, 0, sizeof(int) * count);

        for( int i = 0; i < count; i++ )
        {
            int model_id = model_id_sets[0][i];
            if( model_id )
            {
                struct CacheModel* model = get_model_async(&task->loaded_models, model_id);
                if( model )
                {
                    models[model_count] = model;
                    model_ids[model_count] = model_id;
                    model_count++;
                }
            }
        }
    }
    else
    {
        int count = shapes_and_model_count;
        models = malloc(sizeof(struct CacheModel*) * count);
        memset(models, 0, sizeof(struct CacheModel*) * count);
        model_ids = malloc(sizeof(int) * count);
        memset(model_ids, 0, sizeof(int) * count);

        for( int i = 0; i < count; i++ )
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
                        struct CacheModel* model = get_model_async(&task->loaded_models, model_id);
                        if( model )
                        {
                            models[model_count] = model;
                            model_ids[model_count] = model_id;
                            model_count++;
                        }
                    }
                }
            }
        }
    }

    if( model_count == 0 )
    {
        free(models);
        free(model_ids);
        return;
    }

    struct CacheModel* final_model;
    if( model_count > 1 )
    {
        final_model = model_new_merge(models, model_count);
    }
    else
    {
        final_model = model_new_copy(models[0]);
    }

    scene_model->model = final_model;
    scene_model->model_id = model_ids[0];

    // Apply transforms
    loc_apply_transforms(
        loc_config,
        final_model,
        scene_model->_pending_transform_orientation,
        sw_height,
        se_height,
        ne_height,
        nw_height);

    if( final_model->vertex_bone_map )
        scene_model->vertex_bones =
            modelbones_new_decode(final_model->vertex_bone_map, final_model->vertex_count);
    if( final_model->face_bone_map )
        scene_model->face_bones =
            modelbones_new_decode(final_model->face_bone_map, final_model->face_count);

    scene_model->sequence = NULL;

    free(models);
    free(model_ids);
}

struct GameTaskSceneLoad*
gametask_scene_load_new(struct GameIO* io, struct Cache* cache, int chunk_x, int chunk_y)
{
    struct GameTaskSceneLoad* task = malloc(sizeof(struct GameTaskSceneLoad));
    memset(task, 0, sizeof(struct GameTaskSceneLoad));
    task->step = E_SCENE_LOAD_STEP_INITIAL;
    task->cache = cache;
    task->scene = NULL;
    task->io = io;
    task->chunk_x = chunk_x;
    task->chunk_y = chunk_y;

    // Initialize loading state
    task->map_terrain = NULL;
    task->map_locs_iter = NULL;
    task->current_map_loc = NULL;
    task->model_cache = NULL;
    task->config_locs_table = NULL;
    task->config_object_table = NULL;
    task->config_sequence_table = NULL;
    task->config_idk_table = NULL;
    task->shade_map = NULL;

    ht_init(
        &task->loaded_models,
        (struct HashTableInit){
            .element_size = sizeof(struct CacheModel*),
            .capacity_hint = 200,
            .key_size = sizeof(int),
        });

    // Initialize queued models array
    task->queued_model_ids = NULL;
    task->queued_model_count = 0;
    task->queued_model_capacity = 0;

    return task;
}

// Async model loading functions
static void
io_queue_model(struct GameTaskSceneLoad* task, int model_id)
{
    // Check if already queued
    for( int i = 0; i < task->queued_model_count; i++ )
    {
        if( task->queued_model_ids[i] == model_id )
            return;
    }

    // Add to queue
    if( task->queued_model_count >= task->queued_model_capacity )
    {
        task->queued_model_capacity =
            task->queued_model_capacity == 0 ? 16 : task->queued_model_capacity * 2;
        task->queued_model_ids =
            realloc(task->queued_model_ids, sizeof(int) * task->queued_model_capacity);
    }

    task->queued_model_ids[task->queued_model_count++] = model_id;
}

static void
calculate_wall_decor_offset(struct SceneModel* decor, int orientation, int offset, bool diagonal)
{
    assert(orientation >= 0);
    assert(orientation < 4);

    int x_multiplier = diagonal ? WALL_DECOR_ROTATION_DIAGONAL_OFFSET_X[orientation]
                                : WALL_DECOR_ROTATION_OFFSET_X[orientation];
    int z_multiplier = diagonal ? WALL_DECOR_ROTATION_DIAGONAL_OFFSET_Z[orientation]
                                : WALL_DECOR_ROTATION_OFFSET_Z[orientation];
    int offset_x = offset * x_multiplier;
    int offset_z = offset * z_multiplier;

    decor->offset_x = offset_x;
    decor->offset_z = offset_z;
}

static void
init_wall_decor_default(struct WallDecor* wall_decor)
{
    memset(wall_decor, 0, sizeof(struct WallDecor));
    wall_decor->model_a = -1;
    wall_decor->model_b = -1;
}

enum GameIOStatus
gametask_scene_load_send(struct GameTaskSceneLoad* task)
{
    enum GameIOStatus status = E_GAMEIO_STATUS_ERROR;
    struct CacheArchiveTuple archive_tuple = { 0 };
    struct CacheArchive* archive = NULL;
    struct GameIO* io = task->io;

    switch( task->step )
    {
    case E_SCENE_LOAD_STEP_INITIAL:
        goto initial;
    case E_SCENE_LOAD_STEP_MAP_TERRAIN:
        goto map_terrain;
    case E_SCENE_LOAD_STEP_MAP_LOCS:
        goto map_locs;
    case E_SCENE_LOAD_STEP_LOAD_CONFIG_SEQUENCE:
        goto load_config_sequence;
    case E_SCENE_LOAD_STEP_LOAD_CONFIG_LOCS:
        goto load_config_locs;
    case E_SCENE_LOAD_STEP_LOAD_CONFIG_OBJECT:
        goto load_config_object;
    case E_SCENE_LOAD_STEP_LOAD_CONFIG_IDK:
        goto load_config_idk;
    case E_SCENE_LOAD_STEP_PROCESS_LOCS:
        goto process_locs;
    case E_SCENE_LOAD_STEP_LOAD_MODELS:
        goto load_models;
    case E_SCENE_LOAD_STEP_COMPLETE_MODELS:
        goto complete_models;
    case E_SCENE_LOAD_STEP_BUILD_LIGHTING:
        goto build_lighting;
    default:
        goto error;
    }

initial:;
    map_terrain_io(task->cache, task->chunk_x, task->chunk_y, &archive_tuple);
    status = gameio_request_new_archive_load(
        io, archive_tuple.table_id, archive_tuple.archive_id, &task->request);
    if( !gameio_resolved(status) )
        return status;
    task->step = E_SCENE_LOAD_STEP_MAP_TERRAIN;

map_terrain:
{
    printf("Scene load request resolved: %d\n", task->request->request_id);

    // Receive the archive using the proper gameio function
    archive = gameio_request_free_archive_receive(&task->request);
    if( !archive )
    {
        printf("Failed to receive terrain archive\n");
        goto error;
    }
    cache_archive_init_metadata(task->cache, archive);

    // Load the map terrain from the resolved archive
    task->map_terrain = map_terrain_new_from_archive(archive, task->chunk_x, task->chunk_y);
    if( !task->map_terrain )
    {
        printf("Failed to load map terrain\n");
        cache_archive_free(archive);
        goto error;
    }
    cache_archive_free(archive);
    archive = NULL;
    // Transition to next step
    task->step = E_SCENE_LOAD_STEP_LOAD_CONFIG_SEQUENCE;
    goto load_config_sequence;
}

load_config_sequence:
{
    status = gameio_request_new_archive_load(io, CACHE_CONFIGS, CONFIG_SEQUENCE, &task->request);
    if( !gameio_resolved(status) )
        return status;

    // Create the config sequence table from the loaded archive
    task->config_sequence_table = malloc(sizeof(struct CacheConfigSequenceTable));
    if( !task->config_sequence_table )
    {
        printf("Failed to allocate config sequence table\n");
        goto error;
    }
    memset(task->config_sequence_table, 0, sizeof(struct CacheConfigSequenceTable));

    archive = gameio_request_free_archive_receive(&task->request);
    if( !archive )
    {
        printf("Failed to receive config sequence archive\n");
        goto error;
    }
    cache_archive_init_metadata(task->cache, archive);

    task->config_sequence_table = config_sequence_table_new_from_archive(archive);
    if( !task->config_sequence_table )
    {
        printf("Failed to load config sequence table\n");
        cache_archive_free(archive);
        goto error;
    }
    // cache_archive_free(archive);
    archive = NULL;

    // Transition to next step
    task->step = E_SCENE_LOAD_STEP_LOAD_CONFIG_LOCS;
    goto load_config_locs;
}

load_config_locs:
{
    status = gameio_request_new_archive_load(io, CACHE_CONFIGS, CONFIG_LOCS, &task->request);
    if( !gameio_resolved(status) )
        return status;

    archive = gameio_request_free_archive_receive(&task->request);
    if( !archive )
    {
        printf("Failed to receive config locs archive\n");
        goto error;
    }
    cache_archive_init_metadata(task->cache, archive);

    // Create the config locs table from the loaded archive
    task->config_locs_table = config_locs_table_new_from_archive(archive);
    if( !task->config_locs_table )
    {
        printf("Failed to load config locs table\n");
        cache_archive_free(archive);
        goto error;
    }
    // cache_archive_free(archive);
    archive = NULL;

    // Transition to next step
    task->step = E_SCENE_LOAD_STEP_LOAD_CONFIG_OBJECT;
    goto load_config_object;
}

load_config_object:
{
    status = gameio_request_new_archive_load(io, CACHE_CONFIGS, CONFIG_OBJECT, &task->request);
    if( !gameio_resolved(status) )
        return status;

    archive = gameio_request_free_archive_receive(&task->request);
    if( !archive )
    {
        printf("Failed to receive config object archive\n");
        goto error;
    }
    cache_archive_init_metadata(task->cache, archive);

    // Create the config object table from the loaded archive
    task->config_object_table = config_object_table_new_from_archive(archive);
    if( !task->config_object_table )
    {
        printf("Failed to load config object table\n");
        cache_archive_free(archive);
        goto error;
    }
    cache_archive_free(archive);
    archive = NULL;

    // Transition to next step
    task->step = E_SCENE_LOAD_STEP_LOAD_CONFIG_IDK;
    goto load_config_idk;
}

load_config_idk:
{
    status = gameio_request_new_archive_load(io, CACHE_CONFIGS, CONFIG_IDENTKIT, &task->request);
    if( !gameio_resolved(status) )
        return status;

    archive = gameio_request_free_archive_receive(&task->request);
    if( !archive )
    {
        printf("Failed to receive config idk archive\n");
        goto error;
    }
    cache_archive_init_metadata(task->cache, archive);

    // Create the config idk table from the loaded archive
    task->config_idk_table = config_idk_table_new_from_archive(archive);
    if( !task->config_idk_table )
    {
        printf("Failed to load config idk table\n");
        cache_archive_free(archive);
        goto error;
    }
    cache_archive_free(archive);
    archive = NULL;

    // Transition to next step
    task->step = E_SCENE_LOAD_STEP_MAP_LOCS;
    goto map_locs;
}

map_locs:
{
    // Get the locs archive tuple and request it
    map_locs_io(task->cache, task->chunk_x, task->chunk_y, &task->locs_archive_tuple);
    status = gameio_request_new_archive_load(
        io, task->locs_archive_tuple.table_id, task->locs_archive_tuple.archive_id, &task->request);
    if( !gameio_resolved(status) )
        return status;

    // Store the archive and create the iterator
    // Receive the archive using the proper gameio function
    archive = gameio_request_free_archive_receive(&task->request);
    if( !archive )
    {
        printf("Failed to receive locs archive\n");
        goto error;
    }
    cache_archive_init_metadata(task->cache, archive);

    // Create the map locs iterator from the resolved archive
    task->map_locs_iter = map_locs_iter_new_from_archive(archive);
    if( !task->map_locs_iter )
    {
        printf("Failed to load map locs iter\n");
        cache_archive_free(archive);
        goto error;
    }
    // cache_archive_free(archive);
    archive = NULL;

    // Transition to next step
    task->step = E_SCENE_LOAD_STEP_PROCESS_LOCS;
    goto process_locs;
}
process_locs:
{
    // Initialize the scene structure
    task->scene = malloc(sizeof(struct Scene));
    memset(task->scene, 0, sizeof(struct Scene));

    task->scene->grid_tiles = malloc(sizeof(struct GridTile) * MAP_TILE_COUNT);
    memset(task->scene->grid_tiles, 0, sizeof(struct GridTile) * MAP_TILE_COUNT);
    task->scene->grid_tiles_length = MAP_TILE_COUNT;
    task->model_cache = model_cache_new();
    task->scene->_model_cache = task->model_cache;

    task->scene->models = malloc(sizeof(struct SceneModel) * 1024);
    memset(task->scene->models, 0, sizeof(struct SceneModel) * 1024);
    task->scene->models_length = 0;
    task->scene->models_capacity = 1024;

    task->scene->locs = malloc(sizeof(struct Loc) * 1024);
    memset(task->scene->locs, 0, sizeof(struct Loc) * 1024);
    task->scene->locs_length = 0;
    task->scene->locs_capacity = 1024;

    task->shade_map = malloc(sizeof(int) * MAP_TILE_COUNT);
    memset(task->shade_map, 0, sizeof(int) * MAP_TILE_COUNT);
    task->scene->_shade_map = task->shade_map;
    task->scene->_shade_map_length = MAP_TILE_COUNT;

    // Initialize grid tiles
    for( int level = 0; level < MAP_TERRAIN_Z; level++ )
    {
        for( int x = 0; x < MAP_TERRAIN_X; x++ )
        {
            for( int y = 0; y < MAP_TERRAIN_Y; y++ )
            {
                struct GridTile* grid_tile = &task->scene->grid_tiles[MAP_TILE_COORD(x, y, level)];
                grid_tile->x = x;
                grid_tile->z = y;
                grid_tile->level = level;
                grid_tile->spans = 0;
                grid_tile->ground = -1;
                grid_tile->wall = -1;
                grid_tile->ground_decor = -1;
                grid_tile->wall_decor = -1;
                grid_tile->bridge_tile = -1;
                grid_tile->ground_object_bottom = -1;
                grid_tile->ground_object_middle = -1;
                grid_tile->ground_object_top = -1;
            }
        }
    }

    // Process locs - actual implementation
    struct CacheMapLoc* map = NULL;
    struct CacheConfigLocation* loc_config = NULL;
    struct SceneModel* scene_model = NULL;
    struct Loc* loc = NULL;
    struct GridTile* grid_tile = NULL;
    int loc_count = 0;

    while( (map = map_locs_iter_next(task->map_locs_iter)) != NULL )
    {
        loc_count++;

        int tile_x = map->chunk_pos_x;
        int tile_y = map->chunk_pos_y;
        int tile_z = map->chunk_pos_level;

        grid_tile = &task->scene->grid_tiles[MAP_TILE_COORD(tile_x, tile_y, tile_z)];

        int height_sw = task->map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x, tile_y, tile_z)].height;
        int height_se = height_sw;
        if( tile_x + 1 < MAP_TERRAIN_X )
            height_se =
                task->map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x + 1, tile_y, tile_z)].height;

        int height_ne = height_sw;
        if( tile_y + 1 < MAP_TERRAIN_Y && tile_x + 1 < MAP_TERRAIN_X )
            height_ne =
                task->map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x + 1, tile_y + 1, tile_z)].height;

        int height_nw = height_sw;
        if( tile_y + 1 < MAP_TERRAIN_Y )
            height_nw =
                task->map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x, tile_y + 1, tile_z)].height;

        int height_center = (height_sw + height_se + height_ne + height_nw) >> 2;

        loc_config = config_locs_table_get_new(task->config_locs_table, map->loc_id);
        if( !loc_config )
            continue;

        switch( map->shape_select )
        {
        case LOC_SHAPE_WALL_SINGLE_SIDE:
        {
            // Load model asynchronously
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            loc_load_model_async(
                task, scene_model, loc_config, map->shape_select, map->orientation);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = 1; // LOC_TYPE_WALL
            init_wall_default(&loc->_wall);

            loc->_wall.model_a = model_index;
            assert(map->orientation >= 0);
            assert(map->orientation < 4);

            int orientation = map->orientation;
            loc->_wall.side_a = ROTATION_WALL_TYPE[orientation];

            int wall_width = loc_config->wall_width;
            loc->_wall.wall_width = wall_width;

            grid_tile->wall = loc_index;
        }
        break;

        case LOC_SHAPE_WALL_TRI_CORNER:
        {
            // Load model
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            loc_load_model_async(
                task, scene_model, loc_config, map->shape_select, map->orientation);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL;
            init_wall_default(&loc->_wall);

            loc->_wall.model_a = model_index;
            assert(map->orientation >= 0);
            assert(map->orientation < 4);
            loc->_wall.side_a = ROTATION_WALL_CORNER_TYPE[map->orientation];

            grid_tile->wall = loc_index;

            if( loc_config->shadowed )
            {
                switch( map->orientation )
                {
                case 0:
                {
                    if( tile_y < MAP_TERRAIN_Y - 1 )
                        task->shade_map[MAP_TILE_COORD(tile_x, tile_y + 1, tile_z)] = 50;
                }
                break;
                case 1:
                {
                    if( tile_x < MAP_TERRAIN_X - 1 && tile_y < MAP_TERRAIN_Y - 1 )
                        task->shade_map[MAP_TILE_COORD(tile_x + 1, tile_y + 1, tile_z)] = 50;
                }
                break;
                case 2:
                {
                    if( tile_x < MAP_TERRAIN_X - 1 )
                        task->shade_map[MAP_TILE_COORD(tile_x + 1, tile_y, tile_z)] = 50;
                }
                break;
                case 3:
                {
                    task->shade_map[MAP_TILE_COORD(tile_x, tile_y, tile_z)] = 50;
                }
                break;
                default:
                {
                    assert(false && "Invalid orientation");
                }
                }
            }
        }
        break;

        case LOC_SHAPE_WALL_TWO_SIDES:
        {
            int next_orientation = (map->orientation + 1) & 0x3;

            // Load model A
            int model_a_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);
            loc_load_model_async(
                task, scene_model, loc_config, LOC_SHAPE_WALL_TWO_SIDES, map->orientation + 4);
            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // Load model B
            int model_b_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);
            loc_load_model_async(
                task, scene_model, loc_config, LOC_SHAPE_WALL_TWO_SIDES, next_orientation);
            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL;
            init_wall_default(&loc->_wall);

            loc->_wall.model_a = model_a_index;
            loc->_wall.side_a = ROTATION_WALL_TYPE[map->orientation];

            loc->_wall.model_b = model_b_index;
            loc->_wall.side_b = ROTATION_WALL_TYPE[next_orientation];

            int wall_width = loc_config->wall_width;
            loc->_wall.wall_width = wall_width;

            grid_tile->wall = loc_index;
        }
        break;

        case LOC_SHAPE_WALL_RECT_CORNER:
        {
            // Load model
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            loc_load_model_async(
                task, scene_model, loc_config, map->shape_select, map->orientation);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL;
            init_wall_default(&loc->_wall);

            loc->_wall.model_a = model_index;
            assert(map->orientation >= 0);
            assert(map->orientation < 4);
            loc->_wall.side_a = ROTATION_WALL_CORNER_TYPE[map->orientation];

            assert(scene_model->model_id != 0);
            grid_tile->wall = loc_index;

            if( loc_config->shadowed )
            {
                switch( map->orientation )
                {
                case 0:
                {
                    if( tile_y < MAP_TERRAIN_Y - 1 )
                        task->shade_map[MAP_TILE_COORD(tile_x, tile_y + 1, tile_z)] = 50;
                }
                break;
                case 1:
                {
                    if( tile_x < MAP_TERRAIN_X - 1 && tile_y < MAP_TERRAIN_Y - 1 )
                        task->shade_map[MAP_TILE_COORD(tile_x + 1, tile_y + 1, tile_z)] = 50;
                }
                break;
                case 2:
                {
                    if( tile_x < MAP_TERRAIN_X - 1 )
                        task->shade_map[MAP_TILE_COORD(tile_x + 1, tile_y, tile_z)] = 50;
                }
                break;
                case 3:
                {
                    task->shade_map[MAP_TILE_COORD(tile_x, tile_y, tile_z)] = 50;
                }
                break;
                default:
                {
                    assert(false && "Invalid orientation");
                }
                }
            }
        }
        break;

        case LOC_SHAPE_WALL_DIAGONAL:
        {
            // Load model
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            loc_load_model_async(
                task, scene_model, loc_config, map->shape_select, map->orientation);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // Add the loc - diagonal walls are treated as scenery
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_SCENERY;
            loc->_scenery.model = model_index;

            grid_tile->locs[grid_tile->locs_length++] = loc_index;
        }
        break;

        case LOC_SHAPE_WALL_DECOR_NOOFFSET:
        {
            // Load model
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            loc_load_model_async(
                task, scene_model, loc_config, LOC_SHAPE_WALL_DECOR_NOOFFSET, map->orientation);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL_DECOR;
            init_wall_decor_default(&loc->_wall_decor);

            loc->_wall_decor.model_a = model_index;
            loc->_wall_decor.side = ROTATION_WALL_TYPE[map->orientation];

            grid_tile->wall_decor = loc_index;
        }
        break;

        case LOC_SHAPE_WALL_DECOR_OFFSET:
        {
            // Load model
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            loc_load_model_async(
                task, scene_model, loc_config, LOC_SHAPE_WALL_DECOR_NOOFFSET, map->orientation);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // Default offset is 16.
            int offset = 16;
            // The wall was loaded first, get the offset and use it.
            if( grid_tile->wall != -1 )
            {
                struct Loc* other_loc = &task->scene->locs[grid_tile->wall];
                assert(other_loc->type == LOC_TYPE_WALL);
                if( other_loc->_wall.wall_width )
                    offset = other_loc->_wall.wall_width;
            }

            calculate_wall_decor_offset(scene_model, map->orientation, offset, false);

            // Add the loc
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL_DECOR;
            init_wall_decor_default(&loc->_wall_decor);

            loc->_wall_decor.model_a = model_index;
            loc->_wall_decor.side = ROTATION_WALL_TYPE[map->orientation];

            grid_tile->wall_decor = loc_index;
        }
        break;

        case LOC_SHAPE_WALL_DECOR_DIAGONAL_OFFSET:
        {
            // Load model
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            loc_load_model_async(
                task, scene_model, loc_config, LOC_SHAPE_WALL_DECOR_NOOFFSET, map->orientation);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // TODO: Get this from the wall offset?
            // This needs to be taken from the wall offset.
            // Lumbridge walls are 16 thick.
            // Walls in al kharid are 8 thick.
            int offset = 61;
            calculate_wall_decor_offset(scene_model, map->orientation, offset, true);
            scene_model->yaw += 256; // WALL_DECOR_YAW_ADJUST_DIAGONAL_OUTSIDE

            // Add the loc
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL_DECOR;
            init_wall_decor_default(&loc->_wall_decor);

            loc->_wall_decor.model_a = model_index;
            assert(map->orientation >= 0);
            assert(map->orientation < 4);
            loc->_wall_decor.side = ROTATION_WALL_CORNER_TYPE[map->orientation];
            loc->_wall_decor.through_wall_flags = 1; // THROUGHWALL

            grid_tile->wall_decor = loc_index;
        }
        break;

        case LOC_SHAPE_WALL_DECOR_DIAGONAL_NOOFFSET:
        {
            // Load model
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            int orientation = map->orientation;
            orientation = (orientation + 2) % 4;
            loc_load_model_async(
                task, scene_model, loc_config, LOC_SHAPE_WALL_DECOR_NOOFFSET, map->orientation + 3);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            int offset = 45;
            calculate_wall_decor_offset(scene_model, orientation, offset, true);
            scene_model->yaw += 768; // WALL_DECOR_YAW_ADJUST_DIAGONAL_INSIDE

            // Add the loc
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL_DECOR;
            init_wall_decor_default(&loc->_wall_decor);

            loc->_wall_decor.model_a = model_index;
            loc->_wall_decor.side = ROTATION_WALL_CORNER_TYPE[orientation];
            loc->_wall_decor.through_wall_flags = 1; // THROUGHWALL

            grid_tile->wall_decor = loc_index;
        }
        break;

        case LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE:
        {
            int outside_orientation = map->orientation;
            int inside_orientation = (map->orientation + 2) & 0x3;

            // Load outside model
            int model_index_a = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);
            loc_load_model_async(
                task,
                scene_model,
                loc_config,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                outside_orientation + 1);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // TODO: Get this from the wall offset?
            int offset = 61;
            calculate_wall_decor_offset(scene_model, (outside_orientation) & 0x3, offset, true);
            scene_model->yaw += 768; // WALL_DECOR_YAW_ADJUST_DIAGONAL_INSIDE

            // Load inside model
            int model_index_b = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);
            loc_load_model_async(
                task, scene_model, loc_config, LOC_SHAPE_WALL_DECOR_NOOFFSET, inside_orientation);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            offset = 45;
            calculate_wall_decor_offset(scene_model, inside_orientation, offset, true);
            scene_model->yaw += 256; // WALL_DECOR_YAW_ADJUST_DIAGONAL_OUTSIDE

            // Add the loc
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL_DECOR;
            init_wall_decor_default(&loc->_wall_decor);

            loc->_wall_decor.model_a = model_index_a;
            loc->_wall_decor.model_b = model_index_b;
            assert(outside_orientation >= 0);
            assert(outside_orientation < 4);
            loc->_wall_decor.side = ROTATION_WALL_CORNER_TYPE[outside_orientation];
            loc->_wall_decor.through_wall_flags = 1; // THROUGHWALL

            grid_tile->wall_decor = loc_index;
        }
        break;

        case LOC_SHAPE_FLOOR_DECORATION:
        {
            // Load model
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            loc_load_model_async(
                task, scene_model, loc_config, map->shape_select, map->orientation);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_GROUND_DECOR;
            loc->_ground_decor.model = model_index;

            grid_tile->ground_decor = loc_index;
        }
        break;

        case LOC_SHAPE_ROOF_SLOPED:
        case LOC_SHAPE_ROOF_SLOPED_OUTER_CORNER:
        case LOC_SHAPE_ROOF_SLOPED_INNER_CORNER:
        case LOC_SHAPE_ROOF_SLOPED_HARD_INNER_CORNER:
        case LOC_SHAPE_ROOF_SLOPED_HARD_OUTER_CORNER:
        case LOC_SHAPE_ROOF_FLAT:
        case LOC_SHAPE_ROOF_SLOPED_OVERHANG:
        case LOC_SHAPE_ROOF_SLOPED_OVERHANG_OUTER_CORNER:
        case LOC_SHAPE_ROOF_SLOPED_OVERHANG_INNER_CORNER:
        case LOC_SHAPE_ROOF_SLOPED_OVERHANG_HARD_OUTER_CORNER:
        {
            // Load model
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            loc_load_model_async(
                task, scene_model, loc_config, map->shape_select, map->orientation);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // Add the loc - roofs are treated as scenery
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_SCENERY;
            loc->_scenery.model = model_index;

            grid_tile->locs[grid_tile->locs_length++] = loc_index;
        }
        break;

        case LOC_SHAPE_SCENERY:
        case LOC_SHAPE_SCENERY_DIAGIONAL:
        {
            // Load model asynchronously
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            loc_load_model_async(
                task, scene_model, loc_config, map->shape_select, map->orientation);

            int size_x = loc_config->size_x;
            int size_y = loc_config->size_y;
            if( map->orientation == 1 || map->orientation == 3 )
            {
                int temp = size_x;
                size_x = size_y;
                size_y = temp;
            }

            init_scene_model_wxh(scene_model, tile_x, tile_y, height_center, size_x, size_y);
            if( map->shape_select == 11 ) // LOC_SHAPE_SCENERY_DIAGIONAL
                scene_model->yaw += 256;

            // Add the loc
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->size_x = size_x;
            loc->size_y = size_y;
            loc->type = 0; // LOC_TYPE_SCENERY

            loc->_scenery.model = model_index;

            // Compute spans
            compute_normal_scenery_spans(
                task->scene->grid_tiles, tile_x, tile_y, tile_z, size_x, size_y, loc_index);
        }
        break;

        default:
            // Skip other loc types for now
            break;
        }

        // Free the loc config
        // free_loc(loc_config);
    }

    printf("Processed %d locs\n", loc_count);

    task->scene->terrain = task->map_terrain;

    // Now request all the queued models
    task->step = E_SCENE_LOAD_STEP_LOAD_MODELS;
    goto load_models;
}

load_models:
{
    struct CacheModel* cache_model = NULL;
    for( ; task->queue_index < task->queued_model_count; task->queue_index++ )
    {
        int model_id = task->queued_model_ids[task->queue_index];

        // Check if this model is already loaded
        struct CacheModel** loaded =
            ht_lookup(&task->loaded_models, (char const*)&model_id, sizeof(int));
        if( loaded )
            continue;

        status = gameio_request_new_archive_load(task->io, CACHE_MODELS, model_id, &task->request);
        if( !gameio_resolved(status) )
            return status;

        archive = gameio_request_free_archive_receive(&task->request);
        if( !archive )
            return E_GAMEIO_STATUS_ERROR;
        cache_archive_init_metadata(task->cache, archive);

        cache_model = model_new_from_archive(archive, model_id);
        if( cache_model )
        {
            struct CacheModel** stored =
                ht_emplace(&task->loaded_models, (char const*)&model_id, sizeof(int));
            *stored = cache_model;
            cache_model->_flags |= CMODEL_FLAG_SHARED;
        }
        cache_archive_free(archive);
        archive = NULL;
    }

    // No more models in queue, all done
    task->step = E_SCENE_LOAD_STEP_COMPLETE_MODELS;
    goto complete_models;
}
complete_models:
{
    // Complete loading for all scene models
    for( int i = 0; i < task->scene->models_length; i++ )
    {
        struct SceneModel* scene_model = &task->scene->models[i];
        if( scene_model->_pending_loc_config )
        {
            // This scene model needs to be completed
            complete_scene_model_loading(
                task, scene_model, 0, 0, 0, 0); // TODO: Pass proper height values
        }
    }

    task->step = E_SCENE_LOAD_STEP_BUILD_LIGHTING;
    goto build_lighting;
}

build_lighting:
{
    // TODO: Implement full lighting calculation from scene.c
    // For now, just mark as done
    task->step = E_SCENE_LOAD_STEP_DONE;
    goto done;
}

done:
    return E_GAMEIO_STATUS_OK;

error:
    printf("Async Task False Start: %d\n", task->step);
    return E_GAMEIO_STATUS_ERROR;
}

struct Scene*
gametask_scene_value(struct GameTaskSceneLoad* task)
{
    assert(task->step == E_SCENE_LOAD_STEP_DONE);
    assert(task->scene != NULL);
    return task->scene;
}

void
gametask_scene_load_free(struct GameTaskSceneLoad* task)
{
    // Clean up archives
    if( task->locs_archive )
        cache_archive_free(task->locs_archive);

    // Clean up scene if it exists
    if( task->scene )
    {
        free(task->scene->grid_tiles);
        free(task->scene->models);
        free(task->scene->locs);
        free(task->scene->_shade_map);
        if( task->scene->terrain )
            map_terrain_free(task->scene->terrain);
        free(task->scene);
    }

    ht_cleanup(&task->loaded_models);

    // Clean up queued models array
    free(task->queued_model_ids);

    free(task);
}