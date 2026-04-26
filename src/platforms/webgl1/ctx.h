#ifndef PLATFORMS_WEBGL1_CTX_H
#define PLATFORMS_WEBGL1_CTX_H

#ifdef __EMSCRIPTEN__

#    include "tori_rs_render.h"

#    include <SDL.h>
#    include <cmath>
#    include <cstdint>

struct Platform2_SDL2_Renderer_WebGL1;
struct GGame;

struct LogicalViewportRect
{
    int x, y, width, height;
};

struct ToriGlViewportPixels
{
    int x, y, width, height;
};

struct WebGL1RenderCtx
{
    struct Platform2_SDL2_Renderer_WebGL1* renderer = nullptr;
    struct GGame* game = nullptr;
    int win_width = 0;
    int win_height = 0;
    int full_vp_x = 0;
    int full_vp_y = 0;
    int full_vp_w = 0;
    int full_vp_h = 0;
    LogicalViewportRect pass_3d_dst_logical{};
    int clear_rect_slot = 0;
};

inline constexpr int kWebGL1MaxClearRectsPerFrame = 8;
inline constexpr int kWebGL1SpriteSlotBytes = 6 * 4 * (int)sizeof(float);

float
webgl1_dash_yaw_to_radians(int dash_yaw);

void
webgl1_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw);

void
webgl1_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height);

LogicalViewportRect
compute_logical_viewport_rect(int window_width, int window_height, const struct GGame* game);

ToriGlViewportPixels
compute_gl_world_viewport_rect(
    int fb_width,
    int fb_height,
    int win_width,
    int win_height,
    const LogicalViewportRect& lr);

void
webgl1_clamped_gl_scissor(
    int fbw,
    int fbh,
    int win_w,
    int win_h,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h);

float
webgl1_texture_animation_signed(int animation_direction, int animation_speed);

void
webgl1_cache2_batch_push_model_mesh(
    WebGL1RenderCtx* ctx,
    struct DashModel* model,
    int model_id,
    uint32_t batch_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index);

void
webgl1_frame_event_clear_rect(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
webgl1_frame_event_begin_3d(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd,
    const LogicalViewportRect* default_logical_vp);
void
webgl1_frame_event_end_3d(WebGL1RenderCtx* ctx);
void
webgl1_frame_event_texture_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
webgl1_frame_event_model_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
webgl1_frame_event_model_unload(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
webgl1_frame_event_batch_model_load_start(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
webgl1_frame_event_model_batched_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
webgl1_frame_event_batch_model_load_end(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
webgl1_frame_event_batch_model_clear(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
webgl1_frame_event_model_draw(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
webgl1_frame_event_batch_texture_load_start(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
webgl1_frame_event_batch_texture_load_end(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
webgl1_frame_event_model_animation_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

bool
webgl1_gl_create_programs(struct Platform2_SDL2_Renderer_WebGL1* r);
void
webgl1_gl_destroy_programs(struct Platform2_SDL2_Renderer_WebGL1* r);

void
webgl1_atlas_resources_init(struct Platform2_SDL2_Renderer_WebGL1* r);
void
webgl1_atlas_resources_shutdown(struct Platform2_SDL2_Renderer_WebGL1* r);

void
webgl1_sync_drawable_size(struct Platform2_SDL2_Renderer_WebGL1* renderer);

#endif
#endif
