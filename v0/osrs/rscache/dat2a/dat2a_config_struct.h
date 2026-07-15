#ifndef RSCACHE_RSCACHEDAT2A_CONFIG_STRUCT_H
#define RSCACHE_RSCACHEDAT2A_CONFIG_STRUCT_H

#include "osrs/rscache/shared/shared_rs_buffer.h"

struct RSCacheDat2A_ConfigStruct
{
    int id;
    struct RSCacheShared_Params params;
};

void
RSCacheDat2A_ConfigStructDecodeInplace(
    struct RSCacheDat2A_ConfigStruct* entry,
    const void* data,
    int data_size);

void
RSCacheDat2A_ConfigStructFree(struct RSCacheDat2A_ConfigStruct* entry);

#endif
