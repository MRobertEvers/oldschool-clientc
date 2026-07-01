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
    struct ToriDraw_Map* underlay_hmap;
    struct ToriDraw_Map* config_loc_hmap;
    struct ToriDraw_Map* npctype_hmap;
    struct ToriDraw_Map* objtype_hmap;
    struct ToriDraw_Map* sequences_hmap;
    struct ToriDraw_Map* animframes_reftable;
    struct ToriDraw_Map* sprites_hmap;
    struct ToriDraw_Map* fonts_hmap;
    struct ToriDraw_Map* components_hmap;
    struct ToriDraw_Map* clientscripts_hmap;
    struct ToriDraw_Map* skeletal_hmap;
    struct ToriDraw_Map* idk_models_hmap;
    struct ToriDraw_Map* obj_models_hmap;
    size_t asset_bytes[TORIAUXLIBCORE_KIND_COUNT];
    size_t map_buffer_bytes;
};

static void
gamecache_mem_track_add(
    struct ToriAuxLibCore* gamecache,
    enum ToriAuxLibCore_Kind kind,
    size_t bytes)
{
    if( !gamecache || kind >= TORIAUXLIBCORE_KIND_COUNT )
        return;
    gamecache->asset_bytes[kind] += bytes;
}

static void
gamecache_mem_track_sub(
    struct ToriAuxLibCore* gamecache,
    enum ToriAuxLibCore_Kind kind,
    size_t bytes)
{
    if( !gamecache || kind >= TORIAUXLIBCORE_KIND_COUNT )
        return;
    gamecache->asset_bytes[kind] -= bytes;
}

static size_t
gamecache_map_buffer_size(struct ToriDraw_Map* map)
{
    if( !map )
        return 0;
    return ToriDraw_MapBufferSizeFor(ToriDraw_MapEntrySize(map), ToriDraw_MapCapacity(map));
}

struct MapEntry_AnimframeRef
{
    int frame_id;
    int anim_id;
    int frame_index;
};

struct MapEntry_SkeletalAnim
{
    int id;
    struct ToriAuxLibCore_SkeletalAnim* skeletal;
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

struct MapEntry_Npctype
{
    int id;
    struct ToriAuxLibCore_Npctype* npctype;
};

struct MapEntry_Objtype
{
    int id;
    struct ToriAuxLibCore_Objtype* objtype;
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

struct MapEntry_Component
{
    int id;
    struct ToriAuxLibCore_Component* component;
};

struct MapEntry_ClientScript
{
    int id;
    struct ToriAuxLibCore_ClientScript* script;
};

struct MapEntry_IdkModel
{
    int id;
    struct ToriAuxLibCore_Model* model;
};

struct MapEntry_ObjModel
{
    int id;
    struct ToriAuxLibCore_Model* model;
};

static struct ToriDraw_Map*
gamecache_map_new(
    struct ToriAuxLibCore* gamecache,
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
    struct ToriDraw_Map* map = ToriDraw_MapNew(&config, 0);
    if( gamecache && map )
        gamecache->map_buffer_bytes += (size_t)buffer_size;
    return map;
}

static void
gamecache_map_free(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    if( gamecache )
        gamecache->map_buffer_bytes -= gamecache_map_buffer_size(map);
    free(ToriDraw_MapBufferPtr(map));
    ToriDraw_MapFree(map);
}

static void
gamecache_map_reset(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map** map_out,
    int entry_size,
    int capacity)
{
    if( !map_out || !*map_out )
        return;

    gamecache_map_free(gamecache, *map_out);
    *map_out = gamecache_map_new(gamecache, entry_size, capacity);
}

enum
{
    GAMECACHE_CAP_MODELS = 256,
    GAMECACHE_CAP_ANIMATIONS = 256,
    GAMECACHE_CAP_TEXTURES = 64,
    GAMECACHE_CAP_MAP_TERRAIN = 64,
    GAMECACHE_CAP_MAP_SCENERY = 64,
    GAMECACHE_CAP_FLOTYPE = 64,
    GAMECACHE_CAP_LOCATION = 256,
    GAMECACHE_CAP_NPCTYPE = 256,
    GAMECACHE_CAP_OBJTYPE = 256,
    GAMECACHE_CAP_SEQUENCE = 256,
    GAMECACHE_CAP_ANIMFRAME_REF = 512,
    GAMECACHE_CAP_SPRITE = 256,
    GAMECACHE_CAP_FONT = 8,
    GAMECACHE_CAP_COMPONENT = 1024,
    GAMECACHE_CAP_CLIENTSCRIPT = 256,
    GAMECACHE_CAP_SKELETAL = 256,
    GAMECACHE_CAP_IDK_MODEL = 64,
    GAMECACHE_CAP_OBJ_MODEL = 256,
};

static struct ToriDraw_Map*
gamecache_map_ptr(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map** slot,
    int entry_size,
    int initial_capacity)
{
    if( !gamecache || !slot )
        return NULL;
    if( !*slot )
        *slot = gamecache_map_new(gamecache, entry_size, initial_capacity);
    return *slot;
}

static void
gamecache_maybe_grow_hmap(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    uint32_t count = ToriDraw_MapCount(map);
    uint32_t capacity = ToriDraw_MapCapacity(map);
    if( count * 4 <= capacity * 3 )
        return;

    size_t old_buffer_size = gamecache_map_buffer_size(map);
    size_t new_capacity = (size_t)capacity * 2;
    size_t esize = ToriDraw_MapEntrySize(map);
    size_t new_buffer_size = ToriDraw_MapBufferSizeFor(esize, new_capacity);
    void* new_buffer = malloc(new_buffer_size);
    void* old_buffer = NULL;
    int rc = ToriDraw_MapResize(map, new_buffer, new_buffer_size, new_capacity, &old_buffer);
    assert(rc == TORIDRAW_MAP_OK);
    (void)rc;
    free(old_buffer);
    if( gamecache )
        gamecache->map_buffer_bytes += new_buffer_size - old_buffer_size;
}

static void
gamecache_prepare_hmap_insert(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    uint32_t count = ToriDraw_MapCount(map);
    uint32_t capacity = ToriDraw_MapCapacity(map);
    if( capacity > 0 && count * 4 >= capacity * 3 )
        gamecache_maybe_grow_hmap(gamecache, map);
}

static void
gamecache_animframe_ref_add(
    struct ToriAuxLibCore* gamecache,
    int frame_id,
    int anim_id,
    int frame_index)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->animframes_reftable,
        sizeof(struct MapEntry_AnimframeRef),
        GAMECACHE_CAP_ANIMFRAME_REF);
    if( !map )
        return;

