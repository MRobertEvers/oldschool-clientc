#include "world_builder.h"

#include "blendmap.h"
#include "collision_map.h"
#include "decor_buildmap.h"
#include "flag_map.h"
#include "gamecache/gamecache.h"
#include "heightmap.h"
#include "lightmap.h"
#include "minimap.h"
#include "osrs/painters.h"
#include "osrs/rscache/tables/maps.h"
#include "overlaymap.h"
#include "shademap.h"
#include "sharelight_map.h"
#include "terrain_shapemap.h"
#include "toridraw/toridraw_gccontext.h"
#include "toridrawx/toridrawx.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include "world_terrain.u.c"
#include "world_collision.u.c"
#include "world_scenery.u.c"
#include "world_sharelight.u.c"
// clang-format on

static void
world_builder_free_transient_maps(struct WorldBuilder* builder)
{
    if( !builder )
        return;
    if( builder->blendmap )
        blendmap_free(builder->blendmap);
    if( builder->overlaymap )
        overlaymap_free(builder->overlaymap);
    if( builder->terrain_shapemap )
        terrain_shape_map_free(builder->terrain_shapemap);
    if( builder->decor_buildmap )
        decor_buildmap_free(builder->decor_buildmap);
    if( builder->lightmap )
        lightmap_free(builder->lightmap);
    if( builder->sharelight_map )
        sharelight_map_free(builder->sharelight_map);
    if( builder->shademap )
        shademap2_free(builder->shademap);
    if( builder->flag_map )
        flag_map_free(builder->flag_map);
    free(builder->contour_ground_queue);
    builder->blendmap = NULL;
    builder->overlaymap = NULL;
    builder->terrain_shapemap = NULL;
    builder->decor_buildmap = NULL;
    builder->lightmap = NULL;
    builder->sharelight_map = NULL;
    builder->shademap = NULL;
    builder->flag_map = NULL;
    builder->contour_ground_queue = NULL;
    builder->contour_ground_queue_count = 0;
    builder->contour_ground_queue_cap = 0;
}

struct WorldBuilder*
world_builder_new(
    struct World* world,
    struct GameCache* gamecache,
    struct ToriDraw_Context* context,
    struct ToriDrawX* toridrawx)
{
    struct WorldBuilder* builder = calloc(1, sizeof(struct WorldBuilder));
    assert(builder && "Failed to allocate world builder");
    builder->world = world;
    builder->gamecache = gamecache;
    builder->context = context;
    builder->toridrawx = toridrawx;
    return builder;
}

void
world_builder_free(struct WorldBuilder* builder)
{
    if( !builder )
        return;
    world_builder_free_transient_maps(builder);
    free(builder);
}

void
world_builder_rebuild_centerzone_begin(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    int scene_size)
{
    struct World* world = builder->world;
    assert(world && "world_builder_rebuild_centerzone_begin: world is NULL");

    world_builder_free_transient_maps(builder);
    world_reset_scene(world, zone_center_x, zone_center_z, scene_size);

    toridraw_gc_clear_scene(builder->context);

    builder->blendmap = blendmap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->overlaymap = overlaymap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->terrain_shapemap =
        terrain_shape_map_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->decor_buildmap = decor_buildmap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->lightmap = lightmap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->sharelight_map = sharelight_map_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->shademap = shademap2_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    builder->flag_map = flag_map_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
}

void
world_builder_rebuild_centerzone_chunk_scenery(
    struct WorldBuilder* builder,
    int mapx,
    int mapz)
{
    struct World* world = builder->world;
    int map_id = (mapx << 16) | (mapz & 0xFFFF);
    struct GameCache_MapLocs* map_locs = gamecache_map_scenery_get(builder->gamecache, map_id);
    assert(map_locs && "Map scenery must be found");

    int scene_size = world->_scene_size;

    world_builder_minimap_add_chunk_walls(builder, mapx, mapz);

    for( int i = 0; i < map_locs->locs_count; i++ )
    {
        struct GameCache_MapLoc* map_loc = &map_locs->locs[i];
        struct GameCache_Location* config_loc =
            gamecache_location_get(builder->gamecache, map_loc->loc_id);
        if( !config_loc )
            continue;

        int scene_x = world_to_scene_x(world, mapx, map_loc->chunk_pos_x);
        int scene_z = world_to_scene_z(world, mapz, map_loc->chunk_pos_z);

        if( scene_x < 0 || scene_z < 0 || scene_x >= scene_size || scene_z >= scene_size )
            continue;

        world_collision_add_loc(builder, map_loc, config_loc, scene_x, scene_z);
        scenery_add(builder, map_loc, config_loc, scene_x, scene_z);
    }
}

