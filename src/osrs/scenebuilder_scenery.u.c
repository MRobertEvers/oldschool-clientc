#ifndef SCENE_BUILDER_SCENERY_U_C
#define SCENE_BUILDER_SCENERY_U_C

#include "dashlib.h"
#include "graphics/dash.h"
#include "graphics/lighting.h"
#include "model_transforms.h"
#include "tables/model.h"

// clang-format off
#include "terrain_grid.u.c"
// clang-format on

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

/**
 * Transforms must be applied here so that hillskew is correctly applied.
 * (As opposed to passing in rendering offsets/other params.)
 */
static void
apply_transforms(
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
light_model_default(
    struct DashModel* dash_model,
    int model_contrast,
    int model_attenuation)
{
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

static struct DashModel*
load_model(
    struct CacheConfigLocation* loc_config,
    struct DashMap* models_hmap,
    int shape_select,
    int orientation,
    struct TileHeights* tile_heights)
{
    struct ModelEntry* model_entry = NULL;
    int* shapes = loc_config->shapes;
    int** model_id_sets = loc_config->models;
    int* lengths = loc_config->lengths;
    int shapes_and_model_count = loc_config->shapes_and_model_count;

    struct CacheModel** models = NULL;
    int model_count = 0;
    int* model_ids = NULL;

    struct CacheModel* model = NULL;

    if( !model_id_sets )
        return NULL;

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

            model_entry = (struct ModelEntry*)dashmap_search(models_hmap, &model_id, DASHMAP_FIND);
            assert(model_entry);
            model = model_entry->model;

            assert(model);
            models[model_count] = model;
            model_ids[model_count] = model_id;
            model_count++;
        }
    }
    else
    {
        models = malloc(sizeof(struct CacheModel*) * shapes_and_model_count);
        memset(models, 0, sizeof(struct CacheModel*) * shapes_and_model_count);
        model_ids = malloc(sizeof(int) * shapes_and_model_count);
        memset(model_ids, 0, sizeof(int) * shapes_and_model_count);

        bool found = false;
        for( int i = 0; i < shapes_and_model_count; i++ )
        {
            int count_inner = lengths[i];

            int loc_type = shapes[i];
            if( loc_type == shape_select )
            {
                for( int j = 0; j < count_inner; j++ )
                {
                    int model_id = model_id_sets[i][j];
                    assert(model_id);

                    model_entry =
                        (struct ModelEntry*)dashmap_search(models_hmap, &model_id, DASHMAP_FIND);
                    assert(model_entry);
                    model = model_entry->model;

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

    // printf("face textures: ");
    // if( model->face_textures )
    //     for( int f = 0; f < model->face_count; f++ )
    //     {
    //         printf("%d; ", model->face_textures[f]);
    //     }
    // printf("\n");

    apply_transforms(
        loc_config,
        model,
        orientation,
        tile_heights->sw_height,
        tile_heights->se_height,
        tile_heights->ne_height,
        tile_heights->nw_height);

    struct DashModel* dash_model = NULL;
    dash_model = dashmodel_new_from_cache_model(model);
    model_free(model);

    //     scene_model->light_ambient = loc_config->ambient;
    //     scene_model->light_contrast = loc_config->contrast;

    light_model_default(dash_model, loc_config->contrast, loc_config->ambient);

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

        // scene_model->yaw = 512 * orientation;
        // scene_model->yaw %= 2048;
        // orientation = 0;
    }

    // scene_model->model = model;
    // scene_model->model_id = model_ids[0];

    // scene_model->light_ambient = loc_config->ambient;
    // scene_model->light_contrast = loc_config->contrast;
    // scene_model->sharelight = loc_config->sharelight;

    // scene_model->__loc_id = loc_config->_id;

    // if( model->vertex_bone_map )
    //     scene_model->vertex_bones =
    //         modelbones_new_decode(model->vertex_bone_map, model->vertex_count);
    // if( model->face_bone_map )
    //     scene_model->face_bones = modelbones_new_decode(model->face_bone_map,
    // model->face_count);

    // scene_model->sequence = NULL;
    // if( loc_config->seq_id != -1 )
    // {
    //     scene_model_vertices_create_original(scene_model);

    //     if( model->face_alphas )
    //         scene_model_face_alphas_create_original(scene_model);

    //     sequence = config_sequence_table_get_new(sequence_table, loc_config->seq_id);
    //     assert(sequence);
    //     assert(sequence->frame_lengths);
    //     scene_model->sequence = sequence;

    //     assert(scene_model->frames == NULL);
    //     scene_model->frames = malloc(sizeof(struct CacheFrame*) * sequence->frame_count);
    //     memset(scene_model->frames, 0, sizeof(struct CacheFrame*) * sequence->frame_count);

    //     int frame_id = sequence->frame_ids[0];
    //     int frame_archive_id = (frame_id >> 16) & 0xFFFF;
    //     // Get the frame definition ID from the second 2 bytes of the sequence frame ID The
    //     //     first 2 bytes are the sequence ID,
    //     //     the second 2 bytes are the frame archive ID

    //     frame_archive = cache_archive_new_load(cache, CACHE_ANIMATIONS, frame_archive_id);
    //     frame_filelist = filelist_new_from_cache_archive(frame_archive);
    //     for( int i = 0; i < sequence->frame_count; i++ )
    //     {
    //         assert(((sequence->frame_ids[i] >> 16) & 0xFFFF) == frame_archive_id);
    //         // assert(i < frame_filelist->file_count);

    //         int frame_id = sequence->frame_ids[i];
    //         int frame_archive_id = (frame_id >> 16) & 0xFFFF;
    //         int frame_file_id = frame_id & 0xFFFF;

    //         assert(frame_file_id > 0);
    //         assert(frame_file_id - 1 < frame_filelist->file_count);

    //         char* frame_data = frame_filelist->files[frame_file_id - 1];
    //         int frame_data_size = frame_filelist->file_sizes[frame_file_id - 1];
    //         int framemap_id = framemap_id_from_frame_archive(frame_data, frame_data_size);

    //         if( !scene_model->framemap )
    //         {
    //             framemap = framemap_new_from_cache(cache, framemap_id);
    //             scene_model->framemap = framemap;
    //         }

    //         frame = frame_new_decode2(frame_id, scene_model->framemap, frame_data,
    //         frame_data_size);

    //         scene_model->frames[scene_model->frame_count++] = frame;
    //     }

    //     cache_archive_free(frame_archive);
    //     frame_archive = NULL;
    //     filelist_free(frame_filelist);
    //     frame_filelist = NULL;
    // }

    free(models);
    free(model_ids);

    return dash_model;
}

static struct DashPosition*
dash_position_from_offset_wxh(
    struct TerrainGridOffsetFromSW* offset,
    int height_center,
    int size_x,
    int size_z)
{
    struct DashPosition* dash_position = malloc(sizeof(struct DashPosition));
    memset(dash_position, 0, sizeof(struct DashPosition));

    //         // model->region_x = tile_x * TILE_SIZE + size_x * 64;
    //         // model->region_z = tile_y * TILE_SIZE + size_y * 64;
    //         // model->region_height = height_center;

    dash_position->x = offset->x * TILE_SIZE + size_x * 64;
    dash_position->z = offset->z * TILE_SIZE + size_z * 64;
    dash_position->y = height_center;

    return dash_position;
}

static struct DashPosition*
dash_position_from_offset_1x1(
    struct TerrainGridOffsetFromSW* offset,
    int height_center)
{
    return dash_position_from_offset_wxh(offset, height_center, 1, 1);
}

static void
scenery_add_wall_single(
    struct SceneBuilder* scene_builder,
    struct TerrainGridOffsetFromSW* offset,
    struct TileHeights* tile_heights,
    struct CacheMapLoc* map_loc,
    struct CacheConfigLocation* config_loc,
    struct SceneScenery* scenery)
{
    struct DashModel* dash_model = NULL;
    struct DashPosition* dash_position = NULL;
    struct SceneElement scene_element;
    int element_id = -1;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        map_loc->shape_select,
        map_loc->orientation,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_wall(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[map_loc->orientation]);
}

static void
scenery_add_wall_tri_corner(
    struct SceneBuilder* scene_builder,
    struct TerrainGridOffsetFromSW* offset,
    struct TileHeights* tile_heights,
    struct CacheMapLoc* map_loc,
    struct CacheConfigLocation* config_loc,
    struct SceneScenery* scenery)
{
    struct DashModel* dash_model = NULL;
    struct DashPosition* dash_position = NULL;
    struct SceneElement scene_element;
    int element_id = -1;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        map_loc->shape_select,
        map_loc->orientation,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_wall(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[map_loc->orientation]);
}

static void
scenery_add_wall_two_sides(
    struct SceneBuilder* scene_builder,
    struct TerrainGridOffsetFromSW* offset,
    struct TileHeights* tile_heights,
    struct CacheMapLoc* map_loc,
    struct CacheConfigLocation* config_loc,
    struct SceneScenery* scenery)
{
    struct DashModel* dash_model_one = NULL;
    struct DashModel* dash_model_two = NULL;
    struct DashPosition* dash_position_one = NULL;
    struct DashPosition* dash_position_two = NULL;
    struct SceneElement scene_element;
    int element_one_id = -1;
    int element_two_id = -1;

    int next_orientation = (map_loc->orientation + 1) & 0x3;

    dash_model_one = load_model(
        config_loc,
        scene_builder->models_hmap,
        LOC_SHAPE_WALL_TWO_SIDES,
        map_loc->orientation + 4,
        tile_heights);

    dash_position_one = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model_one;
    scene_element.dash_position = dash_position_one;

    element_one_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_one_id != -1);

    dash_model_two = load_model(
        config_loc,
        scene_builder->models_hmap,
        LOC_SHAPE_WALL_TWO_SIDES,
        next_orientation,
        tile_heights);

    dash_position_two = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model_two;
    scene_element.dash_position = dash_position_two;
    element_two_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_two_id != -1);

    painter_add_wall(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_one_id,
        WALL_A,
        ROTATION_WALL_TYPE[map_loc->orientation]);

    painter_add_wall(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_two_id,
        WALL_B,
        ROTATION_WALL_TYPE[next_orientation]);
}

static void
scenery_add_wall_rect_corner(
    struct SceneBuilder* scene_builder,
    struct TerrainGridOffsetFromSW* offset,
    struct TileHeights* tile_heights,
    struct CacheMapLoc* map_loc,
    struct CacheConfigLocation* config_loc,
    struct SceneScenery* scenery)
{
    struct DashModel* dash_model = NULL;
    struct DashPosition* dash_position = NULL;
    struct SceneElement scene_element;
    int element_id = -1;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        LOC_SHAPE_WALL_RECT_CORNER,
        map_loc->orientation,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_wall(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[map_loc->orientation]);
}

static void
scenery_add_wall_decor_nooffset(
    struct SceneBuilder* scene_builder,
    struct TerrainGridOffsetFromSW* offset,
    struct TileHeights* tile_heights,
    struct CacheMapLoc* map_loc,
    struct CacheConfigLocation* config_loc,
    struct SceneScenery* scenery)
{
    struct DashModel* dash_model = NULL;
    struct DashPosition* dash_position = NULL;
    struct SceneElement scene_element;
    int element_id = -1;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        LOC_SHAPE_WALL_DECOR_NOOFFSET,
        map_loc->orientation,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_wall_decor(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[map_loc->orientation],
        0);
}

static void
scenery_add_wall_decor_offset(
    struct SceneBuilder* scene_builder,
    struct TerrainGridOffsetFromSW* offset,
    struct TileHeights* tile_heights,
    struct CacheMapLoc* map_loc,
    struct CacheConfigLocation* config_loc,
    struct SceneScenery* scenery)
{
    struct DashModel* dash_model = NULL;
    struct DashPosition* dash_position = NULL;
    struct SceneElement scene_element;
    int element_id = -1;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        // This is correct.
        LOC_SHAPE_WALL_DECOR_NOOFFSET,
        map_loc->orientation,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_wall_decor(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[map_loc->orientation],
        0);
}

static void
scenery_add_wall_decor_diagonal_offset(
    struct SceneBuilder* scene_builder,
    struct TerrainGridOffsetFromSW* offset,
    struct TileHeights* tile_heights,
    struct CacheMapLoc* map_loc,
    struct CacheConfigLocation* config_loc,
    struct SceneScenery* scenery)
{
    struct DashModel* dash_model = NULL;
    struct DashPosition* dash_position = NULL;
    struct SceneElement scene_element;
    int element_id = -1;

    int orientation = map_loc->orientation;
    orientation = (orientation + 2) & 0x3;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        // This is correct.
        LOC_SHAPE_WALL_DECOR_NOOFFSET,
        map_loc->orientation + 3,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_wall_decor(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation],
        0);
}

