#include "gamecache.h"

#include "toridraw/toridraw_animation.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct MapEntry_ToriModel
{
    int id;
    struct ToriDraw_ModelHandle model;
};

struct MapEntry_MapTerrain
{
    int id;
    struct GameCache_MapTerrain* terrain;
};

struct MapEntry_MapScenery
{
    int id;
    struct GameCache_MapLocs* locs;
};

struct MapEntry_Flotype
{
    int id;
    struct GameCache_Flotype* flotype;
};

struct MapEntry_Location
{
    int id;
    struct GameCache_Location* loc;
};

struct MapEntry_Sequence
{
    int id;
    struct GameCache_Sequence* sequence;
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

static void
gamecache_map_reset(
    struct ToriDraw_Map** map_out,
    int entry_size,
    int capacity)
{
    if( !map_out || !*map_out )
        return;

    gamecache_map_free(*map_out);
    *map_out = gamecache_map_new(entry_size, capacity);
}

struct GameCache*
gamecache_new(void)
{
    struct GameCache* gamecache = calloc(1, sizeof(struct GameCache));
    if( !gamecache )
        return NULL;

    gamecache->models_hmap = gamecache_map_new(sizeof(struct MapEntry_ToriModel), 1024);
    gamecache->map_terrain_hmap = gamecache_map_new(sizeof(struct MapEntry_MapTerrain), 256);
    gamecache->map_scenery_hmap = gamecache_map_new(sizeof(struct MapEntry_MapScenery), 256);
    gamecache->flotype_hmap = gamecache_map_new(sizeof(struct MapEntry_Flotype), 256);
    gamecache->config_loc_hmap = gamecache_map_new(sizeof(struct MapEntry_Location), 1024);
    gamecache->sequences_hmap = gamecache_map_new(sizeof(struct MapEntry_Sequence), 1024);
    gamecache->animation_hmap = gamecache_map_new(sizeof(struct MapEntry_Animation), 512);

    return gamecache;
}

static void
gamecache_free_map_terrain(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(map);
    struct MapEntry_MapTerrain* entry = NULL;
    while( (entry = (struct MapEntry_MapTerrain*)toridraw_map_iter_next(iter)) )
    {
        if( entry->terrain )
            gamecache_map_terrain_free(entry->terrain);
    }
    toridraw_map_iter_free(iter);
}

static void
gamecache_free_map_scenery(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(map);
    struct MapEntry_MapScenery* entry = NULL;
    while( (entry = (struct MapEntry_MapScenery*)toridraw_map_iter_next(iter)) )
    {
        if( entry->locs )
            gamecache_map_locs_free(entry->locs);
    }
    toridraw_map_iter_free(iter);
}

static void
gamecache_free_flotype(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(map);
    struct MapEntry_Flotype* entry = NULL;
    while( (entry = (struct MapEntry_Flotype*)toridraw_map_iter_next(iter)) )
    {
        if( entry->flotype )
            gamecache_flotype_free(entry->flotype);
    }
    toridraw_map_iter_free(iter);
}

static void
gamecache_free_locations(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(map);
    struct MapEntry_Location* entry = NULL;
    while( (entry = (struct MapEntry_Location*)toridraw_map_iter_next(iter)) )
    {
        if( entry->loc )
            gamecache_location_free(entry->loc);
    }
    toridraw_map_iter_free(iter);
}

static void
gamecache_free_sequences(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(map);
    struct MapEntry_Sequence* entry = NULL;
    while( (entry = (struct MapEntry_Sequence*)toridraw_map_iter_next(iter)) )
    {
        if( entry->sequence )
            gamecache_sequence_free(entry->sequence);
    }
    toridraw_map_iter_free(iter);
}

static void
gamecache_free_animations(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(map);
    struct MapEntry_Animation* entry = NULL;
    while( (entry = (struct MapEntry_Animation*)toridraw_map_iter_next(iter)) )
    {
        if( entry->animation )
            toridraw_animation_free(entry->animation);
    }
    toridraw_map_iter_free(iter);
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

    gamecache_free_map_terrain(gamecache->map_terrain_hmap);
    gamecache_map_free(gamecache->map_terrain_hmap);

    gamecache_free_map_scenery(gamecache->map_scenery_hmap);
    gamecache_map_free(gamecache->map_scenery_hmap);

    gamecache_free_flotype(gamecache->flotype_hmap);
    gamecache_map_free(gamecache->flotype_hmap);

    gamecache_free_locations(gamecache->config_loc_hmap);
    gamecache_map_free(gamecache->config_loc_hmap);

    gamecache_free_sequences(gamecache->sequences_hmap);
    gamecache_map_free(gamecache->sequences_hmap);

    gamecache_free_animations(gamecache->animation_hmap);
    gamecache_map_free(gamecache->animation_hmap);

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

bool
gamecache_model_has(
    struct GameCache* gamecache,
    int model_id)
{
    return gamecache_model_get(gamecache, model_id).kind == TORIDRAWMK_MODEL;
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

    gamecache_map_reset(&gamecache->models_hmap, sizeof(struct MapEntry_ToriModel), 1024);
}

void
gamecache_map_terrain_add(
    struct GameCache* gamecache,
    int map_id,
    struct GameCache_MapTerrain* terrain)
{
    struct MapEntry_MapTerrain* entry = (struct MapEntry_MapTerrain*)toridraw_map_search(
        gamecache->map_terrain_hmap, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->terrain )
        gamecache_map_terrain_free(entry->terrain);

    entry->id = map_id;
    entry->terrain = terrain;
}

struct GameCache_MapTerrain*
gamecache_map_terrain_get(
    struct GameCache* gamecache,
    int map_id)
{
    struct MapEntry_MapTerrain* entry = (struct MapEntry_MapTerrain*)toridraw_map_search(
        gamecache->map_terrain_hmap, &map_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->terrain;
}

bool
gamecache_map_terrain_has(
    struct GameCache* gamecache,
    int map_id)
{
    return gamecache_map_terrain_get(gamecache, map_id) != NULL;
}

void
gamecache_map_scenery_add(
    struct GameCache* gamecache,
    int map_id,
    struct GameCache_MapLocs* locs)
{
    struct MapEntry_MapScenery* entry = (struct MapEntry_MapScenery*)toridraw_map_search(
        gamecache->map_scenery_hmap, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->locs )
        gamecache_map_locs_free(entry->locs);

    entry->id = map_id;
    entry->locs = locs;
}

struct GameCache_MapLocs*
gamecache_map_scenery_get(
    struct GameCache* gamecache,
    int map_id)
{
    struct MapEntry_MapScenery* entry = (struct MapEntry_MapScenery*)toridraw_map_search(
        gamecache->map_scenery_hmap, &map_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->locs;
}

bool
gamecache_map_scenery_has(
    struct GameCache* gamecache,
    int map_id)
{
    return gamecache_map_scenery_get(gamecache, map_id) != NULL;
}

void
gamecache_flotype_add(
    struct GameCache* gamecache,
    int flo_id,
    struct GameCache_Flotype* flotype)
{
    struct MapEntry_Flotype* entry = (struct MapEntry_Flotype*)toridraw_map_search(
        gamecache->flotype_hmap, &flo_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->flotype )
        gamecache_flotype_free(entry->flotype);

    entry->id = flo_id;
    entry->flotype = flotype;
}

struct GameCache_Flotype*
gamecache_flotype_get(
    struct GameCache* gamecache,
    int flo_id)
{
    struct MapEntry_Flotype* entry = (struct MapEntry_Flotype*)toridraw_map_search(
        gamecache->flotype_hmap, &flo_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->flotype;
}

bool
gamecache_flotype_has(
    struct GameCache* gamecache,
    int flo_id)
{
    return gamecache_flotype_get(gamecache, flo_id) != NULL;
}

void
gamecache_location_add(
    struct GameCache* gamecache,
    int loc_id,
    struct GameCache_Location* loc)
{
    struct MapEntry_Location* entry = (struct MapEntry_Location*)toridraw_map_search(
        gamecache->config_loc_hmap, &loc_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->loc )
        gamecache_location_free(entry->loc);

    entry->id = loc_id;
    entry->loc = loc;
}

struct GameCache_Location*
gamecache_location_get(
    struct GameCache* gamecache,
    int loc_id)
{
    struct MapEntry_Location* entry = (struct MapEntry_Location*)toridraw_map_search(
        gamecache->config_loc_hmap, &loc_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->loc;
}

bool
gamecache_location_has(
    struct GameCache* gamecache,
    int loc_id)
{
    return gamecache_location_get(gamecache, loc_id) != NULL;
}

void
gamecache_sequence_add(
    struct GameCache* gamecache,
    int seq_id,
    struct GameCache_Sequence* seq)
{
    struct MapEntry_Sequence* entry = (struct MapEntry_Sequence*)toridraw_map_search(
        gamecache->sequences_hmap, &seq_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->sequence )
        gamecache_sequence_free(entry->sequence);

    entry->id = seq_id;
    entry->sequence = seq;
}

struct GameCache_Sequence*
gamecache_sequence_get(
    struct GameCache* gamecache,
    int seq_id)
{
    struct MapEntry_Sequence* entry = (struct MapEntry_Sequence*)toridraw_map_search(
        gamecache->sequences_hmap, &seq_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->sequence;
}

bool
gamecache_sequence_has(
    struct GameCache* gamecache,
    int seq_id)
{
    return gamecache_sequence_get(gamecache, seq_id) != NULL;
}

void
gamecache_animation_add(
    struct GameCache* gamecache,
    int anim_id,
    struct ToriDraw_Animation* animation)
{
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)toridraw_map_search(
        gamecache->animation_hmap, &anim_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->animation )
        toridraw_animation_free(entry->animation);

    entry->id = anim_id;
    entry->animation = animation;
}

struct ToriDraw_Animation*
gamecache_animation_get(
    struct GameCache* gamecache,
    int anim_id)
{
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)toridraw_map_search(
        gamecache->animation_hmap, &anim_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->animation;
}

bool
gamecache_animation_has(
    struct GameCache* gamecache,
    int anim_id)
{
    return gamecache_animation_get(gamecache, anim_id) != NULL;
}
