#ifndef WORLD_SCENERY_U_C
#define WORLD_SCENERY_U_C

#include "contour_ground.h"
#include "dash_utils.h"
#include "model_transforms.h"
#include "world.h"

#include <assert.h>
#include <stdlib.h>

// clang-format off
#include "_light_model_default.u.c"
// clang-format on

#define WALL_DECOR_YAW_ADJUST 256

static const int WALL_DECOR_ROTATION_OFFSET_X[] = { 1, 0, -1, 0 };
static const int WALL_DECOR_ROTATION_OFFSET_Z[] = { 0, -1, 0, 1 };
static const int WALL_DECOR_ROTATION_DIAGONAL_OFFSET_X[] = { 1, -1, -1, 1 };
static const int WALL_DECOR_ROTATION_DIAGONAL_OFFSET_Z[] = { -1, -1, 1, 1 };

void
world_contour_ground_queue_push(
    struct World* world,
    int entity_id,
    int loc_id,
    int shape_select,
    int rotation,
    int size_x,
    int size_z,
    int element_slot)
{
    if( entity_id < 0 )
        return;

    if( world->contour_ground_queue_count >= world->contour_ground_queue_cap )
    {
        int ncap = world->contour_ground_queue_cap ? world->contour_ground_queue_cap * 2 : 512;
        struct ContourGroundQueueEntry* next = (struct ContourGroundQueueEntry*)realloc(
            world->contour_ground_queue, (size_t)ncap * sizeof(struct ContourGroundQueueEntry));
        assert(next);
        world->contour_ground_queue = next;
        world->contour_ground_queue_cap = ncap;
    }

    struct ContourGroundQueueEntry* r =
        &world->contour_ground_queue[world->contour_ground_queue_count++];
    r->entity_id = entity_id;
    r->loc_id = loc_id;
    r->shape_select = shape_select;
    r->rotation = rotation;
    r->size_x = size_x;
    r->size_z = size_z;
    r->element_slot = element_slot;
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
    struct MapBuildLocEntity* entity,
    struct EntitySceneCoord* entity_scene_coord,
    struct EntitySceneElement* entity_scene_element,
    int size_x,
    int size_z)
{
    if( entity )
    {
        entity->place_size_x = (uint16_t)size_x;
        entity->place_size_z = (uint16_t)size_z;
    }

    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity_scene_element->element_id);
    scene2_element_expect(scene_element, "scenery_element_position_init");

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
    scene2_element_set_dash_position_ptr(scene_element, dashposition_new());
    struct DashPosition* dp = scene2_element_dash_position(scene_element);
    dp->x = 128 * tile_x + 64 * (size_x);
    dp->z = 128 * tile_z + 64 * (size_z);
    dp->y = heights.height_center;
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
world_load_scenery_model(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct EntitySceneCoord* entity_scene_coord,
    struct EntitySceneElement* entity_scene_element,
    int shape_select,
    int rotation,
    int size_x,
    int size_z,
    struct CacheConfigLocation* config_loc)
{
    /* Snapshot before model work: heavy alloc may grow entity_vec and invalidate
     * entity_scene_element if it pointed into reallocated storage. */
    const int element_id = entity_scene_element->element_id;

    struct CacheModel* model = NULL;

    if( entity_scene_element->element_id < 0 )
    {
        fprintf(
            stderr,
            "world_load_scenery_model: invalid element_id=%d\n",
            entity_scene_element->element_id);
        abort();
    }

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
            if( !model )
                printf("Model not found: %d\n", model_id);
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
                    if( !model )
                        printf("Model not found: %d\n", model_id);
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

    if( models_count <= 0 )
    {
        fprintf(stderr, "world_load_scenery_model: no models (shape_select=%d)\n", shape_select);
        abort();
    }

    if( models_count > 1 )
    {
        model = model_new_merge(models, models_count);
    }
    else
    {
        model = model_new_copy(models[0]);
    }

    struct DashModel* dash_model = NULL;
    struct Scene2Element* scene_element = NULL;

    apply_transforms(config_loc, model, rotation, true);

    dash_model = dashmodel_new_from_cache_model(model);
    model_free(model);

