#include "engine/cache_provider.h"

#include "cs2vm2/cs2vm2_script.h"

#include <assert.h>
#include <stdlib.h>

#define CACHE_PROVIDER_MODEL_CAPACITY 8192
#define CACHE_PROVIDER_SPRITE_CAPACITY 4096
#define CACHE_PROVIDER_FONT_CAPACITY 256
#define CACHE_PROVIDER_ENUM_CAPACITY 2048
#define CACHE_PROVIDER_STRUCT_CAPACITY 2048
#define CACHE_PROVIDER_COMPONENTPACK_CAPACITY 512
#define CACHE_PROVIDER_CLIENTSCRIPT_CAPACITY 4096
#define CACHE_PROVIDER_OBJTYPE_CAPACITY 4096
#define CACHE_PROVIDER_NPCTYPE_CAPACITY 4096
#define CACHE_PROVIDER_IDK_CAPACITY 512
#define CACHE_PROVIDER_MAP_TERRAIN_CAPACITY 512
#define CACHE_PROVIDER_MAP_SCENERY_CAPACITY 512
#define CACHE_PROVIDER_LOCATION_CAPACITY 4096
#define CACHE_PROVIDER_FLOTYPE_CAPACITY 512
#define CACHE_PROVIDER_UNDERLAY_CAPACITY 512
#define CACHE_PROVIDER_TEXTURE_CAPACITY 512

struct MapEntry_ProviderModel
{
    int id;
    struct ToriRS_Model* model;
};

struct MapEntry_ProviderSprite
{
    int id;
    struct ToriRS_Sprite* sprite;
};

struct MapEntry_ProviderFont
{
    int id;
    struct ToriRS_Font* font;
};

struct MapEntry_ProviderEnum
{
    int id;
    struct ToriRS_Enum* e;
};

struct MapEntry_ProviderStruct
{
    int id;
    struct ToriRS_Struct* s;
};

struct MapEntry_ProviderComponentPack
{
    int id;
    struct ToriRS_ComponentPack* pack;
};

struct MapEntry_ProviderClientScript
{
    int id;
    struct CS2VM2_Script* script;
};

struct MapEntry_ProviderObjtype
{
    int id;
    struct ToriRS_Objtype* objtype;
};

struct MapEntry_ProviderNpctype
{
    int id;
    struct ToriRS_Npctype* npctype;
};

struct MapEntry_ProviderIdk
{
    int id;
    struct ToriRS_Idk* idk;
};

struct MapEntry_ProviderMapTerrain
{
    int id;
    struct ToriRS_MapTerrain* terrain;
};

struct MapEntry_ProviderMapScenery
{
    int id;
    struct ToriRS_MapLocs* locs;
};

struct MapEntry_ProviderLocation
{
    int id;
    struct ToriRS_Location* location;
};

struct MapEntry_ProviderFlotype
{
    int id;
    struct ToriRS_Flotype* flotype;
};

struct MapEntry_ProviderTexture
{
    int id;
    struct ToriRS_Texture* texture;
};

static size_t
cache_provider_hmap_buffer_bytes(
    size_t entry_size,
    size_t capacity)
{
    const size_t align = 16;
    size_t entry_offset = align;
    size_t raw_stride = entry_offset + entry_size;
    size_t stride = (raw_stride + align - 1) & ~(align - 1);
    return align + stride * capacity;
}

static struct HMap*
cache_provider_hmap_new(
    size_t entry_size,
    size_t capacity)
{
    size_t buffer_size = cache_provider_hmap_buffer_bytes(entry_size, capacity);
    void* buffer = malloc(buffer_size);
    assert(buffer);

    struct HashConfig config = {
        .key_size = sizeof(int),
        .entry_size = entry_size,
        .buffer = buffer,
        .buffer_size = buffer_size,
        .capacity = capacity,
    };
    struct HMap* map = hmap_new(&config, 0);
    assert(map);
    return map;
}

static void
cache_provider_hmap_free(struct HMap* map)
{
    if( !map )
        return;
    free(hmap_free(map));
}

static void
cache_provider_hmap_maybe_grow(struct HMap** map_out)
{
    struct HMap* map;
    size_t new_capacity;
    size_t buffer_size;
    void* new_buffer;
    void* old_buffer;

    assert(map_out);
    map = *map_out;
    assert(map);

    if( map->size * 4 <= map->capacity * 3 )
        return;

    new_capacity = map->capacity * 2;
    buffer_size = cache_provider_hmap_buffer_bytes(map->entry_size, new_capacity);
    new_buffer = malloc(buffer_size);
    assert(new_buffer);

    if( hmap_resize(*map_out, new_buffer, buffer_size, new_capacity, &old_buffer) != HMAP_OK )
    {
        free(new_buffer);
        return;
    }

    free(old_buffer);
}

