#ifndef RSCACHE_RSCACHEDAT2A_CONFIG_PARAM_H
#define RSCACHE_RSCACHEDAT2A_CONFIG_PARAM_H

#include <stdbool.h>

struct RSCacheDat2A_ConfigParam
{
    int id;
    char type_char;
    int default_int;
    char* default_string;
};

void
RSCacheDat2A_ConfigParamDecodeInplace(
    struct RSCacheDat2A_ConfigParam* param,
    const void* data,
    int data_size);

void
RSCacheDat2A_ConfigParamFree(struct RSCacheDat2A_ConfigParam* param);

#endif
