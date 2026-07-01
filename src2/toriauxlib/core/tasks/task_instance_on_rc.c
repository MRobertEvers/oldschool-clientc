#include "task_instance_on_rc.h"

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "core/tapi/tapi_dat1.h"
#include "core/tapi/tapi_dat2.h"
#include "core_task_await.h"
#include "task_rs_component_load.h"
#include "task_rs_inv_load.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "toriauxlib/c/toriauxlibcache_font_convert.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "ui/ui_font_lookup.h"
#include "ui/uitree_layout.h"
#include "games/runescape.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_scene.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char const*
toriauxlibcore_component_type_name(enum ToriAuxLibCore_ComponentType type)
{
    switch( type )
    {
    case TORIAUXLIBCORE_COMPONENT_LAYER:
        return "layer";
    case TORIAUXLIBCORE_COMPONENT_INV:
        return "inv";
    case TORIAUXLIBCORE_COMPONENT_RECT:
        return "rect";
    case TORIAUXLIBCORE_COMPONENT_TEXT:
        return "text";
    case TORIAUXLIBCORE_COMPONENT_GRAPHIC:
        return "graphic";
    case TORIAUXLIBCORE_COMPONENT_MODEL:
        return "model";
    case TORIAUXLIBCORE_COMPONENT_INV_TEXT:
        return "inv_text";
    default:
        return "unknown";
    }
}

static char const*
dat1_component_type_name(int type)
{
    switch( type )
    {
    case COMPONENT_TYPE_LAYER:
        return "layer";
    case COMPONENT_TYPE_INV:
        return "inv";
    case COMPONENT_TYPE_RECT:
        return "rect";
    case COMPONENT_TYPE_TEXT:
        return "text";
    case COMPONENT_TYPE_GRAPHIC:
        return "graphic";
    case COMPONENT_TYPE_MODEL:
        return "model";
    case COMPONENT_TYPE_INV_TEXT:
        return "inv_text";
    default:
        return "unknown";
    }
}

static void
task_on_rc_log_sidebar_inv_load_failure(
    struct Task_InstanceOnRCUIComponent const* task,
    struct InstanceRevConfigRSSubtree const* subtree)
{
    struct ToriAuxLibCore* core = task->cache ? ToriAuxLibCache_Core(task->cache) : NULL;

    fprintf(stderr, "sidebar RS component load did not capture an inventory component\n");
    fprintf(
        stderr,
        "  owner=%s type=%s componentno=%d inv=%s components_walked=%d\n",
        task->item.name[0] != '\0' ? task->item.name : "(unnamed)",
        task->item.type,
        task->item.componentno,
        task->item.inv[0] != '\0' ? task->item.inv : "(none)",
        task->rs_load ? task->rs_load->components_walked : -1);
    fprintf(
        stderr,
        "  rs_subtree items=%d core=%p rc_ctx=%p\n",
        subtree ? subtree->item_count : 0,
        (void*)core,
        (void*)task->rc_ctx);

    if( task->rc_ctx && task->item.componentno >= 0 && task->item.componentno < 1024 )
    {
        fprintf(
            stderr,
            "  panel_root_id[%d]=%d\n",
            task->item.componentno,
            task->rc_ctx->panel_root_id[task->item.componentno]);
    }

    if( task->rc_ctx && task->rc_ctx->dat1_bc && task->item.componentno >= 0 )
    {
        struct RSCacheDat1A_ConfigComponentList* ifaces =
            dat1_buildcache_get_interfaces(task->rc_ctx->dat1_bc);
        if( ifaces && task->item.componentno < ifaces->components_count )
        {
            struct RSCacheDat1A_ConfigComponent* direct =
                ifaces->components[task->item.componentno];
            if( direct )
            {
                fprintf(
                    stderr,
                    "  dat1 direct[%d]: id=%d type=%s(%d) layer=%d size=%dx%d children=%d\n",
                    task->item.componentno,
                    direct->id,
                    dat1_component_type_name(direct->type),
                    direct->type,
                    direct->layer,
                    direct->width,
                    direct->height,
                    direct->children_count);
                if( direct->layer >= 0 && ifaces )
                {
                    struct RSCacheDat1A_ConfigComponent* layer_parent = NULL;
                    if( direct->layer < ifaces->components_count )
                        layer_parent = ifaces->components[direct->layer];
                    if( !layer_parent )
                    {
                        for( int j = 0; j < ifaces->components_count; j++ )
                        {
                            if( ifaces->components[j] &&
                                ifaces->components[j]->id == direct->layer )
                            {
                                layer_parent = ifaces->components[j];
                                break;
                            }
                        }
                    }
                    if( layer_parent )
                    {
                        fprintf(
                            stderr,
                            "  dat1 layer_parent[%d]: id=%d type=%s(%d) size=%dx%d children=%d\n",
                            direct->layer,
                            layer_parent->id,
                            dat1_component_type_name(layer_parent->type),
                            layer_parent->type,
                            layer_parent->width,
                            layer_parent->height,
                            layer_parent->children_count);
                    }
                }
            }
            else
            {
                fprintf(
                    stderr,
                    "  dat1 direct[%d]: missing (components_count=%d)\n",
                    task->item.componentno,
                    ifaces->components_count);
            }
        }
    }

