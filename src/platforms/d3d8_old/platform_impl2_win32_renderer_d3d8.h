#ifndef PLATFORM_IMPL2_WIN32_RENDERER_D3D8_H
#define PLATFORM_IMPL2_WIN32_RENDERER_D3D8_H

#if defined(_WIN32) && defined(TORIRS_HAS_D3D8)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "platform_impl2_win32.h"
#include <stdbool.h>
#include <stdint.h>

struct GGame;
struct ToriRSRenderCommandBuffer;

struct Platform2_Win32_Renderer_D3D8
{
    /** Opaque GPU/renderer state (see .cpp). */
    void* _internal;

    struct Platform2_Win32* platform;
    int width;
    int height;
    int max_width;
    int max_height;

    int dash_offset_x;
    int dash_offset_y;

    int initial_width;
    int initial_height;

    int initial_view_port_width;
    int initial_view_port_height;

    bool pixel_size_dynamic;

    void (*on_viewport_changed)(
        struct GGame* game,
        int new_width,
        int new_height,
        void* userdata);
    void* on_viewport_changed_userdata;

    int present_dst_x;
    int present_dst_y;
    int present_dst_w;
    int present_dst_h;

    bool resize_dirty;

    void* nk_rawfb;
    uint8_t* nk_font_tex_mem;
    int* nk_pixel_buffer;

    LARGE_INTEGER nk_qpc_freq;
    LARGE_INTEGER nk_prev_qpc;
    int nk_prev_qpc_valid;
    double nk_dt_smoothed;
};

struct Platform2_Win32_Renderer_D3D8*
PlatformImpl2_Win32_Renderer_D3D8_New(
    int width,
    int height,
    int max_width,
    int max_height);

void
PlatformImpl2_Win32_Renderer_D3D8_Free(struct Platform2_Win32_Renderer_D3D8* renderer);

bool
PlatformImpl2_Win32_Renderer_D3D8_Init(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    struct Platform2_Win32* platform);

void
PlatformImpl2_Win32_Renderer_D3D8_Render(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

void
PlatformImpl2_Win32_Renderer_D3D8_PresentOrInvalidate(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    HDC hdc,
    int client_w,
    int client_h);

void
PlatformImpl2_Win32_Renderer_D3D8_MarkResizeDirty(struct Platform2_Win32_Renderer_D3D8* renderer);

void
PlatformImpl2_Win32_Renderer_D3D8_SetDashOffset(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    int offset_x,
    int offset_y);

void
PlatformImpl2_Win32_Renderer_D3D8_SetDynamicPixelSize(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    bool dynamic);

void
PlatformImpl2_Win32_Renderer_D3D8_SetViewportChangedCallback(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    void (*callback)(
        struct GGame* game,
        int new_width,
        int new_height,
        void* userdata),
    void* userdata);

#endif /* _WIN32 && TORIRS_HAS_D3D8 */

#endif /* PLATFORM_IMPL2_WIN32_RENDERER_D3D8_H */
