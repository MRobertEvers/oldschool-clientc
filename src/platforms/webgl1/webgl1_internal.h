#ifndef PLATFORMS_WEBGL1_WEBGL1_INTERNAL_H
#define PLATFORMS_WEBGL1_WEBGL1_INTERNAL_H

#include "platforms/webgl1/webgl1.h"
#include "platforms/webgl1/webgl1_vertex.h"

#include "platforms/common/buffered_2d_order.h"
#include "platforms/common/buffered_face_order.h"
#include "platforms/common/buffered_font_2d.h"
#include "platforms/common/buffered_sprite_2d.h"
#include "platforms/common/torirs_gpu_clipspace.h"
#include "platforms/common/torirs_nk_ui_bridge.h"
#include "platforms/common/torirs_nuklear_debug_panel.h"
#include "tori_rs_render.h"

#include <SDL.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern "C" {
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "graphics/shared_tables.h"
#include "graphics/uv_pnm.h"
#include "platforms/common/platform_memory.h"
#include "tori_rs.h"
}

struct LogicalViewportRect
{
    int x, y, width, height;
};

struct GLViewportRect
{
    int x, y, width, height;
};

struct ToriGlViewportPixels
{
    int x, y, width, height;
};

struct WgDrawBuffersResolved
{
    GLuint vbo = 0;
    int batch_chunk_index = -1;
    int vertex_index_base = 0;
    int gpu_face_count = 0;
};

struct WebGL1RenderCtx
{
    struct Platform2_SDL2_Renderer_WebGL1* renderer = nullptr;
    struct GGame* game = nullptr;

    int win_width = 0;
    int win_height = 0;
    LogicalViewportRect logical_vp{};
    ToriGlViewportPixels ui_gl_vp{};
    int lb_x = 0;
    int lb_gl_y = 0;
    int lb_w = 0;
    int lb_h = 0;

    float world_uniforms[16 + 16 + 4];

    BufferedFaceOrder* bfo3d = nullptr;
    BufferedSprite2D* bsp2d = nullptr;
    BufferedFont2D* bft2d = nullptr;
    Buffered2DOrder* b2d_order = nullptr;
    bool split_sprite_before_next_enqueue = false;
    bool split_font_before_next_set_font = false;

    int current_pass = 0;
    bool world_clear_pending = false;
    int world_clear_x = 0;
    int world_clear_y = 0;
    int world_clear_w = 0;
    int world_clear_h = 0;
};

extern struct nk_context* s_webgl1_nk;
extern Uint64 s_webgl1_ui_prev_perf;

LogicalViewportRect
wg1_compute_logical_viewport_rect(
    int window_width,
    int window_height,
    const struct GGame* game);

ToriGlViewportPixels
wg1_compute_gl_world_viewport_rect(
    int fb_width,
    int fb_height,
    int win_width,
    int win_height,
    const LogicalViewportRect& lr);

GLViewportRect
wg1_compute_world_viewport_in_letterbox(
    int lb_w,
    int lb_h,
    int window_width,
    int window_height,
    const LogicalViewportRect& logical_rect);

void
wg1_matrices_for_frame(
    struct GGame* game,
    float projection_width,
    float projection_height,
    float* out_modelview16,
    float* out_proj16);

float
wg1_dash_yaw_to_radians(int dash_yaw);

void
wg1_prebake_model_yaw_cos_sin(int dash_yaw, float* cos_out, float* sin_out);

float
wg1_texture_animation_signed(int animation_direction, int animation_speed);

void
wg1_world_atlas_ensure(struct Platform2_SDL2_Renderer_WebGL1* r);
bool
wg1_world_atlas_alloc_rect(
    struct Platform2_SDL2_Renderer_WebGL1* r,
    int w,
    int h,
    int* out_x,
    int* out_y);
void
wg1_world_atlas_shutdown(struct Platform2_SDL2_Renderer_WebGL1* r);
void
wg1_upload_tile_meta_tex(struct Platform2_SDL2_Renderer_WebGL1* r);

bool
wg1_fill_model_face_vertices_model_local(
    const struct DashModel* model,
    int f,
    int raw_tex,
    WgVertex out[3]);

void
wg1_vertex_fill_invisible(WgVertex* v);

bool
wg1_fill_face_corner_vertices_from_fa(
    const struct DashVertexArray* va,
    const struct DashFaceArray* fa,
    int face_index,
    int raw_tex,
    const int* pnm_ref_face_per_face,
    WgVertex out[3]);

bool
wg1_build_model_instance(
    struct Platform2_SDL2_Renderer_WebGL1* r,
    const struct DashModel* model,
    int model_id,
    int anim_id,
    int frame_index);

bool
wg1_dispatch_model_load(
    struct Platform2_SDL2_Renderer_WebGL1* r,
    int model_id,
    struct DashModel* model,
    bool in_batch,
    uint32_t batch_id);

bool
wg1_resolve_model_draw_buffers(
    Gpu3DCache<GLuint>& cache,
    int model_gpu_id,
    Gpu3DCache<GLuint>::ModelBufferRange* range,
    WgDrawBuffersResolved* out);

GLuint
wg1_compile_shader(GLenum type, const char* src);
GLuint
wg1_link_program(GLuint vs, GLuint fs);

bool
wg1_shaders_init(struct Platform2_SDL2_Renderer_WebGL1* r);
void
wg1_shaders_shutdown(struct Platform2_SDL2_Renderer_WebGL1* r);

void
wg1_restore_gl_state_after_ui(struct Platform2_SDL2_Renderer_WebGL1* r);

void
wg1_flush_3d(WebGL1RenderCtx* ctx, BufferedFaceOrder* bfo);
void
wg1_flush_2d(WebGL1RenderCtx* ctx);
void
wg1_ensure_world_pipe(WebGL1RenderCtx* ctx);
void
wg1_ensure_ui_sprite_pipe(WebGL1RenderCtx* ctx);
void
wg1_ensure_font_pipe(WebGL1RenderCtx* ctx);

void
wg1_frame_event_begin_3d(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_end_3d(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_begin_2d(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_end_2d(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_clear_rect(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_texture_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_batch_texture_load_start(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_batch_texture_load_end(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_model_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_model_unload(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_model_draw(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_model_animation_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_batch_model_load_start(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_model_batched_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_batch_model_load_end(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_batch_model_clear(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_batch_vertex_array_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_batch_face_array_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_patch_batched_va_model_verts(WebGL1RenderCtx* ctx, struct DashModel* model);
void
wg1_frame_event_sprite_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_sprite_unload(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_sprite_draw(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_font_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_font_draw(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_batch_sprite_load_start(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_batch_sprite_load_end(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_batch_font_load_start(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);
void
wg1_frame_event_batch_font_load_end(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

int
wg1_count_loaded_model_slots(Gpu3DCache<GLuint>& C);

void
wg1_release_model_gpu_buffers(struct Platform2_SDL2_Renderer_WebGL1* r, int mid);

void
sync_canvas_size(struct Platform2_SDL2_Renderer_WebGL1* renderer);

#endif
