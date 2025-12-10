#include "gio_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_PATH "../cache"

struct Cache*
gioqb_cache_new(void)
{
    struct Cache* cache = cache_new_from_directory(CACHE_PATH);
    if( !cache )
    {
        printf("Failed to load cache from directory: %s\n", CACHE_PATH);
        return NULL;
    }

    return cache;
}

struct CacheModel*
gioqb_cache_model_new_load(struct Cache* cache, int model_id)
{
    struct CacheModel* model = model_new_from_cache(cache, model_id);
    if( !model )
    {
        printf("Failed to load model %d\n", model_id);
        return NULL;
    }

    return model;
}