static void
scenery_add_wall_decor_diagonal_nooffset(
    struct SceneBuilder* scene_builder,
    struct TerrainGridOffsetFromSW* offset,
    struct TileHeights* tile_heights,
    struct CacheMapLoc* map_loc,
    struct CacheConfigLocation* config_loc,
    struct SceneScenery* scenery)
{
    struct DashModel* dash_model = NULL;
    struct DashPosition* dash_position = NULL;
    struct SceneElement scene_element;
    int element_id = -1;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        // This is correct.
        LOC_SHAPE_WALL_DECOR_NOOFFSET,
        map_loc->orientation,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_wall_decor(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[map_loc->orientation],
        0);
}

static void
scenery_add_wall_decor_diagonal_double(
    struct SceneBuilder* scene_builder,
    struct TerrainGridOffsetFromSW* offset,
    struct TileHeights* tile_heights,
    struct CacheMapLoc* map_loc,
    struct CacheConfigLocation* config_loc,
    struct SceneScenery* scenery)
{
    struct DashModel* dash_model_one = NULL;
    struct DashModel* dash_model_two = NULL;
    struct DashPosition* dash_position_one = NULL;
    struct DashPosition* dash_position_two = NULL;
    struct SceneElement scene_element;
    int element_one_id = -1;
    int element_two_id = -1;

