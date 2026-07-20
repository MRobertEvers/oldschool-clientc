#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "engine/uitree_cmd_render.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_cs2_host.h"
#include "game/task_cs2_run.h"
#include "input/torirs_input.h"
#include "inv/inv_manager.h"
#include "platform/platform_sdl2.h"
#include "platform/platform_sdl2_renderer_soft3d.h"
#include "platform/platform_x_io.h"
#include "render/torirs_frame.h"
#include "toridraw.h"
#include "toridraw_scene.h"
#include "ui/uitree.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_host.h"
#include "ui/uitree_hover.h"
#include "ui/uitree_input.h"
#include "ui/uitree_layout.h"
#include "varp/varp_manager.h"

#include <SDL.h>

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CACHE_DIR "/Users/matthewevers/Documents/git_repos/3draster/cache.jan2026"
#define CONFIG_DIR "/Users/matthewevers/Documents/git_repos/3draster/config"
#define SCRIPT_DIR "/Users/matthewevers/Documents/git_repos/3draster/script"
#define DEFAULT_INTERFACE_ID 630
#define UITREE_CLICK_DEBUG 0

static int
demo_uitree_host_request(void* user, struct UITreeHostRequest* req)
{
    struct UITreeSceneBridge* bridge = (struct UITreeSceneBridge*)user;

    assert(req);
    if( req->kind == UITREE_HOST_GET_SCROLLBAR_SCENE )
    {
        assert(bridge);
        return UITreeSceneBridge_ScrollbarSceneId(bridge);
    }
    return 0;
}

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

static struct UIInputResult
bridge_input_to_uitree(
    struct UIInputState* ui_state,
    struct UITree* tree,
    struct UITreeHost const* host,
    struct LibToriRS_Input* input)
{
    struct UIInputResult last;
    struct UIInputEvent move = {
        .kind = UI_INPUT_MOVE,
        .x = input->curr.mouse_x,
        .y = input->curr.mouse_y,
        .button = 0,
    };

    assert(ui_state);
    assert(tree);
    assert(input);

    last = UITree_InputUpdate(ui_state, tree, host, NULL, move);

    if( LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) )
    {
        struct UIInputEvent down = {
            .kind = UI_INPUT_DOWN,
            .x = input->curr.mouse_x,
            .y = input->curr.mouse_y,
            .button = TORIRSM_LEFT,
        };
        last = UITree_InputUpdate(ui_state, tree, host, NULL, down);
    }

    if( LibToriRS_Input_IsClick(input, TORIRSM_LEFT) )
    {
        struct UIInputEvent up = {
            .kind = UI_INPUT_UP,
            .x = input->last_click_x[TORIRSM_LEFT],
            .y = input->last_click_y[TORIRSM_LEFT],
            .button = TORIRSM_LEFT,
        };
        last = UITree_InputUpdate(ui_state, tree, host, NULL, up);
    }

    return last;
}

static void
pump_task_queue(
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_IO* io,
    struct PlatformX_IO* px)
{
    assert(queue);
    assert(io);
    assert(px);
    while( ToriRS_TaskQueue_Run(queue, io) == TORIRS_ASYNCIO_STAT_YIELD )
        PlatformX_IO_Process(px, io);
}

static void
run_runtime_hook(
    struct RS_CS2Host* host,
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_IO* io,
    struct PlatformX_IO* px,
    int component_id,
    struct UITreeRuntimeScriptHook const* hook)
{
    struct ToriRS_Task* task;

    assert(host);
    assert(queue);
    assert(io);
    assert(px);
    if( !hook || hook->script_id <= 0 || component_id < 0 )
    {
#if UITREE_CLICK_DEBUG
        fprintf(
            stderr,
            "uitree_click: run_runtime_hook skip component_id=%d hook=%p script_id=%d\n",
            component_id,
            (void const*)hook,
            hook ? hook->script_id : -1);
#endif
        return;
    }

#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: run_runtime_hook run component_id=%d script_id=%d argc=%d\n",
        component_id,
        hook->script_id,
        hook->argc);
