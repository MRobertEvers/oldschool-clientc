#include "scene.h"

#include "filelist.h"
#include "lighting.h"
#include "model_transforms.h"
#include "tables/config_idk.h"
#include "tables/config_locs.h"
#include "tables/config_object.h"
#include "tables/configs.h"
#include "tables/maps.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TILE_SIZE 128

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

static void
loc_load_model(
    struct SceneModel* scene_model,
    struct CacheConfigLocation* loc_config,
    struct Cache* cache,
    struct ModelCache* model_cache,
    struct CacheConfigSequenceTable* sequence_table,
    int shape_select,
    int orientation,
    int sw_height,
    int se_height,
    int ne_height,
    int nw_height)
{
    struct CacheConfigSequence* sequence = NULL;
    struct CacheFramemap* framemap = NULL;
    struct CacheFrame* frame = NULL;
    struct CacheArchive* frame_archive = NULL;
    struct FileList* frame_filelist = NULL;
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

    if( model->_id == 2255 && scene_model->yaw != 0 )
    {
        int i = 0;
    }

    // Sequences don't account for rotations, so models must be rotated AFTER the animation is
    // applied.
    if( loc_config->seq_id != -1 )
    {
        // TODO: account for transforms.
        // Also, I believe this is the only overridden transform.
        //         const isEntity =
        // seqId !== -1 ||
        // locType.transforms !== undefined ||
        // locLoadType === LocLoadType.NO_MODELS;

        scene_model->yaw = 512 * orientation;
        scene_model->yaw %= 2048;
        orientation = 0;
    }

    loc_apply_transforms(
        loc_config, model, orientation, sw_height, se_height, ne_height, nw_height);

    scene_model->model = model;
    scene_model->model_id = model_ids[0];

    scene_model->light_ambient = loc_config->ambient;
    scene_model->light_contrast = loc_config->contrast;
    scene_model->sharelight = loc_config->sharelight;

    scene_model->__loc_id = loc_config->_id;

    if( model->vertex_bone_map )
        scene_model->vertex_bones =
            modelbones_new_decode(model->vertex_bone_map, model->vertex_count);
    if( model->face_bone_map )
        scene_model->face_bones = modelbones_new_decode(model->face_bone_map, model->face_count);

    scene_model->sequence = NULL;
    if( loc_config->seq_id != -1 )
    {
        scene_model_vertices_create_original(scene_model);

        if( model->face_alphas )
            scene_model_face_alphas_create_original(scene_model);

        sequence = config_sequence_table_get_new(sequence_table, loc_config->seq_id);
        assert(sequence);
        assert(sequence->frame_lengths);
        scene_model->sequence = sequence;

        assert(scene_model->frames == NULL);
        scene_model->frames = malloc(sizeof(struct CacheFrame*) * sequence->frame_count);
        memset(scene_model->frames, 0, sizeof(struct CacheFrame*) * sequence->frame_count);

        int frame_id = sequence->frame_ids[0];
        int frame_archive_id = (frame_id >> 16) & 0xFFFF;
        // Get the frame definition ID from the second 2 bytes of the sequence frame ID The
        //     first 2 bytes are the sequence ID,
        //     the second 2 bytes are the frame archive ID

        frame_archive = cache_archive_new_load(cache, CACHE_ANIMATIONS, frame_archive_id);
        frame_filelist = filelist_new_from_cache_archive(frame_archive);
        for( int i = 0; i < sequence->frame_count; i++ )
        {
            assert(((sequence->frame_ids[i] >> 16) & 0xFFFF) == frame_archive_id);
            // assert(i < frame_filelist->file_count);

            int frame_id = sequence->frame_ids[i];
            int frame_archive_id = (frame_id >> 16) & 0xFFFF;
            int frame_file_id = frame_id & 0xFFFF;

            assert(frame_file_id > 0);
            assert(frame_file_id - 1 < frame_filelist->file_count);

            char* frame_data = frame_filelist->files[frame_file_id - 1];
            int frame_data_size = frame_filelist->file_sizes[frame_file_id - 1];
            int framemap_id = framemap_id_from_frame_archive(frame_data, frame_data_size);

            if( !scene_model->framemap )
            {
                framemap = framemap_new_from_cache(cache, framemap_id);
                scene_model->framemap = framemap;
            }

            frame = frame_new_decode2(frame_id, scene_model->framemap, frame_data, frame_data_size);

            scene_model->frames[scene_model->frame_count++] = frame;
        }

        cache_archive_free(frame_archive);
        frame_archive = NULL;
        filelist_free(frame_filelist);
        frame_filelist = NULL;
    }

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
    return &scene->models[scene->models_length - 1];
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
    struct CacheConfigObjectTable* config_object_table = NULL;
    struct CacheConfigSequenceTable* config_sequence_table = NULL;
    struct CacheConfigIdkTable* config_idk_table = NULL;
    struct CacheMapLoc* map = NULL;
    struct Loc* loc = NULL;
    struct Loc* other_loc = NULL;
    struct SceneModel* scene_model = NULL;
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

    config_sequence_table = config_sequence_table_new(cache);
    assert(config_sequence_table);

    config_locs_table = config_locs_table_new(cache);
    if( !config_locs_table )
    {
        printf("Failed to load config locs table\n");
        goto error;
    }

    config_object_table = config_object_table_new(cache);
    if( !config_object_table )
    {
        printf("Failed to load config object table\n");
        goto error;
    }

    iter = map_locs_iter_new(cache, chunk_x, chunk_y);
    if( !iter )
    {
        printf("Failed to load map locs iter\n");
        goto error;
    }

    int p1x1_height_center = 0;

    while( (map = map_locs_iter_next(iter)) )
    {
        scene_model = NULL;
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

        if( tile_x == 1 && tile_y == 1 && tile_z == 0 )
        {
            p1x1_height_center = height_center;
        }

        loc_config = config_locs_table_get_new(config_locs_table, map->loc_id);
        assert(loc_config);

        switch( map->shape_select )
        {
        case LOC_SHAPE_WALL_SINGLE_SIDE:
        {
            // Load model
            int model_index = vec_model_push(scene);
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

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
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

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
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
                LOC_SHAPE_WALL_TWO_SIDES,
                map->orientation + 4,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            int model_b_index = vec_model_push(scene);
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
                LOC_SHAPE_WALL_TWO_SIDES,
                next_orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

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
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

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

            assert(scene_model->model_id != 0);
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
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

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
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
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
                other_loc = &scene->locs[grid_tile->wall];
                assert(other_loc->type == LOC_TYPE_WALL);
                if( other_loc->_wall.wall_width )
                    offset = other_loc->_wall.wall_width;
            }

            calculate_wall_decor_offset(
                scene_model, map->orientation, offset, false // diagonal
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
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                map->orientation + 4,
                height_sw,
                height_se,
                height_ne,
                height_nw);
            scene_model->yaw += 256;

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            // TODO: Get this from the wall offset??
            // This needs to be taken from the wall offset.
            // Lumbridge walls are 16 thick.
            // Walls in al kharid are 8 thick.
            int offset = 45;
            calculate_wall_decor_offset(
                scene_model, map->orientation, offset, true // diagonal
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
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

            int offset = 45;
            calculate_wall_decor_offset(
                scene_model, map->orientation, offset, true // diagonal
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
        case LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE:
        {
            int outside_orientation = map->orientation;
            int inside_orientation = (map->orientation + 2) & 0x3;

            int model_index_a = vec_model_push(scene);
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                outside_orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);
            scene_model->yaw += 256;

            // TODO: Get this from the wall offset??
            // This needs to be taken from the wall offset.
            // Lumbridge walls are 16 thick.
            // Walls in al kharid are 8 thick.
            int offset = 53;
            calculate_wall_decor_offset(
                scene_model, outside_orientation, offset, true // diagonal
            );

            int model_index_b = vec_model_push(scene);
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
                LOC_SHAPE_WALL_DECOR_NOOFFSET,
                inside_orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);
            scene_model->yaw += 256;

            // TODO: Get this from the wall offset??
            offset = 45;
            calculate_wall_decor_offset(
                scene_model, inside_orientation, offset, true // diagonal
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
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

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
            // Load model
            int model_index = vec_model_push(scene);
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
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

            init_scene_model_wxh(scene_model, tile_x, tile_y, height_center, size_x, size_y);
            if( map->shape_select == LOC_SHAPE_SCENERY_DIAGIONAL )
                scene_model->yaw += 256;

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
            int model_index = vec_model_push(scene);
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

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
            scene_model = vec_model_back(scene);
            loc_load_model(
                scene_model,
                loc_config,
                cache,
                model_cache,
                config_sequence_table,
                map->shape_select,
                map->orientation,
                height_sw,
                height_se,
                height_ne,
                height_nw);

            init_scene_model_1x1(scene_model, tile_x, tile_y, height_center);

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

        free_loc(loc_config);
        loc_config = NULL;
    }

    map_locs_iter_free(iter);

    config_idk_table = config_idk_table_new(cache);
    if( !config_idk_table )
    {
        printf("Failed to load config idk table\n");
        goto error;
    }

    // TODO: Remove
    {
        // int const abyssal_whip = 4151;
        // Infernal cape
        int const abyssal_whip = 21295;
        struct CacheConfigObject* object =
            config_object_table_get_new(config_object_table, abyssal_whip);
        if( object )
        {
            printf("Abyssal whip: %d\n", object->male_model_0);
        }
        int abyssal_tile_x = 1;
        int abyssal_tile_y = 1;

        int loc_index = vec_loc_push(scene);
        loc = vec_loc_back(scene);
        init_loc_1x1(loc, abyssal_tile_x, abyssal_tile_y, 0);

        int model_index = vec_model_push(scene);
        scene_model = vec_model_back(scene);

        struct CacheModel* abyssal_model =
            model_cache_checkout(model_cache, cache, object->inventory_model_id);
        if( !abyssal_model )
        {
            printf("Failed to checkout model for abyssal whip\n");
            goto error;
        }

        abyssal_model = model_new_copy(abyssal_model);
        scene_model->model = abyssal_model;
        scene_model->model_id = abyssal_model->_id;
        init_scene_model_1x1(scene_model, abyssal_tile_x, abyssal_tile_y, p1x1_height_center);

        scene_model->light_ambient = object->ambient;
        scene_model->light_contrast = object->contrast;

        loc->type = LOC_TYPE_GROUND_OBJECT;
        loc->_ground_object.model = model_index;

        grid_tile = &scene->grid_tiles[MAP_TILE_COORD(abyssal_tile_x, abyssal_tile_y, 0)];
        grid_tile->ground_object_bottom = loc_index;
    }

    // TODO: Remove
    // {
    //     // 4,274  6,282 292 256 289 298 266
    //     int idk_ids[12] = { 0, 0, 0, 0, 274, 0, 282, 292, 256, 289, 298, 266 };
    //     int parts__models_ids[10] = { 0 };
    //     int parts__models_count = 0;

    //     for( int i = 0; i < 12; i++ )
    //     {
    //         int idk_id = idk_ids[i];
    //         if( idk_id >= 256 && idk_id < 512 )
    //         {
    //             idk_id -= 256;
    //         }
    //         else
    //             continue;
    //         struct CacheConfigIdk* idk = config_idk_table_get(config_idk_table, idk_id);
    //         if( idk )
    //         {
    //             assert(idk->model_ids_count == 1);
    //             parts__models_ids[parts__models_count++] = idk->model_ids[0];
    //         }
    //     }

    //     struct CacheModel* models[12] = { 0 };
    //     for( int i = 0; i < parts__models_count; i++ )
    //     {
    //         struct CacheModel* model =
    //             model_cache_checkout(model_cache, cache, parts__models_ids[i]);
    //         if( model )
    //         {
    //             models[i] = model;
    //         }
    //         else
    //         {
    //             assert(false);
    //         }
    //     }

    //     struct CacheModel* merged_model = model_new_merge(models, parts__models_count);

    //     model->model = merged_model;

    //     struct CacheConfigSequence* sequence = NULL;

    //     sequence = config_sequence_table_get_new(config_sequence_table, 819);
    //     if( sequence )
    //     {
    //         model->sequence = sequence;

    //         if( model->model->vertex_bone_map )
    //             model->vertex_bones = modelbones_new_decode(
    //                 model->model->vertex_bone_map, model->model->vertex_count);
    //         if( model->model->face_bone_map )
    //             model->face_bones =
    //                 modelbones_new_decode(model->model->face_bone_map, model->model->face_count);

    //         model->original_vertices_x = malloc(sizeof(int) * model->model->vertex_count);
    //         model->original_vertices_y = malloc(sizeof(int) * model->model->vertex_count);
    //         model->original_vertices_z = malloc(sizeof(int) * model->model->vertex_count);

    //         memcpy(
    //             model->original_vertices_x,
    //             model->model->vertices_x,
    //             sizeof(int) * model->model->vertex_count);
    //         memcpy(
    //             model->original_vertices_y,
    //             model->model->vertices_y,
    //             sizeof(int) * model->model->vertex_count);
    //         memcpy(
    //             model->original_vertices_z,
    //             model->model->vertices_z,
    //             sizeof(int) * model->model->vertex_count);

    //         if( model->model->face_alphas )
    //         {
    //             model->original_face_alphas = malloc(sizeof(int) * model->model->face_count);
    //             memcpy(
    //                 model->original_face_alphas,
    //                 model->model->face_alphas,
    //                 sizeof(int) * model->model->face_count);
    //         }

    //         assert(model->frames == NULL);
    //         model->frames = malloc(sizeof(struct CacheFrame*) * sequence->frame_count);
    //         memset(model->frames, 0, sizeof(struct CacheFrame*) * sequence->frame_count);

    //         int frame_id = sequence->frame_ids[0];
    //         int frame_archive_id = (frame_id >> 16) & 0xFFFF;
    //         // Get the frame definition ID from the second 2 bytes of the sequence frame ID The
    //         //     first 2 bytes are the sequence ID,
    //         //     the second 2 bytes are the frame archive ID

    //         struct CacheArchive* frame_archive =
    //             cache_archive_new_load(cache, CACHE_ANIMATIONS, frame_archive_id);
    //         struct FileList* frame_filelist = filelist_new_from_cache_archive(frame_archive);
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

    //             if( !model->framemap )
    //             {
    //                 model->framemap = framemap_new_from_cache(cache, framemap_id);
    //             }

    //             struct CacheFrame* frame =
    //                 frame_new_decode2(frame_id, model->framemap, frame_data, frame_data_size);

    //             model->frames[model->frame_count++] = frame;
    //         }

    //         cache_archive_free(frame_archive);
    //         frame_archive = NULL;
    //         filelist_free(frame_filelist);
    //         frame_filelist = NULL;
    //     }
    // }
    // TODO: End Remove

    // This must happen after loading of locs because locs influence the lightness.
    scene_tiles = scene_tiles_new_from_map_terrain_cache(map_terrain, shade_map, cache);
    if( !scene_tiles )
    {
        printf("Failed to load scene tiles\n");
        goto error;
    }
    scene->scene_tiles = scene_tiles;
    scene->scene_tiles_length = MAP_TILE_COUNT;

    for( int i = 0; i < MAP_TILE_COUNT; i++ )
    {
        struct SceneTile* scene_tile = &scene_tiles[i];
        grid_tile = &scene->grid_tiles[MAP_TILE_COORD(
            scene_tile->chunk_pos_x, scene_tile->chunk_pos_y, scene_tile->chunk_pos_level)];
        grid_tile->ground = i;
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

                    // If the other loc is a wall, treat it as if it is one off.
                    if( scene_model->_chunk_pos_x == 48 && scene_model->_chunk_pos_y == 10 )
                    {
                        int iii = 0;
                    }

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

    for( int i = 0; i < scene->models_length; i++ )
    {
        struct SceneModel* scene_model = &scene->models[i];
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

        if( scene_model->_chunk_pos_x == 48 && scene_model->_chunk_pos_y == 10 &&
            scene_model->model->face_count == 10 )
        {
            int iii = 0;
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

    scene->terrain = map_terrain;

    config_locs_table_free(config_locs_table);
    config_object_table_free(config_object_table);
    config_sequence_table_free(config_sequence_table);
    config_idk_table_free(config_idk_table);

    free(shade_map);

    return scene;

error:
    return NULL;
}

static void
lighting_normals_free(struct ModelNormals* normals)
{
    if( !normals )
        return;

    free(normals->lighting_vertex_normals);
    free(normals->lighting_face_normals);

    free(normals);
}

static void
model_lighting_free(struct ModelLighting* lighting)
{
    if( !lighting )
        return;

    free(lighting->face_colors_hsl_a);
    free(lighting->face_colors_hsl_b);
    free(lighting->face_colors_hsl_c);
    free(lighting);
}

void
scene_free(struct Scene* scene)
{
    model_cache_free(scene->_model_cache);
    free_tiles(scene->scene_tiles, scene->scene_tiles_length);

    for( int i = 0; i < scene->models_length; i++ )
    {
        struct SceneModel* model = &scene->models[i];
        scene_model_free(model);
    }

    free(scene->models);
    free(scene->locs);
    free(scene->grid_tiles);
    map_terrain_free(scene->terrain);
    free(scene);
}

void
scene_clear_entities(struct Scene* scene)
{
    struct GridTile* tile = NULL;
    struct Loc* loc = NULL;
    int index = 0;

    for( int i = 0; i < scene->entities_length; i++ )
    {
        tile = &scene->grid_tiles[MAP_TILE_COORD(
            scene->entities[i].x, scene->entities[i].z, scene->entities[i].level)];
        while( tile->locs_length > 0 &&
               scene->locs[tile->locs[tile->locs_length - 1]].entity != -1 )
        {
            tile->locs_length--;
            scene->models_length--;
        }
    }

    scene->locs_length -= scene->temporary_locs_length;
    scene->temporary_locs_length = 0;

    memset(scene->entities, 0, sizeof(scene->entities));
    scene->entities_length = 0;
}

void
scene_add_player_entity(struct Scene* scene, int x, int z, int level, struct SceneModel* model)
{
    struct GridTile* tile = NULL;
    struct Loc* loc = NULL;

    int tile_x = x;
    int tile_y = z;
    int tile_z = level;

    struct CacheMapTerrain* map_terrain = scene->terrain;

    int height_sw = map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x, tile_y, tile_z)].height;
    int height_se = height_sw;
    if( tile_x + 1 < MAP_TERRAIN_X )
        height_se = map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x + 1, tile_y, tile_z)].height;

    int height_ne = height_sw;
    if( tile_y + 1 < MAP_TERRAIN_Y && tile_x + 1 < MAP_TERRAIN_X )
        height_ne = map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x + 1, tile_y + 1, tile_z)].height;

    int height_nw = height_sw;
    if( tile_y + 1 < MAP_TERRAIN_Y )
        height_nw = map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x, tile_y + 1, tile_z)].height;

    int height_center = (height_sw + height_se + height_ne + height_nw) >> 2;

    model->region_x = x * 128 + 64;
    model->region_z = z * 128 + 64;
    model->region_height = height_center;

    scene->models[scene->models_length++] = *model;

    loc = &scene->locs[scene->locs_length++];
    loc->entity = scene->temporary_locs_length++;
    loc->chunk_pos_x = x;
    loc->chunk_pos_y = z;
    loc->chunk_pos_level = level;
    loc->size_x = 1;
    loc->size_y = 1;

    loc->type = LOC_TYPE_SCENERY;
    loc->_scenery.model = scene->models_length - 1;

    tile = &scene->grid_tiles[MAP_TILE_COORD(x, z, level)];

    tile->locs[tile->locs_length++] = scene->locs_length - 1;

    scene->entities[scene->entities_length++] = (struct TemporaryEntity){
        .x = x,
        .z = z,
        .level = level,
    };
}

struct SceneModel*
scene_model_new_from_idkit(struct Scene* scene, struct Cache* cache, struct SceneIdKit* kit)
{
    int parts__models_ids[12] = { 0 };
    int parts__models_count = 0;
}

void
scene_model_set_sequence(
    struct Scene* scene, struct Cache* cache, struct SceneModel* model, int sequence_id)
{}

static void
scene_model_free(struct SceneModel* model)
{
    model_free(model->model);
    lighting_normals_free(model->normals);
    lighting_normals_free(model->aliased_lighting_normals);
    model_lighting_free(model->lighting);
    modelbones_free(model->vertex_bones);
    modelbones_free(model->face_bones);

    for( int i = 0; i < model->frame_count; i++ )
    {
        frame_free(model->frames[i]);
    }
    free(model->frames);
    config_sequence_free(model->sequence);

    framemap_free(model->framemap);

    free(model->original_vertices_x);
    free(model->original_vertices_y);
    free(model->original_vertices_z);
    free(model->original_face_alphas);
}
