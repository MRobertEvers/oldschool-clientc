#ifndef WORLD_SCENERY_U_C
#define WORLD_SCENERY_U_C

#include "dash_utils.h"
#include "model_transforms.h"
#include "world.h"

// clang-format off
#include "_light_model_default.u.c"
// clang-format on

#define WALL_DECOR_YAW_ADJUST 256

static const int WALL_DECOR_ROTATION_OFFSET_X[] = { 1, 0, -1, 0 };
static const int WALL_DECOR_ROTATION_OFFSET_Z[] = { 0, -1, 0, 1 };
static const int WALL_DECOR_ROTATION_DIAGONAL_OFFSET_X[] = { 1, -1, -1, 1 };
static const int WALL_DECOR_ROTATION_DIAGONAL_OFFSET_Z[] = { -1, -1, 1, 1 };

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

static const int ROTATION_WALL_TYPE[] = {
    WALL_SIDE_WEST, WALL_SIDE_NORTH, WALL_SIDE_EAST, WALL_SIDE_SOUTH
};
static const int ROTATION_WALL_CORNER_TYPE[] = {
    WALL_CORNER_NORTHWEST,
    WALL_CORNER_NORTHEAST,
    WALL_CORNER_SOUTHEAST,
    WALL_CORNER_SOUTHWEST,
};

static void
scenery_element_position_init(
    struct World* world,
    struct EntitySceneCoord* entity_scene_coord,
    struct EntitySceneElement* entity_scene_element,
    int size_x,
    int size_z)
{
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity_scene_element->element_id);

    struct HeightmapHeights heights = { 0 };
    heightmap_get_heights_sized(
        world->heightmap,
        entity_scene_coord->sx,
        entity_scene_coord->sz,
        entity_scene_coord->slevel,
        size_x,
        size_z,
        &heights);

    int tile_x = entity_scene_coord->sx;
    int tile_z = entity_scene_coord->sz;
    int tile_level = entity_scene_coord->slevel;
    scene_element->dash_position = dashposition_new();
    scene_element->dash_position->x = 128 * tile_x + 64 * (size_x);
    scene_element->dash_position->z = 128 * tile_z + 64 * (size_z);
    scene_element->dash_position->y = heights.height_center;
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
    int old_revision)
{
    // This should never be called on a shared model.
    assert((model->_flags & CMODEL_FLAG_SHARED) == 0);

    for( int i = 0; i < loc->recolor_count; i++ )
    {
        model_transform_recolor(model, loc->recolors_from[i], loc->recolors_to[i]);

        // From rsmapviewer
        // const retexture =
        // locType.cacheInfo.game === "runescape" && locType.cacheInfo.revision <= 464;
        if( old_revision )
        {
            model_transform_retexture(model, loc->recolors_from[i], loc->recolors_to[i]);
        }
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

    // if( hillskewed )
    //     model_transform_hillskew(model, sw_height, se_height, ne_height, nw_height);
}

static void
apply_contour_ground(
    struct World* world,
    struct EntitySceneCoord* entity_scene_coord,
    struct CacheModel* model,
    int contour_ground_type,
    int contour_ground_param)
{
    if( contour_ground_type == 1 )
    {
        struct HeightmapHeights heights = { 0 };
        heightmap_get_heights_sized(
            world->heightmap,
            entity_scene_coord->sx,
            entity_scene_coord->sz,
            entity_scene_coord->slevel,
            1,
            1,
            &heights);

        model_transform_hillskew(
            model, heights.sw_height, heights.se_height, heights.ne_height, heights.nw_height);
    }
}

static void
world_load_scenery_model(
    struct World* world,
    struct MapBuildLocEntity* entity,
    int shape_select,
    int rotation,
    struct CacheConfigLocation* config_loc)
{
    struct CacheModel* model = NULL;

    int model_ids[10];
    int model_ids_count = 0;

    struct CacheModel* models[10];
    int models_count = 0;

    int* shapes = config_loc->shapes;
    int** model_id_sets = config_loc->models;
    int* lengths = config_loc->lengths;
    int shapes_and_model_count = config_loc->shapes_and_model_count;

    // In old dat caches, the shape_select matched the loctype.
    // if( !shapes || shapes[0] == 10 || shapes[0] == 11 )
    if( !shapes )
    {
        int count = lengths[0];

        for( int i = 0; i < count; i++ )
        {
            int model_id = model_id_sets[0][i];
            assert(model_id);

            model = buildcachedat_get_model(world->buildcachedat, model_id);
            assert(model);

            models[models_count] = model;
            model_ids[models_count] = model_id;
            models_count++;
        }
    }
    else
    {
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

                    model = buildcachedat_get_model(world->buildcachedat, model_id);
                    assert(model);

                    models[models_count] = model;
                    model_ids[models_count] = model_id;
                    models_count++;
                    found = true;
                }
            }
        }
        assert(found);
    }

    assert(models_count > 0);

    if( models_count > 1 )
    {
        model = model_new_merge(models, models_count);
    }
    else
    {
        model = model_new_copy(models[0]);
    }

    struct EntitySceneCoord* entity_scene_coord = &entity->scene_coord;
    struct EntitySceneElement* entity_scene_element = &entity->scene_element;
    struct DashModel* dash_model = NULL;
    struct Scene2Element* scene_element = NULL;

    apply_transforms(config_loc, model, rotation, true);
    apply_contour_ground(
        world,
        entity_scene_coord,
        model,
        config_loc->contour_ground_type,
        config_loc->contour_ground_param);

    dash_model = dashmodel_new_from_cache_model(model);
    model_free(model);

    _light_model_default(dash_model, config_loc->contrast, config_loc->ambient);

    scene_element = scene2_element_at(world->scene2, entity_scene_element->element_id);

    scene_element->dash_model = dash_model;
}

