#ifndef TORIRS_PLATFORM_KIT_TRSPK_OPENGL3_H
#define TORIRS_PLATFORM_KIT_TRSPK_OPENGL3_H

#include "../../tools/trspk_batch32.h"
#include "../../tools/trspk_dynamic_pass.h"
#include "../../tools/trspk_dynamic_slotmap32.h"
#include "../../tools/trspk_resource_cache.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRSPK_OPENGL3_MAX_3D_PASSES_PER_FRAME 32u
#define TRSPK_OPENGL3_DYNAMIC_INDEX_CAPACITY (1u << 21)
#define TRSPK_OPENGL3_INFLIGHT_FRAMES 3u
#define TRSPK_OPENGL3_UNIFORM_ALIGN 256u

/**
 * Pass subdraw `vbo` field: static batches store the real GL buffer name (often small integers
 * from glGenBuffers). Dynamic draws store these sentinels so they cannot collide with GLuint
 * object names (unlike low integers such as 3 or 4, which match early GL allocations).
 */
#define TRSPK_OPENGL3_SUBDRAW_VBO_DYNAMIC_NPC ((TRSPK_GPUHandle)UINTPTR_MAX)
#define TRSPK_OPENGL3_SUBDRAW_VBO_DYNAMIC_PROJECTILE ((TRSPK_GPUHandle)(UINTPTR_MAX - 1u))

/** World 3D UBO layout (std140); matches Metal `TRSPK_MetalUniforms` packing. */
typedef struct TRSPK_OpenGL3Uniforms
{
    float modelViewMatrix[16];
    float projectionMatrix[16];
    float uClock;
    float _pad[3];
} TRSPK_OpenGL3Uniforms;

typedef struct TRSPK_OpenGL3StateCache
{
    uint32_t program;
    uint32_t texture;
    bool blend_enabled;
} TRSPK_OpenGL3StateCache;

/** Staged dynamic mesh bakes; GPU upload in trspk_opengl3_pass_flush_pending_dynamic_gpu_uploads. */
typedef struct TRSPK_OpenGL3DeferredDynamicBake
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
    uint32_t vbo_offset;
    uint32_t ebo_offset;
    uint32_t vertex_count;
    uint32_t index_count;
} TRSPK_OpenGL3DeferredDynamicBake;

typedef struct TRSPK_OpenGL3WorldShaderLocs
{
    int32_t a_position;
    int32_t a_color;
    int32_t a_texcoord;
    int32_t a_tex_id;
    int32_t a_uv_mode;
    int32_t s_atlas;
} TRSPK_OpenGL3WorldShaderLocs;

void
trspk_opengl3_bind_world_attribs(
    const TRSPK_OpenGL3WorldShaderLocs* locs,
    uint32_t mesh_vbo);

typedef struct TRSPK_OpenGL3SubDraw
{
    TRSPK_GPUHandle vbo;
    TRSPK_GPUHandle vao;
    uint32_t pool_start;
    uint32_t index_count;
} TRSPK_OpenGL3SubDraw;

typedef struct TRSPK_OpenGL3PassState
{
    uint32_t* index_pool;
    uint32_t index_count;
    uint32_t index_capacity;
    TRSPK_OpenGL3SubDraw* subdraws;
    uint32_t subdraw_count;
    uint32_t subdraw_capacity;
    uint32_t uniform_pass_subslot;
    size_t index_upload_offset;
} TRSPK_OpenGL3PassState;

