

#ifndef PLATFORM_IMPL2_BROWSER_SDL2_H
#define PLATFORM_IMPL2_BROWSER_SDL2_H

extern "C" {
#include "osrs/game.h"
#include "osrs/ginput.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}
#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct Cache;
struct Platform2_Emscripten_SDL2
{
    SDL_Window* window;
    struct GInput* input;
    struct GGame* current_game;
    struct ToriRSRenderCommandBuffer* render_command_buffer;

    int secret;

    int* pixel_buffer;

    int window_width;
    int window_height;
    int drawable_width;
    int drawable_height;

    // Fixed game screen dimensions (used for mouse coordinate transformation)
    int game_screen_width;
    int game_screen_height;

    /* HiDPI / UI scale — browser uses 1.0f; matches macOS platform for ImGui init. */
    float display_scale;

    uint64_t last_frame_time_ticks;
};

struct Platform2_Emscripten_SDL2*
Platform2_Emscripten_SDL2_New(void);
void
Platform2_Emscripten_SDL2_Free(struct Platform2_Emscripten_SDL2* platform);

bool
Platform2_Emscripten_SDL2_InitForSoft3D(
    struct Platform2_Emscripten_SDL2* platform,
    int screen_width,
    int screen_height);
bool
Platform2_Emscripten_SDL2_InitForWebGL1(
    struct Platform2_Emscripten_SDL2* platform,
    int screen_width,
    int screen_height);
void
Platform2_Emscripten_SDL2_Shutdown(struct Platform2_Emscripten_SDL2* platform);

void
Platform2_Emscripten_SDL2_PollEvents(struct Platform2_Emscripten_SDL2* platform);

/** Match SDL window + canvas backing store to #canvas CSS size (Emscripten only). */
void
Platform2_Emscripten_SDL2_SyncCanvasCssSize(
    struct Platform2_Emscripten_SDL2* platform,
    struct GGame* game_nullable);

void
Platform2_Emscripten_SDL2_RunLuaScripts(struct Platform2_Emscripten_SDL2* platform);

#endif