/*
 * The assets table's loaders whose load is the cache's own task.
 * @see asset_cache_loaders.h.
 */
#include "engine/asset_cache_loaders.h"

#include "engine/asset_load_task.h"
#include "engine/cache_provider.h"
#include "engine/task_obj_model_load.h"

#include <assert.h>
#include <stddef.h>

bool
AssetCache_HandlesKind(enum AssetKind kind)
{
    return kind == ASSET_KIND_MODEL || kind == ASSET_KIND_SKYBOX || kind == ASSET_KIND_TEXTURE ||
           kind == ASSET_KIND_SPRITE || kind == ASSET_KIND_IF_GROUP || kind == ASSET_KIND_SOUND ||
           kind == ASSET_KIND_OBJ_MODEL || kind == ASSET_KIND_CONFIG ||
           kind == ASSET_KIND_MAP_TERRAIN || kind == ASSET_KIND_MAP_SCENERY;
}

/* ---- configs: one row per table ---------------------------------------- */

static bool
config_resident(
    struct CacheProvider* p,
    struct AssetKey key)
{
    int size;

    switch( key.variant )
    {
    case ASSET_CONFIG_IDK:
        return CacheProvider_IdkHas(p, key.id);
    case ASSET_CONFIG_OBJ:
        return CacheProvider_ObjtypeHas(p, key.id);
    case ASSET_CONFIG_UNDERLAY:
        return CacheProvider_UnderlayHas(p, key.id);
    case ASSET_CONFIG_FLOTYPE:
        return CacheProvider_FlotypeHas(p, key.id);
    case ASSET_CONFIG_SPOTANIM:
        return CacheProvider_SpotanimtypeHas(p, key.id);
    case ASSET_CONFIG_SCRIPT:
        return CacheProvider_ClientScriptHas(p, key.id);
    case ASSET_CONFIG_ENUM:
        return CacheProvider_EnumHas(p, key.id);
    case ASSET_CONFIG_STRUCT:
        return CacheProvider_StructHas(p, key.id);
    case ASSET_CONFIG_PARAM:
        return CacheProvider_ParamHas(p, key.id);
    case ASSET_CONFIG_INVTYPE:
        return CacheProvider_InvtypeGet(p, key.id, &size);
    case ASSET_CONFIG_NPC:
        return CacheProvider_NpctypeHas(p, key.id);
    case ASSET_CONFIG_LOC:
        return CacheProvider_LocationHas(p, key.id);
    case ASSET_CONFIG_DBROW:
        return CacheProvider_DbRowHas(p, key.id);
    case ASSET_CONFIG_DBTABLE:
        return CacheProvider_DbTableHas(p, key.id);
    case ASSET_CONFIG_DBINDEX:
        return CacheProvider_DbTableIndexHas(p, key.id);
    case ASSET_CONFIG_FONT:
        return CacheProvider_FontHas(p, key.id);
    case ASSET_CONFIG_MAPELEMENT:
        return CacheProvider_MapElementHas(p, key.id);
    case ASSET_CONFIG_WORLDMAP:
        return CacheProvider_WorldMapGet(p) != NULL;
    case ASSET_CONFIG_OBJ_ALL:
        return p->objtypes_all_loaded;
    }
    assert(0 && "not a config table");
    return false;
}

static struct ToriRS_Task*
config_load(
    struct CacheProvider* p,
    struct AssetKey key)
{
    switch( key.variant )
    {
    case ASSET_CONFIG_IDK:
        return CreateTask_IdkLoad(p, key.id);
    case ASSET_CONFIG_OBJ:
        return CreateTask_ObjLoad(p, key.id);
    case ASSET_CONFIG_UNDERLAY:
        return CreateTask_UnderlayLoad(p, key.id);
    case ASSET_CONFIG_FLOTYPE:
        return CreateTask_FlotypeLoad(p, key.id);
    case ASSET_CONFIG_SPOTANIM:
        return CreateTask_SpotanimLoad(p, key.id);
    case ASSET_CONFIG_SCRIPT:
        return CreateTask_ClientScriptLoad(p, key.id);
    case ASSET_CONFIG_ENUM:
        return CreateTask_EnumLoad(p, key.id);
    case ASSET_CONFIG_STRUCT:
        return CreateTask_StructLoad(p, key.id);
    case ASSET_CONFIG_PARAM:
        return CreateTask_ParamLoad(p, key.id);
    case ASSET_CONFIG_INVTYPE:
        return CreateTask_InvtypeLoad(p, key.id);
    case ASSET_CONFIG_NPC:
        return CreateTask_NpcLoad(p, key.id);
    case ASSET_CONFIG_LOC:
        return CreateTask_LocLoad(p, key.id);
    case ASSET_CONFIG_DBROW:
        return CreateTask_DbRowLoad(p, key.id);
    case ASSET_CONFIG_DBTABLE:
        return CreateTask_DbTableLoad(p, key.id);
    case ASSET_CONFIG_DBINDEX:
        return CreateTask_DbTableIndexLoad(p, key.id);
    case ASSET_CONFIG_FONT:
        return CreateTask_FontLoad(p, key.id);
    case ASSET_CONFIG_MAPELEMENT:
        return CreateTask_MapElementLoad(p, key.id);
    case ASSET_CONFIG_WORLDMAP:
        return CreateTask_WorldMapLoad(p);
    case ASSET_CONFIG_OBJ_ALL:
        return CreateTask_ObjLoadAll(p);
    }
    assert(0 && "not a config table");
    return NULL;
}

