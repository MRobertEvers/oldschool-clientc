#include "gamecache.h"

#include "gamecache_model.h"
#include "graphics/dash.h"
#include "src/osrs/dash_utils.h"
#include "src/osrs/rscache/tables/model.h"

#include <stdlib.h>
#include <string.h>

struct MapEntry_GameCacheModel
{
    int id;
    struct GameCacheModel* model;
    struct DashModel* dash_model;
};

struct GameCache*
gamecache_new(void)
{
    struct GameCache* gamecache = malloc(sizeof(struct GameCache));
    if( !gamecache )
        return NULL;

    memset(gamecache, 0, sizeof(struct GameCache));

    struct DashMapConfig config = { 0 };
    int buffer_size = dashmap_buffer_size_for(sizeof(struct MapEntry_GameCacheModel), 1024);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct MapEntry_GameCacheModel),
    };
    gamecache->models_hmap = dashmap_new(&config, 0);

    return gamecache;
}

void
gamecache_free(struct GameCache* gamecache)
{
    if( !gamecache )
        return;

    if( gamecache->models_hmap )
    {
        struct DashMapIter* iter = dashmap_iter_new(gamecache->models_hmap);
        struct MapEntry_GameCacheModel* entry = NULL;
        while( (entry = (struct MapEntry_GameCacheModel*)dashmap_iter_next(iter)) )
        {
            if( entry->model )
                gamecache_model_free(entry->model);
            if( entry->dash_model )
                dashmodel_free(entry->dash_model);
        }
        dashmap_iter_free(iter);

        free(dashmap_buffer_ptr(gamecache->models_hmap));
        dashmap_free(gamecache->models_hmap);
    }

    free(gamecache);
}

void
gamecache_model_add(
    struct GameCache* gamecache,
    int model_id,
    struct GameCacheModel* gamecache_model)
{
    struct MapEntry_GameCacheModel* entry = (struct MapEntry_GameCacheModel*)dashmap_search(
        gamecache->models_hmap, &model_id, DASHMAP_INSERT);
    if( !entry )
        return;

    entry->id = model_id;
    entry->model = gamecache_model;
    entry->dash_model = NULL;
}

struct GameCacheModel*
gamecache_model_get(
    struct GameCache* gamecache,
    int model_id)
{
    struct MapEntry_GameCacheModel* entry = (struct MapEntry_GameCacheModel*)dashmap_search(
        gamecache->models_hmap, &model_id, DASHMAP_FIND);
    if( !entry )
        return NULL;
    return entry->model;
}

struct DashModel*
gamecache_dashmodel_get(
    struct GameCache* gamecache,
    int model_id)
{
    struct MapEntry_GameCacheModel* entry = (struct MapEntry_GameCacheModel*)dashmap_search(
        gamecache->models_hmap, &model_id, DASHMAP_FIND);
    if( !entry )
        return NULL;

    if( entry->dash_model )
        return entry->dash_model;

    if( !entry->model )
        return NULL;

    struct CacheModel* cache_model = gamecache_model_transfer_to_cache_model(entry->model);
    if( !cache_model )
        return NULL;

    struct DashModel* dash_model = dashmodel_new_from_cache_model(cache_model);

    model_free(cache_model);

    entry->dash_model = dash_model;
    return dash_model;
}
