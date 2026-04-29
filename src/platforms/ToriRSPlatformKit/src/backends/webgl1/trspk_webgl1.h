#ifndef TORIRS_PLATFORM_KIT_TRSPK_WEBGL1_H
#define TORIRS_PLATFORM_KIT_TRSPK_WEBGL1_H

#include "../../tools/trspk_dynamic_slotmap16.h"
#include "../../tools/trspk_batch16.h"
#include "../../tools/trspk_dynamic_pass.h"
#include "../../tools/trspk_resource_cache.h"

#include <stdbool.h>

#ifdef __EMSCRIPTEN__
#include <GLES2/gl2.h>
#else
typedef unsigned int GLuint;
typedef int GLint;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TRSPK_WebGL1WorldShaderLocs
{
    GLint a_position;
    GLint a_color;
    GLint a_texcoord;
    GLint a_tex_id;
    GLint a_uv_mode;
    GLint u_modelViewMatrix;
    GLint u_projectionMatrix;
    GLint u_clock;
    GLint s_atlas;
} TRSPK_WebGL1WorldShaderLocs;

/**
 * One model draw in scene order. Chunk pools hold concatenated uint16 indices per chunk; each
 * subdraw references a contiguous [pool_start, pool_start+index_count) in that chunk's pool.
 * Submit must replay subdraws in this order (not a single pass per chunk), which matches the
 * legacy pass_3d builder and fixes painter order when 64k index rolls split a batch.
 */
typedef struct TRSPK_WebGL1SubDraw
{
    GLuint vbo;
    uint8_t chunk;
    uint32_t pool_start;
    uint32_t index_count;
} TRSPK_WebGL1SubDraw;

typedef struct TRSPK_WebGL1PassState
{
    uint16_t* chunk_index_pools[TRSPK_MAX_WEBGL1_CHUNKS];
    uint32_t chunk_index_counts[TRSPK_MAX_WEBGL1_CHUNKS];
    uint32_t chunk_index_caps[TRSPK_MAX_WEBGL1_CHUNKS];
    bool chunk_has_draws[TRSPK_MAX_WEBGL1_CHUNKS];
    TRSPK_WebGL1SubDraw* subdraws;
    uint32_t subdraw_count;
    uint32_t subdraw_cap;
    uint32_t uniform_pass_subslot;
} TRSPK_WebGL1PassState;

typedef struct TRSPK_WebGL1Renderer
{
    uint32_t width;
    uint32_t height;
    uint32_t window_width;
    uint32_t window_height;
    bool ready;
    GLuint prog_world3d;
    TRSPK_WebGL1WorldShaderLocs world_locs;
    GLuint atlas_texture;
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
    TRSPK_Rect pass_logical_rect;
    TRSPK_Rect pass_gl_rect;
    TRSPK_ResourceCache* cache;
    TRSPK_Batch16* batch_staging;
    TRSPK_WebGL1PassState pass_state;
} TRSPK_WebGL1Renderer;

struct DashModel;
struct GGame;
struct ToriRSRenderCommand;

typedef struct TRSPK_WebGL1EventContext
{
    TRSPK_WebGL1Renderer* renderer;
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
} TRSPK_WebGL1EventContext;

TRSPK_WebGL1Renderer*
TRSPK_WebGL1_Init(
    const char* canvas_id,
    uint32_t width,
    uint32_t height);
TRSPK_WebGL1Renderer*
TRSPK_WebGL1_InitWithCurrentContext(
    uint32_t width,
    uint32_t height);
void
TRSPK_WebGL1_Shutdown(TRSPK_WebGL1Renderer* renderer);
void
TRSPK_WebGL1_Resize(
    TRSPK_WebGL1Renderer* renderer,
    uint32_t width,
    uint32_t height);
void
TRSPK_WebGL1_SetWindowSize(
    TRSPK_WebGL1Renderer* renderer,
    uint32_t width,
    uint32_t height);
void
TRSPK_WebGL1_FrameBegin(TRSPK_WebGL1Renderer* renderer);
void
TRSPK_WebGL1_FrameEnd(TRSPK_WebGL1Renderer* renderer);
TRSPK_ResourceCache*
TRSPK_WebGL1_GetCache(TRSPK_WebGL1Renderer* renderer);
TRSPK_Batch16*
TRSPK_WebGL1_GetBatchStaging(TRSPK_WebGL1Renderer* renderer);

