#ifndef SCENELOAD_U_C
#define SCENELOAD_U_C

#include "scene.h"
#include "tables/config_locs.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TILE_SIZE 128
#define WALL_DECOR_YAW_ADJUST_DIAGONAL_OUTSIDE 256
#define WALL_DECOR_YAW_ADJUST_DIAGONAL_INSIDE (768 + 1024)

static const int WALL_DECOR_ROTATION_OFFSET_X[] = { 1, 0, -1, 0 };
static const int WALL_DECOR_ROTATION_OFFSET_Z[] = { 0, -1, 0, 1 };
static const int WALL_DECOR_ROTATION_DIAGONAL_OFFSET_X[] = { 1, -1, -1, 1 };
static const int WALL_DECOR_ROTATION_DIAGONAL_OFFSET_Z[] = { -1, -1, 1, 1 };

static const int ROTATION_WALL_TYPE[] = {
    WALL_SIDE_WEST, WALL_SIDE_NORTH, WALL_SIDE_EAST, WALL_SIDE_SOUTH
};
static const int ROTATION_WALL_CORNER_TYPE[] = {
    WALL_CORNER_NORTHWEST,
    WALL_CORNER_NORTHEAST,
    WALL_CORNER_SOUTHEAST,
    WALL_CORNER_SOUTHWEST,
};

static struct ModelNormals*
model_normals_new(int vertex_count, int face_count)
{
    struct ModelNormals* normals = malloc(sizeof(struct ModelNormals));
    memset(normals, 0, sizeof(struct ModelNormals));

    normals->lighting_vertex_normals = malloc(sizeof(struct LightingNormal) * vertex_count);
    memset(normals->lighting_vertex_normals, 0, sizeof(struct LightingNormal) * vertex_count);

    normals->lighting_face_normals = malloc(sizeof(struct LightingNormal) * face_count);
    memset(normals->lighting_face_normals, 0, sizeof(struct LightingNormal) * face_count);

    normals->lighting_vertex_normals_count = vertex_count;
    normals->lighting_face_normals_count = face_count;

    return normals;
}

static struct ModelNormals*
model_normals_new_copy(struct ModelNormals* normals)
{
    struct ModelNormals* aliased_normals = malloc(sizeof(struct ModelNormals));
    memset(aliased_normals, 0, sizeof(struct ModelNormals));

    int vertex_count = normals->lighting_vertex_normals_count;
    int face_count = normals->lighting_face_normals_count;

    aliased_normals->lighting_vertex_normals = malloc(sizeof(struct LightingNormal) * vertex_count);
    memcpy(
        aliased_normals->lighting_vertex_normals,
        normals->lighting_vertex_normals,
        sizeof(struct LightingNormal) * vertex_count);

    aliased_normals->lighting_face_normals = malloc(sizeof(struct LightingNormal) * face_count);
    memcpy(
        aliased_normals->lighting_face_normals,
        normals->lighting_face_normals,
        sizeof(struct LightingNormal) * face_count);

    aliased_normals->lighting_vertex_normals_count = vertex_count;
    aliased_normals->lighting_face_normals_count = face_count;

    return aliased_normals;
}

static struct ModelLighting*
model_lighting_new(int face_count)
{
    struct ModelLighting* lighting = malloc(sizeof(struct ModelLighting));
    memset(lighting, 0, sizeof(struct ModelLighting));

    lighting->face_colors_hsl_a = malloc(sizeof(int) * face_count);
    memset(lighting->face_colors_hsl_a, 0, sizeof(int) * face_count);

    lighting->face_colors_hsl_b = malloc(sizeof(int) * face_count);
    memset(lighting->face_colors_hsl_b, 0, sizeof(int) * face_count);

    lighting->face_colors_hsl_c = malloc(sizeof(int) * face_count);
    memset(lighting->face_colors_hsl_c, 0, sizeof(int) * face_count);

    return lighting;
}

/**
 * If a model is animated, we need to store the original vertices
 */
static void
scene_model_vertices_create_original(struct SceneModel* scene_model)
{
    scene_model->original_vertices_x = (int*)malloc(sizeof(int) * scene_model->model->vertex_count);
    scene_model->original_vertices_y = (int*)malloc(sizeof(int) * scene_model->model->vertex_count);
    scene_model->original_vertices_z = (int*)malloc(sizeof(int) * scene_model->model->vertex_count);

    memcpy(
        scene_model->original_vertices_x,
        scene_model->model->vertices_x,
        sizeof(int) * scene_model->model->vertex_count);

    memcpy(
        scene_model->original_vertices_y,
        scene_model->model->vertices_y,
        sizeof(int) * scene_model->model->vertex_count);

    memcpy(
        scene_model->original_vertices_z,
        scene_model->model->vertices_z,
        sizeof(int) * scene_model->model->vertex_count);
}

