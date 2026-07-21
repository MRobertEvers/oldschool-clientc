#include "app.h"
#include "game/rs_cs2_dispatch.h"
#include "input/torirs_input.h"
#include "platform/platform_sdl2.h"
#include "ui/uitree_interact.h"
#include "ui/uitree_layout.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Repo-relative defaults (run from the repo root); pass an explicit cache dir
 * as argv[1] from anywhere else. */
#define CACHE_DIR "cache.jan2026"
#define CONFIG_DIR "config"
#define SCRIPT_DIR "script"
#define DEFAULT_INTERFACE_ID 84

static void
update_window_title(
    struct PlatformSDL2* sdl,
    struct App const* app,
    int interface_id)
{
    char title[160];

    snprintf(
        title,
        sizeof(title),
        "torirs iface=%d hover=%d clicked=%d",
        interface_id,
        app->hover_com_id,
        app->clicked_com_id);
    PlatformSDL2_SetTitle(sdl, title);
}

int
main(
    int argc,
    char** argv)
{
    struct AppConfig cfg = {
        .cache_dir = CACHE_DIR,
        .config_dir = CONFIG_DIR,
        .script_dir = SCRIPT_DIR,
        .interface_id = DEFAULT_INTERFACE_ID,
    };
    static struct App app;
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
            cfg.cache_dir = argv[argi];
            positional++;
            continue;
        }
        if( positional == 1 )
        {
            cfg.interface_id = atoi(argv[argi]);
            if( cfg.interface_id <= 0 )
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

    App_Init(&app, &cfg);
    App_OpenRootInterface(&app, cfg.interface_id);

    /* TEMP (hang diagnosis): CLICK_SIM=<component_id> injects a real synthetic
     * mouse click at that component's center and drives App_RunOnce through the
     * full production path (hover, hit-test, intents, timers), printing progress
     * so the exact hang site is observable. CLICK_SIM_LIST=1 dumps clickable
     * components with their boxes instead. */
    {
        char const* sim = getenv("CLICK_SIM");
        if( getenv("CLICK_SIM_LIST") )
        {
            UITree_LayoutResolve(app.tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            for( uint32_t ci = 0; ci < app.tree->component_count; ci++ )
            {
                struct UITreeComponent const* c = &app.tree->components[ci];
                if( c->component_id < 0 )
                    continue;
                if( c->runtime_hooks.on_op.script_id <= 0 &&
                    c->runtime_hooks.on_click.script_id <= 0 )
                    continue;
                fprintf(
                    stderr,
                    "CLICKABLE id=%d type=%d box=(%d,%d %dx%d) hide=%d on_op=%d on_click=%d\n",
                    c->component_id,
                    (int)c->type,
                    c->position.abs_x,
                    c->position.abs_y,
                    c->position.abs_w,
                    c->position.abs_h,
                    (int)c->behavior.hide,
                    c->runtime_hooks.on_op.script_id,
                    c->runtime_hooks.on_click.script_id);
            }
            App_Shutdown(&app);
            return 0;
        }
        if( sim )
        {
            int sim_id = atoi(sim);
            int32_t sidx;
            int cx = 0, cy = 0;
            struct LibToriRS_Input sim_input_storage;
            struct LibToriRS_Input* sim_input = LibToriRS_Input_Init(&sim_input_storage, 0);

            UITree_LayoutResolve(app.tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            sidx = UITree_FindByComponentId(app.tree, sim_id);
            if( sidx >= 0 )
            {
                struct UITreeComponent const* c = &app.tree->components[sidx];
                cx = c->position.abs_x + c->position.abs_w / 2;
                cy = c->position.abs_y + c->position.abs_h / 2;
                fprintf(
                    stderr,
                    "CLICK_SIM id=%d idx=%d type=%d box=(%d,%d %dx%d) -> click (%d,%d)\n",
                    sim_id,
                    (int)sidx,
                    (int)c->type,
                    c->position.abs_x,
                    c->position.abs_y,
                    c->position.abs_w,
                    c->position.abs_h,
                    cx,
                    cy);
            }
            else
            {
                fprintf(stderr, "CLICK_SIM id=%d NOT FOUND\n", sim_id);
            }

            {
                int* sim_pixels =
                    malloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H * sizeof(int));
                uint64_t tick_ms = 0;
                assert(sim_pixels);

                /* Phase A: click the target component, rendering every frame. */
                for( int t = 1; t <= 30; t++ )
                {
                    tick_ms += 20;
                    LibToriRS_Input_Begin(sim_input, tick_ms);
                    if( t == 2 )
                        LibToriRS_Input_PushMouseMove(sim_input, cx, cy);
                    if( t == 4 )
                        LibToriRS_Input_PushMouseDown(sim_input, TORIRSM_LEFT, cx, cy);
                    if( t == 6 )
                        LibToriRS_Input_PushMouseUp(sim_input, TORIRSM_LEFT, cx, cy);
                    LibToriRS_Input_End(sim_input);
                    if( App_RunOnce(&app, tick_ms, sim_input) )
                        App_Render(
                            &app, sim_pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
                    fprintf(
                        stderr, "CLICK_SIM tick %d ok (clicked=%d)\n", t, app.clicked_com_id);
                }

                /* Phase B: sweep-click every visible clickable that appeared or
                 * lives near/below the target (the opened dropdown), rendering
                 * each frame, to catch second-click hangs. */
                for( uint32_t ci = 0; ci < app.tree->component_count; ci++ )
                {
                    struct UITreeComponent const* c = &app.tree->components[ci];
                    int tx, ty;
                    if( c->component_id < 0 || c->behavior.hide )
                        continue;
                    if( c->runtime_hooks.on_op.script_id <= 0 &&
                        c->runtime_hooks.on_click.script_id <= 0 )
                        continue;
                    if( !c->position.layout_resolved || c->position.abs_w <= 0 ||
                        c->position.abs_h <= 0 )
                        continue;
                    tx = c->position.abs_x + c->position.abs_w / 2;
                    ty = c->position.abs_y + c->position.abs_h / 2;
                    if( tx < 0 || ty < 0 || tx >= UITREE_LAYOUT_ROOT_W ||
                        ty >= UITREE_LAYOUT_ROOT_H )
                        continue;
                    fprintf(
                        stderr,
                        "CLICK_SIM sweep id=%d at (%d,%d) op=%d click=%d...\n",
                        c->component_id,
                        tx,
                        ty,
                        c->runtime_hooks.on_op.script_id,
                        c->runtime_hooks.on_click.script_id);
                    for( int t = 1; t <= 8; t++ )
                    {
                        tick_ms += 20;
                        LibToriRS_Input_Begin(sim_input, tick_ms);
                        if( t == 1 )
                            LibToriRS_Input_PushMouseMove(sim_input, tx, ty);
                        if( t == 3 )
                            LibToriRS_Input_PushMouseDown(sim_input, TORIRSM_LEFT, tx, ty);
                        if( t == 5 )
                            LibToriRS_Input_PushMouseUp(sim_input, TORIRSM_LEFT, tx, ty);
                        LibToriRS_Input_End(sim_input);
                        if( App_RunOnce(&app, tick_ms, sim_input) )
                            App_Render(
                                &app, sim_pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
                    }
                    fprintf(stderr, "CLICK_SIM sweep id=%d ok\n", c->component_id);
                }
                free(sim_pixels);
            }
            fprintf(stderr, "CLICK_SIM complete\n");
            App_Shutdown(&app);
            return 0;
        }
    }

    /* TEMP (scrollbar diagnosis): DRAG_SIM="x0,y0,x1,y1" presses at (x0,y0),
     * drags stepwise to (x1,y1), releases, printing scroll state of every
     * scrollable layer each tick; writes build/drag_sim.bmp at the end. */
    {
        char const* dsim = getenv("DRAG_SIM");
        if( dsim )
        {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            struct LibToriRS_Input sim_input_storage;
            struct LibToriRS_Input* sim_input = LibToriRS_Input_Init(&sim_input_storage, 0);
            int* sim_pixels =
                malloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H * sizeof(int));
            uint64_t tick_ms = 0;
            int const drag_ticks = 20;

            assert(sim_pixels);
            sscanf(dsim, "%d,%d,%d,%d", &x0, &y0, &x1, &y1);
            UITree_LayoutResolve(app.tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            fprintf(stderr, "DRAG_SIM (%d,%d) -> (%d,%d)\n", x0, y0, x1, y1);

            for( int t = 1; t <= drag_ticks + 14; t++ )
            {
                tick_ms += 20;
                LibToriRS_Input_Begin(sim_input, tick_ms);
                if( t == 2 )
                    LibToriRS_Input_PushMouseMove(sim_input, x0, y0);
                if( t == 4 )
                    LibToriRS_Input_PushMouseDown(sim_input, TORIRSM_LEFT, x0, y0);
                if( t > 4 && t <= 4 + drag_ticks )
                {
                    int mx = x0 + (x1 - x0) * (t - 4) / drag_ticks;
                    int my = y0 + (y1 - y0) * (t - 4) / drag_ticks;
                    LibToriRS_Input_PushMouseMove(sim_input, mx, my);
                }
                if( t == 4 + drag_ticks + 2 )
                    LibToriRS_Input_PushMouseUp(sim_input, TORIRSM_LEFT, x1, y1);
                LibToriRS_Input_End(sim_input);
                if( App_RunOnce(&app, tick_ms, sim_input) )
                    App_Render(&app, sim_pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

                for( uint32_t ci = 0; ci < app.tree->component_count; ci++ )
                {
                    struct UITreeComponent const* c = &app.tree->components[ci];
                    if( c->freed || c->type != UIELEM_RS_LAYER )
                        continue;
                    if( c->u.rs_layer.scroll_height <= 0 && c->u.rs_layer.scroll_width <= 0 )
                        continue;
                    fprintf(
                        stderr,
                        "DRAG_SIM t=%d layer id=%d box=(%d,%d %dx%d) scroll=(%d,%d) "
                        "size=(%d,%d) if3=%d\n",
                        t,
                        c->component_id,
                        c->position.abs_x,
                        c->position.abs_y,
                        c->position.abs_w,
                        c->position.abs_h,
                        c->scroll_x,
                        c->scroll_y,
                        c->u.rs_layer.scroll_width,
                        c->u.rs_layer.scroll_height,
                        (int)c->if3);
                }
            }
            App_WriteBmp(&app, "build/drag_sim.bmp", UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            fprintf(stderr, "DRAG_SIM complete\n");
            App_Shutdown(&app);
            return 0;
        }
    }

    if( write_bmp )
    {
        char path[256];
        snprintf(path, sizeof(path), "build/interface_%d.bmp", cfg.interface_id);
        if( App_WriteBmp(&app, path, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) == 0 )
            printf("wrote %s (%d cmds)\n", path, app.emit.count);
        else
            fprintf(stderr, "failed to write %s\n", path);
    }

    {
        struct PlatformSDL2* sdl = PlatformSDL2_New();
        struct LibToriRS_Input input_storage;
        struct LibToriRS_Input* input;
        char title[64];

        snprintf(title, sizeof(title), "torirs iface=%d", cfg.interface_id);
        if( !sdl || !PlatformSDL2_Init(sdl, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, title) )
        {
            fprintf(stderr, "SDL init failed\n");
            PlatformSDL2_Free(sdl);
            App_Shutdown(&app);
            return 1;
        }

        input = LibToriRS_Input_Init(&input_storage, SDL_GetTicks64());

        App_Render(&app, PlatformSDL2_Pixels(sdl), UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        PlatformSDL2_Present(sdl);

        /* TEMP (hang diagnosis): CLICK_SIM_LIVE=<component_id> injects a
         * synthetic click on that component inside the real windowed loop. */
        int sim_live_id = getenv("CLICK_SIM_LIVE") ? atoi(getenv("CLICK_SIM_LIVE")) : -1;
        int sim_live_frame = 0;

        while( !PlatformSDL2_QuitRequested(sdl) )
        {
            LibToriRS_Input_Begin(input, SDL_GetTicks64());
            PlatformSDL2_PollInput(sdl, input);
            if( sim_live_id >= 0 )
            {
                int32_t sidx = UITree_FindByComponentId(app.tree, sim_live_id);
                if( sidx >= 0 )
                {
                    struct UITreeComponent const* c = &app.tree->components[sidx];
                    int cx = c->position.abs_x + c->position.abs_w / 2;
                    int cy = c->position.abs_y + c->position.abs_h / 2;
                    sim_live_frame++;
                    if( sim_live_frame == 30 )
                        LibToriRS_Input_PushMouseMove(input, cx, cy);
                    if( sim_live_frame == 60 )
                        LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, cx, cy);
                    if( sim_live_frame == 90 )
                        LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, cx, cy);
                    if( sim_live_frame % 100 == 0 )
                        fprintf(stderr, "CLICK_SIM_LIVE frame %d alive\n", sim_live_frame);
                }
            }
            LibToriRS_Input_End(input);

            if( App_RunOnce(&app, SDL_GetTicks64(), input) )
                App_Render(
                    &app, PlatformSDL2_Pixels(sdl), UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

            update_window_title(sdl, &app, cfg.interface_id);
            PlatformSDL2_Present(sdl);
            SDL_Delay(1);
        }

        PlatformSDL2_Free(sdl);
    }

    App_Shutdown(&app);
    return 0;
}
