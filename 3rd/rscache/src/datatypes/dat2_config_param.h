#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_PARAM_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_PARAM_H

#include "../rsbuffer.h"

struct RSCache_Dat2ConfigParam
{
    int id;
    char type;
    int default_int;
    long long default_long;
    int auto_disable;
    char* default_string;
};

void
RSCache_Dat2ConfigParamDecodeInplace(
    struct RSCache_Dat2ConfigParam* entry,
    const void* data,
    int data_size);

void
RSCache_Dat2ConfigParamFree(struct RSCache_Dat2ConfigParam* entry);

void
RSCache_Dat2ConfigParamFreeInplace(struct RSCache_Dat2ConfigParam* entry);

#endif
