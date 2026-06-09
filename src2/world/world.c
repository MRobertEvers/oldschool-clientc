#include "world.h"

#include "blendmap.h"
#include "collision_map.h"
#include "flag_map.h"
#include "gamecache/gamecache.h"
#include "heightmap.h"
#include "lightmap.h"
#include "minimap.h"
#include "osrs/rscache/tables/maps.h"
#include "overlaymap.h"
#include "shademap.h"
#include "sharelight_map.h"
#include "terrain_shapemap.h"
#include "world_scene.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include "world_terrain.u.c"
#include "world_collision.u.c"
#include "world_scenery.u.c"
#include "world_sharelight.u.c"
// clang-format on

struct World*
world_new(
    struct GameCache* gamecache,
    struct WorldScene* scene)
{
    struct World* world = calloc(1, sizeof(struct World));
    if( !world )
        return NULL;
    world->gamecache = gamecache;
    world->scene = scene;
    return world;
}

void
world_free(struct World* world)
{
    if( !world )
        return;
    if( world->heightmap )
        heightmap_free(world->heightmap);
    if( world->blendmap )
        blendmap_free(world->blendmap);
    if( world->overlaymap )
        overlaymap_free(world->overlaymap);
    if( world->terrain_shapemap )
        terrain_shape_map_free(world->terrain_shapemap);
    if( world->decor_buildmap )
        decor_buildmap_free(world->decor_buildmap);
    if( world->lightmap )
        lightmap_free(world->lightmap);
    if( world->sharelight_map )
        sharelight_map_free(world->sharelight_map);
    if( world->shademap )
        shademap2_free(world->shademap);
    if( world->flag_map )
        flag_map_free(world->flag_map);
    if( world->minimap )
        minimap_free(world->minimap);
    for( int i = 0; i < COLLISION_LEVELS; i++ )
    {
        if( world->collision_maps[i] )
            collision_map_free(world->collision_maps[i]);
    }
    if( world->cullmap )
        painters_cullmap_free(world->cullmap);
    if( world->painter )
        painter_free(world->painter);
    free(world->terrain_element_ids);
    free(world->contour_ground_queue);
    free(world);
}

void
world_set_painters_cullmap(
    struct World* world,
    struct PaintersCullMap* cm)
{
    if( !world )
        return;
    if( world->cullmap )
        painters_cullmap_free(world->cullmap);
    world->cullmap = cm;
    if( world->painter )
        painter_set_cullmap(world->painter, cm);
}

void
world_rebuild_centerzone_begin(
    struct World* world,
    int zone_center_x,
    int zone_center_z,
    int scene_size)
{
    int zone_padding = scene_size / (2 * 8);
    int zone_sw_x = zone_center_x - zone_padding;
    int zone_sw_z = zone_center_z - zone_padding;
    int zone_ne_x = zone_center_x + zone_padding;
    int zone_ne_z = zone_center_z + zone_padding;
    int world_sw_x = zone_sw_x * 8;
    int world_sw_z = zone_sw_z * 8;

    world->load_complete = false;
    world->contour_ground_queue_count = 0;

    world_scene_clear(world->scene);

    if( world->heightmap )
        heightmap_free(world->heightmap);
    if( world->blendmap )
        blendmap_free(world->blendmap);
    if( world->overlaymap )
        overlaymap_free(world->overlaymap);
    if( world->terrain_shapemap )
        terrain_shape_map_free(world->terrain_shapemap);
    if( world->decor_buildmap )
        decor_buildmap_free(world->decor_buildmap);
    if( world->lightmap )
        lightmap_free(world->lightmap);
    if( world->sharelight_map )
        sharelight_map_free(world->sharelight_map);
    if( world->shademap )
        shademap2_free(world->shademap);
    if( world->flag_map )
        flag_map_free(world->flag_map);
    if( world->minimap )
        minimap_free(world->minimap);
    if( world->cullmap )
    {
        painters_cullmap_free(world->cullmap);
        world->cullmap = NULL;
    }
    if( world->painter )
    {
        painter_free(world->painter);
        world->painter = NULL;
    }
    free(world->terrain_element_ids);
    world->terrain_element_ids = NULL;
    for( int i = 0; i < COLLISION_LEVELS; i++ )
    {
        if( world->collision_maps[i] )
            collision_map_free(world->collision_maps[i]);
        world->collision_maps[i] = NULL;
    }

    world->_offset_x = world_sw_x % 64;
    world->_offset_z = world_sw_z % 64;
    world->_base_tile_x = zone_sw_x * 8;
    world->_base_tile_z = zone_sw_z * 8;
    world->_chunk_sw_x = zone_sw_x / 8;
    world->_chunk_sw_z = zone_sw_z / 8;
    world->_chunk_ne_x = zone_ne_x / 8;
    world->_chunk_ne_z = zone_ne_z / 8;
    world->_scene_size = scene_size;

    world->heightmap = heightmap_new(scene_size + 1, scene_size + 1, WORLD_MAP_TERRAIN_LEVELS);
    world->blendmap = blendmap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    world->overlaymap = overlaymap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    world->terrain_shapemap =
        terrain_shape_map_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    world->decor_buildmap = decor_buildmap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    world->lightmap = lightmap_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    world->sharelight_map = sharelight_map_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    world->shademap = shademap2_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    world->flag_map = flag_map_new(scene_size, scene_size, WORLD_MAP_TERRAIN_LEVELS);
    for( int i = 0; i < COLLISION_LEVELS; i++ )
        world->collision_maps[i] = collision_map_new(scene_size, scene_size);
    world->minimap = minimap_new(scene_size, scene_size);

    world->painter = painter_new(
        scene_size,
        scene_size,
        WORLD_MAP_TERRAIN_LEVELS,
        PAINTER_NEW_CTX_BUCKET | PAINTER_NEW_CTX_WORLD3D);
    world->cullmap = painters_cullmap_new_nocull();
    if( world->painter )
        painter_set_cullmap(world->painter, world->cullmap);

    int terrain_tile_count = scene_size * scene_size * WORLD_MAP_TERRAIN_LEVELS;
    world->terrain_element_ids = malloc((size_t)terrain_tile_count * sizeof(int));
    if( world->terrain_element_ids )
        memset(world->terrain_element_ids, 0xFF, (size_t)terrain_tile_count * sizeof(int));
}

