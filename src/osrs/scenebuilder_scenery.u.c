#ifndef SCENE_BUILDER_SCENERY_U_C
#define SCENE_BUILDER_SCENERY_U_C

#include "dashlib.h"
#include "graphics/dash.h"
#include "graphics/lighting.h"
#include "model_transforms.h"
#include "osrs/dash_utils.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "painters.h"
#include "scene.h"

// clang-format off
#include "scenebuilder.u.c"
#include "scenebuilder_sharelight.u.c"
#include "terrain_grid.u.c"
// clang-format on

#include <assert.h>

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
    bool scaled = loc->resize_x != 128 || loc->resize_height != 128 || loc->resize_z != 128;
    bool translated = loc->offset_x != 0 || loc->offset_y != 0 || loc->offset_z != 0;
    // TODO: handle the other contoured ground types.
    bool hillskewed = loc->contour_ground_type == 1;

    if( mirrored )
        model_transform_mirror(model);

    if( oriented )
        model_transform_orient(model, orientation);

    if( scaled )
        model_transform_scale(model, loc->resize_x, loc->resize_z, loc->resize_height);

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
    int rotation,
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
        rotation = 0;
    }

    apply_transforms(
        loc_config,
        model,
        rotation,
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

static struct SceneAnimation*
load_model_animations(
    struct CacheConfigLocation* loc_config,

    struct DashMap* sequences_configmap,
    struct DashMap* frames_hmap)
{
    struct SceneAnimation* scene_animation = NULL;
    struct FrameEntry* frame_entry = NULL;
    struct FramemapEntry* framemap_entry = NULL;

    struct DashFrame* dash_frame = NULL;
    struct DashFramemap* dash_framemap = NULL;

    if( loc_config->seq_id == -1 || !sequences_configmap )
        return NULL;

    scene_animation = malloc(sizeof(struct SceneAnimation));
    memset(scene_animation, 0, sizeof(struct SceneAnimation));

    struct CacheConfigSequence* sequence =
        (struct CacheConfigSequence*)configmap_get(sequences_configmap, loc_config->seq_id);
    assert(sequence);
    assert(sequence->frame_lengths);

    scene_animation->config_sequence = sequence;
    if( loc_config->seq_random_start )
    {
        int frame_index = rand() % sequence->frame_count;
        int cycle_start = rand() % sequence->frame_lengths[frame_index];
        scene_animation->frame_index = frame_index;
        scene_animation->cycle = cycle_start;
    }
    else
    {
        scene_animation->frame_index = 0;
    }

    for( int i = 0; i < sequence->frame_count; i++ )
    {
        // Get the frame definition ID from the second 2 bytes of the sequence frame ID The
        //     first 2 bytes are the sequence ID,
        //     the second 2 bytes are the frame file ID
        int frame_id = sequence->frame_ids[i];

        frame_entry = (struct FrameEntry*)dashmap_search(frames_hmap, &frame_id, DASHMAP_FIND);
        assert(frame_entry);

        scene_animation_push_frame(
            scene_animation,
            dashframe_new_from_cache_frame(frame_entry->frame),
            dashframemap_new_from_cache_framemap(frame_entry->frame->_framemap));
    }

    return scene_animation;
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
push_scene_action(
    struct SceneElement* scene_element,
    char* action)
{
    assert(action != NULL);
    struct SceneAction* scene_action = malloc(sizeof(struct SceneAction));
    memset(scene_action, 0, sizeof(struct SceneAction));

    assert(strlen(action) < sizeof(scene_action->action) - 1);
    memset(scene_action->action, 0, sizeof(scene_action->action));

    memcpy(scene_action->action, action, strlen(action));

    if( !scene_element->actions )
    {
        scene_element->actions = scene_action;
    }
    else
    {
        struct SceneAction* current = scene_element->actions;
        while( current->next )
        {
            current = current->next;
        }
        current->next = scene_action;
    }
}

static void
init_scene_element(
    struct SceneElement* scene_element,
    struct CacheConfigLocation* config_loc)
{
    memset(scene_element, 0, sizeof(struct SceneElement));
    scene_element->interactable = config_loc->is_interactive;
    scene_element->config_loc = config_loc;
    scene_element->dash_model = NULL;
    scene_element->dash_position = NULL;
    scene_element->animation = NULL;

    if( config_loc->name )
    {
        assert(strlen(config_loc->name) < sizeof(scene_element->_dbg_name) - 1);

        memset(scene_element->_dbg_name, 0, sizeof(scene_element->_dbg_name));
        memcpy(scene_element->_dbg_name, config_loc->name, strlen(config_loc->name));
    }

    for( int i = 0; i < 10; i++ )
    {
        if( config_loc->actions[i] )
        {
            push_scene_action(scene_element, config_loc->actions[i]);
        }
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
    init_scene_element(&scene_element, config_loc);

    int element_id = -1;

    int rotation = map_loc->orientation;
    int orientation = map_loc->orientation;
    dash_model = load_model(
        config_loc, scene_builder->models_hmap, map_loc->shape_select, rotation, tile_heights);

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
        ROTATION_WALL_TYPE[orientation]);

    /* Shademap */
    if( config_loc->shadowed )
        shademap_set_wall(
            scene_builder->shademap, offset->x, offset->z, offset->level, orientation, 50);
    /**
     * Build grid
     */
    build_grid_tile_add_wall(
        scene_builder->build_grid, offset->x, offset->z, offset->level, WALL_A, element_id);

    build_grid_set_element(
        scene_builder->build_grid, element_id, config_loc, offset, orientation, 1, 1);

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
    init_scene_element(&scene_element, config_loc);

    int element_id = -1;

    int rotation = map_loc->orientation;
    int orientation = map_loc->orientation;
    dash_model = load_model(
        config_loc, scene_builder->models_hmap, map_loc->shape_select, rotation, tile_heights);

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
        ROTATION_WALL_CORNER_TYPE[orientation]);

    /* Shademap */
    if( config_loc->shadowed )
        shademap_set_wall_corner(
            scene_builder->shademap, offset->x, offset->z, offset->level, orientation, 50);

    /**
     * Build grid
     */
    build_grid_set_element(
        scene_builder->build_grid, element_id, config_loc, offset, orientation, 1, 1);
    build_grid_tile_add_wall(
        scene_builder->build_grid, offset->x, offset->z, offset->level, WALL_A, element_id);

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
    struct BuildElement* build_element = NULL;
    struct SceneElement scene_element = { 0 };
    init_scene_element(&scene_element, config_loc);

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

    /**
     * Build grid
     */
    build_grid_set_element(
        scene_builder->build_grid, element_one_id, config_loc, offset, map_loc->orientation, 1, 1);
    build_grid_set_element(
        scene_builder->build_grid, element_two_id, config_loc, offset, next_orientation, 1, 1);

    build_grid_tile_add_wall(
        scene_builder->build_grid, offset->x, offset->z, offset->level, WALL_A, element_one_id);
    build_grid_tile_add_wall(
        scene_builder->build_grid, offset->x, offset->z, offset->level, WALL_B, element_two_id);

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
    init_scene_element(&scene_element, config_loc);
    int element_id = -1;

    if( offset->x == 161 )
    {
        printf("scenery_add_wall_rect_corner: %d, %d\n", offset->x, offset->z);
    }

    int rotation = map_loc->orientation;
    int orientation = map_loc->orientation;
    dash_model = load_model(
        config_loc, scene_builder->models_hmap, LOC_SHAPE_WALL_RECT_CORNER, rotation, tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    // scene_element.dash_position->x += config_loc->offset_x;
    // scene_element.dash_position->z += config_loc->offset_z;
    // scene_element.dash_position->y += config_loc->offset_y;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_wall(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation]);

    /* Shademap */
    if( config_loc->shadowed )
        shademap_set_wall_corner(
            scene_builder->shademap, offset->x, offset->z, offset->level, orientation, 50);

    /**
     * Build grid
     */
    build_grid_set_element(
        scene_builder->build_grid, element_id, config_loc, offset, orientation, 1, 1);

    build_grid_tile_add_wall(
        scene_builder->build_grid, offset->x, offset->z, offset->level, WALL_A, element_id);

    return 1;
}

static int
scenery_add_wall_decor_inside(
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
    init_scene_element(&scene_element, config_loc);
    int element_id = -1;

    int rotation = config_loc->seq_id != -1 ? 0 : map_loc->orientation;
    int orientation = map_loc->orientation;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        tile_heights);

    struct SceneAnimation* scene_animation = load_model_animations(
        config_loc, scene_builder->sequences_configmap, scene_builder->frames_hmap);

    scene_element.animation = scene_animation;

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    if( config_loc->seq_id != -1 )
    {
        scene_element.dash_position->yaw += 512 * orientation;
        scene_element.dash_position->yaw %= 2048;
    }

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_wall_decor(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation],
        0);

    /**
     *  Build grid
     */
    build_grid_set_element(
        scene_builder->build_grid, element_id, config_loc, offset, orientation, 1, 1);

    build_grid_set_decor(scene_builder->build_grid, element_id, DECOR_DISPLACEMENT_KIND_STRAIGHT);

    return 1;
}

