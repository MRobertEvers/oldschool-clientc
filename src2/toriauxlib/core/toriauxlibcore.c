#include "toriauxlib/core/toriauxlibcore.h"

#include "toridraw/toridraw_map.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriAuxLibCore
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
    struct ToriDraw_Map* sprites_hmap;
    struct ToriDraw_Map* fonts_hmap;
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
    struct ToriAuxLibCore_Model* model;
};

struct MapEntry_Animation
{
    int id;
    struct ToriAuxLibCore_Animation* animation;
};

struct MapEntry_Texture
{
    int id;
    struct ToriAuxLibCore_Texture* texture;
};

struct MapEntry_MapTerrain
{
    int id;
    struct ToriAuxLibCore_MapTerrain* terrain;
};

struct MapEntry_MapScenery
{
    int id;
    struct ToriAuxLibCore_MapLocs* locs;
};

struct MapEntry_Flotype
{
    int id;
    struct ToriAuxLibCore_Flotype* flotype;
};

struct MapEntry_Location
{
    int id;
    struct ToriAuxLibCore_Location* loc;
};

struct MapEntry_Sequence
{
    int id;
    struct ToriAuxLibCore_Sequence* sequence;
};

struct MapEntry_Sprite
{
    int id;
    struct ToriAuxLibCore_Sprite* sprite;
};

struct MapEntry_Font
{
    int id;
    struct ToriAuxLibCore_Font* font;
};

static struct ToriDraw_Map*
gamecache_map_new(
    int entry_size,
    int capacity)
{
    int buffer_size = ToriDraw_MapBufferSizeFor(entry_size, capacity);
    struct ToriDraw_MapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = entry_size,
    };
    return ToriDraw_MapNew(&config, 0);
}

static void
gamecache_map_free(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    free(ToriDraw_MapBufferPtr(map));
    ToriDraw_MapFree(map);
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
    uint32_t count = ToriDraw_MapCount(map);
    uint32_t capacity = ToriDraw_MapCapacity(map);
    if( count * 4 <= capacity * 3 )
        return;

    size_t new_capacity = (size_t)capacity * 2;
    size_t esize = ToriDraw_MapEntrySize(map);
    size_t new_buffer_size = ToriDraw_MapBufferSizeFor(esize, new_capacity);
    void* new_buffer = malloc(new_buffer_size);
    void* old_buffer = NULL;
    int rc = ToriDraw_MapResize(map, new_buffer, new_buffer_size, new_capacity, &old_buffer);
    assert(rc == TORIDRAW_MAP_OK);
    (void)rc;
    free(old_buffer);
}

