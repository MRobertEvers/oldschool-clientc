#ifndef PLATFORM_IMPL_EMSCRIPTEN_SDL2_H
#define PLATFORM_IMPL_EMSCRIPTEN_SDL2_H

#include "libinput.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Platform
{
    SDL_Window* window;

    int* pixel_buffer;

    int window_width;
    int window_height;
    int drawable_width;
    int drawable_height;

    uint64_t last_frame_time_ticks;
};

struct Platform* PlatformImpl_Emscripten_SDL2_New(void);
void PlatformImpl_Emscripten_SDL2_Free(struct Platform* platform);

bool PlatformImpl_Emscripten_SDL2_InitForSoft3D(
    struct Platform* platform,
    int canvas_width,
    int canvas_height,
    int max_canvas_width,
    int max_canvas_height);
void PlatformImpl_Emscripten_SDL2_Shutdown(struct Platform* platform);

void PlatformImpl_Emscripten_SDL2_PollEvents(struct Platform* platform, struct GameInput* input);

#ifdef __cplusplus
}
#endif

#endif