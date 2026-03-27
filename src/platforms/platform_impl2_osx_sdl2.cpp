#include "platform_impl2_osx_sdl2.h"

extern "C" {
#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "common/tori_rs_sdl2_gameinput.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/filepack.h"
#include "osrs/game.h"
#include "osrs/gio_cache_dat.h"
#include "osrs/lua_sidecar/lua_buildcachedat.h"
#include "osrs/lua_sidecar/lua_configfile.h"
#include "osrs/lua_sidecar/lua_dash.h"
#include "osrs/lua_sidecar/lua_game.h"
#include "osrs/lua_sidecar/lua_gametypes.h"
#include "osrs/lua_sidecar/lua_ui.h"
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

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_PATH "../cache254"
#define LUA_SCRIPTS_DIR "../src/osrs/scripts"

static struct LuaGameType*
game_callback(
    void* ctx,
    struct LuaGameType* args)
{
    struct Platform2_OSX_SDL2* platform = (struct Platform2_OSX_SDL2*)ctx;
    struct BuildCacheDat* bcd = platform->current_game->buildcachedat;

    struct LuaGameType* command_gametype = LuaGameType_GetVarTypeArrayAt(args, 0);
    assert(command_gametype->kind == LUAGAMETYPE_STRING);
    const char* command = LuaGameType_GetString(command_gametype);

    struct LuaGameType* result = NULL;
    struct LuaGameType* args_view = LuaGameType_NewVarTypeArrayView(args, 1);

    if( LuaBuildCacheDat_CommandHasPrefix((char*)command) )
    {
        result = LuaBuildCacheDat_DispatchCommand(bcd, (char*)command, args_view);
    }
    else if( LuaDash_CommandHasPrefix((char*)command) )
    {
        result = LuaDash_DispatchCommand(
            platform->current_game->sys_dash, bcd, (char*)command, args_view);
    }
    else if( LuaGame_CommandHasPrefix((char*)command) )
    {
        result = LuaGame_DispatchCommand(platform->current_game, (char*)command, args_view);
    }
    else if( LuaUI_CommandHasPrefix((char*)command) )
    {
        result = LuaUI_DispatchCommand(platform->current_game, bcd, (char*)command, args_view);
    }

    LuaGameType_Free(args_view);

    if( result )
        return result;
    else
        return LuaGameType_NewVoid();
}

static void
transform_mouse_coordinates(
    int window_mouse_x,
    int window_mouse_y,
    int* game_mouse_x,
    int* game_mouse_y,
    struct Platform2_OSX_SDL2* platform)
{
    // Calculate the same scaling transformation as the rendering
    // Use the fixed game screen dimensions, not the window dimensions
    float src_aspect = (float)platform->game_screen_width / (float)platform->game_screen_height;
    float dst_aspect = (float)platform->drawable_width / (float)platform->drawable_height;

    int dst_x, dst_y, dst_w, dst_h;

    if( src_aspect > dst_aspect )
    {
        // Source is wider - fit to width
        dst_w = platform->drawable_width;
        dst_h = (int)(platform->drawable_width / src_aspect);
        dst_x = 0;
        dst_y = (platform->drawable_height - dst_h) / 2;
    }
    else
    {
        // Source is taller - fit to height
        dst_h = platform->drawable_height;
        dst_w = (int)(platform->drawable_height * src_aspect);
        dst_y = 0;
        dst_x = (platform->drawable_width - dst_w) / 2;
    }

    // Transform window coordinates to game coordinates
    // Account for the offset and scaling of the game rendering area
    if( window_mouse_x < dst_x || window_mouse_x >= dst_x + dst_w || window_mouse_y < dst_y ||
        window_mouse_y >= dst_y + dst_h )
    {
        // Mouse is outside the game rendering area
        *game_mouse_x = -1;
        *game_mouse_y = -1;
    }
    else
    {
        // Transform from window coordinates to game coordinates
        float relative_x = (float)(window_mouse_x - dst_x) / dst_w;
        float relative_y = (float)(window_mouse_y - dst_y) / dst_h;

        *game_mouse_x = (int)(relative_x * platform->game_screen_width);
        *game_mouse_y = (int)(relative_y * platform->game_screen_height);
    }
}

struct Platform2_OSX_SDL2*
Platform2_OSX_SDL2_New(void)
{
    struct Platform2_OSX_SDL2* platform =
        (struct Platform2_OSX_SDL2*)malloc(sizeof(struct Platform2_OSX_SDL2));
    memset(platform, 0, sizeof(struct Platform2_OSX_SDL2));

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
Platform2_OSX_SDL2_Shutdown(struct Platform2_OSX_SDL2* platform)
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
Platform2_OSX_SDL2_Free(struct Platform2_OSX_SDL2* platform)
{
    if( !platform )
        return;
    Platform2_OSX_SDL2_Shutdown(platform);
    if( platform->lua_sidecar )
        LuaCSidecar_Free(platform->lua_sidecar);
    if( platform->cache_dat )
        cache_dat_free(platform->cache_dat);
    free(platform);
}

bool
Platform2_OSX_SDL2_InitForSoft3D(
    struct Platform2_OSX_SDL2* platform,
    int screen_width,
    int screen_height)
{
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window = SDL_CreateWindow(
        "Game",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        screen_width,
        screen_height,
        SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    platform->window_width = screen_width;
    platform->window_height = screen_height;
    // Store game screen dimensions in drawable_width/drawable_height
    // (these represent the game's logical screen size, not the actual drawable size)
    platform->drawable_width = screen_width;
    platform->drawable_height = screen_height;

    // Store fixed game screen dimensions for mouse coordinate transformation
    platform->game_screen_width = screen_width;
    platform->game_screen_height = screen_height;

    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}

bool
Platform2_OSX_SDL2_InitForOpenGL3(
    struct Platform2_OSX_SDL2* platform,
    int screen_width,
    int screen_height)
{
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    platform->window = SDL_CreateWindow(
        "Game",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        screen_width,
        screen_height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    platform->window_width = screen_width;
    platform->window_height = screen_height;
    platform->drawable_width = screen_width;
    platform->drawable_height = screen_height;
    SDL_GL_GetDrawableSize(platform->window, &platform->drawable_width, &platform->drawable_height);

    platform->game_screen_width = screen_width;
    platform->game_screen_height = screen_height;
    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}

bool
Platform2_OSX_SDL2_InitForMetal(
    struct Platform2_OSX_SDL2* platform,
    int screen_width,
    int screen_height)
{
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window = SDL_CreateWindow(
        "Game",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        screen_width,
        screen_height,
        SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    platform->window_width = screen_width;
    platform->window_height = screen_height;
    platform->drawable_width = screen_width;
    platform->drawable_height = screen_height;

    platform->game_screen_width = screen_width;
    platform->game_screen_height = screen_height;
    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}

void
Platform2_OSX_SDL2_PollEvents(
    struct Platform2_OSX_SDL2* platform,
    struct GInput* input,
    int chat_focused)
{
    double time_s = (double)(SDL_GetTicks64()) / 1000.0;
    ToriRSLibPlatform_SDL2_GameInput_PollEvents(input, time_s);
}

static struct LuaGameType*
on_lua_async_call(
    struct Platform2_OSX_SDL2* platform,
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
                LuaCSidecar_TrackConfigFile(
                    platform->lua_sidecar, (struct LuaConfigFile*)file);
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
Platform2_OSX_SDL2_RunLuaScripts(
    struct Platform2_OSX_SDL2* platform,
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
    }
}