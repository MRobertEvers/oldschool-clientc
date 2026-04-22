#ifndef PLATFORMS_METAL_METAL_INTERNAL_H
#define PLATFORMS_METAL_METAL_INTERNAL_H

#include "platforms/metal/metal.h"

#include "nuklear/backends/torirs_nk_metal_sdl.h"
#include "platforms/common/torirs_nk_ui_bridge.h"
#include "platforms/common/torirs_nuklear_debug_panel.h"
#include "platforms/common/buffered_face_order.h"
#include "tori_rs_render.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <SDL.h>
#include <climits>
#include <cstdint>
#include <cmath>
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

struct TorirsNkMetalUi;
extern struct TorirsNkMetalUi* s_mtl_nk;
extern Uint64 s_mtl_ui_prev_perf;

inline constexpr size_t kMetalRunUniformStride = 256;

/** Vertex local UV policy for world atlas (see Shaders.metal fragmentShader). */
enum : uint16_t
{
    /** Match OpenGL: clamp U, fract V with insets (standard models). */
    kMetalVertexUvMode_Standard = 0,
    /** VA/FA ground: tile both U and V with fract (large UV from uv_pnm). */
    kMetalVertexUvMode_VaFractTile = 1
};

struct MetalVertex
{
    float position[4];
    float color[4];
    float texcoord[2];
    uint16_t tex_id;
    uint16_t uv_mode;
};

struct MetalUniforms
{
    float modelViewMatrix[16];
    float projectionMatrix[16];
    float uClock;
    float _pad_uniform[3];
};

struct MetalRunUniform
{
    float texBlendOverride;
    float textureOpaque;
    float textureAnimSpeed;
    float pad;
};

struct LogicalViewportRect
{
    int x, y, width, height;
};

/** GL-style viewport in drawable pixels (y from bottom). Not Apple's MTLViewport. */
struct ToriGlViewportPixels
{
    int x, y, width, height;
};

enum MetalPipeKind
{
    kMTLPipeNone = 0,
    kMTLPipe3D = 1,
    kMTLPipeUI = 2,
    kMTLPipeFont = 3
};

enum MetalPassKind
{
    kMTLPassNone = 0,
    kMTLPass3D,
    kMTLPass2D
};

static const NSUInteger kSpriteSlotBytes = 6 * 4 * sizeof(float);

struct MetalDrawBuffersResolved
{
    void* vbo = nullptr;
    int batch_chunk_index = -1;
    int vertex_index_base = 0;
    int gpu_face_count = 0;
};

struct MetalRenderCtx
{
    struct Platform2_SDL2_Renderer_Metal* renderer = nullptr;
    struct GGame* game = nullptr;
    /** Main frame command buffer; used to peek ahead for VA PNM when baking face arrays. */
    struct ToriRSRenderCommandBuffer* render_commands = nullptr;
    id<MTLDevice> device = nil;
    id<MTLRenderCommandEncoder> encoder = nil;
    id<MTLBuffer> unifBuf = nil;
    id<MTLRenderPipelineState> pipeState = nil;
    id<MTLRenderPipelineState> uiPipeState = nil;
    id<MTLRenderPipelineState> fontPipeState = nil;
    id<MTLDepthStencilState> dsState = nil;
    id<MTLTexture> dummyTex = nil;
    id<MTLSamplerState> samp = nil;
    id<MTLSamplerState> uiSamplerNearest = nil;
    id<MTLSamplerState> fontSampler = nil;
    id<MTLBuffer> spriteQuadBuf = nil;
    id<MTLBuffer> fontVbo = nil;
    id<MTLTexture> worldAtlasTex = nil;
    id<MTLBuffer> worldAtlasTilesBuf = nil;
    id<MTLBuffer> runRingBuf = nil;
    BufferedFaceOrder* bfo3d = nullptr;
    MTLViewport metalVp{};
    MTLViewport spriteVp{};
    int win_width = 0;
    int win_height = 0;
    float fbw_font = 0.0f;
    float fbh_font = 0.0f;
    int current_pipe = 0;
    int current_font_id = -1;
    id<MTLTexture> current_font_atlas_tex = nil;
    NSUInteger sprite_slot = 0;
    std::vector<float> font_verts;
    int encode_slot = 0;
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

/** cos/sin for model Y rotation in world xz; uses `metal_dash_yaw_to_radians` then `cosf`/`sinf`. */
void
metal_prebake_model_yaw_cos_sin(int dash_yaw, float* cos_out, float* sin_out);

void
metal_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height);

void
metal_remap_projection_opengl_to_metal_z(float* proj_colmajor);

float
metal_texture_animation_signed(int animation_direction, int animation_speed);

LogicalViewportRect
compute_logical_viewport_rect(int window_width, int window_height, const struct GGame* game);

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

bool
metal_resolve_model_draw_buffers(
    Gpu3DCache<void*>& cache,
    int model_gpu_id,
    Gpu3DCache<void*>::ModelBufferRange* range,
    MetalDrawBuffersResolved* out);

bool
fill_model_face_vertices_model_local(
    const struct DashModel* model,
    int f,
    int raw_tex,
    MetalVertex out[3]);

void
metal_vertex_fill_invisible(MetalVertex* v);

bool
build_model_instance(
    struct Platform2_SDL2_Renderer_Metal* renderer,
    id<MTLDevice> device,
    const struct DashModel* model,
    int model_id,
    int anim_id = 0,
    int frame_index = 0);

bool
metal_dispatch_model_load(
    struct Platform2_SDL2_Renderer_Metal* renderer,
    id<MTLDevice> device,
    int model_id,
    struct DashModel* model,
    bool in_batch,
    uint32_t batch_id);

bool
fill_face_corner_vertices_from_fa(
    const struct DashVertexArray* va,
    const struct DashFaceArray* fa,
    int face_index,
    int raw_tex,
    const int* pnm_ref_face_per_face,
    MetalVertex out[3]);

void
render_nuklear_overlay(
    struct Platform2_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    id<MTLRenderCommandEncoder> encoder);

void
metal_flush_font_batch(MetalRenderCtx* ctx);
void
metal_flush_batch(MetalRenderCtx* ctx);
void
metal_flush_3d(MetalRenderCtx* ctx, BufferedFaceOrder* bfo);
void
metal_ensure_pipe(MetalRenderCtx* ctx, int desired);

void
metal_world_atlas_init(struct Platform2_SDL2_Renderer_Metal* r, id<MTLDevice> device);
void
metal_world_atlas_shutdown(struct Platform2_SDL2_Renderer_Metal* r);

void
metal_frame_event_texture_load(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_model_load(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_model_unload(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_model_load_start(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_model_batched_load(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_model_load_end(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_model_clear(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_model_draw(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_sprite_load(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_sprite_unload(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_sprite_draw(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_font_load(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_font_draw(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_texture_load_start(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_texture_load_end(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_sprite_load_start(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_sprite_load_end(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_font_load_start(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_font_load_end(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_model_animation_load(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
metal_frame_event_vertex_array_load(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_vertex_array_unload(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_face_array_load(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_face_array_unload(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_vertex_array_load(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
metal_frame_event_batch_face_array_load(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
metal_sprite_draw_impl(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

#endif