    scene_element = scene2_element_at(world->scene2, element_id);
    if( !scene_element )
    {
        fprintf(
            stderr,
            "world_load_scenery_model: invalid element_id=%d (scene2 total=%d shape_select=%d)\n",
            element_id,
            scene2_elements_total(world->scene2),
            shape_select);
        dashmodel_free(dash_model);
        abort();
    }

    scene2_element_set_dash_model(world->scene2, scene_element, dash_model);

    if( config_loc->contour_ground_type != 0 )
    {
        int element_slot = (entity_scene_element == &entity->scene_element_two) ? 1 : 0;
        world_contour_ground_queue_push(
            world,
            entity->entity_id,
            map_tile->loc_id,
            shape_select,
            rotation,
            size_x,
            size_z,
            element_slot);
    }
}

static int
scenery_element_acquire(
    struct World* world,
    int entity_id,
    bool animated)
{
    int parent = (int)entity_unified_id(ENTITY_KIND_MAP_BUILD_LOC, entity_id);
    if( animated )
        return scene2_element_acquire_full(world->scene2, parent, SCENE2_ELEMENT_SCENERY);

    return scene2_element_acquire_fast(world->scene2, parent, SCENE2_ELEMENT_SCENERY);
}

static void
scenery_init_from_config_loc(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheConfigLocation* config_loc)
{
    entity->interactable = config_loc->is_interactive;

    if( config_loc->name )
        strncpy(entity->name, config_loc->name, sizeof(entity->name));

    if( config_loc->desc )
        strncpy(entity->description, config_loc->desc, sizeof(entity->description));

    for( int i = 0; i < 10; i++ )
    {
        if( config_loc->actions[i] )
        {
            world_map_build_loc_entity_push_action(
                world, entity->entity_id, i, config_loc->actions[i]);
        }
    }
}

static void
scenery_add_wall_single(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);

    if( entity->scene_element.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_single: invalid element_id=%d\n",
            entity->scene_element.element_id);
        abort();
    }

    int rotation = map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        LOC_SHAPE_WALL_SINGLE_SIDE,
        rotation,
        1,
        1,
        config_loc);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);

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

    if( config_loc->shadowed )
    {
        shademap2_set_wall(
            world->shademap,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            orientation,
            50);
    }

    sharelight_map_push(
        world->sharelight_map,
        config_loc->sharelight != 0,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        1,
        1,
        config_loc->ambient,
        config_loc->contrast);
}

static void
scenery_add_wall_tri_corner(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);

    if( entity->scene_element.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_tri_corner: invalid element_id=%d\n",
            entity->scene_element.element_id);
        abort();
    }

    int rotation = map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        LOC_SHAPE_WALL_TRI_CORNER,
        rotation,
        1,
        1,
        config_loc);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);

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

    /* Shademap */
    if( config_loc->shadowed )
        shademap2_set_wall_corner(
            world->shademap,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            orientation,
            50);

    sharelight_map_push(
        world->sharelight_map,
        config_loc->sharelight != 0,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        1,
        1,
        config_loc->ambient,
        config_loc->contrast);
}

static void
scenery_add_wall_two_sides(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);
    entity->scene_element_two.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);

    if( entity->scene_element.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_two_sides: invalid element_id=%d\n",
            entity->scene_element.element_id);
        abort();
    }

    if( entity->scene_element_two.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_two_sides: invalid element_id=%d\n",
            entity->scene_element_two.element_id);
        abort();
    }

    int orientation = map_tile->orientation;
    // +4 for Mirrored
    int rotation = map_tile->orientation + 4;

    int next_orientation = (orientation + 1) & 0x3;
    int next_rotation = (rotation + 1) & 0x3;

    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        LOC_SHAPE_WALL_TWO_SIDES,
        rotation,
        1,
        1,
        config_loc);
    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element_two,
        LOC_SHAPE_WALL_TWO_SIDES,
        next_rotation,
        1,
        1,
        config_loc);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element_two, 1, 1);

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

    sharelight_map_push(
        world->sharelight_map,
        config_loc->sharelight != 0,
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
        config_loc->sharelight != 0,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element_two.element_id,
        1,
        1,
        config_loc->ambient,
        config_loc->contrast);
}

