#include "sharelight_map.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static inline int
sharelight_map_coord(
    struct SharelightMap* sharelight_map,
    int x,
    int z,
    int level)
{
    assert(x >= 0 && x < sharelight_map->width);
    assert(z >= 0 && z < sharelight_map->height);
    assert(level >= 0 && level < sharelight_map->levels);
    return x + z * sharelight_map->width + level * sharelight_map->width * sharelight_map->height;
}

struct SharelightMap*
sharelight_map_new(
    int width,
    int height,
    int levels)
{
    struct SharelightMap* sharelight_map = malloc(sizeof(struct SharelightMap));
    sharelight_map->tiles = malloc(width * height * levels * sizeof(struct SharelightMapTile));
    memset(sharelight_map->tiles, 0, width * height * levels * sizeof(struct SharelightMapTile));
    sharelight_map->width = width;
    sharelight_map->height = height;
    sharelight_map->levels = levels;
    return sharelight_map;
}

void
sharelight_map_free(struct SharelightMap* sharelight_map)
{
    free(sharelight_map->tiles);
    free(sharelight_map);
}

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
    int light_attenuation)
{
    if( shared )
    {
        sharelight_map_push_shared(
            sharelight_map,
            x,
            z,
            level,
            element_idx,
            size_x,
            size_z,
            light_ambient,
            light_attenuation);
    }
    else
    {
        sharelight_map_push_default_lit_element(
            sharelight_map,
            x,
            z,
            level,
            element_idx,
            size_x,
            size_z,
            light_ambient,
            light_attenuation);
    }
}

void
sharelight_map_push_shared(
    struct SharelightMap* sharelight_map,
    int x,
    int z,
    int level,
    int element_idx,
    int size_x,
    int size_z,
    int light_ambient,
    int light_attenuation)
{
    struct SharelightMapTile* tile =
        &sharelight_map->tiles[sharelight_map_coord(sharelight_map, x, z, level)];

    assert(tile->sharelight_count < 10);
    tile->sharelight[tile->sharelight_count++] =
        (struct SharelightMapElement){ .element_idx = element_idx,
                                       .size_x = size_x,
                                       .size_z = size_z,
                                       .light_ambient = light_ambient,
                                       .light_attenuation = light_attenuation };
}

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
    int light_attenuation)
{
    struct SharelightMapTile* tile =
        &sharelight_map->tiles[sharelight_map_coord(sharelight_map, x, z, level)];

    assert(tile->default_lit_count < 10);
    tile->defaultlight[tile->default_lit_count++] =
        (struct SharelightMapElement){ .element_idx = element_idx,
                                       .size_x = element_size_x,
                                       .size_z = element_size_z,
                                       .light_ambient = light_ambient,
                                       .light_attenuation = light_attenuation };
}

struct SharelightMapTile*
sharelight_map_tile_at(
    struct SharelightMap* sharelight_map,
    int x,
    int z,
    int level)
{
    return &sharelight_map->tiles[sharelight_map_coord(sharelight_map, x, z, level)];
}