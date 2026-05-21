#include "gamecache.h"

#include "graphics/dash.h"
#include "toripix/toridraw_types.h"

#include <stdlib.h>
#include <string.h>

struct MapEntry_DashModel
{
    int id;
    struct ToriDraw_ModelHandle model;
};

struct GameCache*
gamecache_new(void)
{
    struct GameCache* gamecache = malloc(sizeof(struct GameCache));
    if( !gamecache )
        return NULL;

    memset(gamecache, 0, sizeof(struct GameCache));

    struct DashMapConfig config = { 0 };
    int buffer_size = dashmap_buffer_size_for(sizeof(struct MapEntry_DashModel), 1024);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct MapEntry_DashModel),
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
        struct MapEntry_DashModel* entry = NULL;
        while( (entry = (struct MapEntry_DashModel*)dashmap_iter_next(iter)) )
        {
            // if( entry->model.kind == TORIDRAWMK_MODEL )
            //     toridraw_model_free(entry->model.u.model.model);
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
    struct ToriDraw_ModelHandle model)
{
    struct MapEntry_DashModel* entry = (struct MapEntry_DashModel*)dashmap_search(
        gamecache->models_hmap, &model_id, DASHMAP_INSERT);
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
    struct MapEntry_DashModel* entry =
        (struct MapEntry_DashModel*)dashmap_search(gamecache->models_hmap, &model_id, DASHMAP_FIND);

    struct ToriDraw_ModelHandle model = { .kind = TORIDRAWMK_NONE };
    if( !entry )
        return model;

    return entry->model;
}