static void
cache_provider_hmap_prepare_insert(struct HMap** map_out)
{
    assert(map_out);
    assert(*map_out);
    if( (*map_out)->capacity > 0 && (*map_out)->size * 4 >= (*map_out)->capacity * 3 )
        cache_provider_hmap_maybe_grow(map_out);
}

void
CacheProvider_InitEngineCaches(struct CacheProvider* provider)
{
    assert(provider);

    provider->model_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderModel), CACHE_PROVIDER_MODEL_CAPACITY);
    provider->sprite_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderSprite), CACHE_PROVIDER_SPRITE_CAPACITY);
    provider->font_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderFont), CACHE_PROVIDER_FONT_CAPACITY);
    provider->enum_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderEnum), CACHE_PROVIDER_ENUM_CAPACITY);
    provider->struct_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderStruct), CACHE_PROVIDER_STRUCT_CAPACITY);
    provider->componentpack_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderComponentPack), CACHE_PROVIDER_COMPONENTPACK_CAPACITY);
    provider->clientscript_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderClientScript), CACHE_PROVIDER_CLIENTSCRIPT_CAPACITY);
    provider->objtype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderObjtype), CACHE_PROVIDER_OBJTYPE_CAPACITY);
    provider->npctype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderNpctype), CACHE_PROVIDER_NPCTYPE_CAPACITY);
    provider->idk_cache =
        cache_provider_hmap_new(sizeof(struct MapEntry_ProviderIdk), CACHE_PROVIDER_IDK_CAPACITY);
    provider->map_terrain_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderMapTerrain), CACHE_PROVIDER_MAP_TERRAIN_CAPACITY);
    provider->map_scenery_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderMapScenery), CACHE_PROVIDER_MAP_SCENERY_CAPACITY);
    provider->location_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderLocation), CACHE_PROVIDER_LOCATION_CAPACITY);
    provider->flotype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderFlotype), CACHE_PROVIDER_FLOTYPE_CAPACITY);
    provider->underlay_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderFlotype), CACHE_PROVIDER_UNDERLAY_CAPACITY);
    provider->texture_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderTexture), CACHE_PROVIDER_TEXTURE_CAPACITY);
}

void
CacheProvider_FreeEngineCaches(struct CacheProvider* provider)
{
    assert(provider);

    CacheProvider_ModelsCleanup(provider);
    CacheProvider_SpritesCleanup(provider);
    CacheProvider_FontsCleanup(provider);
    CacheProvider_EnumsCleanup(provider);
    CacheProvider_StructsCleanup(provider);
    CacheProvider_ComponentPacksCleanup(provider);
    CacheProvider_ClientScriptsCleanup(provider);
    CacheProvider_ObjtypesCleanup(provider);
    CacheProvider_NpctypesCleanup(provider);
    CacheProvider_IdksCleanup(provider);
    CacheProvider_MapTerrainsCleanup(provider);
    CacheProvider_MapSceneryCleanup(provider);
    CacheProvider_LocationsCleanup(provider);
    CacheProvider_FlotypesCleanup(provider);
    CacheProvider_UnderlaysCleanup(provider);
    CacheProvider_TexturesCleanup(provider);

    cache_provider_hmap_free(provider->model_cache);
    provider->model_cache = NULL;
    cache_provider_hmap_free(provider->sprite_cache);
    provider->sprite_cache = NULL;
    cache_provider_hmap_free(provider->font_cache);
    provider->font_cache = NULL;
    cache_provider_hmap_free(provider->enum_cache);
    provider->enum_cache = NULL;
    cache_provider_hmap_free(provider->struct_cache);
    provider->struct_cache = NULL;
    cache_provider_hmap_free(provider->componentpack_cache);
    provider->componentpack_cache = NULL;
    cache_provider_hmap_free(provider->clientscript_cache);
    provider->clientscript_cache = NULL;
    cache_provider_hmap_free(provider->objtype_cache);
    provider->objtype_cache = NULL;
    cache_provider_hmap_free(provider->npctype_cache);
    provider->npctype_cache = NULL;
    cache_provider_hmap_free(provider->idk_cache);
    provider->idk_cache = NULL;
    cache_provider_hmap_free(provider->map_terrain_cache);
    provider->map_terrain_cache = NULL;
    cache_provider_hmap_free(provider->map_scenery_cache);
    provider->map_scenery_cache = NULL;
    cache_provider_hmap_free(provider->location_cache);
    provider->location_cache = NULL;
    cache_provider_hmap_free(provider->flotype_cache);
    provider->flotype_cache = NULL;
    cache_provider_hmap_free(provider->underlay_cache);
    provider->underlay_cache = NULL;
    cache_provider_hmap_free(provider->texture_cache);
    provider->texture_cache = NULL;
}

