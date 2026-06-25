#ifndef REVCONFIG_UI_LOAD_H
#define REVCONFIG_UI_LOAD_H

#include "../../core/configio.h"
#include "../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "revconfig/revconfig.h"
#include "revconfig/revconfig_load.h"
#include "ui/uitree.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "revconfig_ui_build.h"
#include "toriauxlib/c/dat1io.h"
#include "toriauxlib/c/toriauxlibc.h"
#include "toriauxlib/c/toriauxlibc_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toridraw/toridraw_scene.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REVCONFIG_UI_CACHE_INI "../../../src/osrs/revconfig/configs/rev_245_2/rev_245_2_cache.ini"
#define REVCONFIG_UI_UI_INI "../../../src/osrs/revconfig/configs/rev_245_2/rev_245_2_ui.ini"

#define REVCONFIG_UI_FONT_COUNT 4

enum RevConfigUIWorkKind
{
    REVCONFIG_UIWORK_FONT = 0,
    REVCONFIG_UIWORK_NODE,
};

struct RevConfigUIWorkItem
{
    enum RevConfigUIWorkKind kind;
    int layout_index;
    int font_id;
    char font_name[16];
};

struct Task_RevConfigUILoad
{
    struct pt thread;
    struct ToriAuxLibC* c;
    struct ToriDraw_Scene* scene;

    struct RevConfigBuffer* cache_fields;
    struct RevConfigBuffer* ui_fields;
    struct RevConfigItemBuffer* items;
    struct RevConfigUIBuildState build;
    struct UITree* tree;

    struct RevConfigUIWorkItem work_items[REVCONFIG_UI_MAX_WORK_ITEMS];
    int work_head;
    int work_count;

    bool ui_ready;
};

struct Task_RevConfigUILoad*
Task_RevConfigUILoad_New(
    struct ToriAuxLibC* c,
    struct ToriDraw_Scene* scene,
    struct UITree* tree)
{
    struct Task_RevConfigUILoad* task = calloc(1, sizeof(struct Task_RevConfigUILoad));
    assert(task);
    PT_INIT(&task->thread);
    task->c = c;
    task->scene = scene;
    task->tree = tree;
    revconfig_ui_build_init(&task->build);
    return task;
}

void
Task_RevConfigUILoad_Free(struct Task_RevConfigUILoad* task)
{
    if( !task )
        return;
    revconfig_buffer_free(task->cache_fields);
    revconfig_buffer_free(task->ui_fields);
    revconfig_item_buffer_free(task->items);
    free(task);
}

struct UITree*
Task_RevConfigUILoad_Tree(struct Task_RevConfigUILoad* task)
{
    return task ? task->tree : NULL;
}

bool
Task_RevConfigUILoad_IsReady(struct Task_RevConfigUILoad* task)
{
    return task && task->ui_ready;
}

static void
revconfig_ui_work_push(
    struct Task_RevConfigUILoad* task,
    struct RevConfigUIWorkItem const* item)
{
    if( !task || !item || task->work_count >= REVCONFIG_UI_MAX_WORK_ITEMS )
        return;
    int tail = (task->work_head + task->work_count) % REVCONFIG_UI_MAX_WORK_ITEMS;
    task->work_items[tail] = *item;
    task->work_count++;
}

static bool
revconfig_ui_work_pop(
    struct Task_RevConfigUILoad* task,
    struct RevConfigUIWorkItem* out)
{
    if( !task || task->work_count <= 0 )
        return false;
    if( out )
        *out = task->work_items[task->work_head];
    task->work_head = (task->work_head + 1) % REVCONFIG_UI_MAX_WORK_ITEMS;
    task->work_count--;
    return true;
}

