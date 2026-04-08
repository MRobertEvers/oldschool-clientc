#include "world.h"

#include "blendmap.h"
#include "datatypes/appearances.h"
#include "game.h"
#include "heightmap.h"
#include "terrain_shapemap.h"
#include "world_scenebuild.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include "world_scenery.u.c"
#include "world_terrain.u.c"
#include "world_sharelight.u.c"
#include "world_cycle.u.c"
// clang-format on

#define CURRENT_LEVEL 0

static void
init_map_build_loc_entity(
    struct MapBuildLocEntity* map_build_loc_entity,
    int entity_id)
{
    memset(map_build_loc_entity, 0, sizeof(struct MapBuildLocEntity));
    map_build_loc_entity->scene_element.element_id = -1;
    map_build_loc_entity->entity_id = entity_id;
    map_build_loc_entity->scene_element_two.element_id = -1;
}

static struct MapBuildLocEntity*
next_map_build_loc_entity(struct World* world)
{
    if( world->active_loc_entity_count < MAX_MAP_BUILD_LOC_ENTITIES )
    {
        int entity_id = world->active_loc_entity_count;
        struct MapBuildLocEntity* loc = world_loc_entity_ensure(world, entity_id);
        init_map_build_loc_entity(loc, entity_id);
        world->active_loc_entities[world->active_loc_entity_count] = entity_id;
        world->active_loc_entity_count++;
        return loc;
    }
    assert(false && "No more map build loc entities");
    return NULL;
}

/** Slot 0 would otherwise match `entity_id != id` never (0==0) while still all-zero from pool. */
static void
world_prime_map_build_tile_slot0(struct World* world)
{
    struct MapBuildTileEntity* e =
        ENTITY_VEC_ENSURE(world->map_build_tile_entities, struct MapBuildTileEntity, 0);
    memset(e, 0, sizeof(*e));
    e->scene_element.element_id = -1;
    e->entity_id = 0;
}

struct World*
world_new(
    struct BuildCacheDat* buildcachedat,
    struct Scene2* scene2_shared)
{
    assert(scene2_shared != NULL && "World requires caller-owned Scene2");
    struct World* world = malloc(sizeof(struct World));
    memset(world, 0, sizeof(struct World));

    world->scene2 = scene2_shared;
    world->painter = NULL;
    world->collision_map = NULL;
    world->heightmap = NULL;
    world->minimap = NULL;
    world->buildcachedat = buildcachedat;

    entity_vec_init(&world->players, sizeof(struct PlayerEntity), MAX_PLAYERS);
    entity_vec_init(&world->npcs, sizeof(struct NPCEntity), MAX_NPCS);
    entity_vec_init(
        &world->map_build_loc_entities,
        sizeof(struct MapBuildLocEntity),
        MAX_MAP_BUILD_LOC_ENTITIES);
    entity_vec_init(
        &world->map_build_tile_entities,
        sizeof(struct MapBuildTileEntity),
        MAX_MAP_BUILD_TILE_ENTITIES);

    world_prime_map_build_tile_slot0(world);

    /* Local player is fixed at ACTIVE_PLAYER_SLOT; prime so world_player(world, ...) is in range.
     */
    world_player_ensure(world, ACTIVE_PLAYER_SLOT);

    return world;
}

