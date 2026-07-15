#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "games/runescape.h"
#include "core/tapi/tapi_dat1.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/cache/toriauxlibcache.h"
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

struct Task_Dat1FontLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct InstanceRevConfigContext* rc_ctx;
    struct ToriAuxLibCache* cache;
    struct RevConfigFontItem item;
    int font_id;
};

static void
dat1_font_load_register_core_font(
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
Task_Dat1FontLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat1FontLoad* task = LibToriRS_container_of(base, struct Task_Dat1FontLoad, base);

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

    if( !task->rc_ctx->title_fonts_ready )
    {
        IO_REQUEST(ctx, 0, TAPIDat1_FetchTitleFontsJagfile(ctx));
        PT_YIELD(&task->pt);
        struct RSCacheShared_FileListDat* title = TAPIDat1_DecodeTitleFontsJagfile(ctx, 0);
        if( title && task->rc_ctx->dat1_bc )
            dat1_buildcache_set_title_fonts_jagfile(task->rc_ctx->dat1_bc, title);
        LibToriRS_IOQueueClear(ctx->io);
        task->rc_ctx->title_fonts_ready = true;
    }

    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->cache);
    if( task->item.cache_font_id >= 0 &&
        task->item.cache_font_id < TORIDRAW_CACHE_FONT_SLOT_COUNT )
    {
        task->font_id = task->item.cache_font_id;
        assert(
            !ToriAuxLibCore_FontHas(core, task->font_id) &&
            "cache_font_id collision in revconfig load");
    }
    else
    {
        task->font_id = task->rc_ctx->next_element_id++;
        assert(
            !ToriAuxLibCore_FontHas(core, task->font_id) &&
            "font_id collision in revconfig load");
    }

    char const* font_stem =
        task->item.font_name[0] != '\0' ? task->item.font_name : task->item.name;

    struct ToriAuxLibCore_Font* font =
        dat1_buildcache_font_decode(task->rc_ctx->dat1_bc, font_stem);
    assert(font && "failed to decode revconfig font (check title_fonts jagfile / font stem)");
    dat1_font_load_register_core_font(
        task->rc_ctx, task->font_id, font, task->item.name, task->item.cache_font_id, -1);

    PT_END(&task->pt);
}

static void
Task_Dat1FontLoad_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat1_font_load_vtable = {
    .run_fn = Task_Dat1FontLoad_Run,
    .free_fn = Task_Dat1FontLoad_Free,
};

struct LibToriRS_Task*
Task_Dat1FontLoad_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigFontItem const* item)
{
    struct Task_Dat1FontLoad* task = calloc(1, sizeof(struct Task_Dat1FontLoad));
    assert(task);
    task->base.vtable = &g_task_dat1_font_load_vtable;
    PT_INIT(&task->pt);
    task->rc_ctx = rc_ctx;
    task->cache = cache;
    if( item )
        task->item = *item;
    return &task->base;
}
