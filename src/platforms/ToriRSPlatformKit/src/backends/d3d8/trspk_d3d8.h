#ifndef TORIRS_PLATFORM_KIT_TRSPK_D3D8_H
#define TORIRS_PLATFORM_KIT_TRSPK_D3D8_H

#include "../../tools/trspk_dynamic_slotmap16.h"
#include "../../tools/trspk_batch16.h"
#include "../../tools/trspk_dynamic_pass.h"
#include "../../tools/trspk_resource_cache.h"
#include "../../tools/trspk_dynamic_batch_upload.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Opaque Win32 HWND for TRSPK_D3D8_Init; cast at platform boundary. */
#ifndef TRSPK_D3D8_HWND_TYPEDEF
#define TRSPK_D3D8_HWND_TYPEDEF
typedef void* TRSPK_D3D8_WindowHandle;
#endif

typedef uint32_t GLuint;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One model append in scene order. Chunk pools hold concatenated uint16 indices per chunk; each
 * subdraw references a contiguous [pool_start, pool_start+index_count) in that chunk's pool.
 * Indices are model-local (legacy D3D8); IDirect3DDevice8::SetIndices BaseVertexIndex = vbo_offset.
 * Submit walks subdraws in order and merges consecutive entries with the same chunk, same VBO,
 * same vbo_offset, and contiguous pool_start into one DrawIndexedPrimitive (see d3d8_draw.cpp).
 */
typedef struct TRSPK_D3D8SubDraw
{
    GLuint vbo;
    /** Chunk VBO byte offset in vertices; D3D8 SetIndices BaseVertexIndex (legacy platform_impl2). */
    uint32_t vbo_offset;
    uint8_t chunk;
    uint32_t pool_start;
    uint32_t index_count;
} TRSPK_D3D8SubDraw;

/** Staged dynamic draw mesh bakes; GPU upload in trspk_d3d8_pass_flush_pending_dynamic_gpu_uploads. */
typedef struct TRSPK_D3D8DeferredDynamicBake
{
    TRSPK_UsageClass usage;
    /** Scene2 element id: keys dynamic VBO sub-range and cached pose. */
    TRSPK_ModelId model_id;
    /** Scene2 `visual_id`: keys CPU mesh LRU for deferred bake. */
    TRSPK_ModelId lru_model_id;
    uint32_t pose_index;
    uint8_t seg;
    uint16_t frame_i;
    TRSPK_BakeTransform bake;
    uint8_t chunk;
    uint32_t vbo_offset;
    uint32_t ebo_offset;
    uint32_t vertex_count;
    uint32_t index_count;
} TRSPK_D3D8DeferredDynamicBake;

typedef struct TRSPK_D3D8PassState
{
    uint16_t* chunk_index_pools[TRSPK_MAX_WEBGL1_CHUNKS];
    uint32_t chunk_index_counts[TRSPK_MAX_WEBGL1_CHUNKS];
    uint32_t chunk_index_caps[TRSPK_MAX_WEBGL1_CHUNKS];
    bool chunk_has_draws[TRSPK_MAX_WEBGL1_CHUNKS];
    TRSPK_D3D8SubDraw* subdraws;
    uint32_t subdraw_count;
    uint32_t subdraw_cap;
    uint32_t uniform_pass_subslot;
} TRSPK_D3D8PassState;