void
world_free(struct World* world)
{
    if( !world )
        return;

    for( int i = 0; i < entity_vec_count(&world->map_build_loc_entities); i++ )
        free(world_loc_entity(world, i)->actions);

    for( int i = 0; i < entity_vec_count(&world->npcs); i++ )
    {
        struct NPCEntity* npc = world_npc(world, i);
        free(npc->name);
        free(npc->description);
        free(npc->actions);
    }

    entity_vec_free(&world->players);
    entity_vec_free(&world->npcs);
    entity_vec_free(&world->map_build_loc_entities);
    entity_vec_free(&world->map_build_tile_entities);

    painter_free(world->painter);
    collision_map_free(world->collision_map);
    heightmap_free(world->heightmap);
    minimap_free(world->minimap);
    for( int ti = 0; ti < MAP_TERRAIN_LEVELS; ti++ )
    {
        if( world->terrain_va[ti] )
        {
            dashvertexarray_free(world->terrain_va[ti]);
            world->terrain_va[ti] = NULL;
        }
        if( world->terrain_face_array[ti] )
        {
            dashfacearray_free(world->terrain_face_array[ti]);
            world->terrain_face_array[ti] = NULL;
        }
    }
    world->scene2 = NULL;
    if( world->decor_buildmap )
        decor_buildmap_free(world->decor_buildmap);
    if( world->lightmap )
        lightmap_free(world->lightmap);
    if( world->shademap )
        shademap2_free(world->shademap);
    if( world->overlaymap )
        overlaymap_free(world->overlaymap);
    if( world->sharelight_map )
        sharelight_map_free(world->sharelight_map);
    if( world->blendmap )
        blendmap_free(world->blendmap);
    if( world->terrain_shapemap )
        terrain_shape_map_free(world->terrain_shapemap);
    free(world);
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

static inline int
world_to_scene_x(
    struct World* world,
    int mapx,
    int chunk_x)
{
    return (chunk_x - world->_offset_x) + (mapx - world->_chunk_sw_x) * MAP_TERRAIN_X;
}

static inline int
world_to_scene_z(
    struct World* world,
    int mapz,
    int chunk_z)
{
    return (chunk_z - world->_offset_z) + (mapz - world->_chunk_sw_z) * MAP_TERRAIN_Z;
}

struct FlagMap
{
    int* flags;
    int size_x;
    int size_z;
    int levels;
};

struct FlagMap*
flag_map_new(
    int size_x,
    int size_z,
    int levels)
{
    struct FlagMap* flag_map = malloc(sizeof(struct FlagMap));
    flag_map->flags = malloc(sizeof(int) * size_x * size_z * levels);
    memset(flag_map->flags, 0, sizeof(int) * size_x * size_z * levels);
    flag_map->size_x = size_x;
    flag_map->size_z = size_z;
    flag_map->levels = levels;
    return flag_map;
};

void
flag_map_free(struct FlagMap* flag_map)
{
    free(flag_map->flags);
    free(flag_map);
}

static inline int
flag_map_index(
    struct FlagMap* flag_map,
    int x,
    int z,
    int level)
{
    assert(x >= 0 && x < flag_map->size_x);
    assert(z >= 0 && z < flag_map->size_z);
    assert(level >= 0 && level < flag_map->levels);
    return x + z * flag_map->size_x + level * flag_map->size_x * flag_map->size_z;
}
int
flag_map_get(
    struct FlagMap* flag_map,
    int x,
    int z,
    int level)
{
    return flag_map->flags[flag_map_index(flag_map, x, z, level)];
}

void
flag_map_set(
    struct FlagMap* flag_map,
    int x,
    int z,
    int level,
    int flag)
{
    flag_map->flags[flag_map_index(flag_map, x, z, level)] = flag;
}

#include "platforms/common/platform_memory.h"

static void
world_print_scene2_dashmodel_heap_stats(struct World* world)
{
    struct Scene2* scene2 = world->scene2;
    if( !scene2 )
        return;

    printf("world_buildcachedat_rebuild_centerzone: DashModel heap per scene2 element:\n");
    size_t total = 0;
    long long total_vertices = 0;
    long long total_faces = 0;
    int count = 0;
    for( int i = 0; i < scene2_elements_total(scene2); i++ )
    {
        struct Scene2Element* el = scene2_element_at((struct Scene2*)scene2, i);
        struct DashModel* m = scene2_element_dash_model(el);
        if( !m )
            continue;
        size_t bytes = dashmodel_heap_bytes(m);
        total += bytes;
        total_vertices += dashmodel_vertex_count(m);
        total_faces += dashmodel_face_count(m);
        count++;
        // printf(
        //     "  element %d: %zu bytes, vertices %d, faces %d\n",
        //     i,
        //     bytes,
        //     m->vertex_count,
        //     m->face_count);
    }
    // printf(
    //     "world_buildcachedat_rebuild_centerzone: %d DashModels, total heap %zu bytes, "
    //     "vertices %lld, faces %lld\n",
    //     count,
    //     total,
    //     total_vertices,
    //     total_faces);
}

void
world_buildcachedat_rebuild_centerzone(
    struct World* world,
    int zone_center_x,
    int zone_center_z,
    int scene_size)
{
    struct BuildCacheDat* buildcachedat = world->buildcachedat;

    // Rebuild the center zone
    int zone_padding = scene_size / (2 * 8);
    int zone_sw_x = zone_center_x - zone_padding;
    int zone_sw_z = zone_center_z - zone_padding;
    int zone_ne_x = zone_center_x + zone_padding;
    int zone_ne_z = zone_center_z + zone_padding;

    int world_sw_x = zone_sw_x * 8;
    int world_sw_z = zone_sw_z * 8;
    int world_ne_x = zone_ne_x * 8;
    int world_ne_z = zone_ne_z * 8;

    world->_offset_x = world_sw_x % 64;
    world->_offset_z = world_sw_z % 64;

    world->_base_tile_x = zone_sw_x * 8;
    world->_base_tile_z = zone_sw_z * 8;

    if( world->painter )
        painter_free(world->painter);
    if( world->collision_map )
        collision_map_free(world->collision_map);
    if( world->heightmap )
        heightmap_free(world->heightmap);
    if( world->minimap )
        minimap_free(world->minimap);

    if( world->lightmap )
        lightmap_free(world->lightmap);
    if( world->shademap )
        shademap2_free(world->shademap);
    if( world->blendmap )
        blendmap_free(world->blendmap);
    if( world->overlaymap )
        overlaymap_free(world->overlaymap);
    if( world->terrain_shapemap )
        terrain_shape_map_free(world->terrain_shapemap);
    if( world->decor_buildmap )
        decor_buildmap_free(world->decor_buildmap);
    if( world->sharelight_map )
        sharelight_map_free(world->sharelight_map);

    for( int ti = 0; ti < MAP_TERRAIN_LEVELS; ti++ )
    {
        if( world->terrain_va[ti] )
        {
            dashvertexarray_free(world->terrain_va[ti]);
            world->terrain_va[ti] = NULL;
        }
        if( world->terrain_face_array[ti] )
        {
            dashfacearray_free(world->terrain_face_array[ti]);
            world->terrain_face_array[ti] = NULL;
        }
    }

    {
        int prev_scene = world->_scene_size;
        int tile_slots = 0;
        if( prev_scene > 0 )
            tile_slots = prev_scene * prev_scene * MAP_TERRAIN_LEVELS;
        else
            tile_slots = entity_vec_count(&world->map_build_tile_entities);
        if( tile_slots > MAX_MAP_BUILD_TILE_ENTITIES )
            tile_slots = MAX_MAP_BUILD_TILE_ENTITIES;
        for( int i = 0; i < tile_slots; i++ )
        {
            world_cleanup_map_build_tile_entity(world, i);
        }
        entity_vec_free(&world->map_build_tile_entities);
        entity_vec_init(
            &world->map_build_tile_entities,
            sizeof(struct MapBuildTileEntity),
            MAX_MAP_BUILD_TILE_ENTITIES);
        world_prime_map_build_tile_slot0(world);
    }
    for( int i = 0; i < world->active_loc_entity_count; i++ )
    {
        world_cleanup_map_build_loc_entity(world, world->active_loc_entities[i]);
    }
    world->active_loc_entity_count = 0;

    struct PlatformMemoryInfo mem = { 0 };
    platform_get_memory_info(&mem);
    printf(
        "Pre-Alloc: Memory info: %zu / %zu / %zu\n", mem.heap_used, mem.heap_total, mem.heap_peak);

    world->painter = painter_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->collision_map = collision_map_new(scene_size, scene_size);
    world->heightmap = heightmap_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->minimap = minimap_new(scene_size, scene_size);

    world->lightmap = lightmap_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->shademap = shademap2_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->blendmap = blendmap_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->overlaymap = overlaymap_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->terrain_shapemap = terrain_shape_map_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->decor_buildmap = decor_buildmap_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->sharelight_map = sharelight_map_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);

    struct PlatformMemoryInfo mem2 = { 0 };
    platform_get_memory_info(&mem2);
    printf(
        "Post-Alloc: Memory info: %zu / %zu / %zu\n",
        mem2.heap_used,
        mem2.heap_total,
        mem2.heap_peak);

    int chunk_sw_x = zone_sw_x / 8;
    int chunk_sw_z = zone_sw_z / 8;
    int chunk_ne_x = zone_ne_x / 8;
    int chunk_ne_z = zone_ne_z / 8;
    world->_chunk_sw_x = chunk_sw_x;
    world->_chunk_sw_z = chunk_sw_z;
    world->_scene_size = scene_size;

    struct FlagMap* flag_map = flag_map_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);

    /**
     * Heightmap.
     */
    int mapx;
    int mapz;
    int offset_x;
    int offset_z;
    struct CacheMapTerrain* map_terrain = NULL;
    struct CacheMapLocs* map_locs = NULL;
    for( mapx = chunk_sw_x; mapx <= chunk_ne_x; mapx++ )
    {
        for( mapz = chunk_sw_z; mapz <= chunk_ne_z; mapz++ )
        {
            map_terrain = buildcachedat_get_map_terrain(buildcachedat, mapx, mapz);
            assert(map_terrain && "Map terrain must be found");

            for( int tile_x = 0; tile_x < MAP_TERRAIN_X; tile_x++ )
            {
                for( int tile_z = 0; tile_z < MAP_TERRAIN_Z; tile_z++ )
                {
                    for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
                    {
                        offset_x = world_to_scene_x(world, mapx, tile_x);
                        offset_z = world_to_scene_z(world, mapz, tile_z);

                        if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size ||
                            offset_z >= scene_size )
                            continue;

                        int chunk_index = MAP_TILE_COORD(tile_x, tile_z, level);
                        struct CacheMapFloor* tile = &map_terrain->tiles_xyz[chunk_index];
                        int height = tile->height;
                        heightmap_set(world->heightmap, offset_x, offset_z, level, height);

                        flag_map_set(flag_map, offset_x, offset_z, level, tile->settings);
                    }
                }
            }
        }
    }

    /**
     * Collision map.
     */
    struct CacheMapLoc* map_tile = NULL;
    struct CacheConfigLocation* config_loc = NULL;
    for( mapx = chunk_sw_x; mapx <= chunk_ne_x; mapx++ )
    {
        for( mapz = chunk_sw_z; mapz <= chunk_ne_z; mapz++ )
        {
            map_locs = buildcachedat_get_scenery(buildcachedat, mapx, mapz);
            assert(map_terrain && "Map terrain must be found");

            for( int i = 0; i < map_locs->locs_count; i++ )
            {
                map_tile = &map_locs->locs[i];

                offset_x = world_to_scene_x(world, mapx, map_tile->chunk_pos_x);
                offset_z = world_to_scene_z(world, mapz, map_tile->chunk_pos_z);

                if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size ||
                    offset_z >= scene_size )
                    continue;

                config_loc = buildcachedat_get_config_loc(buildcachedat, map_tile->loc_id);
                assert(config_loc && "Config location must be found");

                int size_x = config_loc->size_x;
                int size_z = config_loc->size_z;
                int angle = map_tile->orientation;
                int blockrange = config_loc->blocks_projectiles ? 1 : 0;

                switch( map_tile->shape_select )
                {
                case LOC_SHAPE_FLOOR_DECORATION: /* Client: LocShape.GROUND_DECOR -> blockGround */
                {
                    // Seen in OS1. LC254 only has true and false.
                    // The default for Ground Decor is to NOT block walk.
                    // The default for other locs is to block walk.
                    if( config_loc->blocks_walk == 1 )
                        collision_map_add_floor(world->collision_map, offset_x, offset_z);
                    break;
                }
                case LOC_SHAPE_WALL_SINGLE_SIDE:
                {
                    if( config_loc->blocks_walk != 0 )
                        collision_map_add_wall(
                            world->collision_map,
                            offset_x,
                            offset_z,
                            LOC_SHAPE_WALL_SINGLE_SIDE,
                            angle,
                            blockrange);
                    break;
                }
                case LOC_SHAPE_WALL_TRI_CORNER:
                {
                    if( config_loc->blocks_walk != 0 )
                        collision_map_add_wall(
                            world->collision_map,
                            offset_x,
                            offset_z,
                            LOC_SHAPE_WALL_TRI_CORNER,
                            angle,
                            blockrange);
                    break;
                }
                case LOC_SHAPE_WALL_TWO_SIDES:
                {
                    if( config_loc->blocks_walk != 0 )
                        collision_map_add_wall(
                            world->collision_map,
                            offset_x,
                            offset_z,
                            LOC_SHAPE_WALL_TWO_SIDES,
                            angle,
                            blockrange);
                    break;
                }
                case LOC_SHAPE_WALL_RECT_CORNER:
                {
                    if( config_loc->blocks_walk != 0 )
                        collision_map_add_wall(
                            world->collision_map,
                            offset_x,
                            offset_z,
                            LOC_SHAPE_WALL_RECT_CORNER,
                            angle,
                            blockrange);
                    break;
                }
                case LOC_SHAPE_WALL_DIAGONAL:
                {
                    if( config_loc->blocks_walk != 0 )
                        collision_map_add_loc(
                            world->collision_map,
                            offset_x,
                            offset_z,
                            size_x,
                            size_z,
                            angle,
                            blockrange);
                    break;
                }
                case LOC_SHAPE_SCENERY:
                {
                    if( config_loc->blocks_walk != 0 )
                        collision_map_add_loc(
                            world->collision_map,
                            offset_x,
                            offset_z,
                            size_x,
                            size_z,
                            angle,
                            blockrange);
                    break;
                }
                case LOC_SHAPE_SCENERY_DIAGIONAL:
                    /* Client-TS uses same addLoc(x, z, loc.width, loc.length, angle) for both
                     * CENTREPIECE_STRAIGHT and CENTREPIECE_DIAGONAL. Diagonal only adds yaw for
                     * rendering. Use 1x1 so diagonal blocks only the placement tile; full (sx,sz)
                     * can over-block and break pathfinding. */
                    {
                        if( config_loc->blocks_walk != 0 )
                            collision_map_add_loc(
                                world->collision_map,
                                offset_x,
                                offset_z,
                                size_x,
                                size_z,
                                angle,
                                blockrange);
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
                        collision_map_add_loc(
                            world->collision_map,
                            offset_x,
                            offset_z,
                            size_x,
                            size_z,
                            angle,
                            blockrange);
                    break;
                }
                default:
                    break;
                }
            }
        }
    }

    /**
     * Minimap Locs
     */
    for( mapx = chunk_sw_x; mapx <= chunk_ne_x; mapx++ )
    {
        for( mapz = chunk_sw_z; mapz <= chunk_ne_z; mapz++ )
        {
            map_locs = buildcachedat_get_scenery(buildcachedat, mapx, mapz);
            assert(map_locs && "Map scenery must be found");

            for( int i = 0; i < map_locs->locs_count; i++ )
            {
                map_tile = &map_locs->locs[i];
                offset_x = world_to_scene_x(world, mapx, map_tile->chunk_pos_x);
                offset_z = world_to_scene_z(world, mapz, map_tile->chunk_pos_z);

                if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size ||
                    offset_z >= scene_size )
                    continue;

                int level = map_tile->chunk_pos_level;
                if( config_loc->map_scene_id == -1 )
                    continue;

                if( level != CURRENT_LEVEL )
                    continue;

                switch( map_tile->shape_select )
                {
                case LOC_SHAPE_WALL_SINGLE_SIDE:
                {
                    minimap_add_tile_wall(
                        world->minimap,
                        offset_x,
                        offset_z,
                        orientation_wall_flag(map_tile->orientation));
                    break;
                }
                case LOC_SHAPE_WALL_TRI_CORNER:
                    break;
                case LOC_SHAPE_WALL_TWO_SIDES:
                {
                    minimap_add_tile_wall(
                        world->minimap,
                        offset_x,
                        offset_z,
                        orientation_wall_flag(map_tile->orientation));

                    int next_orientation = (map_tile->orientation + 1) & 0x3;

                    minimap_add_tile_wall(
                        world->minimap,
                        offset_x,
                        offset_z,
                        orientation_wall_flag(next_orientation));
                    break;
                }
                case LOC_SHAPE_WALL_RECT_CORNER:
                    break;
                case LOC_SHAPE_WALL_DIAGONAL:
                {
                    minimap_add_tile_wall(
                        world->minimap,
                        offset_x,
                        offset_z,
                        orientation_wall_flag_diagonal(map_tile->orientation));
                    break;
                }
                case LOC_SHAPE_SCENERY:
                case LOC_SHAPE_SCENERY_DIAGIONAL:
                case LOC_SHAPE_ROOF_SLOPED:
                case LOC_SHAPE_ROOF_SLOPED_OUTER_CORNER:
                case LOC_SHAPE_ROOF_SLOPED_INNER_CORNER:
                case LOC_SHAPE_ROOF_SLOPED_HARD_INNER_CORNER:
                case LOC_SHAPE_ROOF_SLOPED_HARD_OUTER_CORNER:
                case LOC_SHAPE_ROOF_FLAT:
                case LOC_SHAPE_ROOF_SLOPED_OVERHANG:
                    break;
                }
            }
        }
    }

    /**
     * Scene
     * Entities
     */
    int count = 0;
    for( mapx = chunk_sw_x; mapx <= chunk_ne_x; mapx++ )
    {
        for( mapz = chunk_sw_z; mapz <= chunk_ne_z; mapz++ )
        {
            map_locs = buildcachedat_get_scenery(buildcachedat, mapx, mapz);
            assert(map_locs && "Map scenery must be found");
            count += map_locs->locs_count;
        }
    }

    // struct MapBuildLocEntity* map_build_loc_entities =
    //     malloc(count * sizeof(struct MapBuildLocEntity));
    // memset(map_build_loc_entities, 0, count * sizeof(struct MapBuildLocEntity));
    struct MapBuildLocEntity* entity = NULL;
    int idx = 0;
    for( mapx = chunk_sw_x; mapx <= chunk_ne_x; mapx++ )
    {
        for( mapz = chunk_sw_z; mapz <= chunk_ne_z; mapz++ )
        {
            map_locs = buildcachedat_get_scenery(buildcachedat, mapx, mapz);
            assert(map_locs && "Map scenery must be found");

            for( int i = 0; i < map_locs->locs_count; i++ )
            {
                map_tile = &map_locs->locs[i];
                config_loc = buildcachedat_get_config_loc(buildcachedat, map_tile->loc_id);
                assert(config_loc && "Config location must be found");

                offset_x = world_to_scene_x(world, mapx, map_tile->chunk_pos_x);
                offset_z = world_to_scene_z(world, mapz, map_tile->chunk_pos_z);

                if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size ||
                    offset_z >= scene_size )
                    continue;

                entity = next_map_build_loc_entity(world);
                entity->scene_coord.sx = offset_x;
                entity->scene_coord.sz = offset_z;
                entity->scene_coord.slevel = map_tile->chunk_pos_level;
                scenery_add(world, entity, map_tile, config_loc);
            }
        }
    }

    struct DecorElementsOnWall* elements = NULL;
    struct Scene2Element* scene_element = NULL;
    for( int sx = 0; sx < scene_size; sx++ )
    {
        for( int sz = 0; sz < scene_size; sz++ )
        {
            for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
            {
                int wall_width =
                    decor_buildmap_get_wall_offset(world->decor_buildmap, sx, sz, level);

                elements = decor_buildmap_get_elements(world->decor_buildmap, sx, sz, level);

                for( int i = 0; i < elements->count; i++ )
                {
                    int element_id = elements->element_id[i];
                    int displacement_kind = elements->displacement_kind[i];
                    int orientation = elements->orientation[i];
                    scene_element = scene2_element_at(world->scene2, element_id);

                    bool diagonal = false;
                    int offset = 0;
                    switch( displacement_kind )
                    {
                    case DECOR_DISPLACEMENT_KIND_STRAIGHT_ONWALL_OFFSET:
                        offset += wall_width;
                        break;
                    case DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET:
                        offset += (wall_width / 16) * 8 + (45);
                        diagonal = true;
                        break;
                    case DECOR_DISPLACEMENT_KIND_STRAIGHT:
                        offset += 0;
                        break;
                    case DECOR_DISPLACEMENT_KIND_DIAGONAL:
                        offset += 45;
                        diagonal = true;
                        break;
                    }

                    calculate_wall_decor_offset(
                        scene2_element_dash_position(scene_element), orientation, offset, diagonal);
                }
            }
        }
    }

    // Adjust bridges.
    /**
     * Bridges are adjusted from an upper level.
     *
     * The "bridge_tile" is actually the tiles below the bridge.
     * The bridge itself is taken from the level above.
     *
     * E.g.
     *
     * Buffer Level 0: Tile := (Water and Bridge Walls), Bridge := Nothing
     * Buffer Level 1: Tile := (Bridge Walking Surface and Walls)
     * Buffer Level 2: Nothing
     * Buffer Level 3: Nothing
     *
     * After this adjustment,
     *
     * Buffer Level 0: Tile := (Previous Level 1),
     * Buffer Level 1: Nothing
     * Buffer Level 2: Nothing
     * Buffer Level 3: Tile := (Previous Level 0)
     */
    int ground_flags = 0;
    int bridge_flags = 0;
    struct PaintersTile bridge_tile_tmp = { 0 };
    for( int x = 0; x < scene_size; x++ )
    {
        for( int z = 0; z < scene_size; z++ )
        {
            /**
             * Dane's 317
             * 	for (int var76 = 0; var76 < 104; var76++) {
             *	for (int var77 = 0; var77 < 104; var77++) {
             *		if ((mapl[1][var76][var77] & 0x2) == 2) {
             *			arg0.pushDown(var76, var77);
             *		}
             *	}
             * }
             * OS1:
             *   for (int x = 0; x < maxTileX; x++) {
             *     for (int z = 0; z < maxTileZ; z++) {
             *         if ((levelTileFlags[1][x][z] & 0x2) == 2) {
             *             scene.setBridge(x, z);
             *         }
             *     }
             * }
             */
            bridge_flags = flag_map_get(flag_map, x, z, 1);

            if( (bridge_flags & FLOFLAG_LINK_BELOW_PUSHDOWN) != 0 )
            {
                bridge_tile_tmp = *painter_tile_at(world->painter, x, z, 0);

                /**
                 * Shift tile definitions down
                 */
                for( int level = 0; level < painter_max_levels(world->painter) - 1; level++ )
                {
                    painter_tile_copyto(
                        world->painter,
                        // From
                        x,
                        z,
                        level + 1,
                        // To
                        x,
                        z,
                        level);

                    painter_tile_set_draw_level(world->painter, x, z, level, level);
                }

                // Use the newly unused tile on level 3 as the bridge slot.
                *painter_tile_at(world->painter, x, z, 3) = bridge_tile_tmp;
                painter_tile_set_bridge(world->painter, x, z, 0, x, z, 3);
            }
        }
    }

    flag_map_free(flag_map);

    painter_mark_static_count(world->painter);

    /**
     * Shademap
     */

    /**
     * Lightmap
     */
    lightmap_build(world->lightmap, world->heightmap);

    /**
     * Blendmap
     */
    struct CacheMapFloor* tile = NULL;
    struct CacheConfigOverlay* flotype = NULL;
    for( mapx = chunk_sw_x; mapx <= chunk_ne_x; mapx++ )
    {
        for( mapz = chunk_sw_z; mapz <= chunk_ne_z; mapz++ )
        {
            map_terrain = buildcachedat_get_map_terrain(buildcachedat, mapx, mapz);
            assert(map_terrain && "Map terrain must be found");

            for( int tile_x = 0; tile_x < MAP_TERRAIN_X; tile_x++ )
            {
                for( int tile_z = 0; tile_z < MAP_TERRAIN_Z; tile_z++ )
                {
                    for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
                    {
                        offset_x = world_to_scene_x(world, mapx, tile_x);
                        offset_z = world_to_scene_z(world, mapz, tile_z);

                        if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size ||
                            offset_z >= scene_size )
                            continue;

                        tile = &map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x, tile_z, level)];
                        if( tile->underlay_id > 0 )
                        {
                            flotype =
                                buildcachedat_get_flotype(buildcachedat, tile->underlay_id - 1);
                            assert(flotype && "Flotype must be found");

                            blendmap_set_underlay_rgb(
                                world->blendmap, offset_x, offset_z, level, flotype->rgb_color);
                        }
                    }
                }
            }
        }
    }

    blendmap_build(world->blendmap);

    /**
     * Overlaymap
     */
    struct DashTexture* dash_texture = NULL;
    for( mapx = chunk_sw_x; mapx <= chunk_ne_x; mapx++ )
    {
        for( mapz = chunk_sw_z; mapz <= chunk_ne_z; mapz++ )
        {
            map_terrain = buildcachedat_get_map_terrain(buildcachedat, mapx, mapz);
            assert(map_terrain && "Map terrain must be found");

            for( int tile_x = 0; tile_x < MAP_TERRAIN_X; tile_x++ )
            {
                for( int tile_z = 0; tile_z < MAP_TERRAIN_Z; tile_z++ )
                {
                    for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
                    {
                        offset_x = world_to_scene_x(world, mapx, tile_x);
                        offset_z = world_to_scene_z(world, mapz, tile_z);

                        if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size ||
                            offset_z >= scene_size )
                            continue;

                        tile = &map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x, tile_z, level)];

                        int overlay_id = tile->overlay_id - 1;
                        int underlay_id = tile->underlay_id - 1;

                        if( overlay_id == -1 )
                            continue;

                        flotype = buildcachedat_get_flotype(buildcachedat, overlay_id);
                        assert(flotype && "Flotype must be found");

                        overlaymap_set_tile_rgb(
                            world->overlaymap, offset_x, offset_z, level, flotype->rgb_color);

                        if( flotype->texture != -1 )
                        {
                            dash_texture = scene2_texture_get(world->scene2, flotype->texture);
                            assert(dash_texture && "Dash texture must be found");

                            int average_hsl = dash_texture_average_hsl(dash_texture);
                            overlaymap_set_tile_texture(
                                world->overlaymap,
                                offset_x,
                                offset_z,
                                level,
                                flotype->texture,
                                average_hsl);
                        }

                        if( flotype->secondary_rgb_color > 0 )
                        {
                            overlaymap_set_tile_minimap(
                                world->overlaymap,
                                offset_x,
                                offset_z,
                                level,
                                flotype->secondary_rgb_color);
                        }
                    }
                }
            }
        }
    }

    /**
     * Shapemap
     *
     */
    for( mapx = chunk_sw_x; mapx <= chunk_ne_x; mapx++ )
    {
        for( mapz = chunk_sw_z; mapz <= chunk_ne_z; mapz++ )
        {
            map_terrain = buildcachedat_get_map_terrain(buildcachedat, mapx, mapz);
            assert(map_terrain && "Map terrain must be found");

            for( int tile_x = 0; tile_x < MAP_TERRAIN_X; tile_x++ )
            {
                for( int tile_z = 0; tile_z < MAP_TERRAIN_Z; tile_z++ )
                {
                    for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
                    {
                        offset_x = world_to_scene_x(world, mapx, tile_x);
                        offset_z = world_to_scene_z(world, mapz, tile_z);

                        if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size ||
                            offset_z >= scene_size )
                            continue;

                        tile = &map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x, tile_z, level)];

                        int underlay_id = tile->underlay_id - 1;
                        int overlay_id = tile->overlay_id - 1;

                        if( underlay_id == -1 && overlay_id == -1 )
                            continue;

                        int shape = 0;
                        int rotation = 0;

                        if( overlay_id != -1 )
                        {
                            shape = tile->shape + 1;
                            rotation = tile->rotation;
                        }

                        terrain_shape_map_set_tile(
                            world->terrain_shapemap, offset_x, offset_z, level, shape, rotation);
                    }
                }
            }
        }
    }

    // build_scene_terrain(world);
    build_scene_terrain_va(world);

    world_build_lighting(world);