#endif

    task = CreateTask_CS2Run(
        host,
        hook->script_id,
        component_id,
        component_id,
        hook->argc > 0 ? hook->argv : NULL,
        hook->argc);
    if( !task )
    {
#if UITREE_CLICK_DEBUG
        fprintf(
            stderr,
            "uitree_click: CreateTask_CS2Run failed component_id=%d script_id=%d\n",
            component_id,
            hook->script_id);
#endif
        return;
    }
    ToriRS_TaskQueue_Add(queue, task);
    pump_task_queue(queue, io, px);
    if( host->pending_inv_transmit_redispatch )
    {
        host->pending_inv_transmit_redispatch = 0;
        task = CreateTask_CS2InvTransmitDispatch(host, -1);
        assert(task);
        ToriRS_TaskQueue_Add(queue, task);
        pump_task_queue(queue, io, px);
    }
    if( host->pending_var_transmit_redispatch )
    {
        host->pending_var_transmit_redispatch = 0;
        task = CreateTask_CS2VarTransmitDispatch(host, -1);
        assert(task);
        ToriRS_TaskQueue_Add(queue, task);
        pump_task_queue(queue, io, px);
    }
}

static struct UITreeRuntimeScriptHook const*
component_runtime_hook(
    struct UITree* tree,
    int component_id,
    struct UITreeRuntimeScriptHook const* (*pick)(struct UITreeComponent const*))
{
    int32_t idx;

    assert(tree);
    assert(pick);
    if( component_id < 0 )
        return NULL;
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return NULL;
    return pick(&tree->components[idx]);
}

static struct UITreeRuntimeScriptHook const*
pick_on_mouse_over(struct UITreeComponent const* node)
{
    return &node->runtime_hooks.on_mouse_over;
}

static struct UITreeRuntimeScriptHook const*
pick_on_mouse_leave(struct UITreeComponent const* node)
{
    return &node->runtime_hooks.on_mouse_leave;
}

static struct UITreeRuntimeScriptHook const*
pick_on_drag_complete(struct UITreeComponent const* node)
{
    return &node->runtime_hooks.on_drag_complete;
}

#if UITREE_CLICK_DEBUG
static void
count_runtime_hooks(
    struct UITree const* tree,
    int* out_on_click,
    int* out_on_op)
{
    uint32_t i;
    int on_click = 0;
    int on_op = 0;

    assert(tree);
    assert(out_on_click);
    assert(out_on_op);

    for( i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* node = &tree->components[i];
        if( node->runtime_hooks.on_click.script_id > 0 )
            on_click++;
        if( node->runtime_hooks.on_op.script_id > 0 )
            on_op++;
    }
    *out_on_click = on_click;
    *out_on_op = on_op;
}
#endif

/* Prefer on_op, else on_click. Walk parents from leaf until a hooked node is found. */
static struct UITreeRuntimeScriptHook const*
resolve_click_hook(
    struct UITree* tree,
    int32_t leaf_index,
    int* out_component_id,
    char const** out_slot_name)
{
    int32_t idx;

    assert(tree);
    assert(out_component_id);
    assert(out_slot_name);

    *out_component_id = -1;
    *out_slot_name = NULL;

    if( leaf_index < 0 || (uint32_t)leaf_index >= tree->component_count )
        return NULL;

    for( idx = leaf_index; idx >= 0; idx = tree->components[idx].parent )
    {
        struct UITreeComponent const* node = &tree->components[idx];
        int on_op_id = node->runtime_hooks.on_op.script_id;
        int on_click_id = node->runtime_hooks.on_click.script_id;

#if UITREE_CLICK_DEBUG
        fprintf(
            stderr,
            "uitree_click: walk idx=%d id=%d type=%s on_op=%d on_click=%d click_mask=0x%x\n",
            (int)idx,
            node->component_id,
            UITree_ComponentTypeStr(node->type),
            on_op_id,
            on_click_id,
            (unsigned)node->behavior.click_mask);
#endif

        if( on_op_id > 0 )
        {
            *out_component_id = node->component_id;
            *out_slot_name = "on_op";
            return &node->runtime_hooks.on_op;
        }
        if( on_click_id > 0 )
        {
            *out_component_id = node->component_id;
            *out_slot_name = "on_click";
            return &node->runtime_hooks.on_click;
        }
    }

#if UITREE_CLICK_DEBUG
    fprintf(stderr, "uitree_click: walk found no on_op/on_click ancestor\n");
#endif
    return NULL;
}

static int
find_hover_component_id(
    struct UITree* tree,
    int mouse_x,
    int mouse_y)
{
    assert(tree);
    return UITree_FindHoveredComponentIdForRegion(
        tree,
        NULL,
        NULL,
        -1,
        mouse_x,
        mouse_y,
        0,
        0,
        UITREE_LAYOUT_ROOT_W,
        UITREE_LAYOUT_ROOT_H);
}

