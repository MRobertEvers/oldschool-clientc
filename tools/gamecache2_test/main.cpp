/*
 * gamecache2_test — standalone SDL2 test for GameCache2 + Dat1 worldbuild.
 *
 * Usage:
 *   gamecache2_test [OPTIONS]
 *
 * Options:
 *   --cache=DIR             CacheDat directory (default: ../../cache254)
 *   --script=PATH           Lua worldbuild script (default: <test-dir>/gamecache2_test.lua)
 *   --zone=X,Z              Zone center in tile coords  (default: 3200,3200)
 *   --scene-size=N          Scene half-size in tiles    (default: 104)
 *   -h, --help              Print this help message
 *
 * Controls:
 *   W/S / Arrow Up/Down     Move camera forward/back  (scene-space Z)
 *   A/D / Arrow Left/Right  Strafe camera left/right  (scene-space X)
 *   Q/E                     Move camera up/down
 *   Mouse drag (left)       Rotate yaw/pitch
 *   Escape                  Quit
 */

#define SDL_MAIN_HANDLED

extern "C" {
#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "lua_gc2_test.h"
#include "osrs/gamecache2/gamecache2.h"
#include "osrs/ginput.h"
#include "osrs/world.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

#include "platforms/platform_impl2_sdl2.h"
#include "platforms/platform_impl2_sdl2_renderer_soft3d.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Defaults
 * -------------------------------------------------------------------------*/

#ifndef GAMECACHE2_TEST_ROOT
#define GAMECACHE2_TEST_ROOT "../.."
#endif

#define DEFAULT_CACHE_DIR GAMECACHE2_TEST_ROOT "/cache254"
#define DEFAULT_SCRIPT_PATH GAMECACHE2_TEST_ROOT "/tools/gamecache2_test/gamecache2_test.lua"

static constexpr int kViewportInset = 4;
static constexpr int kGameW = 765;
static constexpr int kGameH = 503;
static constexpr int kScreenW = kViewportInset + kGameW + kViewportInset;
static constexpr int kScreenH = kViewportInset + kGameH + kViewportInset;

/* Default OSRS Lumbridge area (zone tiles, not world pixels) */
static constexpr int kDefaultZoneX = 3200;
static constexpr int kDefaultZoneZ = 3200;
static constexpr int kDefaultSceneSz = 104;

/* Camera speed increments per frame */
static constexpr int kMoveSpeed = 60;
static constexpr int kRotSpeed = 10;

/* ---------------------------------------------------------------------------
 * CLI
 * -------------------------------------------------------------------------*/

struct Cli
{
    const char* cache_dir;
    const char* script_path;
    int zone_center_x;
    int zone_center_z;
    int scene_size;
    bool help;
};

static void
print_help(const char* prog)
{
    fprintf(
        stderr,
        "Usage: %s [--cache=DIR] [--script=PATH] [--zone=X,Z] [--scene-size=N]\n"
        "  --cache=DIR      CacheDat directory  (default: %s)\n"
        "  --script=PATH    Lua script path     (default: %s)\n"
        "  --zone=X,Z       Zone center tiles   (default: %d,%d)\n"
        "  --scene-size=N   Scene half-size     (default: %d)\n"
        "Controls: W/S/A/D move, Q/E up/down, mouse drag rotates, Escape quits\n",
        prog,
        DEFAULT_CACHE_DIR,
        DEFAULT_SCRIPT_PATH,
        kDefaultZoneX,
        kDefaultZoneZ,
        kDefaultSceneSz);
}

static const char*
arg_value(
    const char* arg,
    const char* prefix)
{
    size_t n = strlen(prefix);
    if( strncmp(arg, prefix, n) == 0 )
        return arg + n;
    return NULL;
}

static Cli
parse_cli(
    int argc,
    char* argv[])
{
    Cli cli = {};
    cli.cache_dir = DEFAULT_CACHE_DIR;
    cli.script_path = DEFAULT_SCRIPT_PATH;
    cli.zone_center_x = kDefaultZoneX;
    cli.zone_center_z = kDefaultZoneZ;
    cli.scene_size = kDefaultSceneSz;

    for( int i = 1; i < argc; i++ )
    {
        const char* v;
        if( (v = arg_value(argv[i], "--cache=")) )
        {
            cli.cache_dir = v;
            continue;
        }
        if( (v = arg_value(argv[i], "--script=")) )
        {
            cli.script_path = v;
            continue;
        }
        if( (v = arg_value(argv[i], "--zone=")) )
        {
            sscanf(v, "%d,%d", &cli.zone_center_x, &cli.zone_center_z);
            continue;
        }
        if( (v = arg_value(argv[i], "--scene-size=")) )
        {
            cli.scene_size = atoi(v);
            continue;
        }
        if( strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 )
        {
            cli.help = true;
            continue;
        }
        fprintf(stderr, "Unknown argument: %s\n", argv[i]);
    }
    return cli;
}

/* ---------------------------------------------------------------------------
 * Cleanup helper
 * -------------------------------------------------------------------------*/

static void
cleanup(
    struct GGame* game,
    struct GameCache2* gc2,
    struct Platform2_SDL2* platform,
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    struct ToriRSRenderCommandBuffer* rcb,
    struct ToriRSNetSharedBuffer* net)
{
    if( game )
        LibToriRS_GameFree(game);

    if( renderer )
        PlatformImpl2_SDL2_Renderer_Soft3D_Free(renderer);
    if( platform )
        Platform2_SDL2_Free(platform);
    if( rcb )
        LibToriRS_RenderCommandBufferFree(rcb);
    if( net )
        LibToriRS_NetFreeBuffer(net);
}

/* ---------------------------------------------------------------------------
 * Camera helpers — game stores yaw/pitch/world_x/y/z in RS units
 * -------------------------------------------------------------------------*/

static void
camera_apply_keyboard(
    struct GGame* game,
    const Uint8* keys)
{
    const int move = kMoveSpeed;
    const int rot = kRotSpeed;

    if( keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] )
        game->camera_world_z += move;
    if( keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] )
        game->camera_world_z -= move;
    if( keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] )
        game->camera_world_x -= move;
    if( keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] )
        game->camera_world_x += move;
    if( keys[SDL_SCANCODE_Q] )
        game->camera_world_y += move;
    if( keys[SDL_SCANCODE_E] )
        game->camera_world_y -= move;
    if( keys[SDL_SCANCODE_COMMA] )
        game->camera_yaw = (game->camera_yaw - rot + 2048) % 2048;
    if( keys[SDL_SCANCODE_PERIOD] )
        game->camera_yaw = (game->camera_yaw + rot) % 2048;
}

