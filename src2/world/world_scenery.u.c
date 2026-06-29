#ifndef WORLD_SCENERY_U_C
#define WORLD_SCENERY_U_C

#include "minimap.h"
#include "osrs/painters.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/varp_varbit_manager.h"
#include "shademap.h"
#include "sharelight_map.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/toriauxlib.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"
#include "toridraw/toridraw_scene.h"
#include "world_builder.h"

// clang-format off
#include "world_contour_ground.u.c"
// clang-format on

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WALL_DECOR_YAW_ADJUST 256
#define WORLD_TILE_SIZE 128
#define WORLD_CURRENT_LEVEL 0

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

static struct ToriAuxLibCore_Location*
world_builder_resolve_loc(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_Location* base_loc)
{
    int resolved_id;

    if( !base_loc || base_loc->transform_count <= 0 || !base_loc->transforms )
        return base_loc;

    resolved_id = varp_varbit_resolve_transform(
        builder->varp_varbit,
        base_loc->transforms,
        base_loc->transform_count,
        base_loc->transform_varbit,
        base_loc->transform_varp);
    if( resolved_id < 0 )
        return NULL;

    return ToriAuxLibCore_LocationGet(builder->core, resolved_id);
}

static void
calculate_wall_decor_offset(
    struct ToriDraw_Position* position,
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
    position->x += offset * x_multiplier;
    position->z += offset * z_multiplier;
}

void
world_builder_apply_wall_decor_offsets(struct WorldBuilder* builder)
{
    struct World* world = builder->world;
    if( !world || !builder->decor_buildmap || !builder->scene )
        return;

    int scene_size = world->_scene_size;
    for( int sx = 0; sx < scene_size; sx++ )
    {
        for( int sz = 0; sz < scene_size; sz++ )
        {
            for( int level = 0; level < WORLD_MAP_TERRAIN_LEVELS; level++ )
            {
                int wall_width =
                    decor_buildmap_get_wall_offset(builder->decor_buildmap, sx, sz, level);
                struct DecorElementsOnWall* elements =
                    decor_buildmap_get_elements(builder->decor_buildmap, sx, sz, level);

                for( int i = 0; i < elements->count; i++ )
                {
                    int element_id = elements->element_id[i];
                    int displacement_kind = elements->displacement_kind[i];
                    int orientation = elements->orientation[i];
                    struct ToriDraw_SceneElement* scene_element =
                        ToriDraw_SceneElementGet(builder->scene, element_id);
                    if( !scene_element )
                        continue;

                    bool diagonal = false;
                    int offset = 0;
                    switch( displacement_kind )
                    {
                    case DECOR_DISPLACEMENT_KIND_STRAIGHT_ONWALL_OFFSET:
                        offset += wall_width;
                        break;
                    case DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET:
                        offset += (wall_width / 16) * 8 + 45;
                        diagonal = true;
                        break;
                    case DECOR_DISPLACEMENT_KIND_STRAIGHT:
                        offset += 0;
                        break;
                    case DECOR_DISPLACEMENT_KIND_DIAGONAL:
                        offset += 45;
                        diagonal = true;
                        break;
                    default:
                        break;
                    }

                    calculate_wall_decor_offset(
                        &scene_element->world_position, orientation, offset, diagonal);
                }
            }
        }
    }
}

static inline enum MinimapWallFlag
orientation_wall_flag(int orientation)
{
    switch( orientation & 0x3 )
    {
    case 0:
        return MINIMAP_WALL_WEST;
    case 1:
        return MINIMAP_WALL_NORTH;
    case 2:
        return MINIMAP_WALL_EAST;
    case 3:
        return MINIMAP_WALL_SOUTH;
    default:
        return MINIMAP_WALL_NONE;
    }
}

static inline enum MinimapWallFlag
orientation_wall_flag_diagonal(int orientation)
{
    switch( orientation & 0x3 )
    {
    case 0:
        return MINIMAP_WALL_NORTHEAST_SOUTHWEST;
    case 1:
        return MINIMAP_WALL_NORTHWEST_SOUTHEAST;
    case 2:
        return MINIMAP_WALL_NORTHEAST_SOUTHWEST;
    case 3:
        return MINIMAP_WALL_NORTHWEST_SOUTHEAST;
    default:
        return MINIMAP_WALL_NONE;
    }
}

