#ifndef GAMETASK_CACHE_LOAD_H
#define GAMETASK_CACHE_LOAD_H

#include "osrs/cache.h"
#include "osrs/gameio.h"

struct GameTaskCacheLoad;

struct GameTaskCacheLoad* gametask_cache_load_new(struct GameIO* io, struct Cache* cache);

void gametask_cache_load_free(struct GameTaskCacheLoad* task);

enum GameIOStatus gametask_cache_load_send(struct GameTaskCacheLoad* task);

struct Cache* gametask_cache_value(struct GameTaskCacheLoad* task);

#endif