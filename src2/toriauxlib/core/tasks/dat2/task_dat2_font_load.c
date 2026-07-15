#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat2_buildcache.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "core/tapi/tapi_dat2.h"
#include "games/runescape.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_font_convert.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_scene.h"
#include "ui/ui_font_lookup.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2FontLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct InstanceRevConfigContext* rc_ctx;
    struct ToriAuxLibCache* cache;
    struct RevConfigFontItem item;
    int font_id;
};

static void
dat2_font_load_register_core_font(
    struct InstanceRevConfigContext* ctx,
    int font_id,
    struct ToriAuxLibCore_Font* font,
    char const* lookup_name,
    int cache_font_id,
    int cache_archive_id)
{
    assert(ctx && ctx->cache && font && lookup_name);
    assert(ctx->td && "revconfig font load requires ToriAuxLibTD");
    assert(ctx->scene && "revconfig font load requires ToriDraw scene");
    assert(ctx->core && "revconfig font load requires ToriAuxLibCore");

    ToriAuxLibCache_SubmitFont(ctx->cache, font_id, font);
    assert(ToriAuxLibCore_FontHas(ctx->core, font_id));

    bool const promoted = ToriAuxLibTD_Font(ctx->td, font_id);
    assert(promoted && "ToriAuxLibTD_Font failed");
    assert(ToriDraw_SceneFontHas(ctx->scene, font_id));

    struct ToriDraw_Font* scene_font = ToriDraw_SceneFontGet(ctx->scene, font_id);
    assert(scene_font && "revconfig font missing from scene after ToriAuxLibTD_Font");
    assert(ToriDraw_FontValidate(scene_font));

    if( cache_font_id >= 0 && cache_font_id < TORIDRAW_CACHE_FONT_SLOT_COUNT )
        ToriDraw_SceneCacheFontSet(ctx->scene, cache_font_id, scene_font);

    ui_font_lookup_add(&ctx->font_lookup, lookup_name, font_id, cache_font_id, cache_archive_id);
}

static int
Task_Dat2FontLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2FontLoad* task = LibToriRS_container_of(base, struct Task_Dat2FontLoad, base);

    PT_BEGIN(&task->pt);

    assert(task->rc_ctx);
    assert(task->cache);
    if( task->item.name[0] == '\0' )
        PT_EXIT(&task->pt);

    if( task->item.cache_font_id >= 0 &&
        task->item.cache_font_id < TORIDRAW_CACHE_FONT_SLOT_COUNT )
    {
        task->font_id = task->item.cache_font_id;
        struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->cache);
        assert(
            !ToriAuxLibCore_FontHas(core, task->font_id) &&
            "cache_font_id collision in revconfig load");
    }
    else
    {
        task->font_id = task->rc_ctx->next_element_id++;
    }
    if( task->item.archive_id < 0 )
        PT_EXIT(&task->pt);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchFont(ctx, task->item.archive_id));
    IO_REQUEST(ctx, 1, TAPIDat2_FetchSprite(ctx, task->item.archive_id));
    PT_YIELD(&task->pt);

    struct RSCacheDat2Disk_Archive* font_archive =
        TAPIDat2_DecodeFontArchive(ctx, 0, task->item.archive_id);
    struct RSCacheDat2Disk_Archive* sprite_archive =
        TAPIDat2_DecodeSpriteArchive(ctx, 1, task->item.archive_id);
    if( !font_archive || !sprite_archive )
    {
        fprintf(
            stderr,
            "Task_Dat2FontLoad: failed to fetch font archives archive_id=%d "
            "(font=%p sprite=%p)\n",
            task->item.archive_id,
            (void*)font_archive,
            (void*)sprite_archive);
        RSCacheDat2Disk_ArchiveFree(font_archive);
        RSCacheDat2Disk_ArchiveFree(sprite_archive);
        PT_EXIT(&task->pt);
    }

    if( !dat2_buildcache_font_add_from_archives(
            dat2(task->cache), task->item.archive_id, font_archive, sprite_archive) )
    {
        RSCacheDat2Disk_ArchiveFree(font_archive);
        RSCacheDat2Disk_ArchiveFree(sprite_archive);
        PT_EXIT(&task->pt);
    }
    RSCacheDat2Disk_ArchiveFree(font_archive);
    RSCacheDat2Disk_ArchiveFree(sprite_archive);
    LibToriRS_IOQueueClear(ctx->io);

    struct Dat2BuildCache_FontAsset* font_asset =
        dat2_buildcache_font_get(dat2(task->cache), task->item.archive_id);
    struct ToriAuxLibCore_Font* font =
        ToriAuxLibCache_FontNewFromDat2FontAsset(font_asset, task->item.archive_id);
    if( !font )
    {
        fprintf(
            stderr,
            "Task_Dat2FontLoad: failed to decode font archive_id=%d name=%s\n",
            task->item.archive_id,
            task->item.name);
        PT_EXIT(&task->pt);
    }
    dat2_font_load_register_core_font(
        task->rc_ctx,
        task->font_id,
        font,
        task->item.name,
        task->item.cache_font_id,
        task->item.archive_id);

    PT_END(&task->pt);
}

static void
Task_Dat2FontLoad_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat2_font_load_vtable = {
    .run_fn = Task_Dat2FontLoad_Run,
    .free_fn = Task_Dat2FontLoad_Free,
};

struct LibToriRS_Task*
Task_Dat2FontLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigFontItem const* item)
{
    struct Task_Dat2FontLoad* task = calloc(1, sizeof(struct Task_Dat2FontLoad));
    assert(task);
    task->base.vtable = &g_task_dat2_font_load_vtable;
    PT_INIT(&task->pt);
    task->rc_ctx = rc_ctx;
    task->cache = cache;
    if( item )
        task->item = *item;
    return &task->base;
}
