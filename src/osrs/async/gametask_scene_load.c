#include "gametask_scene_load.h"

#include "datastruct/ht.h"
#include "graphics/lighting.h"
#include "osrs/cache.h"
#include "osrs/configmap.h"
#include "osrs/filelist.h"
#include "osrs/model_transforms.h"
#include "osrs/scene.h"
#include "osrs/tables/config_floortype.h"
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

// clang-format off
#include "../sceneload.u.c"
// clang-format on

/**
 * Data Dependencies
 *
 * Map Locs
 *   -> Log Configs
 *      -> Models
 *         -> Textures
 *      -> Sequences
 *         -> Animations
 *             -> Framemaps
 *
 * Map Terrain
 *   -> Underlay
 *      -> Textures
 *   -> Overlay
 *      -> Textures
 *
 * Flattened:
 * Map Locs
 * Loc Configs
 * Models
 * Sequences
 * Animations
 * Framemaps
 * Terrain
 * Underlays
 * Overlay
 * Textures
 *
 *
 * Don't need:
 * IDK, Object
 */

struct ArchiveList
{
    struct Archive* archive;
    struct ArchiveList* next;
};

enum SceneLoadStep
{
    E_SCENE_LOAD_STEP_INITIAL = 0,
    E_SCENE_LOAD_STEP_MAP_TERRAIN,
    E_SCENE_LOAD_STEP_MAP_LOCS,
    E_SCENE_LOAD_STEP_LOAD_CONFIG_SEQUENCE,
    E_SCENE_LOAD_STEP_LOAD_CONFIG_LOCS,
    E_SCENE_LOAD_STEP_PROCESS_LOCS,
    E_SCENE_LOAD_STEP_LOAD_MODELS,
    E_SCENE_LOAD_STEP_COMPLETE_MODELS,
    E_SCENE_LOAD_STEP_BUILD_LIGHTING,
    E_SCENE_LOAD_STEP_LOAD_CONFIG_UNDERLAY,
    E_SCENE_LOAD_STEP_LOAD_CONFIG_OVERLAY,
    E_SCENE_LOAD_STEP_LOAD_TILES,
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
    // struct CacheConfigLocationTable* config_locs_table;
    struct ConfigMap* config_locs_map;
    // struct CacheConfigObjectTable* config_object_table;
    // struct CacheConfigSequenceTable* config_sequence_table;
    struct ConfigMap* config_sequence_map;
    // struct CacheConfigIdkTable* config_idk_table;
    int* shade_map;
    struct CacheArchiveTuple locs_archive_tuple;
    struct CacheArchive* locs_archive;

    // Tile config loading
    struct ConfigMap* config_underlay_map;
    struct ConfigMap* config_overlay_map;

    // Async model loading
    struct CacheModel** loaded_models; // Array of loaded models
    int* model_ids;                    // Corresponding model IDs
    int loaded_models_count;
    int loaded_models_capacity;
    int* queued_model_ids; // Array of model IDs to load
    int queued_model_count;
    int queued_model_capacity;

    int queue_index;
    bool models_requested; // Whether queued models have been requested
};

// Forward declarations for async model functions
static void io_queue_model(struct GameTaskSceneLoad* task, int model_id);
static struct CacheModel* get_model_async(struct GameTaskSceneLoad* task, int model_id);
static void
add_loaded_model(struct GameTaskSceneLoad* task, int model_id, struct CacheModel* model);

static struct CacheModel*
get_model_async(struct GameTaskSceneLoad* task, int model_id)
{
    // Search through the loaded models list
    for( int i = 0; i < task->loaded_models_count; i++ )
    {
        if( task->model_ids[i] == model_id )
        {
            return task->loaded_models[i];
        }
    }

    return NULL; // Model not found
}

static void
add_loaded_model(struct GameTaskSceneLoad* task, int model_id, struct CacheModel* model)
{
    // Expand arrays if needed
    if( task->loaded_models_count >= task->loaded_models_capacity )
    {
        task->loaded_models_capacity =
            task->loaded_models_capacity == 0 ? 16 : task->loaded_models_capacity * 2;
        task->loaded_models =
            realloc(task->loaded_models, sizeof(struct CacheModel*) * task->loaded_models_capacity);
        task->model_ids = realloc(task->model_ids, sizeof(int) * task->loaded_models_capacity);
    }

    task->loaded_models[task->loaded_models_count] = model;
    task->model_ids[task->loaded_models_count] = model_id;
    task->loaded_models_count++;
}