static void
update_window_title(
    struct PlatformSDL2* sdl,
    int interface_id,
    int hover_com_id,
    int clicked_com_id)
{
    char title[160];

    assert(sdl);
    snprintf(
        title,
        sizeof(title),
        "torirs iface=%d hover=%d clicked=%d",
        interface_id,
        hover_com_id,
        clicked_com_id);
    PlatformSDL2_SetTitle(sdl, title);
}

static void
render_emit_soft3d(
    struct ToriDraw_Scene* scene,
    struct UITreeEmitBuffer const* buf,
    int* pixels,
    int width,
    int height)
{
    struct ToriRS_Frame frame;
    struct ToriRS_Soft3D soft;

    assert(scene);
    assert(buf);
    assert(pixels);

    ToriRS_FrameInit(&frame);
    ToriRS_FrameSetScene(&frame, scene);
    ToriRS_FrameSetCanvas(&frame, width, height);
    ToriRS_FrameSetEmitBuffer(&frame, buf);
    ToriRS_Soft3D_Init(&soft, scene, pixels, width, height);
    ToriRS_Soft3D_RenderFrame(&soft, &frame);
}

int
main(
    int argc,
    char** argv)
{
    const char* cache_dir = CACHE_DIR;
    const char* config_dir = CONFIG_DIR;
    const char* script_dir = SCRIPT_DIR;
    int interface_id = DEFAULT_INTERFACE_ID;
    int write_bmp = 0;
    int positional = 0;
    int argi;

    for( argi = 1; argi < argc; argi++ )
    {
        if( strcmp(argv[argi], "--bmp") == 0 )
        {
            write_bmp = 1;
            continue;
        }
        if( positional == 0 )
        {
            cache_dir = argv[argi];
            positional++;
            continue;
        }
        if( positional == 1 )
        {
            interface_id = atoi(argv[argi]);
            if( interface_id <= 0 )
            {
                fprintf(stderr, "invalid interface id: %s\n", argv[argi]);
                return 1;
            }
            positional++;
            continue;
        }
        fprintf(stderr, "usage: %s [cache_dir] [interface_id] [--bmp]\n", argv[0]);
        return 1;
    }

    ToriDraw_Init();

    struct ToriRS_IO* io = ToriRS_IO_New();
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    assert(disk != NULL);
    struct PlatformX_IO* px = PlatformX_IO_New();
    assert(px != NULL);
    PlatformX_IO_InitDat2Disk(px, disk);
    PlatformX_IO_InitConfigPath(px, config_dir);
    PlatformX_IO_InitScriptPath(px, script_dir);

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0);
    assert(scene);

    struct UITree* tree = UITree_New(256);
    assert(tree);
    struct InvManager invs;
    InvManager_Init(&invs);
    struct VarPManager varps;
    VarPManager_Init(&varps);

    seed_inv_defaults(&invs);

    struct UITreeSceneBridge bridge;
    UITreeSceneBridge_Init(&bridge, scene, provider);

    struct RS_CS2Host host;
    RS_CS2Host_Init(&host, tree, provider, &invs, &varps);
    RS_CS2Host_SetBridge(&host, &bridge);

    struct InterfaceOpenStats stats;
    memset(&stats, 0, sizeof(stats));

    /* Open the requested interface directly as the tree root (TS parity:
     * WidgetManager.setRootInterface(groupId) — any group can be the root; no
     * hardcoded 161 chrome required). */
    {
        struct ToriRS_Task* root_task = CreateTask_InterfaceOpen(
            provider, tree, &host, &invs, &bridge, interface_id, &stats);
        assert(root_task != NULL);
        ToriRS_TaskQueue_Add(queue, root_task);
        while( ToriRS_TaskQueue_Run(queue, io) == TORIRS_ASYNCIO_STAT_YIELD )
            PlatformX_IO_Process(px, io);
    }

    printf(
        "InterfaceOpen done: iface=%d pack_components=%d tree_components=%u onloads=%d "
        "inv_hooks=%d var_hooks=%d mounts=%d\n",
        stats.interface_id,
        stats.pack_component_count,
        tree->component_count,
        stats.onload_count,
        host.inv_transmit_hook_count,
        host.var_transmit_hook_count,
        tree->interface_parent_count);

    if( getenv("DUMP_LAYOUT") )
    {
        for( uint32_t i = 0; i < tree->component_count; i++ )
        {
            struct UITreeComponent* dc = &tree->components[i];
            int dx = 0, dy = 0, dw = 0, dh = 0;
            UITree_LayoutGetBounds(&dc->position, &dx, &dy, &dw, &dh);
            fprintf(
                stderr,
                "LAY file=%d type=%d abs=(%d,%d,%d,%d) base=(%d,%d,%d,%d) "
                "mode=(x%d,y%d,w%d,h%d)\n",
                dc->component_id & 0xFFFF,
                (int)dc->type,
                dx,
                dy,
                dw,
                dh,
                dc->position.x,
                dc->position.y,
                dc->position.width,
                dc->position.height,
                (int)dc->position.x_mode,
                (int)dc->position.y_mode,
                (int)dc->position.width_mode,
                (int)dc->position.height_mode);
        }
    }

