#ifndef FLAG_MAP_H
#define FLAG_MAP_H

struct FlagMap
{
    int* flags;
    int size_x;
    int size_z;
    int levels;
};

struct FlagMap*
flag_map_new(
    int size_x,
    int size_z,
    int levels);

void
flag_map_free(struct FlagMap* flag_map);

int
flag_map_get(
    struct FlagMap* flag_map,
    int x,
    int z,
    int level);

void
flag_map_set(
    struct FlagMap* flag_map,
    int x,
    int z,
    int level,
    int flag);

#endif