void
CacheProvider_ModelAdd(
    struct CacheProvider* provider,
    int model_id,
    struct ToriRS_Model* model)
{
    struct MapEntry_ProviderModel* entry;

    assert(provider);
    assert(model);

    cache_provider_hmap_prepare_insert(&provider->model_cache);
    entry = (struct MapEntry_ProviderModel*)hmap_search(
        provider->model_cache, &model_id, HMAP_INSERT);
    assert(entry && "Model must be inserted into hmap");

    entry->id = model_id;
    entry->model = model;
}

struct ToriRS_Model*
CacheProvider_ModelGet(
    struct CacheProvider* provider,
    int model_id)
{
    struct MapEntry_ProviderModel* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderModel*)hmap_search(
        provider->model_cache, &model_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->model;
}

bool
CacheProvider_ModelHas(
    struct CacheProvider* provider,
    int model_id)
{
    return CacheProvider_ModelGet(provider, model_id) != NULL;
}

void
CacheProvider_ModelsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderModel* entry;

    assert(provider);
    if( !provider->model_cache )
        return;

    iter = hmap_iter_new(provider->model_cache);
    while( (entry = (struct MapEntry_ProviderModel*)hmap_iter_next(iter)) )
    {
        if( entry->model )
            ToriRS_ModelFree(entry->model);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->model_cache);
    provider->model_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderModel), CACHE_PROVIDER_MODEL_CAPACITY);
}

void
CacheProvider_SpriteAdd(
    struct CacheProvider* provider,
    int sprite_id,
    struct ToriRS_Sprite* sprite)
{
    struct MapEntry_ProviderSprite* entry;

    assert(provider);
    assert(sprite);

    cache_provider_hmap_prepare_insert(&provider->sprite_cache);
    entry = (struct MapEntry_ProviderSprite*)hmap_search(
        provider->sprite_cache, &sprite_id, HMAP_INSERT);
    assert(entry && "Sprite must be inserted into hmap");

    entry->id = sprite_id;
    entry->sprite = sprite;
}

struct ToriRS_Sprite*
CacheProvider_SpriteGet(
    struct CacheProvider* provider,
    int sprite_id)
{
    struct MapEntry_ProviderSprite* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderSprite*)hmap_search(
        provider->sprite_cache, &sprite_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->sprite;
}

bool
CacheProvider_SpriteHas(
    struct CacheProvider* provider,
    int sprite_id)
{
    return CacheProvider_SpriteGet(provider, sprite_id) != NULL;
}

void
CacheProvider_SpritesCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderSprite* entry;

    assert(provider);
    if( !provider->sprite_cache )
        return;

    iter = hmap_iter_new(provider->sprite_cache);
    while( (entry = (struct MapEntry_ProviderSprite*)hmap_iter_next(iter)) )
    {
        if( entry->sprite )
            ToriRS_SpriteFree(entry->sprite);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->sprite_cache);
    provider->sprite_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderSprite), CACHE_PROVIDER_SPRITE_CAPACITY);
}

void
CacheProvider_FontAdd(
    struct CacheProvider* provider,
    int font_id,
    struct ToriRS_Font* font)
{
    struct MapEntry_ProviderFont* entry;

    assert(provider);
    assert(font);

    cache_provider_hmap_prepare_insert(&provider->font_cache);
    entry =
        (struct MapEntry_ProviderFont*)hmap_search(provider->font_cache, &font_id, HMAP_INSERT);
    assert(entry && "Font must be inserted into hmap");

    entry->id = font_id;
    entry->font = font;
}

struct ToriRS_Font*
CacheProvider_FontGet(
    struct CacheProvider* provider,
    int font_id)
{
    struct MapEntry_ProviderFont* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderFont*)hmap_search(provider->font_cache, &font_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->font;
}