#if UITREE_CLICK_DEBUG
    {
        int hook_on_click = 0;
        int hook_on_op = 0;
        count_runtime_hooks(tree, &hook_on_click, &hook_on_op);
        fprintf(
            stderr,
            "uitree_click: runtime hooks on_click=%d on_op=%d\n",
            hook_on_click,
            hook_on_op);
    }
#endif

    struct UITreeEmitBuffer buf;
    struct UITreeHost ui_host;
    UITree_EmitBufferInit(&buf);
    UITree_HostInit(&ui_host);
    ui_host.user = &bridge;
    ui_host.request = demo_uitree_host_request;
    UITree_EmitWalk(tree, &ui_host, &buf, -1);

    if( write_bmp )
    {
        char path[256];
        snprintf(path, sizeof(path), "build/interface_%d.bmp", interface_id);
        if( UITreeCmd_WriteBmp(
                scene, buf.cmds, buf.count, path, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) == 0 )
            printf("wrote %s (%d cmds)\n", path, buf.count);
        else
            fprintf(stderr, "failed to write %s\n", path);
    }

    {
        struct PlatformSDL2* sdl = PlatformSDL2_New();
        struct LibToriRS_Input input_storage;
        struct LibToriRS_Input* input;
        struct UIInputState ui_state = { .hovered = -1, .pressed = -1 };
        struct UIInputResult ui_result = {
            .hovered = -1,
            .prev_hovered = -1,
            .clicked = -1,
            .hover_changed = false,
        };
        int hover_com_id = -1;
        int prev_hover_com_id = -1;
        int need_redraw = 1;
        char title[64];

        assert(sdl);
        snprintf(title, sizeof(title), "torirs iface=%d", interface_id);
        if( !PlatformSDL2_Init(sdl, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, title) )
        {
            fprintf(stderr, "SDL init failed\n");
            PlatformSDL2_Free(sdl);
            UITree_EmitBufferFree(&buf);
            goto teardown;
        }

        input = LibToriRS_Input_Init(&input_storage, SDL_GetTicks64());
        assert(input);

        render_emit_soft3d(
            scene,
            &buf,
            PlatformSDL2_Pixels(sdl),
            UITREE_LAYOUT_ROOT_W,
            UITREE_LAYOUT_ROOT_H);
        PlatformSDL2_Present(sdl);
        need_redraw = 0;

        while( !PlatformSDL2_QuitRequested(sdl) )
        {
            int ran_cs2 = 0;
            int clicked_com_id = -1;

            LibToriRS_Input_Begin(input, SDL_GetTicks64());
            PlatformSDL2_PollInput(sdl, input);
            LibToriRS_Input_End(input);

            ui_result = bridge_input_to_uitree(&ui_state, tree, &ui_host, input);

            /* Drag tick while held (deadzone+deadtime); fire onDrag / onDragComplete. */
            {
                int left_held = LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) ||
                                LibToriRS_Input_IsDragging(input, TORIRSM_LEFT);
                if( ui_state.drag_source_idx >= 0 && left_held )
                {
                    int drag_ch = UITree_InputDragTick(
                        &ui_state,
                        tree,
                        &ui_host,
                        input->curr.mouse_x,
                        input->curr.mouse_y,
                        1);
                    if( ui_state.drag_active )
                    {
                        struct UITreeComponent* src =
                            &tree->components[ui_state.drag_source_idx];
                        int parent_x = 0, parent_y = 0, parent_w = 0, parent_h = 0;
                        int32_t parent_idx = src->parent;
                        if( src->drag_render_area_uid >= 0 )
                            parent_idx =
                                UITree_FindByComponentId(tree, src->drag_render_area_uid);
                        if( parent_idx >= 0 )
                            UITree_LayoutGetBounds(
                                &tree->components[parent_idx].position,
                                &parent_x,
                                &parent_y,
                                &parent_w,
                                &parent_h);
                        host.event_mouse_x =
                            src->drag_visual_x - parent_x +
                            (parent_idx >= 0 ? tree->components[parent_idx].scroll_x : 0);
                        host.event_mouse_y =
                            src->drag_visual_y - parent_y +
                            (parent_idx >= 0 ? tree->components[parent_idx].scroll_y : 0);
                        host.event_drag_target_id = ui_state.drag_target_id;
                        host.event_drag_target_child_index = -1;
                        if( ui_state.drag_target_id >= 0 )
                        {
                            int32_t tidx =
                                UITree_FindByComponentId(tree, ui_state.drag_target_id);
                            if( tidx >= 0 && tree->components[tidx].dynamic )
                                host.event_drag_target_child_index =
                                    tree->components[tidx].dynamic_child_index;
                        }
                        run_runtime_hook(
                            &host,
                            queue,
                            io,
                            px,
                            ui_state.drag_source_id,
                            &src->runtime_hooks.on_drag);
                        ran_cs2 = 1;
                        need_redraw = 1;
                    }
                    else if( drag_ch )
                        need_redraw = 1;
                }
                if( ui_result.drag_ended )
                {
                    host.event_drag_target_id = ui_result.drag_target_id;
                    host.event_drag_target_child_index = -1;
                    if( ui_result.drag_target_id >= 0 )
                    {
                        int32_t tidx =
                            UITree_FindByComponentId(tree, ui_result.drag_target_id);
                        if( tidx >= 0 && tree->components[tidx].dynamic )
                            host.event_drag_target_child_index =
                                tree->components[tidx].dynamic_child_index;
                    }
                    if( ui_result.drag_source_id >= 0 )
                    {
                        run_runtime_hook(
                            &host,
                            queue,
                            io,
                            px,
                            ui_result.drag_source_id,
                            component_runtime_hook(
                                tree, ui_result.drag_source_id, pick_on_drag_complete));
                        ran_cs2 = 1;
                    }
#if UITREE_CLICK_DEBUG
                    fprintf(
                        stderr,
                        "uitree_drag: complete source=%d target=%d\n",
                        ui_result.drag_source_id,
                        ui_result.drag_target_id);
#endif
                    need_redraw = 1;
                }
            }

#if UITREE_CLICK_DEBUG
            if( LibToriRS_Input_IsClick(input, TORIRSM_LEFT) || ui_result.clicked >= 0 )
            {
                int click_x = input->last_click_x[TORIRSM_LEFT];
                int click_y = input->last_click_y[TORIRSM_LEFT];
                int32_t geo_idx = ui_result.clicked >= 0 ? ui_result.clicked
                                                         : UITree_HitTest(tree, click_x, click_y);
                int32_t ihit =
                    UITree_HitTestInteractive(tree, &ui_host, NULL, click_x, click_y);
                fprintf(
                    stderr,
                    "uitree_click: IsClick=%d clicked=%d mouse=(%d,%d)\n",
                    LibToriRS_Input_IsClick(input, TORIRSM_LEFT) ? 1 : 0,
                    (int)ui_result.clicked,
                    click_x,
                    click_y);
                if( geo_idx >= 0 && (uint32_t)geo_idx < tree->component_count )
                {
                    struct UITreeComponent const* geo = &tree->components[geo_idx];
                    fprintf(
                        stderr,
                        "uitree_click: geo idx=%d id=%d type=%s on_op=%d on_click=%d "
                        "click_mask=0x%x\n",
                        (int)geo_idx,
                        geo->component_id,
                        UITree_ComponentTypeStr(geo->type),
                        geo->runtime_hooks.on_op.script_id,
                        geo->runtime_hooks.on_click.script_id,
                        (unsigned)geo->behavior.click_mask);
                }
                else
                {
                    fprintf(stderr, "uitree_click: geo miss idx=%d\n", (int)geo_idx);
                }
                if( ihit >= 0 && (uint32_t)ihit < tree->component_count )
                {
                    struct UITreeComponent const* inode = &tree->components[ihit];
                    fprintf(
                        stderr,
                        "uitree_click: interactive idx=%d id=%d type=%s on_op=%d on_click=%d "
                        "click_mask=0x%x\n",
                        (int)ihit,
                        inode->component_id,
                        UITree_ComponentTypeStr(inode->type),
                        inode->runtime_hooks.on_op.script_id,
                        inode->runtime_hooks.on_click.script_id,
                        (unsigned)inode->behavior.click_mask);
                }
                else
                {
                    fprintf(stderr, "uitree_click: interactive miss idx=%d\n", (int)ihit);
                }
            }
#endif

            hover_com_id =
                find_hover_component_id(tree, input->curr.mouse_x, input->curr.mouse_y);

            if( hover_com_id != prev_hover_com_id )
            {
                if( prev_hover_com_id >= 0 )
                {
                    run_runtime_hook(
                        &host,
                        queue,
                        io,
                        px,
                        prev_hover_com_id,
                        component_runtime_hook(tree, prev_hover_com_id, pick_on_mouse_leave));
                    ran_cs2 = 1;
                }
                if( hover_com_id >= 0 )
                {
                    run_runtime_hook(
                        &host,
                        queue,
                        io,
                        px,
                        hover_com_id,
                        component_runtime_hook(tree, hover_com_id, pick_on_mouse_over));
                    ran_cs2 = 1;
                }
                prev_hover_com_id = hover_com_id;
                need_redraw = 1;
            }

            if( ui_result.clicked >= 0 &&
                (uint32_t)ui_result.clicked < tree->component_count )
            {
                struct UITreeRuntimeScriptHook const* click_hook = NULL;
                char const* slot_name = NULL;
                int hook_com_id = -1;
                int32_t resolve_idx = -1;
                int click_x = input->last_click_x[TORIRSM_LEFT];
                int click_y = input->last_click_y[TORIRSM_LEFT];
                int32_t ihit =
                    UITree_HitTestInteractive(tree, &ui_host, NULL, click_x, click_y);

                /* Prefer interactive hit so clickMask targets beat decorative overlays. */
                if( ihit >= 0 && (uint32_t)ihit < tree->component_count )
                {
                    resolve_idx = ihit;
                    click_hook = resolve_click_hook(
                        tree, resolve_idx, &hook_com_id, &slot_name);
                }
                if( !click_hook )
                {
                    resolve_idx = ui_result.clicked;
                    click_hook = resolve_click_hook(
                        tree, resolve_idx, &hook_com_id, &slot_name);
                }

                clicked_com_id = hook_com_id >= 0
                                     ? hook_com_id
                                     : (ihit >= 0 && (uint32_t)ihit < tree->component_count
                                            ? tree->components[ihit].component_id
                                            : tree->components[ui_result.clicked].component_id);

#if UITREE_CLICK_DEBUG
                fprintf(
                    stderr,
                    "uitree_click: resolved leaf_idx=%d leaf_id=%d resolve_idx=%d -> "
                    "hook_id=%d slot=%s script_id=%d\n",
                    (int)ui_result.clicked,
                    tree->components[ui_result.clicked].component_id,
                    (int)resolve_idx,
                    hook_com_id,
                    slot_name ? slot_name : "(none)",
                    click_hook ? click_hook->script_id : -1);
#endif

                if( click_hook && click_hook->script_id > 0 )
                {
                    run_runtime_hook(
                        &host, queue, io, px, hook_com_id, click_hook);
                    ran_cs2 = 1;
                }
                else
                {
                    run_runtime_hook(
                        &host, queue, io, px, hook_com_id, click_hook);
                }
                need_redraw = 1;
            }

            if( ran_cs2 )
            {
                UITree_LayoutResolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            }

            update_window_title(sdl, interface_id, hover_com_id, clicked_com_id);

            if( need_redraw )
            {
                buf.count = 0;
                UITree_EmitWalk(tree, &ui_host, &buf, hover_com_id);
                render_emit_soft3d(
                    scene,
                    &buf,
                    PlatformSDL2_Pixels(sdl),
                    UITREE_LAYOUT_ROOT_W,
                    UITREE_LAYOUT_ROOT_H);
                need_redraw = 0;
            }

            PlatformSDL2_Present(sdl);
            SDL_Delay(1);
        }

        PlatformSDL2_Free(sdl);
    }

    UITree_EmitBufferFree(&buf);

teardown:
    UITreeSceneBridge_Free(&bridge);
    ToriDraw_SceneFree(scene);
    UITree_Free(tree);
    InvManager_Free(&invs);
    VarPManager_Free(&varps);
    ToriRS_TaskQueue_Free(queue);
    ToriRS_IO_Free(io);
    PlatformX_IO_Free(px);
    RSCache_Dat2DiskFree(disk);
    dat2_buildcache_free(bc);

    (void)script_dir;
    return 0;
}
