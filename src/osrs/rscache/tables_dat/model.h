#ifndef TABLES_DAT_MODEL_H
#define TABLES_DAT_MODEL_H

#include "../tables/model.h"

struct CacheModel*
cache_dat_model_new(
    struct CacheDat* cache_dat,
    int model_id);

void
cache_dat_model_free(struct CacheModel* model);

#endif