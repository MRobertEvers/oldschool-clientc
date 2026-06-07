#include "gamecache.h"

#include "../platforms/platform_x/cachelib_client.h"
#include "toridraw/toridraw_model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct MapEntry_ToriModel
{
    int id;
    struct ToriDraw_ModelHandle model;
};

static struct ToriDraw_Map*
gamecache_map_new(
    int entry_size,
    int capacity)
{
    int buffer_size = toridraw_map_buffer_size_for(entry_size, capacity);
    struct ToriDraw_MapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = entry_size,
    };
    return toridraw_map_new(&config, 0);
}

static void
gamecache_map_free(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    free(toridraw_map_buffer_ptr(map));
    toridraw_map_free(map);
}

struct GameCache*
gamecache_new(void)
{
    struct GameCache* gamecache = malloc(sizeof(struct GameCache));
    if( !gamecache )
        return NULL;

    memset(gamecache, 0, sizeof(struct GameCache));

    gamecache->models_hmap = gamecache_map_new(sizeof(struct MapEntry_ToriModel), 1024);

    return gamecache;
}

void
gamecache_free(struct GameCache* gamecache)
{
    if( !gamecache )
        return;

    if( gamecache->models_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(gamecache->models_hmap);
        struct MapEntry_ToriModel* entry = NULL;
        while( (entry = (struct MapEntry_ToriModel*)toridraw_map_iter_next(iter)) )
        {
            if( entry->model.kind == TORIDRAWMK_MODEL )
                toridraw_model_free(entry->model.u.model.model);
        }
        toridraw_map_iter_free(iter);
        gamecache_map_free(gamecache->models_hmap);
    }

    free(gamecache);
}

void
gamecache_model_add(
    struct GameCache* gamecache,
    int model_id,
    struct ToriDraw_ModelHandle model)
{
    struct MapEntry_ToriModel* entry = (struct MapEntry_ToriModel*)toridraw_map_search(
        gamecache->models_hmap, &model_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = model_id;
    entry->model = model;
}

struct ToriDraw_ModelHandle
gamecache_model_get(
    struct GameCache* gamecache,
    int model_id)
{
    struct MapEntry_ToriModel* entry = (struct MapEntry_ToriModel*)toridraw_map_search(
        gamecache->models_hmap, &model_id, TORIDRAW_MAP_FIND);

    struct ToriDraw_ModelHandle model = { .kind = TORIDRAWMK_NONE };
    if( !entry )
        return model;

    return entry->model;
}

struct ToriDraw_ModelHandle
gamecache_model_remove(
    struct GameCache* gamecache,
    int model_id)
{
    struct ToriDraw_ModelHandle none = { .kind = TORIDRAWMK_NONE };
    if( !gamecache )
        return none;

    struct MapEntry_ToriModel* entry = (struct MapEntry_ToriModel*)toridraw_map_search(
        gamecache->models_hmap, &model_id, TORIDRAW_MAP_REMOVE);
    if( !entry )
        return none;

    return entry->model;
}

void
gamecache_models_clear_all(struct GameCache* gamecache)
{
    if( !gamecache || !gamecache->models_hmap )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(gamecache->models_hmap);
    struct MapEntry_ToriModel* entry = NULL;
    while( (entry = (struct MapEntry_ToriModel*)toridraw_map_iter_next(iter)) )
    {
        if( entry->model.kind == TORIDRAWMK_MODEL )
            toridraw_model_free(entry->model.u.model.model);
    }
    toridraw_map_iter_free(iter);

    gamecache_map_free(gamecache->models_hmap);

    gamecache->models_hmap = gamecache_map_new(sizeof(struct MapEntry_ToriModel), 1024);
}