static void
scenery_add_wall_rect_corner(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);

    if( entity->scene_element.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_rect_corner: invalid element_id=%d\n",
            entity->scene_element.element_id);
        abort();
    }

    int rotation = map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        LOC_SHAPE_WALL_RECT_CORNER,
        rotation,
        1,
        1,
        config_loc);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);

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

    if( config_loc->shadowed )
        shademap2_set_wall_corner(
            world->shademap,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            orientation,
            50);

    sharelight_map_push(
        world->sharelight_map,
        config_loc->sharelight != 0,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        1,
        1,
        config_loc->ambient,
        config_loc->contrast);
}

static void
scenery_add_wall_decor_inside(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);

    if( entity->scene_element.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_decor_inside: invalid element_id=%d\n",
            entity->scene_element.element_id);
        abort();
    }

    int rotation = config_loc->seq_id != -1 ? 0 : map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        1,
        1,
        config_loc);
    world_map_build_loc_entity_set_animation(world, entity->entity_id, config_loc->seq_id);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity->scene_element.element_id);
    scene2_element_expect(scene_element, "scenery_add_wall_decor_inside");
    if( config_loc->seq_id != -1 )
    {
        scene2_element_dash_position(scene_element)->yaw += 512 * orientation;
    }
    scene2_element_dash_position(scene_element)->yaw %= 2048;

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

    sharelight_map_push(
        world->sharelight_map,
        config_loc->sharelight != 0,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        1,
        1,
        config_loc->ambient,
        config_loc->contrast);
}

static void
scenery_add_wall_decor_outside(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);

    if( entity->scene_element.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_decor_outside: invalid element_id=%d\n",
            entity->scene_element.element_id);
        abort();
    }

    int rotation = config_loc->seq_id != -1 ? 0 : map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        1,
        1,
        config_loc);
    world_map_build_loc_entity_set_animation(world, entity->entity_id, config_loc->seq_id);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);

    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity->scene_element.element_id);
    scene2_element_expect(scene_element, "scenery_add_wall_decor_outside");
    if( config_loc->seq_id != -1 )
    {
        scene2_element_dash_position(scene_element)->yaw += 512 * orientation;
    }
    scene2_element_dash_position(scene_element)->yaw %= 2048;

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

    sharelight_map_push(
        world->sharelight_map,
        config_loc->sharelight != 0,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        1,
        1,
        config_loc->ambient,
        config_loc->contrast);
}

// Lumbridge shield
static void
scenery_add_wall_decor_diagonal_outside(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);

    if( entity->scene_element.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_decor_diagonal_outside: invalid element_id=%d\n",
            entity->scene_element.element_id);
        abort();
    }

    entity->interactable = config_loc->is_interactive;

    int rotation = config_loc->seq_id != -1 ? 0 : map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        1,
        1,
        config_loc);
    world_map_build_loc_entity_set_animation(world, entity->entity_id, config_loc->seq_id);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);

    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity->scene_element.element_id);
    scene2_element_expect(scene_element, "scenery_add_wall_decor_diagonal_outside");
    scene2_element_dash_position(scene_element)->yaw += WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
    {
        scene2_element_dash_position(scene_element)->yaw += 512 * orientation;
    }
    scene2_element_dash_position(scene_element)->yaw %= 2048;

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

    sharelight_map_push(
        world->sharelight_map,
        config_loc->sharelight != 0,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        1,
        1,
        config_loc->ambient,
        config_loc->contrast);
}

