#include "heightmap.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Heightmap*
heightmap_new(
    int size_x,
    int size_z)
{
    struct Heightmap* heightmap = malloc(sizeof(struct Heightmap));
    heightmap->heights = malloc(size_x * size_z * sizeof(int16_t));
    memset(heightmap->heights, 0, size_x * size_z * sizeof(int16_t));
    heightmap->size_x = size_x;
    heightmap->size_z = size_z;
    return heightmap;
}

static inline int
heightmap_coord_idx(
    struct Heightmap* heightmap,
    int x,
    int z)
{
    assert(x >= 0 && x < heightmap->size_x);
    assert(z >= 0 && z < heightmap->size_z);
    return x + z * heightmap->size_x;
}

void
heightmap_free(struct Heightmap* heightmap)
{
    free(heightmap->heights);
    free(heightmap);
}

int
heightmap_get(
    struct Heightmap* heightmap,
    int x,
    int z)
{
    return heightmap->heights[heightmap_coord_idx(heightmap, x, z)];
}

void
heightmap_set(
    struct Heightmap* heightmap,
    int x,
    int z,
    int height)
{
    heightmap->heights[heightmap_coord_idx(heightmap, x, z)] = height;
}