bool
CacheProvider_FontHas(
    struct CacheProvider* provider,
    int font_id)
{
    return CacheProvider_FontGet(provider, font_id) != NULL;
}

void
CacheProvider_FontsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderFont* entry;

    assert(provider);
    if( !provider->font_cache )
        return;

    iter = hmap_iter_new(provider->font_cache);
    while( (entry = (struct MapEntry_ProviderFont*)hmap_iter_next(iter)) )
    {
        if( entry->font )
            ToriRS_FontFree(entry->font);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->font_cache);
    provider->font_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderFont), CACHE_PROVIDER_FONT_CAPACITY);
}

void
CacheProvider_EnumAdd(
    struct CacheProvider* provider,
    int enum_id,
    struct ToriRS_Enum* e)
{
    struct MapEntry_ProviderEnum* entry;

    assert(provider);
    assert(e);

    cache_provider_hmap_prepare_insert(&provider->enum_cache);
    entry =
        (struct MapEntry_ProviderEnum*)hmap_search(provider->enum_cache, &enum_id, HMAP_INSERT);
    assert(entry && "Enum must be inserted into hmap");

    entry->id = enum_id;
    entry->e = e;
}

struct ToriRS_Enum*
CacheProvider_EnumGet(
    struct CacheProvider* provider,
    int enum_id)
{
    struct MapEntry_ProviderEnum* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderEnum*)hmap_search(provider->enum_cache, &enum_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->e;
}

bool
CacheProvider_EnumHas(
    struct CacheProvider* provider,
    int enum_id)
{
    return CacheProvider_EnumGet(provider, enum_id) != NULL;
}

void
CacheProvider_EnumsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderEnum* entry;

    assert(provider);
    if( !provider->enum_cache )
        return;

    iter = hmap_iter_new(provider->enum_cache);
    while( (entry = (struct MapEntry_ProviderEnum*)hmap_iter_next(iter)) )
    {
        if( entry->e )
            ToriRS_EnumFree(entry->e);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->enum_cache);
    provider->enum_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderEnum), CACHE_PROVIDER_ENUM_CAPACITY);
}

void
CacheProvider_StructAdd(
    struct CacheProvider* provider,
    int struct_id,
    struct ToriRS_Struct* s)
{
    struct MapEntry_ProviderStruct* entry;

    assert(provider);
    assert(s);

    cache_provider_hmap_prepare_insert(&provider->struct_cache);
    entry = (struct MapEntry_ProviderStruct*)hmap_search(
        provider->struct_cache, &struct_id, HMAP_INSERT);
    assert(entry && "Struct must be inserted into hmap");

    entry->id = struct_id;
    entry->s = s;
}

struct ToriRS_Struct*
CacheProvider_StructGet(
    struct CacheProvider* provider,
    int struct_id)
{
    struct MapEntry_ProviderStruct* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderStruct*)hmap_search(
        provider->struct_cache, &struct_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->s;
}

bool
CacheProvider_StructHas(
    struct CacheProvider* provider,
    int struct_id)
{
    return CacheProvider_StructGet(provider, struct_id) != NULL;
}

void
CacheProvider_StructsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderStruct* entry;

    assert(provider);
    if( !provider->struct_cache )
        return;

    iter = hmap_iter_new(provider->struct_cache);
    while( (entry = (struct MapEntry_ProviderStruct*)hmap_iter_next(iter)) )
    {
        if( entry->s )
            ToriRS_StructFree(entry->s);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->struct_cache);
    provider->struct_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderStruct), CACHE_PROVIDER_STRUCT_CAPACITY);
}

void
CacheProvider_ComponentPackAdd(
    struct CacheProvider* provider,
    int iface_id,
    struct ToriRS_ComponentPack* pack)
{
    struct MapEntry_ProviderComponentPack* entry;

    assert(provider);
    assert(pack);

    cache_provider_hmap_prepare_insert(&provider->componentpack_cache);
    entry = (struct MapEntry_ProviderComponentPack*)hmap_search(
        provider->componentpack_cache, &iface_id, HMAP_INSERT);
    assert(entry && "Component pack must be inserted into hmap");

    entry->id = iface_id;
    entry->pack = pack;
}

struct ToriRS_ComponentPack*
CacheProvider_ComponentPackGet(
    struct CacheProvider* provider,
    int iface_id)
{
    struct MapEntry_ProviderComponentPack* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderComponentPack*)hmap_search(
        provider->componentpack_cache, &iface_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->pack;
}

