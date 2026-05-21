#ifndef GAMECACHE_H
#define GAMECACHE_H

#include "graphics/dash.h"

#include <stdint.h>

struct GameCache
{
    struct DashMap* models_hmap;
};

struct GameCache*
gamecache_new(void);

void
gamecache_free(struct GameCache* gamecache);

void
gamecache_dashmodel_add(
    struct GameCache* gamecache,
    int model_id,
    struct DashModel* dash_model);

struct DashModel*
gamecache_dashmodel_get(
    struct GameCache* gamecache,
    int model_id);

#endif