// Lumbridge sconce
// Support beams in lumbridge general store along wall.
static void
scenery_add_wall_decor_diagonal_inside(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);

    if( entity->scene_element.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_decor_diagonal_inside: invalid element_id=%d\n",
            entity->scene_element.element_id);
        abort();
    }

    entity->interactable = config_loc->is_interactive;

    int orientation = (map_tile->orientation + 2) & 0x3;
    int rotation = config_loc->seq_id != -1 ? 0 : orientation;

    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        1,
        1,
        config_loc);
    world_map_build_loc_entity_set_animation(world, entity->entity_id, config_loc->seq_id);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);

    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity->scene_element.element_id);
    scene2_element_expect(scene_element, "scenery_add_wall_decor_diagonal_inside primary");
    scene2_element_dash_position(scene_element)->yaw += WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
    {
        scene2_element_dash_position(scene_element)->yaw += 512 * orientation;
    }
    scene2_element_dash_position(scene_element)->yaw %= 2048;

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

    sharelight_map_push(
        world->sharelight_map,
        config_loc->sharelight != 0,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        1,
        1,
        config_loc->ambient,
        config_loc->contrast);
}

static void
scenery_add_wall_decor_diagonal_double(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);
    entity->scene_element_two.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);

    if( entity->scene_element.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_decor_diagonal_double: invalid element_id=%d\n",
            entity->scene_element.element_id);
        abort();
    }

    if( entity->scene_element_two.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_decor_diagonal_double: invalid element_id=%d\n",
            entity->scene_element_two.element_id);
        abort();
    }

    entity->interactable = config_loc->is_interactive;

    int outside_rotation = config_loc->seq_id != -1 ? 0 : map_tile->orientation;
    int outside_orientation = map_tile->orientation;
    int inside_rotation = config_loc->seq_id != -1 ? 0 : (outside_orientation + 2) & 0x3;
    int inside_orientation = (outside_orientation + 2) & 0x3;

    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        LOC_SHAPE_WALL_DECOR_INSIDE,
        outside_rotation,
        1,
        1,
        config_loc);
    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element_two,
        LOC_SHAPE_WALL_DECOR_INSIDE,
        inside_rotation,
        1,
        1,
        config_loc);
    world_map_build_loc_entity_set_animation(world, entity->entity_id, config_loc->seq_id);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element_two, 1, 1);

    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity->scene_element.element_id);
    scene2_element_expect(scene_element, "scenery_add_wall_decor_diagonal_double primary");
    scene2_element_dash_position(scene_element)->yaw += WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
        scene2_element_dash_position(scene_element)->yaw += 512 * outside_orientation;
    scene2_element_dash_position(scene_element)->yaw %= 2048;

    scene_element = scene2_element_at(world->scene2, entity->scene_element_two.element_id);
    scene2_element_expect(scene_element, "scenery_add_wall_decor_diagonal_double secondary");
    scene2_element_dash_position(scene_element)->yaw += WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
        scene2_element_dash_position(scene_element)->yaw += 512 * inside_orientation;
    scene2_element_dash_position(scene_element)->yaw %= 2048;

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

    sharelight_map_push(
        world->sharelight_map,
        config_loc->sharelight != 0,
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
        config_loc->sharelight != 0,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element_two.element_id,
        1,
        1,
        config_loc->ambient,
        config_loc->contrast);
}

static void
scenery_add_wall_diagonal(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);

    if( entity->scene_element.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_diagonal: invalid element_id=%d\n",
            entity->scene_element.element_id);
        abort();
    }

    int rotation = map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        LOC_SHAPE_WALL_DIAGONAL,
        rotation,
        1,
        1,
        config_loc);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);

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

    sharelight_map_push(
        world->sharelight_map,
        config_loc->sharelight != 0,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        1,
        1,
        config_loc->ambient,
        config_loc->contrast);
}

static void
scenery_add_normal(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);

    if( entity->scene_element.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_normal: invalid element_id=%d\n",
            entity->scene_element.element_id);
        abort();
    }

    entity->interactable = config_loc->is_interactive;

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

    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        LOC_SHAPE_SCENERY,
        rotation,
        size_x,
        size_z,
        config_loc);
    world_map_build_loc_entity_set_animation(world, entity->entity_id, config_loc->seq_id);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element, size_x, size_z);

    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, entity->scene_element.element_id);
    scene2_element_expect(scene_element, "scenery_add_normal");
    if( map_tile->shape_select == LOC_SHAPE_SCENERY_DIAGIONAL )
        scene2_element_dash_position(scene_element)->yaw += 256;
    if( config_loc->seq_id != -1 )
        scene2_element_dash_position(scene_element)->yaw += 512 * orientation;
    scene2_element_dash_position(scene_element)->yaw %= 2048;

    painter_add_normal_scenery(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        size_x,
        size_z);

    /* Shademap */
    int shade = size_x * size_z * 11;
    if( shade > 30 )
        shade = 30;
    if( config_loc->shadowed )
        shademap2_set_sized(
            world->shademap,
            entity->scene_coord.sx,
            entity->scene_coord.sz,
            entity->scene_coord.slevel,
            size_x,
            size_z,
            shade);

    sharelight_map_push(
        world->sharelight_map,
        config_loc->sharelight != 0,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        size_x,
        size_z,
        config_loc->ambient,
        config_loc->contrast);
}

