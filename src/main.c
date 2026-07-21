#include "app.h"
#include "input/torirs_input.h"
#include "platform/platform_sdl2.h"
#include "ui/uitree_layout.h"

#include <assert.h>
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

    /* TEMP (scrollbar/dropdown verification, removed when done): DRAG_SIM runs
     * ';'-separated gestures headlessly: "x0,y0,x1,y1" drags (degenerate =
     * click), "w<x>,<y>,<d>" wheel. Prints scroll state; BMP per segment. */
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
            char const* seg = dsim;
            int seg_no = 0;

            assert(sim_pixels);
            UITree_LayoutResolve(app.tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

            while( seg && *seg )
            {
                char bmp_path[128];
                int wheel_amt = 0;
                int is_wheel = (*seg == 'w');
                if( is_wheel )
                {
                    if( sscanf(seg + 1, "%d,%d,%d", &x0, &y0, &wheel_amt) != 3 )
                        break;
                    x1 = x0;
                    y1 = y0;
                }
                else if( sscanf(seg, "%d,%d,%d,%d", &x0, &y0, &x1, &y1) != 4 )
                    break;
                seg_no++;
                fprintf(
                    stderr,
                    "DRAG_SIM seg %d (%d,%d)->(%d,%d) wheel=%d\n",
                    seg_no, x0, y0, x1, y1, wheel_amt);

                for( int t = 1; t <= drag_ticks + 14; t++ )
                {
                    tick_ms += 20;
                    LibToriRS_Input_Begin(sim_input, tick_ms);
                    if( t == 2 )
                        LibToriRS_Input_PushMouseMove(sim_input, x0, y0);
                    if( is_wheel && t >= 3 && t <= 6 )
                        LibToriRS_Input_PushMouseWheel(sim_input, wheel_amt);
                    if( !is_wheel )
                    {
                        if( t == 4 )
                            LibToriRS_Input_PushMouseDown(sim_input, TORIRSM_LEFT, x0, y0);
                        if( t > 4 && t <= 4 + drag_ticks )
                            LibToriRS_Input_PushMouseMove(
                                sim_input,
                                x0 + ((x1 - x0) * (t - 4)) / drag_ticks,
                                y0 + ((y1 - y0) * (t - 4)) / drag_ticks);
                        if( t == 4 + drag_ticks + 2 )
                            LibToriRS_Input_PushMouseUp(sim_input, TORIRSM_LEFT, x1, y1);
                    }
                    LibToriRS_Input_End(sim_input);
                    if( App_RunOnce(&app, tick_ms, sim_input) )
                        App_Render(&app, sim_pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

                    for( uint32_t ci = 0; ci < app.tree->component_count; ci++ )
                    {
                        struct UITreeComponent const* c = &app.tree->components[ci];
                        if( c->freed || c->type != UIELEM_RS_LAYER )
                            continue;
                        if( c->u.rs_layer.scroll_height <= 0 )
                            continue;
                        fprintf(
                            stderr,
                            "DRAG_SIM t=%d layer id=%d scroll=(%d,%d)\n",
                            t, c->component_id, c->scroll_x, c->scroll_y);
                    }
                    if( !is_wheel && t == 4 + drag_ticks / 2 )
                    {
                        App_Render(&app, sim_pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
                        snprintf(
                            bmp_path, sizeof(bmp_path), "build/drag_sim_%d_mid.bmp", seg_no);
                        App_WriteBmp(&app, bmp_path, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
                    }
                }
                /* Mid-drag snapshot support: a trailing segment "d..." not
                 * needed — the release happens before the ticks run out. */
                snprintf(bmp_path, sizeof(bmp_path), "build/drag_sim_%d.bmp", seg_no);
                App_WriteBmp(&app, bmp_path, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
                seg = strchr(seg, ';');
                if( seg )
                    seg++;
            }

            /* Post-run dump: every live component with box + visual fields, for
             * dropdown/scrollbar inspection. */
            if( getenv("DRAG_SIM_DUMP") )
            {
                for( int ei = 0; ei < app.emit.count; ei++ )
                {
                    struct UITreeEmitDesc const* d = &app.emit.cmds[ei];
                    if( d->x < 595 || d->x > 640 )
                        continue;
                    fprintf(
                        stderr,
                        "EMIT kind=%d id=%d box=(%d,%d %dx%d) scene=%d\n",
                        (int)d->kind, d->component_id, d->x, d->y, d->w, d->h, d->scene_id);
                }
                for( uint32_t ci = 0; ci < app.tree->component_count; ci++ )
                {
                    struct UITreeComponent const* c = &app.tree->components[ci];
                    struct UITreeElemPosition const* p = &c->position;
                    if( c->freed || c->component_id < 0 )
                        continue;
                    fprintf(
                        stderr,
                        "DUMP id=%d idx=%u type=%d box=(%d,%d %dx%d) hide=%d trans=%d "
                        "dyn=%d subid=%d parent_id=%d",
                        c->component_id, ci, (int)c->type,
                        p->abs_x, p->abs_y, p->abs_w, p->abs_h,
                        (int)c->behavior.hide, c->trans,
                        (int)c->dynamic, c->dynamic_child_index,
                        c->parent >= 0 ? app.tree->components[c->parent].component_id : -1);
                    if( c->type == UIELEM_RS_RECT )
                        fprintf(
                            stderr, " rect color=0x%x filled=%d",
                            c->u.rs_rect.color, c->u.rs_rect.filled);
                    if( c->type == UIELEM_RS_GRAPHIC )
                        fprintf(
                            stderr, " gfx scene=%d tiled=%d",
                            c->u.rs_graphic.scene_id, (int)c->u.rs_graphic.tiled);
                    if( c->type == UIELEM_RS_TEXT && c->u.rs_text.text )
                        fprintf(stderr, " text=\"%.24s\"", c->u.rs_text.text);
                    fprintf(stderr, "\n");
                }
            }
            free(sim_pixels);
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

        input = LibToriRS_Input_Init(&input_storage, PlatformSDL2_Ticks64());

        App_Render(&app, PlatformSDL2_Pixels(sdl), UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        PlatformSDL2_Present(sdl);

        while( !PlatformSDL2_QuitRequested(sdl) )
        {
            LibToriRS_Input_Begin(input, PlatformSDL2_Ticks64());
            PlatformSDL2_PollInput(sdl, input);
            LibToriRS_Input_End(input);

            if( App_RunOnce(&app, PlatformSDL2_Ticks64(), input) )
                App_Render(
                    &app, PlatformSDL2_Pixels(sdl), UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

            update_window_title(sdl, &app, cfg.interface_id);
            PlatformSDL2_Present(sdl);
            PlatformSDL2_Delay(1);
        }

        PlatformSDL2_Free(sdl);
    }

    App_Shutdown(&app);
    return 0;
}