static void
scenery_register_sharelight(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z,
    int level,
    int element_id,
    int size_x,
    int size_z)
{
    struct World* world = builder->world;
    sharelight_map_push(
        builder->sharelight_map,
        config_loc->sharelight != 0,
        scene_x,
        scene_z,
        level,
        element_id,
        size_x,
        size_z,
        config_loc->ambient,
        config_loc->contrast);
}

static void
apply_transforms(
    struct ToriAuxLibCore_Location* loc,
    struct ToriDraw_Model* model,
    int orientation,
    bool old_revision)
{
    for( int i = 0; i < loc->recolor_count; i++ )
    {
        ToriDraw_ModelRecolor(model, loc->recolors_from[i], loc->recolors_to[i]);
        if( old_revision )
            ToriDraw_ModelRetexture(model, loc->recolors_from[i], loc->recolors_to[i]);
    }

    for( int i = 0; i < loc->retexture_count; i++ )
        ToriDraw_ModelRetexture(model, loc->retextures_from[i], loc->retextures_to[i]);

    bool mirrored = (loc->mirrored != (orientation > 3));
    bool oriented = orientation != 0;
    bool scaled = loc->resize_x != 128 || loc->resize_height != 128 || loc->resize_z != 128;
    bool translated = loc->offset_x != 0 || loc->offset_y != 0 || loc->offset_z != 0;

    if( mirrored )
        ToriDraw_ModelMirror(model);
    if( oriented )
        ToriDraw_ModelOrient(model, orientation);
    if( scaled )
        ToriDraw_ModelScale(model, loc->resize_x, loc->resize_z, loc->resize_height);
    if( translated )
        ToriDraw_ModelTranslate(model, loc->offset_x, loc->offset_y, loc->offset_z);
}

static int
scenery_load_model(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_tile,
    struct ToriAuxLibCore_Location* config_loc,
    int shape_select,
    int rotation,
    int scene_x,
    int scene_z,
    int size_x,
    int size_z)
{
    struct World* world = builder->world;
    int model_ids[10] = { 0 };
    int models_count = 0;

    if( !config_loc->shapes )
    {
        int count = config_loc->lengths ? config_loc->lengths[0] : 0;
        for( int i = 0; i < count && models_count < 10; i++ )
        {
            int model_id = config_loc->models[0][i];
            model_ids[models_count++] = model_id;
        }
    }
    else
    {
        bool found = false;
        for( int i = 0; i < config_loc->shapes_and_model_count; i++ )
        {
            if( config_loc->shapes[i] != shape_select )
                continue;
            int count_inner = config_loc->lengths[i];
            for( int j = 0; j < count_inner && models_count < 10; j++ )
            {
                int model_id = config_loc->models[i][j];

                model_ids[models_count++] = model_id;
                found = true;
            }
        }
        if( !found )
        {
            fprintf(stderr, "scenery_load_model: no models (shape_select=%d)\n", shape_select);
            abort();
        }
    }

    if( models_count <= 0 )
    {
        fprintf(stderr, "scenery_load_model: no models (shape_select=%d)\n", shape_select);
        abort();
    }

    struct ToriDraw_Model* models[10] = { 0 };
    for( int i = 0; i < models_count; i++ )
    {
        models[i] = ToriAuxLibTD_ModelNewInstanceFromCore(builder->td, model_ids[i]);
        assert(models[i] && "scenery_load_model: failed to load model instance");
    }

    struct ToriDraw_Model* model = NULL;
    if( models_count > 1 )
    {
        model = ToriDraw_ModelMerge(models, models_count);
        for( int i = 0; i < models_count; i++ )
            ToriDraw_ModelFree(models[i]);
    }
    else
        model = models[0];

    apply_transforms(config_loc, model, rotation, true);

    int element_id = ToriDraw_SceneElementAdd(builder->scene);
    assert(element_id >= 0 && "world_load_scenery_model: invalid element_id");

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = model,
    };
    ToriDraw_SceneElementSetModel(builder->scene, element_id, hnd);

    if( config_loc->contour_ground_type != 0 )
    {
        contour_ground_q_push(
            &builder->contour_ground_queue,
            element_id,
            map_tile->loc_id,
            shape_select,
            rotation,
            size_x,
            size_z,
            map_tile->chunk_pos_level);
    }

    return element_id;
}

