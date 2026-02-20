#include "decor_buildmap.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static inline int
buildmap_idx(
    struct DecorBuildMap* build_map,
    int x,
    int z,
    int level)
{
    return x + z * build_map->size_x + level * build_map->size_x * build_map->size_z;
    assert(x >= 0 && x < build_map->size_x);
    assert(z >= 0 && z < build_map->size_z);
    assert(level >= 0 && level < build_map->levels);
    return x + z * build_map->size_x + level * build_map->size_x * build_map->size_z;
}

struct DecorBuildMap*
decor_buildmap_new(
    int size_x,
    int size_z,
    int levels)
{
    struct DecorBuildMap* build_map = malloc(sizeof(struct DecorBuildMap));
    memset(build_map, 0, sizeof(struct DecorBuildMap));
    build_map->elements = malloc(sizeof(struct DecorElementsOnWall) * size_x * size_z * levels);
    memset(build_map->elements, 0, sizeof(struct DecorElementsOnWall) * size_x * size_z * levels);
    build_map->offsets = malloc(sizeof(int16_t) * size_x * size_z * levels);
    memset(build_map->offsets, 0, sizeof(int16_t) * size_x * size_z * levels);
    build_map->size_x = size_x;
    build_map->size_z = size_z;
    build_map->levels = levels;
    return build_map;
}

void
decor_buildmap_free(struct DecorBuildMap* build_map)
{
    free(build_map->elements);
    free(build_map->offsets);
    free(build_map);
}

int
decor_buildmap_get_wall_offset(
    struct DecorBuildMap* build_map,
    int x,
    int z,
    int level)
{
    int index = buildmap_idx(build_map, x, z, level);
    return build_map->offsets[index];
}

void
decor_buildmap_set_wall_offset(
    struct DecorBuildMap* build_map,
    int x,
    int z,
    int level,
    int wall_offset)
{
    int index = buildmap_idx(build_map, x, z, level);
    build_map->offsets[index] = wall_offset;
}

void
decor_buildmap_add_element(
    struct DecorBuildMap* build_map,
    int x,
    int z,
    int level,
    int element_id,
    int orientation,
    enum DecorDisplacementKind displacement_kind)
{
    int index = buildmap_idx(build_map, x, z, level);
    assert(build_map->elements[index].count < 2);

    int slot = build_map->elements[index].count;
    build_map->elements[index].element_id[slot] = element_id;
    build_map->elements[index].displacement_kind[slot] = displacement_kind;
    build_map->elements[index].orientation[slot] = orientation;
    build_map->elements[index].count++;
}

struct DecorElementsOnWall*
decor_buildmap_get_elements(
    struct DecorBuildMap* build_map,
    int x,
    int z,
    int level)
{
    int index = buildmap_idx(build_map, x, z, level);
    return &build_map->elements[index];
}