#ifndef PLATFORMS_OPENGL3_OPENGL3_CTX_H
#define PLATFORMS_OPENGL3_OPENGL3_CTX_H

#    include "platforms/opengl3/opengl3_renderer_core.h"

#    include "tori_rs_render.h"

#    include <SDL.h>
#    include <cmath>
#    include <cstdint>

struct GGame;

struct OpenGL3RenderCtx
{
    struct Platform2_SDL2_Renderer_OpenGL3* renderer = nullptr;
    struct GGame* game = nullptr;
};

inline constexpr int kOpenGL3MaxClearRectsPerFrame = 8;

float
opengl3_dash_yaw_to_radians(int dash_yaw);

void
opengl3_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw);

void
opengl3_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height);

LogicalViewportRect
opengl3_compute_logical_viewport_rect(int window_width, int window_height, const struct GGame* game);

ToriGlViewportPixels
opengl3_compute_gl_world_viewport_rect(
    int fb_width,
    int fb_height,
    int win_width,
    int win_height,
    const LogicalViewportRect& lr);

void
opengl3_clamped_gl_scissor(
    int fbw,
    int fbh,
    int win_w,
    int win_h,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h);

float
opengl3_texture_animation_signed(int animation_direction, int animation_speed);

bool
opengl3_gl_create_programs(struct Platform2_SDL2_Renderer_OpenGL3* r);
void
opengl3_gl_destroy_programs(struct Platform2_SDL2_Renderer_OpenGL3* r);

void
opengl3_atlas_resources_init(struct Platform2_SDL2_Renderer_OpenGL3* r);
void
opengl3_atlas_resources_shutdown(struct Platform2_SDL2_Renderer_OpenGL3* r);

void
opengl3_sync_drawable_size(struct Platform2_SDL2_Renderer_OpenGL3* renderer);

void
opengl3_gl_bind_default_world_gl_state(void);

#endif
