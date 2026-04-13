#ifndef SHARELIGHT_MAP_H
#define SHARELIGHT_MAP_H

#include <stdbool.h>
#include <stdint.h>

struct SharelightMapElement
{
    int16_t element_idx;
    uint8_t size_x;
    uint8_t size_z;

    uint8_t light_ambient;
    uint8_t light_attenuation;
};

struct SharelightMapTile
{
    int32_t sharelight_head;
    int32_t defaultlight_head;
    uint8_t sharelight_count;
    uint8_t default_lit_count;
};

struct SharelightMapPoolEntry
{
    struct SharelightMapElement element;
    int32_t next;
};

struct SharelightMap
{
    struct SharelightMapTile* tiles;
    struct SharelightMapPoolEntry* pool;
    int pool_count;
    int pool_capacity;
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
    bool shared,
    int x,
    int z,
    int level,
    int element_idx,
    int size_x,
    int size_z,
    int light_ambient,
    int light_attenuation);

void
sharelight_map_push_shared(
    struct SharelightMap* sharelight_map,
    int x,
    int z,
    int level,
    int element_idx,
    int element_size_x,
    int element_size_z,
    int light_ambient,
    int light_attenuation);

void
sharelight_map_push_default_lit_element(
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
