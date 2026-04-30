#ifndef TORIRS_PLATFORM_KIT_TRSPK_D3D8_FIXED_H
#define TORIRS_PLATFORM_KIT_TRSPK_D3D8_FIXED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef TRSPK_D3D8_HWND_TYPEDEF
#define TRSPK_D3D8_HWND_TYPEDEF
typedef void* TRSPK_D3D8_WindowHandle;
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct GGame;
struct ToriRSRenderCommand;
struct Platform2_Win32_Renderer_D3D8;

/** Runtime switch between atlas-baked UV mode and legacy per-id texture binding. */
typedef enum TRSPK_D3D8FixedTextureMode
{
    TRSPK_D3D8_FIXED_TEX_ATLAS  = 0, /* Default: TRSPK atlas + baked UVs. */
    TRSPK_D3D8_FIXED_TEX_PER_ID = 1, /* Legacy: per-id IDirect3DTexture8 binding. */
} TRSPK_D3D8FixedTextureMode;

/**
 * Win32 D3D8 fixed-function backend with TRSPK atlas + LRU + Batch16 infrastructure.
 * `opaque_internal` holds C++ D3D8FixedInternal.
 */
typedef struct TRSPK_D3D8FixedRenderer
{
    uint32_t width;
    uint32_t height;
    uint32_t window_width;
    uint32_t window_height;
    bool ready;

    int32_t dash_offset_x;
    int32_t dash_offset_y;

    /** Mirrors legacy debug_model_draws / debug_triangles for overlay stats. */
    uint32_t diag_frame_submitted_draws;
    uint32_t diag_frame_pass_subdraws;

    float frame_u_clock;
    int frame_vp_w;
    int frame_vp_h;

    TRSPK_D3D8_WindowHandle platform_hwnd;
    uintptr_t com_device;
    uintptr_t com_d3d;
    bool device_lost;

    void* opaque_internal;
} TRSPK_D3D8FixedRenderer;

typedef struct TRSPK_D3D8Fixed_EventContext
{
    TRSPK_D3D8FixedRenderer* renderer;
    struct GGame* game;
    struct Platform2_Win32_Renderer_D3D8* platform_renderer;
    int scene_width;
    int scene_height;
    int window_width;
    int window_height;
    float u_clock;
    int frame_vp_w;
    int frame_vp_h;
} TRSPK_D3D8Fixed_EventContext;

TRSPK_D3D8FixedRenderer*
TRSPK_D3D8Fixed_Init(TRSPK_D3D8_WindowHandle hwnd, uint32_t width, uint32_t height);

bool
TRSPK_D3D8Fixed_TryReset(TRSPK_D3D8FixedRenderer* renderer);

void
TRSPK_D3D8Fixed_NotifyDeviceLost(TRSPK_D3D8FixedRenderer* renderer);

void
TRSPK_D3D8Fixed_Shutdown(TRSPK_D3D8FixedRenderer* renderer);

void
TRSPK_D3D8Fixed_Resize(TRSPK_D3D8FixedRenderer* renderer, uint32_t width, uint32_t height);

void
TRSPK_D3D8Fixed_SetWindowSize(TRSPK_D3D8FixedRenderer* renderer, uint32_t width, uint32_t height);

void
TRSPK_D3D8Fixed_FrameBegin(
    TRSPK_D3D8FixedRenderer* renderer,
    int view_w,
    int view_h,
    struct GGame* game);

/** After all TORIRS commands, before Nuklear: flush accumulated font geometry (legacy order). */
void
TRSPK_D3D8Fixed_FlushFont(TRSPK_D3D8FixedRenderer* renderer);

void
TRSPK_D3D8Fixed_FrameEnd(TRSPK_D3D8FixedRenderer* renderer);

void
TRSPK_D3D8Fixed_Present(
    TRSPK_D3D8FixedRenderer* renderer,
    int window_width,
    int window_height,
    struct Platform2_Win32_Renderer_D3D8* platform);

void*
TRSPK_D3D8Fixed_GetDevice(TRSPK_D3D8FixedRenderer* renderer);

void
trspk_d3d8_fixed_event_tex_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_res_model_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_res_model_unload(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_res_anim_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_batch3d_begin(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_batch3d_model_add(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_batch3d_end(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_batch3d_clear(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_batch3d_anim_add(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_sprite_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_sprite_unload(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_font_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_state_clear_rect(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_draw_model(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_draw_sprite(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_draw_font(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_state_begin_3d(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_state_end_3d(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_state_begin_2d(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_state_end_2d(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_fixed_event_none_or_default(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);

/**
 * Switch between TRSPK atlas mode (default) and legacy per-id texture binding.
 * Must be called before any resource loads; switching mid-frame is undefined.
 */
void
TRSPK_D3D8Fixed_SetTextureMode(
    TRSPK_D3D8FixedRenderer* renderer,
    TRSPK_D3D8FixedTextureMode mode);

#ifdef __cplusplus
}
#endif

#endif
