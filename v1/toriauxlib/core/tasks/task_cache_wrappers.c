#include "toriauxlib/core/tasks/toriauxlib_tasks.h"

struct LibToriRS_Task*
Task_CacheModelLoad_New(
    struct ToriAuxLibCache* c,
    int model_id)
{
    if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1ModelLoad_New(c, model_id);
    return Task_Dat2ModelLoad_New(c, model_id);
}

struct LibToriRS_Task*
Task_CacheAnimate_New(
    struct ToriAuxLibCache* c,
    int anim_id)
{
    if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1Animate_New(c, anim_id);
    return Task_Dat2Animate_New(c, anim_id);
}

struct LibToriRS_Task*
Task_CacheNpcAdd_New(
    struct ToriAuxLibCache* c,
    int npc_id)
{
    if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1NpcAdd_New(c, npc_id);
    return Task_Dat2NpcAdd_New(c, npc_id);
}

struct LibToriRS_Task*
Task_CachePlayerAdd_New(
    struct ToriAuxLibCache* c,
    const int appearance[12],
    int readyanim,
    int walkanim,
    int turnanim,
    int runanim,
    int walkanim_b,
    int walkanim_r,
    int walkanim_l)
{
    if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1PlayerAdd_New(
            c,
            appearance,
            readyanim,
            walkanim,
            turnanim,
            runanim,
            walkanim_b,
            walkanim_r,
            walkanim_l);
    return Task_Dat2PlayerAdd_New(
        c,
        appearance,
        readyanim,
        walkanim,
        turnanim,
        runanim,
        walkanim_b,
        walkanim_r,
        walkanim_l);
}

struct LibToriRS_Task*
Task_CacheProjectileAdd_New(
    struct ToriAuxLibCache* c,
    int model_id,
    int anim_id)
{
    if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1ProjectileAdd_New(c, model_id, anim_id);
    return Task_Dat2ProjectileAdd_New(c, model_id, anim_id);
}

struct LibToriRS_Task*
Task_CacheWorldRebuildNormalCenterzone_New(
    struct ToriAuxLibCache* c,
    int zonex,
    int zonez)
{
    if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1WorldRebuildNormalCenterzone_New(c, zonex, zonez);
    return Task_Dat2WorldRebuildCenterzone_New(c, zonex, zonez);
}

struct LibToriRS_Task*
Task_CacheWorldRebuildChunkList_New(
    struct ToriAuxLibCache* c,
    const int* chunks_x,
    const int* chunks_z,
    int count)
{
    if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1WorldRebuildChunkList_New(c, chunks_x, chunks_z, count);
    return Task_Dat2WorldRebuildChunkList_New(c, chunks_x, chunks_z, count);
}

struct LibToriRS_Task*
Task_CacheComponentLoad_New(
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct InstanceRevConfigContext* rc_ctx,
    int root_component_id,
    struct RSComponentLoadCallbacks const* callbacks)
{
    if( ToriAuxLibCache_Mode(cache) == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1ComponentLoad_New(cache, scene, rc_ctx, root_component_id, callbacks);
    return Task_Dat2ComponentLoad_New(cache, scene, rc_ctx, root_component_id, callbacks);
}

struct LibToriRS_Task*
Task_CacheInvLoad_New(
    struct ToriAuxLibCache* cache,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigInvItem const* inv_item,
    struct RSInvLoadCallbacks const* callbacks)
{
    if( ToriAuxLibCache_Mode(cache) == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1InvLoad_New(cache, rc_ctx, inv_item, callbacks);
    return Task_Dat2InvLoad_New(cache, rc_ctx, inv_item, callbacks);
}

struct LibToriRS_Task*
Task_CacheSpriteLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigCacheItem const* item)
{
    if( ToriAuxLibCache_Mode(cache) == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1SpriteLoad_New(rc_ctx, cache, item);
    return Task_Dat2SpriteLoad_New(rc_ctx, cache, item);
}

struct LibToriRS_Task*
Task_CacheFontLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigFontItem const* item)
{
    if( ToriAuxLibCache_Mode(cache) == TORIAUXLIBCACHE_MODE_DAT1 )
        return Task_Dat1FontLoad_New(rc_ctx, cache, item);
    return Task_Dat2FontLoad_New(rc_ctx, cache, item);
}

struct LibToriRS_Task*
Task_CacheTexturesLoad_New(struct ToriAuxLibCache* cache)
{
    return Task_Dat2TexturesLoad_New(cache);
}

struct LibToriRS_Task*
Task_CacheConfigEntryLoad_New(
    struct ToriAuxLibCache* cache,
    int config_kind,
    const int* ids,
    int id_count)
{
    return Task_Dat2ConfigEntryLoad_New(cache, config_kind, ids, id_count);
}

struct LibToriRS_Task*
Task_CacheClientScriptLoad_New(
    struct ToriAuxLibCache* cache,
    const int* script_ids,
    int script_count)
{
    return Task_Dat2ClientScriptLoad_New(cache, script_ids, script_count);
}
