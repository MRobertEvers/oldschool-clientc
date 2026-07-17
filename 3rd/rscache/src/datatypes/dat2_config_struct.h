#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_STRUCT_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_STRUCT_H

#include "../rsbuffer.h"

struct RSCache_Dat2ConfigStruct
{
    int id;
    struct RSCache_Params params;
};

void
RSCache_Dat2ConfigStructDecodeInplace(
    struct RSCache_Dat2ConfigStruct* entry,
    const void* data,
    int data_size);

void
RSCache_Dat2ConfigStructFree(struct RSCache_Dat2ConfigStruct* entry);

void
RSCache_Dat2ConfigStructFreeInplace(struct RSCache_Dat2ConfigStruct* entry);

#endif
