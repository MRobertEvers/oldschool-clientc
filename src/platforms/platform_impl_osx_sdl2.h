#ifndef PLATFORM_IMPL_OSX_SDL2_H
#define PLATFORM_IMPL_OSX_SDL2_H

#include "libinput.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct Platform
{
    SDL_Window* window;

    int* pixel_buffer;

    int window_width;
    int window_height;
    int drawable_width;
    int drawable_height;
};

struct Platform* PlatformImpl_OSX_SDL2_New(void);
void PlatformImpl_OSX_SDL2_Free(struct Platform* platform);

bool
PlatformImpl_OSX_SDL2_InitForSoft3D(struct Platform* platform, int screen_width, int screen_height);
void PlatformImpl_OSX_SDL2_Shutdown(struct Platform* platform);

void PlatformImpl_OSX_SDL2_PollEvents(struct Platform* platform, struct GameInput* input);

#endif