done:
    lightmap_free(world->lightmap);
    world->lightmap = NULL;
    shademap2_free(world->shademap);
    world->shademap = NULL;
    blendmap_free(world->blendmap);
    world->blendmap = NULL;
    overlaymap_free(world->overlaymap);
    world->overlaymap = NULL;
    terrain_shape_map_free(world->terrain_shapemap);
    world->terrain_shapemap = NULL;
    decor_buildmap_free(world->decor_buildmap);
    world->decor_buildmap = NULL;
    sharelight_map_free(world->sharelight_map);
    world->sharelight_map = NULL;

    world_print_scene2_dashmodel_heap_stats(world);

    if( buildcachedat )
        buildcachedat_clear_map_chunks(buildcachedat);
}

void
world_cleanup_map_build_loc_entity(
    struct World* world,
    int entity_id)
{
    if( entity_id < 0 || entity_id >= entity_vec_count(&world->map_build_loc_entities) )
        return;

    struct MapBuildLocEntity* entity = world_loc_entity(world, entity_id);

    scene2_element_release(world->scene2, entity->scene_element.element_id);
    scene2_element_release(world->scene2, entity->scene_element_two.element_id);
    entity->scene_element.element_id = -1;
    entity->scene_element_two.element_id = -1;
    memset(&entity->scene_coord, 0, sizeof(struct EntitySceneCoord));
    free(entity->actions);
    entity->actions = NULL;
    entity->action_count = 0;
}

