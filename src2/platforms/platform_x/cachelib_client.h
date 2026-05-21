#ifndef PLATFORM_X_CACHELIB_CLIENT_H
#define PLATFORM_X_CACHELIB_CLIENT_H

#include "cachelib.h"
#include "src/osrs/rscache/cache_dat.h"
#include "src/osrs/rscache/tables_dat/configs_dat.h"

static inline void
cachelib_dat1_config_file_fetch(struct CacheLib_IORequest* out)
{
    out->table_id = CACHE_DAT_CONFIGS;
    out->archive_id = CONFIG_DAT_CONFIGS;
    out->flags = 0;
}

static inline void
cachelib_dat1_model_fetch(
    int model_id,
    struct CacheLib_IORequest* out)
{
    out->table_id = CACHE_DAT_MODELS;
    out->archive_id = model_id;
    out->flags = 0;
}

#endif