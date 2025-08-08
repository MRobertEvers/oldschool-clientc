#include "scene.h"

#include "filelist.h"
#include "game_model.h"
#include "tables/config_locs.h"
#include "tables/configs.h"
#include "tables/maps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TILE_SIZE 128

static void
init_wall_default(struct Wall* wall)
{
    memset(wall, 0, sizeof(struct Wall));
    wall->model_a = -1;
    wall->model_b = -1;
    // wall->side_a = 0;
    // wall->side_b = 0;
}

static void
init_wall_decor_default(struct WallDecor* wall_decor)
{
    memset(wall_decor, 0, sizeof(struct WallDecor));
    wall_decor->model_a = -1;
    wall_decor->model_b = -1;
    // wall_decor->side = 0;
    // wall_decor->through_wall_flags = 0;
}

static void
init_scene_model_wxh(
    struct SceneModel* model, int tile_x, int tile_y, int height_center, int size_x, int size_y)
{
    model->region_x = tile_x * TILE_SIZE + size_x * 64;
    model->region_y = tile_y * TILE_SIZE + size_y * 64;
    model->region_z = height_center;

    model->_size_x = size_x;
    model->_size_y = size_y;
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

static const int WALL_DECOR_ROTATION_OFFSET_X[] = { 1, 0, -1, 0 };
static const int WALL_DECOR_ROTATION_OFFSET_Z[] = { 0, -1, 0, 1 };
static const int WALL_DECOR_ROTATION_DIAGONAL_OFFSET_X[] = { 1, -1, -1, 1 };
static const int WALL_DECOR_ROTATION_DIAGONAL_OFFSET_Z[] = { -1, -1, 1, 1 };

const int ROTATION_WALL_TYPE[] = {
    WALL_SIDE_WEST, WALL_SIDE_NORTH, WALL_SIDE_EAST, WALL_SIDE_SOUTH
};
const int ROTATION_WALL_CORNER_TYPE[] = {
    WALL_CORNER_NORTHWEST,
    WALL_CORNER_NORTHEAST,
    WALL_CORNER_SOUTHEAST,
    WALL_CORNER_SOUTHWEST,
};

/**
 * This is a configured offset for a loc, then there may be additional
 * offsets applied by other locs on the tile.
 *
 * For example, walls will offset the decor locs.
 *
 * @param decor
 * @param orientation
 * @param offset
 */
static void
calculate_wall_decor_offset(struct SceneModel* decor, int orientation, int offset, bool diagonal)
{
    assert(orientation >= 0);
    assert(orientation < 4);

    int x_multiplier = diagonal ? WALL_DECOR_ROTATION_DIAGONAL_OFFSET_X[orientation]
                                : WALL_DECOR_ROTATION_OFFSET_X[orientation];
    int y_multiplier = diagonal ? WALL_DECOR_ROTATION_DIAGONAL_OFFSET_Z[orientation]
                                : WALL_DECOR_ROTATION_OFFSET_Z[orientation];
    int offset_x = offset * x_multiplier;
    int offset_y = offset * y_multiplier;

    decor->offset_x = offset_x;
    decor->offset_y = offset_y;
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

/**
 * Transforms must be applied here so that hillskew is correctly applied.
 * (As opposed to passing in rendering offsets/other params.)
 */
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

    bool mirrored = (loc->mirrored ^ (orientation > 3)) != 0;
    bool oriented = orientation != 0;
    bool scaled = loc->resize_x != 128 || loc->resize_y != 128 || loc->resize_z != 128;
    bool translated = loc->offset_x != 0 || loc->offset_y != 0 || loc->offset_height != 0;
    // TODO: handle the other contoured ground types.
    bool hillskewed = loc->contour_ground_type == 1;

    if( mirrored )
        model_transform_mirror(model);

    if( oriented )
        model_transform_orient(model, orientation);

    if( scaled )
        model_transform_scale(model, loc->resize_x, loc->resize_y, loc->resize_z);

    if( translated )
        model_transform_translate(model, loc->offset_x, loc->offset_y, loc->offset_height);

    if( hillskewed )
        model_transform_hillskew(model, sw_height, se_height, ne_height, nw_height);
}

static void
loc_load_model(
    struct SceneModel* scene_loc,
    struct CacheConfigLocation* loc_config,
    struct Cache* cache,
    struct ModelCache* model_cache,
    int shape_select,
    int orientation,
    int sw_height,
    int se_height,
    int ne_height,
    int nw_height)
{
    int* shapes = loc_config->shapes;
    int** model_id_sets = loc_config->models;
    int* lengths = loc_config->lengths;
    int shapes_and_model_count = loc_config->shapes_and_model_count;

    struct CacheModel** models = NULL;
    int model_count = 0;
    int* model_ids = NULL;

    struct CacheModel* model = NULL;

    if( !model_id_sets )
        return;

    if( !shapes )
    {
        int count = lengths[0];

        models = malloc(sizeof(struct CacheModel) * count);
        memset(models, 0, sizeof(struct CacheModel) * count);
        model_ids = malloc(sizeof(int) * count);
        memset(model_ids, 0, sizeof(int) * count);

        for( int i = 0; i < count; i++ )
        {
            int model_id = model_id_sets[0][i];
            assert(model_id);

            model = model_cache_checkout(model_cache, cache, model_id);
            assert(model);
            models[model_count] = model;
            model_ids[model_count] = model_id;
            model_count++;
        }
    }
    else
    {
        int count = shapes_and_model_count;

        models = malloc(sizeof(struct CacheModel*) * count);
        memset(models, 0, sizeof(struct CacheModel*) * count);
        model_ids = malloc(sizeof(int) * count);
        memset(model_ids, 0, sizeof(int) * count);

        bool found = false;
        for( int i = 0; i < count; i++ )
        {
            int count_inner = lengths[i];

            int loc_type = shapes[i];
            if( loc_type == shape_select )
            {
                assert(count_inner <= count);
                for( int j = 0; j < count_inner; j++ )
                {
                    int model_id = model_id_sets[i][j];
                    assert(model_id);

                    model = model_cache_checkout(model_cache, cache, model_id);
                    assert(model);
                    models[model_count] = model;
                    model_ids[model_count] = model_id;
                    model_count++;
                    found = true;
                }
            }
        }
        assert(found);
    }

    assert(model_count > 0);

    if( model_count > 1 )
    {
        model = model_new_merge(models, model_count);
    }
    else
    {
        model = model_new_copy(models[0]);
    }

    loc_apply_transforms(
        loc_config, model, orientation, sw_height, se_height, ne_height, nw_height);

    scene_loc->model = model;
    scene_loc->model_id = model_ids[0];

    scene_loc->light_ambient = loc_config->ambient;
    scene_loc->light_contrast = loc_config->contrast;
    scene_loc->sharelight = loc_config->sharelight;

    scene_loc->__loc_id = loc_config->_id;

    free(models);
    free(model_ids);
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

    scene->models[scene->models_length].light_ambient = -1;
    scene->models[scene->models_length].light_contrast = -1;

    return scene->models_length++;
}

static struct SceneModel*
vec_model_back(struct Scene* scene)
{
    return &scene->models[scene->models_length - 1];
}

static struct SceneModel*
tile_loc_model_nullable(struct SceneModel* models, struct Loc* loc)
{
    if( loc->type == LOC_TYPE_SCENERY )
    {
        assert(loc->_scenery.model >= 0);
        return &models[loc->_scenery.model];
    }

    return NULL;
}

static struct SceneModel*
tile_wall_model_nullable(struct SceneModel* models, struct Loc* loc, int a)
{
    if( loc->type == LOC_TYPE_WALL )
    {
        if( a )
            return &models[loc->_wall.model_a];
        else if( loc->_wall.model_b >= 0 )
            return &models[loc->_wall.model_b];
        else
            return NULL;
    }

    return NULL;
}

static void
merge_normals(
    struct CacheModel* model,
    struct LightingNormal* vertex_normals,
    struct LightingNormal* lighting_vertex_normals,
    struct CacheModel* other_model,
    struct LightingNormal* other_vertex_normals,
    struct LightingNormal* other_lighting_vertex_normals,
    int check_offset_x,
    int check_offset_y,
    int check_offset_z,
    int tile_x,
    int tile_y,
    int tile_level,
    int other_tile_x,
    int other_tile_y,
    int other_tile_level)
{
    struct LightingNormal* model_a_normal = NULL;
    struct LightingNormal* model_b_normal = NULL;
    struct LightingNormal* model_a_lighting_normal = NULL;
    struct LightingNormal* model_b_lighting_normal = NULL;
    int x, y, z;
    int other_x, other_y, other_z;

    for( int vertex = 0; vertex < model->vertex_count; vertex++ )
    {
        x = model->vertices_x[vertex] - check_offset_x;
        y = model->vertices_y[vertex] - check_offset_y;
        z = model->vertices_z[vertex] - check_offset_z;

        model_a_normal = &vertex_normals[vertex];
        model_a_lighting_normal = &lighting_vertex_normals[vertex];

        for( int other_vertex = 0; other_vertex < other_model->vertex_count; other_vertex++ )
        {
            other_x = other_model->vertices_x[other_vertex];
            other_y = other_model->vertices_y[other_vertex];
            other_z = other_model->vertices_z[other_vertex];

            model_b_normal = &other_vertex_normals[other_vertex];
            model_b_lighting_normal = &other_lighting_vertex_normals[other_vertex];

            if( x == other_x && y == other_y && z == other_z && model_b_normal->face_count > 0 &&
                model_a_normal->face_count > 0 )
            {
                printf(
                    "merging normals (%d, %d, %d) and (%d, %d, %d)\n",
                    tile_x,
                    tile_y,
                    tile_level,
                    other_tile_x,
                    other_tile_y,
                    other_tile_level);
                model_a_lighting_normal->x += model_b_normal->x;
                model_a_lighting_normal->y += model_b_normal->y;
                model_a_lighting_normal->z += model_b_normal->z;
                model_a_lighting_normal->face_count += model_b_normal->face_count;
                model_a_lighting_normal->merged++;

                model_b_lighting_normal->x += model_a_normal->x;
                model_b_lighting_normal->y += model_a_normal->y;
                model_b_lighting_normal->z += model_a_normal->z;
                model_b_lighting_normal->face_count += model_a_normal->face_count;
                model_b_lighting_normal->merged++;
            }
        }
    }
}

struct IterGrid
{
    int x;
    int y;
    int level;