void
world_cleanup_map_build_tile_entity(
    struct World* world,
    int entity_id)
{
    if( entity_id == -1 )
        return;

    assert(entity_id >= 0 && entity_id < MAX_MAP_BUILD_TILE_ENTITIES);
    if( entity_id >= entity_vec_count(&world->map_build_tile_entities) )
        return;

    struct MapBuildTileEntity* entity =
        (struct MapBuildTileEntity*)entity_vec_at(&world->map_build_tile_entities, entity_id);
    if( entity->entity_id != entity_id )
        return;
    if( entity->scene_element.element_id == -1 )
        return;

    scene2_element_release(world->scene2, entity->scene_element.element_id);
    entity->scene_element.element_id = -1;
    memset(&entity->scene_coord, 0, sizeof(struct EntitySceneCoord));
}

void
world_cleanup_player_entity(
    struct World* world,
    int entity_id)
{
    if( entity_id < 0 || entity_id >= entity_vec_count(&world->players) )
        return;

    struct PlayerEntity* player = world_player(world, entity_id);
    scene2_element_release(world->scene2, player->scene_element2.element_id);
    player->scene_element2.element_id = -1;
    memset(&player->pathing, 0, sizeof(struct EntityPathing));
    memset(&player->appearance, 0, sizeof(struct PlayerAppearanceSlots));
    memset(&player->draw_position, 0, sizeof(struct EntityDrawPosition));
    memset(&player->orientation, 0, sizeof(struct EntityOrientation));
    memset(&player->animation, 0, sizeof(struct EntityAnimation));
    player->alive = false;
}