/* ---- every kind ---------------------------------------------------------- */

bool
AssetCache_Resident(
    struct CacheProvider* provider,
    struct AssetKey key)
{
    assert(provider);
    assert(AssetCache_HandlesKind((enum AssetKind)key.kind));
    switch( key.kind )
    {
    case ASSET_KIND_MODEL:
    case ASSET_KIND_SKYBOX:
        return CacheProvider_ModelHas(provider, key.id);
    case ASSET_KIND_TEXTURE:
        return CacheProvider_TextureHas(provider, key.id);
    case ASSET_KIND_SPRITE:
        return CacheProvider_SpriteHas(provider, key.id);
    case ASSET_KIND_IF_GROUP:
        return CacheProvider_ComponentPackHas(provider, key.id);
    case ASSET_KIND_SOUND:
        return CacheProvider_SoundHas(provider, key.id);
    case ASSET_KIND_OBJ_MODEL:
        return !ObjModelLoad_NeedsWork(provider, key.id, key.variant);
    case ASSET_KIND_CONFIG:
        return config_resident(provider, key);
    case ASSET_KIND_MAP_TERRAIN:
        return CacheProvider_MapTerrainHas(provider, key.id);
    case ASSET_KIND_MAP_SCENERY:
        return CacheProvider_MapSceneryHas(provider, key.id);
    }
    return false;
}

struct ToriRS_Task*
AssetCache_LoadTask(
    struct CacheProvider* provider,
    struct AssetKey key,
    int const* args)
{
    assert(provider);
    assert(AssetCache_HandlesKind((enum AssetKind)key.kind));
    switch( key.kind )
    {
    case ASSET_KIND_MODEL:
    case ASSET_KIND_SKYBOX:
        return CreateTask_ModelLoad(provider, key.id);
    case ASSET_KIND_TEXTURE:
        return CreateTask_TextureLoad(provider, key.id);
    case ASSET_KIND_SPRITE:
        return CreateTask_SpriteLoad(provider, key.id);
    case ASSET_KIND_IF_GROUP:
        return CreateTask_ComponentPackLoad(provider, key.id);
    case ASSET_KIND_SOUND:
        return CreateTask_SoundLoad(provider, key.id);
    case ASSET_KIND_OBJ_MODEL:
        assert(args);
        return CreateTask_ObjModelLoad(provider, &args[0], &args[1], 1);
    case ASSET_KIND_CONFIG:
        return config_load(provider, key);
    case ASSET_KIND_MAP_TERRAIN:
        return CreateTask_MapTerrainLoad(provider, key.id >> 16, key.id & 0xFFFF);
    case ASSET_KIND_MAP_SCENERY:
        return CreateTask_MapSceneryLoad(provider, key.id >> 16, key.id & 0xFFFF);
    }
    return NULL;
}

/* ---- the table's loader -------------------------------------------------- */

static bool
cache_loader_resident(
    void* data,
    struct AssetKey key)
{
    return AssetCache_Resident((struct CacheProvider*)data, key);
}

static enum AssetStart
cache_loader_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    struct CacheProvider* provider = (struct CacheProvider*)data;
    struct AssetLoadTask* task = AssetLoadTask_New(assets, key, cache_loader_resident, provider);

    assert(provider->asset_queue);
    return AssetLoadTask_Start(
        task, AssetCache_LoadTask(provider, key, task->args), provider->asset_queue);
}

void
AssetCache_RegisterLoaders(
    struct Assets* assets,
    struct CacheProvider* provider)
{
    struct AssetLoader const loader = {
        .start = cache_loader_start,
        .resident = cache_loader_resident,
        .data = provider,
    };

    assert(assets);
    assert(provider);
    for( int kind = 0; kind < ASSET_KIND_COUNT; kind++ )
        if( AssetCache_HandlesKind((enum AssetKind)kind) )
            Assets_SetLoader(assets, (enum AssetKind)kind, &loader);
    CacheProvider_SetAssets(provider, assets);
}
