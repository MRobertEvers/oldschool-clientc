#ifndef TORIDRAW_GRAPHICS_PROJECTION_ORTHO_SSE2_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_ORTHO_SSE2_U_C

#include "projection_ortho.h"

#include "sse2_41compat.h"

/*
 * The SSE2 lane of the parallel projection: four vertices at a time, on the
 * plain-SSE2 floor. Two holes to fill -- there is no pmulld, so the transform's
 * 32-bit multiply is mullo_epi32_sse from sse2_41compat.h, and there is no
 * pblendvb, so the clip families' sentinel goes in through and/andnot/or.
 * See projection_ortho.h for the contract all four hooks keep.
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
        __m128i const c_my = _mm_set1_epi32(cos_model_yaw);
        __m128i const s_my = _mm_set1_epi32(sin_model_yaw);
        __m128i const c_yaw = _mm_set1_epi32(cos_camera_yaw);
        __m128i const s_yaw = _mm_set1_epi32(sin_camera_yaw);
        __m128i const c_pitch = _mm_set1_epi32(cos_camera_pitch);
        __m128i const s_pitch = _mm_set1_epi32(sin_camera_pitch);
        __m128i const v_zoom = _mm_set1_epi32(camera_zoom16);
        __m128i const v_mid = _mm_set1_epi32(model_mid_z);

        for( ; i + 4 <= num_vertices; i += 4 )
        {
            __m128i xv =
                _mm_set_epi32(vertex_x[i + 3], vertex_x[i + 2], vertex_x[i + 1], vertex_x[i]);
            __m128i yv =
                _mm_set_epi32(vertex_y[i + 3], vertex_y[i + 2], vertex_y[i + 1], vertex_y[i]);
            __m128i zv =
                _mm_set_epi32(vertex_z[i + 3], vertex_z[i + 2], vertex_z[i + 1], vertex_z[i]);

            __m128i x_rot = _mm_srai_epi32(
                _mm_add_epi32(mullo_epi32_sse(xv, c_my), mullo_epi32_sse(zv, s_my)), 16);
            __m128i z_rot = _mm_srai_epi32(
                _mm_sub_epi32(mullo_epi32_sse(zv, c_my), mullo_epi32_sse(xv, s_my)), 16);
            x_rot = _mm_add_epi32(x_rot, _mm_set1_epi32(scene_x));
            __m128i y_rot = _mm_add_epi32(yv, _mm_set1_epi32(scene_y));
            z_rot = _mm_add_epi32(z_rot, _mm_set1_epi32(scene_z));

            __m128i x_cam = _mm_srai_epi32(
                _mm_add_epi32(mullo_epi32_sse(x_rot, c_yaw), mullo_epi32_sse(z_rot, s_yaw)), 16);
            __m128i z_cam = _mm_srai_epi32(
                _mm_sub_epi32(mullo_epi32_sse(z_rot, c_yaw), mullo_epi32_sse(x_rot, s_yaw)), 16);
            __m128i y_cam = _mm_srai_epi32(
                _mm_sub_epi32(mullo_epi32_sse(y_rot, c_pitch), mullo_epi32_sse(z_cam, s_pitch)),
                16);
            __m128i z_fin = _mm_srai_epi32(
                _mm_add_epi32(mullo_epi32_sse(y_rot, s_pitch), mullo_epi32_sse(z_cam, c_pitch)),
                16);

            _mm_storeu_si128((__m128i*)&orthographic_vertices_x[i], x_cam);
            _mm_storeu_si128((__m128i*)&orthographic_vertices_y[i], y_cam);
            _mm_storeu_si128((__m128i*)&orthographic_vertices_z[i], z_fin);

            __m128i px = _mm_srai_epi32(mullo_epi32_sse(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m128i py = _mm_srai_epi32(mullo_epi32_sse(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);

            _mm_storeu_si128((__m128i*)&screen_vertices_x[i], px);
            _mm_storeu_si128((__m128i*)&screen_vertices_y[i], py);
            _mm_storeu_si128((__m128i*)&screen_vertices_z[i], _mm_sub_epi32(z_fin, v_mid));
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
        __m128i const c_my = _mm_set1_epi32(cos_model_yaw);
        __m128i const s_my = _mm_set1_epi32(sin_model_yaw);
        __m128i const c_yaw = _mm_set1_epi32(cos_camera_yaw);
        __m128i const s_yaw = _mm_set1_epi32(sin_camera_yaw);
        __m128i const c_pitch = _mm_set1_epi32(cos_camera_pitch);
        __m128i const s_pitch = _mm_set1_epi32(sin_camera_pitch);
        __m128i const v_zoom = _mm_set1_epi32(camera_zoom16);
        __m128i const v_mid = _mm_set1_epi32(model_mid_z);
        __m128i const v_near = _mm_set1_epi32(near_plane_z);
        __m128i const v_sentinel = _mm_set1_epi32(TORIDRAW_SCREEN_X_NEAR_CLIPPED);

        for( ; i + 4 <= num_vertices; i += 4 )
        {
            __m128i xv =
                _mm_set_epi32(vertex_x[i + 3], vertex_x[i + 2], vertex_x[i + 1], vertex_x[i]);
            __m128i yv =
                _mm_set_epi32(vertex_y[i + 3], vertex_y[i + 2], vertex_y[i + 1], vertex_y[i]);
            __m128i zv =
                _mm_set_epi32(vertex_z[i + 3], vertex_z[i + 2], vertex_z[i + 1], vertex_z[i]);

            __m128i x_rot = _mm_srai_epi32(
                _mm_add_epi32(mullo_epi32_sse(xv, c_my), mullo_epi32_sse(zv, s_my)), 16);
            __m128i z_rot = _mm_srai_epi32(
                _mm_sub_epi32(mullo_epi32_sse(zv, c_my), mullo_epi32_sse(xv, s_my)), 16);
            x_rot = _mm_add_epi32(x_rot, _mm_set1_epi32(scene_x));
            __m128i y_rot = _mm_add_epi32(yv, _mm_set1_epi32(scene_y));
            z_rot = _mm_add_epi32(z_rot, _mm_set1_epi32(scene_z));

            __m128i x_cam = _mm_srai_epi32(
                _mm_add_epi32(mullo_epi32_sse(x_rot, c_yaw), mullo_epi32_sse(z_rot, s_yaw)), 16);
            __m128i z_cam = _mm_srai_epi32(
                _mm_sub_epi32(mullo_epi32_sse(z_rot, c_yaw), mullo_epi32_sse(x_rot, s_yaw)), 16);
            __m128i y_cam = _mm_srai_epi32(
                _mm_sub_epi32(mullo_epi32_sse(y_rot, c_pitch), mullo_epi32_sse(z_cam, s_pitch)),
                16);
            __m128i z_fin = _mm_srai_epi32(
                _mm_add_epi32(mullo_epi32_sse(y_rot, s_pitch), mullo_epi32_sse(z_cam, c_pitch)),
                16);

            _mm_storeu_si128((__m128i*)&orthographic_vertices_x[i], x_cam);
            _mm_storeu_si128((__m128i*)&orthographic_vertices_y[i], y_cam);
            _mm_storeu_si128((__m128i*)&orthographic_vertices_z[i], z_fin);

            __m128i px = _mm_srai_epi32(mullo_epi32_sse(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m128i py = _mm_srai_epi32(mullo_epi32_sse(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            { /* and/andnot/or: a blend that is valid on plain SSE2 too. */
                __m128i m = _mm_cmplt_epi32(z_fin, v_near);
                px = _mm_or_si128(_mm_and_si128(m, v_sentinel), _mm_andnot_si128(m, px));
            }

            _mm_storeu_si128((__m128i*)&screen_vertices_x[i], px);
            _mm_storeu_si128((__m128i*)&screen_vertices_y[i], py);
            _mm_storeu_si128((__m128i*)&screen_vertices_z[i], _mm_sub_epi32(z_fin, v_mid));
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
        __m128i const c_my = _mm_set1_epi32(cos_model_yaw);
        __m128i const s_my = _mm_set1_epi32(sin_model_yaw);
        __m128i const c_yaw = _mm_set1_epi32(cos_camera_yaw);
        __m128i const s_yaw = _mm_set1_epi32(sin_camera_yaw);
        __m128i const c_pitch = _mm_set1_epi32(cos_camera_pitch);
        __m128i const s_pitch = _mm_set1_epi32(sin_camera_pitch);
        __m128i const v_zoom = _mm_set1_epi32(camera_zoom16);
        __m128i const v_mid = _mm_set1_epi32(model_mid_z);

        for( ; i + 4 <= num_vertices; i += 4 )
        {
            __m128i xv =
                _mm_set_epi32(vertex_x[i + 3], vertex_x[i + 2], vertex_x[i + 1], vertex_x[i]);
            __m128i yv =
                _mm_set_epi32(vertex_y[i + 3], vertex_y[i + 2], vertex_y[i + 1], vertex_y[i]);
            __m128i zv =
                _mm_set_epi32(vertex_z[i + 3], vertex_z[i + 2], vertex_z[i + 1], vertex_z[i]);

            __m128i x_rot = _mm_srai_epi32(
                _mm_add_epi32(mullo_epi32_sse(xv, c_my), mullo_epi32_sse(zv, s_my)), 16);
            __m128i z_rot = _mm_srai_epi32(
                _mm_sub_epi32(mullo_epi32_sse(zv, c_my), mullo_epi32_sse(xv, s_my)), 16);
            x_rot = _mm_add_epi32(x_rot, _mm_set1_epi32(scene_x));
            __m128i y_rot = _mm_add_epi32(yv, _mm_set1_epi32(scene_y));
            z_rot = _mm_add_epi32(z_rot, _mm_set1_epi32(scene_z));

            __m128i x_cam = _mm_srai_epi32(
                _mm_add_epi32(mullo_epi32_sse(x_rot, c_yaw), mullo_epi32_sse(z_rot, s_yaw)), 16);
            __m128i z_cam = _mm_srai_epi32(
                _mm_sub_epi32(mullo_epi32_sse(z_rot, c_yaw), mullo_epi32_sse(x_rot, s_yaw)), 16);
            __m128i y_cam = _mm_srai_epi32(
                _mm_sub_epi32(mullo_epi32_sse(y_rot, c_pitch), mullo_epi32_sse(z_cam, s_pitch)),
                16);
            __m128i z_fin = _mm_srai_epi32(
                _mm_add_epi32(mullo_epi32_sse(y_rot, s_pitch), mullo_epi32_sse(z_cam, c_pitch)),
                16);

            __m128i px = _mm_srai_epi32(mullo_epi32_sse(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m128i py = _mm_srai_epi32(mullo_epi32_sse(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);

            _mm_storeu_si128((__m128i*)&screen_vertices_x[i], px);
            _mm_storeu_si128((__m128i*)&screen_vertices_y[i], py);
            _mm_storeu_si128((__m128i*)&screen_vertices_z[i], _mm_sub_epi32(z_fin, v_mid));
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
        __m128i const c_my = _mm_set1_epi32(cos_model_yaw);
        __m128i const s_my = _mm_set1_epi32(sin_model_yaw);
        __m128i const c_yaw = _mm_set1_epi32(cos_camera_yaw);
        __m128i const s_yaw = _mm_set1_epi32(sin_camera_yaw);
        __m128i const c_pitch = _mm_set1_epi32(cos_camera_pitch);
        __m128i const s_pitch = _mm_set1_epi32(sin_camera_pitch);
        __m128i const v_zoom = _mm_set1_epi32(camera_zoom16);
        __m128i const v_mid = _mm_set1_epi32(model_mid_z);
        __m128i const v_near = _mm_set1_epi32(near_plane_z);
        __m128i const v_sentinel = _mm_set1_epi32(TORIDRAW_SCREEN_X_NEAR_CLIPPED);

        for( ; i + 4 <= num_vertices; i += 4 )
        {
            __m128i xv =
                _mm_set_epi32(vertex_x[i + 3], vertex_x[i + 2], vertex_x[i + 1], vertex_x[i]);
            __m128i yv =
                _mm_set_epi32(vertex_y[i + 3], vertex_y[i + 2], vertex_y[i + 1], vertex_y[i]);
            __m128i zv =
                _mm_set_epi32(vertex_z[i + 3], vertex_z[i + 2], vertex_z[i + 1], vertex_z[i]);

            __m128i x_rot = _mm_srai_epi32(
                _mm_add_epi32(mullo_epi32_sse(xv, c_my), mullo_epi32_sse(zv, s_my)), 16);
            __m128i z_rot = _mm_srai_epi32(
                _mm_sub_epi32(mullo_epi32_sse(zv, c_my), mullo_epi32_sse(xv, s_my)), 16);
            x_rot = _mm_add_epi32(x_rot, _mm_set1_epi32(scene_x));
            __m128i y_rot = _mm_add_epi32(yv, _mm_set1_epi32(scene_y));
            z_rot = _mm_add_epi32(z_rot, _mm_set1_epi32(scene_z));

            __m128i x_cam = _mm_srai_epi32(
                _mm_add_epi32(mullo_epi32_sse(x_rot, c_yaw), mullo_epi32_sse(z_rot, s_yaw)), 16);
            __m128i z_cam = _mm_srai_epi32(
                _mm_sub_epi32(mullo_epi32_sse(z_rot, c_yaw), mullo_epi32_sse(x_rot, s_yaw)), 16);
            __m128i y_cam = _mm_srai_epi32(
                _mm_sub_epi32(mullo_epi32_sse(y_rot, c_pitch), mullo_epi32_sse(z_cam, s_pitch)),
                16);
            __m128i z_fin = _mm_srai_epi32(
                _mm_add_epi32(mullo_epi32_sse(y_rot, s_pitch), mullo_epi32_sse(z_cam, c_pitch)),
                16);

            __m128i px = _mm_srai_epi32(mullo_epi32_sse(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m128i py = _mm_srai_epi32(mullo_epi32_sse(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            { /* and/andnot/or: a blend that is valid on plain SSE2 too. */
                __m128i m = _mm_cmplt_epi32(z_fin, v_near);
                px = _mm_or_si128(_mm_and_si128(m, v_sentinel), _mm_andnot_si128(m, px));
            }

            _mm_storeu_si128((__m128i*)&screen_vertices_x[i], px);
            _mm_storeu_si128((__m128i*)&screen_vertices_y[i], py);
            _mm_storeu_si128((__m128i*)&screen_vertices_z[i], _mm_sub_epi32(z_fin, v_mid));
        }
    }

    return i;
}

#endif /* TORIDRAW_GRAPHICS_PROJECTION_ORTHO_SSE2_U_C */
