#include "engine/cache_provider.h"

#include "cs2vm2/cs2vm2_script.h"

#include <assert.h>
#include <stdlib.h>

#define CACHE_PROVIDER_MODEL_CAPACITY 8192
#define CACHE_PROVIDER_COMPONENTPACK_CAPACITY 512
#define CACHE_PROVIDER_CLIENTSCRIPT_CAPACITY 4096
#define CACHE_PROVIDER_OBJTYPE_CAPACITY 4096
#define CACHE_PROVIDER_NPCTYPE_CAPACITY 4096
#define CACHE_PROVIDER_IDK_CAPACITY 512

struct MapEntry_ProviderModel
{
    int id;
    struct ToriRS_Model* model;
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
    provider->sprite_cache = NULL;
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
}

void
CacheProvider_FreeEngineCaches(struct CacheProvider* provider)
{
    assert(provider);

    CacheProvider_ModelsCleanup(provider);
    CacheProvider_ComponentPacksCleanup(provider);
    CacheProvider_ClientScriptsCleanup(provider);
    CacheProvider_ObjtypesCleanup(provider);
    CacheProvider_NpctypesCleanup(provider);
    CacheProvider_IdksCleanup(provider);

    cache_provider_hmap_free(provider->model_cache);
    provider->model_cache = NULL;
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
    cache_provider_hmap_free(provider->sprite_cache);
    provider->sprite_cache = NULL;
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