typedef struct TRSPK_D3D8Renderer
{
    uint32_t width;
    uint32_t height;
    uint32_t window_width;
    uint32_t window_height;
    bool ready;
    GLuint atlas_texture;
    /** IDirect3DTexture8* — offscreen scene color (RENDERTARGET X8R8G8B8), legacy scene_rt_tex. */
    GLuint scene_rt_tex;
    /** IDirect3DSurface8* — level 0 of scene_rt_tex, legacy scene_rt_surf. */
    GLuint scene_rt_surf;
    /** IDirect3DSurface8* — depth for scene RT (D16), legacy scene_ds. */
    GLuint depth_stencil_surf;
    /** Legacy platform_impl2 dash_offset_* for 3D viewport origin inside the scene buffer. */
    int32_t dash_offset_x;
    int32_t dash_offset_y;
    GLuint dynamic_ibo;
    GLuint batch_chunk_vbos[TRSPK_MAX_WEBGL1_CHUNKS];
    GLuint batch_chunk_ebos[TRSPK_MAX_WEBGL1_CHUNKS];
    uint32_t chunk_count;
    GLuint dynamic_npc_vbos[TRSPK_MAX_WEBGL1_CHUNKS];
    GLuint dynamic_npc_ebos[TRSPK_MAX_WEBGL1_CHUNKS];
    GLuint dynamic_projectile_vbos[TRSPK_MAX_WEBGL1_CHUNKS];
    GLuint dynamic_projectile_ebos[TRSPK_MAX_WEBGL1_CHUNKS];
    TRSPK_DynamicSlotmap16* dynamic_npc_slotmap;
    TRSPK_DynamicSlotmap16* dynamic_projectile_slotmap;
    TRSPK_DynamicSlotHandle* dynamic_slot_handles;
    TRSPK_UsageClass* dynamic_slot_usages;
    double frame_clock;
    uint32_t diag_frame_submitted_draws;
    /**
     * Merge / batching diagnostics (reset in TRSPK_D3D8_FrameBegin). Updated in
     * trspk_d3d8_draw_submit_3d when __EMSCRIPTEN__. Define TRSPK_WEBGL1_VERBOSE_MERGE for
     * per-pass stderr logs from that function.
     */
    uint32_t diag_frame_pass_subdraws;
    /** Times merge stopped: next subdraw had a different batch chunk than the run. */
    uint32_t diag_frame_merge_break_chunk;
    /** Times merge stopped: next subdraw used a different mesh VBO. */
    uint32_t diag_frame_merge_break_vbo;
    /** Times merge stopped: next.pool_start != contiguous end of run (pool order gap). */
    uint32_t diag_frame_merge_break_pool_gap;
    /** Times merge stopped: next subdraw invalid or pool bounds check failed. */
    uint32_t diag_frame_merge_break_next_invalid;
    /** Subdraw slots skipped at start of a run (invalid first entry). */
    uint32_t diag_frame_merge_outer_skips;
    TRSPK_Rect pass_logical_rect;
    TRSPK_Rect pass_gl_rect;
    TRSPK_ResourceCache* cache;
    TRSPK_Batch16* batch_staging;
    /** Grows once; avoids per-call malloc when converting u32→u16 indices for dynamic uploads. */
    uint16_t* u16_index_scratch;
    uint32_t u16_index_scratch_cap;
    /** Reused by batched dynamic flush on Emscripten (merge scratch + sort indices + u16 pool). */
    TRSPK_DynamicBatchScratch dynamic_flush_batch;
    uint16_t* d3d8_flush_u16_pool;
    size_t d3d8_flush_u16_pool_bytes;
    TRSPK_D3D8DeferredDynamicBake* deferred_dynamic_bakes;
    uint32_t deferred_dynamic_bake_count;
    uint32_t deferred_dynamic_bake_cap;
    TRSPK_D3D8PassState pass_state;
    /** Win32 HWND passed to TRSPK_D3D8_Init (opaque). */
    TRSPK_D3D8_WindowHandle platform_hwnd;
    /** IDirect3DDevice8* and IDirect3D8* for internal backend use. */
    uintptr_t com_device;
    uintptr_t com_d3d;
    bool device_lost;
} TRSPK_D3D8Renderer;

struct DashModel;
struct GGame;
struct ToriRSRenderCommand;