static void
scenery_element_position_init(
    struct WorldBuilder* builder,
    int element_id,
    int scene_x,
    int scene_z,
    int level,
    int size_x,
    int size_z,
    int yaw)
{
    struct World* world = builder->world;
    struct HeightmapHeights heights;
    heightmap_get_heights_sized(
        world->heightmap, scene_x, scene_z, level, size_x, size_z, &heights);

    ToriDraw_SceneElementSetPosition(
        builder->scene,
        element_id,
        scene_x * WORLD_TILE_SIZE + 64 * size_x,
        heights.height_center,
        scene_z * WORLD_TILE_SIZE + 64 * size_z,
        yaw % 2048);
}

static void
scenery_load_animation(
    struct WorldBuilder* builder,
    int element_id,
    int seq_id)
{
    if( seq_id == -1 )
        return;

    bool loaded = ToriAuxLibTD_ElementSetSequenceId(builder->td, element_id, seq_id);
    if( !loaded )
        fprintf(
            stderr,
            "scenery_load_animation: animation specified but failed to load "
            "(seq_id=%d element_id=%d)\n",
            seq_id,
            element_id);
    assert(loaded && "scenery animation specified but failed to load");
    (void)loaded;
}

static void
scenery_add_wall_single(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = map_loc->orientation;
    int orientation = map_loc->orientation;

    int element_id = scenery_load_model(
        builder, map_loc, config_loc, LOC_SHAPE_WALL_SINGLE_SIDE, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
    {
        fprintf(stderr, "scenery_add_wall_single: invalid element_id=%d\n", element_id);
        abort();
    }

    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);

    painter_add_wall(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation]);

    decor_buildmap_set_wall_offset(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    if( config_loc->shadowed )
    {
        shademap2_set_wall(
            builder->shademap,
            scene_x,
            scene_z,
            map_loc->chunk_pos_level,
            map_loc->orientation,
            50);
    }

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_tri_corner(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = map_loc->orientation;
    int orientation = map_loc->orientation;
    int element_id = scenery_load_model(
        builder, map_loc, config_loc, LOC_SHAPE_WALL_TRI_CORNER, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
    {
        fprintf(stderr, "scenery_add_wall_tri_corner: invalid element_id=%d\n", element_id);
        abort();
    }

    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);

    painter_add_wall(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation]);

    decor_buildmap_set_wall_offset(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    if( config_loc->shadowed )
    {
        shademap2_set_wall_corner(
            builder->shademap, scene_x, scene_z, map_loc->chunk_pos_level, orientation, 50);
    }

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_two_sides(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int orientation = map_loc->orientation;
    int rotation = orientation + 4;
    int next_orientation = (orientation + 1) & 0x3;
    int next_rotation = (rotation + 1) & 0x3;

    int element_id = scenery_load_model(
        builder, map_loc, config_loc, LOC_SHAPE_WALL_TWO_SIDES, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
    {
        fprintf(stderr, "scenery_add_wall_two_sides: invalid element_id=%d\n", element_id);
        abort();
    }
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);
    painter_add_wall(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation]);
    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);

    int element_id2 = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        LOC_SHAPE_WALL_TWO_SIDES,
        next_rotation,
        scene_x,
        scene_z,
        1,
        1);
    if( element_id2 < 0 )
    {
        fprintf(stderr, "scenery_add_wall_two_sides: invalid element_id2=%d\n", element_id2);
        abort();
    }
    scenery_element_position_init(
        builder, element_id2, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);
    painter_add_wall(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id2,
        WALL_B,
        ROTATION_WALL_TYPE[next_orientation]);

    decor_buildmap_set_wall_offset(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id2, 1, 1);
}

