#include "model.h"

struct CacheModel*
cache_dat_model_new(
    struct CacheDat* cache_dat,
    int model_id)
{
    struct CacheModel* model = model_new_from_cache(cache_dat, model_id);
    return model;
}

void
cache_dat_model_free(struct CacheModel* model)
{
    model_free(model);
}