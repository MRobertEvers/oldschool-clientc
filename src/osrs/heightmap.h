#ifndef HEIGHTMAP_H
#define HEIGHTMAP_H

#include <stdint.h>

struct Heightmap
{
    int16_t* heights;
    int size_x;
    int size_z;
};

struct Heightmap*
heightmap_new(
    int size_x,
    int size_z);
void
heightmap_free(struct Heightmap* heightmap);

int
heightmap_get(
    struct Heightmap* heightmap,
    int x,
    int z);

void
heightmap_set(
    struct Heightmap* heightmap,
    int x,
    int z,
    int height);

#endif