static void
scenery_add_wall_single(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id = scene2_element_acquire(world->scene2, entity->entity_id);

    int rotation = map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(world, entity, LOC_SHAPE_WALL_SINGLE_SIDE, rotation, config_loc);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element, 1, 1);

    painter_add_wall(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation]);

    decor_buildmap_set_wall_offset(
        world->decor_buildmap,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        config_loc->wall_width);

    if( config_loc->sharelight )
    {
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
    }
}

static void
scenery_add_wall_tri_corner(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id = scene2_element_acquire(world->scene2, entity->entity_id);

    int rotation = map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(world, entity, LOC_SHAPE_WALL_TRI_CORNER, rotation, config_loc);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element, 1, 1);

    painter_add_wall(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation]);

    decor_buildmap_set_wall_offset(
        world->decor_buildmap,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        config_loc->wall_width);

    if( config_loc->sharelight )
    {
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
    }
}

static void
scenery_add_wall_two_sides(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id = scene2_element_acquire(world->scene2, entity->entity_id);
    entity->scene_element_two.element_id = scene2_element_acquire(world->scene2, entity->entity_id);

    int orientation = map_tile->orientation;
    // +4 for Mirrored
    int rotation = map_tile->orientation + 4;

    int next_orientation = (orientation + 1) & 0x3;
    int next_rotation = (rotation + 1) & 0x3;

    world_load_scenery_model(world, entity, LOC_SHAPE_WALL_TWO_SIDES, rotation, config_loc);
    world_load_scenery_model(world, entity, LOC_SHAPE_WALL_TWO_SIDES, next_rotation, config_loc);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element, 1, 1);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element_two, 1, 1);

    painter_add_wall(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation]);
    painter_add_wall(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element_two.element_id,
        WALL_B,
        ROTATION_WALL_TYPE[next_orientation]);

    decor_buildmap_set_wall_offset(
        world->decor_buildmap,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        config_loc->wall_width);

    if( config_loc->sharelight )
    {
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element_two.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
    }
}

static void
scenery_add_wall_rect_corner(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id = scene2_element_acquire(world->scene2, entity->entity_id);

    int rotation = map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(world, entity, LOC_SHAPE_WALL_RECT_CORNER, rotation, config_loc);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element, 1, 1);

    painter_add_wall(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation]);

    decor_buildmap_set_wall_offset(
        world->decor_buildmap,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        config_loc->wall_width);

    if( config_loc->sharelight )
    {
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
    }
}

static void
scenery_add_wall_decor_inside(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id = scene2_element_acquire(world->scene2, entity->entity_id);

    int rotation = config_loc->seq_id != -1 ? 0 : map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(world, entity, LOC_SHAPE_WALL_DECOR_INSIDE, rotation, config_loc);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element, 1, 1);
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity->scene_element.element_id);
    if( config_loc->seq_id != -1 )
    {
        scene_element->dash_position->yaw += 512 * orientation;
    }
    scene_element->dash_position->yaw %= 2048;

    painter_add_wall_decor(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation],
        0);

    decor_buildmap_add_element(
        world->decor_buildmap,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        orientation,
        DECOR_DISPLACEMENT_KIND_STRAIGHT);

    if( config_loc->sharelight )
    {
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
    }
}