typedef struct TRSPK_D3D8EventContext
{
    TRSPK_D3D8Renderer* renderer;
    TRSPK_ResourceCache* cache;
    TRSPK_Batch16* staging;
    uint32_t current_batch_id;
    bool batch_active;
    uint32_t diag_frame_model_draw_cmds;
    uint32_t diag_frame_pose_invalid_skips;
    uint32_t diag_frame_submitted_model_draws;
    uint8_t rgba_scratch
        [TRSPK_TEXTURE_DIMENSION * TRSPK_TEXTURE_DIMENSION * TRSPK_ATLAS_BYTES_PER_PIXEL];
    /** Window-space 3D rect when begin_3d w/h are 0; from opengl3_compute_logical_viewport_rect. */
    TRSPK_Rect default_pass_logical;
    bool has_default_pass_logical;
} TRSPK_D3D8EventContext;

TRSPK_D3D8Renderer*
TRSPK_D3D8_Init(
    TRSPK_D3D8_WindowHandle hwnd,
    uint32_t width,
    uint32_t height);
/** After D3DERR_DEVICENOTRESET; returns false if still lost. */
bool
TRSPK_D3D8_TryReset(TRSPK_D3D8Renderer* renderer);
void
TRSPK_D3D8_NotifyDeviceLost(TRSPK_D3D8Renderer* renderer);
void
TRSPK_D3D8_Shutdown(TRSPK_D3D8Renderer* renderer);
void
TRSPK_D3D8_Resize(
    TRSPK_D3D8Renderer* renderer,
    uint32_t width,
    uint32_t height);
void
TRSPK_D3D8_SetWindowSize(
    TRSPK_D3D8Renderer* renderer,
    uint32_t width,
    uint32_t height);
/**
 * Begins the offscreen scene pass (legacy platform_impl2_win32_renderer_d3d8: SetRenderTarget
 * scene_rt + scene_ds, default render state, Z-clear in 3D sub-viewport, BeginScene).
 * `view_w` / `view_h` are the 3D sub-rect size (e.g. game->view_port); if <= 0, uses buffer size.
 */
void
TRSPK_D3D8_FrameBegin(TRSPK_D3D8Renderer* renderer, int view_w, int view_h);
/** Ends the scene (`EndScene`). Call after optional platform overlays; then `TRSPK_D3D8_Present`. */
void
TRSPK_D3D8_FrameEnd(TRSPK_D3D8Renderer* renderer);
void
TRSPK_D3D8_Present(TRSPK_D3D8Renderer* renderer);
/** IDirect3DDevice8* — do not Release; owned by the renderer. */
void*
TRSPK_D3D8_GetDevice(TRSPK_D3D8Renderer* renderer);
TRSPK_ResourceCache*
TRSPK_D3D8_GetCache(TRSPK_D3D8Renderer* renderer);
TRSPK_Batch16*
TRSPK_D3D8_GetBatchStaging(TRSPK_D3D8Renderer* renderer);

void
trspk_d3d8_cache_init_atlas(
    TRSPK_D3D8Renderer* r,
    uint32_t width,
    uint32_t height);
void
trspk_d3d8_cache_load_texture_128(
    TRSPK_D3D8Renderer* r,
    TRSPK_TextureId id,
    const uint8_t* rgba_128x128,
    float anim_u,
    float anim_v,
    bool opaque);
void
trspk_d3d8_cache_refresh_atlas(TRSPK_D3D8Renderer* r);
void
trspk_d3d8_cache_batch_submit(
    TRSPK_D3D8Renderer* r,
    TRSPK_BatchId batch_id,
    int usage_hint);
void
trspk_d3d8_cache_batch_clear(
    TRSPK_D3D8Renderer* r,
    TRSPK_BatchId batch_id);
void
trspk_d3d8_cache_unload_model(
    TRSPK_D3D8Renderer* r,
    TRSPK_ModelId id);
