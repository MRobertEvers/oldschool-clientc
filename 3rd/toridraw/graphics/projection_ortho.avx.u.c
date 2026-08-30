#ifndef TORIDRAW_GRAPHICS_PROJECTION_ORTHO_AVX_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_ORTHO_AVX_U_C

#include "projection_ortho.h"

#include <immintrin.h>

/*
 * The AVX2 lane of the parallel projection: eight vertices at a time, with
 * vpmovsxwd for the widening load, vpmulld for the 32-bit multiply the
 * transform is built out of, and vpblendvb for the clip families' sentinel.
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
        __m256i const c_my = _mm256_set1_epi32(cos_model_yaw);
        __m256i const s_my = _mm256_set1_epi32(sin_model_yaw);
        __m256i const c_yaw = _mm256_set1_epi32(cos_camera_yaw);
        __m256i const s_yaw = _mm256_set1_epi32(sin_camera_yaw);
        __m256i const c_pitch = _mm256_set1_epi32(cos_camera_pitch);
        __m256i const s_pitch = _mm256_set1_epi32(sin_camera_pitch);
        __m256i const v_zoom = _mm256_set1_epi32(camera_zoom16);
        __m256i const v_mid = _mm256_set1_epi32(model_mid_z);

        for( ; i + 8 <= num_vertices; i += 8 )
        {
            __m256i xv = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i const*)&vertex_x[i]));
            __m256i yv = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i const*)&vertex_y[i]));
            __m256i zv = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i const*)&vertex_z[i]));

            __m256i x_rot = _mm256_srai_epi32(
                _mm256_add_epi32(_mm256_mullo_epi32(xv, c_my), _mm256_mullo_epi32(zv, s_my)), 16);
            __m256i z_rot = _mm256_srai_epi32(
                _mm256_sub_epi32(_mm256_mullo_epi32(zv, c_my), _mm256_mullo_epi32(xv, s_my)), 16);
            x_rot = _mm256_add_epi32(x_rot, _mm256_set1_epi32(scene_x));
            __m256i y_rot = _mm256_add_epi32(yv, _mm256_set1_epi32(scene_y));
            z_rot = _mm256_add_epi32(z_rot, _mm256_set1_epi32(scene_z));

            __m256i x_cam = _mm256_srai_epi32(
                _mm256_add_epi32(
                    _mm256_mullo_epi32(x_rot, c_yaw), _mm256_mullo_epi32(z_rot, s_yaw)),
                16);
            __m256i z_cam = _mm256_srai_epi32(
                _mm256_sub_epi32(
                    _mm256_mullo_epi32(z_rot, c_yaw), _mm256_mullo_epi32(x_rot, s_yaw)),
                16);
            __m256i y_cam = _mm256_srai_epi32(
                _mm256_sub_epi32(
                    _mm256_mullo_epi32(y_rot, c_pitch), _mm256_mullo_epi32(z_cam, s_pitch)),
                16);
            __m256i z_fin = _mm256_srai_epi32(
                _mm256_add_epi32(
                    _mm256_mullo_epi32(y_rot, s_pitch), _mm256_mullo_epi32(z_cam, c_pitch)),
                16);

            _mm256_storeu_si256((__m256i*)&orthographic_vertices_x[i], x_cam);
            _mm256_storeu_si256((__m256i*)&orthographic_vertices_y[i], y_cam);
            _mm256_storeu_si256((__m256i*)&orthographic_vertices_z[i], z_fin);

            __m256i px =
                _mm256_srai_epi32(_mm256_mullo_epi32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m256i py =
                _mm256_srai_epi32(_mm256_mullo_epi32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);

            _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], px);
            _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], py);
            _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], _mm256_sub_epi32(z_fin, v_mid));
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
        __m256i const c_my = _mm256_set1_epi32(cos_model_yaw);
        __m256i const s_my = _mm256_set1_epi32(sin_model_yaw);
        __m256i const c_yaw = _mm256_set1_epi32(cos_camera_yaw);
        __m256i const s_yaw = _mm256_set1_epi32(sin_camera_yaw);
        __m256i const c_pitch = _mm256_set1_epi32(cos_camera_pitch);
        __m256i const s_pitch = _mm256_set1_epi32(sin_camera_pitch);
        __m256i const v_zoom = _mm256_set1_epi32(camera_zoom16);
        __m256i const v_mid = _mm256_set1_epi32(model_mid_z);
        __m256i const v_near = _mm256_set1_epi32(near_plane_z);
        __m256i const v_sentinel = _mm256_set1_epi32(TORIDRAW_SCREEN_X_NEAR_CLIPPED);

        for( ; i + 8 <= num_vertices; i += 8 )
        {
            __m256i xv = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i const*)&vertex_x[i]));
            __m256i yv = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i const*)&vertex_y[i]));
            __m256i zv = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i const*)&vertex_z[i]));

            __m256i x_rot = _mm256_srai_epi32(
                _mm256_add_epi32(_mm256_mullo_epi32(xv, c_my), _mm256_mullo_epi32(zv, s_my)), 16);
            __m256i z_rot = _mm256_srai_epi32(
                _mm256_sub_epi32(_mm256_mullo_epi32(zv, c_my), _mm256_mullo_epi32(xv, s_my)), 16);
            x_rot = _mm256_add_epi32(x_rot, _mm256_set1_epi32(scene_x));
            __m256i y_rot = _mm256_add_epi32(yv, _mm256_set1_epi32(scene_y));
            z_rot = _mm256_add_epi32(z_rot, _mm256_set1_epi32(scene_z));

            __m256i x_cam = _mm256_srai_epi32(
                _mm256_add_epi32(
                    _mm256_mullo_epi32(x_rot, c_yaw), _mm256_mullo_epi32(z_rot, s_yaw)),
                16);
            __m256i z_cam = _mm256_srai_epi32(
                _mm256_sub_epi32(
                    _mm256_mullo_epi32(z_rot, c_yaw), _mm256_mullo_epi32(x_rot, s_yaw)),
                16);
            __m256i y_cam = _mm256_srai_epi32(
                _mm256_sub_epi32(
                    _mm256_mullo_epi32(y_rot, c_pitch), _mm256_mullo_epi32(z_cam, s_pitch)),
                16);
            __m256i z_fin = _mm256_srai_epi32(
                _mm256_add_epi32(
                    _mm256_mullo_epi32(y_rot, s_pitch), _mm256_mullo_epi32(z_cam, c_pitch)),
                16);

            _mm256_storeu_si256((__m256i*)&orthographic_vertices_x[i], x_cam);
            _mm256_storeu_si256((__m256i*)&orthographic_vertices_y[i], y_cam);
            _mm256_storeu_si256((__m256i*)&orthographic_vertices_z[i], z_fin);

            __m256i px =
                _mm256_srai_epi32(_mm256_mullo_epi32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m256i py =
                _mm256_srai_epi32(_mm256_mullo_epi32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            px = _mm256_blendv_epi8(px, v_sentinel, _mm256_cmpgt_epi32(v_near, z_fin));

            _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], px);
            _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], py);
            _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], _mm256_sub_epi32(z_fin, v_mid));
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
        __m256i const c_my = _mm256_set1_epi32(cos_model_yaw);
        __m256i const s_my = _mm256_set1_epi32(sin_model_yaw);
        __m256i const c_yaw = _mm256_set1_epi32(cos_camera_yaw);
        __m256i const s_yaw = _mm256_set1_epi32(sin_camera_yaw);
        __m256i const c_pitch = _mm256_set1_epi32(cos_camera_pitch);
        __m256i const s_pitch = _mm256_set1_epi32(sin_camera_pitch);
        __m256i const v_zoom = _mm256_set1_epi32(camera_zoom16);
        __m256i const v_mid = _mm256_set1_epi32(model_mid_z);

        for( ; i + 8 <= num_vertices; i += 8 )
        {
            __m256i xv = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i const*)&vertex_x[i]));
            __m256i yv = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i const*)&vertex_y[i]));
            __m256i zv = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i const*)&vertex_z[i]));

            __m256i x_rot = _mm256_srai_epi32(
                _mm256_add_epi32(_mm256_mullo_epi32(xv, c_my), _mm256_mullo_epi32(zv, s_my)), 16);
            __m256i z_rot = _mm256_srai_epi32(
                _mm256_sub_epi32(_mm256_mullo_epi32(zv, c_my), _mm256_mullo_epi32(xv, s_my)), 16);
            x_rot = _mm256_add_epi32(x_rot, _mm256_set1_epi32(scene_x));
            __m256i y_rot = _mm256_add_epi32(yv, _mm256_set1_epi32(scene_y));
            z_rot = _mm256_add_epi32(z_rot, _mm256_set1_epi32(scene_z));

            __m256i x_cam = _mm256_srai_epi32(
                _mm256_add_epi32(
                    _mm256_mullo_epi32(x_rot, c_yaw), _mm256_mullo_epi32(z_rot, s_yaw)),
                16);
            __m256i z_cam = _mm256_srai_epi32(
                _mm256_sub_epi32(
                    _mm256_mullo_epi32(z_rot, c_yaw), _mm256_mullo_epi32(x_rot, s_yaw)),
                16);
            __m256i y_cam = _mm256_srai_epi32(
                _mm256_sub_epi32(
                    _mm256_mullo_epi32(y_rot, c_pitch), _mm256_mullo_epi32(z_cam, s_pitch)),
                16);
            __m256i z_fin = _mm256_srai_epi32(
                _mm256_add_epi32(
                    _mm256_mullo_epi32(y_rot, s_pitch), _mm256_mullo_epi32(z_cam, c_pitch)),
                16);

            __m256i px =
                _mm256_srai_epi32(_mm256_mullo_epi32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m256i py =
                _mm256_srai_epi32(_mm256_mullo_epi32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);

            _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], px);
            _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], py);
            _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], _mm256_sub_epi32(z_fin, v_mid));
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
        __m256i const c_my = _mm256_set1_epi32(cos_model_yaw);
        __m256i const s_my = _mm256_set1_epi32(sin_model_yaw);
        __m256i const c_yaw = _mm256_set1_epi32(cos_camera_yaw);
        __m256i const s_yaw = _mm256_set1_epi32(sin_camera_yaw);
        __m256i const c_pitch = _mm256_set1_epi32(cos_camera_pitch);
        __m256i const s_pitch = _mm256_set1_epi32(sin_camera_pitch);
        __m256i const v_zoom = _mm256_set1_epi32(camera_zoom16);
        __m256i const v_mid = _mm256_set1_epi32(model_mid_z);
        __m256i const v_near = _mm256_set1_epi32(near_plane_z);
        __m256i const v_sentinel = _mm256_set1_epi32(TORIDRAW_SCREEN_X_NEAR_CLIPPED);

        for( ; i + 8 <= num_vertices; i += 8 )
        {
            __m256i xv = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i const*)&vertex_x[i]));
            __m256i yv = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i const*)&vertex_y[i]));
            __m256i zv = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i const*)&vertex_z[i]));

            __m256i x_rot = _mm256_srai_epi32(
                _mm256_add_epi32(_mm256_mullo_epi32(xv, c_my), _mm256_mullo_epi32(zv, s_my)), 16);
            __m256i z_rot = _mm256_srai_epi32(
                _mm256_sub_epi32(_mm256_mullo_epi32(zv, c_my), _mm256_mullo_epi32(xv, s_my)), 16);
            x_rot = _mm256_add_epi32(x_rot, _mm256_set1_epi32(scene_x));
            __m256i y_rot = _mm256_add_epi32(yv, _mm256_set1_epi32(scene_y));
            z_rot = _mm256_add_epi32(z_rot, _mm256_set1_epi32(scene_z));

            __m256i x_cam = _mm256_srai_epi32(
                _mm256_add_epi32(
                    _mm256_mullo_epi32(x_rot, c_yaw), _mm256_mullo_epi32(z_rot, s_yaw)),
                16);
            __m256i z_cam = _mm256_srai_epi32(
                _mm256_sub_epi32(
                    _mm256_mullo_epi32(z_rot, c_yaw), _mm256_mullo_epi32(x_rot, s_yaw)),
                16);
            __m256i y_cam = _mm256_srai_epi32(
                _mm256_sub_epi32(
                    _mm256_mullo_epi32(y_rot, c_pitch), _mm256_mullo_epi32(z_cam, s_pitch)),
                16);
            __m256i z_fin = _mm256_srai_epi32(
                _mm256_add_epi32(
                    _mm256_mullo_epi32(y_rot, s_pitch), _mm256_mullo_epi32(z_cam, c_pitch)),
                16);

            __m256i px =
                _mm256_srai_epi32(_mm256_mullo_epi32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m256i py =
                _mm256_srai_epi32(_mm256_mullo_epi32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            px = _mm256_blendv_epi8(px, v_sentinel, _mm256_cmpgt_epi32(v_near, z_fin));

            _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], px);
            _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], py);
            _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], _mm256_sub_epi32(z_fin, v_mid));
        }
    }

    return i;
}

#endif /* TORIDRAW_GRAPHICS_PROJECTION_ORTHO_AVX_U_C */
