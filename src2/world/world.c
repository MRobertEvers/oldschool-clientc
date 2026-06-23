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

int
world_projectile_spawn(
    struct World* world,
    int element_id,
    int level,
    int pos_x,
    int pos_z,
    int vel_x,
    int vel_z,
    int yaw)
{
    if( !world || element_id < 0 )
        return -1;

    int idx = -1;
    for( int i = 0; i < world->projectile_count; i++ )
    {
        if( !world->projectiles[i].alive )
        {
            idx = i;
            break;
        }
    }

    if( idx < 0 )
    {
        if( world->projectile_count >= WORLD_MAX_PROJECTILES )
            return -1;
        idx = world->projectile_count++;
    }

    world->projectiles[idx] = (struct WorldProjectile){
        .alive = true,
        .element_id = element_id,
        .level = level,
        .pos_x = pos_x,
        .pos_z = pos_z,
        .vel_x = vel_x,
        .vel_z = vel_z,
        .yaw = yaw,
    };
    return idx;
}

#define WORLD_PROJECTILE_PAINTER_PADDING 60

struct WorldPainterFootprint
{
    int sx;
    int sz;
    int size_x;
    int size_z;
};

static void
world_projectile_painter_footprint(
    int pos_x,
    int pos_z,
    int draw_padding,
    int scene_size,
    struct WorldPainterFootprint* out)
{
    int x0 = (pos_x - draw_padding) / 128;
    int z0 = (pos_z - draw_padding) / 128;
    int x1 = (pos_x + draw_padding) / 128;
    int z1 = (pos_z + draw_padding) / 128;

    if( x0 < 0 )
        x0 = 0;
    if( z0 < 0 )
        z0 = 0;
    if( x1 >= scene_size )
        x1 = scene_size - 1;
    if( z1 >= scene_size )
        z1 = scene_size - 1;

    out->sx = x0;
    out->sz = z0;
    out->size_x = x1 - x0 + 1;
    out->size_z = z1 - z0 + 1;
}

void
world_projectile_despawn(
    struct World* world,
    int idx)
{
    if( !world || idx < 0 || idx >= world->projectile_count )
        return;
    world->projectiles[idx].alive = false;
}

void
world_cycle(
    struct World* world,
    int cycles_elapsed)
{
    if( !world || !world->painter || !world->load_complete )
        return;

    painter_reset_to_static(world->painter);

    for( int i = 0; i < world->projectile_count; i++ )
    {
        struct WorldProjectile* p = &world->projectiles[i];
        if( !p->alive )
            continue;

        if( cycles_elapsed > 0 )
        {
            p->pos_x += p->vel_x * cycles_elapsed;
            p->pos_z += p->vel_z * cycles_elapsed;
        }

        int grid_x = p->pos_x >> 7;
        int grid_z = p->pos_z >> 7;
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size || grid_z >= world->_scene_size )
        {
            world_projectile_despawn(world, i);
            continue;
        }

        struct WorldPainterFootprint footprint;
        world_projectile_painter_footprint(
            p->pos_x,
            p->pos_z,
            WORLD_PROJECTILE_PAINTER_PADDING,
            world->_scene_size,
            &footprint);

        painter_add_normal_scenery(
            world->painter,
            footprint.sx,
            footprint.sz,
            p->level,
            p->element_id,
            footprint.size_x,
            footprint.size_z);
    }
}
