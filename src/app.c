#include "app.h"

#include "cs2vm2/cs2vm2.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/uitree_cmd_render.h"
#include "game/rs_cs2_dispatch.h"
#include "platform/platform_sdl2_renderer_soft3d.h"
#include "render/torirs_frame.h"
#include "toridraw.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    APP_LOGIC_TICK_MS = 20,
    APP_MAX_CATCHUP_TICKS = 5,
};

static int
app_host_request(void* user, struct UITreeHostRequest* req)
{
    struct App* app = (struct App*)user;
    struct InvSlot slot;

    assert(req);
    assert(app);

    switch( req->kind )
    {
    case UITREE_HOST_GET_SCROLLBAR_SCENE:
        return UITreeSceneBridge_ScrollbarSceneId(&app->bridge);
    case UITREE_HOST_GET_INV_SOURCE_SLOT:
        assert(req->u.get_inv_source_slot.out);
        if( !InvManager_GetSlot(
                &app->invs,
                req->u.get_inv_source_slot.source_id,
                req->u.get_inv_source_slot.slot,
                &slot) )
            return 0;
        req->u.get_inv_source_slot.out->obj_id = slot.obj_id;
        req->u.get_inv_source_slot.out->obj_count = slot.obj_count;
        req->u.get_inv_source_slot.out->scene_id = slot.scene_id;
        req->u.get_inv_source_slot.out->atlas_index = slot.atlas_index;
        return 1;
    default:
        return 0;
    }
}

/* Demo content until real state sync exists: seed the worn/backpack/bank
 * containers so item-bearing interfaces have something to show. */
static void
seed_inv_defaults(struct InvManager* invs)
{
    static int const k_worn_items[] = { 1153, 1007, 1725, 1333, 1115, 1201,
                                        1189, 1063, 1067, 2564, 882 };
    static int const k_backpack_items[] = { 1333 };
    /* Bank contents so interface 12 renders its item grid + tab row. */
    static int const k_bank_items[] = {
        995,  1333, 1153, 1007, 1725, 1115, 1201, 1189, 1063, 1067, 2564, 882,
        4151, 1305, 1319, 1215, 1231, 1147, 1163, 1079, 1093, 861,  1163, 1704,
        2550, 6585, 1725, 3105, 1387, 1275, 1291, 4587, 1215, 1333, 995,  1038 };

    assert(invs);
    assert(InvManager_ResolveSource(invs, INV_MANAGER_SOURCE_NAME_WORN) >= 0);
    assert(InvManager_ResolveSource(invs, INV_MANAGER_SOURCE_NAME_BACKPACK) >= 0);

    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_WORN,
        k_worn_items,
        NULL,
        (int)(sizeof(k_worn_items) / sizeof(k_worn_items[0]))));
    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_BACKPACK,
        k_backpack_items,
        NULL,
        (int)(sizeof(k_backpack_items) / sizeof(k_backpack_items[0]))));
    assert(InvManager_EnsureContainer(invs, INV_MANAGER_CONTAINER_BANK, 800, "bank") >= 0);
    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_BANK,
        k_bank_items,
        NULL,
        (int)(sizeof(k_bank_items) / sizeof(k_bank_items[0]))));
}

/* Scene models reference textures by face id, but the ToriDraw texture map
 * starts empty (reference: textures load on demand and faces skip-render
 * until they land). Collect ids the scene is missing, load them through the
 * async pipeline, and publish into the scene. Ids that fail stay marked in
 * the bridge and are never re-requested. */
static void
app_sync_textures(struct App* app)
{
    int ids[256];
    int id_count;
    int queued = 0;

    id_count = UITreeSceneBridge_CollectMissingTextures(&app->bridge, ids, 256);
    if( id_count == 0 )
        return;

    for( int i = 0; i < id_count; i++ )
    {
        struct ToriRS_Task* task = CreateTask_TextureLoad(app->provider, ids[i]);
        if( task )
        {
            ToriRS_TaskQueue_Add(app->runner.queue, task);
            queued++;
        }
    }
    if( queued )
        TaskRunner_Drain(&app->runner);

    if( UITreeSceneBridge_PublishTextures(&app->bridge, ids, id_count) )
        app->need_redraw = 1;
}

