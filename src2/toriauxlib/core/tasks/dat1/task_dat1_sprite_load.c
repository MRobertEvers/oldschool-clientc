#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "games/runescape.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "core/tapi/tapi_dat1.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "ui/ui_sprite_lookup.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat1SpriteLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct InstanceRevConfigContext* rc_ctx;
    struct ToriAuxLibCache* cache;
    struct RevConfigCacheItem item;
    int element_id;
};

static void
dat1_sprite_load_register_core_sprite(
    struct InstanceRevConfigContext* ctx,
    int element_id,
    struct ToriAuxLibCore_Sprite* sprite,
    char const* lookup_name)
{
    assert(ctx && ctx->cache && sprite && lookup_name && ctx->game && ctx->game->td);

    ToriAuxLibCache_SubmitSprite(ctx->cache, element_id, sprite);
    bool const promoted = ToriAuxLibTD_Sprite(ctx->game->td, element_id);
    assert(promoted && "ToriAuxLibTD_Sprite failed for revconfig cache sprite");
    ui_sprite_lookup_add(&ctx->sprite_lookup, lookup_name, element_id, sprite->frame_count);
}

static int
Task_Dat1SpriteLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat1SpriteLoad* task =
        LibToriRS_container_of(base, struct Task_Dat1SpriteLoad, base);

    PT_BEGIN(&task->pt);

    if( !task->rc_ctx || !task->cache || task->item.name[0] == '\0' )
        PT_EXIT(&task->pt);

    if( !task->rc_ctx->jagfiles_ready )
    {
        IO_REQUEST(ctx, 0, TAPIDat1_FetchMediaJagfile(ctx));
        PT_YIELD(&task->pt);
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
        struct ToriAuxLibCore_Sprite* existing = ToriAuxLibCore_SpriteGet(core, task->element_id);
        int atlas_count = existing ? existing->frame_count : 1;
        bool const promoted = ToriAuxLibTD_Sprite(task->rc_ctx->game->td, task->element_id);
        assert(promoted && "ToriAuxLibTD_Sprite failed for existing revconfig cache sprite");
        ui_sprite_lookup_add(
            &task->rc_ctx->sprite_lookup, task->item.name, task->element_id, atlas_count);
    }
    else
    {
        struct ToriAuxLibCore_Sprite* sprite =
            dat1_buildcache_sprite_decode(task->rc_ctx->dat1_bc, &task->item);
        assert(
            sprite && sprite->frame_count > 0 && task->rc_ctx->game &&
            "failed to decode revconfig cache sprite (check media jagfile / sprite item)");
        dat1_sprite_load_register_core_sprite(
            task->rc_ctx, task->element_id, sprite, task->item.name);
    }

    PT_END(&task->pt);
}

static void
Task_Dat1SpriteLoad_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat1_sprite_load_vtable = {
    .run_fn = Task_Dat1SpriteLoad_Run,
    .free_fn = Task_Dat1SpriteLoad_Free,
};

struct LibToriRS_Task*
Task_Dat1SpriteLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigCacheItem const* item)
{
    struct Task_Dat1SpriteLoad* task = calloc(1, sizeof(struct Task_Dat1SpriteLoad));
    assert(task);
    task->base.vtable = &g_task_dat1_sprite_load_vtable;
    PT_INIT(&task->pt);
    task->rc_ctx = rc_ctx;
    task->cache = cache;
    if( item )
        task->item = *item;
    return &task->base;
}
