#include "t_ui_dat2_load.h"

#include "games/runescape.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/varp_varbit_manager.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "ui/uitree_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char const*
ui_dat2_pick_layout_group(struct Task_Dat2UILoad const* task)
{
    if( !task->is_resized )
        return UI_DAT2_LAYOUT_GROUP_FIXED;

    struct VarPVarBitManager* mgr = ToriAuxLibCache_VarPVarBit(task->c);
    if( mgr && varp_varbit_get_varbit(mgr, UI_VARBIT_SIDE_PANELS) == 1 )
        return UI_DAT2_LAYOUT_GROUP_RESIZABLE_BOTTOM;
    return UI_DAT2_LAYOUT_GROUP_RESIZABLE_BOX;
}

static int
dat2_sprite_id_from_ref_name(char const* name)
{
    if( !name || name[0] == '\0' )
        return -1;
    return atoi(name);
}

static int
dat2_sprite_id_from_ref(struct RevConfigUISpriteRef const* ref)
{
    if( !ref )
        return -1;
    if( ref->cache_item.archive_id >= 0 )
        return ref->cache_item.archive_id;
    return dat2_sprite_id_from_ref_name(ref->name);
}

static void
ui_dat2_work_push(
    struct Task_Dat2UILoad* task,
    struct UIDat2WorkItem const* item)
{
    if( !task || !item || task->work_count >= REVCONFIG_UI_MAX_WORK_ITEMS )
        return;
    int tail = (task->work_head + task->work_count) % REVCONFIG_UI_MAX_WORK_ITEMS;
    task->work_items[tail] = *item;
    task->work_count++;
}

static bool
ui_dat2_work_pop(
    struct Task_Dat2UILoad* task,
    struct UIDat2WorkItem* out)
{
    if( !task || task->work_count <= 0 )
        return false;
    if( out )
        *out = task->work_items[task->work_head];
    task->work_head = (task->work_head + 1) % REVCONFIG_UI_MAX_WORK_ITEMS;
    task->work_count--;
    return true;
}

struct Task_Dat2UILoad*
Task_Dat2UILoad_New(
    struct ToriAuxLibCache* c,
    struct ToriDraw_Scene* scene,
    struct UITree* tree,
    struct GameRunescape* game)
{
    struct Task_Dat2UILoad* task = calloc(1, sizeof(struct Task_Dat2UILoad));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->c = c;
    task->scene = scene;
    task->tree = tree;
    task->game = game;
    strncpy(task->layout_group, UI_DAT2_LAYOUT_GROUP_FIXED, sizeof(task->layout_group) - 1);
    revconfig_ui_build_init(&task->build);
    return task;
}

void
Task_Dat2UILoad_SetResized(struct Task_Dat2UILoad* task, bool is_resized)
{
    if( task )
        task->is_resized = is_resized;
}

void
Task_Dat2UILoad_SetLayoutGroup(struct Task_Dat2UILoad* task, char const* layout_group)
{
    if( !task || !layout_group )
        return;
    strncpy(task->layout_group, layout_group, sizeof(task->layout_group) - 1);
    task->layout_group[sizeof(task->layout_group) - 1] = '\0';
}

void
Task_Dat2UILoad_Free(struct Task_Dat2UILoad* task)
{
    if( !task )
        return;
    revconfig_buffer_free(task->cache_fields);
    revconfig_buffer_free(task->ui_fields);
    revconfig_item_buffer_free(task->items);
    free(task);
}

bool
Task_Dat2UILoad_IsReady(struct Task_Dat2UILoad* task)
{
    return task && task->ui_ready;
}

static bool
ui_dat2_fonts_available(struct ToriAuxLibCache* c)
{
    struct RSCacheDat2Disk* disk = ToriAuxLibCache_Dat2Disk(c);
    if( !disk )
        return false;

    struct RSCacheDat2Disk_Archive* arch =
        RSCacheDat2Disk_ArchiveNewLoad(disk, RSCacheDat2Disk_Table_Fonts, 0);
    if( !arch )
        return false;

    RSCacheDat2Disk_ArchiveFree(arch);
    return true;
}

static void
ui_dat2_seed_work_queue(struct Task_Dat2UILoad* task)
{
    task->fonts_skipped = !ui_dat2_fonts_available(task->c);
    if( task->fonts_skipped )
        fprintf(stderr, "dat2 ui load: fonts unavailable in cache, skipping font preload\n");
    else
    {
        for( int i = 0; i < UI_DAT2_FONT_COUNT; i++ )
        {
            struct UIDat2WorkItem item = { 0 };
            item.kind = UI_DAT2WORK_FONT;
            item.font_id = i;
            item.numeric_id = i;
            ui_dat2_work_push(task, &item);
        }
    }

    for( int i = 0; i < task->build.sprite_ref_count; i++ )
    {
        struct UIDat2WorkItem item = { 0 };
        item.kind = UI_DAT2WORK_SPRITE;
        item.ref_index = i;
        item.numeric_id = dat2_sprite_id_from_ref(&task->build.sprite_refs[i]);
        ui_dat2_work_push(task, &item);
    }
}