static void
revconfig_ui_seed_work_queue(struct Task_RevConfigUILoad* task)
{
    static char const* const font_names[REVCONFIG_UI_FONT_COUNT] = { "p11", "p12", "b12", "q8" };

    for( int i = 0; i < REVCONFIG_UI_FONT_COUNT; i++ )
    {
        struct RevConfigUIWorkItem item = { 0 };
        item.kind = REVCONFIG_UIWORK_FONT;
        item.font_id = i;
        strncpy(item.font_name, font_names[i], sizeof(item.font_name) - 1);
        revconfig_ui_work_push(task, &item);
    }

    for( int i = 0; i < task->build.layout_entry_count; i++ )
    {
        struct RevConfigUIWorkItem item = { 0 };
        item.kind = REVCONFIG_UIWORK_NODE;
        item.layout_index = i;
        revconfig_ui_work_push(task, &item);
    }
}

static bool
revconfig_ui_ensure_sprite_loaded(
    struct Task_RevConfigUILoad* task,
    char const* sprite_ref)
{
    if( !sprite_ref || sprite_ref[0] == '\0' )
        return true;

    struct RevConfigUISpriteRef const* ref =
        revconfig_ui_build_find_ref(&task->build, sprite_ref);
    if( !ref )
        return false;

    struct ToriAuxLibCore* core = ToriAuxLibC_Core(task->c);
    struct Dat1BuildCache* bc = dat1(task->c);
    if( ToriAuxLibCore_SpriteHas(core, ref->element_id) )
        return true;

    int count = 0;
    struct ToriDraw_Sprite** sprites = dat1_buildcache_sprite_decode(bc, &ref->cache_item, &count);
    if( !sprites || count <= 0 )
    {
        fprintf(stderr, "revconfig_ui: failed to decode sprite '%s'\n", ref->name);
        return false;
    }

    struct ToriAuxLibCore_Sprite* gc_sprite = calloc(1, sizeof(struct ToriAuxLibCore_Sprite));
    if( !gc_sprite )
    {
        for( int j = 0; j < count; j++ )
            ToriDraw_SpriteFree(sprites[j]);
        free(sprites);
        return false;
    }
    strncpy(gc_sprite->name, ref->name, sizeof(gc_sprite->name) - 1);
    gc_sprite->sprites = sprites;
    gc_sprite->count = count;
    ToriAuxLibC_SubmitSpriteFromDat1(task->c, ref->element_id, gc_sprite);
    ToriDraw_SceneSpriteAdd(task->scene, ref->element_id, sprites, count);
    return true;
}

static struct RevConfigUIComponentItem const*
revconfig_ui_find_component(
    struct RevConfigUIBuildState const* state,
    char const* name)
{
    if( !state || !name )
        return NULL;
    for( int i = 0; i < state->component_count; i++ )
    {
        if( strcmp(state->components[i].name, name) == 0 )
            return &state->components[i];
    }
    return NULL;
}

static bool
revconfig_ui_process_work_item(
    struct Task_RevConfigUILoad* task,
    struct RevConfigUIWorkItem const* item)
{
    struct ToriAuxLibCore* core = ToriAuxLibC_Core(task->c);
    struct Dat1BuildCache* bc = dat1(task->c);

    if( item->kind == REVCONFIG_UIWORK_FONT )
    {
        if( ToriAuxLibCore_FontHas(core, item->font_id) )
            return true;

        struct ToriDraw_Font* font = dat1_buildcache_font_decode(bc, item->font_name);
        if( !font )
            return false;

        struct ToriAuxLibCore_Font* gc_font = calloc(1, sizeof(struct ToriAuxLibCore_Font));
        if( !gc_font )
        {
            ToriDraw_FontFree(font);
            return false;
        }
        strncpy(gc_font->name, item->font_name, sizeof(gc_font->name) - 1);
        gc_font->font = font;
        ToriAuxLibC_SubmitFontFromDat1(task->c, item->font_id, gc_font);
        ToriDraw_SceneFontAdd(task->scene, item->font_id, font);
        return true;
    }