void
App_Init(
    struct App* app,
    struct AppConfig const* cfg)
{
    assert(app);
    assert(cfg);
    memset(app, 0, sizeof(*app));
    app->cfg = *cfg;

    ToriDraw_Init();

    /* Phase 1: task runtime + disk. The runner owns the async pipeline every
     * other phase loads through. */
    app->runner.io = ToriRS_IO_New();
    app->runner.queue = ToriRS_TaskQueue_New();
    app->disk = RSCache_Dat2DiskNewFromDirectory(cfg->cache_dir);
    assert(app->disk != NULL);
    app->runner.px = PlatformX_IO_New();
    assert(app->runner.px != NULL);
    PlatformX_IO_InitDat2Disk(app->runner.px, app->disk);
    PlatformX_IO_InitConfigPath(app->runner.px, cfg->config_dir);
    PlatformX_IO_InitScriptPath(app->runner.px, cfg->script_dir);

    /* Phase 2: asset pipeline (provider is a view over the build cache). */
    app->bc = dat2_buildcache_new();
    app->provider = dat2_buildcache_as_provider(app->bc);

    /* Phase 3: renderer scene + id bridge (bridge needs scene + provider). */
    app->scene = ToriDraw_SceneNew(0);
    assert(app->scene);
    UITreeSceneBridge_Init(&app->bridge, app->scene, app->provider);

    /* Phase 4: game state (host needs tree + provider + invs + varps, then
     * the bridge for icon rasterization). */
    app->tree = UITree_New(256);
    assert(app->tree);
    InvManager_Init(&app->invs);
    VarPManager_Init(&app->varps);
    seed_inv_defaults(&app->invs);
    RS_CS2Host_Init(&app->host, app->tree, app->provider, &app->invs, &app->varps);
    RS_CS2Host_SetBridge(&app->host, &app->bridge);

    /* Phase 5: frame state. */
    UITree_EmitBufferInit(&app->emit);
    UITree_HostInit(&app->ui_host);
    app->ui_host.user = app;
    app->ui_host.request = app_host_request;
    UIInteraction_Init(&app->interact);
    app->hover_com_id = -1;
    app->clicked_com_id = -1;
    app->need_redraw = 1;
}

void
App_Shutdown(struct App* app)
{
    assert(app);
    UITree_EmitBufferFree(&app->emit);
    VarPManager_Free(&app->varps);
    InvManager_Free(&app->invs);
    UITree_Free(app->tree);
    UITreeSceneBridge_Free(&app->bridge);
    ToriDraw_SceneFree(app->scene);
    dat2_buildcache_free(app->bc);
    PlatformX_IO_Free(app->runner.px);
    RSCache_Dat2DiskFree(app->disk);
    ToriRS_TaskQueue_Free(app->runner.queue);
    ToriRS_IO_Free(app->runner.io);
}

void
App_OpenRootInterface(
    struct App* app,
    int interface_id)
{
    assert(app);
    memset(&app->open_stats, 0, sizeof(app->open_stats));

    if( getenv("TORIRS_CS2_TRACE") )
        g_cs2_trace_mode = 2;

    /* Open the requested interface directly as the tree root (TS parity:
     * WidgetManager.setRootInterface(groupId) — any group can be the root; no
     * hardcoded 161 chrome required). */
    {
        struct ToriRS_Task* root_task = CreateTask_InterfaceOpen(
            app->provider,
            app->tree,
            &app->host,
            &app->invs,
            &app->bridge,
            interface_id,
            &app->open_stats);
        assert(root_task != NULL);
        ToriRS_TaskQueue_Add(app->runner.queue, root_task);
        TaskRunner_Drain(&app->runner);
    }

    printf(
        "InterfaceOpen done: iface=%d pack_components=%d tree_components=%u onloads=%d "
        "inv_hooks=%d var_hooks=%d mounts=%d\n",
        app->open_stats.interface_id,
        app->open_stats.pack_component_count,
        app->tree->component_count,
        app->open_stats.onload_count,
        app->host.inv_transmit_hook_count,
        app->host.var_transmit_hook_count,
        app->tree->interface_parent_count);

    if( getenv("TORIRS_ANIM_DEBUG") )
    {
        for( uint32_t i = 0; i < app->tree->component_count; i++ )
        {
            struct UITreeComponent const* node = &app->tree->components[i];
            if( node->freed || node->type != UIELEM_RS_MODEL )
                continue;
            fprintf(
                stderr,
                "anim_debug: com=0x%x model=%d seq=%d\n",
                node->component_id,
                node->u.rs_model.gamecache_model_id,
                node->u.rs_model.anim_seq_id);
        }
    }

    /* Load model-widget sequences and apply the first frame before the
     * initial render; App_RunOnce advances frames each tick. */
    UITreeAnim_RequestMissing(
        app->tree, app->scene, app->provider, app->runner.queue, &app->seq_loads);
    TaskRunner_Drain(&app->runner);
    UITreeAnim_Advance(app->tree, app->scene, 0);

    app_sync_textures(app);

    app->emit.count = 0;
    UITree_EmitWalk(app->tree, &app->ui_host, &app->emit, -1);
    app->need_redraw = 1;
}

