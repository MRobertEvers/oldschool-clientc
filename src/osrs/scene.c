#include "scene.h"

#include "filelist.h"
#include "tables/config_locs.h"
#include "tables/configs.h"
#include "tables/maps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TILE_SIZE 128

/**
 * Add Loc To Grid
 * Load Model
 */
// static void
// add_loc()

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
load_loc_models(
    struct SceneModel* scene_loc,
    int* shapes,
    int** models,
    int* lengths,
    int shapes_and_model_count,
    struct Cache* cache,
    struct ModelCache* model_cache,
    int shape_select)
{
    struct CacheModel* model = NULL;
    if( !models )
        return;

    if( !shapes )
    {
        int count = lengths[0];

        scene_loc->models = malloc(sizeof(struct CacheModel) * count);
        memset(scene_loc->models, 0, sizeof(struct CacheModel) * count);
        scene_loc->model_ids = malloc(sizeof(int) * count);
        memset(scene_loc->model_ids, 0, sizeof(int) * count);

        for( int i = 0; i < count; i++ )
        {
            assert(models);
            assert(models[0]);
            int model_id = models[0][i];
            assert(model_id);

            model = model_cache_checkout(model_cache, cache, model_id);
            assert(model);
            scene_loc->models[scene_loc->model_count] = model;
            scene_loc->model_ids[scene_loc->model_count] = model_id;
            scene_loc->model_count++;
        }
    }
    else
    {
        int count = shapes_and_model_count;

        scene_loc->models = malloc(sizeof(struct CacheModel) * count);
        memset(scene_loc->models, 0, sizeof(struct CacheModel) * count);
        scene_loc->model_ids = malloc(sizeof(int) * count);
        memset(scene_loc->model_ids, 0, sizeof(int) * count);

        bool found = false;
        for( int i = 0; i < count; i++ )
        {
            int count_inner = lengths[i];

            int loc_type = shapes[i];
            if( loc_type == shape_select )
            {
                for( int j = 0; j < count_inner; j++ )
                {
                    int model_id = models[i][j];
                    assert(model_id);

                    model = model_cache_checkout(model_cache, cache, model_id);
                    assert(model);
                    scene_loc->models[scene_loc->model_count] = model;
                    scene_loc->model_ids[scene_loc->model_count] = model_id;
                    scene_loc->model_count++;
                    found = true;
                }
            }
        }
        assert(found);
    }
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

    return scene->models_length++;
}

static struct SceneModel*
vec_model_back(struct Scene* scene)
{
    return &scene->models[scene->models_length - 1];
}

const int ROTATION_WALL_TYPE[] = {
    WALL_SIDE_WEST, WALL_SIDE_NORTH, WALL_SIDE_EAST, WALL_SIDE_SOUTH
};
const int ROTATION_WALL_CORNER_TYPE[] = {
    WALL_CORNER_NORTHEAST,
    WALL_CORNER_NORTHWEST,
    WALL_CORNER_SOUTHWEST,
    WALL_CORNER_SOUTHEAST,
};

const int WALL_DECORATION_ROTATION_FORWARD_X[] = { 1, 0, -1, 0 };
const int WALL_DECORATION_ROTATION_FORWARD_Z[] = { 0, -1, 0, 1 };