/* ---------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/

int
main(
    int argc,
    char* argv[])
{
    Cli cli = parse_cli(argc, argv);
    if( cli.help )
    {
        print_help(argv[0]);
        return 0;
    }

    /* --- allocations --- */
    struct ToriRSNetSharedBuffer* net = LibToriRS_NetNewBuffer();
    struct GGame* game = LibToriRS_GameNew(net, kGameW, kGameH);
    struct ToriRSRenderCommandBuffer* rcb = LibToriRS_RenderCommandBufferNew(1024);
    struct Platform2_SDL2* platform = Platform2_SDL2_New();

    struct CacheDat* cache_dat = cache_dat_new_from_directory(cli.cache_dir);
    if( !cache_dat )
    {
        fprintf(stderr, "[gc2_test] cache_dat_new_from_directory failed\n");
        cleanup(game, NULL, platform, NULL, rcb, net);
        return 1;
    }

    if( !net || !game || !rcb || !platform )
    {
        fprintf(stderr, "[gc2_test] allocation failed\n");
        cleanup(game, NULL, platform, NULL, rcb, net);
        return 1;
    }

    /* --- SDL2 init --- */
    if( !Platform2_SDL2_InitForSoft3D(platform, kScreenW, kScreenH) )
    {
        fprintf(stderr, "[gc2_test] SDL2 init failed\n");
        cleanup(game, NULL, platform, NULL, rcb, net);
        return 1;
    }

    SDL_SetWindowTitle(platform->window, "GameCache2 Test");
    platform->current_game = game;

    /* --- Soft3D renderer --- */
    struct Platform2_SDL2_Renderer_Soft3D* renderer =
        PlatformImpl2_SDL2_Renderer_Soft3D_New(kScreenW, kScreenH, kGameW, kGameH);
    if( !renderer || !PlatformImpl2_SDL2_Renderer_Soft3D_Init(renderer, platform) )
    {
        fprintf(stderr, "[gc2_test] Soft3D init failed\n");
        if( renderer )
            PlatformImpl2_SDL2_Renderer_Soft3D_Free(renderer);
        cleanup(game, NULL, platform, NULL, rcb, net);
        return 1;
    }

    /* --- Viewport setup (mirrors test/sdl2.cpp) --- */
    game->iface_view_port->width = kGameW;
    game->iface_view_port->height = kGameH;
    game->iface_view_port->x_center = kGameW / 2;
    game->iface_view_port->y_center = kGameH / 2;
    game->iface_view_port->stride = kGameW;
    game->iface_view_port->clip_left = 0;
    game->iface_view_port->clip_top = 0;
    game->iface_view_port->clip_right = kGameW;
    game->iface_view_port->clip_bottom = kGameH;

    game->viewport_offset_x = kViewportInset;
    game->viewport_offset_y = kViewportInset;
    PlatformImpl2_SDL2_Renderer_Soft3D_SetDashOffset(renderer, kViewportInset, kViewportInset);

    LibToriRS_GameSetWorldViewportSize(game, kGameW, kGameH);

    /* --- GameCache2 (wrapping game->gamecache) --- */
    struct GameCache2* gc2 = GameCache2_New();
    if( !gc2 )
    {
        fprintf(stderr, "[gc2_test] GameCache2_New failed\n");
        cleanup(game, NULL, platform, renderer, rcb, net);
        return 1;
    }

    printf("[gc2_test] running worldbuild script: %s\n", cli.script_path);
    printf(
        "[gc2_test] zone=(%d,%d) scene_size=%d\n",
        cli.zone_center_x,
        cli.zone_center_z,
        cli.scene_size);

    /* --- World + scene build --- */
    game->world = world_new(game->gamecache, game->scene2);
    if( !game->world )
    {
        fprintf(stderr, "[gc2_test] world_new failed\n");
        cleanup(game, gc2, platform, renderer, rcb, net);
        return 1;
    }

    /* --- Run worldbuild Lua script --- */
    lua_State* L = luaL_newstate();
    if( !L )
    {
        fprintf(stderr, "[gc2_test] luaL_newstate failed\n");
        cleanup(game, gc2, platform, renderer, rcb, net);
        return 1;
    }
    luaL_openlibs(L);

    if( gamecache2_test_run_worldbuild_script(L, cli.script_path, gc2, cache_dat) != 0 )
    {
        fprintf(stderr, "[gc2_test] worldbuild script failed\n");
        lua_close(L);
        cleanup(game, gc2, platform, renderer, rcb, net);
        return 1;
    }
    lua_close(L);

    printf("[gc2_test] worldbuild done, building scene...\n");

    /* --- Render loop --- */
    static struct GInput input = {};
    bool mouse_dragging = false;
    int mouse_drag_last_x = 0;
    int mouse_drag_last_y = 0;

    while( LibToriRS_GameIsRunning(game) )
    {
        SDL_Event ev;
        while( SDL_PollEvent(&ev) )
        {
            if( ev.type == SDL_QUIT )
            {
                game->running = false;
                break;
            }
            if( ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE )
            {
                game->running = false;
                break;
            }
            if( ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT )
            {
                mouse_dragging = true;
                mouse_drag_last_x = ev.button.x;
                mouse_drag_last_y = ev.button.y;
            }
            if( ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT )
                mouse_dragging = false;
            if( ev.type == SDL_MOUSEMOTION && mouse_dragging )
            {
                int dx = ev.motion.x - mouse_drag_last_x;
                int dy = ev.motion.y - mouse_drag_last_y;
                mouse_drag_last_x = ev.motion.x;
                mouse_drag_last_y = ev.motion.y;
                game->camera_yaw = (game->camera_yaw + dx * 2 + 2048) % 2048;
                game->camera_pitch = game->camera_pitch + dy * 2;
                if( game->camera_pitch < 128 )
                    game->camera_pitch = 128;
                if( game->camera_pitch > 2040 )
                    game->camera_pitch = 2040;
            }
        }

        const Uint8* keys = SDL_GetKeyboardState(NULL);
        camera_apply_keyboard(game, keys);

        game->tick_ms = SDL_GetTicks64();

        LibToriRS_GameStep(game, &input, rcb);

        PlatformImpl2_SDL2_Renderer_Soft3D_Render(renderer, game, rcb);
    }

    /* --- Cleanup --- */
    gc2 = NULL;

    cleanup(game, NULL, platform, renderer, rcb, net);
    return 0;
}
