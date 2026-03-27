#ifndef SHARELIGHT_MAP_H
#define SHARELIGHT_MAP_H

#include <stdint.h>

struct SharelightMapElement
{
    int element_idx;
    uint8_t size_x;
    uint8_t size_z;

    uint8_t light_ambient;
    uint8_t light_attenuation;
};

struct SharelightMapTile
{
    struct SharelightMapElement elements[10];
    int elements_count;
};

struct SharelightMap
{
    struct SharelightMapTile* tiles;
    int width;
    int height;
    int levels;
};

struct SharelightMap*
sharelight_map_new(
    int width,
    int height,
    int levels);

void
sharelight_map_free(struct SharelightMap* element_map);

void
sharelight_map_push(
    struct SharelightMap* sharelight_map,
    int x,
    int z,
    int level,
    int element_idx,
    int element_size_x,
    int element_size_z,
    int light_ambient,
    int light_attenuation);

struct SharelightMapTile*
sharelight_map_tile_at(
    struct SharelightMap* sharelight_map,
    int x,
    int z,
    int level);

#endif