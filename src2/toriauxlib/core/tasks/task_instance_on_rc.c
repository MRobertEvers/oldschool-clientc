#include "task_instance_on_rc.h"

#include "buildcache/dat1_buildcache_ui.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "core/tapi/tapi_dat1.h"
#include "core/tapi/tapi_dat2.h"
#include "core_task_await.h"
#include "task_rs_component_load.h"
#include "task_rs_inv_load.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "games/runescape.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RCUIComponentLoadUser
{
    struct InstanceRevConfigContext* ctx;
    char owner_component[64];
};

static void
on_rc_uicomponent_rs_loaded(
    void* user,
    int component_id)
{
    struct RCUIComponentLoadUser* load_user = user;
    assert(load_user && load_user->ctx && component_id >= 0);

    struct InstanceRevConfigRSSubtree* subtree =
        instance_revconfig_rs_subtree_get_or_create(load_user->ctx, load_user->owner_component);
    assert(subtree);
    instance_revconfig_rs_subtree_append(subtree, component_id);
}

static void
instance_revconfig_register_core_sprite(
    struct InstanceRevConfigContext* ctx,
    int element_id,
    struct ToriAuxLibCore_Sprite* sprite,
    char const* lookup_name)
{
    if( !ctx || !ctx->cache || !sprite || !lookup_name || !ctx->game || !ctx->game->td )
        return;

    ToriAuxLibCache_SubmitSprite(ctx->cache, element_id, sprite);
    ToriAuxLibTD_Sprite(ctx->game->td, element_id);
    ui_sprite_lookup_add(&ctx->sprite_lookup, lookup_name, element_id, sprite->frame_count);
}

static bool
revconfig_uicomponent_needs_rs_load(struct RevConfigUIComponentItem const* item)
{
    assert(item);
    if( item->componentno < 0 )
        return false;

    if( strcmp(item->type, "sidebar") == 0 )
        return true;
    if( strcmp(item->type, "chat") == 0 )
        return true;
    if( strcmp(item->type, "rs_layer") == 0 )
        return true;
    if( strcmp(item->type, "rs_graphic") == 0 )
        return true;
    if( strcmp(item->type, "rs_text") == 0 )
        return true;
    if( strcmp(item->type, "rs_rect") == 0 )
        return true;
    if( strcmp(item->type, "rs_model") == 0 )
        return true;
    if( strcmp(item->type, "rs_inv") == 0 )
        return true;
    return false;
}

void
Task_InstanceOnRCCacheSprite_Init(
    struct Task_InstanceOnRCCacheSprite* task,
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigCacheItem const* item)
{
    assert(item);
    memset(task, 0, sizeof(*task));
    PT_INIT(&task->thread);
    task->rc_ctx = rc_ctx;
    task->cache = cache;
    task->item = *item;
}

int
Task_InstanceOnRCCacheSprite_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_InstanceOnRCCacheSprite* task = task_state;

    PT_BEGIN(&task->thread);

    if( !task->rc_ctx || !task->cache || task->item.name[0] == '\0' )
        PT_EXIT(&task->thread);

    if( ToriAuxLibCache_Mode(task->cache) == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        if( !task->rc_ctx->jagfiles_ready )
        {
            IO_REQUEST(ctx, 0, TAPIDat1_FetchMediaJagfile(ctx));
            PT_YIELD(&task->thread);
            struct RSCacheShared_FileListDat* media = TAPIDat1_DecodeMediaJagfile(ctx, 0);
            if( media && task->rc_ctx->dat1_bc )
                dat1_buildcache_set_media_2d_graphics_jagfile(task->rc_ctx->dat1_bc, media);
            LibToriRS_IOQueueClear(ctx->io);
            task->rc_ctx->jagfiles_ready = true;
        }

        struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->cache);
        task->element_id = task->rc_ctx->next_element_id++;
        if( ToriAuxLibCore_SpriteHas(core, task->element_id) )
        {
            ui_sprite_lookup_add(
                &task->rc_ctx->sprite_lookup, task->item.name, task->element_id, 1);
        }
        else
        {
            struct ToriAuxLibCore_Sprite* sprite =
                dat1_buildcache_sprite_decode(task->rc_ctx->dat1_bc, &task->item);
            if( sprite && sprite->frame_count > 0 && task->rc_ctx->game )
            {
                instance_revconfig_register_core_sprite(
                    task->rc_ctx, task->element_id, sprite, task->item.name);
            }
            else
            {
                ToriAuxLibCore_SpriteFree(sprite);
            }
        }
    }
    else if( ToriAuxLibCache_Mode(task->cache) == TORIAUXLIBCACHE_MODE_DAT2 )
    {
        struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->cache);
        task->element_id = task->rc_ctx->next_element_id++;
        if( task->item.archive_id < 0 )
            PT_EXIT(&task->thread);

        IO_REQUEST(ctx, 0, TAPIDat2_FetchSprite(ctx, task->item.archive_id));
        PT_YIELD(&task->thread);

        struct RSCacheDat2Disk_Archive* sprite_archive =
            TAPIDat2_DecodeSpriteArchive(ctx, 0, task->item.archive_id);
        if( !sprite_archive )
            PT_EXIT(&task->thread);

        struct ToriAuxLibCore_Sprite* sprite =
            dat2_buildcache_sprite_decode_from_archive(sprite_archive, &task->item);
        if( sprite && sprite->frame_count > 0 && task->rc_ctx->game )
        {
            instance_revconfig_register_core_sprite(
                task->rc_ctx, task->element_id, sprite, task->item.name);
        }
        else
        {
            ToriAuxLibCore_SpriteFree(sprite);
        }
        (void)core;
    }

    PT_END(&task->thread);
}

