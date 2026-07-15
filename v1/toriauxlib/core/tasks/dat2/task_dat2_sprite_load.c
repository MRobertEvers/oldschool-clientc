#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "core/tapi/tapi_dat2.h"
#include "games/runescape.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "ui/ui_sprite_lookup.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2SpriteLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct InstanceRevConfigContext* rc_ctx;
    struct ToriAuxLibCache* cache;
    struct RevConfigCacheItem item;
    int element_id;
};

static void
dat2_sprite_load_register_core_sprite(
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
Task_Dat2SpriteLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2SpriteLoad* task =
        LibToriRS_container_of(base, struct Task_Dat2SpriteLoad, base);

    PT_BEGIN(&task->pt);

    assert(task->rc_ctx);
    assert(task->cache);
    if( task->item.name[0] == '\0' )
        PT_EXIT(&task->pt);

    task->element_id = task->rc_ctx->next_element_id++;
    if( task->item.archive_id < 0 )
        PT_EXIT(&task->pt);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchSprite(ctx, task->item.archive_id));
    PT_YIELD(&task->pt);

    struct RSCacheDat2Disk_Archive* sprite_archive =
        TAPIDat2_DecodeSpriteArchive(ctx, 0, task->item.archive_id);
    assert(sprite_archive && "failed to decode revconfig cache sprite archive");

    struct ToriAuxLibCore_Sprite* sprite =
        dat2_buildcache_sprite_decode_from_archive(sprite_archive, &task->item);
    assert(
        sprite && sprite->frame_count > 0 && task->rc_ctx->game &&
        "failed to decode revconfig cache sprite from archive");
    dat2_sprite_load_register_core_sprite(
        task->rc_ctx, task->element_id, sprite, task->item.name);

    PT_END(&task->pt);
}

static void
Task_Dat2SpriteLoad_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat2_sprite_load_vtable = {
    .run_fn = Task_Dat2SpriteLoad_Run,
    .free_fn = Task_Dat2SpriteLoad_Free,
};

struct LibToriRS_Task*
Task_Dat2SpriteLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigCacheItem const* item)
{
    struct Task_Dat2SpriteLoad* task = calloc(1, sizeof(struct Task_Dat2SpriteLoad));
    assert(task);
    task->base.vtable = &g_task_dat2_sprite_load_vtable;
    PT_INIT(&task->pt);
    task->rc_ctx = rc_ctx;
    task->cache = cache;
    if( item )
        task->item = *item;
    return &task->base;
}
