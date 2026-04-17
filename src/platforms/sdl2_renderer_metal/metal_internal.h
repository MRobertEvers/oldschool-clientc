#ifndef METAL_INTERNAL_H
#define METAL_INTERNAL_H

#include "platform_impl2_sdl2_renderer_metal.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <vector>

extern "C" {
struct GGame;
struct ToriRSRenderCommandBuffer;
struct ToriRSRenderCommand;
struct DashModel;
struct DashTexture;
struct DashSprite;
struct DashPixFont;
}

inline constexpr size_t kMetalInstanceUniformStride = 256;
inline constexpr size_t kMetalRunUniformStride = 256;

struct MetalVertex
{
    float position[4];
    float color[4];
    float texcoord[2];
    float texBlend;
    float _pad_vertex;
};

struct MetalUniforms
{
    float modelViewMatrix[16];
    float projectionMatrix[16];
    float uClock;
    float _pad_uniform[3];
};

struct MetalInstanceUniform
{
    float cos_yaw, sin_yaw, world_x, world_y;
    float world_z, _pad_i[3];
};

struct MetalRunUniform
{
    float texBlendOverride;
    float textureOpaque;
    float textureAnimSpeed;
    float pad;
};

struct MTLViewportRect
{
    int x, y, width, height;
};

struct LogicalViewportRect
{
    int x, y, width, height;
};

void metal_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw);

void metal_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height);

void metal_remap_projection_opengl_to_metal_z(float* proj_colmajor);

float metal_texture_animation_signed(int animation_direction, int animation_speed);

LogicalViewportRect compute_logical_viewport_rect(
    int window_width,
    int window_height,
    const struct GGame* game);

MTLViewportRect compute_gl_world_viewport_rect(
    int fb_width,
    int fb_height,
    int win_width,
    int win_height,
    const LogicalViewportRect& lr);

MTLScissorRect metal_clamped_scissor_from_logical_dst_bb(
    int fbw,
    int fbh,
    int win_w,
    int win_h,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h);

void sync_drawable_size(struct Platform2_SDL2_Renderer_Metal* renderer);

void release_metal_model_gpu(MetalModelGpu* gpu);

MetalModelGpu* lookup_or_build_model_gpu(
    struct Platform2_SDL2_Renderer_Metal* renderer,
    id<MTLDevice> device,
    const struct DashModel* model,
    uint64_t model_key);

MetalModelGpu* build_model_gpu(
    struct Platform2_SDL2_Renderer_Metal* renderer,
    id<MTLDevice> device,
    const struct DashModel* model,
    uint64_t model_key);

void metal_ensure_ring_bytes(
    struct Platform2_SDL2_Renderer_Metal* r,
    id<MTLDevice> device,
    void** ring_buf,
    size_t* ring_size,
    size_t need_total_bytes);

int model_id_from_model_cache_key(uint64_t k);

void register_model_index_slot(struct Platform2_SDL2_Renderer_Metal* renderer, uint64_t model_key);

void render_nuklear_overlay(
    struct Platform2_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    id<MTLRenderCommandEncoder> encoder);

bool metal_nuklear_bootstrap(struct Platform2_SDL2_Renderer_Metal* renderer, id<MTLDevice> device);

void metal_nuklear_shutdown_if_needed(void);

bool metal_init_pipelines_and_resources(
    struct Platform2_SDL2_Renderer_Metal* renderer,
    CAMetalLayer* layer,
    id<MTLDevice> device);

struct MetalRenderCommandContext
{
    struct Platform2_SDL2_Renderer_Metal* renderer = nullptr;
    struct GGame* game = nullptr;
    id<MTLDevice> device = nil;
    id<MTLRenderCommandEncoder> encoder = nil;
    id<MTLRenderPipelineState> pipeState = nil;
    id<MTLDepthStencilState> dsState = nil;
    id<MTLBuffer> unifBuf = nil;
    MTLViewport metalVp{};
    MTLViewport spriteVp{};
    int win_width = 0;
    int win_height = 0;
    id<MTLTexture> dummyTex = nil;
    id<MTLSamplerState> samp = nil;
    id<MTLRenderPipelineState> uiPipeState = nil;
    id<MTLRenderPipelineState> fontPipeState = nil;
    id<MTLBuffer> spriteQuadBuf = nil;
    id<MTLBuffer> fontVbo = nil;
    id<MTLSamplerState> uiSamplerNearest = nil;
    id<MTLSamplerState> fontSampler = nil;
    float fbw_font = 0.0f;
    float fbh_font = 0.0f;
    std::vector<float> font_verts;
    id<MTLTexture> current_font_atlas_tex = nil;
    int current_pipe = 0;
    int current_font_id = -1;
    NSUInteger sprite_slot = 0;
};

void metal_run_render_command_loop(
    MetalRenderCommandContext& ctx,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

void metal_flush_font_batch(MetalRenderCommandContext& ctx);
void metal_ensure_pipe(MetalRenderCommandContext& ctx, int desired);
void metal_flush_batch(MetalRenderCommandContext& ctx);

#endif