static void
scenery_add_wall_rect_corner(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = map_loc->orientation;
    int orientation = map_loc->orientation;
    int element_id = scenery_load_model(
        builder, map_loc, config_loc, LOC_SHAPE_WALL_RECT_CORNER, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
    {
        fprintf(stderr, "scenery_add_wall_rect_corner: invalid element_id=%d\n", element_id);
        abort();
    }
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);

    painter_add_wall(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation]);

    decor_buildmap_set_wall_offset(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    if( config_loc->shadowed )
    {
        shademap2_set_wall_corner(
            builder->shademap,
            scene_x,
            scene_z,
            map_loc->chunk_pos_level,
            map_loc->orientation,
            50);
    }
    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_decor_inside(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = config_loc->seq_id != -1 ? 0 : map_loc->orientation;
    int orientation = map_loc->orientation;
    int yaw = config_loc->seq_id != -1 ? 512 * orientation : 0;

    int element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        scene_x,
        scene_z,
        1,
        1);
    if( element_id < 0 )
    {
        fprintf(stderr, "scenery_add_wall_decor_inside: invalid element_id=%d\n", element_id);
        abort();
    }
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, yaw);
    scenery_load_animation(builder, element_id, config_loc->seq_id);
    painter_add_wall_decor(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation],
        0);

    decor_buildmap_add_element(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        orientation,
        DECOR_DISPLACEMENT_KIND_STRAIGHT);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_decor_outside(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = config_loc->seq_id != -1 ? 0 : map_loc->orientation;
    int orientation = map_loc->orientation;
    int yaw = config_loc->seq_id != -1 ? 512 * orientation : 0;

    int element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        scene_x,
        scene_z,
        1,
        1);

    if( element_id < 0 )
    {
        fprintf(stderr, "scenery_add_wall_decor_outside: invalid element_id=%d\n", element_id);
        abort();
    }
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, yaw);
    scenery_load_animation(builder, element_id, config_loc->seq_id);

    painter_add_wall_decor(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation],
        0);

    decor_buildmap_add_element(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        orientation,
        DECOR_DISPLACEMENT_KIND_STRAIGHT_ONWALL_OFFSET);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_decor_diagonal_outside(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = config_loc->seq_id != -1 ? 0 : map_loc->orientation;
    int orientation = map_loc->orientation;
    int yaw = WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
        yaw += 512 * orientation;

    int element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        scene_x,
        scene_z,
        1,
        1);
    if( element_id < 0 )
    {
        fprintf(
            stderr, "scenery_add_wall_decor_diagonal_outside: invalid element_id=%d\n", element_id);
        abort();
    }
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, yaw);
    scenery_load_animation(builder, element_id, config_loc->seq_id);
    painter_add_wall_decor(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation],
        THROUGHWALL);

    decor_buildmap_add_element(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        orientation,
        DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_decor_diagonal_inside(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int orientation = (map_loc->orientation + 2) & 0x3;
    int rotation = config_loc->seq_id != -1 ? 0 : orientation;
    int yaw = WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
        yaw += 512 * orientation;

    int element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        scene_x,
        scene_z,
        1,
        1);
    if( element_id < 0 )
    {
        fprintf(
            stderr, "scenery_add_wall_decor_diagonal_inside: invalid element_id=%d\n", element_id);
        abort();
    }

    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, yaw);
    scenery_load_animation(builder, element_id, config_loc->seq_id);
    painter_add_wall_decor(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation],
        THROUGHWALL);

    decor_buildmap_add_element(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        orientation,
        DECOR_DISPLACEMENT_KIND_DIAGONAL);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_decor_diagonal_double(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int outside_orientation = map_loc->orientation;
    int inside_orientation = (outside_orientation + 2) & 0x3;
    int outside_rotation = config_loc->seq_id != -1 ? 0 : outside_orientation;
    int inside_rotation = config_loc->seq_id != -1 ? 0 : inside_orientation;
    int outside_yaw = WALL_DECOR_YAW_ADJUST;
    int inside_yaw = WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
    {
        outside_yaw += 512 * outside_orientation;
        inside_yaw += 512 * inside_orientation;
    }

    int outside_element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        LOC_SHAPE_WALL_DECOR_INSIDE,
        outside_rotation,
        scene_x,
        scene_z,
        1,
        1);
    if( outside_element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_decor_diagonal_double: invalid element_id=%d\n",
            outside_element_id);
        abort();
    }

    int inside_element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        LOC_SHAPE_WALL_DECOR_INSIDE,
        inside_rotation,
        scene_x,
        scene_z,
        1,
        1);
    if( inside_element_id < 0 )
    {
        fprintf(
            stderr,
            "scenery_add_wall_decor_diagonal_double: invalid element_id=%d\n",
            inside_element_id);
        abort();
    }

    scenery_element_position_init(
        builder, outside_element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, outside_yaw);
    scenery_element_position_init(
        builder, inside_element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, inside_yaw);
    scenery_load_animation(builder, outside_element_id, config_loc->seq_id);
    scenery_load_animation(builder, inside_element_id, config_loc->seq_id);

    painter_add_wall_decor(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        outside_element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[outside_orientation],
        THROUGHWALL);

    painter_add_wall_decor(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        inside_element_id,
        WALL_B,
        ROTATION_WALL_CORNER_TYPE[inside_orientation],
        THROUGHWALL);

    decor_buildmap_add_element(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        outside_element_id,
        outside_orientation,
        DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET);

    decor_buildmap_add_element(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        inside_element_id,
        inside_orientation,
        DECOR_DISPLACEMENT_KIND_DIAGONAL);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, outside_element_id, 1, 1);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, inside_element_id, 1, 1);
}

