#ifndef HEIGHTMAP_H
#define HEIGHTMAP_H

#include <stdbool.h>
#include <stdint.h>

struct Heightmap
{
    int16_t* heights;
    int size_x;
    int size_z;
    int levels;
};

struct Heightmap*
heightmap_new(
    int size_x,
    int size_z,
    int levels);
void
heightmap_free(struct Heightmap* heightmap);

int
heightmap_get(
    struct Heightmap* heightmap,
    int x,
    int z,
    int level);

int
heightmap_get_center(
    struct Heightmap* heightmap,
    int x,
    int z,
    int level);

struct HeightmapHeights
{
    int sw_height;
    int se_height;
    int ne_height;
    int nw_height;
    int height_center;
};

void
heightmap_get_heights(
    struct Heightmap* heightmap,
    int x,
    int z,
    int level,
    struct HeightmapHeights* heights);

void
heightmap_get_heights_sized(
    struct Heightmap* heightmap,
    int x,
    int z,
    int level,
    int size_x,
    int size_z,
    struct HeightmapHeights* heights);

void
heightmap_set(
    struct Heightmap* heightmap,
    int x,
    int z,
    int level,
    int height);

#endif