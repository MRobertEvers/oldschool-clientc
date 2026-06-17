#include "world.h"

#include "collision_map.h"
#include "heightmap.h"
#include "minimap.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct World*
world_new(void)
{
    struct World* world = calloc(1, sizeof(struct World));
    assert(world && "Failed to allocate world");
    return world;
}

void
world_free(struct World* world)
{
    if( !world )
        return;
    if( world->heightmap )
        heightmap_free(world->heightmap);
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
    free(world);
}

void
world_reset_scene(
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

    if( world->heightmap )
        heightmap_free(world->heightmap);
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

int
world_terrain_element_at(
    struct World* world,
    int x,
    int z,
    int level)
{
    if( !world || !world->terrain_element_ids )
        return -1;
    if( x < 0 || z < 0 || level < 0 )
        return -1;
    if( x >= world->_scene_size || z >= world->_scene_size || level >= WORLD_MAP_TERRAIN_LEVELS )
        return -1;
    int idx = x + z * world->_scene_size + level * world->_scene_size * world->_scene_size;
    return world->terrain_element_ids[idx];
}
