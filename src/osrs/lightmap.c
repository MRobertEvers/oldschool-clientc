#include "lightmap.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static inline int
lightmap_coord_idx(
    struct Lightmap* lightmap,
    int x,
    int z,
    int level)
{
    assert(x >= 0 && x < lightmap->width);
    assert(z >= 0 && z < lightmap->height);
    assert(level >= 0 && level < lightmap->levels);
    return x + z * lightmap->width + level * lightmap->width * lightmap->height;
}

struct Lightmap*
lightmap_new(
    int width,
    int height,
    int levels)
{
    struct Lightmap* lightmap = malloc(sizeof(struct Lightmap));
    lightmap->lights = malloc(width * height * levels * sizeof(uint8_t));
    memset(lightmap->lights, 0, width * height * levels * sizeof(uint8_t));
    lightmap->width = width;
    lightmap->height = height;
    lightmap->levels = levels;
    return lightmap;
}

void
lightmap_free(struct Lightmap* lightmap)
{
    free(lightmap->lights);
    free(lightmap);
}

int
lightmap_get(
    struct Lightmap* lightmap,
    int x,
    int z,
    int level)
{
    return lightmap->lights[lightmap_coord_idx(lightmap, x, z, level)];
}

void
lightmap_set(
    struct Lightmap* lightmap,
    int x,
    int z,
    int level,
    uint8_t light)
{
    lightmap->lights[lightmap_coord_idx(lightmap, x, z, level)] = light;
}