static int
scenery_add_wall_decor_outside(
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
    init_scene_element(&scene_element, config_loc);
    int element_id = -1;

    int rotation = config_loc->seq_id != -1 ? 0 : map_loc->orientation;
    int orientation = map_loc->orientation;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        // This is correct.
        LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    struct SceneAnimation* scene_animation = load_model_animations(
        config_loc, scene_builder->sequences_configmap, scene_builder->frames_hmap);

    scene_element.animation = scene_animation;

    if( config_loc->seq_id != -1 )
    {
        scene_element.dash_position->yaw += 512 * orientation;
        scene_element.dash_position->yaw %= 2048;
    }

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_wall_decor(
        scene_builder->painter,
        offset->x,
        offset->z,
        offset->level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation],
        0);

    /**
     *  Build grid
     */
    build_grid_set_element(
        scene_builder->build_grid, element_id, config_loc, offset, orientation, 1, 1);

    build_grid_set_decor(
        scene_builder->build_grid, element_id, DECOR_DISPLACEMENT_KIND_STRAIGHT_ONWALL_OFFSET);

    return 1;
}

// Lumbridge shield
static int
scenery_add_wall_decor_diagonal_outside(
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
    init_scene_element(&scene_element, config_loc);
    int element_id = -1;

    int rotation = config_loc->seq_id != -1 ? 0 : map_loc->orientation;
    int orientation = map_loc->orientation;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        // This is correct.
        LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;
    // scene_model->yaw = 512 * orientation;
    // scene_model->yaw %= 2048;
    // scene_element.dash_position->yaw = 512 * map_loc->orientation;
    scene_element.dash_position->yaw += WALL_DECOR_YAW_ADJUST_DIAGONAL_OUTSIDE;
    if( config_loc->seq_id != -1 )
    {
        scene_element.dash_position->yaw += 512 * orientation;
    }
    scene_element.dash_position->yaw %= 2048;

    struct SceneAnimation* scene_animation = load_model_animations(
        config_loc, scene_builder->sequences_configmap, scene_builder->frames_hmap);

    scene_element.animation = scene_animation;

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

    /**
     * Build grid
     */
    build_grid_set_element(
        scene_builder->build_grid, element_id, config_loc, offset, orientation, 1, 1);

    build_grid_set_decor(
        scene_builder->build_grid, element_id, DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET);

    return 1;
}