static void
scenery_add_wall_diagonal(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = map_loc->orientation;
    int element_id = scenery_load_model(
        builder, map_loc, config_loc, LOC_SHAPE_WALL_DIAGONAL, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
    {
        fprintf(stderr, "scenery_add_wall_diagonal: invalid element_id=%d\n", element_id);
        abort();
    }
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);

    painter_add_normal_scenery(
        world->painter, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);

    decor_buildmap_set_wall_offset(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_normal(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = config_loc->seq_id != -1 ? 0 : map_loc->orientation;
    int orientation = map_loc->orientation;
    int size_x = config_loc->size_x;
    int size_z = config_loc->size_z;
    int yaw = 0;

    if( map_loc->orientation == 1 || map_loc->orientation == 3 )
    {
        int tmp = size_x;
        size_x = size_z;
        size_z = tmp;
    }

    if( map_loc->shape_select == LOC_SHAPE_SCENERY_DIAGIONAL )
        yaw += WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
        yaw += 512 * orientation;

    int element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        LOC_SHAPE_SCENERY,
        rotation,
        scene_x,
        scene_z,
        size_x,
        size_z);
    if( element_id < 0 )
    {
        fprintf(stderr, "scenery_add_normal: invalid element_id=%d\n", element_id);
        abort();
    }
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, size_x, size_z, yaw);
    scenery_load_animation(builder, element_id, config_loc->seq_id);

    painter_add_normal_scenery(
        world->painter, scene_x, scene_z, map_loc->chunk_pos_level, element_id, size_x, size_z);
    int shade = size_x * size_z * 11;
    if( shade > 30 )
        shade = 30;
    if( config_loc->shadowed )
    {
        shademap2_set_sized(
            builder->shademap, scene_x, scene_z, map_loc->chunk_pos_level, size_x, size_z, shade);
    }

    scenery_register_sharelight(
        builder,
        config_loc,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        size_x,
        size_z);
}