// Async loc_load_model - requests models asynchronously instead of loading synchronously
static void
io_queue_model_load(
    struct GameTaskSceneLoad* task,
    struct SceneModel* scene_model,
    struct CacheConfigLocation* loc_config,
    int shape_select,
    int orientation,
    int height_sw,
    int height_se,
    int height_ne,
    int height_nw)
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

    // Handle sequences - sequences don't account for rotations, so models must be rotated AFTER the
    // animation is applied
    if( loc_config->seq_id != -1 )
    {
        // Apply rotation to yaw instead of using orientation
        scene_model->yaw = 512 * orientation;
        scene_model->yaw %= 2048;
        orientation = 0; // Reset orientation since it's now in yaw
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
    scene_model->_pending_height_sw = height_sw;
    scene_model->_pending_height_se = height_se;
    scene_model->_pending_height_ne = height_ne;
    scene_model->_pending_height_nw = height_nw;
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
                struct CacheModel* model = get_model_async(task, model_id);
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
                        struct CacheModel* model = get_model_async(task, model_id);
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

    // Create normals
    scene_model->normals = model_normals_new(final_model->vertex_count, final_model->face_count);

    calculate_vertex_normals(
        scene_model->normals->lighting_vertex_normals,
        scene_model->normals->lighting_face_normals,
        final_model->vertex_count,
        final_model->face_indices_a,
        final_model->face_indices_b,
        final_model->face_indices_c,
        final_model->vertices_x,
        final_model->vertices_y,
        final_model->vertices_z,
        final_model->face_count);

    // Handle sharelight
    if( scene_model->sharelight )
        scene_model->aliased_lighting_normals = model_normals_new_copy(scene_model->normals);

    // Create lighting
    scene_model->lighting = model_lighting_new_default(
        final_model,
        scene_model->sharelight ? scene_model->aliased_lighting_normals : scene_model->normals,
        scene_model->light_contrast,
        0);

    // Handle bones
    if( final_model->vertex_bone_map )
        scene_model->vertex_bones =
            modelbones_new_decode(final_model->vertex_bone_map, final_model->vertex_count);
    if( final_model->face_bone_map )
        scene_model->face_bones =
            modelbones_new_decode(final_model->face_bone_map, final_model->face_count);

    // Handle sequences
    if( loc_config->seq_id != -1 )
    {
        // Create original vertices for animation
        scene_model_vertices_create_original(scene_model);

        if( final_model->face_alphas )
            scene_model_face_alphas_create_original(scene_model);

        // Load the sequence
        struct CacheConfigSequence* sequence = (struct CacheConfigSequence*)configmap_get(
            task->config_sequence_map, loc_config->seq_id);
        if( sequence )
        {
            scene_model->sequence = sequence;

            // Set up frames array
            scene_model->frames = malloc(sizeof(struct CacheFrame*) * sequence->frame_count);
            memset(scene_model->frames, 0, sizeof(struct CacheFrame*) * sequence->frame_count);

            // TODO: Load frame data - this is complex and involves archive loading
            // For now, just set up the structure
            scene_model->anim_frame_count = sequence->frame_count;
        }
        else
        {
            scene_model->sequence = NULL;
        }
    }
    else
    {
        scene_model->sequence = NULL;
    }

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
    // task->config_locs_table = NULL;
    // task->config_object_table = NULL;
    // task->config_sequence_table = NULL;
    // task->config_idk_table = NULL;
    task->shade_map = NULL;

    // Initialize loaded models array
    task->loaded_models = NULL;
    task->model_ids = NULL;
    task->loaded_models_count = 0;
    task->loaded_models_capacity = 0;

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

enum GameIOStatus
gametask_scene_load_send(struct GameTaskSceneLoad* task)
{
    enum GameIOStatus status = E_GAMEIO_STATUS_ERROR;
    struct CacheArchiveTuple archive_tuple = { 0 };
    struct CacheArchive* archive = NULL;
    struct GameIO* io = task->io;
    struct CacheMapFloor* floor;
    struct GridTile bridge_tile;
    struct GridTile* grid_tile;
    struct CacheMapLoc* map;
    struct CacheConfigLocation* loc_config;
    struct Loc* other_loc;
    struct SceneModel* scene_model;
    struct SceneModel* other_model;
    struct Loc* loc;

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
    case E_SCENE_LOAD_STEP_PROCESS_LOCS:
        goto process_locs;
    case E_SCENE_LOAD_STEP_LOAD_MODELS:
        goto load_models;
    case E_SCENE_LOAD_STEP_COMPLETE_MODELS:
        goto complete_models;
    case E_SCENE_LOAD_STEP_BUILD_LIGHTING:
        goto build_lighting;
    case E_SCENE_LOAD_STEP_LOAD_CONFIG_UNDERLAY:
        goto load_config_underlay;
    case E_SCENE_LOAD_STEP_LOAD_CONFIG_OVERLAY:
        goto load_config_overlay;
    case E_SCENE_LOAD_STEP_LOAD_TILES:
        goto load_tiles;
    case E_SCENE_LOAD_STEP_DONE:
        goto error;
    default:
        goto error;
    }

initial:;
    map_terrain_io(task->cache, task->chunk_x, task->chunk_y, &archive_tuple);
    status = gameio_request_new_archive_load(
        io, archive_tuple.table_id, archive_tuple.archive_id, &task->request);
    if( !gameio_resolved(status) )
        return status;

map_terrain:
    task->step = E_SCENE_LOAD_STEP_MAP_TERRAIN;

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

load_config_sequence:
    task->step = E_SCENE_LOAD_STEP_LOAD_CONFIG_SEQUENCE;

    status = gameio_request_new_archive_load(io, CACHE_CONFIGS, CONFIG_SEQUENCE, &task->request);
    if( !gameio_resolved(status) )
        return status;

    archive = gameio_request_free_archive_receive(&task->request);
    if( !archive )
    {
        printf("Failed to receive config sequence archive\n");
        goto error;
    }
    cache_archive_init_metadata(task->cache, archive);

    task->config_sequence_map = configmap_new_from_archive(task->cache, archive);
    if( !task->config_sequence_map )
    {
        printf("Failed to load config sequence map\n");
        goto error;
    }
    cache_archive_free(archive);
    archive = NULL;

load_config_locs:
    task->step = E_SCENE_LOAD_STEP_LOAD_CONFIG_LOCS;

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

    task->config_locs_map = configmap_new_from_archive(task->cache, archive);
    if( !task->config_locs_map )
    {
        printf("Failed to load config locs map\n");
        goto error;
    }
    cache_archive_free(archive);
    archive = NULL;

map_locs:
    task->step = E_SCENE_LOAD_STEP_MAP_LOCS;

    // Get the locs archive tuple and request it
    map_locs_io(task->cache, task->chunk_x, task->chunk_y, &task->locs_archive_tuple);
    status = gameio_request_new_archive_load(
        io, task->locs_archive_tuple.table_id, task->locs_archive_tuple.archive_id, &task->request);
    if( !gameio_resolved(status) )
        return status;

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
process_locs:
    task->step = E_SCENE_LOAD_STEP_PROCESS_LOCS;

    // Initialize the scene structure
    task->scene = malloc(sizeof(struct Scene));
    memset(task->scene, 0, sizeof(struct Scene));

    task->scene->textures_cache = textures_cache_new(task->cache);
    if( !task->scene->textures_cache )
    {
        printf("Failed to create textures cache\n");
        goto error;
    }

    task->scene->_shade_map = malloc(sizeof(int) * MAP_TILE_COUNT);
    memset(task->scene->_shade_map, 0, sizeof(int) * MAP_TILE_COUNT);
    task->scene->_shade_map_length = MAP_TILE_COUNT;

    task->scene->grid_tiles = malloc(sizeof(struct GridTile) * MAP_TILE_COUNT);
    memset(task->scene->grid_tiles, 0, sizeof(struct GridTile) * MAP_TILE_COUNT);
    task->scene->grid_tiles_length = MAP_TILE_COUNT;

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

    // Allocate scene tiles
    task->scene->scene_tiles = malloc(sizeof(struct SceneTile) * MAP_TILE_COUNT);
    memset(task->scene->scene_tiles, 0, sizeof(struct SceneTile) * MAP_TILE_COUNT);
    task->scene->scene_tiles_length = MAP_TILE_COUNT;

    // Initialize grid tiles
    init_grid_tiles(task->scene->grid_tiles);

    // Process locs - actual implementation
    map = NULL;
    loc_config = NULL;
    other_loc = NULL;
    scene_model = NULL;
    other_model = NULL;
    loc = NULL;
    grid_tile = NULL;
    int* shade_map = task->scene->_shade_map;
    struct Scene* scene = task->scene;
    struct TileHeights tile_heights;
    int loc_count = 0;
    int height_sw = 0;
    int height_se = 0;
    int height_ne = 0;
    int height_nw = 0;
    int height_center = 0;

    /**
     * Data dependencies:
     * Map Locs archive
     * Config locs archive
     *
     *
     */
    while( (map = map_locs_iter_next(task->map_locs_iter)) != NULL )
    {
        loc_count++;

        int tile_x = map->chunk_pos_x;
        int tile_y = map->chunk_pos_y;
        int tile_z = map->chunk_pos_level;

        grid_tile = &task->scene->grid_tiles[MAP_TILE_COORD(tile_x, tile_y, tile_z)];

        tile_heights_init_from_floor(
            &tile_heights, task->map_terrain->tiles_xyz, tile_x, tile_y, tile_z);
        height_sw = tile_heights.sw_height;
        height_se = tile_heights.se_height;
        height_ne = tile_heights.ne_height;
        height_nw = tile_heights.nw_height;
        height_center = tile_heights.height_center;

        loc_config = configmap_get(task->config_locs_map, map->loc_id);
        if( !loc_config )
            continue;

        switch( map->shape_select )
        {
        case LOC_SHAPE_WALL_SINGLE_SIDE:
        {
            // Load model asynchronously
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

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

            int orientation = map->orientation;
            loc->_wall.side_a = ROTATION_WALL_TYPE[orientation];

            int wall_width = loc_config->wall_width;
            loc->_wall.wall_width = wall_width;

            grid_tile->wall = loc_index;

            // If wall_decor was loaded first, we need to update the offset.
            // Decor models are default offset by 16, so we only need to update the offset
            // if the wall width is not 16.
            if( grid_tile->wall_decor != -1 && wall_width != 16 )
            {
                other_loc = &scene->locs[grid_tile->wall_decor];
                assert(other_loc->type == LOC_TYPE_WALL_DECOR);

                other_model = &scene->models[other_loc->_wall_decor.model_a];
                assert(other_model);

                calculate_wall_decor_offset(
                    other_model,
                    map->orientation,
                    wall_width,
                    false // diagonal
                );
            }

            grid_tile->wall = loc_index;

            if( loc_config->shadowed )
            {
                switch( orientation )
                {
                case 0:
                {
                    shade_map[MAP_TILE_COORD(tile_x, tile_y, tile_z)] = 50;
                    if( tile_y < MAP_TERRAIN_Y - 1 )
                        shade_map[MAP_TILE_COORD(tile_x, tile_y + 1, tile_z)] = 50;
                }
                break;
                case 1:
                {
                    if( tile_y < MAP_TERRAIN_Y - 1 )
                        shade_map[MAP_TILE_COORD(tile_x, tile_y + 1, tile_z)] = 50;
                    if( tile_x < MAP_TERRAIN_X - 1 && tile_y < MAP_TERRAIN_Y - 1 )
                        shade_map[MAP_TILE_COORD(tile_x + 1, tile_y + 1, tile_z)] = 50;
                }
                break;
                case 2:
                {
                    if( tile_x < MAP_TERRAIN_X - 1 )
                        shade_map[MAP_TILE_COORD(tile_x + 1, tile_y, tile_z)] = 50;
                    if( tile_x < MAP_TERRAIN_X - 1 && tile_y < MAP_TERRAIN_Y - 1 )
                        shade_map[MAP_TILE_COORD(tile_x + 1, tile_y + 1, tile_z)] = 50;
                }
                break;
                case 3:
                {
                    shade_map[MAP_TILE_COORD(tile_x, tile_y, tile_z)] = 50;
                    if( tile_x < MAP_TERRAIN_X - 1 )
                        shade_map[MAP_TILE_COORD(tile_x + 1, tile_y, tile_z)] = 50;
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

        case LOC_SHAPE_WALL_TRI_CORNER:
        {
            // Load model
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

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
            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                LOC_SHAPE_WALL_TWO_SIDES,
                map->orientation + 4,
                height_sw,
                height_se,
                height_ne,
                height_nw);
            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // Load model B
            int model_b_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);
            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                LOC_SHAPE_WALL_TWO_SIDES,
                next_orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);
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

            // If wall_decor was loaded first, we need to update the offset.
            // Decor models are default offset by 16, so we only need to update the offset
            // if the wall width is not 16.
            if( grid_tile->wall_decor != -1 && wall_width != 16 )
            {
                other_loc = &scene->locs[grid_tile->wall_decor];
                assert(other_loc->type == LOC_TYPE_WALL_DECOR);

                other_model = &scene->models[other_loc->_wall_decor.model_a];
                assert(other_model);

                calculate_wall_decor_offset(
                    other_model,
                    map->orientation,
                    wall_width,
                    false // diagonal
                );
            }
        }
        break;

        case LOC_SHAPE_WALL_RECT_CORNER:
        {
            // Load model
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

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

        case LOC_SHAPE_WALL_DECOR_NOOFFSET:
        {
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

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

            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

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

            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

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
            loc->_wall_decor.through_wall_flags = THROUGHWALL; // THROUGHWALL

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
            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                map->orientation + 3,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            int offset = 45;
            calculate_wall_decor_offset(scene_model, orientation, offset, true);
            scene_model->yaw += WALL_DECOR_YAW_ADJUST_DIAGONAL_INSIDE;

            // Add the loc
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL_DECOR;
            init_wall_decor_default(&loc->_wall_decor);

            loc->_wall_decor.model_a = model_index;
            loc->_wall_decor.side = ROTATION_WALL_CORNER_TYPE[orientation];
            loc->_wall_decor.through_wall_flags = THROUGHWALL; // THROUGHWALL

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
            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                outside_orientation + 1,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // TODO: Get this from the wall offset?
            int offset = 61;
            calculate_wall_decor_offset(scene_model, (outside_orientation) & 0x3, offset, true);
            scene_model->yaw += 768; // WALL_DECOR_YAW_ADJUST_DIAGONAL_INSIDE

            // Load inside model
            int model_index_b = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);
            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                inside_orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

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

        case LOC_SHAPE_WALL_DIAGONAL:
        {
            // Load model
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            // Diagonal walls are treated as scenery.
            // So they are rendered when locs are drawn.
            // (essentially the middle of the tile)
            // This is true in 2004scape and later deobs.
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

            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            int size_x = loc_config->size_x;
            int size_y = loc_config->size_y;
            if( map->orientation == 1 || map->orientation == 3 )
            {
                int temp = size_x;
                size_x = size_y;
                size_y = temp;
            }

            init_scene_model_wxh(scene_model, tile_x, tile_y, height_center, size_x, size_y);
            if( map->shape_select == LOC_SHAPE_SCENERY_DIAGIONAL )
                scene_model->yaw += 256;

            // Add the loc
            int loc_index = vec_loc_push(task->scene);
            loc = vec_loc_back(task->scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->size_x = size_x;
            loc->size_y = size_y;
            loc->type = LOC_TYPE_SCENERY;

            loc->_scenery.model = model_index;

            // Compute spans
            compute_normal_scenery_spans(
                task->scene->grid_tiles, tile_x, tile_y, tile_z, size_x, size_y, loc_index);

            if( loc_config->shadowed )
            {
                for( int x = 0; x < size_x; x++ )
                {
                    for( int y = 0; y < size_y; y++ )
                    {
                        int shade = size_x * size_y * 11;

                        if( shade > 30 )
                            shade = 30;

                        int shade_x = x + tile_x;
                        int shade_y = y + tile_y;

                        if( shade_x < MAP_TERRAIN_X && shade_y < MAP_TERRAIN_Y )
                            shade_map[MAP_TILE_COORD(shade_x, shade_y, tile_z)] = shade;
                    }
                }
            }
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

            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

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

        case LOC_SHAPE_FLOOR_DECORATION:
        {
            // Load model
            int model_index = vec_model_push(task->scene);
            scene_model = vec_model_back(task->scene);

            io_queue_model_load(
                task,
                scene_model,
                loc_config,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

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

        default:
            printf("Unknown loc shape: %d\n", map->shape_select);
            break;
        }

        // Free the loc config
        // free_loc(loc_config);
    }

    printf("Processed %d locs\n", loc_count);

    task->scene->terrain = task->map_terrain;

    // Now request all the queued models

load_models:
    task->step = E_SCENE_LOAD_STEP_LOAD_MODELS;

    struct CacheModel* cache_model = NULL;
    for( ; task->queue_index < task->queued_model_count; task->queue_index++ )
    {
        int model_id = task->queued_model_ids[task->queue_index];

        // Check if this model is already loaded
        bool already_loaded = false;
        for( int i = 0; i < task->loaded_models_count; i++ )
        {
            if( task->model_ids[i] == model_id )
            {
                already_loaded = true;
                break;
            }
        }
        if( already_loaded )
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
            add_loaded_model(task, model_id, cache_model);
            cache_model->_flags |= CMODEL_FLAG_SHARED;
        }
        cache_archive_free(archive);
        archive = NULL;
    }

// No more models in queue, all done
complete_models:
    task->step = E_SCENE_LOAD_STEP_COMPLETE_MODELS;

    // Complete loading for all scene models
    for( int i = 0; i < task->scene->models_length; i++ )
    {
        struct SceneModel* scene_model = &task->scene->models[i];
        if( scene_model->_pending_loc_config )
        {
            // This scene model needs to be completed
            complete_scene_model_loading(
                task,
                scene_model,
                scene_model->_pending_height_sw,
                scene_model->_pending_height_se,
                scene_model->_pending_height_ne,
                scene_model->_pending_height_nw);
        }
    }

load_config_underlay:
    task->step = E_SCENE_LOAD_STEP_LOAD_CONFIG_UNDERLAY;

    status = gameio_request_new_archive_load(io, CACHE_CONFIGS, CONFIG_UNDERLAY, &task->request);
    if( !gameio_resolved(status) )
        return status;

    archive = gameio_request_free_archive_receive(&task->request);
    if( !archive )
    {
        printf("Failed to receive config underlay archive\n");
        goto error;
    }
    cache_archive_init_metadata(task->cache, archive);

    task->config_underlay_map = configmap_new_from_archive(task->cache, archive);

    cache_archive_free(archive);
    archive = NULL;

load_config_overlay:
    task->step = E_SCENE_LOAD_STEP_LOAD_CONFIG_OVERLAY;

    status = gameio_request_new_archive_load(io, CACHE_CONFIGS, CONFIG_OVERLAY, &task->request);
    if( !gameio_resolved(status) )
        return status;

    archive = gameio_request_free_archive_receive(&task->request);
    if( !archive )
    {
        printf("Failed to receive config overlay archive\n");
        goto error;
    }
    cache_archive_init_metadata(task->cache, archive);
    task->config_overlay_map = configmap_new_from_archive(task->cache, archive);

    cache_archive_free(archive);
    archive = NULL;

load_tiles:
    task->step = E_SCENE_LOAD_STEP_LOAD_TILES;

    struct SceneTile* tiles = scene_tiles_new_from_map_terrain(
        task->map_terrain, task->shade_map, task->config_underlay_map, task->config_overlay_map);

    if( !tiles )
    {
        printf("Failed to create scene tiles\n");
        goto error;
    }

    // Copy the created tiles to the scene
    memcpy(task->scene->scene_tiles, tiles, sizeof(struct SceneTile) * MAP_TILE_COUNT);

    // Free the temporary tiles array (but not the individual tile data which was moved)
    free(tiles);

    // Assign tiles to grid tiles
    for( int i = 0; i < MAP_TILE_COUNT; i++ )
    {
        struct SceneTile* scene_tile = &task->scene->scene_tiles[i];
        grid_tile = &task->scene->grid_tiles[MAP_TILE_COORD(
            scene_tile->chunk_pos_x, scene_tile->chunk_pos_y, scene_tile->chunk_pos_level)];
        grid_tile->ground = i;
    }

build_lighting:
    task->step = E_SCENE_LOAD_STEP_BUILD_LIGHTING;

    scene = task->scene;
    struct CacheMapTerrain* map_terrain = task->map_terrain;

    // Adjust bridges.
    // This MUST occur after the tiles are assigned to the grid tiles.
    /**
     * Bridges are adjusted from an upper level.
     *
     * The "bridge_tile" is actually the tiles below the bridge.
     * The bridge itself is taken from the level above.
     *
     * E.g.
     *
     * Level 0: Tile := (Water and Bridge Walls), Bridge := Nothing
     * Level 1: Tile := (Bridge Walking Surface and Walls)
     * Level 2: Nothing
     * Level 3: Nothing
     *
     * After this adjustment,
     *
     * Level 0: Tile := (Previous Level 1), Bridge := (Previous Level 0)
     * Level 1: Nothing
     * Level 2: Nothing
     * Level 3: Nothing.
     */

    grid_tile = NULL;
    bridge_tile = (struct GridTile){ 0 };
    for( int x = 0; x < MAP_TERRAIN_X; x++ )
    {
        for( int y = 0; y < MAP_TERRAIN_Y; y++ )
        {
            floor = &map_terrain->tiles_xyz[MAP_TILE_COORD(x, y, 1)];
            if( (floor->settings & FLOFLAG_BRIDGE) != 0 )
            {
                bridge_tile = scene->grid_tiles[MAP_TILE_COORD(x, y, 0)];
                for( int level = 0; level < MAP_TERRAIN_Z - 1; level++ )
                {
                    scene->grid_tiles[MAP_TILE_COORD(x, y, level)] =
                        scene->grid_tiles[MAP_TILE_COORD(x, y, level + 1)];

                    scene->grid_tiles[MAP_TILE_COORD(x, y, level)].level--;
                }

                // Use the newly unused tile on level 3 as the bridge slot.
                scene->grid_tiles[MAP_TILE_COORD(x, y, 0)].bridge_tile = MAP_TILE_COORD(x, y, 3);

                bridge_tile.level = 3;
                bridge_tile.flags |= GRID_TILE_FLAG_BRIDGE;
                scene->grid_tiles[MAP_TILE_COORD(x, y, 3)] = bridge_tile;
            }
        }
    }

    // Here:
    // Build model lighting
    // 1. Assign and share normals for SceneModels
    // 2. Scene merge normals of abutting locs.
    // 3. Compute lighting

    scene_model = NULL;
    other_model = NULL;
    for( int i = 0; i < scene->models_length; i++ )
    {
        scene_model = &scene->models[i];

        if( scene_model->model == NULL )
            continue;

        struct CacheModel* cache_model = scene_model->model;

        struct ModelNormals* normals =
            model_normals_new(cache_model->vertex_count, cache_model->face_count);

        calculate_vertex_normals(
            normals->lighting_vertex_normals,
            normals->lighting_face_normals,
            cache_model->vertex_count,
            cache_model->face_indices_a,
            cache_model->face_indices_b,
            cache_model->face_indices_c,
            cache_model->vertices_x,
            cache_model->vertices_y,
            cache_model->vertices_z,
            cache_model->face_count);

        scene_model->normals = normals;

        // Make a copy of the normals so sharelight can mutate them.
        if( scene_model->sharelight )
            scene_model->aliased_lighting_normals = model_normals_new_copy(normals);
    }

    struct IterGrid iter_grid = iter_grid_init(0, 0, 0);
    int adjacent_tiles[40] = { 0 };
    struct SceneModel* sharelight_models[40] = { 0 };
    struct SceneModel* adjacent_sharelight_models[40] = { 0 };
    struct GridTile* adjacent_tile = NULL;

    // sharelight
    // for each tile
    //   each walla, wallb, loc[n], ground_decor,        => gather_sharelight_models
    //     each adjacent tile                            => gather_adjacent_tiles
    //       each walla, wallb, loc[n], ground_decor,    => gather_sharelight_models

    while( !iter_grid_done(&iter_grid) )
    {
        grid_tile = &scene->grid_tiles[MAP_TILE_COORD(iter_grid.x, iter_grid.y, iter_grid.level)];

        int sharelight_models_count = gather_sharelight_models(
            sharelight_models,
            sizeof(sharelight_models) / sizeof(sharelight_models[0]),
            grid_tile,
            scene->locs,
            scene->locs_length,
            scene->models,
            scene->models_length);

        for( int i = 0; i < sharelight_models_count; i++ )
        {
            scene_model = sharelight_models[i];
            assert(scene_model->sharelight);

            int adjacent_tiles_count = gather_adjacent_tiles(
                adjacent_tiles,
                sizeof(adjacent_tiles) / sizeof(adjacent_tiles[0]),
                scene->grid_tiles,
                iter_grid.x,
                iter_grid.y,
                iter_grid.level,
                scene_model->_size_x,
                scene_model->_size_y);

            for( int j = 0; j < adjacent_tiles_count; j++ )
            {
                adjacent_tile = &scene->grid_tiles[adjacent_tiles[j]];

                int adjacent_sharelight_models_count = gather_sharelight_models(
                    adjacent_sharelight_models,
                    sizeof(adjacent_sharelight_models) / sizeof(adjacent_sharelight_models[0]),
                    adjacent_tile,
                    scene->locs,
                    scene->locs_length,
                    scene->models,
                    scene->models_length);

                for( int k = 0; k < adjacent_sharelight_models_count; k++ )
                {
                    other_model = adjacent_sharelight_models[k];
                    assert(other_model->sharelight);

                    int check_offset_x =
                        (other_model->_chunk_pos_x - scene_model->_chunk_pos_x) * 128 +
                        (other_model->_size_x - scene_model->_size_x) * 64;
                    int check_offset_y =
                        (other_model->_chunk_pos_y - scene_model->_chunk_pos_y) * 128 +
                        (other_model->_size_y - scene_model->_size_y) * 64;
                    int check_offset_level =
                        other_model->region_height - scene_model->region_height;

                    merge_normals(
                        scene_model->model,
                        scene_model->normals->lighting_vertex_normals,
                        scene_model->aliased_lighting_normals->lighting_vertex_normals,
                        other_model->model,
                        other_model->normals->lighting_vertex_normals,
                        other_model->aliased_lighting_normals->lighting_vertex_normals,
                        check_offset_x,
                        check_offset_level,
                        check_offset_y);
                }
            }
        }

        iter_grid_next(&iter_grid);
    }

    for( int i = 0; i < task->scene->models_length; i++ )
    {
        struct SceneModel* scene_model = &task->scene->models[i];
        if( scene_model->model == NULL )
            continue;

        struct ModelLighting* lighting = model_lighting_new(scene_model->model->face_count);

        scene_model->lighting = lighting;

        int light_ambient = 64;
        int light_attenuation = 768;
        int lightsrc_x = -50;
        int lightsrc_y = -10;
        int lightsrc_z = -50;

        {
            light_ambient += scene_model->light_ambient;
            // 2004Scape multiplies contrast by 5.
            // Later versions do not.
            light_attenuation += scene_model->light_contrast;
        }

        int light_magnitude =
            (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
        int attenuation = (light_attenuation * light_magnitude) >> 8;

        apply_lighting(
            lighting->face_colors_hsl_a,
            lighting->face_colors_hsl_b,
            lighting->face_colors_hsl_c,
            scene_model->aliased_lighting_normals
                ? scene_model->aliased_lighting_normals->lighting_vertex_normals
                : scene_model->normals->lighting_vertex_normals,
            scene_model->normals->lighting_face_normals,
            scene_model->model->face_indices_a,
            scene_model->model->face_indices_b,
            scene_model->model->face_indices_c,
            scene_model->model->face_count,
            scene_model->model->face_colors,
            scene_model->model->face_alphas,
            scene_model->model->face_textures,
            scene_model->model->face_infos,
            light_ambient,
            attenuation,
            lightsrc_x,
            lightsrc_y,
            lightsrc_z);
    }

    task->scene->terrain = task->map_terrain;

    // Free config tables
    // if( task->config_locs_table )
    //     config_locs_table_free(task->config_locs_table);
    // if( task->config_object_table )
    //     config_object_table_free(task->config_object_table);
    // if( task->config_sequence_table )
    //     config_sequence_table_free(task->config_sequence_table);
    // if( task->config_idk_table )
    //     config_idk_table_free(task->config_idk_table);

    // Free shade map is handled in scene struct

done:
    task->step = E_SCENE_LOAD_STEP_DONE;
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
    struct Scene* scene = task->scene;
    task->scene = NULL;
    return scene;
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

    // Clean up tile config data
    configmap_free(task->config_underlay_map);
    configmap_free(task->config_overlay_map);

    // Clean up model arrays
    free(task->loaded_models);
    free(task->model_ids);

    // Clean up queued models array
    free(task->queued_model_ids);

    free(task);
}