void
world_cleanup_npc_entity(
    struct World* world,
    int entity_id)
{
    if( entity_id < 0 || entity_id >= entity_vec_count(&world->npcs) )
        return;

    struct NPCEntity* npc = world_npc(world, entity_id);
    free(npc->name);
    npc->name = NULL;
    free(npc->description);
    npc->description = NULL;
    free(npc->actions);
    npc->actions = NULL;
    npc->action_count = 0;
    scene2_element_release(world->scene2, npc->scene_element2.element_id);
    npc->scene_element2.element_id = -1;
    memset(&npc->pathing, 0, sizeof(struct EntityPathing));
    memset(&npc->draw_position, 0, sizeof(struct EntityDrawPosition));
    memset(&npc->orientation, 0, sizeof(struct EntityOrientation));
    memset(&npc->animation, 0, sizeof(struct EntityAnimation));
    npc->alive = false;
}

void
world_player_entity_set_appearance(
    struct World* world,
    int player_entity_id,
    struct PlayerAppearance* appearance)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);

    struct Scene2Element* element =
        scene2_element_at(world->scene2, player->scene_element2.element_id);
    assert(element && "Element must be found");

    world_scenebuild_player_entity_set_appearance(
        world, player_entity_id, appearance->appearance, appearance->color);

    struct PassiveAnimationInfo passive_animations = {
        .readyanim = appearance->readyanim,
        .walkanim = appearance->walkanim,
        .turnanim = appearance->turnanim,
        .runanim = appearance->runanim,
        .walkanim_b = appearance->walkanim_b,
        .walkanim_r = appearance->walkanim_r,
        .walkanim_l = appearance->walkanim_l,
    };

    world_player_entity_set_passive_animations(world, player_entity_id, &passive_animations);

    strncpy(player->name.name, appearance->name, sizeof(player->name.name));
    player->visible_level.level = appearance->combat_level;
}

