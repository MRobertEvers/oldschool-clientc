#ifndef PLATFORM_IMPL2_WIN32_H
#define PLATFORM_IMPL2_WIN32_H

#if defined(_WIN32)

#include "osrs/buildcachedat.h"
#include "osrs/ginput.h"
#include "tori_rs_render.h"
#include <stdbool.h>
#include <stdint.h>

struct Cache;
struct CacheDat;
struct GGame;
struct GInput;
struct LuaCSidecar;
struct ToriRSRenderCommandBuffer;

/** Native Win32 window + input (no SDL2). XP SP3–compatible APIs only in implementation. */
struct Platform2_Win32
{
    void* hwnd; /* HWND */
    struct GInput* input;
    struct GGame* current_game;
    struct ToriRSRenderCommandBuffer* render_command_buffer;
    int secret;

    int window_width;
    int window_height;
    int drawable_width;
    int drawable_height;

    int game_screen_width;
    int game_screen_height;

    float display_scale;

    uint32_t last_frame_time_ticks;

    struct LuaCSidecar* lua_sidecar;
    struct Cache* cache;
    struct CacheDat* cache_dat;

    int tracked_mouse_x;
    int tracked_mouse_y;

    /** Set after renderer init so WM_PAINT can present the last CPU buffer. */
    void* gdi_renderer_for_paint;

    /** Nuklear context for input (struct nk_context*), set after rawfb renderer init. */
    void* nk_ctx_for_input;

    /** Set for the duration of PollEvents so WndProc can set input->quit on WM_CLOSE. */
    struct GInput* poll_input;
};

struct Platform2_Win32*
Platform2_Win32_New(void);
void
Platform2_Win32_Free(struct Platform2_Win32* platform);

bool
Platform2_Win32_InitForSoft3D(
    struct Platform2_Win32* platform,
    int screen_width,
    int screen_height);
void
Platform2_Win32_Shutdown(struct Platform2_Win32* platform);

void
Platform2_Win32_PollEvents(
    struct Platform2_Win32* platform,
    struct GInput* input,
    int chat_focused);

void
Platform2_Win32_RunLuaScripts(struct Platform2_Win32* platform, struct GGame* game);

#endif /* _WIN32 */

#endif /* PLATFORM_IMPL2_WIN32_H */
