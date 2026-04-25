#include "platform_impl2_sdl2.h"
#include "platform_impl2_sdl2_internal.h"

#ifdef __EMSCRIPTEN__

bool
Platform2_SDL2_InitForSoft3D(
    struct Platform2_SDL2* platform,
    int canvas_width,
    int canvas_height)
{
    if( !Platform2_SDL2_RendererFlagSupported(TORIRS_SDL2_RENDERER_SOFT3D) )
        return false;
    return PlatformImpl2_SDL2_Port_Emscripten_InitForSoft3D(platform, canvas_width, canvas_height);
}

void
Platform2_SDL2_SyncCanvasCssSize(
    struct Platform2_SDL2* platform,
    struct GGame* game_nullable)
{
    PlatformImpl2_SDL2_Port_Emscripten_SyncCanvasCssSize(platform, game_nullable);
}

struct Platform2_SDL2*
Platform2_SDL2_New(void)
{
    struct Platform2_SDL2* platform =
        (struct Platform2_SDL2*)malloc(sizeof(struct Platform2_SDL2));
    memset(platform, 0, sizeof(struct Platform2_SDL2));
    if( !PlatformImpl2_SDL2_Port_Emscripten_InitNewFields(platform) )
    {
        free(platform);
        return NULL;
    }
    return platform;
}

void
Platform2_SDL2_Free(struct Platform2_SDL2* platform)
{
    if( !platform )
        return;
    PlatformImpl2_SDL2_Port_Emscripten_FreeExtra(platform);
    free(platform);
}

void
Platform2_SDL2_Shutdown(struct Platform2_SDL2* platform)
{
    if( !platform )
        return;
    if( platform->window )
    {
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
    }
    SDL_Quit();
}

void
Platform2_SDL2_PollEvents(
    struct Platform2_SDL2* platform,
    struct GInput* input,
    int chat_focused)
{
    PlatformImpl2_SDL2_Port_Emscripten_PollEvents(platform, input, chat_focused);
}

void
Platform2_SDL2_RunLuaScripts(struct Platform2_SDL2* platform, struct GGame* game)
{
    PlatformImpl2_SDL2_Port_Emscripten_RunLuaScripts(platform, game);
}

#else

