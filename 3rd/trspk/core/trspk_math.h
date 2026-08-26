#ifndef TRSPK_MATH_H
#define TRSPK_MATH_H

#include "trspk_color_simd.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

struct TRSPK_Rect
{
    int x;
    int y;
    int w;
    int h;
};

struct TRSPK_Letterbox
{
    int x;
    int y;
    int w;
    int h;
};

static inline void
trspk_compute_view_matrix(
    float out_matrix[16],
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw)
{
    const float cosPitch = cosf(-pitch);
    const float sinPitch = sinf(-pitch);
    const float cosYaw = cosf(-yaw);
    const float sinYaw = sinf(-yaw);

    out_matrix[0] = cosYaw;
    out_matrix[1] = sinYaw * sinPitch;
    out_matrix[2] = sinYaw * cosPitch;
    out_matrix[3] = 0.0f;
    out_matrix[4] = 0.0f;
    out_matrix[5] = cosPitch;
    out_matrix[6] = -sinPitch;
    out_matrix[7] = 0.0f;
    out_matrix[8] = -sinYaw;
    out_matrix[9] = cosYaw * sinPitch;
    out_matrix[10] = cosYaw * cosPitch;
    out_matrix[11] = 0.0f;
    out_matrix[12] = -camera_x * cosYaw + camera_z * sinYaw;
    out_matrix[13] =
        -camera_x * sinYaw * sinPitch - camera_y * cosPitch - camera_z * cosYaw * sinPitch;
    out_matrix[14] =
        -camera_x * sinYaw * cosPitch + camera_y * sinPitch - camera_z * cosYaw * cosPitch;
    out_matrix[15] = 1.0f;
}

/**
 * The GPU spelling of the rasterizer's projection.
 *
 * ToriDraw projects `screen = coord * scale / z` (see
 * 3rd/toridraw/graphics/projection.h): a plain integer multiplier recomputed
 * per layout from the world viewport height, not an angle. So this takes that
 * same scale, and the matrix is the identity translation of the formula —
 * divide the screen offset by half the viewport to reach NDC:
 *
 *     ndc_x =  (scale * x / z) / (w/2)
 *     ndc_y = -(scale * y / z) / (h/2)      y is down in screen space
 *     w_clip = z
 *
 * There used to be a `fov` parameter here, multiplying a hardcoded 512. It is
 * gone on purpose. An angle cannot express most scales — the conversion opens
 * with `fov >> 1`, so only 320 of the 961 integer scales in [64,1024] are
 * reachable and the reference's own 191 is not among them — and a renderer that
 * takes an angle can only ever approximate the rasterizer it is meant to match.
 * Callers resolve `toridraw_proj_cot16(...) / 128.0f` and pass the result.
 */
static inline void
trspk_compute_projection_matrix(
    float out_matrix[16],
    float proj_scale,
    float screen_width,
    float screen_height)
{
    memset(out_matrix, 0, sizeof(float) * 16u);
    out_matrix[0] = proj_scale / (screen_width * 0.5f);
    out_matrix[5] = -proj_scale / (screen_height * 0.5f);
    out_matrix[11] = 1.0f;
    out_matrix[14] = -1.0f;
    out_matrix[15] = 0.0f;
}

/**
 * Parallel (map-editor) projection: no perspective divide at all.
 *
 * `zoom` is pixels per world unit. Depth still has to reach clip space, so z is
 * mapped through the same [-1,1] range the perspective path lands in, with the
 * near plane behind the camera — under parallel projection nothing is unsafe,
 * so the near plane is policy rather than a singularity.
 */
static inline void
trspk_compute_projection_matrix_parallel(
    float out_matrix[16],
    float zoom,
    float screen_width,
    float screen_height,
    float depth_range)
{
    memset(out_matrix, 0, sizeof(float) * 16u);
    out_matrix[0] = zoom / (screen_width * 0.5f);
    out_matrix[5] = -zoom / (screen_height * 0.5f);
    out_matrix[10] = 1.0f / depth_range;
    out_matrix[15] = 1.0f;
}

static inline void
trspk_mat4_ortho2d_top_left(
    float m[16],
    float left,
    float right,
    float bottom,
    float top)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / (right - left);
    m[5] = 2.0f / (top - bottom);
    m[10] = -1.0f;
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[15] = 1.0f;
}

