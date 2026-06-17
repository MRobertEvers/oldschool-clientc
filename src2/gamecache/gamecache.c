#include "gamecache.h"

#include "toridraw/toridraw_map.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct GameCache
{
    struct ToriDraw_Map* models_hmap;
    struct ToriDraw_Map* animations_hmap;
    struct ToriDraw_Map* textures_hmap;
    struct ToriDraw_Map* map_terrain_hmap;
    struct ToriDraw_Map* map_scenery_hmap;
    struct ToriDraw_Map* flotype_hmap;
    struct ToriDraw_Map* config_loc_hmap;
    struct ToriDraw_Map* sequences_hmap;
    struct ToriDraw_Map* animframes_reftable;
};

struct MapEntry_AnimframeRef
{
    int frame_id;
    int anim_id;
    int frame_index;
};

struct MapEntry_Model
{
    int id;
    struct GameCache_Model* model;
};

struct MapEntry_Animation
{
    int id;
    struct GameCache_Animation* animation;
};

struct MapEntry_Texture
{
    int id;
    struct GameCache_Texture* texture;
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

static void
gamecache_maybe_grow_hmap(struct ToriDraw_Map* map)
{
    uint32_t count = toridraw_map_count(map);
    uint32_t capacity = toridraw_map_capacity(map);
    if( count * 4 <= capacity * 3 )
        return;

    size_t new_capacity = (size_t)capacity * 2;
    size_t esize = toridraw_map_entry_size(map);
    size_t new_buffer_size = toridraw_map_buffer_size_for(esize, new_capacity);
    void* new_buffer = malloc(new_buffer_size);
    void* old_buffer = NULL;
    int rc = toridraw_map_resize(map, new_buffer, new_buffer_size, new_capacity, &old_buffer);
    assert(rc == TORIDRAW_MAP_OK);
    (void)rc;
    free(old_buffer);
}

static void
gamecache_animframe_ref_add(
    struct GameCache* gamecache,
    int frame_id,
    int anim_id,
    int frame_index)
{
    struct MapEntry_AnimframeRef* entry = (struct MapEntry_AnimframeRef*)toridraw_map_search(
        gamecache->animframes_reftable, &frame_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->frame_id = frame_id;
    entry->anim_id = anim_id;
    entry->frame_index = frame_index;
    gamecache_maybe_grow_hmap(gamecache->animframes_reftable);
}

struct GameCache*
gamecache_new(void)
{
    struct GameCache* gamecache = calloc(1, sizeof(struct GameCache));
    if( !gamecache )
        return NULL;

    gamecache->models_hmap = gamecache_map_new(sizeof(struct MapEntry_Model), 1024);
    gamecache->animations_hmap = gamecache_map_new(sizeof(struct MapEntry_Animation), 512);
    gamecache->textures_hmap = gamecache_map_new(sizeof(struct MapEntry_Texture), 64);
    gamecache->map_terrain_hmap = gamecache_map_new(sizeof(struct MapEntry_MapTerrain), 256);
    gamecache->map_scenery_hmap = gamecache_map_new(sizeof(struct MapEntry_MapScenery), 256);
    gamecache->flotype_hmap = gamecache_map_new(sizeof(struct MapEntry_Flotype), 256);
    gamecache->config_loc_hmap = gamecache_map_new(sizeof(struct MapEntry_Location), 1024);
    gamecache->sequences_hmap = gamecache_map_new(sizeof(struct MapEntry_Sequence), 1024);
    gamecache->animframes_reftable = gamecache_map_new(sizeof(struct MapEntry_AnimframeRef), 4096);

    return gamecache;
}

static void
gamecache_free_models(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(map);
    struct MapEntry_Model* entry = NULL;
    while( (entry = (struct MapEntry_Model*)toridraw_map_iter_next(iter)) )
    {
        if( entry->model )
            gamecache_model_free(entry->model);
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
            gamecache_animation_free(entry->animation);
    }
    toridraw_map_iter_free(iter);
}

static void
gamecache_free_textures(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = toridraw_map_iter_new(map);
    struct MapEntry_Texture* entry = NULL;
    while( (entry = (struct MapEntry_Texture*)toridraw_map_iter_next(iter)) )
    {
        if( entry->texture )
            gamecache_texture_free(entry->texture);
    }
    toridraw_map_iter_free(iter);
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

    gamecache_free_models(gamecache->models_hmap);
    gamecache_map_free(gamecache->models_hmap);

    gamecache_free_animations(gamecache->animations_hmap);
    gamecache_map_free(gamecache->animations_hmap);

    gamecache_free_textures(gamecache->textures_hmap);
    gamecache_map_free(gamecache->textures_hmap);

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

    gamecache_map_free(gamecache->animframes_reftable);

    free(gamecache);
}

void
gamecache_model_add(
    struct GameCache* gamecache,
    int model_id,
    struct GameCache_Model* model)
{
    struct MapEntry_Model* entry = (struct MapEntry_Model*)toridraw_map_search(
        gamecache->models_hmap, &model_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->model )
        gamecache_model_free(entry->model);

    entry->id = model_id;
    entry->model = model;
}

struct GameCache_Model*
gamecache_model_get(
    struct GameCache* gamecache,
    int model_id)
{
    struct MapEntry_Model* entry = (struct MapEntry_Model*)toridraw_map_search(
        gamecache->models_hmap, &model_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->model;
}

bool
gamecache_model_has(
    struct GameCache* gamecache,
    int model_id)
{
    return gamecache_model_get(gamecache, model_id) != NULL;
}

struct GameCache_Model*
gamecache_model_remove(
    struct GameCache* gamecache,
    int model_id)
{
    struct MapEntry_Model* entry = (struct MapEntry_Model*)toridraw_map_search(
        gamecache->models_hmap, &model_id, TORIDRAW_MAP_REMOVE);
    if( !entry )
        return NULL;
    return entry->model;
}

void
gamecache_models_clear_all(struct GameCache* gamecache)
{
    if( !gamecache || !gamecache->models_hmap )
        return;

    gamecache_free_models(gamecache->models_hmap);
    gamecache_map_reset(&gamecache->models_hmap, sizeof(struct MapEntry_Model), 1024);
}

void
gamecache_texture_add(
    struct GameCache* gamecache,
    int texture_id,
    struct GameCache_Texture* texture)
{
    struct MapEntry_Texture* entry = (struct MapEntry_Texture*)toridraw_map_search(
        gamecache->textures_hmap, &texture_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->texture )
        gamecache_texture_free(entry->texture);

    entry->id = texture_id;
    entry->texture = texture;
}

struct GameCache_Texture*
gamecache_texture_get(
    struct GameCache* gamecache,
    int texture_id)
{
    struct MapEntry_Texture* entry = (struct MapEntry_Texture*)toridraw_map_search(
        gamecache->textures_hmap, &texture_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->texture;
}

bool
gamecache_texture_has(
    struct GameCache* gamecache,
    int texture_id)
{
    return gamecache_texture_get(gamecache, texture_id) != NULL;
}

void
gamecache_textures_clear_all(struct GameCache* gamecache)
{
    if( !gamecache || !gamecache->textures_hmap )
        return;

    gamecache_free_textures(gamecache->textures_hmap);
    gamecache_map_reset(&gamecache->textures_hmap, sizeof(struct MapEntry_Texture), 64);
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
    struct GameCache_Animation* animation)
{
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)toridraw_map_search(
        gamecache->animations_hmap, &anim_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->animation )
        gamecache_animation_free(entry->animation);

    entry->id = anim_id;
    entry->animation = animation;

    if( animation && animation->frames )
    {
        for( int i = 0; i < animation->frame_count; i++ )
            gamecache_animframe_ref_add(gamecache, animation->frames[i].id, anim_id, i);
    }
}

struct GameCache_Animation*
gamecache_animation_get(
    struct GameCache* gamecache,
    int anim_id)
{
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)toridraw_map_search(
        gamecache->animations_hmap, &anim_id, TORIDRAW_MAP_FIND);
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

struct GameCache_Animation*
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
gamecache_sequence_resolved_free(struct GameCache_Animation* anim)
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
    const struct GameCache_AnimFrame** out_frame,
    const struct GameCache_AnimBase** out_base,
    int* out_delay);

static bool
gamecache_animframe_copy(
    struct GameCache_AnimFrame* dst,
    const struct GameCache_AnimFrame* src)
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

struct GameCache_Animation*
gamecache_sequence_resolved_animation(
    struct GameCache* gamecache,
    int seq_id)
{
    struct GameCache_Sequence* seq = gamecache_sequence_get(gamecache, seq_id);
    if( !seq || !seq->frames || seq->frame_count <= 0 )
        return NULL;

    if( seq->resolved )
        return seq->resolved;

    struct GameCache_Animation* resolved = calloc(1, sizeof(struct GameCache_Animation));
    if( !resolved )
        return NULL;

    resolved->frame_count = seq->frame_count;
    resolved->frames = calloc((size_t)seq->frame_count, sizeof(struct GameCache_AnimFrame));
    if( !resolved->frames )
    {
        free(resolved);
        return NULL;
    }

    for( int i = 0; i < seq->frame_count; i++ )
    {
        const struct GameCache_AnimFrame* src_frame = NULL;
        const struct GameCache_AnimBase* src_base = NULL;
        int delay = 1;
        if( !gamecache_sequence_resolve_frame_impl(
                gamecache, seq_id, i, &src_frame, &src_base, &delay) )
        {
            gamecache_sequence_resolved_free(resolved);
            return NULL;
        }

        if( i == 0 )
            resolved->base = (struct GameCache_AnimBase*)src_base;

        struct GameCache_AnimFrame* dst = &resolved->frames[i];
        if( !gamecache_animframe_copy(dst, src_frame) )
        {
            gamecache_sequence_resolved_free(resolved);
            return NULL;
        }
        dst->delay = delay;
    }

    seq->resolved = resolved;
    return resolved;
}

static bool
gamecache_sequence_resolve_frame_impl(
    struct GameCache* gamecache,
    int seq_id,
    int frame_index,
    const struct GameCache_AnimFrame** out_frame,
    const struct GameCache_AnimBase** out_base,
    int* out_delay)
{
    struct GameCache_Sequence* seq = gamecache_sequence_get(gamecache, seq_id);
    if( !seq || !seq->frames || frame_index < 0 || frame_index >= seq->frame_count )
        return false;

    int const frame_id = seq->frames[frame_index];

    struct MapEntry_AnimframeRef* ref = (struct MapEntry_AnimframeRef*)toridraw_map_search(
        gamecache->animframes_reftable, &frame_id, TORIDRAW_MAP_FIND);
    if( !ref )
        return false;

    struct GameCache_Animation* anim = gamecache_animation_get(gamecache, ref->anim_id);
    if( !anim || !anim->frames || ref->frame_index < 0 || ref->frame_index >= anim->frame_count )
        return false;

    *out_frame = &anim->frames[ref->frame_index];
    *out_base = anim->base;

    if( out_delay )
    {
        int delay = seq->delay ? seq->delay[frame_index] : 0;
        if( delay == 0 )
            delay = anim->frames[ref->frame_index].delay;
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
    const struct GameCache_AnimFrame** out_frame,
    const struct GameCache_AnimBase** out_base,
    int* out_delay)
{
    return gamecache_sequence_resolve_frame_impl(
        gamecache, seq_id, frame_index, out_frame, out_base, out_delay);
}
