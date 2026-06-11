#include "gamecache.h"

#include "toridraw/toridraw_animation.h"
#include "toridraw/toridraw_gccontext.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

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
gamecache_new(struct ToriDraw_Context* context)
{
    struct GameCache* gamecache = calloc(1, sizeof(struct GameCache));
    if( !gamecache )
        return NULL;

    gamecache->context = context;
    gamecache->map_terrain_hmap = gamecache_map_new(sizeof(struct MapEntry_MapTerrain), 256);
    gamecache->map_scenery_hmap = gamecache_map_new(sizeof(struct MapEntry_MapScenery), 256);
    gamecache->flotype_hmap = gamecache_map_new(sizeof(struct MapEntry_Flotype), 256);
    gamecache->config_loc_hmap = gamecache_map_new(sizeof(struct MapEntry_Location), 1024);
    gamecache->sequences_hmap = gamecache_map_new(sizeof(struct MapEntry_Sequence), 1024);

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

void
gamecache_free(struct GameCache* gamecache)
{
    if( !gamecache )
        return;

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

    free(gamecache);
}

void
gamecache_model_add(
    struct GameCache* gamecache,
    int model_id,
    struct ToriDraw_ModelHandle model)
{
    if( !gamecache || !gamecache->context )
        return;
    toridraw_gc_model_add(gamecache->context, model_id, model);
}

struct ToriDraw_ModelHandle
gamecache_model_get(
    struct GameCache* gamecache,
    int model_id)
{
    struct ToriDraw_ModelHandle none = { .kind = TORIDRAWMK_NONE };
    if( !gamecache || !gamecache->context )
        return none;
    return toridraw_gc_model_get(gamecache->context, model_id);
}

bool
gamecache_model_has(
    struct GameCache* gamecache,
    int model_id)
{
    if( !gamecache || !gamecache->context )
        return false;
    return toridraw_gc_model_has(gamecache->context, model_id);
}

struct ToriDraw_ModelHandle
gamecache_model_remove(
    struct GameCache* gamecache,
    int model_id)
{
    struct ToriDraw_ModelHandle none = { .kind = TORIDRAWMK_NONE };
    if( !gamecache || !gamecache->context )
        return none;
    return toridraw_gc_model_remove(gamecache->context, model_id);
}

void
gamecache_models_clear_all(struct GameCache* gamecache)
{
    if( !gamecache || !gamecache->context )
        return;
    toridraw_gc_models_clear_all(gamecache->context);
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
    if( !gamecache || !gamecache->context )
        return;
    toridraw_gc_animation_add(gamecache->context, anim_id, animation);
}

struct ToriDraw_Animation*
gamecache_animation_get(
    struct GameCache* gamecache,
    int anim_id)
{
    if( !gamecache || !gamecache->context )
        return NULL;
    return toridraw_gc_animation_get(gamecache->context, anim_id);
}

bool
gamecache_animation_has(
    struct GameCache* gamecache,
    int anim_id)
{
    if( !gamecache || !gamecache->context )
        return false;
    return toridraw_gc_animation_has(gamecache->context, anim_id);
}

struct ToriDraw_Animation*
gamecache_sequence_primary_animation(
    struct GameCache* gamecache,
    int seq_id)
{
    struct GameCache_Sequence* seq = gamecache_sequence_get(gamecache, seq_id);
    if( !seq || !seq->frames || seq->frame_count <= 0 )
        return NULL;

    int const archive_id = (seq->frames[0] >> 16) & 0xFFFF;
    return gamecache_animation_get(gamecache, archive_id);
}

static void
gamecache_sequence_resolved_free(struct ToriDraw_Animation* anim)
{
    if( !anim )
        return;

    if( anim->frames )
    {
        for( int i = 0; i < anim->frame_count; i++ )
        {
            free(anim->frames[i].groups);
            free(anim->frames[i].x);
            free(anim->frames[i].y);
            free(anim->frames[i].z);
        }
        free(anim->frames);
    }

    free(anim);
}

static bool
gamecache_sequence_resolve_frame_impl(
    struct GameCache* gamecache,
    int seq_id,
    int frame_index,
    const struct ToriDraw_AnimFrame** out_frame,
    const struct ToriDraw_AnimBase** out_base,
    int* out_delay);

static bool
gamecache_animframe_copy(
    struct ToriDraw_AnimFrame* dst,
    const struct ToriDraw_AnimFrame* src)
{
    memset(dst, 0, sizeof(*dst));
    dst->id = src->id;
    dst->length = src->length;
    dst->delay = src->delay;

    if( src->length <= 0 )
        return true;

    dst->groups = malloc((size_t)src->length * sizeof(int16_t));
    dst->x = malloc((size_t)src->length * sizeof(int16_t));
    dst->y = malloc((size_t)src->length * sizeof(int16_t));
    dst->z = malloc((size_t)src->length * sizeof(int16_t));
    if( !dst->groups || !dst->x || !dst->y || !dst->z )
    {
        free(dst->groups);
        free(dst->x);
        free(dst->y);
        free(dst->z);
        memset(dst, 0, sizeof(*dst));
        return false;
    }

    memcpy(dst->groups, src->groups, (size_t)src->length * sizeof(int16_t));
    memcpy(dst->x, src->x, (size_t)src->length * sizeof(int16_t));
    memcpy(dst->y, src->y, (size_t)src->length * sizeof(int16_t));
    memcpy(dst->z, src->z, (size_t)src->length * sizeof(int16_t));
    return true;
}

struct ToriDraw_Animation*
gamecache_sequence_resolved_animation(
    struct GameCache* gamecache,
    int seq_id)
{
    struct GameCache_Sequence* seq = gamecache_sequence_get(gamecache, seq_id);
    if( !seq || !seq->frames || seq->frame_count <= 0 )
        return NULL;

    if( seq->resolved )
        return seq->resolved;

    struct ToriDraw_Animation* resolved = calloc(1, sizeof(struct ToriDraw_Animation));
    if( !resolved )
        return NULL;

    resolved->frame_count = seq->frame_count;
    resolved->frames = calloc((size_t)seq->frame_count, sizeof(struct ToriDraw_AnimFrame));
    if( !resolved->frames )
    {
        free(resolved);
        return NULL;
    }

    int const archive_id = (seq->frames[0] >> 16) & 0xFFFF;
    struct ToriDraw_Animation* primary = gamecache_animation_get(gamecache, archive_id);

    resolved->base = primary->base;

    for( int i = 0; i < seq->frame_count; i++ )
    {
        const struct ToriDraw_AnimFrame* src_frame = NULL;
        const struct ToriDraw_AnimBase* src_base = NULL;
        int delay = 1;
        if( !gamecache_sequence_resolve_frame_impl(
                gamecache, seq_id, i, &src_frame, &src_base, &delay) )
            continue;

        struct ToriDraw_AnimFrame* dst = &resolved->frames[i];
        if( !gamecache_animframe_copy(dst, src_frame) )
        {
            gamecache_sequence_resolved_free(resolved);
            return NULL;
        }
        dst->delay = delay;
        (void)src_base;
    }

    seq->resolved = resolved;
    return resolved;
}

static bool
gamecache_sequence_resolve_frame_impl(
    struct GameCache* gamecache,
    int seq_id,
    int frame_index,
    const struct ToriDraw_AnimFrame** out_frame,
    const struct ToriDraw_AnimBase** out_base,
    int* out_delay)
{
    struct GameCache_Sequence* seq = gamecache_sequence_get(gamecache, seq_id);
    if( !seq || !seq->frames || frame_index < 0 || frame_index >= seq->frame_count )
        return false;

    int const packed = seq->frames[frame_index];
    int const archive_id = (packed >> 16) & 0xFFFF;
    int const index = packed & 0xFFFF;

    struct ToriDraw_Animation* anim = gamecache_animation_get(gamecache, archive_id);
    if( !anim || !anim->frames || index < 0 || index >= anim->frame_count )
        return false;

    *out_frame = &anim->frames[index];
    *out_base = anim->base;

    if( out_delay )
    {
        int delay = seq->delay ? seq->delay[frame_index] : 0;
        if( delay == 0 )
            delay = anim->frames[index].delay;
        if( delay <= 0 )
            delay = 1;
        *out_delay = delay;
    }

    return true;
}

bool
gamecache_sequence_resolve_frame(
    struct GameCache* gamecache,
    int seq_id,
    int frame_index,
    const struct ToriDraw_AnimFrame** out_frame,
    const struct ToriDraw_AnimBase** out_base,
    int* out_delay)
{
    return gamecache_sequence_resolve_frame_impl(
        gamecache, seq_id, frame_index, out_frame, out_base, out_delay);
}