// Lumbridge sconce
// Support beams in lumbridge general store along wall.
static int
scenery_add_wall_decor_diagonal_inside(
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
    init_scene_element(&scene_element, config_loc);
    int element_id = -1;

    int rotation = config_loc->seq_id != -1 ? 0 : (map_loc->orientation);
    int orientation = (map_loc->orientation + 2) & 0x3;

    dash_model = load_model(
        config_loc,
        scene_builder->models_hmap,
        // This is correct.
        LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        // 0,
        tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;
    // scene_model->yaw = 512 * orientation;
    // scene_model->yaw %= 2048;
    scene_element.dash_position->yaw += WALL_DECOR_YAW_ADJUST_DIAGONAL_INSIDE;

    if( config_loc->seq_id != -1 )
        scene_element.dash_position->yaw = 512 * (orientation);
    scene_element.dash_position->yaw %= 2048;

    struct SceneAnimation* scene_animation = load_model_animations(
        config_loc, scene_builder->sequences_configmap, scene_builder->frames_hmap);

    scene_element.animation = scene_animation;

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
        THROUGHWALL);

    /**
     * Build grid
     */
    build_grid_set_element(
        scene_builder->build_grid, element_id, config_loc, offset, orientation, 1, 1);

    build_grid_set_decor(scene_builder->build_grid, element_id, DECOR_DISPLACEMENT_KIND_DIAGONAL);

    return 1;
}