bool
CacheProvider_ComponentPackHas(
    struct CacheProvider* provider,
    int iface_id)
{
    return CacheProvider_ComponentPackGet(provider, iface_id) != NULL;
}

void
CacheProvider_ComponentPacksCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderComponentPack* entry;

    assert(provider);
    if( !provider->componentpack_cache )
        return;

    iter = hmap_iter_new(provider->componentpack_cache);
    while( (entry = (struct MapEntry_ProviderComponentPack*)hmap_iter_next(iter)) )
    {
        if( entry->pack )
            ToriRS_ComponentPackFree(entry->pack);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->componentpack_cache);
    provider->componentpack_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderComponentPack), CACHE_PROVIDER_COMPONENTPACK_CAPACITY);
}

struct ToriRS_Component*
CacheProvider_ComponentGet(
    struct CacheProvider* provider,
    int packed_id)
{
    int iface;
    int child;
    struct ToriRS_ComponentPack* pack;
    int i;

    assert(provider);

    iface = packed_id >> 16;
    child = packed_id & 0xFFFF;
    pack = CacheProvider_ComponentPackGet(provider, iface);
    if( !pack )
        return NULL;

    if( child >= 0 && child < pack->component_count )
    {
        struct ToriRS_Component* by_index = &pack->components[child];
        if( by_index->id == packed_id )
            return by_index;
    }

    for( i = 0; i < pack->component_count; i++ )
    {
        if( pack->components[i].id == packed_id )
            return &pack->components[i];
    }

    /* Fallback when id encoding differs: treat low 16 bits as child index. */
    if( child >= 0 && child < pack->component_count )
        return &pack->components[child];

    return NULL;
}

bool
CacheProvider_ComponentHas(
    struct CacheProvider* provider,
    int packed_id)
{
    return CacheProvider_ComponentGet(provider, packed_id) != NULL;
}

bool
CacheProvider_ComponentPackHasForComponent(
    struct CacheProvider* provider,
    int packed_id)
{
    assert(provider);
    return CacheProvider_ComponentPackHas(provider, packed_id >> 16);
}

void
CacheProvider_ClientScriptAdd(
    struct CacheProvider* provider,
    int script_id,
    struct CS2VM2_Script* script)
{
    struct MapEntry_ProviderClientScript* entry;

    assert(provider);
    assert(script);

    cache_provider_hmap_prepare_insert(&provider->clientscript_cache);
    entry = (struct MapEntry_ProviderClientScript*)hmap_search(
        provider->clientscript_cache, &script_id, HMAP_INSERT);
    assert(entry && "Client script must be inserted into hmap");

    entry->id = script_id;
    entry->script = script;
}

struct CS2VM2_Script*
CacheProvider_ClientScriptGet(
    struct CacheProvider* provider,
    int script_id)
{
    struct MapEntry_ProviderClientScript* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderClientScript*)hmap_search(
        provider->clientscript_cache, &script_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->script;
}

bool
CacheProvider_ClientScriptHas(
    struct CacheProvider* provider,
    int script_id)
{
    return CacheProvider_ClientScriptGet(provider, script_id) != NULL;
}

void
CacheProvider_ClientScriptsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderClientScript* entry;

    assert(provider);
    if( !provider->clientscript_cache )
        return;

    iter = hmap_iter_new(provider->clientscript_cache);
    while( (entry = (struct MapEntry_ProviderClientScript*)hmap_iter_next(iter)) )
    {
        if( entry->script )
            CS2VM2_ScriptFree(entry->script);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->clientscript_cache);
    provider->clientscript_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderClientScript), CACHE_PROVIDER_CLIENTSCRIPT_CAPACITY);
}

void
CacheProvider_ObjtypeAdd(
    struct CacheProvider* provider,
    int obj_id,
    struct ToriRS_Objtype* objtype)
{
    struct MapEntry_ProviderObjtype* entry;

    assert(provider);
    assert(objtype);

    cache_provider_hmap_prepare_insert(&provider->objtype_cache);
    entry = (struct MapEntry_ProviderObjtype*)hmap_search(
        provider->objtype_cache, &obj_id, HMAP_INSERT);
    assert(entry && "Objtype must be inserted into hmap");

    entry->id = obj_id;
    entry->objtype = objtype;
}

struct ToriRS_Objtype*
CacheProvider_ObjtypeGet(
    struct CacheProvider* provider,
    int obj_id)
{
    struct MapEntry_ProviderObjtype* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderObjtype*)hmap_search(
        provider->objtype_cache, &obj_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->objtype;
}

