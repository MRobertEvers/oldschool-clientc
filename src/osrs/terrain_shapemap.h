#ifndef TERRAIN_SHAPEMAP_H
#define TERRAIN_SHAPEMAP_H

#include <stdbool.h>
#include <stdint.h>

struct TerrainShapeMapTile
{
    bool active;
    uint8_t shape;
    uint8_t rotation;
};

struct TerrainShapeMap
{
    struct TerrainShapeMapTile* tiles;
    int width;
    int height;
    int levels;
};

struct TerrainShapeMap*
terrain_shape_map_new(
    int width,
    int height,
    int levels);

void
terrain_shape_map_free(struct TerrainShapeMap* terrain_shape_map);

void
terrain_shape_map_set_tile(
    struct TerrainShapeMap* terrain_shape_map,
    int x,
    int z,
    int level,
    int shape,
    int rotation);

struct TerrainShapeMapTile*
terrain_shape_map_get_tile(
    struct TerrainShapeMap* terrain_shape_map,
    int x,
    int z,
    int level);

#endif