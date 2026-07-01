#include "task_instance_revconfig_load.h"

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat2_buildcache.h"
#include "core/tapi/tapi_config.h"
#include "core_task_await.h"
#include "games/runescape.h"
#include "revconfig/revconfig_load.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toridraw/toridraw_scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_InstanceRevConfigLoad*
Task_InstanceRevConfigLoad_New(
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct UITree* tree,
    struct GameRunescape* game,
    char const* const* config_files,
    int config_file_count,
    char const* layout_group)
{
    struct Task_InstanceRevConfigLoad* task = calloc(1, sizeof(struct Task_InstanceRevConfigLoad));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->cache = cache;
    task->scene = scene;
    task->tree = tree;
    task->game = game;
    task->layout_group = layout_group ? layout_group : "fixed";

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

    return task;
}

void
Task_InstanceRevConfigLoad_Free(struct Task_InstanceRevConfigLoad* task)
{
    if( !task )
        return;
    revconfig_item_buffer_free(task->items);
    if( task->rc_ctx.inv_pool )
        uitree_inv_pool_free(task->rc_ctx.inv_pool);
    free(task);
}

bool
Task_InstanceRevConfigLoad_IsReady(struct Task_InstanceRevConfigLoad* task)
{
    return task && task->ui_ready;
}

int
Task_InstanceRevConfigLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_InstanceRevConfigLoad* task = task_state;

    PT_BEGIN(&task->thread);

    task->items = revconfig_item_buffer_new(256);
    assert(task->items);

    for( task->config_fetch_index = 0; task->config_fetch_index < task->config_file_count;
         task->config_fetch_index++ )
    {
        TAPIConfig_FetchRevconfig(ctx, task->config_files[task->config_fetch_index]);
    }
    PT_YIELD(&task->thread);

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
        task->sub_run = NULL;
        task->sub_state = NULL;

        switch( it->kind )
        {
        case RCITEM_CACHE_SPRITE:
            Task_InstanceOnRCCacheSprite_Init(
                &task->handler.on_cache_sprite, &task->rc_ctx, task->cache, &it->u.cache);
            task->sub_run = Task_InstanceOnRCCacheSprite_Run;
            task->sub_state = &task->handler.on_cache_sprite;
            break;
        case RCITEM_CACHE_FONT:
            Task_InstanceOnRCCacheFont_Init(
                &task->handler.on_cache_font, &task->rc_ctx, task->cache, &it->u.font);
            task->sub_run = Task_InstanceOnRCCacheFont_Run;
            task->sub_state = &task->handler.on_cache_font;
            break;
        case RCITEM_UICOMPONENT:
            Task_InstanceOnRCUIComponent_Init(
                &task->handler.on_uicomponent, &task->rc_ctx, task->cache, &it->u.uicomponent);
            task->sub_run = Task_InstanceOnRCUIComponent_Run;
            task->sub_state = &task->handler.on_uicomponent;
            break;
        case RCITEM_UILAYOUT:
            Task_InstanceOnRCUILayout_Init(
                &task->handler.on_uilayout, &task->rc_ctx, &it->u.uilayout);
            task->sub_run = Task_InstanceOnRCUILayout_Run;
            task->sub_state = &task->handler.on_uilayout;
            break;
        case RCITEM_INV:
            Task_InstanceOnRCInv_Init(
                &task->handler.on_inv, &task->rc_ctx, task->cache, &it->u.inv);
            task->sub_run = Task_InstanceOnRCInv_Run;
            task->sub_state = &task->handler.on_inv;
            break;
        default:
            break;
        }

        if( task->sub_run )
            TASK_AWAIT(&task->thread, task->sub_run(task->sub_state, ctx));
    }

    instance_revconfig_build_tree(&task->rc_ctx);

    if( task->game )
        GameRunescape_SetUIInvPool(task->game, task->rc_ctx.inv_pool);
    task->rc_ctx.inv_pool = NULL;

    ToriDraw_SceneSpritesReemitLoads(task->scene);
    ToriDraw_SceneFontsReemitLoads(task->scene);

    instance_revconfig_assert_fonts_in_scene(&task->rc_ctx);

    if( task->game )
    {
        task->game->ui_sprites_synced = false;
        GameRunescape_SyncUISpritesFromScene(task->game);
        GameRunescape_SetUITreeReady(task->game, true);
    }

    instance_revconfig_context_release_build_state(&task->rc_ctx);

    task->ui_ready = true;
    PT_END(&task->thread);
}