bool
CacheProvider_ObjtypeHas(
    struct CacheProvider* provider,
    int obj_id)
{
    return CacheProvider_ObjtypeGet(provider, obj_id) != NULL;
}

void
CacheProvider_ObjtypesCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderObjtype* entry;

    assert(provider);
    if( !provider->objtype_cache )
        return;

    iter = hmap_iter_new(provider->objtype_cache);
    while( (entry = (struct MapEntry_ProviderObjtype*)hmap_iter_next(iter)) )
    {
        if( entry->objtype )
            ToriRS_ObjtypeFree(entry->objtype);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->objtype_cache);
    provider->objtype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderObjtype), CACHE_PROVIDER_OBJTYPE_CAPACITY);
}

void
CacheProvider_NpctypeAdd(
    struct CacheProvider* provider,
    int npc_id,
    struct ToriRS_Npctype* npctype)
{
    struct MapEntry_ProviderNpctype* entry;

    assert(provider);
    assert(npctype);

    cache_provider_hmap_prepare_insert(&provider->npctype_cache);
    entry = (struct MapEntry_ProviderNpctype*)hmap_search(
        provider->npctype_cache, &npc_id, HMAP_INSERT);
    assert(entry && "Npctype must be inserted into hmap");

    entry->id = npc_id;
    entry->npctype = npctype;
}

struct ToriRS_Npctype*
CacheProvider_NpctypeGet(
    struct CacheProvider* provider,
    int npc_id)
{
    struct MapEntry_ProviderNpctype* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderNpctype*)hmap_search(
        provider->npctype_cache, &npc_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->npctype;
}

bool
CacheProvider_NpctypeHas(
    struct CacheProvider* provider,
    int npc_id)
{
    return CacheProvider_NpctypeGet(provider, npc_id) != NULL;
}

void
CacheProvider_NpctypesCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderNpctype* entry;

    assert(provider);
    if( !provider->npctype_cache )
        return;

    iter = hmap_iter_new(provider->npctype_cache);
    while( (entry = (struct MapEntry_ProviderNpctype*)hmap_iter_next(iter)) )
    {
        if( entry->npctype )
            ToriRS_NpctypeFree(entry->npctype);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->npctype_cache);
    provider->npctype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderNpctype), CACHE_PROVIDER_NPCTYPE_CAPACITY);
}

void
CacheProvider_IdkAdd(
    struct CacheProvider* provider,
    int idk_id,
    struct ToriRS_Idk* idk)
{
    struct MapEntry_ProviderIdk* entry;

    assert(provider);
    assert(idk);

    cache_provider_hmap_prepare_insert(&provider->idk_cache);
    entry = (struct MapEntry_ProviderIdk*)hmap_search(provider->idk_cache, &idk_id, HMAP_INSERT);
    assert(entry && "Idk must be inserted into hmap");

    entry->id = idk_id;
    entry->idk = idk;
}

struct ToriRS_Idk*
CacheProvider_IdkGet(
    struct CacheProvider* provider,
    int idk_id)
{
    struct MapEntry_ProviderIdk* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderIdk*)hmap_search(provider->idk_cache, &idk_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->idk;
}

bool
CacheProvider_IdkHas(
    struct CacheProvider* provider,
    int idk_id)
{
    return CacheProvider_IdkGet(provider, idk_id) != NULL;
}

void
CacheProvider_IdksCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderIdk* entry;

    assert(provider);
    if( !provider->idk_cache )
        return;

    iter = hmap_iter_new(provider->idk_cache);
    while( (entry = (struct MapEntry_ProviderIdk*)hmap_iter_next(iter)) )
    {
        if( entry->idk )
            ToriRS_IdkFree(entry->idk);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->idk_cache);
    provider->idk_cache =
        cache_provider_hmap_new(sizeof(struct MapEntry_ProviderIdk), CACHE_PROVIDER_IDK_CAPACITY);
}

void
CacheProvider_MapTerrainAdd(
    struct CacheProvider* provider,
    int map_id,
    struct ToriRS_MapTerrain* terrain)
{
    struct MapEntry_ProviderMapTerrain* entry;

    assert(provider);
    assert(terrain);

    cache_provider_hmap_prepare_insert(&provider->map_terrain_cache);
    entry = (struct MapEntry_ProviderMapTerrain*)hmap_search(
        provider->map_terrain_cache, &map_id, HMAP_INSERT);
    assert(entry && "Map terrain must be inserted into hmap");