    int outside_orientation = map_loc->orientation;
    int inside_orientation = (map_loc->orientation + 2) & 0x3;

    dash_model_one = load_model(
        config_loc,
        scene_builder->models_hmap,
        // This is correct.
        LOC_SHAPE_WALL_DECOR_NOOFFSET,
        outside_orientation + 1,
        tile_heights);

    dash_position_one = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model_one;
    scene_element.dash_position = dash_position_one;

    element_one_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_one_id != -1);

    dash_model_two = load_model(
        config_loc,
        scene_builder->models_hmap,
        // This is correct.
        LOC_SHAPE_WALL_DECOR_NOOFFSET,
        inside_orientation,
        tile_heights);

    dash_position_two = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model_two;
    scene_element.dash_position = dash_position_two;

    element_two_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_two_id != -1);

    painter_add_wall_decor(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_one_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[outside_orientation],
        THROUGHWALL);

    painter_add_wall_decor(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_two_id,
        WALL_B,
        ROTATION_WALL_CORNER_TYPE[inside_orientation],
        THROUGHWALL);
}

static void
scenery_add_wall_diagonal(
    struct SceneBuilder* scene_builder,
    struct TerrainGridOffsetFromSW* offset,
    struct TileHeights* tile_heights,
    struct CacheMapLoc* map_loc,
    struct CacheConfigLocation* config_loc,
    struct SceneScenery* scenery)
{
    struct DashModel* dash_model = NULL;
    struct DashPosition* dash_position = NULL;
    struct SceneElement scene_element;
    int element_id = -1;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        LOC_SHAPE_WALL_DIAGONAL,
        map_loc->orientation,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_normal_scenery(
        scene_builder->painter, offset->x, offset->z, offset->level, element_id, 1, 1);
}