void
trspk_webgl1_cache_init_atlas(
    TRSPK_WebGL1Renderer* r,
    uint32_t width,
    uint32_t height);
void
trspk_webgl1_cache_load_texture_128(
    TRSPK_WebGL1Renderer* r,
    TRSPK_TextureId id,
    const uint8_t* rgba_128x128,
    float anim_u,
    float anim_v,
    bool opaque);
void
trspk_webgl1_cache_refresh_atlas(TRSPK_WebGL1Renderer* r);
void
trspk_webgl1_cache_batch_submit(
    TRSPK_WebGL1Renderer* r,
    TRSPK_BatchId batch_id,
    int usage_hint);
void
trspk_webgl1_cache_batch_clear(
    TRSPK_WebGL1Renderer* r,
    TRSPK_BatchId batch_id);
void
trspk_webgl1_cache_unload_model(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId id);
bool
trspk_webgl1_dynamic_init(TRSPK_WebGL1Renderer* r);
void
trspk_webgl1_dynamic_shutdown(TRSPK_WebGL1Renderer* r);
bool
trspk_webgl1_dynamic_load_model(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    TRSPK_UsageClass usage_class,
    const TRSPK_BakeTransform* bake);
bool
trspk_webgl1_dynamic_load_anim(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id,
    struct DashModel* model,
    uint8_t segment,
    uint16_t frame_index,
    TRSPK_UsageClass usage_class,
    const TRSPK_BakeTransform* bake);
struct TRSPK_VertexBuffer;

bool
trspk_webgl1_dynamic_store_vertex_buffer(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    const struct TRSPK_VertexBuffer* vb);
bool
trspk_webgl1_dynamic_store_dynamic_mesh(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id,
    TRSPK_UsageClass usage,
    uint32_t pose_index,
    TRSPK_DynamicMesh* mesh);
void
trspk_webgl1_dynamic_unload_model(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId model_id);

void
trspk_webgl1_draw_begin_3d(
    TRSPK_WebGL1Renderer* r,
    const TRSPK_Rect* viewport);
void
trspk_webgl1_draw_add_model(
    TRSPK_WebGL1Renderer* r,
    const TRSPK_ModelPose* pose,
    const uint16_t* sorted_indices,
    uint32_t index_count);
void
trspk_webgl1_draw_submit_3d(
    TRSPK_WebGL1Renderer* r,
    const TRSPK_Mat4* view,
    const TRSPK_Mat4* proj);
void
trspk_webgl1_draw_clear_rect(
    TRSPK_WebGL1Renderer* r,
    const TRSPK_Rect* rect);

GLuint
trspk_webgl1_create_world_program(TRSPK_WebGL1WorldShaderLocs* locs);
void
trspk_webgl1_bind_world_attribs(TRSPK_WebGL1Renderer* r);
void
trspk_webgl1_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw);
void
trspk_webgl1_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height);

void
trspk_webgl1_event_tex_load(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_webgl1_event_res_model_load(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_webgl1_event_res_model_unload(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_webgl1_event_res_anim_load(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_webgl1_event_batch3d_begin(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_webgl1_event_batch3d_model_add(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_webgl1_event_batch3d_anim_add(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_webgl1_event_batch3d_end(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_webgl1_event_batch3d_clear(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_webgl1_event_draw_model(
    TRSPK_WebGL1EventContext* ctx,
    struct GGame* game,
    const struct ToriRSRenderCommand* cmd,
    uint16_t* sorted_indices_buffer,
    uint32_t buffer_capacity);
void
trspk_webgl1_event_state_begin_3d(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);

/**
 * Fills out with the same result as opengl3_compute_logical_viewport_rect (for WebGL1 without
 * linking opengl3 into emscripten targets).
 */
void
trspk_webgl1_compute_default_pass_logical(
    TRSPK_Rect* out,
    int window_width,
    int window_height,
    const struct GGame* game);
TRSPK_Rect
trspk_webgl1_full_window_logical_rect(const TRSPK_WebGL1Renderer* r);
void
trspk_webgl1_event_state_clear_rect(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd);
void
trspk_webgl1_event_state_end_3d(
    TRSPK_WebGL1EventContext* ctx,
    struct GGame* game,
    double frame_clock);

#ifdef __cplusplus
}
#endif

#endif