static void
scenery_add_roof(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);

    if( entity->scene_element.element_id < 0 )
    {
        fprintf(
            stderr, "scenery_add_roof: invalid element_id=%d\n", entity->scene_element.element_id);
        abort();
    }

    int rotation = map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        map_tile->shape_select,
        rotation,
        1,
        1,
        config_loc);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);

    painter_add_normal_scenery(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        1,
        1);

    sharelight_map_push(
        world->sharelight_map,
        config_loc->sharelight != 0,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        1,
        1,
        config_loc->ambient,
        config_loc->contrast);
}

static void
scenery_add_floor_decoration(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    entity->scene_element.element_id =
        scenery_element_acquire(world, entity->entity_id, config_loc->seq_id != -1);

    if( entity->scene_element.element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_floor_decoration: invalid element_id=%d\n",
            entity->scene_element.element_id);
        abort();
    }

    int rotation = map_tile->orientation;
    int orientation = map_tile->orientation;

    world_load_scenery_model(
        world,
        entity,
        map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        LOC_SHAPE_FLOOR_DECORATION,
        rotation,
        1,
        1,
        config_loc);
    scenery_element_position_init(
        world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);

    painter_add_ground_decor(
        world->painter,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id);

    sharelight_map_push(
        world->sharelight_map,
        config_loc->sharelight != 0,
        entity->scene_coord.sx,
        entity->scene_coord.sz,
        entity->scene_coord.slevel,
        entity->scene_element.element_id,
        1,
        1,
        config_loc->ambient,
        config_loc->contrast);
}

