#ifndef GAMECACHE_H
#define GAMECACHE_H

#include "graphics/dash.h"

#include <stdint.h>

struct GameCacheModel;

struct GameCache
{
    struct DashMap* models_hmap;
};

struct GameCache*
gamecache_new(void);

void
gamecache_free(struct GameCache* gamecache);

void
gamecache_model_add(
    struct GameCache* gamecache,
    int model_id,
    struct GameCacheModel* gamecache_model);

struct GameCacheModel*
gamecache_model_get(
    struct GameCache* gamecache,
    int model_id);

struct DashModel*
gamecache_dashmodel_get(
    struct GameCache* gamecache,
    int model_id);

#endif