static void
gamecache_animframe_ref_add(
    struct ToriAuxLibCore* gamecache,
    int frame_id,
    int anim_id,
    int frame_index)
{
    struct MapEntry_AnimframeRef* entry = (struct MapEntry_AnimframeRef*)ToriDraw_MapSearch(
        gamecache->animframes_reftable, &frame_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->frame_id = frame_id;
    entry->anim_id = anim_id;
    entry->frame_index = frame_index;
    gamecache_maybe_grow_hmap(gamecache->animframes_reftable);
}

struct ToriAuxLibCore*
ToriAuxLibCore_New(void)
{
    struct ToriAuxLibCore* gamecache = calloc(1, sizeof(struct ToriAuxLibCore));
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
    gamecache->sprites_hmap = gamecache_map_new(sizeof(struct MapEntry_Sprite), 256);
    gamecache->fonts_hmap = gamecache_map_new(sizeof(struct MapEntry_Font), 16);

    return gamecache;
}

static void
ToriAuxLibCore_Free_models(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Model* entry = NULL;
    while( (entry = (struct MapEntry_Model*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->model )
            ToriAuxLibCore_ModelFree(entry->model);
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_animations(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Animation* entry = NULL;
    while( (entry = (struct MapEntry_Animation*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->animation )
            ToriAuxLibCore_AnimationFree(entry->animation);
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_textures(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Texture* entry = NULL;
    while( (entry = (struct MapEntry_Texture*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->texture )
            ToriAuxLibCore_TextureFree(entry->texture);
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_map_terrain(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_MapTerrain* entry = NULL;
    while( (entry = (struct MapEntry_MapTerrain*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->terrain )
            ToriAuxLibCore_MapTerrainFree(entry->terrain);
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_map_scenery(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_MapScenery* entry = NULL;
    while( (entry = (struct MapEntry_MapScenery*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->locs )
            ToriAuxLibCore_MapLocsFree(entry->locs);
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_flotype(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Flotype* entry = NULL;
    while( (entry = (struct MapEntry_Flotype*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->flotype )
            ToriAuxLibCore_FlotypeFree(entry->flotype);
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_locations(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Location* entry = NULL;
    while( (entry = (struct MapEntry_Location*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->loc )
            ToriAuxLibCore_LocationFree(entry->loc);
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_sequences(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Sequence* entry = NULL;
    while( (entry = (struct MapEntry_Sequence*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->sequence )
            ToriAuxLibCore_SequenceFree(entry->sequence);
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_sprites(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Sprite* entry = NULL;
    while( (entry = (struct MapEntry_Sprite*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->sprite )
            ToriAuxLibCore_SpriteFree(entry->sprite);
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_fonts(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Font* entry = NULL;
    while( (entry = (struct MapEntry_Font*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->font )
            ToriAuxLibCore_FontFree(entry->font);
    }
    ToriDraw_MapIterFree(iter);
}

void
ToriAuxLibCore_Free(struct ToriAuxLibCore* gamecache)
{
    if( !gamecache )
        return;

    ToriAuxLibCore_Free_models(gamecache->models_hmap);
    gamecache_map_free(gamecache->models_hmap);

    ToriAuxLibCore_Free_animations(gamecache->animations_hmap);
    gamecache_map_free(gamecache->animations_hmap);

    ToriAuxLibCore_Free_textures(gamecache->textures_hmap);
    gamecache_map_free(gamecache->textures_hmap);

    ToriAuxLibCore_Free_map_terrain(gamecache->map_terrain_hmap);
    gamecache_map_free(gamecache->map_terrain_hmap);

    ToriAuxLibCore_Free_map_scenery(gamecache->map_scenery_hmap);
    gamecache_map_free(gamecache->map_scenery_hmap);

    ToriAuxLibCore_Free_flotype(gamecache->flotype_hmap);
    gamecache_map_free(gamecache->flotype_hmap);

    ToriAuxLibCore_Free_locations(gamecache->config_loc_hmap);
    gamecache_map_free(gamecache->config_loc_hmap);

    ToriAuxLibCore_Free_sequences(gamecache->sequences_hmap);
    gamecache_map_free(gamecache->sequences_hmap);

    gamecache_map_free(gamecache->animframes_reftable);

    ToriAuxLibCore_Free_sprites(gamecache->sprites_hmap);
    gamecache_map_free(gamecache->sprites_hmap);

    ToriAuxLibCore_Free_fonts(gamecache->fonts_hmap);
    gamecache_map_free(gamecache->fonts_hmap);

    free(gamecache);
}

void
ToriAuxLibCore_ModelAdd(
    struct ToriAuxLibCore* gamecache,
    int model_id,
    struct ToriAuxLibCore_Model* model)
{
    struct MapEntry_Model* entry = (struct MapEntry_Model*)ToriDraw_MapSearch(
        gamecache->models_hmap, &model_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->model )
        ToriAuxLibCore_ModelFree(entry->model);

    entry->id = model_id;
    entry->model = model;
}

struct ToriAuxLibCore_Model*
ToriAuxLibCore_ModelGet(
    struct ToriAuxLibCore* gamecache,
    int model_id)
{
    struct MapEntry_Model* entry = (struct MapEntry_Model*)ToriDraw_MapSearch(
        gamecache->models_hmap, &model_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->model;
}

bool
ToriAuxLibCore_ModelHas(
    struct ToriAuxLibCore* gamecache,
    int model_id)
{
    return ToriAuxLibCore_ModelGet(gamecache, model_id) != NULL;
}

struct ToriAuxLibCore_Model*
ToriAuxLibCore_ModelRemove(
    struct ToriAuxLibCore* gamecache,
    int model_id)
{
    struct MapEntry_Model* entry = (struct MapEntry_Model*)ToriDraw_MapSearch(
        gamecache->models_hmap, &model_id, TORIDRAW_MAP_REMOVE);
    if( !entry )
        return NULL;
    return entry->model;
}

void
ToriAuxLibCore_ModelsClearAll(struct ToriAuxLibCore* gamecache)
{
    if( !gamecache || !gamecache->models_hmap )
        return;

    ToriAuxLibCore_Free_models(gamecache->models_hmap);
    gamecache_map_reset(&gamecache->models_hmap, sizeof(struct MapEntry_Model), 1024);
}

void
ToriAuxLibCore_TextureAdd(
    struct ToriAuxLibCore* gamecache,
    int texture_id,
    struct ToriAuxLibCore_Texture* texture)
{
    struct MapEntry_Texture* entry = (struct MapEntry_Texture*)ToriDraw_MapSearch(
        gamecache->textures_hmap, &texture_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->texture )
        ToriAuxLibCore_TextureFree(entry->texture);

    entry->id = texture_id;
    entry->texture = texture;
}

struct ToriAuxLibCore_Texture*
ToriAuxLibCore_TextureGet(
    struct ToriAuxLibCore* gamecache,
    int texture_id)
{
    struct MapEntry_Texture* entry = (struct MapEntry_Texture*)ToriDraw_MapSearch(
        gamecache->textures_hmap, &texture_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->texture;
}

bool
ToriAuxLibCore_TextureHas(
    struct ToriAuxLibCore* gamecache,
    int texture_id)
{
    return ToriAuxLibCore_TextureGet(gamecache, texture_id) != NULL;
}

void
ToriAuxLibCore_TexturesClearAll(struct ToriAuxLibCore* gamecache)
{
    if( !gamecache || !gamecache->textures_hmap )
        return;

    ToriAuxLibCore_Free_textures(gamecache->textures_hmap);
    gamecache_map_reset(&gamecache->textures_hmap, sizeof(struct MapEntry_Texture), 64);
}

void
ToriAuxLibCore_SpriteAdd(
    struct ToriAuxLibCore* gamecache,
    int sprite_id,
    struct ToriAuxLibCore_Sprite* sprite)
{
    struct MapEntry_Sprite* entry = (struct MapEntry_Sprite*)ToriDraw_MapSearch(
        gamecache->sprites_hmap, &sprite_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->sprite )
        ToriAuxLibCore_SpriteFree(entry->sprite);

    entry->id = sprite_id;
    entry->sprite = sprite;
    gamecache_maybe_grow_hmap(gamecache->sprites_hmap);
}

struct ToriAuxLibCore_Sprite*
ToriAuxLibCore_SpriteGet(
    struct ToriAuxLibCore* gamecache,
    int sprite_id)
{
    struct MapEntry_Sprite* entry = (struct MapEntry_Sprite*)ToriDraw_MapSearch(
        gamecache->sprites_hmap, &sprite_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->sprite;
}

bool
ToriAuxLibCore_SpriteHas(
    struct ToriAuxLibCore* gamecache,
    int sprite_id)
{
    return ToriAuxLibCore_SpriteGet(gamecache, sprite_id) != NULL;
}

void
ToriAuxLibCore_SpritesClearAll(struct ToriAuxLibCore* gamecache)
{
    if( !gamecache || !gamecache->sprites_hmap )
        return;

    ToriAuxLibCore_Free_sprites(gamecache->sprites_hmap);
    gamecache_map_reset(&gamecache->sprites_hmap, sizeof(struct MapEntry_Sprite), 256);
}

void
ToriAuxLibCore_FontAdd(
    struct ToriAuxLibCore* gamecache,
    int font_id,
    struct ToriAuxLibCore_Font* font)
{
    struct MapEntry_Font* entry = (struct MapEntry_Font*)ToriDraw_MapSearch(
        gamecache->fonts_hmap, &font_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->font )
        ToriAuxLibCore_FontFree(entry->font);

    entry->id = font_id;
    entry->font = font;
    gamecache_maybe_grow_hmap(gamecache->fonts_hmap);
}

struct ToriAuxLibCore_Font*
ToriAuxLibCore_FontGet(
    struct ToriAuxLibCore* gamecache,
    int font_id)
{
    struct MapEntry_Font* entry = (struct MapEntry_Font*)ToriDraw_MapSearch(
        gamecache->fonts_hmap, &font_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->font;
}

bool
ToriAuxLibCore_FontHas(
    struct ToriAuxLibCore* gamecache,
    int font_id)
{
    return ToriAuxLibCore_FontGet(gamecache, font_id) != NULL;
}

void
ToriAuxLibCore_FontsClearAll(struct ToriAuxLibCore* gamecache)
{
    if( !gamecache || !gamecache->fonts_hmap )
        return;

    ToriAuxLibCore_Free_fonts(gamecache->fonts_hmap);
    gamecache_map_reset(&gamecache->fonts_hmap, sizeof(struct MapEntry_Font), 16);
}

void
ToriAuxLibCore_MapTerrainAdd(
    struct ToriAuxLibCore* gamecache,
    int map_id,
    struct ToriAuxLibCore_MapTerrain* terrain)
{
    struct MapEntry_MapTerrain* entry = (struct MapEntry_MapTerrain*)ToriDraw_MapSearch(
        gamecache->map_terrain_hmap, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->terrain )
        ToriAuxLibCore_MapTerrainFree(entry->terrain);

    entry->id = map_id;
    entry->terrain = terrain;
}

struct ToriAuxLibCore_MapTerrain*
ToriAuxLibCore_MapTerrainGet(
    struct ToriAuxLibCore* gamecache,
    int map_id)
{
    struct MapEntry_MapTerrain* entry = (struct MapEntry_MapTerrain*)ToriDraw_MapSearch(
        gamecache->map_terrain_hmap, &map_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->terrain;
}

bool
ToriAuxLibCore_MapTerrainHas(
    struct ToriAuxLibCore* gamecache,
    int map_id)
{
    return ToriAuxLibCore_MapTerrainGet(gamecache, map_id) != NULL;
}

void
ToriAuxLibCore_MapSceneryAdd(
    struct ToriAuxLibCore* gamecache,
    int map_id,
    struct ToriAuxLibCore_MapLocs* locs)
{
    struct MapEntry_MapScenery* entry = (struct MapEntry_MapScenery*)ToriDraw_MapSearch(
        gamecache->map_scenery_hmap, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->locs )
        ToriAuxLibCore_MapLocsFree(entry->locs);

    entry->id = map_id;
    entry->locs = locs;
}

struct ToriAuxLibCore_MapLocs*
ToriAuxLibCore_MapSceneryGet(
    struct ToriAuxLibCore* gamecache,
    int map_id)
{
    struct MapEntry_MapScenery* entry = (struct MapEntry_MapScenery*)ToriDraw_MapSearch(
        gamecache->map_scenery_hmap, &map_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->locs;
}

bool
ToriAuxLibCore_MapSceneryHas(
    struct ToriAuxLibCore* gamecache,
    int map_id)
{
    return ToriAuxLibCore_MapSceneryGet(gamecache, map_id) != NULL;
}

void
ToriAuxLibCore_FlotypeAdd(
    struct ToriAuxLibCore* gamecache,
    int flo_id,
    struct ToriAuxLibCore_Flotype* flotype)
{
    struct MapEntry_Flotype* entry = (struct MapEntry_Flotype*)ToriDraw_MapSearch(
        gamecache->flotype_hmap, &flo_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->flotype )
        ToriAuxLibCore_FlotypeFree(entry->flotype);

    entry->id = flo_id;
    entry->flotype = flotype;
}

struct ToriAuxLibCore_Flotype*
ToriAuxLibCore_FlotypeGet(
    struct ToriAuxLibCore* gamecache,
    int flo_id)
{
    struct MapEntry_Flotype* entry = (struct MapEntry_Flotype*)ToriDraw_MapSearch(
        gamecache->flotype_hmap, &flo_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->flotype;
}

bool
ToriAuxLibCore_FlotypeHas(
    struct ToriAuxLibCore* gamecache,
    int flo_id)
{
    return ToriAuxLibCore_FlotypeGet(gamecache, flo_id) != NULL;
}

void
ToriAuxLibCore_LocationAdd(
    struct ToriAuxLibCore* gamecache,
    int loc_id,
    struct ToriAuxLibCore_Location* loc)
{
    struct MapEntry_Location* entry = (struct MapEntry_Location*)ToriDraw_MapSearch(
        gamecache->config_loc_hmap, &loc_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->loc )
        ToriAuxLibCore_LocationFree(entry->loc);

    entry->id = loc_id;
    entry->loc = loc;
}

struct ToriAuxLibCore_Location*
ToriAuxLibCore_LocationGet(
    struct ToriAuxLibCore* gamecache,
    int loc_id)
{
    struct MapEntry_Location* entry = (struct MapEntry_Location*)ToriDraw_MapSearch(
        gamecache->config_loc_hmap, &loc_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->loc;
}

bool
ToriAuxLibCore_LocationHas(
    struct ToriAuxLibCore* gamecache,
    int loc_id)
{
    return ToriAuxLibCore_LocationGet(gamecache, loc_id) != NULL;
}

void
ToriAuxLibCore_SequenceAdd(
    struct ToriAuxLibCore* gamecache,
    int seq_id,
    struct ToriAuxLibCore_Sequence* seq)
{
    struct MapEntry_Sequence* entry = (struct MapEntry_Sequence*)ToriDraw_MapSearch(
        gamecache->sequences_hmap, &seq_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->sequence )
        ToriAuxLibCore_SequenceFree(entry->sequence);

    entry->id = seq_id;
    entry->sequence = seq;
}

struct ToriAuxLibCore_Sequence*
ToriAuxLibCore_SequenceGet(
    struct ToriAuxLibCore* gamecache,
    int seq_id)
{
    struct MapEntry_Sequence* entry = (struct MapEntry_Sequence*)ToriDraw_MapSearch(
        gamecache->sequences_hmap, &seq_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->sequence;
}

bool
ToriAuxLibCore_SequenceHas(
    struct ToriAuxLibCore* gamecache,
    int seq_id)
{
    return ToriAuxLibCore_SequenceGet(gamecache, seq_id) != NULL;
}

void
ToriAuxLibCore_AnimationAdd(
    struct ToriAuxLibCore* gamecache,
    int anim_id,
    struct ToriAuxLibCore_Animation* animation)
{
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)ToriDraw_MapSearch(
        gamecache->animations_hmap, &anim_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->animation )
        ToriAuxLibCore_AnimationFree(entry->animation);

    entry->id = anim_id;
    entry->animation = animation;

    if( animation && animation->frames )
    {
        for( int i = 0; i < animation->frame_count; i++ )
            gamecache_animframe_ref_add(gamecache, animation->frames[i].id, anim_id, i);
    }
}

struct ToriAuxLibCore_Animation*
ToriAuxLibCore_AnimationGet(
    struct ToriAuxLibCore* gamecache,
    int anim_id)
{
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)ToriDraw_MapSearch(
        gamecache->animations_hmap, &anim_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->animation;
}

bool
ToriAuxLibCore_AnimationHas(
    struct ToriAuxLibCore* gamecache,
    int anim_id)
{
    return ToriAuxLibCore_AnimationGet(gamecache, anim_id) != NULL;
}

struct ToriAuxLibCore_Animation*
ToriAuxLibCore_SequencePrimaryAnimation(
    struct ToriAuxLibCore* gamecache,
    int seq_id)
{
    struct ToriAuxLibCore_Sequence* seq = ToriAuxLibCore_SequenceGet(gamecache, seq_id);
    if( !seq || !seq->frames || seq->frame_count <= 0 )
        return NULL;

    int const archive_id = (seq->frames[0] >> 16) & 0xFFFF;
    return ToriAuxLibCore_AnimationGet(gamecache, archive_id);
}

static void
gamecache_sequence_resolved_free(struct ToriAuxLibCore_Animation* anim)
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
ToriAuxLibCore_SequenceResolveFrame_impl(
    struct ToriAuxLibCore* gamecache,
    int seq_id,
    int frame_index,
    const struct ToriAuxLibCore_AnimFrame** out_frame,
    const struct ToriAuxLibCore_AnimBase** out_base,
    int* out_delay);

static bool
gamecache_animframe_copy(
    struct ToriAuxLibCore_AnimFrame* dst,
    const struct ToriAuxLibCore_AnimFrame* src)
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

struct ToriAuxLibCore_Animation*
ToriAuxLibCore_SequenceResolvedAnimation(
    struct ToriAuxLibCore* gamecache,
    int seq_id)
{
    struct ToriAuxLibCore_Sequence* seq = ToriAuxLibCore_SequenceGet(gamecache, seq_id);
    if( !seq || !seq->frames || seq->frame_count <= 0 )
        return NULL;

    if( seq->resolved )
        return seq->resolved;

    struct ToriAuxLibCore_Animation* resolved = calloc(1, sizeof(struct ToriAuxLibCore_Animation));
    if( !resolved )
        return NULL;

    resolved->frame_count = seq->frame_count;
    resolved->frames = calloc((size_t)seq->frame_count, sizeof(struct ToriAuxLibCore_AnimFrame));
    if( !resolved->frames )
    {
        free(resolved);
        return NULL;
    }

    for( int i = 0; i < seq->frame_count; i++ )
    {
        const struct ToriAuxLibCore_AnimFrame* src_frame = NULL;
        const struct ToriAuxLibCore_AnimBase* src_base = NULL;
        int delay = 1;
        if( !ToriAuxLibCore_SequenceResolveFrame_impl(
                gamecache, seq_id, i, &src_frame, &src_base, &delay) )
        {
            gamecache_sequence_resolved_free(resolved);
            return NULL;
        }

        if( i == 0 )
            resolved->base = (struct ToriAuxLibCore_AnimBase*)src_base;

        struct ToriAuxLibCore_AnimFrame* dst = &resolved->frames[i];
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
ToriAuxLibCore_SequenceResolveFrame_impl(
    struct ToriAuxLibCore* gamecache,
    int seq_id,
    int frame_index,
    const struct ToriAuxLibCore_AnimFrame** out_frame,
    const struct ToriAuxLibCore_AnimBase** out_base,
    int* out_delay)
{
    struct ToriAuxLibCore_Sequence* seq = ToriAuxLibCore_SequenceGet(gamecache, seq_id);
    if( !seq || !seq->frames || frame_index < 0 || frame_index >= seq->frame_count )
        return false;

    int const frame_id = seq->frames[frame_index];

    struct MapEntry_AnimframeRef* ref = (struct MapEntry_AnimframeRef*)ToriDraw_MapSearch(
        gamecache->animframes_reftable, &frame_id, TORIDRAW_MAP_FIND);
    if( !ref )
        return false;

    struct ToriAuxLibCore_Animation* anim = ToriAuxLibCore_AnimationGet(gamecache, ref->anim_id);
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
ToriAuxLibCore_SequenceResolveFrame(
    struct ToriAuxLibCore* gamecache,
    int seq_id,
    int frame_index,
    const struct ToriAuxLibCore_AnimFrame** out_frame,
    const struct ToriAuxLibCore_AnimBase** out_base,
    int* out_delay)
{
    return ToriAuxLibCore_SequenceResolveFrame_impl(
        gamecache, seq_id, frame_index, out_frame, out_base, out_delay);
}