    if( subtree )
    {
        for( int i = 0; i < subtree->item_count; i++ )
        {
            int component_id = subtree->component_ids[i];
            struct ToriAuxLibCore_Component* info =
                core ? ToriAuxLibCore_ComponentGet(core, component_id) : NULL;
            if( info )
            {
                fprintf(
                    stderr,
                    "  rs_subtree[%d]: id=%d core_type=%s(%d) size=%dx%d parent_id=%d\n",
                    i,
                    component_id,
                    toriauxlibcore_component_type_name(info->type),
                    (int)info->type,
                    info->width,
                    info->height,
                    info->parent_id);
            }
            else
            {
                fprintf(
                    stderr,
                    "  rs_subtree[%d]: id=%d core=missing\n",
                    i,
                    component_id);
            }
        }
    }

    fprintf(
        stderr,
        "  expected at least one TORIAUXLIBCORE_COMPONENT_INV in rs_subtree capture\n");
    fprintf(
        stderr,
        "  likely error: inv= requires a COMPONENT_TYPE_INV in the walked RS interface, but none "
        "was found\n");
    fprintf(
        stderr,
        "  the RS walk succeeded (see rs_subtree above); panel chrome may have loaded but this "
        "interface has no inventory grid widget\n");
    fprintf(
        stderr,
        "  common causes: wrong componentno in revconfig (interface is text/layer chrome only), "
        "or componentno should be -1 and the real interface assigned at runtime via IF_SETTAB "
        "(see rev_osrs_ui.ini sidebar_tab_3)\n");
    fprintf(
        stderr,
        "  fix: set componentno to an interface that contains COMPONENT_TYPE_INV, or use "
        "componentno=-1 until IF_SETTAB wiring is implemented\n");
}

static void
on_rc_uicomponent_rs_loaded(
    void* user,
    int component_id,
    int parent_id,
    int rel_x,
    int rel_y)
{
    struct Task_RSComponentLoad* rs_task = user;
    assert(rs_task && rs_task->rc_ctx && rs_task->owner_component[0] != '\0' && component_id >= 0);

    struct InstanceRevConfigRSSubtree* subtree = instance_revconfig_rs_subtree_get_or_create(
        rs_task->rc_ctx, rs_task->owner_component);
    assert(subtree);
    instance_revconfig_rs_subtree_append(subtree, component_id, parent_id, rel_x, rel_y);
}

static void
instance_revconfig_register_core_sprite(
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

static void
instance_revconfig_register_core_font(
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

    ui_font_lookup_add(
        &ctx->font_lookup, lookup_name, font_id, cache_font_id, cache_archive_id);
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
            struct ToriAuxLibCore_Sprite* existing =
                ToriAuxLibCore_SpriteGet(core, task->element_id);
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
            instance_revconfig_register_core_sprite(
                task->rc_ctx, task->element_id, sprite, task->item.name);
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
        assert(sprite_archive && "failed to decode revconfig cache sprite archive");

        struct ToriAuxLibCore_Sprite* sprite =
            dat2_buildcache_sprite_decode_from_archive(sprite_archive, &task->item);
        assert(
            sprite && sprite->frame_count > 0 && task->rc_ctx->game &&
            "failed to decode revconfig cache sprite from archive");
        instance_revconfig_register_core_sprite(
            task->rc_ctx, task->element_id, sprite, task->item.name);
        (void)core;
    }

    PT_END(&task->thread);
}

void
Task_InstanceOnRCCacheFont_Init(
    struct Task_InstanceOnRCCacheFont* task,
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigFontItem const* item)
{
    assert(item);
    memset(task, 0, sizeof(*task));
    PT_INIT(&task->thread);
    task->rc_ctx = rc_ctx;
    task->cache = cache;
    task->item = *item;
}