    entry->id = map_id;
    entry->terrain = terrain;
}

struct ToriRS_MapTerrain*
CacheProvider_MapTerrainGet(
    struct CacheProvider* provider,
    int map_id)
{
    struct MapEntry_ProviderMapTerrain* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderMapTerrain*)hmap_search(
        provider->map_terrain_cache, &map_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->terrain;
}

bool
CacheProvider_MapTerrainHas(
    struct CacheProvider* provider,
    int map_id)
{
    return CacheProvider_MapTerrainGet(provider, map_id) != NULL;
}

void
CacheProvider_MapTerrainsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderMapTerrain* entry;

    assert(provider);
    if( !provider->map_terrain_cache )
        return;

    iter = hmap_iter_new(provider->map_terrain_cache);
    while( (entry = (struct MapEntry_ProviderMapTerrain*)hmap_iter_next(iter)) )
    {
        if( entry->terrain )
            ToriRS_MapTerrainFree(entry->terrain);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->map_terrain_cache);
    provider->map_terrain_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderMapTerrain), CACHE_PROVIDER_MAP_TERRAIN_CAPACITY);
}

void
CacheProvider_MapSceneryAdd(
    struct CacheProvider* provider,
    int map_id,
    struct ToriRS_MapLocs* locs)
{
    struct MapEntry_ProviderMapScenery* entry;

    assert(provider);
    assert(locs);

    cache_provider_hmap_prepare_insert(&provider->map_scenery_cache);
    entry = (struct MapEntry_ProviderMapScenery*)hmap_search(
        provider->map_scenery_cache, &map_id, HMAP_INSERT);
    assert(entry && "Map scenery must be inserted into hmap");

    entry->id = map_id;
    entry->locs = locs;
}

struct ToriRS_MapLocs*
CacheProvider_MapSceneryGet(
    struct CacheProvider* provider,
    int map_id)
{
    struct MapEntry_ProviderMapScenery* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderMapScenery*)hmap_search(
        provider->map_scenery_cache, &map_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->locs;
}

bool
CacheProvider_MapSceneryHas(
    struct CacheProvider* provider,
    int map_id)
{
    return CacheProvider_MapSceneryGet(provider, map_id) != NULL;
}

void
CacheProvider_MapSceneryCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderMapScenery* entry;

    assert(provider);
    if( !provider->map_scenery_cache )
        return;

    iter = hmap_iter_new(provider->map_scenery_cache);
    while( (entry = (struct MapEntry_ProviderMapScenery*)hmap_iter_next(iter)) )
    {
        if( entry->locs )
            ToriRS_MapLocsFree(entry->locs);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->map_scenery_cache);
    provider->map_scenery_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderMapScenery), CACHE_PROVIDER_MAP_SCENERY_CAPACITY);
}

void
CacheProvider_LocationAdd(
    struct CacheProvider* provider,
    int loc_id,
    struct ToriRS_Location* location)
{
    struct MapEntry_ProviderLocation* entry;

    assert(provider);
    assert(location);

    cache_provider_hmap_prepare_insert(&provider->location_cache);
    entry = (struct MapEntry_ProviderLocation*)hmap_search(
        provider->location_cache, &loc_id, HMAP_INSERT);
    assert(entry && "Location must be inserted into hmap");

    entry->id = loc_id;
    entry->location = location;
}

struct ToriRS_Location*
CacheProvider_LocationGet(
    struct CacheProvider* provider,
    int loc_id)
{
    struct MapEntry_ProviderLocation* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderLocation*)hmap_search(
        provider->location_cache, &loc_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->location;
}

bool
CacheProvider_LocationHas(
    struct CacheProvider* provider,
    int loc_id)
{
    return CacheProvider_LocationGet(provider, loc_id) != NULL;
}

void
CacheProvider_LocationsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderLocation* entry;

    assert(provider);
    if( !provider->location_cache )
        return;

    iter = hmap_iter_new(provider->location_cache);
    while( (entry = (struct MapEntry_ProviderLocation*)hmap_iter_next(iter)) )
    {
        if( entry->location )
            ToriRS_LocationFree(entry->location);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->location_cache);
    provider->location_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderLocation), CACHE_PROVIDER_LOCATION_CAPACITY);
}

void
CacheProvider_FlotypeAdd(
    struct CacheProvider* provider,
    int flo_id,
    struct ToriRS_Flotype* flotype)
{
    struct MapEntry_ProviderFlotype* entry;

    assert(provider);
    assert(flotype);

