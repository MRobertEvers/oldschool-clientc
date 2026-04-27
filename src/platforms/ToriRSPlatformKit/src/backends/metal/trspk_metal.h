#ifndef TORIRS_PLATFORM_KIT_TRSPK_METAL_H
#define TORIRS_PLATFORM_KIT_TRSPK_METAL_H

#include "../../tools/trspk_pass_batch32.h"
#include "../../tools/trspk_resource_cache.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRSPK_METAL_INFLIGHT_FRAMES 3u
#define TRSPK_METAL_MAX_3D_PASSES_PER_FRAME 32u
#define TRSPK_METAL_DYNAMIC_INDEX_CAPACITY (1u << 21)
#define TRSPK_METAL_UNIFORM_ALIGN 256u

typedef struct TRSPK_MetalUniforms
{
    float modelViewMatrix[16];
    float projectionMatrix[16];
    float uClock;
    float _pad[3];
} TRSPK_MetalUniforms;

typedef struct TRSPK_MetalPassState
{
    void* encoder;
    void* drawable;
    void* command_buffer;
    void* depth_stencil_state;
    void* render_pass_descriptor;
    TRSPK_GPUHandle scene_vbo;
    uint32_t* index_pool;
    uint32_t index_count;
    uint32_t index_capacity;
    uint32_t uniform_pass_subslot;
    size_t index_upload_offset;
} TRSPK_MetalPassState;

typedef struct TRSPK_MetalRenderer
{
    void* device;
    void* command_queue;
    void* metal_layer;
    void* uniform_buffer;
    void* dynamic_index_buffer;
    void* pipeline_state;
    void* sampler_state;
    void* atlas_texture;
    void* atlas_tiles_buffer;
    void* frame_semaphore;
    uint32_t uniform_frame_slot;
    uint32_t width;
    uint32_t height;
    uint32_t window_width;
    uint32_t window_height;
    TRSPK_Rect pass_logical_rect;
    double frame_clock;
    bool ready;
    TRSPK_ResourceCache* cache;
    TRSPK_Batch32* batch_staging;
    TRSPK_MetalPassState pass_state;
} TRSPK_MetalRenderer;

struct GGame;
struct ToriRSRenderCommand;

typedef struct TRSPK_MetalEventContext
{
    TRSPK_MetalRenderer* renderer;
    TRSPK_ResourceCache* cache;
    TRSPK_Batch32* staging;
    uint32_t current_batch_id;
    bool batch_active;
    TRSPK_Rect default_pass_logical;
    bool has_default_pass_logical;
    uint8_t rgba_scratch
        [TRSPK_TEXTURE_DIMENSION * TRSPK_TEXTURE_DIMENSION * TRSPK_ATLAS_BYTES_PER_PIXEL];
} TRSPK_MetalEventContext;

TRSPK_MetalRenderer*
TRSPK_Metal_Init(
    void* ca_metal_layer,
    uint32_t width,
    uint32_t height);
void
TRSPK_Metal_Shutdown(TRSPK_MetalRenderer* renderer);
void
TRSPK_Metal_Resize(
    TRSPK_MetalRenderer* renderer,
    uint32_t width,
    uint32_t height);
void
TRSPK_Metal_SetWindowSize(
    TRSPK_MetalRenderer* renderer,
    uint32_t window_width,
    uint32_t window_height);
void
TRSPK_Metal_FrameBegin(TRSPK_MetalRenderer* renderer);
void
TRSPK_Metal_FrameEnd(TRSPK_MetalRenderer* renderer);

TRSPK_ResourceCache*
TRSPK_Metal_GetCache(TRSPK_MetalRenderer* renderer);
TRSPK_Batch32*
TRSPK_Metal_GetBatchStaging(TRSPK_MetalRenderer* renderer);

void
trspk_metal_cache_init_atlas(
    TRSPK_MetalRenderer* r,
    uint32_t width,
    uint32_t height);
void
trspk_metal_cache_load_texture_128(
    TRSPK_MetalRenderer* r,
    TRSPK_TextureId tex_id,
    const uint8_t* rgba_128x128,
    float anim_u,
    float anim_v,
    bool opaque);
void
trspk_metal_cache_refresh_atlas(TRSPK_MetalRenderer* r);
void
trspk_metal_cache_batch_submit(
    TRSPK_MetalRenderer* r,
    TRSPK_BatchId batch_id,
    int usage_hint);
void
trspk_metal_cache_batch_clear(
    TRSPK_MetalRenderer* r,
    TRSPK_BatchId batch_id);
void
trspk_metal_cache_unload_model(
    TRSPK_MetalRenderer* r,
    TRSPK_ModelId model_id);

void
trspk_metal_draw_begin_3d(
    TRSPK_MetalRenderer* r,
    const TRSPK_Rect* viewport);
void
trspk_metal_draw_add_model(
    TRSPK_MetalRenderer* r,
    const TRSPK_ModelPose* pose,
    const uint32_t* sorted_indices,
    uint32_t index_count);
void
trspk_metal_draw_submit_3d(
    TRSPK_MetalRenderer* r,
    const TRSPK_Mat4* view,
    const TRSPK_Mat4* proj);
void
trspk_metal_draw_clear_rect(
    TRSPK_MetalRenderer* r,
    const TRSPK_Rect* rect);

void*
trspk_metal_create_world_pipeline(
    void* device,
    uint32_t color_pixel_format);
float
trspk_metal_dash_yaw_to_radians(int dash_yaw);
void
trspk_metal_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw);
void
trspk_metal_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height);
void
trspk_metal_remap_projection_opengl_to_metal_z(float* proj_colmajor);

void
trspk_metal_event_tex_load(
    TRSPK_MetalEventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_metal_event_batch3d_begin(
    TRSPK_MetalEventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_metal_event_batch3d_model_add(
    TRSPK_MetalEventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_metal_event_batch3d_anim_add(
    TRSPK_MetalEventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_metal_event_batch3d_end(
    TRSPK_MetalEventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_metal_event_batch3d_clear(
    TRSPK_MetalEventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_metal_event_draw_model(
    TRSPK_MetalEventContext* ctx,
    struct GGame* game,
    const struct ToriRSRenderCommand* cmd,
    uint32_t* sorted_indices_buffer,
    uint32_t buffer_capacity);
void
trspk_metal_event_state_begin_3d(
    TRSPK_MetalEventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_metal_event_state_clear_rect(
    TRSPK_MetalEventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_metal_event_state_end_3d(
    TRSPK_MetalEventContext* ctx,
    struct GGame* game,
    double frame_clock);

#ifdef __cplusplus
}
#endif

#endif