/* One 20ms client tick: clock, widget timers, animation loads + advance. */
static int
app_logic_tick(struct App* app)
{
    int redraw = 0;

    RS_CS2Host_Tick(&app->host);

    /* onTimer fires once per client tick for every component with a timer
     * hook (reference processWidgetTimers). Component ids are snapshotted
     * first because a hook can CC_CREATE children and realloc the array. */
    {
        int timer_ids[256];
        int timer_n = 0;
        for( uint32_t ti = 0; ti < app->tree->component_count && timer_n < 256; ti++ )
        {
            struct UITreeComponent const* tc = &app->tree->components[ti];
            if( tc->component_id >= 0 && tc->runtime_hooks.on_timer.script_id > 0 )
                timer_ids[timer_n++] = tc->component_id;
        }
        for( int i = 0; i < timer_n; i++ )
        {
            int32_t idx = UITree_FindByComponentId(app->tree, timer_ids[i]);
            if( idx < 0 )
                continue;
            RS_CS2_DispatchHook(
                &app->host,
                &app->runner,
                timer_ids[i],
                &app->tree->components[idx].runtime_hooks.on_timer);
            redraw = 1;
        }
    }

    /* Widgets-loaded transmit traversal, once per tick (TS processWidgetTransmits).
     * Early-outs unless a widget was unhidden this tick; per-hook serial gating
     * means already-fired hooks run nothing. */
    RS_CS2_PumpTransmits(&app->host, &app->runner);

    /* TORIRS_STATS=1: periodic growth diagnostics — component_count must stay
     * flat under the CC_DELETEALL/CC_CREATE rebuild pattern (reclamation). */
    {
        static int stats_enabled = -1;
        static int stats_tick = 0;
        if( stats_enabled < 0 )
            stats_enabled = getenv("TORIRS_STATS") != NULL;
        if( stats_enabled && ++stats_tick % 250 == 0 )
            fprintf(
                stderr,
                "torirs_stats: tick=%d components=%u free_head=%d inv_hooks=%d var_hooks=%d\n",
                stats_tick,
                app->tree->component_count,
                app->tree->free_head,
                app->host.inv_transmit_hook_count,
                app->host.var_transmit_hook_count);
    }

    {
        static int anim_dbg = -1;
        static int anim_dbg_tick = 0;
        if( anim_dbg < 0 )
            anim_dbg = getenv("TORIRS_ANIM_DEBUG") != NULL;
        if( anim_dbg && ++anim_dbg_tick % 25 == 0 )
        {
            for( uint32_t i = 0; i < app->tree->component_count; i++ )
            {
                struct UITreeComponent const* node = &app->tree->components[i];
                if( node->freed || node->type != UIELEM_RS_MODEL )
                    continue;
                if( node->u.rs_model.anim_seq_id < 0 )
                    continue;
                fprintf(
                    stderr,
                    "anim_tick t=%d com=0x%x seq=%d frame=%d\n",
                    anim_dbg_tick,
                    node->component_id,
                    node->u.rs_model.anim_seq_id,
                    node->u.rs_model.anim_frame);
            }
        }
    }

    /* Animations: request missing sequences (async), apply what's loaded.
     * In-flight sequences render at rest pose until they land. */
    UITreeAnim_RequestMissing(
        app->tree, app->scene, app->provider, app->runner.queue, &app->seq_loads);
    TaskRunner_Drain(&app->runner);
    if( UITreeAnim_Advance(app->tree, app->scene, 1) )
        redraw = 1;

    /* CS2 hooks this tick may have ensured new textured models. */
    app_sync_textures(app);

    return redraw;
}

