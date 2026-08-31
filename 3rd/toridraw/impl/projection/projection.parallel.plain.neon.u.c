#ifndef TORIDRAW_GRAPHICS_PROJECTION_ORTHO_NEON_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_ORTHO_NEON_U_C

#include "impl/projection/projection.parallel.plain.dispatch.h"

#include <arm_neon.h>

/*
 * The AArch64 lane of the parallel projection: four vertices at a time, the
 * 16-bit model coordinates widened in place by vmovl_s16 off a half-vector
 * load. See projection_ortho.h for the contract all four hooks keep.
 */

/** Camera-space output, no near-plane test -- the cheapest of the four. */
static inline int
toridraw_ortho_lane_fused(
    const struct ToriDraw_OrthoFusedCamera* cam,
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices)
{
    int const cos_model_yaw = cam->cos_model_yaw;
    int const sin_model_yaw = cam->sin_model_yaw;
    int const cos_camera_yaw = cam->cos_camera_yaw;
    int const sin_camera_yaw = cam->sin_camera_yaw;
    int const cos_camera_pitch = cam->cos_camera_pitch;
    int const sin_camera_pitch = cam->sin_camera_pitch;
    int const scene_x = cam->scene_x;
    int const scene_y = cam->scene_y;
    int const scene_z = cam->scene_z;
    int const camera_zoom16 = cam->camera_zoom16;
    int const model_mid_z = cam->model_mid_z;
    int i = 0;

    {
        int32x4_t const c_my = vdupq_n_s32(cos_model_yaw);
        int32x4_t const s_my = vdupq_n_s32(sin_model_yaw);
        int32x4_t const c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t const s_yaw = vdupq_n_s32(sin_camera_yaw);
        int32x4_t const c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t const s_pitch = vdupq_n_s32(sin_camera_pitch);
        int32x4_t const v_zoom = vdupq_n_s32(camera_zoom16);
        int32x4_t const v_mid = vdupq_n_s32(model_mid_z);
        int32x4_t const v_sx = vdupq_n_s32(scene_x);
        int32x4_t const v_sy = vdupq_n_s32(scene_y);
        int32x4_t const v_sz = vdupq_n_s32(scene_z);

        for( ; i + 4 <= num_vertices; i += 4 )
        {
            int32x4_t xv = vmovl_s16(vld1_s16(&vertex_x[i]));
            int32x4_t yv = vmovl_s16(vld1_s16(&vertex_y[i]));
            int32x4_t zv = vmovl_s16(vld1_s16(&vertex_z[i]));

            int32x4_t x_rot = vshrq_n_s32(vaddq_s32(vmulq_s32(xv, c_my), vmulq_s32(zv, s_my)), 16);
            int32x4_t z_rot = vshrq_n_s32(vsubq_s32(vmulq_s32(zv, c_my), vmulq_s32(xv, s_my)), 16);
            x_rot = vaddq_s32(x_rot, v_sx);
            int32x4_t y_rot = vaddq_s32(yv, v_sy);
            z_rot = vaddq_s32(z_rot, v_sz);

            int32x4_t x_cam =
                vshrq_n_s32(vaddq_s32(vmulq_s32(x_rot, c_yaw), vmulq_s32(z_rot, s_yaw)), 16);
            int32x4_t z_cam =
                vshrq_n_s32(vsubq_s32(vmulq_s32(z_rot, c_yaw), vmulq_s32(x_rot, s_yaw)), 16);
            int32x4_t y_cam =
                vshrq_n_s32(vsubq_s32(vmulq_s32(y_rot, c_pitch), vmulq_s32(z_cam, s_pitch)), 16);
            int32x4_t z_fin =
                vshrq_n_s32(vaddq_s32(vmulq_s32(y_rot, s_pitch), vmulq_s32(z_cam, c_pitch)), 16);

            vst1q_s32(&orthographic_vertices_x[i], x_cam);
            vst1q_s32(&orthographic_vertices_y[i], y_cam);
            vst1q_s32(&orthographic_vertices_z[i], z_fin);

            int32x4_t px = vshrq_n_s32(vmulq_s32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            int32x4_t py = vshrq_n_s32(vmulq_s32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);

            vst1q_s32(&screen_vertices_x[i], px);
            vst1q_s32(&screen_vertices_y[i], py);
            vst1q_s32(&screen_vertices_z[i], vsubq_s32(z_fin, v_mid));
        }
    }

    return i;
}

/** Camera-space output; marks a vertex behind near_plane_z for the face test to drop. */
static inline int
toridraw_ortho_lane_fused_clip(
    const struct ToriDraw_OrthoFusedCamera* cam,
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices)
{
    int const cos_model_yaw = cam->cos_model_yaw;
    int const sin_model_yaw = cam->sin_model_yaw;
    int const cos_camera_yaw = cam->cos_camera_yaw;
    int const sin_camera_yaw = cam->sin_camera_yaw;
    int const cos_camera_pitch = cam->cos_camera_pitch;
    int const sin_camera_pitch = cam->sin_camera_pitch;
    int const scene_x = cam->scene_x;
    int const scene_y = cam->scene_y;
    int const scene_z = cam->scene_z;
    int const camera_zoom16 = cam->camera_zoom16;
    int const model_mid_z = cam->model_mid_z;
    int const near_plane_z = cam->near_plane_z;
    int i = 0;

    {
        int32x4_t const c_my = vdupq_n_s32(cos_model_yaw);
        int32x4_t const s_my = vdupq_n_s32(sin_model_yaw);
        int32x4_t const c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t const s_yaw = vdupq_n_s32(sin_camera_yaw);
        int32x4_t const c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t const s_pitch = vdupq_n_s32(sin_camera_pitch);
        int32x4_t const v_zoom = vdupq_n_s32(camera_zoom16);
        int32x4_t const v_mid = vdupq_n_s32(model_mid_z);
        int32x4_t const v_sx = vdupq_n_s32(scene_x);
        int32x4_t const v_sy = vdupq_n_s32(scene_y);
        int32x4_t const v_sz = vdupq_n_s32(scene_z);
        int32x4_t const v_near = vdupq_n_s32(near_plane_z);
        int32x4_t const v_sentinel = vdupq_n_s32(TORIDRAW_SCREEN_X_NEAR_CLIPPED);

        for( ; i + 4 <= num_vertices; i += 4 )
        {
            int32x4_t xv = vmovl_s16(vld1_s16(&vertex_x[i]));
            int32x4_t yv = vmovl_s16(vld1_s16(&vertex_y[i]));
            int32x4_t zv = vmovl_s16(vld1_s16(&vertex_z[i]));

            int32x4_t x_rot = vshrq_n_s32(vaddq_s32(vmulq_s32(xv, c_my), vmulq_s32(zv, s_my)), 16);
            int32x4_t z_rot = vshrq_n_s32(vsubq_s32(vmulq_s32(zv, c_my), vmulq_s32(xv, s_my)), 16);
            x_rot = vaddq_s32(x_rot, v_sx);
            int32x4_t y_rot = vaddq_s32(yv, v_sy);
            z_rot = vaddq_s32(z_rot, v_sz);

            int32x4_t x_cam =
                vshrq_n_s32(vaddq_s32(vmulq_s32(x_rot, c_yaw), vmulq_s32(z_rot, s_yaw)), 16);
            int32x4_t z_cam =
                vshrq_n_s32(vsubq_s32(vmulq_s32(z_rot, c_yaw), vmulq_s32(x_rot, s_yaw)), 16);
            int32x4_t y_cam =
                vshrq_n_s32(vsubq_s32(vmulq_s32(y_rot, c_pitch), vmulq_s32(z_cam, s_pitch)), 16);
            int32x4_t z_fin =
                vshrq_n_s32(vaddq_s32(vmulq_s32(y_rot, s_pitch), vmulq_s32(z_cam, c_pitch)), 16);

            vst1q_s32(&orthographic_vertices_x[i], x_cam);
            vst1q_s32(&orthographic_vertices_y[i], y_cam);
            vst1q_s32(&orthographic_vertices_z[i], z_fin);

            int32x4_t px = vshrq_n_s32(vmulq_s32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            int32x4_t py = vshrq_n_s32(vmulq_s32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            px = vbslq_s32(vcltq_s32(z_fin, v_near), v_sentinel, px);

            vst1q_s32(&screen_vertices_x[i], px);
            vst1q_s32(&screen_vertices_y[i], py);
            vst1q_s32(&screen_vertices_z[i], vsubq_s32(z_fin, v_mid));
        }
    }

    return i;
}

/** Screen output only, no near-plane test. */
static inline int
toridraw_ortho_lane_fused_notex(
    const struct ToriDraw_OrthoFusedCamera* cam,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices)
{
    int const cos_model_yaw = cam->cos_model_yaw;
    int const sin_model_yaw = cam->sin_model_yaw;
    int const cos_camera_yaw = cam->cos_camera_yaw;
    int const sin_camera_yaw = cam->sin_camera_yaw;
    int const cos_camera_pitch = cam->cos_camera_pitch;
    int const sin_camera_pitch = cam->sin_camera_pitch;
    int const scene_x = cam->scene_x;
    int const scene_y = cam->scene_y;
    int const scene_z = cam->scene_z;
    int const camera_zoom16 = cam->camera_zoom16;
    int const model_mid_z = cam->model_mid_z;
    int i = 0;

    {
        int32x4_t const c_my = vdupq_n_s32(cos_model_yaw);
        int32x4_t const s_my = vdupq_n_s32(sin_model_yaw);
        int32x4_t const c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t const s_yaw = vdupq_n_s32(sin_camera_yaw);
        int32x4_t const c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t const s_pitch = vdupq_n_s32(sin_camera_pitch);
        int32x4_t const v_zoom = vdupq_n_s32(camera_zoom16);
        int32x4_t const v_mid = vdupq_n_s32(model_mid_z);
        int32x4_t const v_sx = vdupq_n_s32(scene_x);
        int32x4_t const v_sy = vdupq_n_s32(scene_y);
        int32x4_t const v_sz = vdupq_n_s32(scene_z);

        for( ; i + 4 <= num_vertices; i += 4 )
        {
            int32x4_t xv = vmovl_s16(vld1_s16(&vertex_x[i]));
            int32x4_t yv = vmovl_s16(vld1_s16(&vertex_y[i]));
            int32x4_t zv = vmovl_s16(vld1_s16(&vertex_z[i]));

            int32x4_t x_rot = vshrq_n_s32(vaddq_s32(vmulq_s32(xv, c_my), vmulq_s32(zv, s_my)), 16);
            int32x4_t z_rot = vshrq_n_s32(vsubq_s32(vmulq_s32(zv, c_my), vmulq_s32(xv, s_my)), 16);
            x_rot = vaddq_s32(x_rot, v_sx);
            int32x4_t y_rot = vaddq_s32(yv, v_sy);
            z_rot = vaddq_s32(z_rot, v_sz);

            int32x4_t x_cam =
                vshrq_n_s32(vaddq_s32(vmulq_s32(x_rot, c_yaw), vmulq_s32(z_rot, s_yaw)), 16);
            int32x4_t z_cam =
                vshrq_n_s32(vsubq_s32(vmulq_s32(z_rot, c_yaw), vmulq_s32(x_rot, s_yaw)), 16);
            int32x4_t y_cam =
                vshrq_n_s32(vsubq_s32(vmulq_s32(y_rot, c_pitch), vmulq_s32(z_cam, s_pitch)), 16);
            int32x4_t z_fin =
                vshrq_n_s32(vaddq_s32(vmulq_s32(y_rot, s_pitch), vmulq_s32(z_cam, c_pitch)), 16);

            int32x4_t px = vshrq_n_s32(vmulq_s32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            int32x4_t py = vshrq_n_s32(vmulq_s32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);

            vst1q_s32(&screen_vertices_x[i], px);
            vst1q_s32(&screen_vertices_y[i], py);
            vst1q_s32(&screen_vertices_z[i], vsubq_s32(z_fin, v_mid));
        }
    }

    return i;
}

/** Screen output only; marks a vertex behind near_plane_z for the face test to drop. */
static inline int
toridraw_ortho_lane_fused_notex_clip(
    const struct ToriDraw_OrthoFusedCamera* cam,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices)
{
    int const cos_model_yaw = cam->cos_model_yaw;
    int const sin_model_yaw = cam->sin_model_yaw;
    int const cos_camera_yaw = cam->cos_camera_yaw;
    int const sin_camera_yaw = cam->sin_camera_yaw;
    int const cos_camera_pitch = cam->cos_camera_pitch;
    int const sin_camera_pitch = cam->sin_camera_pitch;
    int const scene_x = cam->scene_x;
    int const scene_y = cam->scene_y;
    int const scene_z = cam->scene_z;
    int const camera_zoom16 = cam->camera_zoom16;
    int const model_mid_z = cam->model_mid_z;
    int const near_plane_z = cam->near_plane_z;
    int i = 0;

    {
        int32x4_t const c_my = vdupq_n_s32(cos_model_yaw);
        int32x4_t const s_my = vdupq_n_s32(sin_model_yaw);
        int32x4_t const c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t const s_yaw = vdupq_n_s32(sin_camera_yaw);
        int32x4_t const c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t const s_pitch = vdupq_n_s32(sin_camera_pitch);
        int32x4_t const v_zoom = vdupq_n_s32(camera_zoom16);
        int32x4_t const v_mid = vdupq_n_s32(model_mid_z);
        int32x4_t const v_sx = vdupq_n_s32(scene_x);
        int32x4_t const v_sy = vdupq_n_s32(scene_y);
        int32x4_t const v_sz = vdupq_n_s32(scene_z);
        int32x4_t const v_near = vdupq_n_s32(near_plane_z);
        int32x4_t const v_sentinel = vdupq_n_s32(TORIDRAW_SCREEN_X_NEAR_CLIPPED);

        for( ; i + 4 <= num_vertices; i += 4 )
        {
            int32x4_t xv = vmovl_s16(vld1_s16(&vertex_x[i]));
            int32x4_t yv = vmovl_s16(vld1_s16(&vertex_y[i]));
            int32x4_t zv = vmovl_s16(vld1_s16(&vertex_z[i]));

            int32x4_t x_rot = vshrq_n_s32(vaddq_s32(vmulq_s32(xv, c_my), vmulq_s32(zv, s_my)), 16);
            int32x4_t z_rot = vshrq_n_s32(vsubq_s32(vmulq_s32(zv, c_my), vmulq_s32(xv, s_my)), 16);
            x_rot = vaddq_s32(x_rot, v_sx);
            int32x4_t y_rot = vaddq_s32(yv, v_sy);
            z_rot = vaddq_s32(z_rot, v_sz);

            int32x4_t x_cam =
                vshrq_n_s32(vaddq_s32(vmulq_s32(x_rot, c_yaw), vmulq_s32(z_rot, s_yaw)), 16);
            int32x4_t z_cam =
                vshrq_n_s32(vsubq_s32(vmulq_s32(z_rot, c_yaw), vmulq_s32(x_rot, s_yaw)), 16);
            int32x4_t y_cam =
                vshrq_n_s32(vsubq_s32(vmulq_s32(y_rot, c_pitch), vmulq_s32(z_cam, s_pitch)), 16);
            int32x4_t z_fin =
                vshrq_n_s32(vaddq_s32(vmulq_s32(y_rot, s_pitch), vmulq_s32(z_cam, c_pitch)), 16);

            int32x4_t px = vshrq_n_s32(vmulq_s32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            int32x4_t py = vshrq_n_s32(vmulq_s32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            px = vbslq_s32(vcltq_s32(z_fin, v_near), v_sentinel, px);

            vst1q_s32(&screen_vertices_x[i], px);
            vst1q_s32(&screen_vertices_y[i], py);
            vst1q_s32(&screen_vertices_z[i], vsubq_s32(z_fin, v_mid));
        }
    }

    return i;
}

#endif /* TORIDRAW_GRAPHICS_PROJECTION_ORTHO_NEON_U_C */