static void
scenery_add_wall_decor_outside(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id = scene2_element_acquire(world->scene2, entity->entity_id);

    int rotation = config_loc->seq_id != -1 ? 0 : map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(world, entity, LOC_SHAPE_WALL_DECOR_INSIDE, rotation, config_loc);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element, 1, 1);

    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity->scene_element.element_id);
    if( config_loc->seq_id != -1 )
    {
        scene_element->dash_position->yaw += 512 * orientation;
    }
    scene_element->dash_position->yaw %= 2048;

    painter_add_wall_decor(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation],
        0);

    decor_buildmap_add_element(
        world->decor_buildmap,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        orientation,
        DECOR_DISPLACEMENT_KIND_STRAIGHT_ONWALL_OFFSET);

    if( config_loc->sharelight )
    {
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
    }
}

// Lumbridge shield
static void
scenery_add_wall_decor_diagonal_outside(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id = scene2_element_acquire(world->scene2, entity->entity_id);

    int rotation = config_loc->seq_id != -1 ? 0 : map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(world, entity, LOC_SHAPE_WALL_DECOR_INSIDE, rotation, config_loc);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element, 1, 1);

    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity->scene_element.element_id);
    scene_element->dash_position->yaw += WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
    {
        scene_element->dash_position->yaw += 512 * orientation;
    }
    scene_element->dash_position->yaw %= 2048;

    painter_add_wall_decor(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation],
        THROUGHWALL);

    decor_buildmap_add_element(
        world->decor_buildmap,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        orientation,
        DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET);

    if( config_loc->sharelight )
    {
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
    }
}

static void
scenery_add_wall_decor_diagonal_inside(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id = scene2_element_acquire(world->scene2, entity->entity_id);

    int rotation = config_loc->seq_id != -1 ? 0 : map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(world, entity, LOC_SHAPE_WALL_DECOR_INSIDE, rotation, config_loc);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element, 1, 1);

    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity->scene_element.element_id);
    scene_element->dash_position->yaw += WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
    {
        scene_element->dash_position->yaw += 512 * orientation;
    }
    scene_element->dash_position->yaw %= 2048;

    painter_add_wall_decor(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation],
        THROUGHWALL);

    decor_buildmap_add_element(
        world->decor_buildmap,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        orientation,
        DECOR_DISPLACEMENT_KIND_DIAGONAL);

    if( config_loc->sharelight )
    {
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);

        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element_two.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
    }
}

static void
scenery_add_wall_decor_diagonal_double(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id = scene2_element_acquire(world->scene2, entity->entity_id);
    entity->scene_element_two.element_id = scene2_element_acquire(world->scene2, entity->entity_id);

    int outside_rotation = config_loc->seq_id != -1 ? 0 : map_tile->orientation;
    int outside_orientation = map_tile->orientation;
    int inside_rotation = config_loc->seq_id != -1 ? 0 : (outside_orientation + 2) & 0x3;
    int inside_orientation = (outside_orientation + 2) & 0x3;

    world_load_scenery_model(
        world, entity, LOC_SHAPE_WALL_DECOR_INSIDE, outside_rotation, config_loc);
    world_load_scenery_model(
        world, entity, LOC_SHAPE_WALL_DECOR_INSIDE, inside_rotation, config_loc);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element, 1, 1);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element_two, 1, 1);

    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity->scene_element.element_id);
    scene_element->dash_position->yaw += WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
        scene_element->dash_position->yaw += 512 * outside_orientation;
    scene_element->dash_position->yaw %= 2048;

    scene_element = scene2_element_at(world->scene2, entity->scene_element_two.element_id);
    scene_element->dash_position->yaw += WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
        scene_element->dash_position->yaw += 512 * inside_orientation;
    scene_element->dash_position->yaw %= 2048;

    painter_add_wall_decor(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[outside_orientation],
        THROUGHWALL);

    painter_add_wall_decor(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element_two.element_id,
        WALL_B,
        ROTATION_WALL_CORNER_TYPE[inside_orientation],
        THROUGHWALL);

    decor_buildmap_add_element(
        world->decor_buildmap,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        outside_orientation,
        DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET);
    decor_buildmap_add_element(
        world->decor_buildmap,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element_two.element_id,
        inside_orientation,
        DECOR_DISPLACEMENT_KIND_DIAGONAL);

    if( config_loc->sharelight )
    {
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element_two.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
    }
}