static void
load_scene_animation(
    struct World* world,
    struct Scene2Element* element,
    int animation_id,
    int animation_type)
{
    if( animation_id == -1 )
        return;

    struct CacheAnimframe* animframe = NULL;
    struct CacheDatSequence* sequence =
        buildcachedat_get_sequence(world->buildcachedat, animation_id);
    assert(sequence);

    for( int i = 0; i < sequence->frame_count; i++ )
    {
        // Get the frame definition ID from the second 2 bytes of the sequence frame ID The
        //     first 2 bytes are the sequence ID,
        //     the second 2 bytes are the frame file ID
        int frame_id = sequence->frames[i];

        animframe = buildcachedat_get_animframe(world->buildcachedat, frame_id);
        assert(animframe);

        if( !scene2_element_dash_framemap(element) )
        {
            scene2_element_set_framemap(element, dashframemap_new_from_animframe(animframe));
        }

        // From Client-TS 245.2
        int length = sequence->delay[i];
        if( length == 0 )
            length = animframe->delay;

        if( animation_type == ANIMATION_TYPE_PRIMARY )
        {
            scene2_element_push_animation_frame(
                element, dashframe_new_from_animframe(animframe), length);
        }
        else
        {
            scene2_element_push_secondary_animation_frame(
                element, dashframe_new_from_animframe(animframe), length);
        }
    }
}