    if( item->kind == REVCONFIG_UIWORK_NODE )
    {
        if( !task->tree || item->layout_index < 0 ||
            item->layout_index >= task->build.layout_entry_count )
            return true;

        struct RevConfigUILayoutItem const* le =
            &task->build.layout_entries[item->layout_index];
        if( le->component[0] == '\0' )
            return true;

        struct RevConfigUIComponentItem const* comp =
            revconfig_ui_find_component(&task->build, le->component);
        if( comp )
        {
            (void)revconfig_ui_ensure_sprite_loaded(task, comp->sprite);
            (void)revconfig_ui_ensure_sprite_loaded(task, comp->sprite_active);
        }

        revconfig_ui_build_node(&task->build, task->tree, le);
        return true;
    }

    return false;
}

int
Task_RevConfigUILoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_RevConfigUILoad* task = task_state;
    struct Dat1BuildCache* bc = dat1(task->c);

    PT_BEGIN(&task->thread);

    task->cache_fields = revconfig_buffer_new(1024);
    task->ui_fields = revconfig_buffer_new(1024);
    task->items = revconfig_item_buffer_new(256);

    configio_fetch_revconfig(ctx, REVCONFIG_UI_CACHE_INI);
    configio_fetch_revconfig(ctx, REVCONFIG_UI_UI_INI);
    PT_YIELD(&task->thread);

    {
        void* data = NULL;
        int size = configio_decode_revconfig(ctx, &data);
        if( size > 0 && data )
            revconfig_load_fields_from_ini_bytes(data, (size_t)size, task->cache_fields);
        free(data);
    }
    {
        void* data = NULL;
        int size = configio_decode_revconfig(ctx, &data);
        if( size > 0 && data )
            revconfig_load_fields_from_ini_bytes(data, (size_t)size, task->ui_fields);
        free(data);
    }

    revconfig_items_build(task->cache_fields, task->items);
    revconfig_items_build(task->ui_fields, task->items);
    revconfig_ui_build_collect_items(&task->build, task->items);
    revconfig_ui_build_set_core(&task->build, ToriAuxLibC_Core(task->c));

    dat1io_media_jagfile_fetch(ctx);
    dat1io_title_fonts_jagfile_fetch(ctx);
    dat1io_interfaces_jagfile_fetch(ctx);
    PT_YIELD(&task->thread);

    {
        struct RSCacheShared_FileListDat* media = dat1io_media_jagfile_decode(ctx);
        if( media )
            dat1_buildcache_set_media_2d_graphics_jagfile(bc, media);
        struct RSCacheShared_FileListDat* title = dat1io_title_fonts_jagfile_decode(ctx);
        if( title )
            dat1_buildcache_set_title_fonts_jagfile(bc, title);
        struct RSCacheShared_FileListDat* interfaces_filelist = dat1io_interfaces_jagfile_decode(ctx);
        if( interfaces_filelist )
        {
            int data_idx = RSCacheShared_FileListDatFindFileByName(interfaces_filelist, "data");
            if( data_idx >= 0 )
            {
                struct RSCacheDat1A_ConfigComponentList* interfaces =
                    RSCacheDat1A_ConfigComponentListNewDecode(
                        interfaces_filelist->files[data_idx],
                        interfaces_filelist->file_sizes[data_idx]);
                if( interfaces )
                {
                    dat1_buildcache_set_interfaces(bc, interfaces);
                    ToriAuxLibC_SubmitAllComponentsFromDat1(task->c);
                }
            }
            RSCacheShared_FileListDatFree(interfaces_filelist);
        }
    }
    LibToriRS_IOQueueClear(ctx->io);

    revconfig_ui_seed_work_queue(task);

    while( task->work_count > 0 )
    {
        struct RevConfigUIWorkItem item;
        if( !revconfig_ui_work_pop(task, &item) )
            break;
        if( !revconfig_ui_process_work_item(task, &item) )
        {
            revconfig_ui_work_push(task, &item);
            PT_YIELD(&task->thread);
            continue;
        }
    }

    if( task->tree )
        uitree_mark_all_dirty(task->tree);
    task->ui_ready = true;

    PT_END(&task->thread);
}

#endif
