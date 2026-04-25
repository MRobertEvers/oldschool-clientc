#ifndef PLATFORM_IMPL2_SDL2_H
#define PLATFORM_IMPL2_SDL2_H

extern "C" {
#ifdef __EMSCRIPTEN__
#include "osrs/game.h"
#include "tori_rs.h"
#endif
#include "osrs/ginput.h"
#include "tori_rs_render.h"
#ifndef __EMSCRIPTEN__
#include "osrs/buildcachedat.h"
#endif
}
#include "platform_impl2_sdl2_renderer_flags.h"
#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct Cache;
struct CacheDat;
struct GGame;
struct GInput;
struct LuaCSidecar;
struct ToriRSRenderCommandBuffer;

struct Platform2_SDL2
{
    SDL_Window* window;
    int* pixel_buffer;

    int window_width;
    int window_height;
    int drawable_width;
    int drawable_height;

    int game_screen_width;
    int game_screen_height;

    float display_scale;

    uint64_t last_frame_time_ticks;

    struct GGame* current_game;

    struct GInput* input;
    struct ToriRSRenderCommandBuffer* render_command_buffer;
    int secret;

    struct LuaCSidecar* lua_sidecar;
    struct Cache* cache;
    struct CacheDat* cache_dat;
};

struct Platform2_SDL2*
Platform2_SDL2_New(void);
void
Platform2_SDL2_Free(struct Platform2_SDL2* platform);

bool
Platform2_SDL2_InitForSoft3D(struct Platform2_SDL2* platform, int screen_width, int screen_height);

#ifdef __EMSCRIPTEN__
void
Platform2_SDL2_SyncCanvasCssSize(
    struct Platform2_SDL2* platform,
    struct GGame* game_nullable);
#else
#if !defined(_WIN32)
bool
Platform2_SDL2_InitForOpenGL3(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height);
#endif
bool
Platform2_SDL2_InitForMetal(struct Platform2_SDL2* platform, int screen_width, int screen_height);
#if defined(_WIN32)
bool
Platform2_SDL2_InitForD3D11(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height);
#endif
#endif

void
Platform2_SDL2_Shutdown(struct Platform2_SDL2* platform);

void
Platform2_SDL2_PollEvents(
    struct Platform2_SDL2* platform,
    struct GInput* input,
    int chat_focused);

void
Platform2_SDL2_RunLuaScripts(struct Platform2_SDL2* platform, struct GGame* game);

#endif
