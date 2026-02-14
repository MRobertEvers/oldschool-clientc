#include "world.h"

#include <stdlib.h>
#include <string.h>

struct World*
world_new(void)
{
    struct World* world = malloc(sizeof(struct World));
    memset(world, 0, sizeof(struct World));

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
    free(world);
}

static void
init_terrain_grid(
    struct SceneBuilder* scene_builder,
    struct TerrainGrid* terrain_grid)
{
    int mapxz = 0;
    // memset(terrain_grid, 0, sizeof(struct TerrainGrid));

    // int mapx_sw = scene_builder->wx_sw / 64;
    // int mapz_sw = scene_builder->wz_sw / 64;
    // int mapx_ne = (scene_builder->wx_ne) / 64;
    // int mapz_ne = (scene_builder->wz_ne) / 64;

    // int offset_x = scene_builder->wx_sw % 64;
    // int offset_z = scene_builder->wz_sw % 64;

    // terrain_grid->mapx_ne = mapx_ne;
    // terrain_grid->mapz_ne = mapz_ne;
    // terrain_grid->mapx_sw = mapx_sw;
    // terrain_grid->mapz_sw = mapz_sw;
    // terrain_grid->offset_x = offset_x;
    // terrain_grid->offset_z = offset_z;
    // terrain_grid->size_x = scene_builder->size_x;
    // terrain_grid->size_z = scene_builder->size_z;

    // int map_index = 0;
    // struct CacheMapTerrain* map_terrain = NULL;
    // for( int mapz = mapz_sw; mapz <= mapz_ne; mapz++ )
    // {
    //     for( int mapx = mapx_sw; mapx <= mapx_ne; mapx++ )
    //     {
    //         map_terrain = scenebuilder_compat_get_map_terrain(scene_builder, mapx, mapz);
    //         assert(map_terrain && "Map terrain must be found");

    //         terrain_grid->map_terrain[map_index] = map_terrain;
    //         map_index++;
    //     }
    // }
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

    world->_base_tile_x = zone_sw_x * 8;
    world->_base_tile_z = zone_sw_z * 8;

    world->painter = painter_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->collision_map = collision_map_new(scene_size, scene_size);
    world->heightmap = heightmap_new(scene_size, scene_size);
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
                        heightmap_set(world->heightmap, offset_x, offset_z, height);
                    }
                }
            }
        }
    }

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
                int offset_x = map_tile->chunk_pos_x + (mapx - chunk_sw_x) * MAP_TERRAIN_X;
                int offset_z = map_tile->chunk_pos_z + (mapz - chunk_sw_z) * MAP_TERRAIN_Z;
                int chunk_index = MAP_TILE_COORD(
                    map_tile->chunk_pos_x, map_tile->chunk_pos_z, map_tile->chunk_pos_level);

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
}