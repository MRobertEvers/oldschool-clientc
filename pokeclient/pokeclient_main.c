/*
 * The PokeSprite client: the same game, drawn as a Pokemon overworld.
 *
 * A separate program, not a mode of the 3D client. It links the game's app,
 * net, world, UI and embedded-server code as libraries (built by its own
 * Makefile from src/makefile's lists) and supplies only what differs: this
 * main, and a world drawn from sprite sheets served by io_server
 * (GET /pokesprite/<key>) instead of models.
 *
 * Boot and frame follow src/main.c's sequence for the software lane,
 * trimmed to what a playing client needs: no renderer switching, no harness,
 * no plugin chrome executor, no audio device yet.
 *
 *   pokeclient --manifest <boot.ini> [--user U --pass P] [--assets HOST:PORT]
 *              [--frames N] [--shot PATH.bmp@FRAME]
 */

#include "app.h"
#include "bootmanifest/bootmanifest.h"
#include "cmd/cmdbus.h"
#include "executor_config.h"
#include "input/torirs_input.h"
#include "net/net.h"
#include "net/rev/gameproto_revisions.h"
#include "pacer.h"
#include "platform/net_transport.h"
#include "platform/platform_window.h"
#include "pokesprite/pokesprite_format.h"
#include "ps_fetch.h"
#include "ps_render.h"
#include "ps_world.h"
#include "revconfig/revconfig_refs.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <bmp.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_DIR "config"
#define SCRIPT_DIR "script"
#define INTERFACE_ID_UNSET 0
#define FRAME_PERIOD_MS 20

static struct App app;
static struct ToriRS_ExecutorConfig executor_cfg;
static struct BootManifest boot_manifest; /* cfg points into it */
static struct AppConfig cfg = {
    .cache_dir = NULL,
    .config_dir = CONFIG_DIR,
    .script_dir = SCRIPT_DIR,
    .interface_id = INTERFACE_ID_UNSET,
    .cache_kind = APP_CACHE_DAT1,
    .spawn_x = -1,
    .spawn_z = -1,
};
static struct LibToriRS_Input input_storage;

static void
usage(const char* argv0)
{
    fprintf(stderr,
            "usage: %s --manifest <boot.ini> [--user U --pass P] [--assets HOST:PORT] "
            "[--window WxH] [--frames N] [--shot PATH.bmp@FRAME] [--click X,Y@FRAME]\n"
            "       [--exec TEXT@FRAME]...\n"
            "  --assets  io_server serving the PokeSprite store (default 127.0.0.1:8088)\n"
            "  --frames  exit after N frames (headless checks)\n"
            "  --shot    write the framebuffer at FRAME as a BMP (the client's own\n"
            "            pixels; never a desktop capture); up to four\n"
            "  --click   inject a left click at X,Y on FRAME (headless checks)\n"
            "  --exec    send TEXT as a ::command on FRAME (headless checks; up to 4)\n",
            argv0);
}

