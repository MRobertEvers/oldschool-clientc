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
#define UITREE_CLICK_DEBUG 1

/* Interface 12 bank view panels / posit buttons (file index = component_id & 0xffff). */
#define IFACE12_PANEL_BANK_INV 10
#define IFACE12_PANEL_OTHER 54
#define IFACE12_PANEL_WORN 79
#define IFACE12_POSIT_INV 44
#define IFACE12_POSIT_WORN 46

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
}

static struct UIInputResult
bridge_input_to_uitree(
    struct UIInputState* ui_state,
    struct UITree* tree,
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

    last = UITree_InputUpdate(ui_state, tree, move);

    if( LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) )
    {
        struct UIInputEvent down = {
            .kind = UI_INPUT_DOWN,
            .x = input->curr.mouse_x,
            .y = input->curr.mouse_y,
            .button = TORIRSM_LEFT,
        };
        last = UITree_InputUpdate(ui_state, tree, down);
    }

    if( LibToriRS_Input_IsClick(input, TORIRSM_LEFT) )
    {
        struct UIInputEvent up = {
            .kind = UI_INPUT_UP,
            .x = input->last_click_x[TORIRSM_LEFT],
            .y = input->last_click_y[TORIRSM_LEFT],
            .button = TORIRSM_LEFT,
        };
        last = UITree_InputUpdate(ui_state, tree, up);
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

/* Offline stand-in for server IF_SETHIDE when posit inventory/worn buttons are clicked. */
static int
try_iface12_equip_view_stub(
    struct UITree* tree,
    int interface_id,
    int component_id)
{
    int file;
    int bank_id;
    int worn_id;
    int other_id;

    assert(tree);
    if( interface_id != 12 || component_id < 0 )
        return 0;

    file = component_id & 0xffff;
    if( (component_id >> 16) != 12 )
        return 0;
    /* Decorative overlay on posit inventory (file 45) maps to the button (44). */
    if( file == 45 )
        file = IFACE12_POSIT_INV;
    if( file != IFACE12_POSIT_INV && file != IFACE12_POSIT_WORN )
        return 0;

    bank_id = (12 << 16) | IFACE12_PANEL_BANK_INV;
    worn_id = (12 << 16) | IFACE12_PANEL_WORN;
    other_id = (12 << 16) | IFACE12_PANEL_OTHER;

    if( file == IFACE12_POSIT_WORN )
    {
#if UITREE_CLICK_DEBUG
        fprintf(
            stderr,
            "uitree_click: equip view stub worn — hide bank=%d show worn=%d\n",
            bank_id,
            worn_id);
#endif
        (void)UITree_ApplyHide(tree, bank_id, 1);
        (void)UITree_ApplyHide(tree, other_id, 1);
        (void)UITree_ApplyHide(tree, worn_id, 0);
        return 1;
    }

#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: equip view stub inventory — show bank=%d hide worn=%d\n",
        bank_id,
        worn_id);
#endif
    (void)UITree_ApplyHide(tree, worn_id, 1);
    (void)UITree_ApplyHide(tree, other_id, 1);
    (void)UITree_ApplyHide(tree, bank_id, 0);
    return 1;
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

    struct ToriRS_Task* task =
        CreateTask_InterfaceOpen(provider, tree, &host, &invs, &bridge, interface_id, &stats);
    assert(task != NULL);
    ToriRS_TaskQueue_Add(queue, task);

    while( ToriRS_TaskQueue_Run(queue, io) == TORIRS_ASYNCIO_STAT_YIELD )
    {
        PlatformX_IO_Process(px, io);
    }

    printf(
        "InterfaceOpen done: iface=%d pack_components=%d tree_components=%u onloads=%d "
        "inv_hooks=%d var_hooks=%d\n",
        stats.interface_id,
        stats.pack_component_count,
        tree->component_count,
        stats.onload_count,
        host.inv_transmit_hook_count,
        host.var_transmit_hook_count);

#if UITREE_CLICK_DEBUG
    {
        int hook_on_click = 0;
        int hook_on_op = 0;
        int32_t idx44;
        int32_t idx46;
        int32_t idx10;
        int32_t idx79;
        count_runtime_hooks(tree, &hook_on_click, &hook_on_op);
        fprintf(
            stderr,
            "uitree_click: runtime hooks on_click=%d on_op=%d\n",
            hook_on_click,
            hook_on_op);

        idx44 = UITree_FindByComponentId(tree, (12 << 16) | IFACE12_POSIT_INV);
        idx46 = UITree_FindByComponentId(tree, (12 << 16) | IFACE12_POSIT_WORN);
        idx10 = UITree_FindByComponentId(tree, (12 << 16) | IFACE12_PANEL_BANK_INV);
        idx79 = UITree_FindByComponentId(tree, (12 << 16) | IFACE12_PANEL_WORN);
        if( idx44 >= 0 && idx46 >= 0 && idx10 >= 0 && idx79 >= 0 )
        {
            fprintf(
                stderr,
                "uitree_click: equip bake 44 mask=0x%x 46 mask=0x%x bank_hide=%u worn_hide=%u\n",
                (unsigned)tree->components[idx44].behavior.click_mask,
                (unsigned)tree->components[idx46].behavior.click_mask,
                (unsigned)tree->components[idx10].behavior.hide,
                (unsigned)tree->components[idx79].behavior.hide);
            if( try_iface12_equip_view_stub(tree, 12, (12 << 16) | IFACE12_POSIT_WORN) )
            {
                fprintf(
                    stderr,
                    "uitree_click: after worn stub bank_hide=%u worn_hide=%u\n",
                    (unsigned)tree->components[idx10].behavior.hide,
                    (unsigned)tree->components[idx79].behavior.hide);
                (void)try_iface12_equip_view_stub(
                    tree, 12, (12 << 16) | IFACE12_POSIT_INV);
            }
        }
        else
        {
            fprintf(
                stderr,
                "uitree_click: equip bake miss idx44=%d idx46=%d idx10=%d idx79=%d\n",
                (int)idx44,
                (int)idx46,
                (int)idx10,
                (int)idx79);
        }
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

            ui_result = bridge_input_to_uitree(&ui_state, tree, input);

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
                else if( try_iface12_equip_view_stub(tree, interface_id, clicked_com_id) )
                {
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
                UITree_LayoutResolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

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
