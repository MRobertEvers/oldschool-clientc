#include "gamecache.h"

#include "gamecache_flotype.h"
#include "gamecache_scenery_config.h"
#include "gamecache_sequence.h"
#include "toridraw/toridraw_animation.h"
#include "toridraw/toridraw_model.h"

#include <stdlib.h>
#include <string.h>

struct MapEntry_ToriModel
{
    int id;
    struct ToriDraw_ModelHandle model;
};

struct MapEntry_Sequence
{
    int id;
    struct GameCache_Sequence* sequence;
};

struct MapEntry_Flotype
{
    int id;
    struct GameCache_Flotype* flotype;
};

struct MapEntry_SceneryConfig
{
    int id;
    struct GameCache_SceneryConfig* config;
};

struct MapEntry_Animation
{
    int id;
    struct ToriDraw_Animation* animation;
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

    gamecache->models_hmap =
        gamecache_map_new(sizeof(struct MapEntry_ToriModel), 1024);
    gamecache->sequences_hmap =
        gamecache_map_new(sizeof(struct MapEntry_Sequence), 1024);
    gamecache->flotype_hmap = gamecache_map_new(sizeof(struct MapEntry_Flotype), 256);
    gamecache->scenery_config_hmap =
        gamecache_map_new(sizeof(struct MapEntry_SceneryConfig), 1024);
    gamecache->animations_hmap =
        gamecache_map_new(sizeof(struct MapEntry_Animation), 512);

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

    if( gamecache->sequences_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(gamecache->sequences_hmap);
        struct MapEntry_Sequence* entry = NULL;
        while( (entry = (struct MapEntry_Sequence*)toridraw_map_iter_next(iter)) )
        {
            if( entry->sequence )
                gamecache_sequence_free(entry->sequence);
        }
        toridraw_map_iter_free(iter);
        gamecache_map_free(gamecache->sequences_hmap);
    }

    if( gamecache->flotype_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(gamecache->flotype_hmap);
        struct MapEntry_Flotype* entry = NULL;
        while( (entry = (struct MapEntry_Flotype*)toridraw_map_iter_next(iter)) )
        {
            if( entry->flotype )
                gamecache_flotype_free(entry->flotype);
        }
        toridraw_map_iter_free(iter);
        gamecache_map_free(gamecache->flotype_hmap);
    }

    if( gamecache->scenery_config_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(gamecache->scenery_config_hmap);
        struct MapEntry_SceneryConfig* entry = NULL;
        while( (entry = (struct MapEntry_SceneryConfig*)toridraw_map_iter_next(iter)) )
        {
            if( entry->config )
                gamecache_scenery_config_free(entry->config);
        }
        toridraw_map_iter_free(iter);
        gamecache_map_free(gamecache->scenery_config_hmap);
    }

