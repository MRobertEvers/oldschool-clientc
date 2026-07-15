#include "task_instance_revconfig_load.h"

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat2_buildcache.h"
#include "core/tapi/tapi_config.h"
#include "games/runescape.h"
#include "revconfig/revconfig_load.h"
#include "task_instance_on_rc.h"
#include "toriauxlib/core/tasks/dat1/task_dat1_font_load.h"
#include "toriauxlib/core/tasks/dat1/task_dat1_sprite_load.h"
#include "toriauxlib/core/tasks/dat2/task_dat2_font_load.h"
#include "toriauxlib/core/tasks/dat2/task_dat2_sprite_load.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toridraw/toridraw_scene.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
Task_InstanceRevConfigLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_InstanceRevConfigLoad* task =
        LibToriRS_container_of(base, struct Task_InstanceRevConfigLoad, base);

    PT_BEGIN(&task->pt);

    task->items = revconfig_item_buffer_new(256);
    assert(task->items);

    for( task->config_fetch_index = 0; task->config_fetch_index < task->config_file_count;
         task->config_fetch_index++ )
    {
        TAPIConfig_FetchRevconfig(ctx, task->config_files[task->config_fetch_index]);
    }
    PT_YIELD(&task->pt);

    {
        struct RevConfigBuffer* fields = revconfig_buffer_new(1024);
        if( fields )
        {
            for( int i = 0; i < task->config_file_count; i++ )
            {
                void* data = NULL;
                int size = TAPIConfig_DecodeRevconfig(ctx, &data);
                if( size > 0 && data )
                {
                    revconfig_load_fields_from_ini_bytes(data, (size_t)size, fields);
                    free(data);
                }
            }
            revconfig_items_build(fields, task->items);
            revconfig_buffer_free(fields);
        }
    }

    task->rc_ctx.inv_pool = uitree_inv_pool_new(8);
    task->rc_ctx.rs_capture_enabled = true;

    for( task->item_index = 0; task->item_index < (int)task->items->item_count; task->item_index++ )
    {
        struct RevConfigItem* it = &task->items->items[task->item_index];
        struct LibToriRS_Task* child = NULL;

        if( it->kind == RCITEM_CACHE_SPRITE )
        {
            if( task->rc_ctx.cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
                child = Task_Dat1SpriteLoad_New(&task->rc_ctx, task->cache, &it->u.cache);
            else
                child = Task_Dat2SpriteLoad_New(&task->rc_ctx, task->cache, &it->u.cache);
        }
        else if( it->kind == RCITEM_CACHE_FONT )
        {
            if( task->rc_ctx.cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
                child = Task_Dat1FontLoad_New(&task->rc_ctx, task->cache, &it->u.font);
            else
                child = Task_Dat2FontLoad_New(&task->rc_ctx, task->cache, &it->u.font);
        }
        else if( it->kind == RCITEM_UICOMPONENT )
        {
            child = Task_InstanceOnRCUIComponent_New(&task->rc_ctx, task->cache, &it->u.uicomponent);
        }
        else if( it->kind == RCITEM_UILAYOUT )
        {
            child = Task_InstanceOnRCUILayout_New(&task->rc_ctx, &it->u.uilayout);
        }
        else if( it->kind == RCITEM_INV )
        {
            child = Task_InstanceOnRCInv_New(&task->rc_ctx, task->cache, &it->u.inv);
        }

        if( child )
            TASK_AWAITEX(&task->pt, ctx, child);
    }

    task->rc_ctx.revconfig_ingest_complete = true;

    bool const tree_built = instance_revconfig_build_tree(&task->rc_ctx);

    if( task->game )
        GameRunescape_SetUIInvPool(task->game, task->rc_ctx.inv_pool);
    task->rc_ctx.inv_pool = NULL;

    ToriDraw_SceneSpritesReemitLoads(task->scene);
    ToriDraw_SceneFontsReemitLoads(task->scene);

    instance_revconfig_assert_fonts_in_scene(&task->rc_ctx);
    instance_revconfig_assert_sprites_in_scene(&task->rc_ctx);

    if( task->game )
        instance_revconfig_attach_scrollbar_sprites(&task->rc_ctx, task->game);

    if( task->game )
    {
        task->game->ui_sprites_synced = false;
        task->game->ui_fonts_synced = false;
        GameRunescape_SyncUISpritesFromScene(task->game);
        GameRunescape_SetUITreeReady(task->game, tree_built);
    }

    instance_revconfig_context_release_build_state(&task->rc_ctx);

    task->ui_ready = true;
    PT_END(&task->pt);
}

static void
Task_InstanceRevConfigLoad_FreeImpl(struct LibToriRS_Task* base)
{
    struct Task_InstanceRevConfigLoad* task =
        LibToriRS_container_of(base, struct Task_InstanceRevConfigLoad, base);

    if( !task )
        return;

    revconfig_item_buffer_free(task->items);
    if( task->rc_ctx.inv_pool )
        uitree_inv_pool_free(task->rc_ctx.inv_pool);
    free(task);
}

static struct LibToriRS_TaskVTable g_task_instance_revconfig_load_vtable = {
    .run_fn = Task_InstanceRevConfigLoad_Run,
    .free_fn = Task_InstanceRevConfigLoad_FreeImpl,
};

struct Task_InstanceRevConfigLoad*
Task_InstanceRevConfigLoad_FromTask(struct LibToriRS_Task* base)
{
    return base ? LibToriRS_container_of(base, struct Task_InstanceRevConfigLoad, base) : NULL;
}

struct LibToriRS_Task*
Task_InstanceRevConfigLoad_New(
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct UITree* tree,
    struct GameRunescape* game,
    char const* const* config_files,
    int config_file_count,
    char const* layout_group)
{
    struct Task_InstanceRevConfigLoad* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;

    task->base.vtable = &g_task_instance_revconfig_load_vtable;
    task->cache = cache;
    task->scene = scene;
    task->tree = tree;
    task->game = game;
    task->layout_group = layout_group ? layout_group : "fixed";
    PT_INIT(&task->pt);

    if( config_files && config_file_count > 0 )
    {
        if( config_file_count > INSTANCE_RC_MAX_CONFIG_FILES )
            config_file_count = INSTANCE_RC_MAX_CONFIG_FILES;
        task->config_file_count = config_file_count;
        for( int i = 0; i < config_file_count; i++ )
            task->config_files[i] = config_files[i];
    }

    instance_revconfig_context_init(&task->rc_ctx);
    task->rc_ctx.cache = cache;
    task->rc_ctx.scene = scene;
    task->rc_ctx.tree = tree;
    task->rc_ctx.game = game;
    task->rc_ctx.td = game ? game->td : NULL;
    task->rc_ctx.cache_mode = ToriAuxLibCache_Mode(cache);
    task->rc_ctx.core = ToriAuxLibCache_Core(cache);
    task->rc_ctx.layout_group = task->layout_group;
    if( task->rc_ctx.cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
        task->rc_ctx.dat1_bc = dat1(cache);
    else
        task->rc_ctx.dat2_bc = dat2(cache);

    return &task->base;
}

void
Task_InstanceRevConfigLoad_Free(struct LibToriRS_Task* base)
{
    Task_InstanceRevConfigLoad_FreeImpl(base);
}

bool
Task_InstanceRevConfigLoad_IsReady(struct LibToriRS_Task* base)
{
    struct Task_InstanceRevConfigLoad* task = Task_InstanceRevConfigLoad_FromTask(base);
    return task && task->ui_ready;
}

int
Task_InstanceRevConfigLoad_RunCompat(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct LibToriRS_Task* base = task_state;
    if( !base || !base->vtable || !base->vtable->run_fn )
        return PT_EXITED;
    return base->vtable->run_fn(base, ctx);
}
