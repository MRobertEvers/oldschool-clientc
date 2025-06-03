#include "osrs/cache.h"
#include "osrs/tables/maps.h"
#include "osrs/xtea_config.h"

#include <stdio.h>

int
main()
{
    int xtea_keys_count = xtea_config_load_keys("../cache/xteas.json");
    if( xtea_keys_count == -1 )
    {
        printf("Failed to load xtea keys\n");
        return 1;
    }

    struct Cache* cache = cache_new_from_directory("../cache");
    if( !cache )
    {
        printf("Failed to load cache\n");
        return 1;
    }

    int map_x = 50;
    int map_y = 49;
    struct MapTerrain* map_terrain = map_terrain_new_from_cache(cache, map_x, map_y);
    if( !map_terrain )
    {
        printf("Failed to load map terrain\n");
        return 1;
    }

    // for( int z = 0; z < MAP_TERRAIN_Z; z++ )
    // {
    //     for( int x = 0; x < MAP_TERRAIN_X; x++ )
    //     {
    //         for( int y = 0; y < MAP_TERRAIN_Y; y++ )
    //         {
    //             struct MapTile* tile = &map_terrain->tiles_xyz[MAP_TILE_COORD(x, y, z)];
    //             printf(
    //                 "Tile %d, %d, %d: %d, %d, %d, %d, %d, %d\n",
    //                 x,
    //                 y,
    //                 z,
    //                 tile->height,
    //                 tile->attrOpcode,
    //                 tile->settings,
    //                 tile->overlayId,
    //                 tile->overlayPath,
    //                 tile->overlayRotation,
    //                 tile->underlayId);
    //         }
    //     }
    // }

    struct MapLocs* map_locs = map_locs_new_from_cache(cache, map_x, map_y);
    if( !map_locs )
    {
        printf("Failed to load map locs\n");
        return 1;
    }

    for( int i = 0; i < map_locs->locs_count; i++ )
    {
        struct MapLoc* loc = &map_locs->locs[i];
        printf(
            "Loc %d: %d, %d, %d, %d, %d\n",
            loc->id,
            loc->type,
            loc->orientation,
            loc->pos_x,
            loc->pos_y,
            loc->pos_z);
    }

    map_locs_free(map_locs);

    map_terrain_free(map_terrain);
    cache_free(cache);
}