void
world_builder_rebuild_centerzone_end(struct WorldBuilder* builder)
{
    struct World* world = builder->world;
    int scene_size = world->_scene_size;

    world_collision_apply_bridges(builder);
    world_contour_ground(builder);
    world_builder_apply_wall_decor_offsets(builder);

    if( builder->decor_buildmap )
    {
        decor_buildmap_free(builder->decor_buildmap);
        builder->decor_buildmap = NULL;
    }

    if( world->painter && builder->flag_map )
    {
        struct PaintersTile bridge_tile_tmp = { 0 };
        for( int x = 0; x < scene_size; x++ )
        {
            for( int z = 0; z < scene_size; z++ )
            {
                int bridge_flags = flag_map_get(builder->flag_map, x, z, 1);
                if( (bridge_flags & FLOFLAG_LINK_BELOW) == 0 )
                    continue;

                bridge_tile_tmp = *painter_tile_at(world->painter, x, z, 0);
                for( int level = 0; level < painter_max_levels(world->painter) - 1; level++ )
                    painter_tile_copyto(world->painter, x, z, level + 1, x, z, level);

                *painter_tile_at(world->painter, x, z, 3) = bridge_tile_tmp;
                painters_tile_set_grid_level(painter_tile_at(world->painter, x, z, 3), 3);
                painter_tile_set_bridge(world->painter, x, z, 0, x, z, 3);
            }
        }

        for( int x = 0; x < scene_size; x++ )
        {
            for( int z = 0; z < scene_size; z++ )
            {
                int link_l1 = (flag_map_get(builder->flag_map, x, z, 1) & FLOFLAG_LINK_BELOW) != 0;
                for( int g = 0; g < painter_max_levels(world->painter); g++ )
                {
                    int src = link_l1 ? (g < 3 ? g + 1 : 0) : g;
                    uint8_t st = (uint8_t)flag_map_get(builder->flag_map, x, z, src);
                    int draw = map_floor_vis_below_draw_level(st, src, link_l1);
                    painter_tile_set_draw_level(world->painter, x, z, g, draw);
                }
            }
        }
    }

    if( builder->flag_map )
    {
        flag_map_free(builder->flag_map);
        builder->flag_map = NULL;
    }

    if( world->painter )
        painter_mark_static_count(world->painter);

    world_build_scene_terrain(builder);
    world_build_lighting(builder);

    if( builder->sharelight_map )
    {
        sharelight_map_free(builder->sharelight_map);
        builder->sharelight_map = NULL;
    }
    if( builder->shademap )
    {
        shademap2_free(builder->shademap);
        builder->shademap = NULL;
    }
    if( builder->blendmap )
    {
        blendmap_free(builder->blendmap);
        builder->blendmap = NULL;
    }
    if( builder->overlaymap )
    {
        overlaymap_free(builder->overlaymap);
        builder->overlaymap = NULL;
    }
    if( builder->terrain_shapemap )
    {
        terrain_shape_map_free(builder->terrain_shapemap);
        builder->terrain_shapemap = NULL;
    }
    if( builder->lightmap )
    {
        lightmap_free(builder->lightmap);
        builder->lightmap = NULL;
    }

    world->load_complete = true;
}

void
world_builder_rebuild_centerzone_chunk(
    struct WorldBuilder* builder,
    int mapx,
    int mapz)
{
    world_builder_rebuild_centerzone_chunk_terrain(builder, mapx, mapz);
    world_builder_rebuild_centerzone_chunk_scenery(builder, mapx, mapz);
}

void
world_builder_rebuild_centerzone(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    int scene_size)
{
    world_builder_rebuild_centerzone_begin(builder, zone_center_x, zone_center_z, scene_size);

    toridraw_gc_batch_begin(builder->context);

    struct World* world = builder->world;
    for( int mapx = world->_chunk_sw_x; mapx <= world->_chunk_ne_x; mapx++ )
    {
        for( int mapz = world->_chunk_sw_z; mapz <= world->_chunk_ne_z; mapz++ )
            world_builder_rebuild_centerzone_chunk_terrain(builder, mapx, mapz);
    }

    for( int mapx = world->_chunk_sw_x; mapx <= world->_chunk_ne_x; mapx++ )
    {
        for( int mapz = world->_chunk_sw_z; mapz <= world->_chunk_ne_z; mapz++ )
            world_builder_rebuild_centerzone_chunk_scenery(builder, mapx, mapz);
    }

    world_builder_rebuild_centerzone_end(builder);

    toridraw_gc_batch_end(builder->context);
}
