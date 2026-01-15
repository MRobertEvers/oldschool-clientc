#ifndef SCENE_BUILDER_SCENERY_U_C
#define SCENE_BUILDER_SCENERY_U_C

#include "dashlib.h"
#include "graphics/dash.h"
#include "graphics/lighting.h"
#include "model_transforms.h"
#include "osrs/tables/config_locs.h"
#include "osrs/tables/maps.h"
#include "painters.h"
#include "scene.h"
#include "tables/model.h"

// clang-format off
#include "scenebuilder.u.c"
#include "terrain_grid.u.c"
// clang-format on

#define TILE_SIZE 128
#define WALL_DECOR_YAW_ADJUST_DIAGONAL_OUTSIDE 256
// #define WALL_DECOR_YAW_ADJUST_DIAGONAL_INSIDE (768 + 1024)
#define WALL_DECOR_YAW_ADJUST_DIAGONAL_INSIDE (256 + 1024)

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
calculate_wall_decor_offset(
    struct DashPosition* dash_position,
    int orientation,
    int offset,
    bool diagonal)
{
    assert(orientation >= 0);
    assert(orientation < 4);

    int x_multiplier = diagonal ? WALL_DECOR_ROTATION_DIAGONAL_OFFSET_X[orientation]
                                : WALL_DECOR_ROTATION_OFFSET_X[orientation];
    int z_multiplier = diagonal ? WALL_DECOR_ROTATION_DIAGONAL_OFFSET_Z[orientation]
                                : WALL_DECOR_ROTATION_OFFSET_Z[orientation];
    int offset_x = offset * x_multiplier;
    int offset_z = offset * z_multiplier;

    dash_position->x += offset_x;
    dash_position->z += offset_z;
}

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

    //     scene_model->light_ambient = loc_config->ambient;
    //     scene_model->light_contrast = loc_config->contrast;

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
        orientation = 0;
    }

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

    light_model_default(dash_model, loc_config->contrast, loc_config->ambient);

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

// static struct DashModelNormals*
// model_normals_new_copy(struct DashModelNormals* normals)
// {
//     struct DashModelNormals* aliased_normals = malloc(sizeof(struct DashModelNormals));
//     memset(aliased_normals, 0, sizeof(struct DashModelNormals));

//     int vertex_count = normals->lighting_vertex_normals_count;
//     int face_count = normals->lighting_face_normals_count;

//     aliased_normals->lighting_vertex_normals = malloc(sizeof(struct LightingNormal) *
//     vertex_count); memcpy(
//         aliased_normals->lighting_vertex_normals,
//         normals->lighting_vertex_normals,
//         sizeof(struct LightingNormal) * vertex_count);

//     aliased_normals->lighting_face_normals = malloc(sizeof(struct LightingNormal) * face_count);
//     memcpy(
//         aliased_normals->lighting_face_normals,
//         normals->lighting_face_normals,
//         sizeof(struct LightingNormal) * face_count);

//     aliased_normals->lighting_vertex_normals_count = vertex_count;
//     aliased_normals->lighting_face_normals_count = face_count;

//     return aliased_normals;
// }

static void
init_build_element_from_config_loc(
    struct BuildElement* build_element,
    struct CacheConfigLocation* config_loc,
    int orientation)
{
    build_element->size_x = config_loc->size_x;
    build_element->size_z = config_loc->size_z;
    build_element->wall_offset = config_loc->wall_width;
    build_element->sharelight = config_loc->sharelight;
    build_element->rotation = orientation;
    build_element->light_ambient = config_loc->ambient;
    build_element->light_attenuation = config_loc->contrast;
    build_element->aliased_lighting_normals = NULL;
}

static void
scenery_set_wall_offsets(
    struct BuildElement* build_element,
    int shape_select)
{
    switch( shape_select )
    {
    case LOC_SHAPE_WALL_SINGLE_SIDE:
    case LOC_SHAPE_WALL_TRI_CORNER:
    case LOC_SHAPE_WALL_TWO_SIDES:
    case LOC_SHAPE_WALL_RECT_CORNER:
        break;
    case LOC_SHAPE_WALL_DECOR_NOOFFSET:
    case LOC_SHAPE_WALL_DECOR_OFFSET:
        build_element->wall_offset_type = WALL_OFFSET_TYPE_STRAIGHT;
        break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_OFFSET:
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_NOOFFSET:
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE:
        build_element->wall_offset_type = WALL_OFFSET_TYPE_DIAGONAL;
        break;
    case LOC_SHAPE_WALL_DIAGONAL:
    case LOC_SHAPE_SCENERY:
    case LOC_SHAPE_SCENERY_DIAGIONAL:
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
    case LOC_SHAPE_FLOOR_DECORATION:
        break;
    }
}