int
App_RunOnce(
    struct App* app,
    uint64_t now_ms,
    struct LibToriRS_Input* input)
{
    struct UIInteractOut out;
    int ran_cs2 = 0;

    assert(app);
    assert(input);

    /* Logic ticks at 20ms with bounded catch-up after a stall. */
    if( app->last_logic_ms == 0 )
        app->last_logic_ms = now_ms;
    {
        int ticks = (int)((now_ms - app->last_logic_ms) / APP_LOGIC_TICK_MS);
        if( ticks > 0 )
        {
            app->last_logic_ms += (uint64_t)ticks * APP_LOGIC_TICK_MS;
            if( ticks > APP_MAX_CATCHUP_TICKS )
                ticks = APP_MAX_CATCHUP_TICKS;
            for( int t = 0; t < ticks; t++ )
            {
                if( app_logic_tick(app) )
                {
                    app->need_redraw = 1;
                    ran_cs2 = 1;
                }
            }
        }
    }

    /* Per-frame interaction: returns intents; the app applies event context
     * and dispatches each hook through the game layer. */
    UITree_InteractFrame(
        &app->interact, app->tree, &app->ui_host, input, now_ms, &out);
    app->hover_com_id = out.hover_com_id;
    if( out.clicked_com_id >= 0 )
        app->clicked_com_id = out.clicked_com_id;
    if( out.need_redraw )
        app->need_redraw = 1;

    /* Snapshot hooks by value before dispatching anything: intent->hook points
     * into tree->components[], and an earlier intent's script can CC_CREATE
     * (realloc) or CC_DELETEALL (reclaim/reuse the slot), dangling the pointer. */
    {
        struct UITreeRuntimeScriptHook hook_copies[UI_INTENT_MAX];
        for( int i = 0; i < out.intent_count; i++ )
            if( out.intents[i].hook )
                hook_copies[i] = *out.intents[i].hook;

        for( int i = 0; i < out.intent_count; i++ )
        {
            struct UIIntent const* intent = &out.intents[i];
            if( intent->has_event_mouse )
                RS_CS2_SetEventMouse(&app->host, intent->event_mouse_x, intent->event_mouse_y);
            if( intent->has_drag_target )
                RS_CS2_SetEventDragTarget(&app->host, app->tree, intent->drag_target_id);
            RS_CS2_DispatchHook(
                &app->host,
                &app->runner,
                intent->component_id,
                intent->hook ? &hook_copies[i] : NULL);
            ran_cs2 = 1;
        }
    }

    /* Click handlers can unhide tabs; pump immediately so the freshly visible
     * widgets populate this frame instead of one tick later. Early-outs when
     * nothing was unhidden. */
    if( out.intent_count > 0 )
        RS_CS2_PumpTransmits(&app->host, &app->runner);

    if( ran_cs2 )
        UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    if( app->need_redraw )
    {
        app->emit.count = 0;
        UITree_EmitWalk(app->tree, &app->ui_host, &app->emit, app->hover_com_id);
        app->need_redraw = 0;
        return 1;
    }
    return 0;
}

void
App_Render(
    struct App* app,
    int* pixels,
    int width,
    int height)
{
    struct ToriRS_Frame frame;
    struct ToriRS_Soft3D soft;

    assert(app);
    assert(pixels);

    ToriRS_FrameInit(&frame);
    ToriRS_FrameSetScene(&frame, app->scene);
    ToriRS_FrameSetCanvas(&frame, width, height);
    ToriRS_FrameSetEmitBuffer(&frame, &app->emit);
    ToriRS_Soft3D_Init(&soft, app->scene, pixels, width, height);
    ToriRS_Soft3D_RenderFrame(&soft, &frame);
}

int
App_WriteBmp(
    struct App* app,
    char const* path,
    int width,
    int height)
{
    assert(app);
    return UITreeCmd_WriteBmp(
        app->scene, app->emit.cmds, app->emit.count, path, width, height);
}