static bool
ui_dat2_process_work_item(
    struct Task_Dat2UILoad* task,
    struct UIDat2WorkItem const* item)
{
    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->c);
    struct Dat2BuildCache* bc = dat2(task->c);

    if( item->kind == UI_DAT2WORK_FONT )
    {
        if( task->fonts_skipped )
            return true;

        if( ToriAuxLibCore_FontHas(core, item->font_id) )
            return true;

        struct ToriDraw_Font* font = dat2_buildcache_font_decode_id(bc, item->numeric_id);
        if( !font )
            return false;

        struct ToriAuxLibCore_Font* gc_font = calloc(1, sizeof(struct ToriAuxLibCore_Font));
        if( !gc_font )
        {
            ToriDraw_FontFree(font);
            return false;
        }
        snprintf(gc_font->name, sizeof(gc_font->name), "font_%d", item->numeric_id);
        gc_font->font = font;
        ToriAuxLibCache_SubmitFontFromDat1(task->c, item->font_id, gc_font);
        ToriDraw_SceneFontAdd(task->scene, item->font_id, font);
        return true;
    }

    if( item->kind == UI_DAT2WORK_SPRITE )
    {
        if( item->ref_index < 0 || item->ref_index >= task->build.sprite_ref_count )
            return true;

        struct RevConfigUISpriteRef const* ref = &task->build.sprite_refs[item->ref_index];
        if( ToriAuxLibCore_SpriteHas(core, ref->element_id) )
            return true;

        if( ref->cache_item.archive_id < 0 )
            return false;

        int count = 0;
        struct ToriDraw_Sprite** sprites =
            dat2_buildcache_sprite_decode(bc, &ref->cache_item, &count);
        if( !sprites || count <= 0 )
            return false;

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
        ToriAuxLibCache_SubmitSpriteFromDat1(task->c, ref->element_id, gc_sprite);
        ToriDraw_SceneSpriteAdd(task->scene, ref->element_id, sprites, count);
        return true;
    }

    return false;
}

int
Task_Dat2UILoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2UILoad* task = task_state;
    struct Dat2BuildCache* bc = dat2(task->c);

    PT_BEGIN(&task->thread);

    dat2_buildcache_ui_set_disk(bc, ToriAuxLibCache_Dat2Disk(task->c));

    task->cache_fields = revconfig_buffer_new(1024);
    task->ui_fields = revconfig_buffer_new(1024);
    task->items = revconfig_item_buffer_new(256);

    configio_fetch_revconfig(ctx, UI_DAT2_CACHE_INI);
    configio_fetch_revconfig(ctx, UI_DAT2_UI_INI);
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
    revconfig_ui_build_set_core(&task->build, ToriAuxLibCache_Core(task->c));

    ui_dat2_seed_work_queue(task);

    while( task->work_count > 0 )
    {
        struct UIDat2WorkItem item;
        if( !ui_dat2_work_pop(task, &item) )
            break;
        if( !ui_dat2_process_work_item(task, &item) )
        {
            item.retry_count++;
            if( item.retry_count >= UI_DAT2_MAX_WORK_RETRIES )
            {
                if( item.kind == UI_DAT2WORK_SPRITE && item.ref_index >= 0 &&
                    item.ref_index < task->build.sprite_ref_count )
                {
                    fprintf(
                        stderr,
                        "dat2 ui load: sprite '%s' (id=%d) failed after %d retries\n",
                        task->build.sprite_refs[item.ref_index].name,
                        item.numeric_id,
                        UI_DAT2_MAX_WORK_RETRIES);
                }
                else
                {
                    fprintf(
                        stderr,
                        "dat2 ui load: font %d failed after %d retries\n",
                        item.font_id,
                        UI_DAT2_MAX_WORK_RETRIES);
                }
                continue;
            }
            ui_dat2_work_push(task, &item);
            PT_YIELD(&task->thread);
            continue;
        }
    }

    if( task->tree )
    {
        char const* group = task->layout_group[0] != '\0' ? task->layout_group
                                                         : ui_dat2_pick_layout_group(task);
        revconfig_ui_build_tree(&task->build, task->tree, group);
        if( task->game && task->tree->component_count > 0 )
            GameRunescape_SetUITreeReady(task->game, true);
    }

    task->ui_ready = true;

    PT_END(&task->thread);
}