void
world_map_build_loc_entity_set_animation(
    struct World* world,
    int map_build_loc_entity_id,
    int animation_id)
{
    if( animation_id == -1 )
        return;

    struct Scene2Element* element = NULL;
    struct MapBuildLocEntity* map_build_loc_entity =
        world_loc_entity(world, map_build_loc_entity_id);

    if( map_build_loc_entity->scene_element.element_id != -1 )
    {
        element = scene2_element_at(world->scene2, map_build_loc_entity->scene_element.element_id);
        assert(element && "Element must be found");
        load_scene_animation(world, element, animation_id, ANIMATION_TYPE_PRIMARY);

        memset(&map_build_loc_entity->animation, 0, sizeof(struct EntityAnimation));
        map_build_loc_entity->animation.primary_anim.anim_id = animation_id;
    }

    if( map_build_loc_entity->scene_element_two.element_id != -1 )
    {
        element =
            scene2_element_at(world->scene2, map_build_loc_entity->scene_element_two.element_id);
        assert(element && "Element must be found");

        load_scene_animation(world, element, animation_id, ANIMATION_TYPE_PRIMARY);

        memset(&map_build_loc_entity->animation_two, 0, sizeof(struct EntityAnimation));
        map_build_loc_entity->animation_two.primary_anim.anim_id = animation_id;
    }
}

void
world_player_entity_set_animation(
    struct World* world,
    int player_entity_id,
    int animation_id,
    int animation_type)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);

    struct Scene2Element* element =
        scene2_element_at(world->scene2, player->scene_element2.element_id);

    if( animation_type == ANIMATION_TYPE_PRIMARY )
    {
        scene2_element_clear_animation(element);
    }
    else
    {
        scene2_element_clear_secondary_animation(element);
    }

    load_scene_animation(world, element, animation_id, animation_type);

    if( animation_type == ANIMATION_TYPE_PRIMARY )
    {
        memset(&player->animation.primary_anim, 0, sizeof(struct EntityAnimationStep));
        player->animation.primary_anim.anim_id = animation_id;
    }
    else
    {
        memset(&player->animation.secondary_anim, 0, sizeof(struct EntityAnimationStep));
        player->animation.secondary_anim.anim_id = animation_id;
    }
}

void
world_npc_entity_set_animation(
    struct World* world,
    int npc_entity_id,
    int animation_id,
    int animation_type)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);

    struct Scene2Element* element =
        scene2_element_at(world->scene2, npc->scene_element2.element_id);

    if( animation_type == ANIMATION_TYPE_PRIMARY )
    {
        scene2_element_clear_animation(element);
    }
    else
    {
        scene2_element_clear_secondary_animation(element);
    }

    load_scene_animation(world, element, animation_id, animation_type);

    if( animation_type == ANIMATION_TYPE_PRIMARY )
    {
        memset(&npc->animation.primary_anim, 0, sizeof(struct EntityAnimationStep));
        npc->animation.primary_anim.anim_id = animation_id;
    }
    else
    {
        memset(&npc->animation.secondary_anim, 0, sizeof(struct EntityAnimationStep));
        npc->animation.secondary_anim.anim_id = animation_id;
    }
}

void
world_player_entity_set_passive_animations(
    struct World* world,
    int player_entity_id,
    struct PassiveAnimationInfo* passive_animation_info)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);
    player->animation.readyanim = passive_animation_info->readyanim;
    player->animation.walkanim = passive_animation_info->walkanim;
    player->animation.turnanim = passive_animation_info->turnanim;
    player->animation.runanim = passive_animation_info->runanim;
    player->animation.walkanim_b = passive_animation_info->walkanim_b;
    player->animation.walkanim_r = passive_animation_info->walkanim_r;
    player->animation.walkanim_l = passive_animation_info->walkanim_l;
}