bool
trspk_d3d8_dynamic_init(TRSPK_D3D8Renderer* r);
void
trspk_d3d8_dynamic_shutdown(TRSPK_D3D8Renderer* r);
bool
trspk_d3d8_dynamic_load_model(
    TRSPK_D3D8Renderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    TRSPK_UsageClass usage_class,
    const TRSPK_BakeTransform* bake);
bool
trspk_d3d8_dynamic_load_anim(
    TRSPK_D3D8Renderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    uint8_t segment,
    uint16_t frame_index,
    TRSPK_UsageClass usage_class,
    const TRSPK_BakeTransform* bake);
struct TRSPK_VertexBuffer;

bool
trspk_d3d8_dynamic_store_vertex_buffer(
    TRSPK_D3D8Renderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    const struct TRSPK_VertexBuffer* vb);
bool
trspk_d3d8_dynamic_store_dynamic_mesh(
    TRSPK_D3D8Renderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    TRSPK_DynamicMesh* mesh);
bool
trspk_d3d8_dynamic_enqueue_draw_mesh_deferred(
    TRSPK_D3D8Renderer* r,
    TRSPK_ModelId pose_storage_model_id,
    TRSPK_ModelId lru_model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    uint32_t array_vertex_count,
    uint32_t array_index_count,
    const TRSPK_BakeTransform* bake);

void
trspk_d3d8_pass_free_pending_dynamic_uploads(TRSPK_D3D8Renderer* r);

void
trspk_d3d8_pass_flush_pending_dynamic_gpu_uploads(TRSPK_D3D8Renderer* r);

void
trspk_d3d8_dynamic_unload_model(
    TRSPK_D3D8Renderer* r,
    TRSPK_ModelId model_id);

void
trspk_d3d8_draw_begin_3d(
    TRSPK_D3D8Renderer* r,
    const TRSPK_Rect* viewport);
void
trspk_d3d8_draw_add_model(
    TRSPK_D3D8Renderer* r,
    const TRSPK_ModelPose* pose,
    const uint16_t* sorted_indices,
    uint32_t index_count);
void
trspk_d3d8_draw_submit_3d(
    TRSPK_D3D8Renderer* r,
    const TRSPK_Mat4* view,
    const TRSPK_Mat4* proj);
void
trspk_d3d8_draw_clear_rect(
    TRSPK_D3D8Renderer* r,
    const TRSPK_Rect* rect);

void
trspk_d3d8_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw);
void
trspk_d3d8_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height);
/** Same OpenGL column-major proj to clip-Z half-range as Metal / legacy Win32 D3D8 (in-place). */
void
trspk_d3d8_remap_projection_opengl_to_d3d8_z(float* proj_colmajor);

void
trspk_d3d8_event_tex_load(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_event_res_model_load(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_event_res_model_unload(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_event_res_anim_load(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_event_batch3d_begin(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_event_batch3d_model_add(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_event_batch3d_anim_add(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_event_batch3d_end(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_event_batch3d_clear(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_event_draw_model(
    TRSPK_D3D8EventContext* ctx,
    struct GGame* game,
    const struct ToriRSRenderCommand* cmd,
    uint16_t* sorted_indices_buffer,
    uint32_t buffer_capacity);
void
trspk_d3d8_event_state_begin_3d(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);

/**
 * Fills out with the same result as opengl3_compute_logical_viewport_rect (for WebGL1 without
 * linking opengl3 into emscripten targets).
 */
void
trspk_d3d8_compute_default_pass_logical(
    TRSPK_Rect* out,
    int window_width,
    int window_height,
    const struct GGame* game);
TRSPK_Rect
trspk_d3d8_full_window_logical_rect(const TRSPK_D3D8Renderer* r);
void
trspk_d3d8_event_state_clear_rect(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_d3d8_event_state_end_3d(
    TRSPK_D3D8EventContext* ctx,
    struct GGame* game,
    double frame_clock);

#ifdef __cplusplus
}
#endif

#endif
