#include "scene.h"

#include "filelist.h"
#include "tables/config_locs.h"
#include "tables/configs.h"
#include "tables/maps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TILE_SIZE 128
struct Scene*
scene_new_from_map(struct Cache* cache, int chunk_x, int chunk_y)
{
    struct SceneLocs* scene_locs = NULL;
    struct MapTerrain* map_terrain = NULL;
    struct MapLocs* map_locs = NULL;
    struct SceneTile* scene_tiles = NULL;

    struct Scene* scene = malloc(sizeof(struct Scene));
    memset(scene, 0, sizeof(struct Scene));
    scene->grid_tiles = malloc(sizeof(struct GridTile) * MAP_TILE_COUNT);
    memset(scene->grid_tiles, 0, sizeof(struct GridTile) * MAP_TILE_COUNT);
    scene->grid_tiles_length = MAP_TILE_COUNT;

    map_terrain = map_terrain_new_from_cache(cache, chunk_x, chunk_y);
    if( !map_terrain )
    {
        printf("Failed to load map terrain\n");
        goto error;
    }

    map_locs = map_locs_new_from_cache(cache, chunk_x, chunk_y);
    if( !map_locs )

    {
        printf("Failed to load map locs\n");
        goto error;
    }

    // TODO: This has to happen before locs,
    // because this calls a fixup on the terrain data.
    // That should be done separately.
    scene_tiles = scene_tiles_new_from_map_terrain_cache(map_terrain, cache);
    if( !scene_tiles )
    {
        printf("Failed to load scene tiles\n");
        goto error;
    }

    scene_locs = scene_locs_new_from_map_locs(map_terrain, map_locs, cache);
    if( !scene_locs )
    {
        printf("Failed to load scene locs\n");
        goto error;
    }

    for( int i = 0; i < MAP_TILE_COUNT; i++ )
    {
        struct SceneTile* scene_tile = &scene_tiles[i];
        struct GridTile* grid_tile = &scene->grid_tiles[MAP_TILE_COORD(
            scene_tile->chunk_pos_x, scene_tile->chunk_pos_y, scene_tile->chunk_pos_level)];
        grid_tile->tile = *scene_tile;
    }

    for( int i = 0; i < scene_locs->locs_count; i++ )
    {
        struct SceneLoc* scene_loc = &scene_locs->locs[i];

        int coord_x = scene_loc->chunk_pos_x;
        int coord_y = scene_loc->chunk_pos_y;
        int coord_z = scene_loc->chunk_pos_level;

        struct GridTile* grid_tile = &scene->grid_tiles[MAP_TILE_COORD(coord_x, coord_y, coord_z)];

        grid_tile->locs[grid_tile->locs_length++] = *scene_loc;
    }

    free(scene_tiles);
    free(scene_locs);

    map_terrain_free(map_terrain);
    map_locs_free(map_locs);

    return scene;

error:
    // scene_locs_free(scene_locs);
    // scene_free(scene);
    return NULL;
}