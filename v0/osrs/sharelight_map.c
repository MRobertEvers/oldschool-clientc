#include "sharelight_map.h"

#include <assert.h>
#include <stdlib.h>

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

static void
sharelight_map_pool_ensure(struct SharelightMap* sharelight_map, int extra)
{
    if( sharelight_map->pool_count + extra <= sharelight_map->pool_capacity )
        return;
    int need = sharelight_map->pool_count + extra;
    int cap = sharelight_map->pool_capacity ? sharelight_map->pool_capacity : 256;
    while( cap < need )
        cap *= 2;
    sharelight_map->pool = realloc(
        sharelight_map->pool, (size_t)cap * sizeof(struct SharelightMapPoolEntry));
    sharelight_map->pool_capacity = cap;
}

struct SharelightMap*
sharelight_map_new(
    int width,
    int height,
    int levels)
{
    struct SharelightMap* sharelight_map = malloc(sizeof(struct SharelightMap));
    if( !sharelight_map )
        return NULL;
    int n_tiles = width * height * levels;
    sharelight_map->tiles = malloc((size_t)n_tiles * sizeof(struct SharelightMapTile));
    if( !sharelight_map->tiles )
    {
        free(sharelight_map);
        return NULL;
    }
    for( int i = 0; i < n_tiles; i++ )
    {
        sharelight_map->tiles[i].sharelight_head = -1;
        sharelight_map->tiles[i].defaultlight_head = -1;
        sharelight_map->tiles[i].sharelight_count = 0;
        sharelight_map->tiles[i].default_lit_count = 0;
    }
    sharelight_map->pool = NULL;
    sharelight_map->pool_count = 0;
    sharelight_map->pool_capacity = 0;
    sharelight_map->width = width;
    sharelight_map->height = height;
    sharelight_map->levels = levels;
    return sharelight_map;
}

void
sharelight_map_free(struct SharelightMap* sharelight_map)
{
    free(sharelight_map->tiles);
    free(sharelight_map->pool);
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
    sharelight_map_pool_ensure(sharelight_map, 1);
    int32_t ni = sharelight_map->pool_count++;
    sharelight_map->pool[ni] = (struct SharelightMapPoolEntry){
        .element =
            (struct SharelightMapElement){ .element_idx = element_idx,
                                          .size_x = (uint8_t)size_x,
                                          .size_z = (uint8_t)size_z,
                                          .light_ambient = (uint8_t)light_ambient,
                                          .light_attenuation = (uint8_t)light_attenuation },
        .next = tile->sharelight_head,
    };
    tile->sharelight_head = ni;
    tile->sharelight_count++;
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
    sharelight_map_pool_ensure(sharelight_map, 1);
    int32_t ni = sharelight_map->pool_count++;
    sharelight_map->pool[ni] = (struct SharelightMapPoolEntry){
        .element =
            (struct SharelightMapElement){ .element_idx = element_idx,
                                          .size_x = (uint8_t)element_size_x,
                                          .size_z = (uint8_t)element_size_z,
                                          .light_ambient = (uint8_t)light_ambient,
                                          .light_attenuation = (uint8_t)light_attenuation },
        .next = tile->defaultlight_head,
    };
    tile->defaultlight_head = ni;
    tile->default_lit_count++;
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
