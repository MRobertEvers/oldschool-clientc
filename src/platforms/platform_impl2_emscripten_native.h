
#ifndef PLATFORM_IMPL2_EMSCRIPTEN_NATIVE_H
#define PLATFORM_IMPL2_EMSCRIPTEN_NATIVE_H

extern "C" {
#include "osrs/game.h"
#include "osrs/ginput.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}
#include <stdbool.h>
#include <stdint.h>

struct Cache;
struct Platform2_Emscripten_Native
{
    struct GInput* input;
    struct GGame* current_game;
    struct ToriRSRenderCommandBuffer* render_command_buffer;

    int secret;

    int* pixel_buffer;

    int window_width;
    int window_height;
    int drawable_width;
    int drawable_height;

    int game_screen_width;
    int game_screen_height;

    float display_scale;

    uint64_t last_frame_time_ticks;

    int tracked_mouse_x;
    int tracked_mouse_y;

    bool canvas_ready;
};

struct Platform2_Emscripten_Native*
Platform2_Emscripten_Native_New(void);
void
Platform2_Emscripten_Native_Free(struct Platform2_Emscripten_Native* platform);

bool
Platform2_Emscripten_Native_InitForSoft3D(
    struct Platform2_Emscripten_Native* platform,
    int screen_width,
    int screen_height);
void
Platform2_Emscripten_Native_Shutdown(struct Platform2_Emscripten_Native* platform);

void
Platform2_Emscripten_Native_PollEvents(struct Platform2_Emscripten_Native* platform);

void
Platform2_Emscripten_Native_SyncCanvasCssSize(
    struct Platform2_Emscripten_Native* platform,
    struct GGame* game_nullable);

void
Platform2_Emscripten_Native_RunLuaScripts(struct Platform2_Emscripten_Native* platform);

#endif