    if( gamecache->animations_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(gamecache->animations_hmap);
        struct MapEntry_Animation* entry = NULL;
        while( (entry = (struct MapEntry_Animation*)toridraw_map_iter_next(iter)) )
        {
            if( entry->animation )
                toridraw_animation_free(entry->animation);
        }
        toridraw_map_iter_free(iter);
        gamecache_map_free(gamecache->animations_hmap);
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

    gamecache->models_hmap =
        gamecache_map_new(sizeof(struct MapEntry_ToriModel), 1024);
}

void
gamecache_sequence_add(
    struct GameCache* gamecache,
    int sequence_id,
    struct GameCache_Sequence* sequence)
{
    struct MapEntry_Sequence* entry = (struct MapEntry_Sequence*)toridraw_map_search(
        gamecache->sequences_hmap, &sequence_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = sequence_id;
    entry->sequence = sequence;
}

struct GameCache_Sequence*
gamecache_sequence_get(
    struct GameCache* gamecache,
    int sequence_id)
{
    struct MapEntry_Sequence* entry = (struct MapEntry_Sequence*)toridraw_map_search(
        gamecache->sequences_hmap, &sequence_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->sequence;
}

void
gamecache_flotype_add(
    struct GameCache* gamecache,
    int flotype_id,
    struct GameCache_Flotype* flotype)
{
    struct MapEntry_Flotype* entry = (struct MapEntry_Flotype*)toridraw_map_search(
        gamecache->flotype_hmap, &flotype_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = flotype_id;
    entry->flotype = flotype;
}

struct GameCache_Flotype*
gamecache_flotype_get(
    struct GameCache* gamecache,
    int flotype_id)
{
    struct MapEntry_Flotype* entry = (struct MapEntry_Flotype*)toridraw_map_search(
        gamecache->flotype_hmap, &flotype_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->flotype;
}

void
gamecache_scenery_config_add(
    struct GameCache* gamecache,
    int loc_id,
    struct GameCache_SceneryConfig* config)
{
    struct MapEntry_SceneryConfig* entry = (struct MapEntry_SceneryConfig*)toridraw_map_search(
        gamecache->scenery_config_hmap, &loc_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = loc_id;
    entry->config = config;
}

struct GameCache_SceneryConfig*
gamecache_scenery_config_get(
    struct GameCache* gamecache,
    int loc_id)
{
    struct MapEntry_SceneryConfig* entry = (struct MapEntry_SceneryConfig*)toridraw_map_search(
        gamecache->scenery_config_hmap, &loc_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->config;
}

void
gamecache_animation_add(
    struct GameCache* gamecache,
    int animbaseframes_id,
    struct ToriDraw_Animation* animation)
{
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)toridraw_map_search(
        gamecache->animations_hmap, &animbaseframes_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = animbaseframes_id;
    entry->animation = animation;
}

struct ToriDraw_Animation*
gamecache_animation_get(
    struct GameCache* gamecache,
    int animbaseframes_id)
{
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)toridraw_map_search(
        gamecache->animations_hmap, &animbaseframes_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->animation;
}

void
gamecache_sequences_clear_all(struct GameCache* gamecache)
{
    if( !gamecache || !gamecache->sequences_hmap )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(gamecache->sequences_hmap);
    struct MapEntry_Sequence* entry = NULL;
    while( (entry = (struct MapEntry_Sequence*)toridraw_map_iter_next(iter)) )
    {
        if( entry->sequence )
            gamecache_sequence_free(entry->sequence);
    }
    toridraw_map_iter_free(iter);

    gamecache_map_free(gamecache->sequences_hmap);
    gamecache->sequences_hmap =
        gamecache_map_new(sizeof(struct MapEntry_Sequence), 1024);
}

void
gamecache_floortypes_clear_all(struct GameCache* gamecache)
{
    if( !gamecache || !gamecache->flotype_hmap )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(gamecache->flotype_hmap);
    struct MapEntry_Flotype* entry = NULL;
    while( (entry = (struct MapEntry_Flotype*)toridraw_map_iter_next(iter)) )
    {
        if( entry->flotype )
            gamecache_flotype_free(entry->flotype);
    }
    toridraw_map_iter_free(iter);

    gamecache_map_free(gamecache->flotype_hmap);
    gamecache->flotype_hmap = gamecache_map_new(sizeof(struct MapEntry_Flotype), 256);
}

void
gamecache_scenery_configs_clear_all(struct GameCache* gamecache)
{
    if( !gamecache || !gamecache->scenery_config_hmap )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(gamecache->scenery_config_hmap);
    struct MapEntry_SceneryConfig* entry = NULL;
    while( (entry = (struct MapEntry_SceneryConfig*)toridraw_map_iter_next(iter)) )
    {
        if( entry->config )
            gamecache_scenery_config_free(entry->config);
    }
    toridraw_map_iter_free(iter);

    gamecache_map_free(gamecache->scenery_config_hmap);
    gamecache->scenery_config_hmap =
        gamecache_map_new(sizeof(struct MapEntry_SceneryConfig), 1024);
}

void
gamecache_animations_clear_all(struct GameCache* gamecache)
{
    if( !gamecache || !gamecache->animations_hmap )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(gamecache->animations_hmap);
    struct MapEntry_Animation* entry = NULL;
    while( (entry = (struct MapEntry_Animation*)toridraw_map_iter_next(iter)) )
    {
        if( entry->animation )
            toridraw_animation_free(entry->animation);
    }
    toridraw_map_iter_free(iter);

    gamecache_map_free(gamecache->animations_hmap);
    gamecache->animations_hmap =
        gamecache_map_new(sizeof(struct MapEntry_Animation), 512);
}