    cache_provider_hmap_prepare_insert(&provider->flotype_cache);
    entry = (struct MapEntry_ProviderFlotype*)hmap_search(
        provider->flotype_cache, &flo_id, HMAP_INSERT);
    assert(entry && "Flotype must be inserted into hmap");

    entry->id = flo_id;
    entry->flotype = flotype;
}

struct ToriRS_Flotype*
CacheProvider_FlotypeGet(
    struct CacheProvider* provider,
    int flo_id)
{
    struct MapEntry_ProviderFlotype* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderFlotype*)hmap_search(
        provider->flotype_cache, &flo_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->flotype;
}

bool
CacheProvider_FlotypeHas(
    struct CacheProvider* provider,
    int flo_id)
{
    return CacheProvider_FlotypeGet(provider, flo_id) != NULL;
}

void
CacheProvider_FlotypesCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderFlotype* entry;

    assert(provider);
    if( !provider->flotype_cache )
        return;

    iter = hmap_iter_new(provider->flotype_cache);
    while( (entry = (struct MapEntry_ProviderFlotype*)hmap_iter_next(iter)) )
    {
        if( entry->flotype )
            ToriRS_FlotypeFree(entry->flotype);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->flotype_cache);
    provider->flotype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderFlotype), CACHE_PROVIDER_FLOTYPE_CAPACITY);
}

void
CacheProvider_UnderlayAdd(
    struct CacheProvider* provider,
    int underlay_id,
    struct ToriRS_Flotype* underlay)
{
    struct MapEntry_ProviderFlotype* entry;

    assert(provider);
    assert(underlay);

    cache_provider_hmap_prepare_insert(&provider->underlay_cache);
    entry = (struct MapEntry_ProviderFlotype*)hmap_search(
        provider->underlay_cache, &underlay_id, HMAP_INSERT);
    assert(entry && "Underlay must be inserted into hmap");

    entry->id = underlay_id;
    entry->flotype = underlay;
}

struct ToriRS_Flotype*
CacheProvider_UnderlayGet(
    struct CacheProvider* provider,
    int underlay_id)
{
    struct MapEntry_ProviderFlotype* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderFlotype*)hmap_search(
        provider->underlay_cache, &underlay_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->flotype;
}

bool
CacheProvider_UnderlayHas(
    struct CacheProvider* provider,
    int underlay_id)
{
    return CacheProvider_UnderlayGet(provider, underlay_id) != NULL;
}

void
CacheProvider_UnderlaysCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderFlotype* entry;

    assert(provider);
    if( !provider->underlay_cache )
        return;

    iter = hmap_iter_new(provider->underlay_cache);
    while( (entry = (struct MapEntry_ProviderFlotype*)hmap_iter_next(iter)) )
    {
        if( entry->flotype )
            ToriRS_FlotypeFree(entry->flotype);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->underlay_cache);
    provider->underlay_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderFlotype), CACHE_PROVIDER_UNDERLAY_CAPACITY);
}

void
CacheProvider_TextureAdd(
    struct CacheProvider* provider,
    int texture_id,
    struct ToriRS_Texture* texture)
{
    struct MapEntry_ProviderTexture* entry;

    assert(provider);
    assert(texture);

    cache_provider_hmap_prepare_insert(&provider->texture_cache);
    entry = (struct MapEntry_ProviderTexture*)hmap_search(
        provider->texture_cache, &texture_id, HMAP_INSERT);
    assert(entry && "Texture must be inserted into hmap");

    entry->id = texture_id;
    entry->texture = texture;
}

struct ToriRS_Texture*
CacheProvider_TextureGet(
    struct CacheProvider* provider,
    int texture_id)
{
    struct MapEntry_ProviderTexture* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderTexture*)hmap_search(
        provider->texture_cache, &texture_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->texture;
}

bool
CacheProvider_TextureHas(
    struct CacheProvider* provider,
    int texture_id)
{
    return CacheProvider_TextureGet(provider, texture_id) != NULL;
}

void
CacheProvider_TexturesCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderTexture* entry;

    assert(provider);
    if( !provider->texture_cache )
        return;

    iter = hmap_iter_new(provider->texture_cache);
    while( (entry = (struct MapEntry_ProviderTexture*)hmap_iter_next(iter)) )
    {
        if( entry->texture )
            ToriRS_TextureFree(entry->texture);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->texture_cache);
    provider->texture_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderTexture), CACHE_PROVIDER_TEXTURE_CAPACITY);
}
