#include "overlaymap.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static inline void
init_tile(struct OverlaymapTile* tile)
{
    tile->minimap_rgb_color = -1;
    tile->rgb_color = -1;
    tile->texture_id = -1;
    tile->texture_avg_hsl16 = -1;
}

static inline int
overlaymap_coord_idx(
    struct Overlaymap* overlaymap,
    int x,
    int z,
    int level)
{
    assert(x >= 0 && x < overlaymap->width);
    assert(z >= 0 && z < overlaymap->height);
    assert(level >= 0 && level < overlaymap->levels);
    return x + z * overlaymap->width + level * overlaymap->width * overlaymap->height;
}

struct Overlaymap*
overlaymap_new(
    int width,
    int height,
    int levels)
{
    struct Overlaymap* overlaymap = malloc(sizeof(struct Overlaymap));
    memset(overlaymap, 0, sizeof(struct Overlaymap));

    overlaymap->tiles = malloc(width * height * levels * sizeof(struct OverlaymapTile));
    memset(overlaymap->tiles, 0, width * height * levels * sizeof(struct OverlaymapTile));
    for( int i = 0; i < width * height * levels; i++ )
        init_tile(&overlaymap->tiles[i]);

    overlaymap->width = width;
    overlaymap->height = height;
    overlaymap->levels = levels;

    return overlaymap;
}

void
overlaymap_free(struct Overlaymap* overlaymap)
{
    free(overlaymap->tiles);
    free(overlaymap);
}

void
overlaymap_set_tile_minimap(
    struct Overlaymap* overlaymap,
    int x,
    int z,
    int level,
    uint32_t minimap_rgb_color)
{
    int idx = overlaymap_coord_idx(overlaymap, x, z, level);
    struct OverlaymapTile* tile = &overlaymap->tiles[idx];
    tile->active = true;
    tile->minimap_rgb_color = minimap_rgb_color;
}

void
overlaymap_set_tile_rgb(
    struct Overlaymap* overlaymap,
    int x,
    int z,
    int level,
    uint32_t rgb_color)
{
    int idx = overlaymap_coord_idx(overlaymap, x, z, level);
    struct OverlaymapTile* tile = &overlaymap->tiles[idx];
    tile->active = true;
    tile->rgb_color = rgb_color;
}

void
overlaymap_set_tile_texture(
    struct Overlaymap* overlaymap,
    int x,
    int z,
    int level,
    uint8_t texture_id,
    uint16_t texture_avg_hsl16)
{
    int idx = overlaymap_coord_idx(overlaymap, x, z, level);
    struct OverlaymapTile* tile = &overlaymap->tiles[idx];
    tile->active = true;
    tile->texture_id = texture_id;
    tile->texture_avg_hsl16 = texture_avg_hsl16;
}

struct OverlaymapTile*
overlaymap_get_tile(
    struct Overlaymap* overlaymap,
    int x,
    int z,
    int level)
{
    int idx = overlaymap_coord_idx(overlaymap, x, z, level);
    return &overlaymap->tiles[idx];
}