int
main(
    int argc,
    char** argv)
{
    const char* manifest = NULL;
    char assets_host[128] = "127.0.0.1";
    int assets_port = 8088;
    long max_frames = 0;
    int window_w = 0, window_h = 0;
    char shot_path[4][512];
    long shot_frame[4];
    int shot_count = 0;
    int click_x = -1, click_y = -1;
    long click_frame = -1;
    char exec_text[4][128];
    long exec_frame[4];
    int exec_count = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--manifest") == 0 && i + 1 < argc )
            manifest = argv[++i];
        else if( strcmp(argv[i], "--user") == 0 && i + 1 < argc )
            cfg.connect_user = argv[++i];
        else if( strcmp(argv[i], "--pass") == 0 && i + 1 < argc )
            cfg.connect_pass = argv[++i];
        else if( strcmp(argv[i], "--window") == 0 && i + 1 < argc )
        {
            /* Applied after the manifest, so the command line wins. */
            if( sscanf(argv[++i], "%dx%d", &window_w, &window_h) != 2 )
            {
                usage(argv[0]);
                return 2;
            }
        }
        else if( strcmp(argv[i], "--frames") == 0 && i + 1 < argc )
            max_frames = atol(argv[++i]);
        else if( strcmp(argv[i], "--shot") == 0 && i + 1 < argc && shot_count < 4 )
        {
            const char* v = argv[++i];
            const char* at = strrchr(v, '@');
            if( !at || at == v )
            {
                usage(argv[0]);
                return 2;
            }
            snprintf(shot_path[shot_count], sizeof(shot_path[0]), "%.*s", (int)(at - v), v);
            shot_frame[shot_count++] = atol(at + 1);
        }
        else if( strcmp(argv[i], "--exec") == 0 && i + 1 < argc && exec_count < 4 )
        {
            const char* v = argv[++i];
            const char* at = strrchr(v, '@');
            if( !at || at == v )
            {
                usage(argv[0]);
                return 2;
            }
            snprintf(exec_text[exec_count], sizeof(exec_text[0]), "%.*s", (int)(at - v), v);
            exec_frame[exec_count++] = atol(at + 1);
        }
        else if( strcmp(argv[i], "--click") == 0 && i + 1 < argc )
        {
            if( sscanf(argv[++i], "%d,%d@%ld", &click_x, &click_y, &click_frame) != 3 )
            {
                usage(argv[0]);
                return 2;
            }
        }
        else if( strcmp(argv[i], "--assets") == 0 && i + 1 < argc )
        {
            const char* v = argv[++i];
            const char* colon = strrchr(v, ':');
            if( !colon || colon == v )
            {
                usage(argv[0]);
                return 2;
            }
            snprintf(assets_host, sizeof(assets_host), "%.*s", (int)(colon - v), v);
            assets_port = atoi(colon + 1);
        }
        else
        {
            usage(argv[0]);
            return 2;
        }
    }
    if( !manifest )
    {
        usage(argv[0]);
        return 2;
    }

    /* ---- boot: manifest -> config -> App_Init (src/main.c 6858-7212) ---- */
    ToriRS_ExecutorConfig_Init(&executor_cfg);
    if( BootManifest_LoadFile(&boot_manifest, manifest) != 0 )
        return 1;
    BootManifest_ApplyToConfig(&boot_manifest, &cfg);
    BootManifest_ApplyToExecutorConfig(&boot_manifest, &executor_cfg);
    if( window_w > 0 && window_h > 0 )
    {
        cfg.window_w = window_w;
        cfg.window_h = window_h;
        cfg.window_mode = CS2VM_WINDOW_MODE_RESIZABLE;
    }
    if( !cfg.cache_identity_set )
    {
        fprintf(stderr, "pokeclient: the manifest states no [cache:boot] identity\n");
        return 1;
    }
    if( !cfg.cache_dir )
    {
        fprintf(stderr, "pokeclient: the manifest states no cache dir\n");
        return 1;
    }
    {
        char error[192];
        if( ToriRS_ExecutorConfig_ResolveJs5(&executor_cfg, &cfg, error, sizeof(error)) != 0 )
        {
            fprintf(stderr, "pokeclient: %s\n", error);
            return 1;
        }
    }
    if( cfg.interface_id == INTERFACE_ID_UNSET )
    {
        struct RevConfigRefs refs;
        RevConfigRefs_Init(&refs);
        RevConfigRefs_LoadSources(&refs, cfg.revconfig_ui_ini, cfg.revconfig_cache_ini,
                                  cfg.revconfig_inline_ini);
        int declared = RevConfigRefs_Get(&refs, "iface", "boot");
        RevConfigRefs_Free(&refs);
        if( declared > 0 )
            cfg.interface_id = declared;
    }
    if( cfg.window_w > 0 && cfg.window_h > 0 && cfg.window_mode != CS2VM_WINDOW_MODE_FIXED )
        UITree_LayoutSetRootSize(cfg.window_w, cfg.window_h);

    /* Tell the server, through the login header, that this is the 2D client
     * (src/pokesprite/pokesprite_format.h). */
    cfg.clienttype = POKESPRITE_CLIENT_TYPE;
    App_Init(&app, &cfg);
    if( !cfg.window_mode && App_UiLogic(&app) == APP_UI_LOGIC_CS1 )
        cfg.window_mode = CS2VM_WINDOW_MODE_FIXED;
    App_SetBootWindowMode(&app, cfg.window_mode);
    if( app.net_enabled && App_HasTitleScreen(&app) )
        App_BootGameframeThenTitle(&app);
    else
        App_OpenRootInterface(
            &app, app.net_enabled || cfg.interface_id == INTERFACE_ID_UNSET ? -1 : cfg.interface_id);

    /* ---- window: software present only (main.c 8121-8230) ---- */
    struct PlatformWindow* platform = PlatformWindow_New();
    assert(platform);
    if( !PlatformWindow_Init(platform, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H,
                             "PokeSprite") ||
        !PlatformWindow_SetPresent(platform, PLATFORM_PRESENT_SOFTWARE) )
    {
        fprintf(stderr, "pokeclient: window init failed\n");
        PlatformWindow_Free(platform);
        App_Shutdown(&app);
        return 1;
    }
    App_SetWorldRenderMode(&app, TORIRS_WORLD_PAINTER);
    App_SetRendererAnimatesTextures(&app, false);

    struct ToriRS_CmdBus bus;
    CmdBus_Init(&bus);
    App_SetChromeScale(&app, PlatformWindow_PixelDensity(platform));
    {
        int const resizable = App_WindowMode(&app) == CS2VM_WINDOW_MODE_RESIZABLE;
        PlatformWindow_SetCanvasFollowsWindow(
            platform, &bus, resizable, resizable ? APP_CANVAS_MIN_W : App_FixedWindowWidth(&app),
            resizable ? APP_CANVAS_MIN_H : App_FixedWindowHeight(&app));
        if( !resizable )
            CmdBus_PushWindowResize(&bus, APP_CANVAS_MIN_W, APP_CANVAS_MIN_H);
    }
    App_SetAudioDevicePresent(&app, false);
    struct LibToriRS_Input* input = LibToriRS_Input_Init(&input_storage, PlatformWindow_Ticks64());

    struct ToriRS_Pacer pacer;
    ToriRS_Pacer_Init(&pacer, TORIRS_PACER_DEADLINE, FRAME_PERIOD_MS, 1);

    /* ---- transport (main.c 8557-8596) ---- */
    struct NetTransport* sock = NULL;
    if( app.net )
    {
        int kind = app.net->rev->transport_kind;
        if( strcmp(boot_manifest.transport, "embed") == 0 )
            kind = NET_TRANSPORT_EMBED;
        else if( strcmp(boot_manifest.transport, "ws") == 0 )
            kind = NET_TRANSPORT_WS;
        else if( strcmp(boot_manifest.transport, "tcp") == 0 )
            kind = NET_TRANSPORT_TCP;
        if( kind == NET_TRANSPORT_EMBED && !getenv("TORIRSSERVER_CACHE") )
            setenv("TORIRSSERVER_CACHE", cfg.cache_dir, 0);
        if( kind == NET_TRANSPORT_EMBED && cfg.net_server_scripts && cfg.net_server_scripts[0] &&
            !getenv("TORIRSSERVER_SCRIPTS") )
            setenv("TORIRSSERVER_SCRIPTS", cfg.net_server_scripts, 0);
        sock = NetTransport_New(kind, cfg.connect_port > 0 ? cfg.connect_port : 43594,
                                app.net->rev->name);
    }

    struct PS_Fetch* fetch = PS_FetchNew(assets_host, assets_port);
    struct PS_World* world = PS_WorldNew(fetch);
    fprintf(stderr, "pokeclient: sheets from http://%s:%d/pokesprite/\n", assets_host, assets_port);

    /* ---- frames (main.c frame_loop_step, software lane) ---- */
    for( long frame_no = 0; !PlatformWindow_QuitRequested(platform); frame_no++ )
    {
        if( max_frames > 0 && frame_no >= max_frames )
            break;
        uint64_t const now = PlatformWindow_Ticks64();
        uint64_t const logic_now = ToriRS_Pacer_BeginFrame(&pacer, now);
        CmdBus_PushFrame(&bus, now);
        PlatformWindow_PollCommands(platform, &bus);
        /* --exec: a "::" command, through the bus as typing it would go. */
        for( int e = 0; e < exec_count; e++ )
            if( frame_no == exec_frame[e] )
                CmdBus_PushExecText(&bus, exec_text[e]);
        /* --click: a real press and release through the bus, as SDL would. */
        if( frame_no == click_frame - 2 )
            CmdBus_PushMouseMove(&bus, (int16_t)click_x, (int16_t)click_y);
        if( frame_no == click_frame )
            CmdBus_PushMouseButton(&bus, TORIRS_CMD_INPUT_MOUSE_DOWN, 1, (int16_t)click_x,
                                   (int16_t)click_y);
        if( frame_no == click_frame + 2 )
            CmdBus_PushMouseButton(&bus, TORIRS_CMD_INPUT_MOUSE_UP, 1, (int16_t)click_x,
                                   (int16_t)click_y);
        if( sock )
            NetTransport_Poll(sock, app.net, &bus);

        LibToriRS_Input_Begin(input, now);
        App_DrainCommands(&app, &bus, input);
        LibToriRS_Input_End(input);

        PlatformWindow_SetLayoutSize(platform, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        PlatformWindow_Resize(platform, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

        int const redraw = App_RunOnce(&app, logic_now, input);
        ToriRS_Pacer_SetCapFps(&pacer, App_FrameCapFps(&app));

        /* The 2D world animates on its own clock, so draw every frame. */
        (void)redraw;
        if( PlatformWindow_CanPresent(platform) )
        {
            PS_Render(&app, world, PlatformWindow_Pixels(platform), PlatformWindow_Width(platform),
                      PlatformWindow_Height(platform), now);
            for( int k = 0; k < shot_count; k++ )
                if( frame_no == shot_frame[k] )
                {
                    bmp_write_file(shot_path[k], PlatformWindow_Pixels(platform),
                                   PlatformWindow_Width(platform), PlatformWindow_Height(platform));
                    fprintf(stderr, "pokeclient: wrote %s at frame %ld\n", shot_path[k], frame_no);
                }
            PlatformWindow_Present(platform);
        }

        App_SetDisplayDensity(&app, PlatformWindow_DisplayDensityPercent(platform));
        App_SyncClientScale(&app);
        {
            int keyboard_on = 0;
            if( App_TakeTextInputChange(&app, &keyboard_on) )
                PlatformWindow_SetTextInput(platform, keyboard_on);
        }
        {
            int new_mode = 0;
            if( App_TakeWindowModeChange(&app, &new_mode) )
            {
                int const resizable = new_mode == CS2VM_WINDOW_MODE_RESIZABLE;
                PlatformWindow_SetCanvasFollowsWindow(
                    platform, &bus, resizable,
                    resizable ? APP_CANVAS_MIN_W : App_FixedWindowWidth(&app),
                    resizable ? APP_CANVAS_MIN_H : App_FixedWindowHeight(&app));
                if( !resizable )
                    CmdBus_PushWindowResize(&bus, APP_CANVAS_MIN_W, APP_CANVAS_MIN_H);
            }
        }
        fflush(stderr);
        PlatformWindow_SleepUntil(
            ToriRS_Pacer_WaitDeadline(&pacer, now, PlatformWindow_Ticks64()));
    }

    PS_WorldFree(world);
    PS_FetchFree(fetch);
    NetTransport_Free(sock);
    PlatformWindow_Free(platform);
    App_Shutdown(&app);
    return 0;
}
