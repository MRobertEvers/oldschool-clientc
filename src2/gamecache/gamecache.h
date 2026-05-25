#ifndef GAMECACHE_H
#define GAMECACHE_H

#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_types.h"

#include <stdint.h>

struct GameCache
{
    struct ToriDraw_Map* models_hmap;
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

struct ToriDraw_ModelHandle
gamecache_model_remove(
    struct GameCache* gamecache,
    int model_id);

void
gamecache_models_clear_all(struct GameCache* gamecache);

#endif
