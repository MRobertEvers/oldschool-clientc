#ifndef RSCACHE_RSCACHEDAT2A_CONFIG_ENUM_H
#define RSCACHE_RSCACHEDAT2A_CONFIG_ENUM_H

#include <stdbool.h>

struct RSCacheDat2A_ConfigEnum
{
    int id;
    bool output_is_string;
    int default_int;
    char* default_string;
    int* keys;
    int* int_values;
    char** string_values;
    int count;
};

void
RSCacheDat2A_ConfigEnumDecodeInplace(
    struct RSCacheDat2A_ConfigEnum* entry,
    const void* data,
    int data_size);

void
RSCacheDat2A_ConfigEnumFree(struct RSCacheDat2A_ConfigEnum* entry);

#endif
