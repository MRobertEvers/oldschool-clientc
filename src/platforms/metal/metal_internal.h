#ifndef PLATFORMS_METAL_METAL_INTERNAL_H
#define PLATFORMS_METAL_METAL_INTERNAL_H

#include "platforms/metal/metal.h"
#include "tori_rs_render.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <SDL.h>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "graphics/uv_pnm.h"

extern "C" {
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "graphics/shared_tables.h"
#include "osrs/game.h"
#include "platforms/common/platform_memory.h"
#include "tori_rs.h"
}

struct MetalUniforms
{
    float modelViewMatrix[16];
    float projectionMatrix[16];
    float uClock;
    float _pad_uniform[3];
};

/** Padded stride for dynamic uniform offsets (Metal buffer offset alignment). */
inline size_t
metal_uniforms_stride_padded()
{
    constexpr size_t a = kMetalUniformBufferDynamicAlign;
    return (sizeof(MetalUniforms) + a - 1u) / a * a;
}

struct LogicalViewportRect
{
    int x, y, width, height;
};

/** GL-style viewport in drawable pixels (y from bottom). Not Apple's MTLViewport. */
struct ToriGlViewportPixels
{
    int x, y, width, height;
};

static const NSUInteger kSpriteSlotBytes = 6 * 4 * sizeof(float);

/** Max `TORIRS_GFX_CLEAR_RECT` per frame; vertex slots live in internal clear-quad buffer. */
inline constexpr int kMetalMaxClearRectsPerFrame = 8;

struct MetalRenderCtx
{
    struct Platform2_SDL2_Renderer_Metal* renderer = nullptr;
    struct GGame* game = nullptr;
    id<MTLDevice> device = nil;
    id<MTLRenderCommandEncoder> encoder = nil;
    id<MTLRenderPipelineState> clearRectDepthPipeState = nil;
    id<MTLDepthStencilState> clearRectDepthDsState = nil;
    id<MTLDepthStencilState> dsState = nil;
    id<MTLBuffer> clearQuadBuf = nil;
    MTLViewport metalVp{};
    /** Full drawable in pixels (for CLEAR_RECT and restoring scissor). */
    MTLViewport fullDrawableVp{};
    int win_width = 0;
    int win_height = 0;
    /** Next slot in `clearQuadBuf` for `metal_frame_event_clear_rect` (bytes = slot *
     * kSpriteSlotBytes). */
    int clear_rect_slot = 0;
    /** Last TORIRS_GFX_BEGIN_3D destination rect (window pixels); used for END_3D uniforms. */
    LogicalViewportRect pass_3d_dst_logical{};
};

void
metal_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw);

/** Dash `yaw` / `camera_yaw` in [0,2048) → radians; same factor as `metal_render.mm` uniforms. */
float
metal_dash_yaw_to_radians(int dash_yaw);

/** cos/sin for model Y rotation in world xz; uses `metal_dash_yaw_to_radians` then `cosf`/`sinf`.
 */
void
metal_prebake_model_yaw_cos_sin(
    int dash_yaw,
    float* cos_out,
    float* sin_out);

void
metal_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height);

void
metal_remap_projection_opengl_to_metal_z(float* proj_colmajor);

float
metal_texture_animation_signed(
    int animation_direction,
    int animation_speed);

LogicalViewportRect
compute_logical_viewport_rect(
    int window_width,
    int window_height,
    const struct GGame* game);

ToriGlViewportPixels
compute_gl_world_viewport_rect(
    int fb_width,
    int fb_height,
    int win_width,
    int win_height,
    const LogicalViewportRect& lr);

MTLScissorRect
metal_clamped_scissor_from_logical_dst_bb(
    int fbw,
    int fbh,
    int win_w,
    int win_h,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h);

void
sync_drawable_size(struct Platform2_SDL2_Renderer_Metal* renderer);

/** Main-pass depth stencil + drawable-sized depth texture (single active Metal renderer). */
void
metal_internal_set_depth_stencil(void* retained_depth_stencil);
void*
metal_internal_depth_stencil(void);
void
metal_internal_set_depth_texture(void* retained_depth_texture, int w, int h);
void*
metal_internal_depth_texture(void);
bool
metal_internal_depth_texture_matches(int w, int h);
void
metal_internal_shutdown_depth_pass_resources(void);

void
metal_internal_set_clear_rect_depth_pipeline(void* retained_pipeline);
void*
metal_internal_clear_rect_depth_pipeline(void);
void
metal_internal_set_clear_rect_depth_write_ds(void* retained_ds);
void*
metal_internal_clear_rect_depth_write_ds(void);
void
metal_internal_set_clear_quad_buf(void* retained_buffer);
void*
metal_internal_clear_quad_buf(void);
void
metal_internal_shutdown_clear_rect_aux_resources(void);

void
metal_frame_event_clear_rect(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

void
metal_frame_event_begin_3d(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd,
    const LogicalViewportRect* default_logical_vp);
void
metal_frame_event_end_3d(MetalRenderCtx* ctx, id<MTLBuffer> uniforms_buffer);

void
Pass3DBuilder2SubmitMetal(
    Pass3DBuilder2& builder,
    const GPU3DCache2& cache,
    struct Platform2_SDL2_Renderer_Metal* metal_renderer,
    id<MTLRenderCommandEncoder> render_command_encoder,
    id<MTLBuffer> dynamic_instance_buffer,
    id<MTLBuffer> dynamic_index_buffer,
    NSUInteger instance_base_bytes,
    NSUInteger index_base_bytes,
    id<MTLTexture> fragment_atlas_texture,
    id<MTLBuffer> atlas_tiles_buffer,
    id<MTLBuffer> uniforms_buffer,
    NSUInteger uniforms_buffer_offset_bytes,
    id<MTLSamplerState> fragment_sampler,
    id<MTLDepthStencilState> depth_stencil_state);

void
metal_cache2_atlas_resources_init(
    struct Platform2_SDL2_Renderer_Metal* r,
    id<MTLDevice> device);
void
metal_cache2_atlas_resources_shutdown(struct Platform2_SDL2_Renderer_Metal* r);

void
metal_frame_event_texture_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_model_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_model_unload(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_model_load_start(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_model_batched_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);
/** Append current DashModel mesh into `model_cache2` for an open batch (`batch_id` from scene). */
void
metal_cache2_batch_push_model_mesh(
    MetalRenderCtx* ctx,
    struct DashModel* model,
    int model_id,
    uint32_t batch_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index);
void
metal_frame_event_batch_model_load_end(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_model_clear(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_model_draw(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_texture_load_start(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_texture_load_end(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_model_animation_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

void
metal_frame_event_batch_vertex_array_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_face_array_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

#endif
