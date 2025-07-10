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
    struct CacheMapTerrain* map_terrain = NULL;
    struct CacheMapLocs* map_locs = NULL;
    struct SceneTile* scene_tiles = NULL;
    struct GridTile* grid_tile = NULL;
    struct ModelCache* model_cache = model_cache_new();

    struct Scene* scene = malloc(sizeof(struct Scene));
    memset(scene, 0, sizeof(struct Scene));

    scene->grid_tiles = malloc(sizeof(struct GridTile) * MAP_TILE_COUNT);
    memset(scene->grid_tiles, 0, sizeof(struct GridTile) * MAP_TILE_COUNT);
    scene->grid_tiles_length = MAP_TILE_COUNT;
    scene->_model_cache = model_cache;

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

    scene_locs = scene_locs_new_from_map_locs(map_terrain, map_locs, cache, model_cache);
    if( !scene_locs )
    {
        printf("Failed to load scene locs\n");
        goto error;
    }

    scene->locs = scene_locs;

    for( int level = 0; level < MAP_TERRAIN_Z; level++ )
    {
        for( int x = 0; x < MAP_TERRAIN_X; x++ )
        {
            for( int y = 0; y < MAP_TERRAIN_Y; y++ )
            {
                grid_tile = &scene->grid_tiles[MAP_TILE_COORD(x, y, level)];
                grid_tile->x = x;
                grid_tile->z = y;
                grid_tile->level = level;
            }
        }
    }

    for( int i = 0; i < MAP_TILE_COUNT; i++ )
    {
        struct SceneTile* scene_tile = &scene_tiles[i];
        grid_tile = &scene->grid_tiles[MAP_TILE_COORD(
            scene_tile->chunk_pos_x, scene_tile->chunk_pos_y, scene_tile->chunk_pos_level)];
        grid_tile->tile = *scene_tile;
    }

    for( int i = 0; i < scene_locs->locs_count; i++ )
    {
        struct SceneLoc* scene_loc = &scene_locs->locs[i];

        int tile_coord_x = scene_loc->chunk_pos_x;
        int tile_coord_y = scene_loc->chunk_pos_y;
        int tile_coord_z = scene_loc->chunk_pos_level;

        grid_tile = &scene->grid_tiles[MAP_TILE_COORD(tile_coord_x, tile_coord_y, tile_coord_z)];

        // Subtract 1 because all locs are at least 1 tile wide.
        int min_tile_x = tile_coord_x;
        int min_tile_y = tile_coord_y;

        // The max tile is not actually included in the span
        int max_tile_exclusive_x = tile_coord_x + scene_loc->size_x - 1;
        int max_tile_exclusive_y = tile_coord_y + scene_loc->size_y - 1;
        if( max_tile_exclusive_x > MAP_TERRAIN_X - 1 )
            max_tile_exclusive_x = MAP_TERRAIN_X - 1;
        if( max_tile_exclusive_y > MAP_TERRAIN_Y - 1 )
            max_tile_exclusive_y = MAP_TERRAIN_Y - 1;

        for( int x = min_tile_x; x <= max_tile_exclusive_x; x++ )
        {
            for( int y = min_tile_y; y <= max_tile_exclusive_y; y++ )
            {
                int span_flags = 0;
                if( x > tile_coord_x )
                {
                    // Block until the west tile underlay is drawn.
                    span_flags |= SPAN_FLAG_WEST;
                }

                if( x < max_tile_exclusive_x )
                {
                    // Block until the east tile underlay is drawn.
                    span_flags |= SPAN_FLAG_EAST;
                }

                if( y > tile_coord_y )
                {
                    // Block until the south tile underlay is drawn.
                    span_flags |= SPAN_FLAG_SOUTH;
                }

                if( y < max_tile_exclusive_y )
                {
                    // Block until the north tile underlay is drawn.
                    span_flags |= SPAN_FLAG_NORTH;
                }

                struct GridTile* other = &scene->grid_tiles[MAP_TILE_COORD(x, y, tile_coord_z)];
                other->spans |= span_flags;
                other->locs[other->locs_length++] = i;
            }
        }
    }

    free(scene_tiles);

    map_terrain_free(map_terrain);
    map_locs_free(map_locs);

    return scene;

error:
    // scene_locs_free(scene_locs);
    // scene_free(scene);
    return NULL;
}

void
scene_free(struct Scene* scene)
{
    model_cache_free(scene->_model_cache);
    free(scene->grid_tiles);
    free(scene);
}