// lumbridge windows in castle courtyard walls
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
    init_scene_element(&scene_element, config_loc);
    int element_one_id = -1;
    int element_two_id = -1;

    int outside_orientation = map_loc->orientation;
    int inside_orientation = (map_loc->orientation + 2) & 0x3;

    dash_model_one = load_model(
        config_loc,
        scene_builder->models_hmap,
        // This is correct.
        LOC_SHAPE_WALL_DECOR_INSIDE,
        outside_orientation,
        tile_heights);

    dash_position_one = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model_one;
    scene_element.dash_position = dash_position_one;

    scene_element.dash_position->yaw += WALL_DECOR_YAW_ADJUST_DIAGONAL_OUTSIDE;
    scene_element.dash_position->yaw %= 2048;

    element_one_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_one_id != -1);

    dash_model_two = load_model(
        config_loc,
        scene_builder->models_hmap,
        // This is correct.
        LOC_SHAPE_WALL_DECOR_INSIDE,
        inside_orientation,
        tile_heights);

    dash_position_two = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model_two;
    scene_element.dash_position = dash_position_two;

    scene_element.dash_position->yaw += WALL_DECOR_YAW_ADJUST_DIAGONAL_OUTSIDE;
    scene_element.dash_position->yaw %= 2048;

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

    /**
     * Build grid
     */
    build_grid_set_element(
        scene_builder->build_grid, element_one_id, config_loc, offset, outside_orientation, 1, 1);

    build_grid_set_element(
        scene_builder->build_grid, element_two_id, config_loc, offset, inside_orientation, 1, 1);

    build_grid_set_decor(
        scene_builder->build_grid, element_one_id, DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET);
    build_grid_set_decor(
        scene_builder->build_grid, element_two_id, DECOR_DISPLACEMENT_KIND_DIAGONAL);

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
    init_scene_element(&scene_element, config_loc);
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

    /**
     * Build grid
     */
    build_grid_set_element(
        scene_builder->build_grid, element_id, config_loc, offset, map_loc->orientation, 1, 1);

    build_grid_tile_add_wall(
        scene_builder->build_grid, offset->x, offset->z, offset->level, WALL_A, element_id);

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
    init_scene_element(&scene_element, config_loc);
    int element_id = -1;

    int size_x = config_loc->size_x;
    int size_z = config_loc->size_z;
    if( map_loc->orientation == 1 || map_loc->orientation == 3 )
    {
        int temp = size_x;
        size_x = size_z;
        size_z = temp;
    }

    int rotation = config_loc->seq_id != -1 ? 0 : map_loc->orientation;
    int orientation = map_loc->orientation;

    dash_model = load_model(
        config_loc, scene_builder->models_hmap, LOC_SHAPE_SCENERY, rotation, tile_heights);

    dash_position =
        dash_position_from_offset_wxh(offset, tile_heights->height_center, size_x, size_z);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;
    if( map_loc->shape_select == LOC_SHAPE_SCENERY_DIAGIONAL )
        dash_position->yaw = 256;

    if( config_loc->seq_id != -1 )
    {
        scene_element.dash_position->yaw += 512 * orientation;
        scene_element.dash_position->yaw %= 2048;
    }

    struct SceneAnimation* scene_animation = load_model_animations(
        config_loc, scene_builder->sequences_configmap, scene_builder->frames_hmap);

    scene_element.animation = scene_animation;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_normal_scenery(
        scene_builder->painter, offset->x, offset->z, offset->level, element_id, size_x, size_z);

    /* Shademap */
    int shade = size_x * size_z * 11;
    if( shade > 30 )
        shade = 30;
    if( config_loc->shadowed )
        shademap_set_sized(
            scene_builder->shademap, offset->x, offset->z, offset->level, size_x, size_z, shade);

    /**
     * Build grid
     */
    build_grid_set_element(
        scene_builder->build_grid, element_id, config_loc, offset, orientation, size_x, size_z);

    build_grid_tile_add_element(
        scene_builder->build_grid, offset->x, offset->z, offset->level, element_id);

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
    init_scene_element(&scene_element, config_loc);
    int element_id = -1;

    int rotation = map_loc->orientation;
    int orientation = map_loc->orientation;

    dash_model = load_model(
        config_loc, scene_builder->models_hmap, map_loc->shape_select, rotation, tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_normal_scenery(
        scene_builder->painter, offset->x, offset->z, offset->level, element_id, 1, 1);

    /**
     * Build grid
     */
    build_grid_set_element(
        scene_builder->build_grid, element_id, config_loc, offset, orientation, 1, 1);

    build_grid_tile_add_element(
        scene_builder->build_grid, offset->x, offset->z, offset->level, element_id);
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
    init_scene_element(&scene_element, config_loc);
    int element_id = -1;

    int rotation = map_loc->orientation;
    int orientation = map_loc->orientation;
    dash_model = load_model(
        config_loc, scene_builder->models_hmap, map_loc->shape_select, rotation, tile_heights);

    dash_position = dash_position_from_offset_1x1(offset, tile_heights->height_center);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position;

    element_id = scene_scenery_push_element_move(scenery, &scene_element);
    assert(element_id != -1);

    painter_add_ground_decor(
        scene_builder->painter, offset->x, offset->z, offset->level, element_id);

    /**
     * Build grid
     */
    build_grid_set_element(
        scene_builder->build_grid, element_id, config_loc, offset, orientation, 1, 1);

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
    int elements_pushed = 0;

    struct CacheConfigLocationEntry* config_loc_entry = NULL;
    config_loc_entry = (struct CacheConfigLocationEntry*)dashmap_search(
        scene_builder->config_locs_hmap, &map_loc->loc_id, DASHMAP_FIND);
    printf(
        "config_loc_entry->id: %d == %d == %d\n",
        config_loc_entry->id,
        map_loc->loc_id,
        config_loc_entry->config_loc->_id);
    assert(config_loc_entry && "Config loc must be found in hmap");
    assert(
        config_loc_entry->id == map_loc->loc_id &&
        config_loc_entry->config_loc->_id == map_loc->loc_id);

    config_loc = config_loc_entry->config_loc;
    assert(config_loc && "Config loc must be valid");

    // config_loc = (struct CacheConfigLocation*)configmap_get(
    //     scene_builder->config_locs_configmap, map_loc->loc_id);
    // assert(config_loc && "Config loc must be found in configmap");

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
    case LOC_SHAPE_WALL_DECOR_INSIDE:
        elements_pushed = scenery_add_wall_decor_inside(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_DECOR_OUTSIDE:
        elements_pushed = scenery_add_wall_decor_outside(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_OUTSIDE:
        elements_pushed = scenery_add_wall_decor_diagonal_outside(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_INSIDE:
        elements_pushed = scenery_add_wall_decor_diagonal_inside(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE:
        elements_pushed = scenery_add_wall_decor_diagonal_double(
            scene_builder, &offset, &tile_heights, map_loc, config_loc, scenery);
        break;
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

        if( !scene_element->dash_model ||
            build_element->decor_displacement_kind == DECOR_DISPLACEMENT_KIND_NONE )
            continue;

        struct BuildTile* tile = build_grid_tile_at(
            scene_builder->build_grid, build_element->sx, build_element->sz, build_element->slevel);

        if( tile->wall_element_a_idx == -1 )
            continue;

        struct BuildElement* wall_element =
            build_grid_element_at(scene_builder->build_grid, tile->wall_element_a_idx);
        // INSIDE is unaffected by wall offset.
        int offset = 0;
        int diagonal = false;
        switch( build_element->decor_displacement_kind )
        {
        case DECOR_DISPLACEMENT_KIND_STRAIGHT_ONWALL_OFFSET:
            offset += wall_element->wall_offset;
            break;
        case DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET:
            // I'm not sure why this math is like this but it works.
            offset += (wall_element->wall_offset / 16) * 8 + (45);
            diagonal = true;
            break;
        case DECOR_DISPLACEMENT_KIND_STRAIGHT:
            offset += 0;
            break;
        case DECOR_DISPLACEMENT_KIND_DIAGONAL:
            offset += 45;
            diagonal = true;
            break;
        }

        calculate_wall_decor_offset(
            scene_element->dash_position, build_element->rotation, offset, diagonal);
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

            // TODO: shape_select == 10 causes issues. I think it is a changing loc
            if( map_loc->shape_select == 10 )
                continue;

            scenery_add(
                scene_builder,
                terrain_grid,
                map_locs_entry->mapx,
                map_locs_entry->mapz,
                map_loc,
                scene->scenery);
        }
    }

    // Adjust bridges.
    /**
     * Bridges are adjusted from an upper level.
     *
     * The "bridge_tile" is actually the tiles below the bridge.
     * The bridge itself is taken from the level above.
     *
     * E.g.
     *
     * Buffer Level 0: Tile := (Water and Bridge Walls), Bridge := Nothing
     * Buffer Level 1: Tile := (Bridge Walking Surface and Walls)
     * Buffer Level 2: Nothing
     * Buffer Level 3: Nothing
     *
     * After this adjustment,
     *
     * Buffer Level 0: Tile := (Previous Level 1),
     * Buffer Level 1: Nothing
     * Buffer Level 2: Nothing
     * Buffer Level 3: Tile := (Previous Level 0)
     */
    struct CacheMapFloor* floor = NULL;
    struct PaintersTile bridge_tile_tmp = { 0 };
    for( int x = 0; x < scene->tile_width_x; x++ )
    {
        for( int z = 0; z < scene->tile_width_z; z++ )
        {
            floor = tile_from_sw_origin(terrain_grid, x, z, 1);
            if( (floor->settings & FLOFLAG_BRIDGE) != 0 )
            {
                bridge_tile_tmp = *painter_tile_at(scene_builder->painter, x, z, 0);

                /**
                 * Shift tile definitions down
                 */
                for( int level = 0; level < painter_max_levels(scene_builder->painter) - 1;
                     level++ )
                {
                    painter_tile_copyto(
                        scene_builder->painter,
                        // From
                        x,
                        z,
                        level + 1,
                        // To
                        x,
                        z,
                        level);

                    painter_tile_set_draw_level(scene_builder->painter, x, z, level, level);
                }

                // Use the newly unused tile on level 3 as the bridge slot.
                *painter_tile_at(scene_builder->painter, x, z, 3) = bridge_tile_tmp;
                painter_tile_set_bridge(scene_builder->painter, x, z, 0, x, z, 3);
            }
        }
    }

    // Here:
    // Build model lighting
    // 1. Assign and share normals for SceneModels
    // 2. Scene merge normals of abutting locs.
    // 3. Compute lighting
    sharelight_build_scene(scene_builder, terrain_grid, scene);

    apply_wall_offsets(scene_builder, terrain_grid, scene);
}

#endif