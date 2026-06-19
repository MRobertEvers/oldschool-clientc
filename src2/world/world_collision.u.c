#ifndef WORLD_COLLISION_U_C
#define WORLD_COLLISION_U_C

#include "collision_map.h"
#include "flag_map.h"
#include "toriauxlib/core/toriauxlibcore_types.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/maps.h"
#include "world_builder.h"

static void
world_collision_add_loc(
    struct WorldBuilder* builder,
    struct ToriAuxLibCore_MapLoc* map_loc,
    struct ToriAuxLibCore_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int level = map_loc->chunk_pos_level;
    if( level < 0 || level >= COLLISION_LEVELS )
        return;

    struct CollisionMap* cm = world->collision_maps[level];
    if( !cm )
        return;

    enum CollisionLocAngle angle = (enum CollisionLocAngle)(map_loc->orientation & 0x3);
    int blockrange = config_loc->blocks_projectiles ? 1 : 0;
    int size_x = config_loc->size_x;
    int size_z = config_loc->size_z;

    switch( map_loc->shape_select )
    {
    case LOC_SHAPE_FLOOR_DECORATION:
    {
        if( config_loc->blocks_walk == 1 )
            collision_map_add_floor(cm, scene_x, scene_z);
        break;
    }
    case LOC_SHAPE_WALL_SINGLE_SIDE:
    {
        if( config_loc->blocks_walk != 0 )
            collision_map_add_wall(
                cm, scene_x, scene_z, LOC_SHAPE_WALL_SINGLE_SIDE, angle, blockrange);
        break;
    }
    case LOC_SHAPE_WALL_TRI_CORNER:
    {
        if( config_loc->blocks_walk != 0 )
            collision_map_add_wall(
                cm, scene_x, scene_z, LOC_SHAPE_WALL_TRI_CORNER, angle, blockrange);
        break;
    }
    case LOC_SHAPE_WALL_TWO_SIDES:
    {
        if( config_loc->blocks_walk != 0 )
            collision_map_add_wall(
                cm, scene_x, scene_z, LOC_SHAPE_WALL_TWO_SIDES, angle, blockrange);
        break;
    }
    case LOC_SHAPE_WALL_RECT_CORNER:
    {
        if( config_loc->blocks_walk != 0 )
            collision_map_add_wall(
                cm, scene_x, scene_z, LOC_SHAPE_WALL_RECT_CORNER, angle, blockrange);
        break;
    }
    case LOC_SHAPE_WALL_DIAGONAL:
    {
        if( config_loc->blocks_walk != 0 )
            collision_map_add_loc(cm, scene_x, scene_z, size_x, size_z, angle, blockrange);
        break;
    }
    case LOC_SHAPE_SCENERY:
    {
        if( config_loc->blocks_walk != 0 )
            collision_map_add_loc(cm, scene_x, scene_z, size_x, size_z, angle, blockrange);
        break;
    }
    case LOC_SHAPE_SCENERY_DIAGIONAL:
    {
        if( config_loc->blocks_walk != 0 )
            collision_map_add_loc(cm, scene_x, scene_z, size_x, size_z, angle, blockrange);
        break;
    }
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
        if( config_loc->blocks_walk != 0 )
            collision_map_add_loc(cm, scene_x, scene_z, size_x, size_z, angle, blockrange);
        break;
    }
    default:
        break;
    }
}

static void
world_collision_apply_bridges(struct WorldBuilder* builder)
{
    struct World* world = builder->world;
    int scene_size = world->_scene_size;

    for( int x = 0; x < scene_size; x++ )
    {
        for( int z = 0; z < scene_size; z++ )
        {
            if( (flag_map_get(builder->flag_map, x, z, 1) & FLOFLAG_LINK_BELOW) == 0 )
                continue;

            for( int i = 0; i < COLLISION_LEVELS - 1; i++ )
            {
                struct CollisionMap* cm_below = world->collision_maps[i];
                struct CollisionMap* cm_above = world->collision_maps[i + 1];
                if( !cm_below || !cm_above )
                    continue;

                int idx = collision_map_index_at(cm_below, x, z);
                cm_below->flags[idx] = cm_above->flags[idx];
            }
        }
    }
}

#endif