typedef struct TRSPK_OpenGL3Renderer
{
    uint32_t prog_world3d;
    TRSPK_OpenGL3WorldShaderLocs world_locs;
    uint32_t uniform_ubo;
    void* dynamic_index_map;
    void* uniform_buffer_map;
    uint32_t triple_ring_slot;
    void* ring_inflight_fences[TRSPK_OPENGL3_INFLIGHT_FRAMES];
    TRSPK_OpenGL3StateCache state_cache;
    uint32_t atlas_texture;
    uint32_t dynamic_ibo;
    uint32_t vao_dynamic_npc;
    uint32_t vao_dynamic_projectile;
    uint32_t width;
    uint32_t height;
    uint32_t window_width;
    uint32_t window_height;
    TRSPK_Rect pass_logical_rect;
    double frame_clock;
    bool ready;
    TRSPK_ResourceCache* cache;
    TRSPK_Batch32* batch_staging;
    uint32_t dynamic_npc_vbo;
    uint32_t dynamic_npc_ebo;
    uint32_t dynamic_projectile_vbo;
    uint32_t dynamic_projectile_ebo;
    TRSPK_DynamicSlotmap32* dynamic_npc_slotmap;
    TRSPK_DynamicSlotmap32* dynamic_projectile_slotmap;
    TRSPK_DynamicSlotHandle* dynamic_slot_handles;
    TRSPK_UsageClass* dynamic_slot_usages;
    TRSPK_OpenGL3DeferredDynamicBake* deferred_dynamic_bakes;
    uint32_t deferred_dynamic_bake_count;
    uint32_t deferred_dynamic_bake_cap;
    TRSPK_OpenGL3PassState pass_state;
    uint32_t diag_frame_submitted_draws;
    /**
     * Monotonic per-stream revisions bumped whenever the NPC/projectile VBO or mesh EBO buffer
     * object is replaced. OpenGL may reuse the same GLuint after glDeleteBuffers; comparing names
     * is unsafe for skipping VAO refresh — use revisions instead.
     */
    uint32_t dynamic_npc_stream_revision;
    uint32_t dynamic_projectile_stream_revision;
    /** Last stream_revision applied to each dynamic VAO via world_vao_setup. */
    uint32_t dynamic_vao_applied_revision_npc;
    uint32_t dynamic_vao_applied_revision_projectile;
    size_t dynamic_npc_vbo_allocated_bytes;
    size_t dynamic_npc_ebo_allocated_bytes;
    size_t dynamic_projectile_vbo_allocated_bytes;
    size_t dynamic_projectile_ebo_allocated_bytes;
} TRSPK_OpenGL3Renderer;

struct DashModel;
struct GGame;
struct ToriRSRenderCommand;

typedef struct TRSPK_OpenGL3EventContext
{
    TRSPK_OpenGL3Renderer* renderer;
    TRSPK_ResourceCache* cache;
    TRSPK_Batch32* staging;
    uint32_t current_batch_id;
    bool batch_active;
    TRSPK_Rect default_pass_logical;
    bool has_default_pass_logical;
    uint8_t rgba_scratch
        [TRSPK_TEXTURE_DIMENSION * TRSPK_TEXTURE_DIMENSION * TRSPK_ATLAS_BYTES_PER_PIXEL];
} TRSPK_OpenGL3EventContext;

TRSPK_OpenGL3Renderer*
TRSPK_OpenGL3_InitWithCurrentContext(
    uint32_t width,
    uint32_t height);
void
TRSPK_OpenGL3_Shutdown(TRSPK_OpenGL3Renderer* renderer);
void
TRSPK_OpenGL3_Resize(
    TRSPK_OpenGL3Renderer* renderer,
    uint32_t width,
    uint32_t height);
void
TRSPK_OpenGL3_SetWindowSize(
    TRSPK_OpenGL3Renderer* renderer,
    uint32_t window_width,
    uint32_t window_height);
void
TRSPK_OpenGL3_FrameBegin(TRSPK_OpenGL3Renderer* renderer);
void
TRSPK_OpenGL3_FrameEnd(TRSPK_OpenGL3Renderer* renderer);

TRSPK_ResourceCache*
TRSPK_OpenGL3_GetCache(TRSPK_OpenGL3Renderer* renderer);
TRSPK_Batch32*
TRSPK_OpenGL3_GetBatchStaging(TRSPK_OpenGL3Renderer* renderer);

uint32_t
trspk_opengl3_create_world_program(TRSPK_OpenGL3WorldShaderLocs* locs);

void
trspk_opengl3_world_vao_setup(
    uint32_t vao,
    const TRSPK_OpenGL3WorldShaderLocs* locs,
    uint32_t mesh_vbo,
    uint32_t ibo);

