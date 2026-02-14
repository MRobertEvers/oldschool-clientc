#include "heightmap.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Heightmap*
heightmap_new(
    int size_x,
    int size_z,
    int levels)
{
    struct Heightmap* heightmap = malloc(sizeof(struct Heightmap));
    heightmap->heights = malloc(size_x * size_z * levels * sizeof(int16_t));
    memset(heightmap->heights, 0, size_x * size_z * levels * sizeof(int16_t));
    heightmap->size_x = size_x;
    heightmap->size_z = size_z;
    heightmap->levels = levels;
    return heightmap;
}

static inline int
heightmap_coord_idx(
    struct Heightmap* heightmap,
    int x,
    int z,
    int level)
{
    assert(x >= 0 && x < heightmap->size_x);
    assert(z >= 0 && z < heightmap->size_z);
    assert(level >= 0 && level < heightmap->levels);
    return x + z * heightmap->size_x + level * heightmap->size_x * heightmap->size_z;
}

void
heightmap_free(struct Heightmap* heightmap)
{
    free(heightmap->heights);
    free(heightmap);
}

static inline bool
heightmap_in_bounds(
    struct Heightmap* heightmap,
    int x,
    int z)
{
    return x >= 0 && x < heightmap->size_x && z >= 0 && z < heightmap->size_z;
}

int
heightmap_get(
    struct Heightmap* heightmap,
    int x,
    int z,
    int level)
{
    return heightmap->heights[heightmap_coord_idx(heightmap, x, z, level)];
}

void
heightmap_get_heights(
    struct Heightmap* heightmap,
    int x,
    int z,
    int level,
    struct HeightmapHeights* heights)
{
    heights->sw_height = heightmap_get(heightmap, x, z, level);
    heights->se_height = heights->sw_height;
    heights->ne_height = heights->sw_height;
    heights->nw_height = heights->sw_height;

    if( heightmap_in_bounds(heightmap, x + 1, z) )
    {
        heights->se_height = heightmap_get(heightmap, x + 1, z, level);
    }

    if( heightmap_in_bounds(heightmap, x, z + 1) )
    {
        heights->nw_height = heightmap_get(heightmap, x, z + 1, level);
    }

    if( heightmap_in_bounds(heightmap, x + 1, z + 1) )
    {
        heights->ne_height = heightmap_get(heightmap, x + 1, z + 1, level);
    }

    heights->height_center =
        (heights->sw_height + heights->se_height + heights->ne_height + heights->nw_height) >> 2;
}

/**
 * OSRS uses this
 */
void
heightmap_get_heights_sized(
    struct Heightmap* heightmap,
    int sx,
    int sz,
    int slevel,
    int size_x,
    int size_z,
    struct HeightmapHeights* heights)
{
    int start_x = sx;
    int end_x = sx + 1;
    int start_z = sz;
    int end_z = sz + 1;

    // This is what OSRS uses.
    // Older revs do not.
    if( heightmap_in_bounds(heightmap, sx + size_x, sz) )
    {
        start_x = (size_x >> 1) + sx;
        end_x = ((size_x + 1) >> 1) + sx;
    }

    if( heightmap_in_bounds(heightmap, sx, sz + size_z) )
    {
        start_z = (size_z >> 1) + sz;
        end_z = ((size_z + 1) >> 1) + sz;
    }

    heights->sw_height = heightmap_get(heightmap, start_x, start_z, slevel);

    heights->se_height = heights->sw_height;
    if( heightmap_in_bounds(heightmap, end_x, start_z) )
    {
        heights->se_height = heightmap_get(heightmap, end_x, start_z, slevel);
    }

    heights->ne_height = heights->sw_height;
    if( heightmap_in_bounds(heightmap, end_x, end_z) )
    {
        heights->ne_height = heightmap_get(heightmap, end_x, end_z, slevel);
    }

    heights->nw_height = heights->sw_height;
    if( heightmap_in_bounds(heightmap, start_x, end_z) )
    {
        heights->nw_height = heightmap_get(heightmap, start_x, end_z, slevel);
    }

    int height_center =
        (heights->sw_height + heights->se_height + heights->ne_height + heights->nw_height) >> 2;

    heights->height_center = height_center;
}

void
heightmap_set(
    struct Heightmap* heightmap,
    int x,
    int z,
    int level,
    int height)
{
    heightmap->heights[heightmap_coord_idx(heightmap, x, z, level)] = height;
}