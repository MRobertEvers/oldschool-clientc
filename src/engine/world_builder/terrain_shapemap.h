#ifndef TERRAIN_SHAPEMAP_H
#define TERRAIN_SHAPEMAP_H

#include <stdbool.h>
#include <stdint.h>

struct TerrainShapeMapTile
{
    bool active;
    /** The tile states an underlay of its own (raw underlay_id > 0).
     *
     *  Not the same question as "the blend produced a colour": the blend
     *  averages a +-5 neighbourhood, so a tile with NO underlay surrounded by
     *  lava is handed a lava colour. Using that as the underlay paints a floor
     *  over a hole the map deliberately left. */
    bool has_underlay;
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
    int rotation,
    int has_underlay);

struct TerrainShapeMapTile*
terrain_shape_map_get_tile(
    struct TerrainShapeMap* terrain_shape_map,
    int x,
    int z,
    int level);

#endif