    struct MapEntry_AnimframeRef* entry = (struct MapEntry_AnimframeRef*)ToriDraw_MapSearch(
        map, &frame_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->frame_id = frame_id;
    entry->anim_id = anim_id;
    entry->frame_index = frame_index;
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore*
ToriAuxLibCore_New(void)
{
    return calloc(1, sizeof(struct ToriAuxLibCore));
}

static void
ToriAuxLibCore_Free_models(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Model* entry = NULL;
    while( (entry = (struct MapEntry_Model*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->model )
        {
            gamecache_mem_track_sub(
                gamecache, TORIAUXLIBCORE_KIND_MODEL, ToriAuxLibCore_ModelSizeOf(entry->model));
            ToriAuxLibCore_ModelFree(entry->model);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_animations(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Animation* entry = NULL;
    while( (entry = (struct MapEntry_Animation*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->animation )
        {
            gamecache_mem_track_sub(
                gamecache,
                TORIAUXLIBCORE_KIND_ANIMATION,
                ToriAuxLibCore_AnimationSizeOf(entry->animation));
            ToriAuxLibCore_AnimationFree(entry->animation);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_textures(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Texture* entry = NULL;
    while( (entry = (struct MapEntry_Texture*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->texture )
        {
            gamecache_mem_track_sub(
                gamecache,
                TORIAUXLIBCORE_KIND_TEXTURE,
                ToriAuxLibCore_TextureSizeOf(entry->texture));
            ToriAuxLibCore_TextureFree(entry->texture);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_map_terrain(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_MapTerrain* entry = NULL;
    while( (entry = (struct MapEntry_MapTerrain*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->terrain )
        {
            gamecache_mem_track_sub(
                gamecache,
                TORIAUXLIBCORE_KIND_MAP_TERRAIN,
                ToriAuxLibCore_MapTerrainSizeOf(entry->terrain));
            ToriAuxLibCore_MapTerrainFree(entry->terrain);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_map_scenery(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_MapScenery* entry = NULL;
    while( (entry = (struct MapEntry_MapScenery*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->locs )
        {
            gamecache_mem_track_sub(
                gamecache,
                TORIAUXLIBCORE_KIND_MAP_SCENERY,
                ToriAuxLibCore_MapLocsSizeOf(entry->locs));
            ToriAuxLibCore_MapLocsFree(entry->locs);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_flotype(
    struct ToriAuxLibCore* gamecache,
    enum ToriAuxLibCore_Kind kind,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Flotype* entry = NULL;
    while( (entry = (struct MapEntry_Flotype*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->flotype )
        {
            gamecache_mem_track_sub(gamecache, kind, ToriAuxLibCore_FlotypeSizeOf(entry->flotype));
            ToriAuxLibCore_FlotypeFree(entry->flotype);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_locations(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Location* entry = NULL;
    while( (entry = (struct MapEntry_Location*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->loc )
        {
            gamecache_mem_track_sub(
                gamecache, TORIAUXLIBCORE_KIND_LOCATION, ToriAuxLibCore_LocationSizeOf(entry->loc));
            ToriAuxLibCore_LocationFree(entry->loc);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_npctypes(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Npctype* entry = NULL;
    while( (entry = (struct MapEntry_Npctype*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->npctype )
        {
            gamecache_mem_track_sub(
                gamecache, TORIAUXLIBCORE_KIND_NPCTYPE, ToriAuxLibCore_NpctypeSizeOf(entry->npctype));
            ToriAuxLibCore_NpctypeFree(entry->npctype);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_objtypes(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Objtype* entry = NULL;
    while( (entry = (struct MapEntry_Objtype*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->objtype )
        {
            gamecache_mem_track_sub(
                gamecache, TORIAUXLIBCORE_KIND_OBJTYPE, ToriAuxLibCore_ObjtypeSizeOf(entry->objtype));
            ToriAuxLibCore_ObjtypeFree(entry->objtype);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_sequences(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Sequence* entry = NULL;
    while( (entry = (struct MapEntry_Sequence*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->sequence )
        {
            gamecache_mem_track_sub(
                gamecache,
                TORIAUXLIBCORE_KIND_SEQUENCE,
                ToriAuxLibCore_SequenceSizeOf(entry->sequence));
            ToriAuxLibCore_SequenceFree(entry->sequence);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_sprites(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Sprite* entry = NULL;
    while( (entry = (struct MapEntry_Sprite*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->sprite )
        {
            gamecache_mem_track_sub(
                gamecache, TORIAUXLIBCORE_KIND_SPRITE, ToriAuxLibCore_SpriteSizeOf(entry->sprite));
            ToriAuxLibCore_SpriteFree(entry->sprite);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_fonts(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Font* entry = NULL;
    while( (entry = (struct MapEntry_Font*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->font )
        {
            gamecache_mem_track_sub(
                gamecache, TORIAUXLIBCORE_KIND_FONT, ToriAuxLibCore_FontSizeOf(entry->font));
            ToriAuxLibCore_FontFree(entry->font);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_clientscripts(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_ClientScript* entry = NULL;
    while( (entry = (struct MapEntry_ClientScript*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->script )
        {
            gamecache_mem_track_sub(
                gamecache,
                TORIAUXLIBCORE_KIND_CLIENTSCRIPT,
                ToriAuxLibCore_ClientScriptSizeOf(entry->script));
            ToriAuxLibCore_ClientScriptFree(entry->script);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_components(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Component* entry = NULL;
    while( (entry = (struct MapEntry_Component*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->component )
        {
            gamecache_mem_track_sub(
                gamecache,
                TORIAUXLIBCORE_KIND_COMPONENT,
                ToriAuxLibCore_ComponentSizeOf(entry->component));
            ToriAuxLibCore_ComponentFree(entry->component);
        }
    }
    ToriDraw_MapIterFree(iter);
}

static void
ToriAuxLibCore_Free_skeletal(
    struct ToriAuxLibCore* gamecache,
    struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_SkeletalAnim* entry = NULL;
    while( (entry = (struct MapEntry_SkeletalAnim*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->skeletal )
        {
            gamecache_mem_track_sub(
                gamecache,
                TORIAUXLIBCORE_KIND_SKELETAL,
                ToriAuxLibCore_SkeletalAnimSizeOf(entry->skeletal));
            ToriAuxLibCore_SkeletalAnimFree(entry->skeletal);
        }
    }
    ToriDraw_MapIterFree(iter);
}

void
ToriAuxLibCore_Free(struct ToriAuxLibCore* gamecache)
{
    if( !gamecache )
        return;

    ToriAuxLibCore_Free_models(gamecache, gamecache->models_hmap);
    gamecache_map_free(gamecache, gamecache->models_hmap);

    ToriAuxLibCore_Free_models(gamecache, gamecache->idk_models_hmap);
    gamecache_map_free(gamecache, gamecache->idk_models_hmap);

    ToriAuxLibCore_Free_models(gamecache, gamecache->obj_models_hmap);
    gamecache_map_free(gamecache, gamecache->obj_models_hmap);

    ToriAuxLibCore_Free_animations(gamecache, gamecache->animations_hmap);
    gamecache_map_free(gamecache, gamecache->animations_hmap);

    ToriAuxLibCore_Free_textures(gamecache, gamecache->textures_hmap);
    gamecache_map_free(gamecache, gamecache->textures_hmap);

    ToriAuxLibCore_Free_map_terrain(gamecache, gamecache->map_terrain_hmap);
    gamecache_map_free(gamecache, gamecache->map_terrain_hmap);

    ToriAuxLibCore_Free_map_scenery(gamecache, gamecache->map_scenery_hmap);
    gamecache_map_free(gamecache, gamecache->map_scenery_hmap);

    ToriAuxLibCore_Free_flotype(gamecache, TORIAUXLIBCORE_KIND_FLOTYPE, gamecache->flotype_hmap);
    gamecache_map_free(gamecache, gamecache->flotype_hmap);

    ToriAuxLibCore_Free_flotype(gamecache, TORIAUXLIBCORE_KIND_UNDERLAY, gamecache->underlay_hmap);
    gamecache_map_free(gamecache, gamecache->underlay_hmap);

    ToriAuxLibCore_Free_locations(gamecache, gamecache->config_loc_hmap);
    gamecache_map_free(gamecache, gamecache->config_loc_hmap);

    ToriAuxLibCore_Free_npctypes(gamecache, gamecache->npctype_hmap);
    gamecache_map_free(gamecache, gamecache->npctype_hmap);

    ToriAuxLibCore_Free_objtypes(gamecache, gamecache->objtype_hmap);
    gamecache_map_free(gamecache, gamecache->objtype_hmap);

    ToriAuxLibCore_Free_sequences(gamecache, gamecache->sequences_hmap);
    gamecache_map_free(gamecache, gamecache->sequences_hmap);

    gamecache_map_free(gamecache, gamecache->animframes_reftable);

    ToriAuxLibCore_Free_sprites(gamecache, gamecache->sprites_hmap);
    gamecache_map_free(gamecache, gamecache->sprites_hmap);

    ToriAuxLibCore_Free_fonts(gamecache, gamecache->fonts_hmap);
    gamecache_map_free(gamecache, gamecache->fonts_hmap);

    ToriAuxLibCore_Free_components(gamecache, gamecache->components_hmap);
    gamecache_map_free(gamecache, gamecache->components_hmap);

    ToriAuxLibCore_Free_clientscripts(gamecache, gamecache->clientscripts_hmap);
    gamecache_map_free(gamecache, gamecache->clientscripts_hmap);

    if( gamecache->skeletal_hmap )
    {
        ToriAuxLibCore_Free_skeletal(gamecache, gamecache->skeletal_hmap);
        gamecache_map_free(gamecache, gamecache->skeletal_hmap);
    }

    free(gamecache);
}

void
ToriAuxLibCore_ModelAdd(
    struct ToriAuxLibCore* gamecache,
    int model_id,
    struct ToriAuxLibCore_Model* model)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache, &gamecache->models_hmap, sizeof(struct MapEntry_Model), GAMECACHE_CAP_MODELS);
    if( !map )
        return;

    struct MapEntry_Model* entry =
        (struct MapEntry_Model*)ToriDraw_MapSearch(map, &model_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->model )
    {
        gamecache_mem_track_sub(
            gamecache, TORIAUXLIBCORE_KIND_MODEL, ToriAuxLibCore_ModelSizeOf(entry->model));
        ToriAuxLibCore_ModelFree(entry->model);
    }

    entry->id = model_id;
    entry->model = model;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_MODEL, ToriAuxLibCore_ModelSizeOf(model));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Model*
ToriAuxLibCore_ModelGet(
    struct ToriAuxLibCore* gamecache,
    int model_id)
{
    if( !gamecache || !gamecache->models_hmap )
        return NULL;

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

void
ToriAuxLibCore_IdkModelAdd(
    struct ToriAuxLibCore* gamecache,
    int idk_id,
    struct ToriAuxLibCore_Model* model)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->idk_models_hmap,
        sizeof(struct MapEntry_IdkModel),
        GAMECACHE_CAP_IDK_MODEL);
    if( !map )
        return;

    struct MapEntry_IdkModel* entry =
        (struct MapEntry_IdkModel*)ToriDraw_MapSearch(map, &idk_id, TORIDRAW_MAP_INSERT);
    assert(entry);

    if( entry->model )
    {
        gamecache_mem_track_sub(
            gamecache, TORIAUXLIBCORE_KIND_MODEL, ToriAuxLibCore_ModelSizeOf(entry->model));
        ToriAuxLibCore_ModelFree(entry->model);
    }

    entry->id = idk_id;
    entry->model = model;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_MODEL, ToriAuxLibCore_ModelSizeOf(model));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Model*
ToriAuxLibCore_IdkModelGet(
    struct ToriAuxLibCore* gamecache,
    int idk_id)
{
    if( !gamecache || !gamecache->idk_models_hmap )
        return NULL;

    struct MapEntry_IdkModel* entry = (struct MapEntry_IdkModel*)ToriDraw_MapSearch(
        gamecache->idk_models_hmap, &idk_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->model;
}

bool
ToriAuxLibCore_IdkModelHas(
    struct ToriAuxLibCore* gamecache,
    int idk_id)
{
    return ToriAuxLibCore_IdkModelGet(gamecache, idk_id) != NULL;
}

void
ToriAuxLibCore_ObjModelAdd(
    struct ToriAuxLibCore* gamecache,
    int obj_id,
    struct ToriAuxLibCore_Model* model)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->obj_models_hmap,
        sizeof(struct MapEntry_ObjModel),
        GAMECACHE_CAP_OBJ_MODEL);
    if( !map )
        return;

    struct MapEntry_ObjModel* entry =
        (struct MapEntry_ObjModel*)ToriDraw_MapSearch(map, &obj_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->model )
    {
        gamecache_mem_track_sub(
            gamecache, TORIAUXLIBCORE_KIND_MODEL, ToriAuxLibCore_ModelSizeOf(entry->model));
        ToriAuxLibCore_ModelFree(entry->model);
    }

    entry->id = obj_id;
    entry->model = model;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_MODEL, ToriAuxLibCore_ModelSizeOf(model));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Model*
ToriAuxLibCore_ObjModelGet(
    struct ToriAuxLibCore* gamecache,
    int obj_id)
{
    if( !gamecache || !gamecache->obj_models_hmap )
        return NULL;

    struct MapEntry_ObjModel* entry = (struct MapEntry_ObjModel*)ToriDraw_MapSearch(
        gamecache->obj_models_hmap, &obj_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->model;
}

bool
ToriAuxLibCore_ObjModelHas(
    struct ToriAuxLibCore* gamecache,
    int obj_id)
{
    return ToriAuxLibCore_ObjModelGet(gamecache, obj_id) != NULL;
}

struct ToriAuxLibCore_Model*
ToriAuxLibCore_ModelRemove(
    struct ToriAuxLibCore* gamecache,
    int model_id)
{
    if( !gamecache || !gamecache->models_hmap )
        return NULL;

    struct MapEntry_Model* entry = (struct MapEntry_Model*)ToriDraw_MapSearch(
        gamecache->models_hmap, &model_id, TORIDRAW_MAP_REMOVE);
    if( !entry )
        return NULL;
    if( entry->model )
        gamecache_mem_track_sub(
            gamecache, TORIAUXLIBCORE_KIND_MODEL, ToriAuxLibCore_ModelSizeOf(entry->model));
    return entry->model;
}

void
ToriAuxLibCore_ModelsClearAll(struct ToriAuxLibCore* gamecache)
{
    if( !gamecache || !gamecache->models_hmap )
        return;

    ToriAuxLibCore_Free_models(gamecache, gamecache->models_hmap);
    gamecache_map_reset(
        gamecache, &gamecache->models_hmap, sizeof(struct MapEntry_Model), GAMECACHE_CAP_MODELS);
}

void
ToriAuxLibCore_TextureAdd(
    struct ToriAuxLibCore* gamecache,
    int texture_id,
    struct ToriAuxLibCore_Texture* texture)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->textures_hmap,
        sizeof(struct MapEntry_Texture),
        GAMECACHE_CAP_TEXTURES);
    if( !map )
        return;

    struct MapEntry_Texture* entry =
        (struct MapEntry_Texture*)ToriDraw_MapSearch(map, &texture_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->texture )
    {
        gamecache_mem_track_sub(
            gamecache, TORIAUXLIBCORE_KIND_TEXTURE, ToriAuxLibCore_TextureSizeOf(entry->texture));
        ToriAuxLibCore_TextureFree(entry->texture);
    }

    entry->id = texture_id;
    entry->texture = texture;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_TEXTURE, ToriAuxLibCore_TextureSizeOf(texture));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Texture*
ToriAuxLibCore_TextureGet(
    struct ToriAuxLibCore* gamecache,
    int texture_id)
{
    if( !gamecache || !gamecache->textures_hmap )
        return NULL;

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

    ToriAuxLibCore_Free_textures(gamecache, gamecache->textures_hmap);
    gamecache_map_reset(
        gamecache, &gamecache->textures_hmap, sizeof(struct MapEntry_Texture), GAMECACHE_CAP_TEXTURES);
}

void
ToriAuxLibCore_SpriteAdd(
    struct ToriAuxLibCore* gamecache,
    int sprite_id,
    struct ToriAuxLibCore_Sprite* sprite)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache, &gamecache->sprites_hmap, sizeof(struct MapEntry_Sprite), GAMECACHE_CAP_SPRITE);
    if( !map )
        return;

    gamecache_prepare_hmap_insert(gamecache, map);
    struct MapEntry_Sprite* entry =
        (struct MapEntry_Sprite*)ToriDraw_MapSearch(map, &sprite_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->sprite )
    {
        gamecache_mem_track_sub(
            gamecache, TORIAUXLIBCORE_KIND_SPRITE, ToriAuxLibCore_SpriteSizeOf(entry->sprite));
        ToriAuxLibCore_SpriteFree(entry->sprite);
    }

    entry->id = sprite_id;
    entry->sprite = sprite;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_SPRITE, ToriAuxLibCore_SpriteSizeOf(sprite));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Sprite*
ToriAuxLibCore_SpriteGet(
    struct ToriAuxLibCore* gamecache,
    int sprite_id)
{
    if( !gamecache || !gamecache->sprites_hmap )
        return NULL;

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

    ToriAuxLibCore_Free_sprites(gamecache, gamecache->sprites_hmap);
    gamecache_map_reset(
        gamecache, &gamecache->sprites_hmap, sizeof(struct MapEntry_Sprite), GAMECACHE_CAP_SPRITE);
}

void
ToriAuxLibCore_FontAdd(
    struct ToriAuxLibCore* gamecache,
    int font_id,
    struct ToriAuxLibCore_Font* font)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache, &gamecache->fonts_hmap, sizeof(struct MapEntry_Font), GAMECACHE_CAP_FONT);
    if( !map )
        return;

    struct MapEntry_Font* entry =
        (struct MapEntry_Font*)ToriDraw_MapSearch(map, &font_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->font )
    {
        gamecache_mem_track_sub(
            gamecache, TORIAUXLIBCORE_KIND_FONT, ToriAuxLibCore_FontSizeOf(entry->font));
        ToriAuxLibCore_FontFree(entry->font);
    }

    entry->id = font_id;
    entry->font = font;
    gamecache_mem_track_add(gamecache, TORIAUXLIBCORE_KIND_FONT, ToriAuxLibCore_FontSizeOf(font));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Font*
ToriAuxLibCore_FontGet(
    struct ToriAuxLibCore* gamecache,
    int font_id)
{
    if( !gamecache || !gamecache->fonts_hmap )
        return NULL;

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

    ToriAuxLibCore_Free_fonts(gamecache, gamecache->fonts_hmap);
    gamecache_map_reset(gamecache, &gamecache->fonts_hmap, sizeof(struct MapEntry_Font), GAMECACHE_CAP_FONT);
}

void
ToriAuxLibCore_ClientScriptAdd(
    struct ToriAuxLibCore* gamecache,
    int script_id,
    struct ToriAuxLibCore_ClientScript* script)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->clientscripts_hmap,
        sizeof(struct MapEntry_ClientScript),
        GAMECACHE_CAP_CLIENTSCRIPT);
    if( !map )
        return;

    struct MapEntry_ClientScript* entry = (struct MapEntry_ClientScript*)ToriDraw_MapSearch(
        map, &script_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->script )
    {
        gamecache_mem_track_sub(
            gamecache,
            TORIAUXLIBCORE_KIND_CLIENTSCRIPT,
            ToriAuxLibCore_ClientScriptSizeOf(entry->script));
        ToriAuxLibCore_ClientScriptFree(entry->script);
    }

    entry->id = script_id;
    entry->script = script;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_CLIENTSCRIPT, ToriAuxLibCore_ClientScriptSizeOf(script));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCore_ClientScriptGet(
    struct ToriAuxLibCore* gamecache,
    int script_id)
{
    if( !gamecache || !gamecache->clientscripts_hmap )
        return NULL;

    struct MapEntry_ClientScript* entry = (struct MapEntry_ClientScript*)ToriDraw_MapSearch(
        gamecache->clientscripts_hmap, &script_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->script;
}

bool
ToriAuxLibCore_ClientScriptHas(
    struct ToriAuxLibCore* gamecache,
    int script_id)
{
    return ToriAuxLibCore_ClientScriptGet(gamecache, script_id) != NULL;
}

void
ToriAuxLibCore_ClientScriptsClearAll(struct ToriAuxLibCore* gamecache)
{
    if( !gamecache || !gamecache->clientscripts_hmap )
        return;

    ToriAuxLibCore_Free_clientscripts(gamecache, gamecache->clientscripts_hmap);
    gamecache_map_reset(
        gamecache,
        &gamecache->clientscripts_hmap,
        sizeof(struct MapEntry_ClientScript),
        GAMECACHE_CAP_CLIENTSCRIPT);
}

void
ToriAuxLibCore_ComponentAdd(
    struct ToriAuxLibCore* gamecache,
    int component_id,
    struct ToriAuxLibCore_Component* component)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->components_hmap,
        sizeof(struct MapEntry_Component),
        GAMECACHE_CAP_COMPONENT);
    if( !map )
        return;

    gamecache_prepare_hmap_insert(gamecache, map);
    struct MapEntry_Component* entry = (struct MapEntry_Component*)ToriDraw_MapSearch(
        map, &component_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->component )
    {
        gamecache_mem_track_sub(
            gamecache,
            TORIAUXLIBCORE_KIND_COMPONENT,
            ToriAuxLibCore_ComponentSizeOf(entry->component));
        ToriAuxLibCore_ComponentFree(entry->component);
    }

    entry->id = component_id;
    entry->component = component;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_COMPONENT, ToriAuxLibCore_ComponentSizeOf(component));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Component*
ToriAuxLibCore_ComponentGet(
    struct ToriAuxLibCore* gamecache,
    int component_id)
{
    if( !gamecache || !gamecache->components_hmap )
        return NULL;

    struct MapEntry_Component* entry = (struct MapEntry_Component*)ToriDraw_MapSearch(
        gamecache->components_hmap, &component_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->component;
}

bool
ToriAuxLibCore_ComponentHas(
    struct ToriAuxLibCore* gamecache,
    int component_id)
{
    return ToriAuxLibCore_ComponentGet(gamecache, component_id) != NULL;
}

void
ToriAuxLibCore_ComponentsClearAll(struct ToriAuxLibCore* gamecache)
{
    if( !gamecache || !gamecache->components_hmap )
        return;

    ToriAuxLibCore_Free_components(gamecache, gamecache->components_hmap);
    gamecache_map_reset(
        gamecache,
        &gamecache->components_hmap,
        sizeof(struct MapEntry_Component),
        GAMECACHE_CAP_COMPONENT);
}

void
ToriAuxLibCore_SequencesClearAll(struct ToriAuxLibCore* gamecache)
{
    if( !gamecache || !gamecache->sequences_hmap )
        return;

    ToriAuxLibCore_Free_sequences(gamecache, gamecache->sequences_hmap);
    gamecache_map_reset(
        gamecache, &gamecache->sequences_hmap, sizeof(struct MapEntry_Sequence), GAMECACHE_CAP_SEQUENCE);
}

void
ToriAuxLibCore_AnimationsClearAll(struct ToriAuxLibCore* gamecache)
{
    if( !gamecache || !gamecache->animations_hmap )
        return;

    ToriAuxLibCore_Free_animations(gamecache, gamecache->animations_hmap);
    gamecache_map_reset(
        gamecache, &gamecache->animations_hmap, sizeof(struct MapEntry_Animation), GAMECACHE_CAP_ANIMATIONS);
    gamecache_map_reset(
        gamecache,
        &gamecache->animframes_reftable,
        sizeof(struct MapEntry_AnimframeRef),
        GAMECACHE_CAP_ANIMFRAME_REF);
}

void
ToriAuxLibCore_LocationsClearAll(struct ToriAuxLibCore* gamecache)
{
    if( !gamecache || !gamecache->config_loc_hmap )
        return;

    ToriAuxLibCore_Free_locations(gamecache, gamecache->config_loc_hmap);
    gamecache_map_reset(
        gamecache, &gamecache->config_loc_hmap, sizeof(struct MapEntry_Location), GAMECACHE_CAP_LOCATION);
}

void
ToriAuxLibCore_FlotypesClearAll(struct ToriAuxLibCore* gamecache)
{
    if( gamecache && gamecache->flotype_hmap )
    {
        ToriAuxLibCore_Free_flotype(gamecache, TORIAUXLIBCORE_KIND_FLOTYPE, gamecache->flotype_hmap);
        gamecache_map_reset(
            gamecache, &gamecache->flotype_hmap, sizeof(struct MapEntry_Flotype), GAMECACHE_CAP_FLOTYPE);
    }
    if( gamecache && gamecache->underlay_hmap )
    {
        ToriAuxLibCore_Free_flotype(gamecache, TORIAUXLIBCORE_KIND_UNDERLAY, gamecache->underlay_hmap);
        gamecache_map_reset(
            gamecache, &gamecache->underlay_hmap, sizeof(struct MapEntry_Flotype), GAMECACHE_CAP_FLOTYPE);
    }
}

void
ToriAuxLibCore_MapTerrainAdd(
    struct ToriAuxLibCore* gamecache,
    int map_id,
    struct ToriAuxLibCore_MapTerrain* terrain)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->map_terrain_hmap,
        sizeof(struct MapEntry_MapTerrain),
        GAMECACHE_CAP_MAP_TERRAIN);
    if( !map )
        return;

    struct MapEntry_MapTerrain* entry = (struct MapEntry_MapTerrain*)ToriDraw_MapSearch(
        map, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->terrain )
    {
        gamecache_mem_track_sub(
            gamecache,
            TORIAUXLIBCORE_KIND_MAP_TERRAIN,
            ToriAuxLibCore_MapTerrainSizeOf(entry->terrain));
        ToriAuxLibCore_MapTerrainFree(entry->terrain);
    }

    entry->id = map_id;
    entry->terrain = terrain;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_MAP_TERRAIN, ToriAuxLibCore_MapTerrainSizeOf(terrain));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_MapTerrain*
ToriAuxLibCore_MapTerrainGet(
    struct ToriAuxLibCore* gamecache,
    int map_id)
{
    if( !gamecache || !gamecache->map_terrain_hmap )
        return NULL;

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
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->map_scenery_hmap,
        sizeof(struct MapEntry_MapScenery),
        GAMECACHE_CAP_MAP_SCENERY);
    if( !map )
        return;

    struct MapEntry_MapScenery* entry = (struct MapEntry_MapScenery*)ToriDraw_MapSearch(
        map, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->locs )
    {
        gamecache_mem_track_sub(
            gamecache, TORIAUXLIBCORE_KIND_MAP_SCENERY, ToriAuxLibCore_MapLocsSizeOf(entry->locs));
        ToriAuxLibCore_MapLocsFree(entry->locs);
    }

    entry->id = map_id;
    entry->locs = locs;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_MAP_SCENERY, ToriAuxLibCore_MapLocsSizeOf(locs));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_MapLocs*
ToriAuxLibCore_MapSceneryGet(
    struct ToriAuxLibCore* gamecache,
    int map_id)
{
    if( !gamecache || !gamecache->map_scenery_hmap )
        return NULL;

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

static bool
gamecache_map_id_keep(
    int map_id,
    const int* keep_ids,
    int keep_count)
{
    for( int i = 0; i < keep_count; i++ )
    {
        if( keep_ids[i] == map_id )
            return true;
    }
    return false;
}

void
ToriAuxLibCore_MapTerrainClearExcept(
    struct ToriAuxLibCore* gamecache,
    const int* keep_map_ids,
    int keep_count)
{
    if( !gamecache || !gamecache->map_terrain_hmap || !keep_map_ids || keep_count <= 0 )
        return;

    int remove_cap = 64;
    int remove_count = 0;
    int* remove_ids = malloc((size_t)remove_cap * sizeof(int));
    if( !remove_ids )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(gamecache->map_terrain_hmap);
    struct MapEntry_MapTerrain* entry = NULL;
    while( (entry = (struct MapEntry_MapTerrain*)ToriDraw_MapIterNext(iter)) )
    {
        if( gamecache_map_id_keep(entry->id, keep_map_ids, keep_count) )
            continue;

        if( entry->terrain )
        {
            gamecache_mem_track_sub(
                gamecache,
                TORIAUXLIBCORE_KIND_MAP_TERRAIN,
                ToriAuxLibCore_MapTerrainSizeOf(entry->terrain));
            ToriAuxLibCore_MapTerrainFree(entry->terrain);
            entry->terrain = NULL;
        }

        if( remove_count >= remove_cap )
        {
            remove_cap *= 2;
            int* grown = realloc(remove_ids, (size_t)remove_cap * sizeof(int));
            if( !grown )
                break;
            remove_ids = grown;
        }
        remove_ids[remove_count++] = entry->id;
    }
    ToriDraw_MapIterFree(iter);

    for( int i = 0; i < remove_count; i++ )
        ToriDraw_MapSearch(gamecache->map_terrain_hmap, &remove_ids[i], TORIDRAW_MAP_REMOVE);

    free(remove_ids);
}

void
ToriAuxLibCore_MapSceneryClearExcept(
    struct ToriAuxLibCore* gamecache,
    const int* keep_map_ids,
    int keep_count)
{
    if( !gamecache || !gamecache->map_scenery_hmap || !keep_map_ids || keep_count <= 0 )
        return;

    int remove_cap = 64;
    int remove_count = 0;
    int* remove_ids = malloc((size_t)remove_cap * sizeof(int));
    if( !remove_ids )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(gamecache->map_scenery_hmap);
    struct MapEntry_MapScenery* entry = NULL;
    while( (entry = (struct MapEntry_MapScenery*)ToriDraw_MapIterNext(iter)) )
    {
        if( gamecache_map_id_keep(entry->id, keep_map_ids, keep_count) )
            continue;

        if( entry->locs )
        {
            gamecache_mem_track_sub(
                gamecache,
                TORIAUXLIBCORE_KIND_MAP_SCENERY,
                ToriAuxLibCore_MapLocsSizeOf(entry->locs));
            ToriAuxLibCore_MapLocsFree(entry->locs);
            entry->locs = NULL;
        }

        if( remove_count >= remove_cap )
        {
            remove_cap *= 2;
            int* grown = realloc(remove_ids, (size_t)remove_cap * sizeof(int));
            if( !grown )
                break;
            remove_ids = grown;
        }
        remove_ids[remove_count++] = entry->id;
    }
    ToriDraw_MapIterFree(iter);

    for( int i = 0; i < remove_count; i++ )
        ToriDraw_MapSearch(gamecache->map_scenery_hmap, &remove_ids[i], TORIDRAW_MAP_REMOVE);

    free(remove_ids);
}

void
ToriAuxLibCore_FlotypeAdd(
    struct ToriAuxLibCore* gamecache,
    int flo_id,
    struct ToriAuxLibCore_Flotype* flotype)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache, &gamecache->flotype_hmap, sizeof(struct MapEntry_Flotype), GAMECACHE_CAP_FLOTYPE);
    if( !map )
        return;

    struct MapEntry_Flotype* entry =
        (struct MapEntry_Flotype*)ToriDraw_MapSearch(map, &flo_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->flotype )
        ToriAuxLibCore_FlotypeFree(entry->flotype);

    entry->id = flo_id;
    entry->flotype = flotype;
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Flotype*
ToriAuxLibCore_FlotypeGet(
    struct ToriAuxLibCore* gamecache,
    int flo_id)
{
    if( !gamecache || !gamecache->flotype_hmap )
        return NULL;

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
ToriAuxLibCore_UnderlayAdd(
    struct ToriAuxLibCore* gamecache,
    int underlay_id,
    struct ToriAuxLibCore_Flotype* underlay)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache, &gamecache->underlay_hmap, sizeof(struct MapEntry_Flotype), GAMECACHE_CAP_FLOTYPE);
    if( !map )
        return;

    struct MapEntry_Flotype* entry =
        (struct MapEntry_Flotype*)ToriDraw_MapSearch(map, &underlay_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->flotype )
    {
        gamecache_mem_track_sub(
            gamecache, TORIAUXLIBCORE_KIND_UNDERLAY, ToriAuxLibCore_FlotypeSizeOf(entry->flotype));
        ToriAuxLibCore_FlotypeFree(entry->flotype);
    }

    entry->id = underlay_id;
    entry->flotype = underlay;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_UNDERLAY, ToriAuxLibCore_FlotypeSizeOf(underlay));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Flotype*
ToriAuxLibCore_UnderlayGet(
    struct ToriAuxLibCore* gamecache,
    int underlay_id)
{
    if( !gamecache || !gamecache->underlay_hmap )
        return NULL;

    struct MapEntry_Flotype* entry = (struct MapEntry_Flotype*)ToriDraw_MapSearch(
        gamecache->underlay_hmap, &underlay_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->flotype;
}

bool
ToriAuxLibCore_UnderlayHas(
    struct ToriAuxLibCore* gamecache,
    int underlay_id)
{
    return ToriAuxLibCore_UnderlayGet(gamecache, underlay_id) != NULL;
}

void
ToriAuxLibCore_LocationAdd(
    struct ToriAuxLibCore* gamecache,
    int loc_id,
    struct ToriAuxLibCore_Location* loc)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->config_loc_hmap,
        sizeof(struct MapEntry_Location),
        GAMECACHE_CAP_LOCATION);
    if( !map )
        return;

    struct MapEntry_Location* entry =
        (struct MapEntry_Location*)ToriDraw_MapSearch(map, &loc_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->loc )
    {
        gamecache_mem_track_sub(
            gamecache, TORIAUXLIBCORE_KIND_LOCATION, ToriAuxLibCore_LocationSizeOf(entry->loc));
        ToriAuxLibCore_LocationFree(entry->loc);
    }

    entry->id = loc_id;
    entry->loc = loc;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_LOCATION, ToriAuxLibCore_LocationSizeOf(loc));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Location*
ToriAuxLibCore_LocationGet(
    struct ToriAuxLibCore* gamecache,
    int loc_id)
{
    if( !gamecache || !gamecache->config_loc_hmap )
        return NULL;

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
ToriAuxLibCore_NpctypeAdd(
    struct ToriAuxLibCore* gamecache,
    int npc_id,
    struct ToriAuxLibCore_Npctype* npctype)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->npctype_hmap,
        sizeof(struct MapEntry_Npctype),
        GAMECACHE_CAP_NPCTYPE);
    if( !map )
        return;

    struct MapEntry_Npctype* entry =
        (struct MapEntry_Npctype*)ToriDraw_MapSearch(map, &npc_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->npctype )
    {
        gamecache_mem_track_sub(
            gamecache, TORIAUXLIBCORE_KIND_NPCTYPE, ToriAuxLibCore_NpctypeSizeOf(entry->npctype));
        ToriAuxLibCore_NpctypeFree(entry->npctype);
    }

    entry->id = npc_id;
    entry->npctype = npctype;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_NPCTYPE, ToriAuxLibCore_NpctypeSizeOf(npctype));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Npctype*
ToriAuxLibCore_NpctypeGet(
    struct ToriAuxLibCore* gamecache,
    int npc_id)
{
    if( !gamecache || !gamecache->npctype_hmap )
        return NULL;

    struct MapEntry_Npctype* entry = (struct MapEntry_Npctype*)ToriDraw_MapSearch(
        gamecache->npctype_hmap, &npc_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->npctype;
}

bool
ToriAuxLibCore_NpctypeHas(
    struct ToriAuxLibCore* gamecache,
    int npc_id)
{
    return ToriAuxLibCore_NpctypeGet(gamecache, npc_id) != NULL;
}

void
ToriAuxLibCore_ObjtypeAdd(
    struct ToriAuxLibCore* gamecache,
    int obj_id,
    struct ToriAuxLibCore_Objtype* objtype)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->objtype_hmap,
        sizeof(struct MapEntry_Objtype),
        GAMECACHE_CAP_OBJTYPE);
    if( !map )
        return;

    struct MapEntry_Objtype* entry =
        (struct MapEntry_Objtype*)ToriDraw_MapSearch(map, &obj_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->objtype )
    {
        gamecache_mem_track_sub(
            gamecache, TORIAUXLIBCORE_KIND_OBJTYPE, ToriAuxLibCore_ObjtypeSizeOf(entry->objtype));
        ToriAuxLibCore_ObjtypeFree(entry->objtype);
    }

    entry->id = obj_id;
    entry->objtype = objtype;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_OBJTYPE, ToriAuxLibCore_ObjtypeSizeOf(objtype));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Objtype*
ToriAuxLibCore_ObjtypeGet(
    struct ToriAuxLibCore* gamecache,
    int obj_id)
{
    if( !gamecache || !gamecache->objtype_hmap )
        return NULL;

    struct MapEntry_Objtype* entry = (struct MapEntry_Objtype*)ToriDraw_MapSearch(
        gamecache->objtype_hmap, &obj_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->objtype;
}

bool
ToriAuxLibCore_ObjtypeHas(
    struct ToriAuxLibCore* gamecache,
    int obj_id)
{
    return ToriAuxLibCore_ObjtypeGet(gamecache, obj_id) != NULL;
}

void
ToriAuxLibCore_SequenceAdd(
    struct ToriAuxLibCore* gamecache,
    int seq_id,
    struct ToriAuxLibCore_Sequence* seq)
{
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->sequences_hmap,
        sizeof(struct MapEntry_Sequence),
        GAMECACHE_CAP_SEQUENCE);
    if( !map )
        return;

    struct MapEntry_Sequence* entry =
        (struct MapEntry_Sequence*)ToriDraw_MapSearch(map, &seq_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->sequence )
    {
        gamecache_mem_track_sub(
            gamecache, TORIAUXLIBCORE_KIND_SEQUENCE, ToriAuxLibCore_SequenceSizeOf(entry->sequence));
        ToriAuxLibCore_SequenceFree(entry->sequence);
    }

    entry->id = seq_id;
    entry->sequence = seq;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_SEQUENCE, ToriAuxLibCore_SequenceSizeOf(seq));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Sequence*
ToriAuxLibCore_SequenceGet(
    struct ToriAuxLibCore* gamecache,
    int seq_id)
{
    if( !gamecache || !gamecache->sequences_hmap )
        return NULL;

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
    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->animations_hmap,
        sizeof(struct MapEntry_Animation),
        GAMECACHE_CAP_ANIMATIONS);
    if( !map )
        return;

    struct MapEntry_Animation* entry =
        (struct MapEntry_Animation*)ToriDraw_MapSearch(map, &anim_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    if( entry->animation )
    {
        gamecache_mem_track_sub(
            gamecache,
            TORIAUXLIBCORE_KIND_ANIMATION,
            ToriAuxLibCore_AnimationSizeOf(entry->animation));
        ToriAuxLibCore_AnimationFree(entry->animation);
    }

    entry->id = anim_id;
    entry->animation = animation;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_ANIMATION, ToriAuxLibCore_AnimationSizeOf(animation));

    if( animation && animation->frames )
    {
        for( int i = 0; i < animation->frame_count; i++ )
            gamecache_animframe_ref_add(gamecache, animation->frames[i].id, anim_id, i);
    }
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_Animation*
ToriAuxLibCore_AnimationGet(
    struct ToriAuxLibCore* gamecache,
    int anim_id)
{
    if( !gamecache || !gamecache->animations_hmap )
        return NULL;

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

    size_t const before = ToriAuxLibCore_SequenceSizeOf(seq);
    seq->resolved = resolved;
    gamecache_mem_track_add(
        gamecache,
        TORIAUXLIBCORE_KIND_SEQUENCE,
        ToriAuxLibCore_SequenceSizeOf(seq) - before);
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

    if( !gamecache->animframes_reftable )
        return false;

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

void
ToriAuxLibCore_SkeletalAnimAdd(
    struct ToriAuxLibCore* gamecache,
    int anim_maya_id,
    struct ToriAuxLibCore_SkeletalAnim* skeletal)
{
    if( !gamecache || !skeletal )
        return;

    struct ToriDraw_Map* map = gamecache_map_ptr(
        gamecache,
        &gamecache->skeletal_hmap,
        sizeof(struct MapEntry_SkeletalAnim),
        GAMECACHE_CAP_SKELETAL);
    if( !map )
        return;

    struct MapEntry_SkeletalAnim* entry = (struct MapEntry_SkeletalAnim*)ToriDraw_MapSearch(
        map, &anim_maya_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;
    if( entry->skeletal )
    {
        gamecache_mem_track_sub(
            gamecache,
            TORIAUXLIBCORE_KIND_SKELETAL,
            ToriAuxLibCore_SkeletalAnimSizeOf(entry->skeletal));
        ToriAuxLibCore_SkeletalAnimFree(entry->skeletal);
    }
    entry->id = anim_maya_id;
    entry->skeletal = skeletal;
    gamecache_mem_track_add(
        gamecache, TORIAUXLIBCORE_KIND_SKELETAL, ToriAuxLibCore_SkeletalAnimSizeOf(skeletal));
    gamecache_maybe_grow_hmap(gamecache, map);
}

struct ToriAuxLibCore_SkeletalAnim*
ToriAuxLibCore_SkeletalAnimGet(
    struct ToriAuxLibCore* gamecache,
    int anim_maya_id)
{
    if( !gamecache || !gamecache->skeletal_hmap )
        return NULL;

    struct MapEntry_SkeletalAnim* entry = (struct MapEntry_SkeletalAnim*)ToriDraw_MapSearch(
        gamecache->skeletal_hmap, &anim_maya_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->skeletal;
}

bool
ToriAuxLibCore_SkeletalAnimHas(
    struct ToriAuxLibCore* gamecache,
    int anim_maya_id)
{
    return ToriAuxLibCore_SkeletalAnimGet(gamecache, anim_maya_id) != NULL;
}

void
ToriAuxLibCore_MemBytes(
    struct ToriAuxLibCore* gamecache,
    struct ToriAuxLibCore_MemReport* out)
{
    if( !out )
        return;
    memset(out, 0, sizeof(*out));
    if( !gamecache )
        return;

    out->map_buffer_bytes = gamecache->map_buffer_bytes;
    for( int i = 0; i < TORIAUXLIBCORE_KIND_COUNT; i++ )
        out->asset_bytes[i] = gamecache->asset_bytes[i];

    for( int i = 0; i < TORIAUXLIBCORE_KIND_COUNT; i++ )
        out->asset_bytes_total += gamecache->asset_bytes[i];
    out->bytes_total = out->asset_bytes_total + out->map_buffer_bytes;
}

size_t
ToriAuxLibCore_BytesTotal(struct ToriAuxLibCore* gamecache)
{
    struct ToriAuxLibCore_MemReport report;
    ToriAuxLibCore_MemBytes(gamecache, &report);
    return report.bytes_total;
}
