#ifndef PLATFORM_IMPL2_OSX_SDL2_H
#define PLATFORM_IMPL2_OSX_SDL2_H

extern "C" {
#include "osrs/buildcachedat.h"
#include "osrs/ginput.h"
#include "osrs/lua_sidecar/luac_sidecar.h"
#include "tori_rs_render.h"
}
#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct Cache;
struct Platform2_OSX_SDL2
{
    SDL_Window* window;
    struct LuaCSidecar* lua_sidecar;

    int* pixel_buffer;

    int window_width;
    int window_height;
    int drawable_width;
    int drawable_height;

    // Fixed game screen dimensions (used for mouse coordinate transformation)
    int game_screen_width;
    int game_screen_height;

    uint64_t last_frame_time_ticks;

    struct Cache* cache;
    struct CacheDat* cache_dat;
    struct BuildCacheDat* buildcachedat;
};

struct Platform2_OSX_SDL2*
Platform2_OSX_SDL2_New(void);
void
Platform2_OSX_SDL2_Free(struct Platform2_OSX_SDL2* platform);

bool
Platform2_OSX_SDL2_InitForSoft3D(
    struct Platform2_OSX_SDL2* platform,
    int screen_width,
    int screen_height);
void
Platform2_OSX_SDL2_Shutdown(struct Platform2_OSX_SDL2* platform);

void
Platform2_OSX_SDL2_PollEvents(
    struct Platform2_OSX_SDL2* platform,
    struct GInput* input,
    int chat_focused); /* 1=typing to chat, don't use keys for movement */
void
Platform2_OSX_SDL2_PollIO(
    struct Platform2_OSX_SDL2* platform,
    struct GGame* game);

void
Platform2_OSX_SDL2_RunLuaScripts(
    struct Platform2_OSX_SDL2* platform,
    struct GGame* game);

#endif