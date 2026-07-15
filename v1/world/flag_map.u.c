#ifndef FLAG_MAP_U_C
#define FLAG_MAP_U_C

#include "flag_map.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct FlagMap*
flag_map_new(
    int size_x,
    int size_z,
    int levels)
{
    struct FlagMap* flag_map = malloc(sizeof(struct FlagMap));
    flag_map->flags = malloc(sizeof(int) * size_x * size_z * levels);
    memset(flag_map->flags, 0, sizeof(int) * size_x * size_z * levels);
    flag_map->size_x = size_x;
    flag_map->size_z = size_z;
    flag_map->levels = levels;
    return flag_map;
}

void
flag_map_free(struct FlagMap* flag_map)
{
    if( !flag_map )
        return;
    free(flag_map->flags);
    free(flag_map);
}

static inline int
flag_map_index(
    struct FlagMap* flag_map,
    int x,
    int z,
    int level)
{
    assert(x >= 0 && x < flag_map->size_x);
    assert(z >= 0 && z < flag_map->size_z);
    assert(level >= 0 && level < flag_map->levels);
    return x + z * flag_map->size_x + level * flag_map->size_x * flag_map->size_z;
}

int
flag_map_get(
    struct FlagMap* flag_map,
    int x,
    int z,
    int level)
{
    return flag_map->flags[flag_map_index(flag_map, x, z, level)];
}

void
flag_map_set(
    struct FlagMap* flag_map,
    int x,
    int z,
    int level,
    int flag)
{
    flag_map->flags[flag_map_index(flag_map, x, z, level)] = flag;
}

#endif
