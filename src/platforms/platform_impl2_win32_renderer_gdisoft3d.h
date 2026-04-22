#ifndef PLATFORM_IMPL2_WIN32_RENDERER_GDISOFT3D_H
#define PLATFORM_IMPL2_WIN32_RENDERER_GDISOFT3D_H

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "platform_impl2_win32.h"

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

struct GGame;
struct ToriRSRenderCommandBuffer;

struct Platform2_Win32_Renderer_GDISoft3D
{
    struct Platform2_Win32* platform;
    int* pixel_buffer;
    HBITMAP dib_bitmap;
    HDC dib_dc;
    BITMAPINFOHEADER bmi_header;
    int width;
    int height;
    int max_width;
    int max_height;

    int dash_offset_x;
    int dash_offset_y;

    float time_delta_accumulator;

    int first_frame;
    int clicked_tile_x;
    int clicked_tile_z;
    int clicked_tile_level;

    int initial_width;
    int initial_height;

    int initial_view_port_width;
    int initial_view_port_height;

    bool pixel_size_dynamic;

    /** If true (default), present with StretchDIBits (aspect-fit scaling). If false, 1:1
     *  SetDIBitsToDevice (no scaling). */
    bool present_use_stretch;

    void (*on_viewport_changed)(
        struct GGame* game,
        int new_width,
        int new_height,
        void* userdata);
    void* on_viewport_changed_userdata;

    /** Last letterbox destination in client pixels (for WM_PAINT). */
    int present_dst_x;
    int present_dst_y;
    int present_dst_w;
    int present_dst_h;

    /** Set when the letterbox bars need re-clearing (e.g. after WM_SIZE).
     *  StretchDIBits only covers dst_rect and WM_ERASEBKGND is suppressed, so the bars
     *  are otherwise preserved across frames and don't need per-frame clearing. */
    bool letterbox_dirty;

    void* nk_rawfb; /* struct rawfb_context* */
    uint8_t* nk_font_tex_mem;
    LARGE_INTEGER nk_qpc_freq;
    LARGE_INTEGER nk_prev_qpc;
    int nk_prev_qpc_valid;
    double nk_dt_smoothed;
};

struct Platform2_Win32_Renderer_GDISoft3D*
PlatformImpl2_Win32_Renderer_GDISoft3D_New(
    int width,
    int height,
    int max_width,
    int max_height);

void
PlatformImpl2_Win32_Renderer_GDISoft3D_Free(struct Platform2_Win32_Renderer_GDISoft3D* renderer);

bool
PlatformImpl2_Win32_Renderer_GDISoft3D_Init(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer,
    struct Platform2_Win32* platform);

void
PlatformImpl2_Win32_Renderer_GDISoft3D_Render(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

/** Blit last raster buffer to an HDC (e.g. WM_PAINT). Uses client_w/h for letterboxing. */
void
PlatformImpl2_Win32_Renderer_GDISoft3D_PresentToHdc(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer,
    HDC hdc,
    int client_w,
    int client_h);

/** Signal that the letterbox/pillarbox bars need to be re-cleared on the next present.
 *  Should be called from WM_SIZE and any other event that changes client size or dst_rect. */
void
PlatformImpl2_Win32_Renderer_GDISoft3D_MarkLetterboxDirty(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer);

void
PlatformImpl2_Win32_Renderer_GDISoft3D_SetDashOffset(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer,
    int offset_x,
    int offset_y);

void
PlatformImpl2_Win32_Renderer_GDISoft3D_SetDynamicPixelSize(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer,
    bool dynamic);

void
PlatformImpl2_Win32_Renderer_GDISoft3D_SetPresentUseStretch(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer,
    bool use_stretch);

void
PlatformImpl2_Win32_Renderer_GDISoft3D_SetViewportChangedCallback(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer,
    void (*callback)(
        struct GGame* game,
        int new_width,
        int new_height,
        void* userdata),
    void* userdata);

#endif /* _WIN32 */

#endif /* PLATFORM_IMPL2_WIN32_RENDERER_GDISOFT3D_H */
