#ifndef GAMECACHE_H
#define GAMECACHE_H

#include "graphics/dashmap.h"
#include "toripix/toridraw_types.h"

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
gamecache_model_add(
    struct GameCache* gamecache,
    int model_id,
    struct ToriDraw_ModelHandle model);

struct ToriDraw_ModelHandle
gamecache_model_get(
    struct GameCache* gamecache,
    int model_id);

#endif