void
world_rebuild_centerzone_chunk_scenery(
    struct World* world,
    int mapx,
    int mapz)
{
    int map_id = (mapx << 16) | (mapz & 0xFFFF);
    struct GameCache_MapLocs* map_locs = gamecache_map_scenery_get(world->gamecache, map_id);
    assert(map_locs && "Map scenery must be found");

    int scene_size = world->_scene_size;

    world_minimap_add_chunk_walls(world, mapx, mapz);

    for( int i = 0; i < map_locs->locs_count; i++ )
    {
        struct GameCache_MapLoc* map_loc = &map_locs->locs[i];
        struct GameCache_Location* config_loc =
            gamecache_location_get(world->gamecache, map_loc->loc_id);
        if( !config_loc )
            continue;

        int scene_x = world_to_scene_x(world, mapx, map_loc->chunk_pos_x);
        int scene_z = world_to_scene_z(world, mapz, map_loc->chunk_pos_z);

        if( scene_x < 0 || scene_z < 0 || scene_x >= scene_size || scene_z >= scene_size )
            continue;

        world_collision_add_loc(world, map_loc, config_loc, scene_x, scene_z);
        scenery_add(world, map_loc, config_loc, scene_x, scene_z);
    }
}

void
world_rebuild_centerzone_end(struct World* world)
{
    int scene_size = world->_scene_size;

    world_collision_apply_bridges(world);
    world_contour_ground(world);
    world_apply_wall_decor_offsets(world);

    if( world->decor_buildmap )
    {
        decor_buildmap_free(world->decor_buildmap);
        world->decor_buildmap = NULL;
    }

    if( world->painter && world->flag_map )
    {
        struct PaintersTile bridge_tile_tmp = { 0 };
        for( int x = 0; x < scene_size; x++ )
        {
            for( int z = 0; z < scene_size; z++ )
            {
                int bridge_flags = flag_map_get(world->flag_map, x, z, 1);
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
                int link_l1 = (flag_map_get(world->flag_map, x, z, 1) & FLOFLAG_LINK_BELOW) != 0;
                for( int g = 0; g < painter_max_levels(world->painter); g++ )
                {
                    int src = link_l1 ? (g < 3 ? g + 1 : 0) : g;
                    uint8_t st = (uint8_t)flag_map_get(world->flag_map, x, z, src);
                    int draw = map_floor_vis_below_draw_level(st, src, link_l1);
                    painter_tile_set_draw_level(world->painter, x, z, g, draw);
                }
            }
        }
    }

    if( world->flag_map )
    {
        flag_map_free(world->flag_map);
        world->flag_map = NULL;
    }

    if( world->painter )
        painter_mark_static_count(world->painter);

    world_build_scene_terrain(world);
    world_build_lighting(world);
    if( world->sharelight_map )
    {
        sharelight_map_free(world->sharelight_map);
        world->sharelight_map = NULL;
    }
    if( world->shademap )
    {
        shademap2_free(world->shademap);
        world->shademap = NULL;
    }
    world->load_complete = true;
}

void
world_rebuild_centerzone_chunk(
    struct World* world,
    int mapx,
    int mapz)
{
    world_rebuild_centerzone_chunk_terrain(world, mapx, mapz);
    world_rebuild_centerzone_chunk_scenery(world, mapx, mapz);
}

void
world_rebuild_centerzone(
    struct World* world,
    int zone_center_x,
    int zone_center_z,
    int scene_size)
{
    world_rebuild_centerzone_begin(world, zone_center_x, zone_center_z, scene_size);

    world_scene_batch_begin(world->scene);

    for( int mapx = world->_chunk_sw_x; mapx <= world->_chunk_ne_x; mapx++ )
    {
        for( int mapz = world->_chunk_sw_z; mapz <= world->_chunk_ne_z; mapz++ )
            world_rebuild_centerzone_chunk_terrain(world, mapx, mapz);
    }

    for( int mapx = world->_chunk_sw_x; mapx <= world->_chunk_ne_x; mapx++ )
    {
        for( int mapz = world->_chunk_sw_z; mapz <= world->_chunk_ne_z; mapz++ )
            world_rebuild_centerzone_chunk_scenery(world, mapx, mapz);
    }

    world_rebuild_centerzone_end(world);

    world_scene_batch_end(world->scene);
}