/**
 * If a model is animated, we need to store the original face alphas
 */
static void
scene_model_face_alphas_create_original(struct SceneModel* scene_model)
{
    scene_model->original_face_alphas = malloc(sizeof(int) * scene_model->model->face_count);
    memcpy(
        scene_model->original_face_alphas,
        scene_model->model->face_alphas,
        sizeof(int) * scene_model->model->face_count);
}

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
    int z_multiplier = diagonal ? WALL_DECOR_ROTATION_DIAGONAL_OFFSET_Z[orientation]
                                : WALL_DECOR_ROTATION_OFFSET_Z[orientation];
    int offset_x = offset * x_multiplier;
    int offset_z = offset * z_multiplier;

    decor->offset_x = offset_x;
    decor->offset_z = offset_z;
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

static struct SceneModel*
tile_scenery_model_nullable(struct SceneModel* models, struct Loc* loc)
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

static struct SceneModel*
tile_ground_decor_model_nullable(struct SceneModel* models, struct Loc* loc)
{
    if( loc->type == LOC_TYPE_GROUND_DECOR )
    {
        if( loc->_ground_decor.model >= 0 )
            return &models[loc->_ground_decor.model];
        else
            return NULL;
    }

    return NULL;
}

static int g_merge_index = 0;
static int g_vertex_a_merge_index[10000] = { 0 };
static int g_vertex_b_merge_index[10000] = { 0 };

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
    int check_offset_z)
{
    g_merge_index++;

    struct LightingNormal* model_a_normal = NULL;
    struct LightingNormal* model_b_normal = NULL;
    struct LightingNormal* model_a_lighting_normal = NULL;
    struct LightingNormal* model_b_lighting_normal = NULL;
    int x, y, z;
    int other_x, other_y, other_z;

    int merged_vertex_count = 0;

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

                merged_vertex_count++;

                g_vertex_a_merge_index[vertex] = g_merge_index;
                g_vertex_b_merge_index[other_vertex] = g_merge_index;
            }
        }
    }

    /**
     * Normals that are merged are assumed to be abutting each other and their faces not visible.
     * Hide them.
     */

    // TODO: This isn't allow for locs. Only ground decor.
    if( merged_vertex_count < 3 || true )
        return;

    // Can't have two faces with the same 3 points, so only need to check two.
    for( int face = 0; face < model->face_count; face++ )
    {
        if( g_vertex_a_merge_index[model->face_indices_a[face]] == g_merge_index &&
            g_vertex_a_merge_index[model->face_indices_b[face]] == g_merge_index &&
            g_vertex_a_merge_index[model->face_indices_c[face]] == g_merge_index )
        {
            // OS1 initializes face infos to 0 here.
            if( !model->face_infos )
            {
                model->face_infos = malloc(sizeof(int) * model->face_count);
                memset(model->face_infos, 0, sizeof(int) * model->face_count);
            }
            // Hidden face (facetype 2 is hidden)
            model->face_infos[face] = 2;
            break;
        }
    }
    for( int face = 0; face < other_model->face_count; face++ )
    {
        if( g_vertex_b_merge_index[other_model->face_indices_a[face]] == g_merge_index &&
            g_vertex_b_merge_index[other_model->face_indices_b[face]] == g_merge_index &&
            g_vertex_b_merge_index[other_model->face_indices_c[face]] == g_merge_index )
        {
            // OS1 initializes face infos to 0 here.
            if( !other_model->face_infos )
            {
                other_model->face_infos = malloc(sizeof(int) * other_model->face_count);
                memset(other_model->face_infos, 0, sizeof(int) * other_model->face_count);
            }
            // Hidden face (facetype 2 is hidden)
            other_model->face_infos[face] = 2;
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
    for( int level = 0; level <= tile_level + 1; level++ )
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

                assert(count < out_size);
                out[count++] = MAP_TILE_COORD(x, y, level);
            }
        }
    }

    return count;
}