static void
scenery_add_wall_diagonal(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id = scene2_element_acquire(world->scene2, entity->entity_id);

    int rotation = map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(world, entity, LOC_SHAPE_WALL_DIAGONAL, rotation, config_loc);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element, 1, 1);

    painter_add_normal_scenery(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        1,
        1);

    decor_buildmap_set_wall_offset(
        world->decor_buildmap,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        config_loc->wall_width);

    if( config_loc->sharelight )
    {
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
    }
}

static void
scenery_add_normal(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id = scene2_element_acquire(world->scene2, entity->entity_id);

    int rotation = config_loc->seq_id != -1 ? 0 : map_tile->orientation;
    int orientation = map_tile->orientation;

    int size_x = config_loc->size_x;
    int size_z = config_loc->size_z;
    if( map_tile->orientation == 1 || map_tile->orientation == 3 )
    {
        int temp = size_x;
        size_x = size_z;
        size_z = temp;
    }

    world_load_scenery_model(world, entity, LOC_SHAPE_SCENERY, rotation, config_loc);
    scenery_element_position_init(
        world, &entity->scene_coord, &entity->scene_element, size_x, size_z);

    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity->scene_element.element_id);
    if( config_loc->seq_id != -1 )
        scene_element->dash_position->yaw += 256;
    scene_element->dash_position->yaw %= 2048;

    painter_add_normal_scenery(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        size_x,
        size_z);

    if( config_loc->sharelight )
    {
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element.element_id,
            size_x,
            size_z,
            config_loc->ambient,
            config_loc->contrast);
    }
}

static void
scenery_add_roof(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id = scene2_element_acquire(world->scene2, entity->entity_id);

    int rotation = map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(world, entity, map_tile->shape_select, rotation, config_loc);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element, 1, 1);

    painter_add_normal_scenery(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        1,
        1);

    if( config_loc->sharelight )
    {
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
    }
}

static void
scenery_add_floor_decoration(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id = scene2_element_acquire(world->scene2, entity->entity_id);

    int rotation = map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(world, entity, LOC_SHAPE_FLOOR_DECORATION, rotation, config_loc);
    scenery_element_position_init(world, &entity->scene_coord, &entity->scene_element, 1, 1);

    painter_add_ground_decor(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id);

    if( config_loc->sharelight )
    {
        sharelight_map_push(
            world->sharelight_map,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            entity->scene_element.element_id,
            1,
            1,
            config_loc->ambient,
            config_loc->contrast);
    }
}

static void
scenery_add(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    switch( map_tile->shape_select )
    {
    case LOC_SHAPE_WALL_SINGLE_SIDE:
        scenery_add_wall_single(world, entity, map_tile, config_loc);
        break;
    case LOC_SHAPE_WALL_TRI_CORNER:
        scenery_add_wall_tri_corner(world, entity, map_tile, config_loc);
        break;
    case LOC_SHAPE_WALL_TWO_SIDES:
        scenery_add_wall_two_sides(world, entity, map_tile, config_loc);
        break;
    case LOC_SHAPE_WALL_RECT_CORNER:
        scenery_add_wall_rect_corner(world, entity, map_tile, config_loc);
        break;
    case LOC_SHAPE_WALL_DECOR_INSIDE:
        scenery_add_wall_decor_inside(world, entity, map_tile, config_loc);
        break;
    case LOC_SHAPE_WALL_DECOR_OUTSIDE:
        scenery_add_wall_decor_outside(world, entity, map_tile, config_loc);
        break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_OUTSIDE:
        scenery_add_wall_decor_diagonal_outside(world, entity, map_tile, config_loc);
        break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_INSIDE:
        scenery_add_wall_decor_diagonal_inside(world, entity, map_tile, config_loc);
        break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE:
        scenery_add_wall_decor_diagonal_double(world, entity, map_tile, config_loc);
        break;
    case LOC_SHAPE_WALL_DIAGONAL:
        scenery_add_wall_diagonal(world, entity, map_tile, config_loc);
        break;
    case LOC_SHAPE_SCENERY:
    case LOC_SHAPE_SCENERY_DIAGIONAL:
        scenery_add_normal(world, entity, map_tile, config_loc);
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
        scenery_add_roof(world, entity, map_tile, config_loc);
        break;
    case LOC_SHAPE_FLOOR_DECORATION:
        scenery_add_floor_decoration(world, entity, map_tile, config_loc);
        break;
    }
}

#endif