void
Task_InstanceOnRCUIComponent_Init(
    struct Task_InstanceOnRCUIComponent* task,
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigUIComponentItem const* item)
{
    assert(item);

    memset(task, 0, sizeof(*task));
    PT_INIT(&task->thread);
    task->rc_ctx = rc_ctx;
    task->cache = cache;
    task->item = *item;
}

int
Task_InstanceOnRCUIComponent_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_InstanceOnRCUIComponent* task = task_state;
    struct RCUIComponentLoadUser load_user;

    PT_BEGIN(&task->thread);

    if( !task->rc_ctx || task->item.name[0] == '\0' )
        PT_EXIT(&task->thread);

    if( task->rc_ctx->component_count >= INSTANCE_RC_MAX_COMPONENTS )
        PT_EXIT(&task->thread);

    task->rc_ctx->components[task->rc_ctx->component_count++] = task->item;

    if( revconfig_uicomponent_needs_rs_load(&task->item) && task->cache )
    {
        memset(&load_user, 0, sizeof(load_user));
        load_user.ctx = task->rc_ctx;
        strncpy(load_user.owner_component, task->item.name, sizeof(load_user.owner_component) - 1);

        struct RSComponentLoadCallbacks cbs = { 0 };
        cbs.user = &load_user;
        cbs.on_component = on_rc_uicomponent_rs_loaded;

        task->rs_load = Task_RSComponentLoad_New(
            ToriAuxLibCache_Mode(task->cache),
            task->cache,
            task->rc_ctx->scene,
            task->rc_ctx,
            task->item.componentno,
            &cbs);
        assert(task->rs_load);

        TASK_AWAIT(&task->thread, Task_RSComponentLoad_Run(task->rs_load, ctx));

        Task_RSComponentLoad_Free(task->rs_load);

        task->rs_load = NULL;
    }

    PT_END(&task->thread);
}

void
Task_InstanceOnRCUILayout_Init(
    struct Task_InstanceOnRCUILayout* task,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigUILayoutItem const* item)
{
    assert(item);
    memset(task, 0, sizeof(*task));
    PT_INIT(&task->thread);
    task->rc_ctx = rc_ctx;
    task->item = *item;
}

int
Task_InstanceOnRCUILayout_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_InstanceOnRCUILayout* task = task_state;
    (void)ctx;

    PT_BEGIN(&task->thread);

    if( !task->rc_ctx || task->item.component[0] == '\0' )
        PT_EXIT(&task->thread);

    if( task->rc_ctx->layout_count >= INSTANCE_RC_MAX_LAYOUTS )
        PT_EXIT(&task->thread);

    task->rc_ctx->layouts[task->rc_ctx->layout_count++] = task->item;

    PT_END(&task->thread);
}

void
Task_InstanceOnRCInv_Init(
    struct Task_InstanceOnRCInv* task,
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigInvItem const* item)
{
    assert(item);
    memset(task, 0, sizeof(*task));
    PT_INIT(&task->thread);
    task->rc_ctx = rc_ctx;
    task->cache = cache;
    task->item = *item;
}

int
Task_InstanceOnRCInv_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_InstanceOnRCInv* task = task_state;

    PT_BEGIN(&task->thread);

    if( !task->rc_ctx || !task->cache )
        PT_EXIT(&task->thread);

    struct RSInvLoadCallbacks cbs = { 0 };
    task->inv_load = Task_RSInvLoad_New(
        ToriAuxLibCache_Mode(task->cache), task->cache, task->rc_ctx, &task->item, &cbs);
    assert(task->inv_load);

    TASK_AWAIT(&task->thread, Task_RSInvLoad_Run(task->inv_load, ctx));
    Task_RSInvLoad_Free(task->inv_load);
    task->inv_load = NULL;

    PT_END(&task->thread);
}
