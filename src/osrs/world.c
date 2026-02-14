#include "world.h"

#include <stdlib.h>
#include <string.h>

// clang-format off
#include "world_scenery.u.c"
// clang-format on

static void
init_map_build_loc_entity(struct MapBuildLocEntity* map_build_loc_entity)
{
    memset(map_build_loc_entity, 0, sizeof(struct MapBuildLocEntity));
    map_build_loc_entity->scene_element.element_id = -1;
    map_build_loc_entity->scene_element_two.element_id = -1;
}

struct World*
world_new(struct BuildCacheDat* buildcachedat)
{
    struct World* world = malloc(sizeof(struct World));
    memset(world, 0, sizeof(struct World));

    world->scene2 = scene2_new(1024);
    world->painter = painter_new(104, 104, MAP_TERRAIN_LEVELS);
    world->collision_map = collision_map_new(104, 104);
    world->heightmap = heightmap_new(104, 104, MAP_TERRAIN_LEVELS);
    world->minimap = minimap_new(0, 0, 104, 104, MAP_TERRAIN_LEVELS);

    world->decor_buildmap = decor_buildmap_new(104, 104, MAP_TERRAIN_LEVELS);

    world->buildcachedat = buildcachedat;

    return world;
}

void
world_free(struct World* world)
{
    // Free the entities

    painter_free(world->painter);
    collision_map_free(world->collision_map);
    heightmap_free(world->heightmap);
    minimap_free(world->minimap);
    scene2_free(world->scene2);
    decor_buildmap_free(world->decor_buildmap);
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

void
world_buildcachedat_rebuild_centerzone(
    struct World* world,
    struct BuildCacheDat* buildcachedat,
    int zone_sw_x,
    int zone_sw_z,
    int zone_ne_x,
    int zone_ne_z,
    int scene_size)
{
    // Rebuild the center zone
    int zone_padding = scene_size / (2 * 8);
    int zone_sw_x = zone_sw_x - zone_padding;
    int zone_sw_z = zone_sw_z - zone_padding;
    int zone_ne_x = zone_ne_x + zone_padding;
    int zone_ne_z = zone_ne_z + zone_padding;

    int world_sw_x = zone_sw_x * 8;
    int world_sw_z = zone_sw_z * 8;
    int world_ne_x = zone_ne_x * 8;
    int world_ne_z = zone_ne_z * 8;

    world->_offset_x = world_sw_x % 64;
    world->_offset_z = world_sw_z % 64;

    world->_base_tile_x = zone_sw_x * 8;
    world->_base_tile_z = zone_sw_z * 8;

    world->painter = painter_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->collision_map = collision_map_new(scene_size, scene_size);
    world->heightmap = heightmap_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->minimap =
        minimap_new(world_sw_x, world_sw_z, world_ne_x, world_ne_z, MAP_TERRAIN_LEVELS);
    world->scene2 = scene2_new(scene_size);

    int chunk_sw_x = zone_sw_x / 8;
    int chunk_sw_z = zone_sw_z / 8;
    int chunk_ne_x = zone_ne_x / 8;
    int chunk_ne_z = zone_ne_z / 8;

    /**
     * Heightmap.
     */
    int mapx;
    int mapz;
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
                        int offset_x = tile_x + (mapx - chunk_sw_x) * MAP_TERRAIN_X;
                        int offset_z = tile_z + (mapz - chunk_sw_z) * MAP_TERRAIN_Z;
                        int chunk_index = MAP_TILE_COORD(tile_x, tile_z, level);
                        struct CacheMapFloor* tile = &map_terrain->tiles_xyz[chunk_index];
                        int height = tile->height;
                        heightmap_set(world->heightmap, offset_x, offset_z, level, height);
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

                int offset_x =
                    map_tile->chunk_pos_x - world->_offset_x + (mapx - chunk_sw_x) * MAP_TERRAIN_X;
                int offset_z =
                    map_tile->chunk_pos_z - world->_offset_z + (mapz - chunk_sw_z) * MAP_TERRAIN_Z;

                int chunk_index = MAP_TILE_COORD(
                    map_tile->chunk_pos_x, map_tile->chunk_pos_z, map_tile->chunk_pos_level);

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
                int offset_x =
                    map_tile->chunk_pos_x - world->_offset_x + (mapx - chunk_sw_x) * MAP_TERRAIN_X;
                int offset_z =
                    map_tile->chunk_pos_z - world->_offset_z + (mapz - chunk_sw_z) * MAP_TERRAIN_Z;

                int level = map_tile->chunk_pos_level;
                int chunk_index = MAP_TILE_COORD(
                    map_tile->chunk_pos_x, map_tile->chunk_pos_z, map_tile->chunk_pos_level);

                if( config_loc->map_scene_id != -1 )
                    continue;

                switch( map_tile->shape_select )
                {
                case LOC_SHAPE_WALL_SINGLE_SIDE:
                {
                    minimap_add_tile_wall(
                        world->minimap,
                        offset_x,
                        offset_z,
                        level,
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
                        level,
                        orientation_wall_flag(map_tile->orientation));

                    int next_orientation = (map_tile->orientation + 1) & 0x3;

                    minimap_add_tile_wall(
                        world->minimap,
                        offset_x,
                        offset_z,
                        level,
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
                        level,
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

    struct MapBuildLocEntity* map_build_loc_entities =
        malloc(count * sizeof(struct MapBuildLocEntity));
    memset(map_build_loc_entities, 0, count * sizeof(struct MapBuildLocEntity));
    struct MapBuildLocEntity* entity = NULL;
    int idx = 0;
    for( mapx = chunk_sw_x; mapx <= chunk_ne_x; mapx++ )
    {
        for( mapz = chunk_sw_z; mapz <= chunk_ne_z; mapz++ )
        {
            map_locs = buildcachedat_get_scenery(buildcachedat, mapx, mapz);
            assert(map_locs && "Map scenery must be found");

            config_loc = buildcachedat_get_config_loc(buildcachedat, map_tile->loc_id);
            assert(config_loc && "Config location must be found");

            for( int i = 0; i < map_locs->locs_count; i++ )
            {
                entity = &map_build_loc_entities[idx++];
                init_map_build_loc_entity(entity);

                map_tile = &map_locs->locs[i];
                int offset_x =
                    map_tile->chunk_pos_x - world->_offset_x + (mapx - chunk_sw_x) * MAP_TERRAIN_X;
                int offset_z =
                    map_tile->chunk_pos_z - world->_offset_z + (mapz - chunk_sw_z) * MAP_TERRAIN_Z;
                int chunk_index = MAP_TILE_COORD(
                    map_tile->chunk_pos_x, map_tile->chunk_pos_z, map_tile->chunk_pos_level);

                entity->scene_coord.sx = offset_x;
                entity->scene_coord.sz = offset_z;
                entity->scene_coord.slevel = chunk_index;

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
                        scene_element->dash_position, orientation, offset, diagonal);
                }
            }
        }
    }
}