    int max_x;
    int min_x;
    int max_y;
    int min_y;
    int max_level;
    int min_level;
};

static struct IterGrid
iter_grid_init2(int x, int max_x, int y, int max_y, int level, int max_level)
{
    struct IterGrid iter_grid;
    iter_grid.x = x;
    iter_grid.y = y;
    iter_grid.level = level;
    iter_grid.max_x = max_x;
    iter_grid.min_x = x;
    iter_grid.max_y = max_y;
    iter_grid.min_y = y;
    iter_grid.max_level = max_level;
    iter_grid.min_level = level;
    return iter_grid;
}

static struct IterGrid
iter_grid_init(int x, int y, int z)
{
    return iter_grid_init2(x, MAP_TERRAIN_X, y, MAP_TERRAIN_Y, z, MAP_TERRAIN_Z);
}

static void
iter_grid_next(struct IterGrid* iter_grid)
{
    iter_grid->x++;
    if( iter_grid->x >= iter_grid->max_x )
    {
        iter_grid->x = iter_grid->min_x;
        iter_grid->y++;
    }

    if( iter_grid->y >= iter_grid->max_y )
    {
        iter_grid->y = iter_grid->min_y;
        iter_grid->level++;
    }
}

static bool
iter_grid_done(struct IterGrid* iter_grid)
{
    return iter_grid->level >= iter_grid->max_level;
}

static int
gather_adjacent_tiles(
    int* out,
    int out_size,
    struct GridTile* grid,
    int tile_x,
    int tile_y,
    int tile_level,
    int tile_size_x,
    int tile_size_y)
{
    int min_tile_x = tile_x;
    int max_tile_x = tile_x + tile_size_x;
    int min_tile_y = tile_y - 1;
    int max_tile_y = tile_y + tile_size_y;

    int count = 0;
    for( int level = tile_level; level <= tile_level + 1; level++ )
    {
        for( int x = min_tile_x; x <= max_tile_x; x++ )
        {
            for( int y = min_tile_y; y <= max_tile_y; y++ )
            {
                if( (x == tile_x && y == tile_y && level == tile_level) )
                    continue;
                if( x < 0 || y < 0 || x >= MAP_TERRAIN_X || y >= MAP_TERRAIN_Y || level < 0 ||
                    level >= MAP_TERRAIN_Z )
                    continue;

                out[count++] = MAP_TILE_COORD(x, y, level);
            }
        }
    }

    return count;
}

struct Scene*
scene_new_from_map(struct Cache* cache, int chunk_x, int chunk_y)
{
    struct SceneLocs* scene_locs = NULL;
    struct CacheMapTerrain* map_terrain = NULL;
    struct CacheMapLocs* map_locs = NULL;
    struct SceneTile* scene_tiles = NULL;
    struct GridTile* grid_tile = NULL;
    struct GridTile* adjacent_tile = NULL;
    struct ModelCache* model_cache = model_cache_new();
    struct CacheConfigLocationTable* config_locs_table = NULL;
    struct CacheMapLoc* map = NULL;
    struct Loc* loc = NULL;
    struct Loc* other_loc = NULL;
    struct SceneModel* model = NULL;
    struct SceneModel* other_model = NULL;
    struct CacheMapLocsIter* iter = NULL;
    struct CacheConfigLocation* loc_config = NULL;

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

    int* shade_map = malloc(sizeof(int) * MAP_TILE_COUNT);
    memset(shade_map, 0, sizeof(int) * MAP_TILE_COUNT);
    scene->_shade_map = shade_map;
    scene->_shade_map_length = MAP_TILE_COUNT;

    map_terrain = map_terrain_new_from_cache(cache, chunk_x, chunk_y);
    if( !map_terrain )
    {
        printf("Failed to load map terrain\n");
        goto error;
    }

    scene->scene_tiles = scene_tiles;
    scene->scene_tiles_length = MAP_TILE_COUNT;

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
                grid_tile->spans = 0;
                grid_tile->sharelight = 0;

                grid_tile->wall = -1;
                grid_tile->ground_decor = -1;
                grid_tile->wall_decor = -1;
                grid_tile->bridge_tile = -1;
            }
        }
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
        model = NULL;
        other_model = NULL;

        int tile_x = map->chunk_pos_x;
        int tile_y = map->chunk_pos_y;
        int tile_z = map->chunk_pos_level;

        grid_tile = &scene->grid_tiles[MAP_TILE_COORD(tile_x, tile_y, tile_z)];

        int height_sw = map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x, tile_y, tile_z)].height;
        int height_se = height_sw;
        if( tile_x + 1 < MAP_TERRAIN_X )
            height_se = map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x + 1, tile_y, tile_z)].height;

        int height_ne = height_sw;
        if( tile_y + 1 < MAP_TERRAIN_Y && tile_x + 1 < MAP_TERRAIN_X )
            height_ne =
                map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x + 1, tile_y + 1, tile_z)].height;

        int height_nw = height_sw;
        if( tile_y + 1 < MAP_TERRAIN_Y )
            height_nw = map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x, tile_y + 1, tile_z)].height;

        int height_center = (height_sw + height_se + height_ne + height_nw) >> 2;

        loc_config = config_locs_table_get(config_locs_table, map->loc_id);
        assert(loc_config);

        // grid_tile->sharelight = loc_config->sharelight;

        switch( map->shape_select )
        {
        case LOC_SHAPE_WALL_SINGLE_SIDE:
        {
            // Load model
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
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
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
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
                    // TODO: Fix bounds
                    if( tile_y < MAP_TERRAIN_Y - 1 )
                        shade_map[MAP_TILE_COORD(tile_x, tile_y + 1, tile_z)] = 50;
                }
                break;
                case 1:
                {
                    if( tile_x < MAP_TERRAIN_X - 1 && tile_y < MAP_TERRAIN_Y - 1 )
                        shade_map[MAP_TILE_COORD(tile_x + 1, tile_y + 1, tile_z)] = 50;
                }
                break;
                case 2:
                {
                    if( tile_x < MAP_TERRAIN_X - 1 )
                        shade_map[MAP_TILE_COORD(tile_x + 1, tile_y, tile_z)] = 50;
                }
                break;
                case 3:
                {
                    shade_map[MAP_TILE_COORD(tile_x, tile_y, tile_z)] = 50;
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

            // Load model
            int model_a_index = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                LOC_SHAPE_WALL_TWO_SIDES,
                map->orientation + 4,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            int model_b_index = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                LOC_SHAPE_WALL_TWO_SIDES,
                next_orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL;
            init_wall_default(&loc->_wall);

            loc->_wall.model_a = model_a_index;
            loc->_wall.side_a = ROTATION_WALL_TYPE[map->orientation];

            loc->_wall.model_b = model_b_index;
            loc->_wall.side_b = ROTATION_WALL_TYPE[next_orientation];

            int wall_width = loc_config->wall_width;
            loc->_wall.wall_width = wall_width;

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
        }
        break;
        case LOC_SHAPE_WALL_RECT_CORNER:
        {
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL;
            init_wall_default(&loc->_wall);

            loc->_wall.model_a = model_index;
            assert(map->orientation >= 0);
            assert(map->orientation < 4);

            int orientation = map->orientation;
            loc->_wall.side_a = ROTATION_WALL_CORNER_TYPE[orientation];

            assert(model->model_id != 0);
            grid_tile->wall = loc_index;

            if( loc_config->shadowed )
            {
                switch( orientation )
                {
                case 0:
                {
                    // TODO: Fix bounds
                    if( tile_y < MAP_TERRAIN_Y - 1 )
                        shade_map[MAP_TILE_COORD(tile_x, tile_y + 1, tile_z)] = 50;
                }
                break;
                case 1:
                {
                    if( tile_x < MAP_TERRAIN_X - 1 && tile_y < MAP_TERRAIN_Y - 1 )
                        shade_map[MAP_TILE_COORD(tile_x + 1, tile_y + 1, tile_z)] = 50;
                }
                break;
                case 2:
                {
                    if( tile_x < MAP_TERRAIN_X - 1 )
                        shade_map[MAP_TILE_COORD(tile_x + 1, tile_y, tile_z)] = 50;
                }
                break;
                case 3:
                {
                    shade_map[MAP_TILE_COORD(tile_x, tile_y, tile_z)] = 50;
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
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
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
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            // Default offset is 16.
            int offset = 16;
            // The wall was loaded first, get the offset and use it.
            if( grid_tile->wall != -1 )
            {
                other_loc = &scene->locs[grid_tile->wall];
                assert(other_loc->type == LOC_TYPE_WALL);
                if( other_loc->_wall.wall_width )
                    offset = other_loc->_wall.wall_width;
            }

            calculate_wall_decor_offset(
                model, map->orientation, offset, false // diagonal
            );

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
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
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                map->orientation + 4,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            // TODO: Get this from the wall offset??
            // This needs to be taken from the wall offset.
            // Lumbridge walls are 16 thick.
            // Walls in al kharid are 8 thick.
            int offset = -45;
            calculate_wall_decor_offset(
                model, map->orientation, offset, true // diagonal
            );

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL_DECOR;
            init_wall_decor_default(&loc->_wall_decor);

            loc->_wall_decor.model_a = model_index;
            assert(map->orientation >= 0);
            assert(map->orientation < 4);
            loc->_wall_decor.side = ROTATION_WALL_CORNER_TYPE[map->orientation];
            loc->_wall_decor.through_wall_flags = THROUGHWALL;

            grid_tile->wall_decor = loc_index;
        }
        break;
        case LOC_SHAPE_WALL_DECOR_DIAGONAL_NOOFFSET:
        {
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                map->orientation + 4,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            // int offset = 8;
            // calculate_wall_decor_offset(
            //     model, map->orientation, offset, true // diagonal
            // );

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL_DECOR;
            init_wall_decor_default(&loc->_wall_decor);

            loc->_wall_decor.model_a = model_index;
            assert(map->orientation >= 0);
            assert(map->orientation < 4);
            loc->_wall_decor.side = ROTATION_WALL_CORNER_TYPE[map->orientation];
            loc->_wall_decor.through_wall_flags = THROUGHWALL;

            grid_tile->wall_decor = loc_index;
        }
        break;
        case LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE:
        {
            int outside_orientation = map->orientation;
            int inside_orientation = (map->orientation + 2) & 0x3;

            int model_index_a = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                outside_orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            // TODO: Get this from the wall offset??
            // This needs to be taken from the wall offset.
            // Lumbridge walls are 16 thick.
            // Walls in al kharid are 8 thick.
            int offset = -45;
            calculate_wall_decor_offset(
                model, outside_orientation, offset, true // diagonal
            );

            int model_index_b = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                inside_orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            // TODO: Get this from the wall offset??
            offset = -53;
            calculate_wall_decor_offset(
                model, inside_orientation, offset, true // diagonal
            );

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_WALL_DECOR;
            init_wall_decor_default(&loc->_wall_decor);

            loc->_wall_decor.model_a = model_index_a;
            loc->_wall_decor.model_b = model_index_b;
            assert(outside_orientation >= 0);
            assert(outside_orientation < 4);
            loc->_wall_decor.side = ROTATION_WALL_CORNER_TYPE[outside_orientation];
            loc->_wall_decor.through_wall_flags = THROUGHWALL;

            grid_tile->wall_decor = loc_index;
        }
        break;
        case LOC_SHAPE_WALL_DIAGONAL:
        {
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
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
            if( tile_z == 3 )
            {
                int iiii = 0;
            }
            // Load model
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                LOC_SHAPE_SCENERY,
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

            init_scene_model_wxh(model, tile_x, tile_y, height_center, size_x, size_y);

            // Add the loc

            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->size_x = size_x;
            loc->size_y = size_y;
            loc->type = LOC_TYPE_SCENERY;

            loc->_scenery.model = model_index;

            // Compute spans
            compute_normal_scenery_spans(
                scene->grid_tiles, tile_x, tile_y, tile_z, size_x, size_y, loc_index);

            if( loc_config->shadowed )
            {
                for( int x = 0; x < size_x; x++ )
                {
                    for( int y = 0; y < size_y; y++ )
                    {
                        int shade = size_x * size_y;

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
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

            loc->type = LOC_TYPE_SCENERY;

            loc->_scenery.model = model_index;

            grid_tile->locs[grid_tile->locs_length++] = loc_index;
        }
        break;
        case LOC_SHAPE_FLOOR_DECORATION:
        {
            // Load model
            int model_index = vec_model_push(scene);
            model = vec_model_back(scene);
            loc_load_model(
                model,
                loc_config,
                cache,
                model_cache,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(model, tile_x, tile_y, height_center);

            // Add the loc
            int loc_index = vec_loc_push(scene);
            loc = vec_loc_back(scene);
            init_loc_1x1(loc, tile_x, tile_y, tile_z);

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

    // This must happen after loading of locs because locs influence the lightness.
    scene_tiles = scene_tiles_new_from_map_terrain_cache(map_terrain, shade_map, cache);
    if( !scene_tiles )
    {
        printf("Failed to load scene tiles\n");
        goto error;
    }

    for( int i = 0; i < MAP_TILE_COUNT; i++ )
    {
        struct SceneTile* scene_tile = &scene_tiles[i];
        grid_tile = &scene->grid_tiles[MAP_TILE_COORD(
            scene_tile->chunk_pos_x, scene_tile->chunk_pos_y, scene_tile->chunk_pos_level)];
        grid_tile->tile = scene_tile;
    }

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
    struct CacheMapFloor* floor = NULL;
    struct GridTile bridge_tile = { 0 };
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

    for( int i = 0; i < scene->models_length; i++ )
    {
        struct SceneModel* model = &scene->models[i];

        if( model->model == NULL )
            continue;

        struct CacheModel* cache_model = model->model;

        struct ModelNormals* normals = malloc(sizeof(struct ModelNormals));
        memset(normals, 0, sizeof(struct ModelNormals));

        normals->lighting_vertex_normals =
            malloc(sizeof(struct LightingNormal) * cache_model->vertex_count);
        memset(
            normals->lighting_vertex_normals,
            0,
            sizeof(struct LightingNormal) * cache_model->vertex_count);
        normals->lighting_face_normals =
            malloc(sizeof(struct LightingNormal) * cache_model->face_count);
        memset(
            normals->lighting_face_normals,
            0,
            sizeof(struct LightingNormal) * cache_model->face_count);

        normals->lighting_vertex_normals_count = cache_model->vertex_count;
        normals->lighting_face_normals_count = cache_model->face_count;

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

        model->normals = normals;

        if( model->sharelight )
        {
            struct ModelNormals* aliased_normals = malloc(sizeof(struct ModelNormals));
            memset(aliased_normals, 0, sizeof(struct ModelNormals));

            aliased_normals->lighting_vertex_normals =
                malloc(sizeof(struct LightingNormal) * cache_model->vertex_count);
            memcpy(
                aliased_normals->lighting_vertex_normals,
                normals->lighting_vertex_normals,
                sizeof(struct LightingNormal) * cache_model->vertex_count);

            aliased_normals->lighting_face_normals =
                malloc(sizeof(struct LightingNormal) * cache_model->face_count);
            memcpy(
                aliased_normals->lighting_face_normals,
                normals->lighting_face_normals,
                sizeof(struct LightingNormal) * cache_model->face_count);

            model->aliased_lighting_normals = aliased_normals;
        }
    }

    struct IterGrid iter_grid = iter_grid_init(0, 0, 0);
    struct IterGrid iter_adjacent = { 0 };
    int adjacent_tiles[40] = { 0 };

    while( !iter_grid_done(&iter_grid) )
    {
        grid_tile = &scene->grid_tiles[MAP_TILE_COORD(iter_grid.x, iter_grid.y, iter_grid.level)];

        if( iter_grid.x == 41 && iter_grid.y == 11 )
        {
            int lkdjf = 0;
        }
        for( int i = 0; i < grid_tile->locs_length; i++ )
        {
            loc = &scene->locs[grid_tile->locs[i]];

            model = tile_loc_model_nullable(scene->models, loc);
            if( model == NULL || !model->sharelight )
                continue;

            int adjacent_tiles_count = gather_adjacent_tiles(
                adjacent_tiles,
                sizeof(adjacent_tiles) / sizeof(adjacent_tiles[0]),
                scene->grid_tiles,
                iter_grid.x,
                iter_grid.y,
                iter_grid.level,
                loc->size_x,
                loc->size_y);

            for( int j = 0; j < adjacent_tiles_count; j++ )
            {
                adjacent_tile = &scene->grid_tiles[adjacent_tiles[j]];

                if( adjacent_tile->wall != -1 )
                {
                    other_loc = &scene->locs[adjacent_tile->wall];
                    other_model = tile_wall_model_nullable(scene->models, other_loc, 1);
                    if( other_model && other_model->sharelight )
                    {
                        int other_min_tile_x = other_loc->chunk_pos_x;
                        int other_min_tile_y = other_loc->chunk_pos_y;

                        int check_offset_x =
                            (other_min_tile_x - iter_grid.x) * 128 + (1 - loc->size_x) * 64;
                        int check_offset_y =
                            (other_min_tile_y - iter_grid.y) * 128 + (1 - loc->size_y) * 64;
                        int check_offset_level =
                            (map_terrain
                                 ->tiles_xyz[MAP_TILE_COORD(
                                     other_loc->chunk_pos_x,
                                     other_loc->chunk_pos_y,
                                     other_loc->chunk_pos_level)]
                                 .height -
                             map_terrain
                                 ->tiles_xyz[MAP_TILE_COORD(
                                     iter_grid.x, iter_grid.y, iter_grid.level)]
                                 .height);
                        merge_normals(
                            model->model,
                            model->normals->lighting_vertex_normals,
                            model->aliased_lighting_normals->lighting_vertex_normals,
                            other_model->model,
                            other_model->normals->lighting_vertex_normals,
                            other_model->aliased_lighting_normals->lighting_vertex_normals,
                            check_offset_x,
                            check_offset_level,
                            check_offset_y,
                            iter_grid.x,
                            iter_grid.y,
                            iter_grid.level,
                            other_loc->chunk_pos_x,
                            other_loc->chunk_pos_y,
                            iter_grid.level);
                    }

                    other_model = tile_wall_model_nullable(scene->models, other_loc, 0);
                    if( other_model && other_model->sharelight )
                    {
                        int other_min_tile_x = other_loc->chunk_pos_x;
                        int other_min_tile_y = other_loc->chunk_pos_y;

                        int check_offset_x =
                            (other_min_tile_x - iter_grid.x) * 128 + (1 - loc->size_x) * 64;
                        int check_offset_y =
                            (other_min_tile_y - iter_grid.y) * 128 + (1 - loc->size_y) * 64;
                        int check_offset_level =
                            (map_terrain
                                 ->tiles_xyz[MAP_TILE_COORD(
                                     other_loc->chunk_pos_x,
                                     other_loc->chunk_pos_y,
                                     other_loc->chunk_pos_level)]
                                 .height -
                             map_terrain
                                 ->tiles_xyz[MAP_TILE_COORD(
                                     iter_grid.x, iter_grid.y, iter_grid.level)]
                                 .height);
                        merge_normals(
                            model->model,
                            model->normals->lighting_vertex_normals,
                            model->aliased_lighting_normals->lighting_vertex_normals,
                            other_model->model,
                            other_model->normals->lighting_vertex_normals,
                            other_model->aliased_lighting_normals->lighting_vertex_normals,
                            check_offset_x,
                            check_offset_level,
                            check_offset_y,
                            iter_grid.x,
                            iter_grid.y,
                            iter_grid.level,
                            other_loc->chunk_pos_x,
                            other_loc->chunk_pos_y,
                            iter_grid.level);
                    }
                }
                for( int k = 0; k < adjacent_tile->locs_length; k++ )
                {
                    other_loc = &scene->locs[adjacent_tile->locs[k]];

                    if( loc->chunk_pos_x == other_loc->chunk_pos_x &&
                        loc->chunk_pos_y == other_loc->chunk_pos_y )
                        continue;

                    other_model = tile_loc_model_nullable(scene->models, other_loc);
                    if( other_model && other_model->sharelight )
                    {
                        int other_min_tile_x = other_loc->chunk_pos_x;
                        int other_min_tile_y = other_loc->chunk_pos_y;
                        int other_max_tile_x = other_min_tile_x + other_loc->size_x - 1;
                        int other_max_tile_y = other_min_tile_y + other_loc->size_y - 1;

                        int check_offset_x = (other_min_tile_x - iter_grid.x) * 128 +
                                             (other_loc->size_x - loc->size_x) * 64;
                        int check_offset_y = (other_min_tile_y - iter_grid.y) * 128 +
                                             (other_loc->size_y - loc->size_y) * 64;

                        int check_offset_level =
                            (map_terrain
                                 ->tiles_xyz[MAP_TILE_COORD(
                                     other_loc->chunk_pos_x,
                                     other_loc->chunk_pos_y,
                                     other_loc->chunk_pos_level)]
                                 .height -
                             map_terrain
                                 ->tiles_xyz[MAP_TILE_COORD(
                                     iter_grid.x, iter_grid.y, iter_grid.level)]
                                 .height);
                        merge_normals(
                            model->model,
                            model->normals->lighting_vertex_normals,
                            model->aliased_lighting_normals->lighting_vertex_normals,
                            other_model->model,
                            other_model->normals->lighting_vertex_normals,
                            other_model->aliased_lighting_normals->lighting_vertex_normals,
                            check_offset_x,
                            check_offset_level,
                            check_offset_y,
                            iter_grid.x,
                            iter_grid.y,
                            iter_grid.level,
                            other_loc->chunk_pos_x,
                            other_loc->chunk_pos_y,
                            iter_grid.level);
                    }
                }
            }
        }

        iter_grid_next(&iter_grid);
    }

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
        model_free(model->model);
    }

    free(scene->models);
    free(scene->locs);
    free(scene->grid_tiles);
    free(scene);
}
