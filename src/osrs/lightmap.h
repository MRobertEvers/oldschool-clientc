#ifndef LIGHTMAP_H
#define LIGHTMAP_H

#include <stdint.h>

struct Lightmap
{
    uint8_t* lights;
    int width;
    int height;
    int levels;
};

struct Lightmap*
lightmap_new(
    int width,
    int height,
    int levels);

void
lightmap_free(struct Lightmap* lightmap);

int
lightmap_get(
    struct Lightmap* lightmap,
    int x,
    int z,
    int level);

void
lightmap_set(
    struct Lightmap* lightmap,
    int x,
    int z,
    int level,
    uint8_t light);

#endif