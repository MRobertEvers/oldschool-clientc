#ifndef SCENE_CACHE_C
#define SCENE_CACHE_C

#include "tables/model.h"

struct ModelCache;

struct ModelCache* model_cache_new();
void model_cache_free(struct ModelCache* model_cache);

struct CacheModel*
model_cache_checkout(struct ModelCache* model_cache, struct Cache* cache, int model_id);

void model_cache_checkin(struct ModelCache* model_cache, struct CacheModel* model);

#endif