void
trspk_opengl3_refresh_dynamic_world_vao(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_UsageClass usage);

void
trspk_opengl3_dynamic_world_vao_if_needed(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_UsageClass usage);

void
trspk_opengl3_cache_init_atlas(
    TRSPK_OpenGL3Renderer* r,
    uint32_t width,
    uint32_t height);
void
trspk_opengl3_cache_load_texture_128(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_TextureId tex_id,
    const uint8_t* rgba_128x128,
    float anim_u,
    float anim_v,
    bool opaque);
void
trspk_opengl3_cache_refresh_atlas(TRSPK_OpenGL3Renderer* r);
void
trspk_opengl3_cache_batch_submit(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_BatchId batch_id,
    int usage_hint);
void
trspk_opengl3_cache_batch_clear(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_BatchId batch_id);
void
trspk_opengl3_cache_unload_model(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_ModelId model_id);
bool
trspk_opengl3_dynamic_init(TRSPK_OpenGL3Renderer* r);
void
trspk_opengl3_dynamic_shutdown(TRSPK_OpenGL3Renderer* r);
bool
trspk_opengl3_dynamic_load_model(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    TRSPK_UsageClass usage_class,
    const TRSPK_BakeTransform* bake);
bool
trspk_opengl3_dynamic_load_anim(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    uint8_t segment,
    uint16_t frame_index,
    TRSPK_UsageClass usage_class,
    const TRSPK_BakeTransform* bake);
bool
trspk_opengl3_dynamic_store_vertex_buffer(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    const struct TRSPK_VertexBuffer* vb);
bool
trspk_opengl3_dynamic_store_dynamic_mesh(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    TRSPK_DynamicMesh* mesh);
void
trspk_opengl3_dynamic_unload_model(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_ModelId model_id);

void
trspk_opengl3_pass_flush_pending_dynamic_gpu_uploads(TRSPK_OpenGL3Renderer* r);
void
trspk_opengl3_dynamic_reset_pass(TRSPK_OpenGL3Renderer* r);
bool
trspk_opengl3_dynamic_enqueue_draw_mesh_deferred(
    TRSPK_OpenGL3Renderer* r,
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
trspk_opengl3_draw_begin_3d(
    TRSPK_OpenGL3Renderer* r,
    const TRSPK_Rect* viewport);
void
trspk_opengl3_draw_add_model(
    TRSPK_OpenGL3Renderer* r,
    const TRSPK_ModelPose* pose,
    const uint32_t* sorted_indices,
    uint32_t index_count);
void
trspk_opengl3_draw_submit_3d(
    TRSPK_OpenGL3Renderer* r,
    const TRSPK_Mat4* view,
    const TRSPK_Mat4* proj);
void
trspk_opengl3_draw_clear_rect(
    TRSPK_OpenGL3Renderer* r,
    const TRSPK_Rect* rect);
/** Call after non-renderer code changes GL program, texture unit 0, or blend state. */
void
trspk_opengl3_invalidate_draw_state_cache(TRSPK_OpenGL3Renderer* r);

float
trspk_opengl3_dash_yaw_to_radians(int dash_yaw);
void
trspk_opengl3_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw);
void
trspk_opengl3_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height);

void
trspk_opengl3_event_tex_load(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_opengl3_event_res_model_load(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_opengl3_event_res_model_unload(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_opengl3_event_res_anim_load(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_opengl3_event_batch3d_begin(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_opengl3_event_batch3d_model_add(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_opengl3_event_batch3d_anim_add(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_opengl3_event_batch3d_end(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_opengl3_event_batch3d_clear(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_opengl3_event_draw_model(
    TRSPK_OpenGL3EventContext* ctx,
    struct GGame* game,
    const struct ToriRSRenderCommand* cmd,
    uint32_t* sorted_indices_buffer,
    uint32_t buffer_capacity);
void
trspk_opengl3_event_state_begin_3d(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_opengl3_event_state_clear_rect(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_opengl3_event_state_end_3d(
    TRSPK_OpenGL3EventContext* ctx,
    struct GGame* game,
    double frame_clock);

#ifdef __cplusplus
}
#endif

#endif