struct Scene*
scene_new_from_map(struct Cache* cache, int chunk_x, int chunk_y)
{
    struct SceneLocs* scene_locs = NULL;
    struct CacheMapTerrain* map_terrain = NULL;
    struct CacheMapLocs* map_locs = NULL;
    struct SceneTile* scene_tiles = NULL;
    struct GridTile* grid_tile = NULL;
    struct ModelCache* model_cache = model_cache_new();
    struct CacheConfigLocationTable* config_locs_table = NULL;
    struct CacheMapLoc* map = NULL;
    struct Loc* loc = NULL;
    struct CacheMapLocsIter* iter = NULL;
    struct CacheConfigLocation* loc_config = NULL;
    struct SceneModel* model = NULL;

    struct Scene* scene = malloc(sizeof(struct Scene));
    memset(scene, 0, sizeof(struct Scene));

    scene->grid_tiles = malloc(sizeof(struct GridTile) * MAP_TILE_COUNT);
    memset(scene->grid_tiles, 0, sizeof(struct GridTile) * MAP_TILE_COUNT);
    scene->grid_tiles_length = MAP_TILE_COUNT;
    scene->_model_cache = model_cache;

    scene->models = malloc(sizeof(struct SceneModel) * 1024);
    memset(scene->models, 0, sizeof(struct SceneModel) * 1024);
    scene->models_length = 0;
    scene->models_capacity = 1024;

    scene->locs = malloc(sizeof(struct Loc) * 1024);
    memset(scene->locs, 0, sizeof(struct Loc) * 1024);
    scene->locs_length = 0;
    scene->locs_capacity = 1024;

    map_terrain = map_terrain_new_from_cache(cache, chunk_x, chunk_y);
    if( !map_terrain )
    {
        printf("Failed to load map terrain\n");
        goto error;
    }

    // map_locs = map_locs_new_from_cache(cache, chunk_x, chunk_y);
    // if( !map_locs )

    // {
    //     printf("Failed to load map locs\n");
    //     goto error;
    // }

    // TODO: This has to happen before locs,
    // because this calls a fixup on the terrain data.
    // That should be done separately.
    scene_tiles = scene_tiles_new_from_map_terrain_cache(map_terrain, cache);
    if( !scene_tiles )
    {
        printf("Failed to load scene tiles\n");
        goto error;
    }

    scene->scene_tiles = scene_tiles;
    scene->scene_tiles_length = MAP_TILE_COUNT;

    // scene_locs = scene_locs_new_from_map_locs(map_terrain, map_locs, cache, model_cache);
    // if( !scene_locs )
    // {
    //     printf("Failed to load scene locs\n");
    //     goto error;
    // }

    // scene->locs = scene_locs;

    for( int level = 0; level < MAP_TERRAIN_Z; level++ )
    {
        for( int x = 0; x < MAP_TERRAIN_X; x++ )
        {
            for( int y = 0; y < MAP_TERRAIN_Y; y++ )
            {
                grid_tile = &scene->grid_tiles[MAP_TILE_COORD(x, y, level)];
                grid_tile->x = x;
                grid_tile->z = y;
                grid_tile->level = level;

                grid_tile->wall = -1;
                grid_tile->ground_decor = -1;
                grid_tile->wall_decor = -1;
                grid_tile->spans = 0;
            }
        }
    }

    for( int i = 0; i < MAP_TILE_COUNT; i++ )
    {
        struct SceneTile* scene_tile = &scene_tiles[i];
        grid_tile = &scene->grid_tiles[MAP_TILE_COORD(
            scene_tile->chunk_pos_x, scene_tile->chunk_pos_y, scene_tile->chunk_pos_level)];
        grid_tile->tile = scene_tile;
    }

    config_locs_table = config_locs_table_new(cache);
    if( !config_locs_table )
    {
        printf("Failed to load config locs table\n");
        goto error;
    }

    iter = map_locs_iter_new(cache, chunk_x, chunk_y);
    if( !iter )
    {
        printf("Failed to load map locs iter\n");
        goto error;
    }

    while( (map = map_locs_iter_next(iter)) )
    {
        int tile_x = map->chunk_pos_x;
        int tile_y = map->chunk_pos_y;
        int tile_z = map->chunk_pos_level;

        grid_tile = &scene->grid_tiles[MAP_TILE_COORD(tile_x, tile_y, tile_z)];

        int height_sw = map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x, tile_y, tile_z)].height;
        int height_se = height_sw;
        if( tile_x + 1 < MAP_TERRAIN_X )
            height_se = map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x + 1, tile_y, tile_z)].height;

        int height_ne = height_se;
        if( tile_y + 1 < MAP_TERRAIN_Y && tile_x + 1 < MAP_TERRAIN_X )
            height_ne =
                map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x + 1, tile_y + 1, tile_z)].height;

        int height_nw = height_ne;
        if( tile_y + 1 < MAP_TERRAIN_Y )
            height_nw = map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x, tile_y + 1, tile_z)].height;

        int height_center = (height_sw + height_se + height_ne + height_nw) >> 2;

        loc_config = config_locs_table_get(config_locs_table, map->loc_id);
        assert(loc_config);

        switch( map->shape_select )
        {
        case LOC_SHAPE_WALL:
        {
            // Load model
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            load_loc_models(
                model,
                loc_config->shapes,
                loc_config->models,
                loc_config->lengths,
                loc_config->shapes_and_model_count,
                cache,
                model_cache,
                map->shape_select);

            model->region_x = tile_x * TILE_SIZE;
            model->region_y = tile_y * TILE_SIZE;
            model->region_z = height_center;

            model->orientation = map->orientation;
            model->offset_x = loc_config->offset_x;
            model->offset_y = loc_config->offset_y;
            model->offset_height = loc_config->offset_height;

            model->size_x = 1;
            model->size_y = 1;
            model->mirrored = loc_config->rotated;

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);

            loc->size_x = 1;
            loc->size_y = 1;
            loc->chunk_pos_x = tile_x;
            loc->chunk_pos_y = tile_y;
            loc->chunk_pos_level = tile_z;
            loc->type = LOC_TYPE_WALL;

            loc->_wall.model_a = model_index;
            assert(map->orientation >= 0);
            assert(map->orientation < 4);
            loc->_wall.side_a = ROTATION_WALL_TYPE[map->orientation];

            grid_tile->wall = loc_index;
        }
        break;
        case LOC_SHAPE_WALL_TRI_CORNER:
        {
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            load_loc_models(
                model,
                loc_config->shapes,
                loc_config->models,
                loc_config->lengths,
                loc_config->shapes_and_model_count,
                cache,
                model_cache,
                map->shape_select);

            model->region_x = tile_x * TILE_SIZE;
            model->region_y = tile_y * TILE_SIZE;
            model->region_z = height_center;

            model->orientation = map->orientation;
            model->offset_x = loc_config->offset_x;
            model->offset_y = loc_config->offset_y;
            model->offset_height = loc_config->offset_height;

            model->size_x = 1;
            model->size_y = 1;
            model->mirrored = loc_config->rotated;

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);

            loc->size_x = 1;
            loc->size_y = 1;
            loc->chunk_pos_x = tile_x;
            loc->chunk_pos_y = tile_y;
            loc->chunk_pos_level = tile_z;
            loc->type = LOC_TYPE_WALL;

            loc->_wall.model_a = model_index;
            assert(map->orientation >= 0);
            assert(map->orientation < 4);
            loc->_wall.side_a = ROTATION_WALL_CORNER_TYPE[map->orientation];

            grid_tile->wall = loc_index;
        }
        break;
        case LOC_SHAPE_WALL_CORNER:
        {
            int next_orientation = (map->orientation + 1) & 0x3;

            // Load model
            int model_a_index = vec_model_push(scene);
            model = vec_model_back(scene);
            load_loc_models(
                model,
                loc_config->shapes,
                loc_config->models,
                loc_config->lengths,
                loc_config->shapes_and_model_count,
                cache,
                model_cache,
                LOC_SHAPE_WALL_CORNER);

            model->region_x = tile_x * TILE_SIZE;
            model->region_y = tile_y * TILE_SIZE;
            model->region_z = height_center;

            // Add 4 to turn it around.
            model->orientation = map->orientation + 4;
            model->offset_x = loc_config->offset_x;
            model->offset_y = loc_config->offset_y;
            model->offset_height = loc_config->offset_height;

            model->size_x = 1;
            model->size_y = 1;
            model->mirrored = loc_config->rotated;

            int model_b_index = vec_model_push(scene);
            model = vec_model_back(scene);
            load_loc_models(
                model,
                loc_config->shapes,
                loc_config->models,
                loc_config->lengths,
                loc_config->shapes_and_model_count,
                cache,
                model_cache,
                LOC_SHAPE_WALL_CORNER);

            model->region_x = tile_x * TILE_SIZE;
            model->region_y = tile_y * TILE_SIZE;
            model->region_z = height_center;

            model->orientation = next_orientation;
            model->offset_x = loc_config->offset_x;
            model->offset_y = loc_config->offset_y;
            model->offset_height = loc_config->offset_height;

            model->size_x = 1;
            model->size_y = 1;
            model->mirrored = loc_config->rotated;

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);

            loc->size_x = 1;
            loc->size_y = 1;
            loc->chunk_pos_x = tile_x;
            loc->chunk_pos_y = tile_y;
            loc->chunk_pos_level = tile_z;
            loc->type = LOC_TYPE_WALL;

            loc->_wall.model_a = model_a_index;
            loc->_wall.side_a = ROTATION_WALL_TYPE[map->orientation];

            loc->_wall.model_b = model_b_index;
            loc->_wall.side_b = ROTATION_WALL_TYPE[next_orientation];

            grid_tile->wall = loc_index;
        }
        break;
        case LOC_SHAPE_WALL_RECT_CORNER:
        {
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            load_loc_models(
                model,
                loc_config->shapes,
                loc_config->models,
                loc_config->lengths,
                loc_config->shapes_and_model_count,
                cache,
                model_cache,
                map->shape_select);

            model->region_x = tile_x * TILE_SIZE;
            model->region_y = tile_y * TILE_SIZE;
            model->region_z = height_center;

            model->orientation = map->orientation;
            model->offset_x = loc_config->offset_x;
            model->offset_y = loc_config->offset_y;
            model->offset_height = loc_config->offset_height;

            model->size_x = 1;
            model->size_y = 1;
            model->mirrored = loc_config->rotated;

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);

            loc->size_x = 1;
            loc->size_y = 1;
            loc->chunk_pos_x = tile_x;
            loc->chunk_pos_y = tile_y;
            loc->chunk_pos_level = tile_z;
            loc->type = LOC_TYPE_WALL;

            loc->_wall.model_a = model_index;
            assert(map->orientation >= 0);
            assert(map->orientation < 4);
            loc->_wall.side_a = ROTATION_WALL_CORNER_TYPE[map->orientation];

            // switch( map->orientation )
            // {
            // case 0:
            //     loc->_wall.side_a = WALL_CORNER_NORTHWEST;
            //     break;
            // case 1:
            //     loc->_wall.side_a = WALL_CORNER_NORTHEAST;
            //     break;
            // case 2:
            //     loc->_wall.side_a = WALL_CORNER_SOUTHEAST;
            //     break;
            // case 3:
            //     loc->_wall.side_a = WALL_CORNER_SOUTHWEST;
            //     break;
            // }

            assert(model->model_ids[0] != 0);
            grid_tile->wall = loc_index;
        }
        break;
        case LOC_SHAPE_WALL_DECORATION_INSIDE:
        {
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            load_loc_models(
                model,
                loc_config->shapes,
                loc_config->models,
                loc_config->lengths,
                loc_config->shapes_and_model_count,
                cache,
                model_cache,
                LOC_SHAPE_WALL_DECORATION_INSIDE);

            model->region_x = tile_x * TILE_SIZE;
            model->region_y = tile_y * TILE_SIZE;
            model->region_z = height_center;

            model->orientation = map->orientation;
            model->offset_x = loc_config->offset_x;
            model->offset_y = loc_config->offset_y;
            model->offset_height = loc_config->offset_height;

            model->size_x = 1;
            model->size_y = 1;
            model->mirrored = loc_config->rotated;

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);

            loc->size_x = 1;
            loc->size_y = 1;
            loc->chunk_pos_x = tile_x;
            loc->chunk_pos_y = tile_y;
            loc->chunk_pos_level = tile_z;
            loc->type = LOC_TYPE_WALL_DECOR;

            loc->_wall_decor.model = model_index;
            loc->_wall_decor.side = ROTATION_WALL_TYPE[map->orientation];

            grid_tile->wall_decor = loc_index;
        }
        break;
        case LOC_SHAPE_WALL_DECORATION_OUTSIDE:
        {
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            load_loc_models(
                model,
                loc_config->shapes,
                loc_config->models,
                loc_config->lengths,
                loc_config->shapes_and_model_count,
                cache,
                model_cache,
                LOC_SHAPE_WALL_DECORATION_INSIDE);

            model->region_x = tile_x * TILE_SIZE;
            model->region_y = tile_y * TILE_SIZE;
            model->region_z = height_center;

            model->orientation = map->orientation;
            model->offset_x = loc_config->offset_x + 16;
            model->offset_y = loc_config->offset_y + 16;
            model->offset_height = loc_config->offset_height;

            model->size_x = 1;
            model->size_y = 1;
            model->mirrored = loc_config->rotated;

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);

            loc->size_x = 1;
            loc->size_y = 1;
            loc->chunk_pos_x = tile_x;
            loc->chunk_pos_y = tile_y;
            loc->chunk_pos_level = tile_z;
            loc->type = LOC_TYPE_WALL_DECOR;

            loc->_wall_decor.model = model_index;
            loc->_wall_decor.side = ROTATION_WALL_TYPE[map->orientation];

            grid_tile->wall_decor = loc_index;
        }
        break;
        case LOC_SHAPE_WALL_DIAGONAL:
        {
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            load_loc_models(
                model,
                loc_config->shapes,
                loc_config->models,
                loc_config->lengths,
                loc_config->shapes_and_model_count,
                cache,
                model_cache,
                map->shape_select);

            model->region_x = tile_x * TILE_SIZE;
            model->region_y = tile_y * TILE_SIZE;
            model->region_z = height_center;

            model->orientation = map->orientation;
            model->offset_x = loc_config->offset_x;
            model->offset_y = loc_config->offset_y;
            model->offset_height = loc_config->offset_height;

            model->size_x = 1;
            model->size_y = 1;
            model->mirrored = loc_config->rotated;

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);

            loc->size_x = 1;
            loc->size_y = 1;
            loc->chunk_pos_x = tile_x;
            loc->chunk_pos_y = tile_y;
            loc->chunk_pos_level = tile_z;
            loc->type = LOC_TYPE_WALL;

            loc->_wall.model_a = model_index;
            assert(map->orientation >= 0);
            assert(map->orientation < 4);
            // loc->_wall.side_a = ROTATION_WALL_TYPE[map->orientation];

            switch( map->orientation )
            {
            case 0:
                loc->_wall.side_a = WALL_CORNER_NORTHWEST;
                break;
            case 1:
                loc->_wall.side_a = WALL_CORNER_NORTHEAST;
                break;
            case 2:
                loc->_wall.side_a = WALL_CORNER_SOUTHEAST;
                break;
            case 3:
                loc->_wall.side_a = WALL_CORNER_SOUTHWEST;
                break;
            }

            assert(model->model_ids[0] != 0);
            grid_tile->wall = loc_index;
        }
        break;
        case LOC_SHAPE_NORMAL:
        case LOC_SHAPE_NORMAL_DIAGIONAL:
        {
            // Load model
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            load_loc_models(
                model,
                loc_config->shapes,
                loc_config->models,
                loc_config->lengths,
                loc_config->shapes_and_model_count,
                cache,
                model_cache,
                map->shape_select);

            model->region_x = tile_x * TILE_SIZE;
            model->region_y = tile_y * TILE_SIZE;
            model->region_z = height_center;

            model->orientation = map->orientation;
            model->offset_x = loc_config->offset_x;
            model->offset_y = loc_config->offset_y;
            model->offset_height = loc_config->offset_height;

            int size_x = loc_config->size_x;
            int size_y = loc_config->size_y;
            if( map->orientation == 1 || map->orientation == 3 )
            {
                int temp = size_x;
                size_x = size_y;
                size_y = temp;
            }

            model->size_x = size_x;
            model->size_y = size_y;
            model->mirrored = loc_config->rotated;

            // Add the loc

            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
            loc->size_x = size_x;
            loc->size_y = size_y;
            loc->chunk_pos_x = tile_x;
            loc->chunk_pos_y = tile_y;
            loc->chunk_pos_level = tile_z;
            loc->type = LOC_TYPE_SCENERY;

            loc->_scenery.model = model_index;

            // Compute spans
            compute_normal_scenery_spans(
                scene->grid_tiles, tile_x, tile_y, tile_z, size_x, size_y, loc_index);
        }
        break;
        case LOC_SHAPE_FLOOR_DECORATION:
        {
            // Load model
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            load_loc_models(
                model,
                loc_config->shapes,
                loc_config->models,
                loc_config->lengths,
                loc_config->shapes_and_model_count,
                cache,
                model_cache,
                map->shape_select);

            model->region_x = tile_x * TILE_SIZE;
            model->region_y = tile_y * TILE_SIZE;
            model->region_z = height_center;

            model->orientation = map->orientation;
            model->offset_x = loc_config->offset_x;
            model->offset_y = loc_config->offset_y;
            model->offset_height = loc_config->offset_height;

            model->size_x = 1;
            model->size_y = 1;
            model->mirrored = loc_config->rotated;

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);

            loc->size_x = 1;
            loc->size_y = 1;
            loc->chunk_pos_x = tile_x;
            loc->chunk_pos_y = tile_y;
            loc->chunk_pos_level = tile_z;
            loc->type = LOC_TYPE_GROUND_DECOR;

            loc->_ground_decor.model = model_index;

            grid_tile->ground_decor = loc_index;
        }
        break;
        default:
        {
            printf("Unknown loc shape: %d\n", map->shape_select);
        }
        }
    }
    map_locs_iter_free(iter);

    map_terrain_free(map_terrain);

    config_locs_table_free(config_locs_table);
    // map_locs_free(map_locs);

    return scene;

error:
    // scene_locs_free(scene_locs);
    // scene_free(scene);
    return NULL;
}

void
scene_free(struct Scene* scene)
{
    model_cache_free(scene->_model_cache);
    free_tiles(scene->scene_tiles, scene->scene_tiles_length);

    for( int i = 0; i < scene->models_length; i++ )
    {
        struct SceneModel* model = &scene->models[i];
        free(model->model_ids);
        free(model->models);
    }

    free(scene->models);
    free(scene->locs);
    free(scene->grid_tiles);
    free(scene);
}
