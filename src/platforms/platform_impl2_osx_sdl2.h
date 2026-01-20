#ifndef PLATFORM_IMPL2_OSX_SDL2_H
#define PLATFORM_IMPL2_OSX_SDL2_H

extern "C" {
#include "osrs/ginput.h"
#include "osrs/grender.h"
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

    int* pixel_buffer;

    int window_width;
    int window_height;
    int drawable_width;
    int drawable_height;

    uint64_t last_frame_time_ticks;

    struct Cache* cache;
    struct CacheDat* cache_dat;
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
    struct GInput* input);
void
Platform2_OSX_SDL2_PollIO(
    struct Platform2_OSX_SDL2* platform,
    struct GIOQueue* queue);

#endif