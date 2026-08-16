#ifndef WORLD_COLLISION_U_C
#define WORLD_COLLISION_U_C

#include "collision_map.h"
#include "engine/cache_provider.h"
#include "flag_map.h"
#include <rscache.h>
#include "world_builder.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * LINK_BELOW on cache level 1: collision stamps go to level-1 (Client-TS
 * ClientBuild / LostCity GameMap place-time shift). Geometry still uses the
 * raw cache level. Skip when the shifted level would be negative (level-0
 * content under a bridge contributes no collision).
 */
static int
collision_column_link_below(
    struct WorldBuilder* builder,
    int scene_x,
    int scene_z)
{
    if( builder->flag_map )
        return (flag_map_get(builder->flag_map, scene_x, scene_z, 1) &
                RSCACHE_FLOFLAG_LINK_BELOW) != 0;
    return (World_TileFlagGet(builder->world, scene_x, scene_z, 1) &
            RSCACHE_FLOFLAG_LINK_BELOW) != 0;
}

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
    struct CollisionMap* cm;
    enum CollisionLocAngle angle;
    int blockrange;
    int size_x;
    int size_z;
    int shape;

    if( level < 0 || level >= COLLISION_LEVELS )
        return;
    if( collision_column_link_below(builder, scene_x, scene_z) )
        level--;
    if( level < 0 )
        return;

    cm = world->collision_maps[level];
    if( !cm )
        return;

    angle = (enum CollisionLocAngle)(map_loc->orientation & 0x3);
    blockrange = config_loc->blocks_projectiles ? 1 : 0;
    size_x = config_loc->size_x;
    size_z = config_loc->size_z;
    shape = map_loc->shape_select;

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
 * Place-time LinkBelow shift matches ClientBuild.finishBuild (trueLevel--):
 * stamp FLOOR onto the playable collision level so wall dual-stamps that also
 * target that map stay mirrored. A post-hoc whole-word column overwrite would
 * split wall edges (docs/COLLISION_MAP.md).
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
        int blocked = 0;

        for( int x = 0; x < scene_size; x++ )
        {
            for( int z = 0; z < scene_size; z++ )
            {
                int true_level;
                struct CollisionMap* cm;

                if( (flag_map_get(builder->flag_map, x, z, level) & RSCACHE_FLOFLAG_BLOCK) == 0 )
                    continue;

                true_level = level;
                if( (flag_map_get(builder->flag_map, x, z, 1) & RSCACHE_FLOFLAG_LINK_BELOW) != 0 )
                    true_level--;
                if( true_level < 0 || true_level >= COLLISION_LEVELS )
                    continue;

                cm = world->collision_maps[true_level];
                if( !cm )
                    continue;
                collision_map_add_floor(cm, x, z);
                blocked++;
            }
        }
        if( getenv("TORIRS_TERRAIN_DEBUG") )
            fprintf(stderr, "terrain: cache level %d blocked %d of %d tiles\n", level, blocked,
                    scene_size * scene_size);
    }
}

/*
 * Formerly a whole-flag-word column overwrite (0←1, 1←2, 2←3). That matched
 * geometry pushDown's column shape but split dual-tile wall stamps whenever
 * only one side of an edge was LinkBelow — one-way walk-through. Loc and
 * terrain collision now use Client-TS / LostCity place-time level shift, so
 * this pass is intentionally a no-op. Kept so RebuildCenterzoneEnd's call
 * site stays stable.
 */
static void
world_collision_apply_bridges(struct WorldBuilder* builder)
{
    (void)builder;
}

#endif