static int
gather_sharelight_models(
    struct SceneModel** out,
    int out_size,
    struct GridTile* tile,
    struct Loc* loc_pool,
    int loc_pool_size,
    struct SceneModel* models,
    int models_size)
{
    int count = 0;
    struct Loc* loc = NULL;
    struct SceneModel* model = NULL;

    if( tile->wall != -1 )
    {
        assert(tile->wall >= 0);
        assert(tile->wall < loc_pool_size);
        loc = &loc_pool[tile->wall];

        model = tile_wall_model_nullable(models, loc, 1);
        if( model && model->sharelight )
            out[count++] = model;

        model = tile_wall_model_nullable(models, loc, 0);
        if( model && model->sharelight )
            out[count++] = model;
    }

    // if( tile->ground_decor != -1 )
    // {
    //     assert(tile->ground_decor < loc_pool_size);
    //     loc = &loc_pool[tile->ground_decor];

    //     model = tile_ground_decor_model_nullable(models, loc);
    //     if( model && model->sharelight )
    //         out[count++] = model;
    // }

    for( int i = 0; i < tile->locs_length; i++ )
    {
        loc = &loc_pool[tile->locs[i]];
        model = tile_scenery_model_nullable(models, loc);
        if( model && model->sharelight )
            out[count++] = model;

        model = tile_wall_model_nullable(models, loc, 1);
        if( model && model->sharelight )
            out[count++] = model;

        model = tile_wall_model_nullable(models, loc, 0);
        if( model && model->sharelight )
            out[count++] = model;

        model = tile_ground_decor_model_nullable(models, loc);
        if( model && model->sharelight )
            out[count++] = model;
    }

    assert(count <= out_size);
    return count;
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

static struct ModelLighting*
model_lighting_new_default(
    struct CacheModel* model,
    struct ModelNormals* normals,
    int model_contrast,
    int model_attenuation)
{
    struct ModelLighting* lighting = model_lighting_new(model->face_count);

    int light_ambient = 64;
    int light_attenuation = 768;
    int lightsrc_x = -50;
    int lightsrc_y = -10;
    int lightsrc_z = -50;

    light_ambient += model_contrast;
    light_attenuation += model_attenuation;

    int light_magnitude =
        (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
    int attenuation = (light_attenuation * light_magnitude) >> 8;

    apply_lighting(
        lighting->face_colors_hsl_a,
        lighting->face_colors_hsl_b,
        lighting->face_colors_hsl_c,
        normals->lighting_vertex_normals,
        normals->lighting_face_normals,
        model->face_indices_a,
        model->face_indices_b,
        model->face_indices_c,
        model->face_count,
        model->face_colors,
        model->face_alphas,
        model->face_textures,
        model->face_infos,
        light_ambient,
        attenuation,
        lightsrc_x,
        lightsrc_y,
        lightsrc_z);

    return lighting;
}

struct TileHeights
{
    int sw_height;
    int se_height;
    int ne_height;
    int nw_height;
    int height_center;
};

static void
tile_heights_init_from_floor(
    struct TileHeights* tile_heights,
    struct CacheMapFloor* floor,
    int tile_x,
    int tile_y,
    int tile_level)
{
    int height_sw = floor[MAP_TILE_COORD(tile_x, tile_y, tile_level)].height;
    int height_se = height_sw;
    if( tile_x + 1 < MAP_TERRAIN_X )
        height_se = floor[MAP_TILE_COORD(tile_x + 1, tile_y, tile_level)].height;

    int height_ne = height_sw;
    if( tile_y + 1 < MAP_TERRAIN_Y && tile_x + 1 < MAP_TERRAIN_X )
        height_ne = floor[MAP_TILE_COORD(tile_x + 1, tile_y + 1, tile_level)].height;

    int height_nw = height_sw;
    if( tile_y + 1 < MAP_TERRAIN_Y )
        height_nw = floor[MAP_TILE_COORD(tile_x, tile_y + 1, tile_level)].height;

    int height_center = (height_sw + height_se + height_ne + height_nw) >> 2;

    tile_heights->sw_height = height_sw;
    tile_heights->se_height = height_se;
    tile_heights->ne_height = height_ne;
    tile_heights->nw_height = height_nw;
    tile_heights->height_center = height_center;
}

static void
init_grid_tile(struct GridTile* grid_tile, int x, int y, int level)
{
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

static void
init_grid_tiles(struct GridTile* grid_tiles)
{
    struct GridTile* grid_tile = NULL;
    memset(grid_tiles, 0, sizeof(struct GridTile) * MAP_TILE_COUNT);

    for( int level = 0; level < MAP_TERRAIN_Z; level++ )
    {
        for( int x = 0; x < MAP_TERRAIN_X; x++ )
        {
            for( int y = 0; y < MAP_TERRAIN_Y; y++ )
            {
                grid_tile = &grid_tiles[MAP_TILE_COORD(x, y, level)];
                init_grid_tile(grid_tile, x, y, level);
            }
        }
    }
}

#endif