int
Task_InstanceOnRCCacheFont_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_InstanceOnRCCacheFont* task = task_state;

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

        if( !task->rc_ctx->title_fonts_ready )
        {
            IO_REQUEST(ctx, 0, TAPIDat1_FetchTitleFontsJagfile(ctx));
            PT_YIELD(&task->thread);
            struct RSCacheShared_FileListDat* title =
                TAPIDat1_DecodeTitleFontsJagfile(ctx, 0);
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
        instance_revconfig_register_core_font(
            task->rc_ctx,
            task->font_id,
            font,
            task->item.name,
            task->item.cache_font_id,
            -1);
    }
    else if( ToriAuxLibCache_Mode(task->cache) == TORIAUXLIBCACHE_MODE_DAT2 )
    {
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
            PT_EXIT(&task->thread);

        IO_REQUEST(ctx, 0, TAPIDat2_FetchFont(ctx, task->item.archive_id));
        IO_REQUEST(ctx, 1, TAPIDat2_FetchSprite(ctx, task->item.archive_id));
        PT_YIELD(&task->thread);

        struct RSCacheDat2Disk_Archive* font_archive =
            TAPIDat2_DecodeFontArchive(ctx, 0, task->item.archive_id);
        struct RSCacheDat2Disk_Archive* sprite_archive =
            TAPIDat2_DecodeSpriteArchive(ctx, 1, task->item.archive_id);
        if( !font_archive || !sprite_archive )
        {
            fprintf(
                stderr,
                "Task_InstanceOnRCCacheFont: failed to fetch font archives archive_id=%d "
                "(font=%p sprite=%p)\n",
                task->item.archive_id,
                (void*)font_archive,
                (void*)sprite_archive);
            RSCacheDat2Disk_ArchiveFree(font_archive);
            RSCacheDat2Disk_ArchiveFree(sprite_archive);
            PT_EXIT(&task->thread);
        }

        struct ToriAuxLibCore_Font* font = ToriAuxLibCache_FontNewFromDat2Archives(
            font_archive, sprite_archive, task->item.archive_id);
        if( !font )
        {
            fprintf(
                stderr,
                "Task_InstanceOnRCCacheFont: failed to decode font archive_id=%d name=%s\n",
                task->item.archive_id,
                task->item.name);
            PT_EXIT(&task->thread);
        }
        instance_revconfig_register_core_font(
            task->rc_ctx,
            task->font_id,
            font,
            task->item.name,
            task->item.cache_font_id,
            task->item.archive_id);
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

    PT_BEGIN(&task->thread);

    if( !task->rc_ctx || task->item.name[0] == '\0' )
        PT_EXIT(&task->thread);

    if( task->rc_ctx->component_count >= INSTANCE_RC_MAX_COMPONENTS )
        PT_EXIT(&task->thread);

    task->rc_ctx->components[task->rc_ctx->component_count++] = task->item;

    if( revconfig_uicomponent_needs_rs_load(&task->item) && task->cache )
    {
        task->rs_load = Task_RSComponentLoad_New(
            ToriAuxLibCache_Mode(task->cache),
            task->cache,
            task->rc_ctx->scene,
            task->rc_ctx,
            task->item.componentno,
            NULL);
        assert(task->rs_load);
        strncpy(
            task->rs_load->owner_component,
            task->item.name,
            sizeof(task->rs_load->owner_component) - 1);
        if( strcmp(task->item.type, "sidebar") == 0 )
        {
            task->rs_load->walk_parent_w = UITREE_SIDEBAR_PANEL_W;
            task->rs_load->walk_parent_h = UITREE_SIDEBAR_PANEL_H;
        }
        task->rs_load->callbacks.user = task->rs_load;
        task->rs_load->callbacks.on_component = on_rc_uicomponent_rs_loaded;

        TASK_AWAIT(&task->thread, Task_RSComponentLoad_Run(task->rs_load, ctx));

        if( task->item.componentno >= 0 )
        {
            struct InstanceRevConfigRSSubtree* subtree =
                instance_revconfig_rs_subtree_find(task->rc_ctx, task->item.name);
            bool const panel_invalid =
                ToriAuxLibCache_Mode(task->cache) == TORIAUXLIBCACHE_MODE_DAT1 &&
                task->item.componentno < 1024 &&
                task->rc_ctx->panel_root_id[task->item.componentno] ==
                    INSTANCE_RC_PANEL_ROOT_INVALID;
            if( strcmp(task->item.type, "sidebar") == 0 && !panel_invalid )
            {
                assert(
                    subtree && subtree->item_count > 0 &&
                    "sidebar RS component load produced empty rs_subtree");
            }
            else if( !panel_invalid && (!subtree || subtree->item_count <= 0) )
            {
                fprintf(
                    stderr,
                    "RS component load produced empty rs_subtree for %s\n",
                    task->item.name);
            }

            if( strcmp(task->item.type, "sidebar") == 0 && task->item.inv[0] != '\0' &&
                !panel_invalid && subtree && subtree->item_count > 0 &&
                ToriAuxLibCache_Mode(task->cache) != TORIAUXLIBCACHE_MODE_DAT2 )
            {
                bool found_inv = false;
                struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->cache);
                for( int i = 0; i < subtree->item_count; i++ )
                {
                    struct ToriAuxLibCore_Component* info =
                        core ? ToriAuxLibCore_ComponentGet(core, subtree->component_ids[i]) : NULL;
                    if( info && info->type == TORIAUXLIBCORE_COMPONENT_INV )
                    {
                        found_inv = true;
                        break;
                    }
                }
                if( !found_inv )
                    task_on_rc_log_sidebar_inv_load_failure(task, subtree);
            }
        }

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
