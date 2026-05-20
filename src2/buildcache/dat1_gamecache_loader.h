#ifndef DAT1_GAMECACHE_LOADER_H
#define DAT1_GAMECACHE_LOADER_H

#include "buildcache/dat1_buildcache.h"
#include "gamecache/gamecache_model.h"

#include <stdint.h>

struct GameCacheModel*
dat1_gamecache_loader_new_gamecache_model_from_cache_model(struct CacheModel* model);

#endif