static inline void
trspk_rect_intersect(
    int ax,
    int ay,
    int aw,
    int ah,
    int bx,
    int by,
    int bw,
    int bh,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    const bool a_valid = aw > 0 && ah > 0;
    const bool b_valid = bw > 0 && bh > 0;

    if( !a_valid && !b_valid )
    {
        *out_x = 0;
        *out_y = 0;
        *out_w = 0;
        *out_h = 0;
        return;
    }
    if( !a_valid )
    {
        *out_x = bx;
        *out_y = by;
        *out_w = bw;
        *out_h = bh;
        return;
    }
    if( !b_valid )
    {
        *out_x = ax;
        *out_y = ay;
        *out_w = aw;
        *out_h = ah;
        return;
    }

    const int x0 = ax > bx ? ax : bx;
    const int y0 = ay > by ? ay : by;
    const int x1 = (ax + aw) < (bx + bw) ? (ax + aw) : (bx + bw);
    const int y1 = (ay + ah) < (by + bh) ? (ay + ah) : (by + bh);
    *out_w = x1 - x0;
    *out_h = y1 - y0;
    if( *out_w <= 0 || *out_h <= 0 )
    {
        *out_x = 0;
        *out_y = 0;
        *out_w = 0;
        *out_h = 0;
        return;
    }
    *out_x = x0;
    *out_y = y0;
}

static inline void
trspk_logical_rect_to_framebuffer(
    int logical_x,
    int logical_y,
    int logical_w,
    int logical_h,
    int logical_height,
    int lb_x,
    int lb_y,
    int lb_w,
    int lb_h,
    int window_width,
    int window_height,
    int* out_fb_x,
    int* out_fb_y,
    int* out_fb_w,
    int* out_fb_h)
{
    const float sx = (float)lb_w / (float)window_width;
    const float sy = (float)lb_h / (float)window_height;
    *out_fb_x = lb_x + (int)(logical_x * sx);
    *out_fb_w = (int)(logical_w * sx);
    *out_fb_h = (int)(logical_h * sy);
    const int logical_bottom = logical_height - logical_y - logical_h;
    *out_fb_y = lb_y + (int)(logical_bottom * sy);
}

static inline void
trspk_compute_letterbox(
    int logical_width,
    int logical_height,
    int drawable_width,
    int drawable_height,
    struct TRSPK_Letterbox* out)
{
    const float src_aspect = (float)logical_width / (float)logical_height;
    const float win_aspect = (float)drawable_width / (float)drawable_height;

    if( src_aspect > win_aspect )
    {
        out->w = drawable_width;
        out->h = (int)((float)drawable_width / src_aspect);
        out->x = 0;
        out->y = (drawable_height - out->h) / 2;
    }
    else
    {
        out->h = drawable_height;
        out->w = (int)((float)drawable_height * src_aspect);
        out->y = 0;
        out->x = (drawable_width - out->w) / 2;
    }
}

static inline void
trspk_color_rgb_to_rgba(
    int color,
    float alpha,
    float rgba[4])
{
    trspk_color_unpack_argb((uint32_t)color, rgba);
    /* The caller's alpha wins; the packed byte is not consulted. */
    rgba[3] = alpha;
}

static inline void
trspk_color_argb_to_rgba(
    int argb,
    float rgba[4])
{
    if( (argb & 0xFF000000u) == 0u )
        argb |= 0xFF000000u;
    trspk_color_unpack_argb((uint32_t)argb, rgba);
}

/*
 * View + projection for one world pass.
 *
 * cam_* is the camera's world position (not pre-negated).
 *
 * proj_mode / proj_scale / fov_rpi2048 / parallel_zoom16 are the camera's
 * projection knobs, passed as ints so this header stays free of toridraw's —
 * the same reason graphics/projection.h takes ints. Resolution goes through
 * that header, so the GPU backends and the rasterizer cannot disagree about
 * what the camera asked for.
 */
void
trspk_compute_pass_matrices(
    float view[16],
    float proj[16],
    float cam_x,
    float cam_y,
    float cam_z,
    float pitch_rad,
    float yaw_rad,
    int pass_w,
    int pass_h,
    int proj_mode,
    int proj_scale,
    int fov_rpi2048,
    int parallel_zoom16);

#ifdef __cplusplus
}
#endif

#endif