extern "C" {
#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "common/tori_rs_sdl2_gameinput.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/filepack.h"
#include "osrs/game.h"
#include "osrs/gameproto_parse.h"
#include "osrs/gio_cache_dat.h"
#include "osrs/lua_sidecar/lua_api.h"
#include "osrs/lua_sidecar/lua_configfile.h"
#include "osrs/lua_sidecar/lua_gametypes.h"
#include "osrs/lua_sidecar/luac_gametypes.h"
#include "osrs/lua_sidecar/luac_sidecar.h"
#include "osrs/lua_sidecar/luac_sidecar_cachedat.h"
#include "osrs/lua_sidecar/luac_sidecar_config.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables_dat/config_idk.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "osrs/rscache/tables_dat/config_textures.h"
#include "osrs/rscache/tables_dat/config_versionlist_mapsquare.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
#include "osrs/scripts/lua_cache_fnnos.h"
#include "osrs/texture.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CACHE_PATH
#define CACHE_PATH "../cache254"
#endif
#ifndef LUA_SCRIPTS_DIR
#define LUA_SCRIPTS_DIR "../src/osrs/scripts"
#endif

static struct LuaGameType*
game_callback(
    void* ctx,
    struct LuaGameType* args)
{
    struct Platform2_SDL2* platform = (struct Platform2_SDL2*)ctx;
    struct BuildCacheDat* bcd = platform->current_game->buildcachedat;

    struct LuaGameType* command_gametype = LuaGameType_GetVarTypeArrayAt(args, 0);
    assert(command_gametype->kind == LUAGAMETYPE_INT);
    LuaApiId api_id = (LuaApiId)LuaGameType_GetInt(command_gametype);

    struct LuaGameType* args_view = LuaGameType_NewVarTypeArrayView(args, 1);
    struct LuaGameType* result = lua_api_dispatch(
        platform->current_game,
        bcd,
        platform->current_game->sys_dash,
        api_id,
        args_view);
    LuaGameType_Free(args_view);

    if( result )
        return result;
    return LuaGameType_NewVoid();
}

struct Platform2_SDL2*
Platform2_SDL2_New(void)
{
    struct Platform2_SDL2* platform =
        (struct Platform2_SDL2*)malloc(sizeof(struct Platform2_SDL2));
    memset(platform, 0, sizeof(struct Platform2_SDL2));

    platform->lua_sidecar = LuaCSidecar_New(platform, game_callback);
    if( !platform->lua_sidecar )
    {
        free(platform);
        return NULL;
    }

    platform->cache_dat = cache_dat_new_from_directory(CACHE_PATH);

    return platform;
}

void
Platform2_SDL2_Shutdown(struct Platform2_SDL2* platform)
{
    if( !platform )
        return;
    if( platform->window )
    {
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
    }
    if( SDL_WasInit(SDL_INIT_VIDEO) )
        SDL_Quit();
}

void
Platform2_SDL2_Free(struct Platform2_SDL2* platform)
{
    if( !platform )
        return;
    Platform2_SDL2_Shutdown(platform);
    if( platform->lua_sidecar )
        LuaCSidecar_Free(platform->lua_sidecar);
    if( platform->cache_dat )
        cache_dat_free(platform->cache_dat);
    free(platform);
}

bool
Platform2_SDL2_InitForSoft3D(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height)
{
    if( !Platform2_SDL2_RendererFlagSupported(TORIRS_SDL2_RENDERER_SOFT3D) )
        return false;
#if defined(__APPLE__)
    return PlatformImpl2_SDL2_Port_OSX_InitForSoft3D(platform, screen_width, screen_height);
#elif defined(_WIN32)
    return PlatformImpl2_SDL2_Port_Win_InitForSoft3D(platform, screen_width, screen_height);
#else
    return PlatformImpl2_SDL2_Port_Linux_InitForSoft3D(platform, screen_width, screen_height);
#endif
}

#if !defined(_WIN32)
bool
Platform2_SDL2_InitForOpenGL3(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height)
{
    if( !Platform2_SDL2_RendererFlagSupported(TORIRS_SDL2_RENDERER_OPENGL3) )
        return false;
#if defined(__APPLE__)
    return PlatformImpl2_SDL2_Port_OSX_InitForOpenGL3(platform, screen_width, screen_height);
#else
    return PlatformImpl2_SDL2_Port_Linux_InitForOpenGL3(platform, screen_width, screen_height);
#endif
}
#endif

bool
Platform2_SDL2_InitForMetal(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height)
{
    if( !Platform2_SDL2_RendererFlagSupported(TORIRS_SDL2_RENDERER_METAL) )
        return false;
#if defined(__APPLE__)
    return PlatformImpl2_SDL2_Port_OSX_InitForMetal(platform, screen_width, screen_height);
#else
    (void)platform;
    (void)screen_width;
    (void)screen_height;
    return false;
#endif
}

#if defined(_WIN32)
bool
Platform2_SDL2_InitForD3D11(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height)
{
    if( !Platform2_SDL2_RendererFlagSupported(TORIRS_SDL2_RENDERER_D3D11) )
        return false;
    return PlatformImpl2_SDL2_Port_Win_InitForD3D11(platform, screen_width, screen_height);
}
#endif

/** Map raw window-pixel mouse coordinates to game_screen_width × game_screen_height space,
 *  accounting for aspect-ratio letterboxing.  Used by non-soft3D renderers (Metal, OpenGL3, D3D11)
 *  where game_map_soft3d_window_mouse_to_buffer is not active. */
static void
sdl2_transform_mouse_coordinates(
    int window_mouse_x,
    int window_mouse_y,
    int* game_mouse_x,
    int* game_mouse_y,
    int window_w,
    int window_h,
    int game_screen_w,
    int game_screen_h)
{
    if( window_w <= 0 || window_h <= 0 || game_screen_w <= 0 || game_screen_h <= 0 )
        return;

    float src_aspect = (float)game_screen_w / (float)game_screen_h;
    float dst_aspect = (float)window_w / (float)window_h;

    int dst_x, dst_y, dst_w, dst_h;

    if( src_aspect > dst_aspect )
    {
        dst_w = window_w;
        dst_h = (int)(window_w / src_aspect);
        dst_x = 0;
        dst_y = (window_h - dst_h) / 2;
    }
    else
    {
        dst_h = window_h;
        dst_w = (int)(window_h * src_aspect);
        dst_y = 0;
        dst_x = (window_w - dst_w) / 2;
    }

    if( window_mouse_x < dst_x || window_mouse_x >= dst_x + dst_w || window_mouse_y < dst_y ||
        window_mouse_y >= dst_y + dst_h )
    {
        *game_mouse_x = -1;
        *game_mouse_y = -1;
    }
    else
    {
        float relative_x = (float)(window_mouse_x - dst_x) / (float)dst_w;
        float relative_y = (float)(window_mouse_y - dst_y) / (float)dst_h;

        *game_mouse_x = (int)(relative_x * game_screen_w);
        *game_mouse_y = (int)(relative_y * game_screen_h);
    }
}

void
Platform2_SDL2_PollEvents(
    struct Platform2_SDL2* platform,
    struct GInput* input,
    int chat_focused)
{
    (void)chat_focused;
    double time_s = (double)(SDL_GetTicks64()) / 1000.0;
    ToriRSLibPlatform_SDL2_GameInput_PollEvents(input, time_s);

    /* For non-soft3D renderers (Metal, OpenGL3, D3D11), map raw SDL window-pixel mouse
     * coordinates to game_screen space.  Soft3D renderers set soft3d_mouse_from_window=true
     * and rely on game_map_soft3d_window_mouse_to_buffer in the game layer instead. */
    struct GGame* game = platform->current_game;
    if( game && !game->soft3d_mouse_from_window && platform->window )
    {
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        /* Use logical window size (not drawable/framebuffer size) since SDL mouse events
         * report logical pixel coordinates even on HiDPI displays. */
        int ww = 0, wh = 0;
        SDL_GetWindowSize(platform->window, &ww, &wh);
        sdl2_transform_mouse_coordinates(
            mx, my,
            &input->mouse_state.x, &input->mouse_state.y,
            ww, wh,
            platform->game_screen_width, platform->game_screen_height);
    }
}

static struct LuaGameType*
on_lua_async_call(
    struct Platform2_SDL2* platform,
    int command,
    struct LuaGameType* args)
{
    struct CacheDat* cache_dat = platform->cache_dat;

    switch( command )
    {
    case FUNC_LOAD_ARCHIVE:
        return LuaCSidecar_CachedatLoadArchive(cache_dat, args);
    case FUNC_LOAD_ARCHIVES:
        return LuaCSidecar_CachedatLoadArchives(cache_dat, args);
    case FUNC_LOAD_CONFIG_FILE:
        return LuaCSidecar_Config_LoadConfig(args);
    case FUNC_LOAD_CONFIG_FILES:
    {
        struct LuaGameType* result = LuaCSidecar_Config_LoadConfigs(args);
        int n = LuaGameType_GetUserDataArrayCount(result);
        for( int i = 0; i < n; i++ )
        {
            void* file = LuaGameType_GetUserDataArrayAt(result, i);
            if( file )
                LuaCSidecar_TrackConfigFile(platform->lua_sidecar, (struct LuaConfigFile*)file);
        }
        return result;
    }
    default:
        assert(false && "Unknown cachedat function");
        return NULL;
    }
}

#if LUA_VERSION_NUM >= 504
static int
lua_resume_compat(
    lua_State* co,
    lua_State* from,
    int narg)
{
    int nres = 0;
    return lua_resume(co, from, narg, &nres);
}
#else
#define lua_resume_compat lua_resume
#endif

void
Platform2_SDL2_RunLuaScripts(
    struct Platform2_SDL2* platform,
    struct GGame* game)
{
    while( !LibToriRS_LuaScriptQueueIsEmpty(game) )
    {
        struct LuaGameScript script;
        memset(&script, 0, sizeof(script));
        LibToriRS_LuaScriptQueuePop(game, &script);

        if( !platform->lua_sidecar )
            return;

        struct LuaCYield yield = { 0 };
        int script_status = LuaCSidecar_RunScript(platform->lua_sidecar, &script, &yield);
        LuaGameType_Free(script.args);
        script.args = NULL;

        while( script_status == LUACSIDECAR_YIELDED )
        {
            struct LuaGameType* result = on_lua_async_call(platform, yield.command, yield.args);
            LuaGameType_Free(yield.args);
            yield.args = NULL;

            script_status = LuaCSidecar_ResumeScript(platform->lua_sidecar, result, &yield);
            LuaGameType_Free(result);
        }

        LuaCSidecar_GC(platform->lua_sidecar);

        void* pkt_free = script.lc245_packet_item_to_free;
        if( pkt_free )
        {
            gameproto_free_lc245_2_item((struct RevPacket_LC245_2_Item*)pkt_free);
            free(pkt_free);
        }
    }
}

#endif