static void
scenery_add_normal(
    struct SceneBuilder* scene_builder,
    struct TerrainGridOffsetFromSW* offset,
    struct TileHeights* tile_heights,
    struct CacheMapLoc* map_loc,
    struct CacheConfigLocation* config_loc,
    struct SceneScenery* scenery)
{
    struct DashModel* dash_model = NULL;
    struct DashPosition* dash_position = NULL;
    struct SceneElement scene_element;
    int element_id = -1;

    int size_x = config_loc->size_x;
    int size_z = config_loc->size_z;
    if( map_loc->orientation == 1 || map_loc->orientation == 3 )
    {
        int temp = size_x;
        size_x = size_z;
        size_z = temp;
    }

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        LOC_SHAPE_SCENERY,
        map_loc->orientation,
        tile_heights);

    dash_position =
        dash_position_from_offset_wxh(offset, tile_heights->height_center, size_x, size_z);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_normal_scenery(
        scene_builder->painter, offset->x, offset->z, offset->level, element_id, size_x, size_z);
}

static void
scenery_add_roof(
    struct SceneBuilder* scene_builder,
    struct TerrainGridOffsetFromSW* offset,
    struct TileHeights* tile_heights,
    struct CacheMapLoc* map_loc,
    struct CacheConfigLocation* config_loc,
    struct SceneScenery* scenery)
{
    struct DashModel* dash_model = NULL;
    struct DashPosition* dash_position = NULL;
    struct SceneElement scene_element;
    int element_id = -1;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        map_loc->shape_select,
        map_loc->orientation,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_normal_scenery(
        scene_builder->painter, offset->x, offset->z, offset->level, element_id, 1, 1);
}