void
world_npc_entity_set_passive_animations(
    struct World* world,
    int npc_entity_id,
    struct PassiveAnimationInfo* passive_animation_info)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
    npc->animation.readyanim = passive_animation_info->readyanim;
    npc->animation.walkanim = passive_animation_info->walkanim;
    npc->animation.turnanim = passive_animation_info->turnanim;
    npc->animation.runanim = passive_animation_info->runanim;
    npc->animation.walkanim_b = passive_animation_info->walkanim_b;
    npc->animation.walkanim_r = passive_animation_info->walkanim_r;
    npc->animation.walkanim_l = passive_animation_info->walkanim_l;
}

void
world_npc_entity_path_push_moveto(
    struct World* world,
    int npc_entity_id,
    int x,
    int z)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
}

void
world_player_entity_path_push_step(
    struct World* world,
    int player_entity_id,
    int step_type,
    int direction)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);

    entity_pathing_push_step(&player->pathing, step_type, direction);
}

void
world_npc_entity_path_push_step(
    struct World* world,
    int npc_entity_id,
    int step_type,
    int direction)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);

    entity_pathing_push_step(&npc->pathing, step_type, direction);
}

void
world_player_entity_path_jump_relative_to_active(
    struct World* world,
    int player_entity_id,
    bool force_teleport,
    int dx,
    int dz)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);
    struct PlayerEntity* active_player = world_player(world, ACTIVE_PLAYER_SLOT);

    int x = active_player->pathing.route_x[0] + dx;
    int z = active_player->pathing.route_z[0] + dz;

    enum PathingJump jump = entity_pathing_jump(&player->pathing, force_teleport, x, z);
    if( jump == PATHING_JUMP_TELEPORT )
        entity_draw_position_set_to_tile(&player->draw_position, x, z, 1, 1);
}

void
world_npc_entity_path_jump_relative_to_active(
    struct World* world,
    int npc_entity_id,
    bool force_teleport,
    int dx,
    int dz)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
    struct PlayerEntity* active_player = world_player(world, ACTIVE_PLAYER_SLOT);

    int x = active_player->pathing.route_x[0] + dx;
    int z = active_player->pathing.route_z[0] + dz;

    enum PathingJump jump = entity_pathing_jump(&npc->pathing, force_teleport, x, z);
    if( jump == PATHING_JUMP_TELEPORT )
        entity_draw_position_set_to_tile(&npc->draw_position, x, z, 1, 1);
}

void
world_player_entity_path_jump(
    struct World* world,
    int player_entity_id,
    bool force_teleport,
    int x,
    int z)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);

    enum PathingJump jump = entity_pathing_jump(&player->pathing, force_teleport, x, z);
    if( jump == PATHING_JUMP_TELEPORT )
        entity_draw_position_set_to_tile(&player->draw_position, x, z, 1, 1);
}

void
world_npc_entity_path_jump(
    struct World* world,
    int npc_entity_id,
    bool force_teleport,
    int x,
    int z)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);

    enum PathingJump jump = entity_pathing_jump(&npc->pathing, force_teleport, x, z);
    if( jump == PATHING_JUMP_TELEPORT )
        entity_draw_position_set_to_tile(&npc->draw_position, x, z, npc->size.x, npc->size.z);
}

void
world_player_face_entity(
    struct World* world,
    int player_entity_id,
    int entity_id)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);
}

void
world_npc_face_entity(
    struct World* world,
    int npc_entity_id,
    int entity_id)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
}

void
world_player_face_coord(
    struct World* world,
    int player_entity_id,
    int tile_x,
    int tile_z)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);
}

void
world_npc_face_coord(
    struct World* world,
    int npc_entity_id,
    int tile_x,
    int tile_z)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
}

void
world_npc_set_size(
    struct World* world,
    int npc_entity_id,
    int size_x,
    int size_z)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
    npc->size.x = size_x;
    npc->size.z = size_z;
}

struct NPCEntity*
world_npc_ensure_scene_element(
    struct World* world,
    int npc_id)
{
    struct Scene2Element* element = NULL;
    struct NPCEntity* npc = world_npc_ensure(world, npc_id);

    npc->alive = true;
    if( npc->scene_element2.element_id == -1 )
    {
        npc->scene_element2.element_id = scene2_element_acquire_full(
            world->scene2, (int)entity_unified_id(ENTITY_KIND_NPC, npc_id));

        npc->orientation.dst_yaw = 0;

        element = scene2_element_at(world->scene2, npc->scene_element2.element_id);
        if( element && !scene2_element_dash_position(element) )
            scene2_element_set_dash_position_ptr(element, dashposition_new());
    }
    return npc;
}

struct PlayerEntity*
world_player_ensure_scene_element(
    struct World* world,
    int player_id)
{
    struct Scene2Element* element = NULL;
    struct PlayerEntity* player = world_player_ensure(world, player_id);

    player->alive = true;
    if( player->scene_element2.element_id == -1 )
    {
        player->scene_element2.element_id = scene2_element_acquire_full(
            world->scene2, (int)entity_unified_id(ENTITY_KIND_PLAYER, player_id));

        // player->orientation.face_entity = -1;
        player->orientation.dst_yaw = 0;

        element = scene2_element_at(world->scene2, player->scene_element2.element_id);
        if( element && !scene2_element_dash_position(element) )
            scene2_element_set_dash_position_ptr(element, dashposition_new());
    }

    return player;
}

void
world_map_build_loc_entity_push_action(
    struct World* world,
    int map_build_loc_entity_id,
    int code,
    char* action)
{
    struct MapBuildLocEntity* entity = world_loc_entity(world, map_build_loc_entity_id);
    assert(entity->action_count < 10);
    if( !entity->actions )
        entity->actions = (struct EntityAction*)calloc(10, sizeof(struct EntityAction));
    entity->actions[entity->action_count].code = code;
    strncpy(
        entity->actions[entity->action_count].name,
        action,
        sizeof(entity->actions[entity->action_count].name));
    entity->action_count++;
}