static void
scenery_add(
    struct World* world,
    struct MapBuildLocEntity* entity,
    struct CacheMapLoc* map_tile,
    struct CacheConfigLocation* config_loc)
{
    scenery_init_from_config_loc(world, entity, config_loc);

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

static void
world_contour_ground_refresh_all_loc_dash_y(struct World* world)
{
    struct Heightmap* hm = world->heightmap;
    if( !hm || world->active_loc_entity_count <= 0 )
        return;

    for( int i = 0; i < world->active_loc_entity_count; i++ )
    {
        int eid = world->active_loc_entities[i];
        struct MapBuildLocEntity* entity = world_loc_entity(world, eid);
        int sx = (int)entity->scene_coord.sx;
        int sz = (int)entity->scene_coord.sz;
        int sl = (int)entity->scene_coord.slevel;
        int psx = entity->place_size_x > 0 ? (int)entity->place_size_x : 1;
        int psz = entity->place_size_z > 0 ? (int)entity->place_size_z : 1;

        struct EntitySceneElement* eses[2] = { &entity->scene_element, &entity->scene_element_two };
        for( int s = 0; s < 2; s++ )
        {
            struct EntitySceneElement* ese = eses[s];
            if( ese->element_id < 0 )
                continue;
            struct Scene2Element* scene_el = scene2_element_at(world->scene2, ese->element_id);
            if( !scene_el )
                continue;
            struct DashPosition* dp = scene2_element_dash_position(scene_el);
            if( !dp )
                continue;
            struct HeightmapHeights heights = { 0 };
            heightmap_get_heights_sized(hm, sx, sz, sl, psx, psz, &heights);
            dp->y = heights.height_center;
        }
    }
}

void
world_contour_ground(struct World* world)
{
    struct Heightmap* hm = world->heightmap;
    if( !hm )
        return;

    int n = world->contour_ground_queue_count;
    if( n > 0 && world->contour_ground_queue )
    {
        int loc_count = entity_vec_count(&world->map_build_loc_entities);

        for( int ri = 0; ri < n; ri++ )
        {
            struct ContourGroundQueueEntry* r = &world->contour_ground_queue[ri];
            if( r->entity_id < 0 || r->entity_id >= loc_count )
                continue;

            struct MapBuildLocEntity* entity = world_loc_entity(world, r->entity_id);
            struct EntitySceneElement* ese =
                r->element_slot ? &entity->scene_element_two : &entity->scene_element;
            if( ese->element_id < 0 )
                continue;

            struct CacheConfigLocation* config_loc =
                buildcachedat_get_config_loc(world->buildcachedat, r->loc_id);
            if( !config_loc || config_loc->contour_ground_type == 0 )
                continue;

            struct Scene2Element* scene_el = scene2_element_at(world->scene2, ese->element_id);
            if( !scene_el )
                continue;

            struct DashModel* dm = scene2_element_dash_model(scene_el);
            if( !dm )
                continue;

            int contour_type = config_loc->contour_ground_type;
            int contour_param = config_loc->contour_ground_param;
            struct EntitySceneCoord* coord = &entity->scene_coord;
            int sl = (int)coord->slevel;
            if( (contour_type == CONTOUR_GROUND_ABOVE_OFFSET ||
                 contour_type == CONTOUR_GROUND_DUAL_LEVEL_BLEND) &&
                sl + 1 >= hm->levels )
                continue;

            int scene_x = (int)coord->sx * 128 + 64 * r->size_x;
            int scene_z = (int)coord->sz * 128 + 64 * r->size_z;
            struct HeightmapHeights place_heights = { 0 };
            heightmap_get_heights_sized(
                hm, (int)coord->sx, (int)coord->sz, sl, r->size_x, r->size_z, &place_heights);
            int scene_height = place_heights.height_center;

            int hm_ax = hm->size_x;
            int hm_az = hm->size_z;
            int above_ax = (contour_type == CONTOUR_GROUND_ABOVE_OFFSET ||
                            contour_type == CONTOUR_GROUND_DUAL_LEVEL_BLEND)
                               ? hm_ax
                               : 0;
            int above_az = (contour_type == CONTOUR_GROUND_ABOVE_OFFSET ||
                            contour_type == CONTOUR_GROUND_DUAL_LEVEL_BLEND)
                               ? hm_az
                               : 0;

            int vc = dashmodel_vertex_count(dm);
            vertexint_t* vxs = dashmodel_vertices_x(dm);
            vertexint_t* vys = dashmodel_vertices_y(dm);
            vertexint_t* vzs = dashmodel_vertices_z(dm);
            if( vc <= 0 || !vxs || !vys || !vzs )
                continue;

            struct ContourGround cg;
            if( !contour_ground_init(
                    &cg,
                    contour_type,
                    contour_param,
                    hm_ax,
                    hm_az,
                    above_ax,
                    above_az,
                    vxs,
                    vys,
                    vzs,
                    vc,
                    vc,
                    CONTOUR_VERTEX_INT16,
                    scene_x,
                    scene_z,
                    scene_height,
                    sl) )
                continue;

            struct ContourGroundCommand cmd;
            while( contour_ground_next(&cg, &cmd) )
            {
                switch( cmd.kind )
                {
                case CONTOUR_CMD_FETCH_HEIGHT:
                    contour_ground_provide(
                        &cg, heightmap_get_interpolated(hm, cmd.draw_x, cmd.draw_z, cmd.slevel));
                    break;
                case CONTOUR_CMD_FETCH_HEIGHT_ABOVE:
                    contour_ground_provide(
                        &cg, heightmap_get_interpolated(hm, cmd.draw_x, cmd.draw_z, sl + 1));
                    break;
                case CONTOUR_CMD_SET_Y:
                    vys[cmd.vertex_index] = (vertexint_t)cmd.contour_y;
                    break;
                }
            }

            dashmodel_set_bounds_cylinder(dm);
        }
    }

    world_contour_ground_refresh_all_loc_dash_y(world);

    world->contour_ground_queue_count = 0;
}

#endif