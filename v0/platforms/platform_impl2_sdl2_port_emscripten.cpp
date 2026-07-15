#include "platform_impl2_sdl2.h"
#include "platform_impl2_sdl2_internal.h"

extern "C" {
#include "common/tori_rs_sdl2_gameinput.h"
#include "osrs/game.h"
#include "tori_rs.h"
}

#include "platforms/common/torirs_nk_ui_bridge.h"

#include <emscripten/html5.h>
#include <SDL.h>
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
transform_mouse_coordinates(
    int window_mouse_x,
    int window_mouse_y,
    int* game_mouse_x,
    int* game_mouse_y,
    struct Platform2_SDL2* platform)
{
    float src_aspect = (float)platform->game_screen_width / (float)platform->game_screen_height;
    float dst_aspect = (float)platform->drawable_width / (float)platform->drawable_height;

    int dst_x, dst_y, dst_w, dst_h;

    if( src_aspect > dst_aspect )
    {
        dst_w = platform->drawable_width;
        dst_h = (int)(platform->drawable_width / src_aspect);
        dst_x = 0;
        dst_y = (platform->drawable_height - dst_h) / 2;
    }
    else
    {
        dst_h = platform->drawable_height;
        dst_w = (int)(platform->drawable_height * src_aspect);
        dst_y = 0;
        dst_x = (platform->drawable_width - dst_w) / 2;
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

        *game_mouse_x = (int)(relative_x * platform->game_screen_width);
        *game_mouse_y = (int)(relative_y * platform->game_screen_height);
    }
}

bool
PlatformImpl2_SDL2_Port_Emscripten_InitNewFields(struct Platform2_SDL2* platform)
{
    platform->input = (struct GInput*)malloc(sizeof(struct GInput));
    if( !platform->input )
        return false;
    memset(platform->input, 0, sizeof(struct GInput));
    return true;
}

void
PlatformImpl2_SDL2_Port_Emscripten_FreeExtra(struct Platform2_SDL2* platform)
{
    if( platform->input )
    {
        free(platform->input);
        platform->input = NULL;
    }
}

bool
PlatformImpl2_SDL2_Port_Emscripten_InitForSoft3D(
    struct Platform2_SDL2* platform,
    int canvas_width,
    int canvas_height)
{
    if( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window = SDL_CreateWindow(
        "3D Raster - Soft3D",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        canvas_width,
        canvas_height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window_width = canvas_width;
    platform->window_height = canvas_height;
    platform->drawable_width = canvas_width;
    platform->drawable_height = canvas_height;
    platform->game_screen_width = canvas_width;
    platform->game_screen_height = canvas_height;
    platform->display_scale = 1.0f;
    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}

bool
PlatformImpl2_SDL2_Port_Emscripten_InitForWebGL1(
    struct Platform2_SDL2* platform,
    int canvas_width,
    int canvas_height)
{
    if( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    platform->window = SDL_CreateWindow(
        "3D Raster - WebGL1",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        canvas_width,
        canvas_height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window_width = canvas_width;
    platform->window_height = canvas_height;
    platform->drawable_width = canvas_width;
    platform->drawable_height = canvas_height;
    platform->game_screen_width = canvas_width;
    platform->game_screen_height = canvas_height;
    platform->display_scale = 1.0f;
    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}

void
PlatformImpl2_SDL2_Port_Emscripten_SyncCanvasCssSize(
    struct Platform2_SDL2* platform,
    struct GGame* game_nullable)
{
    if( !platform )
        return;

    double css_width = 0.0;
    double css_height = 0.0;
    emscripten_get_element_css_size("#canvas", &css_width, &css_height);
    int new_w = (int)css_width;
    int new_h = (int)css_height;
    if( new_w <= 0 || new_h <= 0 )
        return;

    if( game_nullable && game_nullable->iface_view_port &&
        game_nullable->iface_view_port->clip_right <= 0 && platform->game_screen_width > 0 )
    {
        struct DashViewPort* ivp = game_nullable->iface_view_port;
        ivp->clip_left = 0;
        ivp->clip_top = 0;
        ivp->clip_right = platform->game_screen_width;
        ivp->clip_bottom = platform->game_screen_height;
    }

    if( new_w == platform->window_width && new_h == platform->window_height )
        return;

    platform->window_width = new_w;
    platform->window_height = new_h;
    platform->drawable_width = new_w;
    platform->drawable_height = new_h;

    if( platform->window )
        SDL_SetWindowSize(platform->window, new_w, new_h);

    emscripten_set_canvas_element_size("#canvas", new_w, new_h);
}

void
PlatformImpl2_SDL2_Port_Emscripten_PollEvents(
    struct Platform2_SDL2* platform,
    struct GInput* input,
    int chat_focused)
{
    (void)chat_focused;
    struct GInput* poll_input = input ? input : platform->input;

    PlatformImpl2_SDL2_Port_Emscripten_SyncCanvasCssSize(platform, platform->current_game);

    double time_s = (double)(SDL_GetTicks64()) / 1000.0;
    ToriRSLibPlatform_SDL2_GameInput_PollEvents(poll_input, time_s);
    if( !torirs_nk_ui_want_capture_mouse_prev() )
    {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        transform_mouse_coordinates(mx, my, &poll_input->mouse_state.x, &poll_input->mouse_state.y, platform);
    }
}

static void
send_lua_game_script_to_js(struct LuaGameScript* script)
{
    // clang-format off
    EM_ASM(
        {
            if (typeof window.done === 'undefined') {
                window.done = false;
            }
            var script = $0;
            window.LUA_SCRIPT_QUEUE = window.LUA_SCRIPT_QUEUE || [];
            window.LUA_SCRIPT_QUEUE.push(script);
        },
        script);
    // clang-format on
}

void
PlatformImpl2_SDL2_Port_Emscripten_RunLuaScripts(struct Platform2_SDL2* platform, struct GGame* game)
{
    struct LuaGameScript* script = NULL;

    while( !LibToriRS_LuaScriptQueueIsEmpty(game) )
    {
        script = LuaGameScript_New();
        LibToriRS_LuaScriptQueuePop(game, script);

        send_lua_game_script_to_js(script);
    }
}