static void
scenery_add_floor_decoration(
    struct SceneBuilder* scene_builder,
    struct TerrainGridOffsetFromSW* offset,
    struct TileHeights* tile_heights,
    struct CacheMapLoc* map_loc,
    struct CacheConfigLocation* config_loc,
    struct SceneScenery* scenery)
{
    struct DashModel* dash_model = NULL;
    struct DashPosition* dash_position = NULL;
    struct SceneElement scene_element;
    int element_id = -1;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        map_loc->shape_select,
        map_loc->orientation,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_ground_decor(
        scene_builder->painter, offset->x, offset->z, offset->level, element_id);
}

static void
scenery_add(
    struct SceneBuilder* scene_builder,
    struct TerrainGrid* terrain_grid,
    int mapx,
    int mapz,
    struct CacheMapLoc* map_loc,
    struct SceneScenery* scenery)
{
    struct CacheConfigLocation* config_loc = NULL;
    struct TileHeights tile_heights;
    struct SceneElement scene_element;
    struct TerrainGridOffsetFromSW offset;
    struct DashModel* dash_model = NULL;
    struct DashPosition* dash_position = NULL;
    int element_id = -1;

    config_loc = (struct CacheConfigLocation*)configmap_get(
        scene_builder->config_locs_configmap, map_loc->loc_id);
    assert(config_loc && "Config loc must be found in configmap");

    terrain_grid_offset_from_sw(
        terrain_grid,
        mapx,
        mapz,
        map_loc->chunk_pos_x,
        map_loc->chunk_pos_z,
        map_loc->chunk_pos_level,
        &offset);

    tile_heights_at(
        terrain_grid,
        mapx,
        mapz,
        map_loc->chunk_pos_x,
        map_loc->chunk_pos_z,
        map_loc->chunk_pos_level,
        &tile_heights);

    switch( map_loc->shape_select )
    {
    case LOC_SHAPE_WALL_SINGLE_SIDE:
        scenery_add_wall_single(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_TRI_CORNER:
        scenery_add_wall_tri_corner(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_TWO_SIDES:
        scenery_add_wall_two_sides(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_RECT_CORNER:
        scenery_add_wall_rect_corner(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_DECOR_NOOFFSET:
        scenery_add_wall_decor_nooffset(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_DECOR_OFFSET:
        scenery_add_wall_decor_offset(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_OFFSET:
        scenery_add_wall_decor_diagonal_offset(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_NOOFFSET:
        scenery_add_wall_decor_diagonal_nooffset(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE:
        scenery_add_wall_decor_diagonal_double(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_DIAGONAL:
        scenery_add_wall_diagonal(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_SCENERY:
    case LOC_SHAPE_SCENERY_DIAGIONAL:
        scenery_add_normal(scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
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
        scenery_add_roof(scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_FLOOR_DECORATION:
        scenery_add_floor_decoration(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    }
}

#endif