static int
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
    struct SceneElement scene_element = { 0 };

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

    /**
     * Painter
     */
    painter_add_wall(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[map_loc->orientation]);

    return 1;
}

static int
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
    struct SceneElement scene_element = { 0 };

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

    return 1;
}

static int
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
    struct SceneElement scene_element = { 0 };
    struct BuildElement* build_element = NULL;

    int element_one_id = -1;
    int element_two_id = -1;

    int next_orientation = (map_loc->orientation + 1) & 0x3;

    dash_model_one = load_model(
        config_loc,
        scene_builder->models_hmap,
        LOC_SHAPE_WALL_TWO_SIDES,
        // +4 for Mirrored
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

    return 2;
}

static int
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
    struct SceneElement scene_element = { 0 };
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

    return 1;
}

static int
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
    struct SceneElement scene_element = { 0 };
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

    return 1;
}

static int
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
    struct SceneElement scene_element = { 0 };
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

    return 1;
}

// Lumbridge shield
static int
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
    struct SceneElement scene_element = { 0 };
    int element_id = -1;

    int orientation = map_loc->orientation;
    orientation = (orientation) & 0x3;

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
    // scene_model->yaw = 512 * orientation;
    // scene_model->yaw %= 2048;
    // scene_element.dash_position->yaw = 512 * map_loc->orientation;
    scene_element.dash_position->yaw += WALL_DECOR_YAW_ADJUST_DIAGONAL_OUTSIDE;
    scene_element.dash_position->yaw %= 2048;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    // The southwest facing lumbridge shield is orientation 1.
    // This is the shield on the north side of the castle exit.
    // However orientation 1 shows the northeast side...
    // I think the painter has things backwards.
    painter_add_wall_decor(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation],
        THROUGHWALL);

    return 1;
}

// Lumbridge sconce
static int
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
    struct SceneElement scene_element = { 0 };
    int element_id = -1;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        // This is correct.
        LOC_SHAPE_WALL_DECOR_NOOFFSET,
        map_loc->orientation,
        // 0,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;
    // scene_model->yaw = 512 * orientation;
    // scene_model->yaw %= 2048;
    int orientation = map_loc->orientation;
    orientation = (orientation + 2) % 4;

    scene_element.dash_position->yaw = 512 * (map_loc->orientation);
    scene_element.dash_position->yaw += WALL_DECOR_YAW_ADJUST_DIAGONAL_INSIDE;
    scene_element.dash_position->yaw %= 2048;

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

    return 1;
}

static int
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
    struct SceneElement scene_element = { 0 };
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

    return 2;
}

static int
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
    struct SceneElement scene_element = { 0 };
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

    return 1;
}

static int
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
    struct SceneElement scene_element = { 0 };
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
    if( map_loc->shape_select == LOC_SHAPE_SCENERY_DIAGIONAL )
        dash_position->yaw = 256;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_normal_scenery(
        scene_builder->painter, offset->x, offset->z, offset->level, element_id, size_x, size_z);

    return 1;
}

static int
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
    struct SceneElement scene_element = { 0 };
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

    return 1;
}

