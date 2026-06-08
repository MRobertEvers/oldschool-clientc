#include "terrain_shapemap.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static inline int
terrain_shape_map_coord(
    struct TerrainShapeMap* terrain_shape_map,
    int x,
    int z,
    int level)
{
    assert(x >= 0 && x < terrain_shape_map->width);
    assert(z >= 0 && z < terrain_shape_map->height);
    assert(level >= 0 && level < terrain_shape_map->levels);
    return x + z * terrain_shape_map->width +
           level * terrain_shape_map->width * terrain_shape_map->height;
}

struct TerrainShapeMap*
terrain_shape_map_new(
    int width,
    int height,
    int levels)
{
    struct TerrainShapeMap* terrain_shape_map = malloc(sizeof(struct TerrainShapeMap));
    terrain_shape_map->tiles = malloc(width * height * levels * sizeof(struct TerrainShapeMapTile));
    memset(
        terrain_shape_map->tiles, 0, width * height * levels * sizeof(struct TerrainShapeMapTile));
    terrain_shape_map->width = width;
    terrain_shape_map->height = height;
    terrain_shape_map->levels = levels;
    return terrain_shape_map;
}

void
terrain_shape_map_free(struct TerrainShapeMap* terrain_shape_map)
{
    free(terrain_shape_map->tiles);
    free(terrain_shape_map);
}

void
terrain_shape_map_set_tile(
    struct TerrainShapeMap* terrain_shape_map,
    int x,
    int z,
    int level,
    int shape,
    int rotation)
{
    int idx = terrain_shape_map_coord(terrain_shape_map, x, z, level);
    terrain_shape_map->tiles[idx] = (struct TerrainShapeMapTile){
        .active = true,
        .shape = shape,
        .rotation = rotation,
    };
}

struct TerrainShapeMapTile*
terrain_shape_map_get_tile(
    struct TerrainShapeMap* terrain_shape_map,
    int x,
    int z,
    int level)
{
    int idx = terrain_shape_map_coord(terrain_shape_map, x, z, level);
    return &terrain_shape_map->tiles[idx];
}