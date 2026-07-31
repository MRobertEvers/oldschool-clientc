#ifndef WORLD_COLLISION_U_C
#define WORLD_COLLISION_U_C

#include "collision_map.h"
#include "engine/cache_provider.h"
#include "flag_map.h"
#include <rscache.h>
#include "world_builder.h"

/* Shared core for add/del of a loc's collision. `add` selects the add_* vs del_*
 * collision primitives (exact inverses), so a runtime LOC change can undo the
 * collision the original loc contributed. Mirrors Client-TS ClientBuild.addLoc /
 * locChangeUnchecked's del path (Client.ts:7763-7789). */
static void
world_collision_apply_loc(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z,
    int add)
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
    int shape = map_loc->shape_select;

    void (*wall_op)(struct CollisionMap*, int, int, int, enum CollisionLocAngle, int) =
        add ? collision_map_add_wall : collision_map_del_wall;
    void (*loc_op)(struct CollisionMap*, int, int, int, int, enum CollisionLocAngle, int) =
        add ? collision_map_add_loc : collision_map_del_loc;

    switch( shape )
    {
    case RSCACHE_LOC_SHAPE_FLOOR_DECORATION:
        /* Reference gates ground decor on blockwalk && active (ClientBuild.ts
         * addLoc / locChangeUnchecked del path): inactive decor never blocks. */
        if( config_loc->blocks_walk == 1 && config_loc->is_interactive )
        {
            if( add )
                collision_map_add_floor(cm, scene_x, scene_z);
            else
                collision_map_del_floor(cm, scene_x, scene_z);
        }
        break;
    case RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE:
    case RSCACHE_LOC_SHAPE_WALL_TRI_CORNER:
    case RSCACHE_LOC_SHAPE_WALL_TWO_SIDES:
    case RSCACHE_LOC_SHAPE_WALL_RECT_CORNER:
        if( config_loc->blocks_walk != 0 )
            wall_op(cm, scene_x, scene_z, shape, angle, blockrange);
        break;
    case RSCACHE_LOC_SHAPE_WALL_DIAGONAL:
    case RSCACHE_LOC_SHAPE_SCENERY:
    case RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OUTER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_INNER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_HARD_INNER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_HARD_OUTER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_FLAT:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_OUTER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_INNER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_HARD_OUTER_CORNER:
        if( config_loc->blocks_walk != 0 )
            loc_op(cm, scene_x, scene_z, size_x, size_z, angle, blockrange);
        break;
    default:
        break;
    }
}

static void
world_collision_add_loc(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    world_collision_apply_loc(builder, map_loc, config_loc, scene_x, scene_z, 1);
}

static void
world_collision_del_loc(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    world_collision_apply_loc(builder, map_loc, config_loc, scene_x, scene_z, 0);
}

/*
 * Terrain blocking — the map square's own per-tile Block flag.
 *
 * This did not exist client-side at all: `RSCACHE_FLOFLAG_BLOCK` appeared
 * exactly once in the whole of `src/`, and that once was the *server's*
 * scene builder. So the client's collision map knew about locs and walls but
 * not about water, cliff faces or any other impassable ground, and its
 * pathfinder happily routed straight across the River Lum. The click was then
 * sent as a run of turning points the server re-pathed leg by leg against a map
 * that *does* block them, which is why a six-tile walk could arrive as
 * `waypoints=16 steps=49`.
 *
 * No level shift here, deliberately. The reference does the LinkBelow
 * `trueLevel--` inline in this same loop (ClientBuild.finishBuild:365-378), but
 * this build already performs it as a separate whole-column pass in
 * `world_collision_apply_bridges` below, which copies level i+1's flag word
 * down on every LinkBelow column. Doing it here as well would apply it twice.
 * That is also why this must run BEFORE the bridge pass rather than after —
 * the push-down has to carry these flags with it.
 */
static void
world_collision_apply_terrain(struct WorldBuilder* builder)
{
    struct World* world = builder->world;
    int scene_size = world->_scene_size;

    if( !builder->flag_map )
        return;

    for( int level = 0; level < COLLISION_LEVELS; level++ )
    {
        struct CollisionMap* cm = world->collision_maps[level];
        int blocked = 0;

        if( !cm )
            continue;
        for( int x = 0; x < scene_size; x++ )
        {
            for( int z = 0; z < scene_size; z++ )
            {
                if( (flag_map_get(builder->flag_map, x, z, level) & RSCACHE_FLOFLAG_BLOCK) != 0 )
                {
                    collision_map_add_floor(cm, x, z);
                    blocked++;
                }
            }
        }
        if( getenv("TORIRS_TERRAIN_DEBUG") )
            fprintf(stderr, "terrain: level %d blocked %d of %d tiles\n", level, blocked,
                    scene_size * scene_size);
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
            if( (flag_map_get(builder->flag_map, x, z, 1) & RSCACHE_FLOFLAG_LINK_BELOW) == 0 )
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
