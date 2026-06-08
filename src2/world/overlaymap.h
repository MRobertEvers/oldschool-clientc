#ifndef OVERLAYMAP_H
#define OVERLAYMAP_H

#include <stdbool.h>
#include <stdint.h>

struct OverlaymapTile
{
    bool active;
    uint32_t minimap_rgb_color;
    uint32_t rgb_color;
    int16_t texture_id;
    uint16_t texture_avg_hsl16;
};

struct Overlaymap
{
    struct OverlaymapTile* tiles;
    int width;
    int height;
    int levels;
};

struct Overlaymap*
overlaymap_new(
    int width,
    int height,
    int levels);

void
overlaymap_free(struct Overlaymap* overlaymap);

void
overlaymap_set_tile_minimap(
    struct Overlaymap* overlaymap,
    int x,
    int z,
    int level,
    uint32_t minimap_rgb_color);

void
overlaymap_set_tile_rgb(
    struct Overlaymap* overlaymap,
    int x,
    int z,
    int level,
    uint32_t rgb_color);

void
overlaymap_set_tile_texture(
    struct Overlaymap* overlaymap,
    int x,
    int z,
    int level,
    uint8_t texture_id,
    uint16_t texture_avg_hsl16);

struct OverlaymapTile*
overlaymap_get_tile(
    struct Overlaymap* overlaymap,
    int x,
    int z,
    int level);

#endif