static int
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
    struct SceneElement scene_element = { 0 };
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

    return 1;
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
    struct SceneElement* scene_element = NULL;
    struct TerrainGridOffsetFromSW offset;
    struct DashModel* dash_model = NULL;
    struct DashPosition* dash_position = NULL;
    struct SceneSceneryTile* scenery_tile = NULL;
    struct BuildTile* build_ile = NULL;
    struct BuildElement* build_element = NULL;
    int elements_pushed = 0;

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
        elements_pushed = scenery_add_wall_single(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_TRI_CORNER:
        elements_pushed = scenery_add_wall_tri_corner(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_TWO_SIDES:
        elements_pushed = scenery_add_wall_two_sides(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_RECT_CORNER:
        elements_pushed = scenery_add_wall_rect_corner(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    // case LOC_SHAPE_WALL_DECOR_NOOFFSET:
    //     elements_pushed = scenery_add_wall_decor_nooffset(
    //         scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
    //     break;
    // case LOC_SHAPE_WALL_DECOR_OFFSET:
    //     elements_pushed = scenery_add_wall_decor_offset(
    //         scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
    //     break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_OFFSET:
        elements_pushed = scenery_add_wall_decor_diagonal_offset(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    // case LOC_SHAPE_WALL_DECOR_DIAGONAL_NOOFFSET:
    //     elements_pushed = scenery_add_wall_decor_diagonal_nooffset(
    //         scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
    //     break;
    // case LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE:
    //     elements_pushed = scenery_add_wall_decor_diagonal_double(
    //         scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
    //     break;
    case LOC_SHAPE_WALL_DIAGONAL:
        elements_pushed = scenery_add_wall_diagonal(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_SCENERY:
    case LOC_SHAPE_SCENERY_DIAGIONAL:
        elements_pushed =
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
        elements_pushed =
            scenery_add_roof(scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_FLOOR_DECORATION:
        elements_pushed = scenery_add_floor_decoration(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    }

    for( int i = 0; i < elements_pushed; i++ )
    {
        int element_idx = scenery->elements_length - elements_pushed + i;
        scene_element = scene_element_at(scenery, element_idx);

        build_element = build_grid_element_at(scene_builder->build_grid, element_idx);

        init_build_element_from_config_loc(build_element, config_loc, map_loc->orientation);

        scenery_set_wall_offsets(build_element, map_loc->shape_select);
    }
}

struct TileCoord
{
    int x;
    int z;
    int level;
};

static int
gather_adjacent_tiles(
    struct TileCoord* out,
    int out_size,
    struct SceneScenery* scenery,
    int tile_x,
    int tile_z,
    int tile_level,
    int tile_size_x,
    int tile_size_z)
{
    int min_tile_x = tile_x;
    int max_tile_x = tile_x + tile_size_x;
    int min_tile_z = tile_z - 1;
    int max_tile_z = tile_z + tile_size_z;

    int count = 0;
    for( int level = 0; level <= tile_level + 1; level++ )
    {
        for( int x = min_tile_x; x <= max_tile_x; x++ )
        {
            for( int z = min_tile_z; z <= max_tile_z; z++ )
            {
                if( (x == tile_x && z == tile_z && level == tile_level) )
                    continue;
                if( x < 0 || z < 0 || x >= scenery->tile_width_x || z >= scenery->tile_width_z ||
                    level < 0 || level >= MAP_TERRAIN_LEVELS )
                    continue;

                assert(count < out_size);
                out[count++] = (struct TileCoord){ .x = x, .z = z, .level = level };
            }
        }
    }

    return count;
}

static int
gather_sharelight_models(
    struct BuildGrid* build_grid,
    struct BuildTile* build_tile,
    int* out,
    int out_size)
{
    int count = 0;
    struct BuildElement* build_element = NULL;
    if( build_tile->wall_a_element_idx != -1 )
    {
        build_element = build_grid_element_at(build_grid, build_tile->wall_a_element_idx);
        assert(build_element && "Element must be valid");

        if( build_element->sharelight )
            out[count++] = build_tile->wall_a_element_idx;
    }

    if( build_tile->wall_b_element_idx != -1 )
    {
        build_element = build_grid_element_at(build_grid, build_tile->wall_b_element_idx);
        assert(build_element && "Element must be valid");

        if( build_element->sharelight )
            out[count++] = build_tile->wall_b_element_idx;
    }

    // if( tile->ground_decor != -1 )
    // {
    //     assert(tile->ground_decor < loc_pool_size);
    //     loc = &loc_pool[tile->ground_decor];

    //     model = tile_ground_decor_model_nullable(models, loc);
    //     if( model && model->sharelight )
    //         out[count++] = model;
    // }

    // for( int i = 0; i < tile->locs_length; i++ )
    // {
    //     loc = &loc_pool[tile->locs[i]];
    //     model = tile_scenery_model_nullable(models, loc);
    //     if( model && model->sharelight )
    //         out[count++] = model;

    //     model = tile_wall_model_nullable(models, loc, 1);
    //     if( model && model->sharelight )
    //         out[count++] = model;

    //     model = tile_wall_model_nullable(models, loc, 0);
    //     if( model && model->sharelight )
    //         out[count++] = model;

    //     model = tile_ground_decor_model_nullable(models, loc);
    //     if( model && model->sharelight )
    //         out[count++] = model;
    // }

    assert(count <= out_size);
    return count;
}

static int g_merge_index = 0;
static int g_vertex_a_merge_index[10000] = { 0 };
static int g_vertex_b_merge_index[10000] = { 0 };

static void
merge_normals(
    struct DashModel* model,
    struct LightingNormal* vertex_normals,
    struct LightingNormal* lighting_vertex_normals,
    struct DashModel* other_model,
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

static struct DashModelNormals*
model_normals_new_copy(struct DashModelNormals* normals)
{
    struct DashModelNormals* aliased_normals = malloc(sizeof(struct DashModelNormals));
    memset(aliased_normals, 0, sizeof(struct DashModelNormals));

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

static void
dashmodel_alias_normals(
    struct BuildElement* build_element,
    struct DashModelNormals* model_normals)
{
    if( build_element->aliased_lighting_normals )
        return;

    build_element->aliased_lighting_normals = model_normals_new_copy(model_normals);
}

static void
build_lighting(
    struct SceneBuilder* scene_builder,
    struct TerrainGrid* terrain_grid,
    struct Scene* scene)
{
#define SHARELIGHT_MODELS_COUNT 40
    struct BuildTile* build_tile = NULL;
    struct TileCoord adjacent_tiles[SHARELIGHT_MODELS_COUNT] = { 0 };
    struct BuildElement* sharelight_build_element = NULL;
    struct BuildElement* adjacent_build_element = NULL;
    struct SceneElement* sharelight_scene_element = NULL;
    struct SceneElement* adjacent_scene_element = NULL;
    struct BuildTile* sharelight_build_tile = NULL;
    struct BuildTile* adjacent_build_tile = NULL;
    int sharelight_models_count = 0;
    int adjacent_sharelight_elements_count = 0;
    int sharelight_elements[SHARELIGHT_MODELS_COUNT] = { 0 };
    int adjacent_sharelight_elements[SHARELIGHT_MODELS_COUNT] = { 0 };
    for( int sx = 0; sx < scene->tile_width_x; sx++ )
    {
        for( int sz = 0; sz < scene->tile_width_z; sz++ )
        {
            for( int slevel = 0; slevel < MAP_TERRAIN_LEVELS; slevel++ )
            {
                build_tile = build_grid_tile_at(scene_builder->build_grid, sx, sz, slevel);
                assert(build_tile && "Build tile must be valid");

                sharelight_models_count = gather_sharelight_models(
                    scene_builder->build_grid,
                    build_tile,
                    sharelight_elements,
                    SHARELIGHT_MODELS_COUNT);

                for( int i = 0; i < sharelight_models_count; i++ )
                {
                    sharelight_build_element =
                        build_grid_element_at(scene_builder->build_grid, sharelight_elements[i]);

                    int adjacent_tiles_count = gather_adjacent_tiles(
                        adjacent_tiles,
                        SHARELIGHT_MODELS_COUNT,
                        scene->scenery,
                        sx,
                        sz,
                        slevel,
                        sharelight_build_element->size_x,
                        sharelight_build_element->size_z);

                    for( int j = 0; j < adjacent_tiles_count; j++ )
                    {
                        struct TileCoord adjacent_tile_coord = adjacent_tiles[j];
                        adjacent_build_tile = build_grid_tile_at(
                            scene_builder->build_grid,
                            adjacent_tile_coord.x,
                            adjacent_tile_coord.z,
                            adjacent_tile_coord.level);

                        adjacent_sharelight_elements_count = gather_sharelight_models(
                            scene_builder->build_grid,
                            adjacent_build_tile,
                            adjacent_sharelight_elements,
                            SHARELIGHT_MODELS_COUNT);

                        for( int k = 0; k < adjacent_sharelight_elements_count; k++ )
                        {
                            adjacent_build_element = build_grid_element_at(
                                scene_builder->build_grid, adjacent_sharelight_elements[k]);
                            assert(
                                adjacent_build_element && "Adjacent build element must be valid");

                            int check_offset_x = (adjacent_tile_coord.x - sx) * 128 +
                                                 (adjacent_build_element->size_x -
                                                  sharelight_build_element->size_x) *
                                                     64;

                            int check_offset_y = (adjacent_tile_coord.z - sz) * 128 +
                                                 (adjacent_build_element->size_z -
                                                  sharelight_build_element->size_z) *
                                                     64;
                            int check_offset_level = adjacent_build_tile->height_center -
                                                     sharelight_build_tile->height_center;

                            dashmodel_alias_normals(
                                sharelight_build_element,
                                sharelight_scene_element->dash_model->normals);
                            dashmodel_alias_normals(
                                adjacent_build_element,
                                adjacent_scene_element->dash_model->normals);

                            merge_normals(
                                sharelight_scene_element->dash_model,
                                sharelight_scene_element->dash_model->normals
                                    ->lighting_vertex_normals,
                                sharelight_build_element->aliased_lighting_normals
                                    ->lighting_vertex_normals,
                                adjacent_scene_element->dash_model,
                                adjacent_scene_element->dash_model->normals
                                    ->lighting_vertex_normals,
                                adjacent_build_element->aliased_lighting_normals
                                    ->lighting_vertex_normals,
                                check_offset_x,
                                check_offset_level,
                                check_offset_y);
                        }
                    }
                }
            }
        }
    }

    for( int i = 0; i < scene->scenery->elements_length; i++ )
    {
        struct SceneElement* scene_element = &scene->scenery->elements[i];
        struct BuildElement* build_element = build_grid_element_at(scene_builder->build_grid, i);
        assert(scene_element && "Element must be valid");

        if( !scene_element->dash_model )
            continue;

        int light_ambient = 64;
        int light_attenuation = 768;
        int lightsrc_x = -50;
        int lightsrc_y = -10;
        int lightsrc_z = -50;

        light_ambient += build_element->light_ambient;
        light_attenuation += build_element->light_attenuation;

        int light_magnitude =
            (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
        int attenuation = (light_attenuation * light_magnitude) >> 8;

        if( build_element->sharelight )
        {
            dashmodel_alias_normals(build_element, scene_element->dash_model->normals);
        }

        apply_lighting(
            scene_element->dash_model->lighting->face_colors_hsl_a,
            scene_element->dash_model->lighting->face_colors_hsl_b,
            scene_element->dash_model->lighting->face_colors_hsl_c,
            build_element->sharelight
                ? build_element->aliased_lighting_normals->lighting_vertex_normals
                : scene_element->dash_model->normals->lighting_vertex_normals,
            scene_element->dash_model->normals->lighting_face_normals,
            scene_element->dash_model->face_indices_a,
            scene_element->dash_model->face_indices_b,
            scene_element->dash_model->face_indices_c,
            scene_element->dash_model->face_count,
            scene_element->dash_model->face_colors,
            scene_element->dash_model->face_alphas,
            scene_element->dash_model->face_textures,
            scene_element->dash_model->face_infos,
            light_ambient,
            attenuation,
            lightsrc_x,
            lightsrc_y,
            lightsrc_z);
    }
}

static void
apply_wall_offsets(
    struct SceneBuilder* scene_builder,
    struct TerrainGrid* terrain_grid,
    struct Scene* scene)
{
    for( int i = 0; i < scene->scenery->elements_length; i++ )
    {
        struct SceneElement* scene_element = scene_element_at(scene->scenery, i);
        struct BuildElement* build_element = build_grid_element_at(scene_builder->build_grid, i);
        assert(scene_element && "Element must be valid");

        if( !scene_element->dash_model || build_element->wall_offset_type == WALL_OFFSET_TYPE_NONE )
            continue;

        if( build_element->wall_offset != 0 )
            calculate_wall_decor_offset(
                scene_element->dash_position,
                build_element->rotation,
                build_element->wall_offset + 45,
                build_element->wall_offset_type == WALL_OFFSET_TYPE_DIAGONAL);
    }
}

static void
build_scene_scenery(
    struct SceneBuilder* scene_builder,
    struct TerrainGrid* terrain_grid,
    struct Scene* scene)
{
    struct DashMapIter* iter = dashmap_iter_new(scene_builder->map_locs_hmap);
    struct MapLocsEntry* map_locs_entry = NULL;
    struct CacheMapLocs* map_locs = NULL;
    struct CacheMapLoc* map_loc = NULL;
    int mapxz = 0;

    while( (map_locs_entry = (struct MapLocsEntry*)dashmap_iter_next(iter)) )
    {
        map_locs = map_locs_entry->locs;
        assert(map_locs && "Map locs must be valid");

        for( int i = 0; i < map_locs->locs_count; i++ )
        {
            map_loc = &map_locs->locs[i];
            assert(map_loc && "Map loc must be valid");

            scenery_add(
                scene_builder,
                terrain_grid,
                map_locs_entry->mapx,
                map_locs_entry->mapz,
                map_loc,
                scene->scenery);
        }
    }

    build_lighting(scene_builder, terrain_grid, scene);

    apply_wall_offsets(scene_builder, terrain_grid, scene);
}

#endif