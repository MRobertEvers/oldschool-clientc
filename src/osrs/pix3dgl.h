#ifndef OSRS_PIX3D_H
#define OSRS_PIX3D_H

#include "graphics/dash_alphaint.h"
#include "graphics/dash_faceint.h"
#include "graphics/dash_hsl16.h"
#include "graphics/dash_vertexint.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Pix3DGL;
struct Pix3DGLCoreBuffers;
struct DashSprite;

struct Pix3DGL*
pix3dgl_new(
    bool z_buffer_enabled,
    bool backface_cull_enabled);
void
pix3dgl_load_texture(
    struct Pix3DGL* pix3dgl,
    int texture_id,
    const int* texels_argb,
    int width,
    int height,
    int animation_direction,
    int animation_speed,
    bool opaque);

void
pix3dgl_render_with_camera(
    struct Pix3DGL* pix3dgl,
    float camera_x,
    float camera_y,
    float camera_z,
    float camera_pitch,
    float camera_yaw,
    float screen_width,
    float screen_height);

void
pix3dgl_begin_3dframe(
    struct Pix3DGL* pix3dgl,
    float camera_x,
    float camera_y,
    float camera_z,
    float camera_pitch,
    float camera_yaw,
    float screen_width,
    float screen_height);
void
pix3dgl_end_3dframe(struct Pix3DGL* pix3dgl);
/** Call before `pix3dgl_sprite_draw`; binds 2D shader and blend state (depth off). */
void
pix3dgl_begin_2dframe(struct Pix3DGL* pix3dgl);
/** Call after the last `pix3dgl_sprite_draw` for the pass. */
void
pix3dgl_end_2dframe(struct Pix3DGL* pix3dgl);

/** Upload sprite pixel data into the per-sprite GPU texture cache (TORIRS_GFX_SPRITE_LOAD).
 *  No-op if the sprite is already cached. */
void
pix3dgl_sprite_load(
    struct Pix3DGL* pix3dgl,
    struct DashSprite* sprite);

/** Evict a sprite from the GPU texture cache and free its GL texture (TORIRS_GFX_SPRITE_UNLOAD). */
void
pix3dgl_sprite_unload(
    struct Pix3DGL* pix3dgl,
    struct DashSprite* sprite);

/** Legacy alias for pix3dgl_begin_3dframe. */
void
pix3dgl_begin_frame(
    struct Pix3DGL* pix3dgl,
    float camera_x,
    float camera_y,
    float camera_z,
    float camera_pitch,
    float camera_yaw,
    float screen_width,
    float screen_height);
/** Legacy alias for pix3dgl_end_3dframe. */
void
pix3dgl_end_frame(struct Pix3DGL* pix3dgl);
/** Reapply Pix3DGL fixed-function GL state after ImGui_ImplOpenGL3_RenderDrawData (no glGet). */
void
pix3dgl_restore_gl_state_after_imgui(struct Pix3DGL* pix3dgl);
void
pix3dgl_model_load(
    struct Pix3DGL* pix3dgl,
    int model_idx,
    vertexint_t* vertices_x,
    vertexint_t* vertices_y,
    vertexint_t* vertices_z,
    faceint_t* face_indices_a,
    faceint_t* face_indices_b,
    faceint_t* face_indices_c,
    int face_count,
    faceint_t* face_textures_nullable,
    faceint_t* face_texture_coords_nullable,
    faceint_t* textured_p_coordinate_nullable,
    faceint_t* textured_m_coordinate_nullable,
    faceint_t* textured_n_coordinate_nullable,
    hsl16_t* face_colors_hsl_a,
    hsl16_t* face_colors_hsl_b,
    hsl16_t* face_colors_hsl_c,
    int* face_infos_nullable,
    alphaint_t* face_alphas_nullable);
void
pix3dgl_model_draw(
    struct Pix3DGL* pix3dgl,
    int model_idx,
    float position_x,
    float position_y,
    float position_z,
    float yaw);

void
pix3dgl_model_draw_ordered(
    struct Pix3DGL* pix3dgl,
    int model_idx,
    float position_x,
    float position_y,
    float position_z,
    float yaw,
    const int* face_order,
    int face_order_count);

void
pix3dgl_set_animation_clock(
    struct Pix3DGL* pix3dgl,
    float clock_value);

void
pix3dgl_cleanup(struct Pix3DGL* pix3dgl);

void
pix3dgl_sprite_draw(
    struct Pix3DGL* pix3dgl,
    struct DashSprite* sprite,
    int dst_x,
    int dst_y,
    int framebuffer_width,
    int framebuffer_height,
    int rotation_r2pi2048,
    int src_x,
    int src_y,
    int src_w,
    int src_h);

#ifdef __cplusplus
}
#endif

#endif // OSRS_PIX3D_H