static void
scenery_add_roof(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = map_loc->orientation;
    int element_id = scenery_load_model(
        builder, map_loc, config_loc, map_loc->shape_select, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
    {
        fprintf(stderr, "scenery_add_roof: invalid element_id=%d\n", element_id);
        abort();
    }

    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);
    painter_add_normal_scenery(
        world->painter, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_floor_decoration(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = map_loc->orientation;
    int element_id = scenery_load_model(
        builder, map_loc, config_loc, LOC_SHAPE_FLOOR_DECORATION, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
    {
        fprintf(stderr, "scenery_add_floor_decoration: invalid element_id=%d\n", element_id);
        abort();
    }
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);
    scenery_load_animation(builder, element_id, config_loc->seq_id);

    painter_add_ground_decor(
        world->painter, scene_x, scene_z, map_loc->chunk_pos_level, element_id);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

void
world_builder_minimap_add_chunk_walls(
    struct WorldBuilder* builder,
    int mapx,
    int mapz)
{
    struct World* world = builder->world;
    if( !world->minimap )
        return;

    int map_id = (mapx << 16) | (mapz & 0xFFFF);
    struct ToriAuxLibCore_MapLocs* map_locs = ToriAuxLibCore_MapSceneryGet(builder->core, map_id);
    if( !map_locs )
        return;

    int scene_size = world->_scene_size;

    for( int i = 0; i < map_locs->locs_count; i++ )
    {
        struct ToriAuxLibCore_MapLoc* map_loc = &map_locs->locs[i];
        int offset_x = world_to_scene_x(world, mapx, map_loc->chunk_pos_x);
        int offset_z = world_to_scene_z(world, mapz, map_loc->chunk_pos_z);

        if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size || offset_z >= scene_size )
            continue;

        struct ToriAuxLibCore_Location* config_loc =
            ToriAuxLibCore_LocationGet(builder->core, map_loc->loc_id);
        if( !config_loc )
            continue;

        config_loc = world_builder_resolve_loc(builder, config_loc);
        if( !config_loc )
            continue;

        if( config_loc->map_scene_id != -1 )
            continue;

        if( map_loc->chunk_pos_level != WORLD_CURRENT_LEVEL )
            continue;

        switch( map_loc->shape_select )
        {
        case LOC_SHAPE_WALL_SINGLE_SIDE:
            minimap_add_tile_wall(
                world->minimap, offset_x, offset_z, orientation_wall_flag(map_loc->orientation));
            break;
        case LOC_SHAPE_WALL_TRI_CORNER:
            break;
        case LOC_SHAPE_WALL_TWO_SIDES:
        {
            minimap_add_tile_wall(
                world->minimap, offset_x, offset_z, orientation_wall_flag(map_loc->orientation));

            int next_orientation = (map_loc->orientation + 1) & 0x3;
            minimap_add_tile_wall(
                world->minimap, offset_x, offset_z, orientation_wall_flag(next_orientation));
            break;
        }
        case LOC_SHAPE_WALL_RECT_CORNER:
            break;
        case LOC_SHAPE_WALL_DIAGONAL:
            minimap_add_tile_wall(
                world->minimap,
                offset_x,
                offset_z,
                orientation_wall_flag_diagonal(map_loc->orientation));
            break;
        default:
            break;
        }
    }
}

static void
scenery_add(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    switch( map_loc->shape_select )
    {
    case LOC_SHAPE_WALL_SINGLE_SIDE:
        scenery_add_wall_single(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case LOC_SHAPE_WALL_TRI_CORNER:
        scenery_add_wall_tri_corner(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case LOC_SHAPE_WALL_TWO_SIDES:
        scenery_add_wall_two_sides(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case LOC_SHAPE_WALL_RECT_CORNER:
        scenery_add_wall_rect_corner(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case LOC_SHAPE_WALL_DECOR_INSIDE:
        scenery_add_wall_decor_inside(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case LOC_SHAPE_WALL_DECOR_OUTSIDE:
        scenery_add_wall_decor_outside(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_OUTSIDE:
        scenery_add_wall_decor_diagonal_outside(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_INSIDE:
        scenery_add_wall_decor_diagonal_inside(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE:
        scenery_add_wall_decor_diagonal_double(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case LOC_SHAPE_WALL_DIAGONAL:
        scenery_add_wall_diagonal(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case LOC_SHAPE_SCENERY:
    case LOC_SHAPE_SCENERY_DIAGIONAL:
        scenery_add_normal(builder, map_loc, config_loc, scene_x, scene_z);
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
        scenery_add_roof(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case LOC_SHAPE_FLOOR_DECORATION:
        scenery_add_floor_decoration(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    }
}

#endif
