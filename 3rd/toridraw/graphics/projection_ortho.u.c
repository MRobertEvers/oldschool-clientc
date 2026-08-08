#ifndef PROJECTION_ORTHO_U_C
#define PROJECTION_ORTHO_U_C

#include "dash_restrict.h"
#include "dash_vertexint.h"
#include "projection.h"

#include <stdbool.h>

/*
 * Parallel (orthographic) projection, for the map editor.
 *
 * The perspective kernels spend their money on one thing: the per-vertex
 * divide. `screen = coord * scale / z` needs a real division per axis, and no
 * target here has an integer SIMD divide, so the vector paths detour through
 * float -- convert z to float, reciprocal-estimate, one Newton-Raphson step,
 * two multiplies, convert back with truncation. That round trip, not the
 * transform, is the expensive part of projecting a vertex.
 *
 * A parallel projection has no eye point, so there is nothing to divide by:
 *
 *     screen_x = (x_camera * zoom16) >> 16
 *     screen_y = (y_camera * zoom16) >> 16
 *
 * One multiply and one shift per axis, in integer lanes that vectorize as wide
 * as the machine allows. The transform, and the depth value the painter sorts
 * on, are unchanged -- so these drop into the existing pipeline as an
 * alternative kernel choice, leaving the perspective kernels untouched.
 *
 * ZOOM. `camera_zoom16` is 16.16 fixed point: 65536 draws one world unit as one
 * pixel, 131072 is 2x, 32768 is half. Continuous rather than stepped, because a
 * multiply costs the same at any value -- there is no reason to restrict a map
 * editor to power-of-two zoom.
 *
 * FOUR KERNELS, WRITTEN OUT. tex/notex and clip/noclip are separate functions
 * rather than one kernel taking flags, matching the perspective families: the
 * choice is a property of the model and the camera, made once by the caller, so
 * nothing below should be re-testing it per vertex. A `bool want_camera_space`
 * or `bool may_clip` parameter would sit in the innermost loop and, worse, stop
 * being a constant at lower optimization levels.
 *
 * WHAT CLIPPING MEANS HERE. With no divide there is no singularity -- a vertex
 * behind the camera projects to a perfectly ordinary coordinate, which is why
 * the *_noclip kernels are the natural default and are strictly cheaper. The
 * *_clip kernels exist for the case a map editor actually hits: a camera placed
 * inside or beneath geometry, where everything behind the view plane would
 * otherwise still be drawn. They mark such vertices with
 * TORIDRAW_SCREEN_X_NEAR_CLIPPED so the existing per-face test rejects the face.
 *
 * CRITICAL: mark-to-DROP, not mark-to-rebuild. The perspective near-clip
 * polygon builder reconstructs a clipped triangle through
 * ToriDraw_TriangleLerpPlaneProjecti, which ends in `SCALE_UNIT(p) /
 * near_plane_z` -- a perspective divide. That is wrong for a parallel
 * projection and would place the rebuilt vertices incorrectly. A caller using
 * the *_clip kernels must therefore leave allow_near_clip false so marked faces
 * are skipped outright. A parallel-correct rebuild would only need a lerp with
 * no divide at all, but that is a triangle-builder change, not a kernel one.
 *
 * SENTINEL Y. The perspective kernels leave a clipped vertex's screen y
 * *undivided*, because dividing by that z is the unsafe operation. Nothing is
 * unsafe here, so these store the correctly projected y and only screen x
 * carries the marker. Consumers reject the whole face on x, so they never read
 * the pair either way.
 *
 * ROUNDING. `>> 16` floors; the perspective path's `/ z` truncates toward zero.
 * Flooring is the better behaviour here -- truncation is discontinuous across
 * x == 0, which makes geometry twitch by a pixel as it crosses the origin under
 * a slow pan. Not bug-compatible with the perspective kernels, deliberately.
 *
 * DOMAIN. The multiply is 32x32->32, so |x_camera * zoom16| must fit in an
 * int32: at 1:1 that is |x_camera| < 32768 world units, and the bound tightens
 * as you zoom in. Anything the AABB cull admits is on screen and comfortably
 * inside it. This is the same assumption the perspective kernels already make
 * for `x * cot15`.
 */

/* TORIDRAW_ORTHO_ZOOM_SHIFT / _UNIT are in projection.h: callers set
 * ToriDraw_Camera.parallel_zoom16, so the unit is public API. */

#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include <arm_neon.h>
#define TORIDRAW_ORTHO_SIMD_NEON 1
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include <immintrin.h>
#define TORIDRAW_ORTHO_SIMD_AVX2 1
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "sse2_41compat.h"
#define TORIDRAW_ORTHO_SIMD_SSE 1
#endif

/** One vertex to camera space. Shared by every scalar tail and the no-SIMD build. */
static inline void
toridraw_ortho_to_camera_space(
    int vx, int vy, int vz,
    int cos_model_yaw, int sin_model_yaw,
    int scene_x, int scene_y, int scene_z,
    int cos_camera_yaw, int sin_camera_yaw,
    int cos_camera_pitch, int sin_camera_pitch,
    int* out_x, int* out_y, int* out_z)
{
    int x_rotated = (vx * cos_model_yaw + vz * sin_model_yaw) >> 16;
    int z_rotated = (vz * cos_model_yaw - vx * sin_model_yaw) >> 16;

    x_rotated += scene_x;
    int y_rotated = vy + scene_y;
    z_rotated += scene_z;

    int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
    int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;

    *out_x = x_scene;
    *out_y = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
    *out_z = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;
}

/** With camera-space output; no near-plane test at all -- the cheapest kernel. */
static inline void
project_vertices_array_ortho_fused_noclip(
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw)
{
    int const cos_camera_pitch = RSCacheDat2A_NoiseCosTable[camera_pitch];
    int const sin_camera_pitch = g_sin_table[camera_pitch];
    int const cos_camera_yaw = RSCacheDat2A_NoiseCosTable[camera_yaw];
    int const sin_camera_yaw = g_sin_table[camera_yaw];
    int const cos_model_yaw = RSCacheDat2A_NoiseCosTable[model_yaw];
    int const sin_model_yaw = g_sin_table[model_yaw];

    int i = 0;

#if defined(TORIDRAW_ORTHO_SIMD_NEON)
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

            int32x4_t x_cam = vshrq_n_s32(vaddq_s32(vmulq_s32(x_rot, c_yaw), vmulq_s32(z_rot, s_yaw)), 16);
            int32x4_t z_cam = vshrq_n_s32(vsubq_s32(vmulq_s32(z_rot, c_yaw), vmulq_s32(x_rot, s_yaw)), 16);
            int32x4_t y_cam = vshrq_n_s32(vsubq_s32(vmulq_s32(y_rot, c_pitch), vmulq_s32(z_cam, s_pitch)), 16);
            int32x4_t z_fin = vshrq_n_s32(vaddq_s32(vmulq_s32(y_rot, s_pitch), vmulq_s32(z_cam, c_pitch)), 16);

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
#elif defined(TORIDRAW_ORTHO_SIMD_AVX2)
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

            __m256i x_rot = _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(xv, c_my), _mm256_mullo_epi32(zv, s_my)), 16);
            __m256i z_rot = _mm256_srai_epi32(_mm256_sub_epi32(_mm256_mullo_epi32(zv, c_my), _mm256_mullo_epi32(xv, s_my)), 16);
            x_rot = _mm256_add_epi32(x_rot, _mm256_set1_epi32(scene_x));
            __m256i y_rot = _mm256_add_epi32(yv, _mm256_set1_epi32(scene_y));
            z_rot = _mm256_add_epi32(z_rot, _mm256_set1_epi32(scene_z));

            __m256i x_cam = _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(x_rot, c_yaw), _mm256_mullo_epi32(z_rot, s_yaw)), 16);
            __m256i z_cam = _mm256_srai_epi32(_mm256_sub_epi32(_mm256_mullo_epi32(z_rot, c_yaw), _mm256_mullo_epi32(x_rot, s_yaw)), 16);
            __m256i y_cam = _mm256_srai_epi32(_mm256_sub_epi32(_mm256_mullo_epi32(y_rot, c_pitch), _mm256_mullo_epi32(z_cam, s_pitch)), 16);
            __m256i z_fin = _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(y_rot, s_pitch), _mm256_mullo_epi32(z_cam, c_pitch)), 16);

            _mm256_storeu_si256((__m256i*)&orthographic_vertices_x[i], x_cam);
            _mm256_storeu_si256((__m256i*)&orthographic_vertices_y[i], y_cam);
            _mm256_storeu_si256((__m256i*)&orthographic_vertices_z[i], z_fin);

            __m256i px = _mm256_srai_epi32(_mm256_mullo_epi32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m256i py = _mm256_srai_epi32(_mm256_mullo_epi32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);

            _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], px);
            _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], py);
            _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], _mm256_sub_epi32(z_fin, v_mid));
        }
    }
#elif defined(TORIDRAW_ORTHO_SIMD_SSE)
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
            __m128i xv = _mm_set_epi32(vertex_x[i + 3], vertex_x[i + 2], vertex_x[i + 1], vertex_x[i]);
            __m128i yv = _mm_set_epi32(vertex_y[i + 3], vertex_y[i + 2], vertex_y[i + 1], vertex_y[i]);
            __m128i zv = _mm_set_epi32(vertex_z[i + 3], vertex_z[i + 2], vertex_z[i + 1], vertex_z[i]);

            __m128i x_rot = _mm_srai_epi32(_mm_add_epi32(mullo_epi32_sse(xv, c_my), mullo_epi32_sse(zv, s_my)), 16);
            __m128i z_rot = _mm_srai_epi32(_mm_sub_epi32(mullo_epi32_sse(zv, c_my), mullo_epi32_sse(xv, s_my)), 16);
            x_rot = _mm_add_epi32(x_rot, _mm_set1_epi32(scene_x));
            __m128i y_rot = _mm_add_epi32(yv, _mm_set1_epi32(scene_y));
            z_rot = _mm_add_epi32(z_rot, _mm_set1_epi32(scene_z));

            __m128i x_cam = _mm_srai_epi32(_mm_add_epi32(mullo_epi32_sse(x_rot, c_yaw), mullo_epi32_sse(z_rot, s_yaw)), 16);
            __m128i z_cam = _mm_srai_epi32(_mm_sub_epi32(mullo_epi32_sse(z_rot, c_yaw), mullo_epi32_sse(x_rot, s_yaw)), 16);
            __m128i y_cam = _mm_srai_epi32(_mm_sub_epi32(mullo_epi32_sse(y_rot, c_pitch), mullo_epi32_sse(z_cam, s_pitch)), 16);
            __m128i z_fin = _mm_srai_epi32(_mm_add_epi32(mullo_epi32_sse(y_rot, s_pitch), mullo_epi32_sse(z_cam, c_pitch)), 16);

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
#endif

    for( ; i < num_vertices; i++ )
    {
        int x_cam;
        int y_cam;
        int z_fin;
        toridraw_ortho_to_camera_space(
            vertex_x[i], vertex_y[i], vertex_z[i],
            cos_model_yaw, sin_model_yaw,
            scene_x, scene_y, scene_z,
            cos_camera_yaw, sin_camera_yaw,
            cos_camera_pitch, sin_camera_pitch,
            &x_cam, &y_cam, &z_fin);

        orthographic_vertices_x[i] = x_cam;
        orthographic_vertices_y[i] = y_cam;
        orthographic_vertices_z[i] = z_fin;

        screen_vertices_x[i] = (x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** With camera-space output; marks vertices behind near_plane_z for the face test to DROP (see header). */
static inline void
project_vertices_array_ortho_fused_clip(
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw)
{
    int const cos_camera_pitch = RSCacheDat2A_NoiseCosTable[camera_pitch];
    int const sin_camera_pitch = g_sin_table[camera_pitch];
    int const cos_camera_yaw = RSCacheDat2A_NoiseCosTable[camera_yaw];
    int const sin_camera_yaw = g_sin_table[camera_yaw];
    int const cos_model_yaw = RSCacheDat2A_NoiseCosTable[model_yaw];
    int const sin_model_yaw = g_sin_table[model_yaw];

    int i = 0;

#if defined(TORIDRAW_ORTHO_SIMD_NEON)
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

            int32x4_t x_cam = vshrq_n_s32(vaddq_s32(vmulq_s32(x_rot, c_yaw), vmulq_s32(z_rot, s_yaw)), 16);
            int32x4_t z_cam = vshrq_n_s32(vsubq_s32(vmulq_s32(z_rot, c_yaw), vmulq_s32(x_rot, s_yaw)), 16);
            int32x4_t y_cam = vshrq_n_s32(vsubq_s32(vmulq_s32(y_rot, c_pitch), vmulq_s32(z_cam, s_pitch)), 16);
            int32x4_t z_fin = vshrq_n_s32(vaddq_s32(vmulq_s32(y_rot, s_pitch), vmulq_s32(z_cam, c_pitch)), 16);

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
#elif defined(TORIDRAW_ORTHO_SIMD_AVX2)
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

            __m256i x_rot = _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(xv, c_my), _mm256_mullo_epi32(zv, s_my)), 16);
            __m256i z_rot = _mm256_srai_epi32(_mm256_sub_epi32(_mm256_mullo_epi32(zv, c_my), _mm256_mullo_epi32(xv, s_my)), 16);
            x_rot = _mm256_add_epi32(x_rot, _mm256_set1_epi32(scene_x));
            __m256i y_rot = _mm256_add_epi32(yv, _mm256_set1_epi32(scene_y));
            z_rot = _mm256_add_epi32(z_rot, _mm256_set1_epi32(scene_z));

            __m256i x_cam = _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(x_rot, c_yaw), _mm256_mullo_epi32(z_rot, s_yaw)), 16);
            __m256i z_cam = _mm256_srai_epi32(_mm256_sub_epi32(_mm256_mullo_epi32(z_rot, c_yaw), _mm256_mullo_epi32(x_rot, s_yaw)), 16);
            __m256i y_cam = _mm256_srai_epi32(_mm256_sub_epi32(_mm256_mullo_epi32(y_rot, c_pitch), _mm256_mullo_epi32(z_cam, s_pitch)), 16);
            __m256i z_fin = _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(y_rot, s_pitch), _mm256_mullo_epi32(z_cam, c_pitch)), 16);

            _mm256_storeu_si256((__m256i*)&orthographic_vertices_x[i], x_cam);
            _mm256_storeu_si256((__m256i*)&orthographic_vertices_y[i], y_cam);
            _mm256_storeu_si256((__m256i*)&orthographic_vertices_z[i], z_fin);

            __m256i px = _mm256_srai_epi32(_mm256_mullo_epi32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m256i py = _mm256_srai_epi32(_mm256_mullo_epi32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            px = _mm256_blendv_epi8(px, v_sentinel, _mm256_cmpgt_epi32(v_near, z_fin));

            _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], px);
            _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], py);
            _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], _mm256_sub_epi32(z_fin, v_mid));
        }
    }
#elif defined(TORIDRAW_ORTHO_SIMD_SSE)
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
            __m128i xv = _mm_set_epi32(vertex_x[i + 3], vertex_x[i + 2], vertex_x[i + 1], vertex_x[i]);
            __m128i yv = _mm_set_epi32(vertex_y[i + 3], vertex_y[i + 2], vertex_y[i + 1], vertex_y[i]);
            __m128i zv = _mm_set_epi32(vertex_z[i + 3], vertex_z[i + 2], vertex_z[i + 1], vertex_z[i]);

            __m128i x_rot = _mm_srai_epi32(_mm_add_epi32(mullo_epi32_sse(xv, c_my), mullo_epi32_sse(zv, s_my)), 16);
            __m128i z_rot = _mm_srai_epi32(_mm_sub_epi32(mullo_epi32_sse(zv, c_my), mullo_epi32_sse(xv, s_my)), 16);
            x_rot = _mm_add_epi32(x_rot, _mm_set1_epi32(scene_x));
            __m128i y_rot = _mm_add_epi32(yv, _mm_set1_epi32(scene_y));
            z_rot = _mm_add_epi32(z_rot, _mm_set1_epi32(scene_z));

            __m128i x_cam = _mm_srai_epi32(_mm_add_epi32(mullo_epi32_sse(x_rot, c_yaw), mullo_epi32_sse(z_rot, s_yaw)), 16);
            __m128i z_cam = _mm_srai_epi32(_mm_sub_epi32(mullo_epi32_sse(z_rot, c_yaw), mullo_epi32_sse(x_rot, s_yaw)), 16);
            __m128i y_cam = _mm_srai_epi32(_mm_sub_epi32(mullo_epi32_sse(y_rot, c_pitch), mullo_epi32_sse(z_cam, s_pitch)), 16);
            __m128i z_fin = _mm_srai_epi32(_mm_add_epi32(mullo_epi32_sse(y_rot, s_pitch), mullo_epi32_sse(z_cam, c_pitch)), 16);

            _mm_storeu_si128((__m128i*)&orthographic_vertices_x[i], x_cam);
            _mm_storeu_si128((__m128i*)&orthographic_vertices_y[i], y_cam);
            _mm_storeu_si128((__m128i*)&orthographic_vertices_z[i], z_fin);

            __m128i px = _mm_srai_epi32(mullo_epi32_sse(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m128i py = _mm_srai_epi32(mullo_epi32_sse(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            {   /* and/andnot/or: a blend that is valid on plain SSE2 too. */
                __m128i m = _mm_cmplt_epi32(z_fin, v_near);
                px = _mm_or_si128(_mm_and_si128(m, v_sentinel), _mm_andnot_si128(m, px));
            }

            _mm_storeu_si128((__m128i*)&screen_vertices_x[i], px);
            _mm_storeu_si128((__m128i*)&screen_vertices_y[i], py);
            _mm_storeu_si128((__m128i*)&screen_vertices_z[i], _mm_sub_epi32(z_fin, v_mid));
        }
    }
#endif

    for( ; i < num_vertices; i++ )
    {
        int x_cam;
        int y_cam;
        int z_fin;
        toridraw_ortho_to_camera_space(
            vertex_x[i], vertex_y[i], vertex_z[i],
            cos_model_yaw, sin_model_yaw,
            scene_x, scene_y, scene_z,
            cos_camera_yaw, sin_camera_yaw,
            cos_camera_pitch, sin_camera_pitch,
            &x_cam, &y_cam, &z_fin);

        orthographic_vertices_x[i] = x_cam;
        orthographic_vertices_y[i] = y_cam;
        orthographic_vertices_z[i] = z_fin;

        screen_vertices_x[i] = (z_fin < near_plane_z)
                                   ? TORIDRAW_SCREEN_X_NEAR_CLIPPED
                                   : ((x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT);
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** No camera-space output; no near-plane test at all -- the cheapest kernel. */
static inline void
project_vertices_array_ortho_fused_notex_noclip(
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw)
{
    int const cos_camera_pitch = RSCacheDat2A_NoiseCosTable[camera_pitch];
    int const sin_camera_pitch = g_sin_table[camera_pitch];
    int const cos_camera_yaw = RSCacheDat2A_NoiseCosTable[camera_yaw];
    int const sin_camera_yaw = g_sin_table[camera_yaw];
    int const cos_model_yaw = RSCacheDat2A_NoiseCosTable[model_yaw];
    int const sin_model_yaw = g_sin_table[model_yaw];

    int i = 0;

#if defined(TORIDRAW_ORTHO_SIMD_NEON)
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

            int32x4_t x_cam = vshrq_n_s32(vaddq_s32(vmulq_s32(x_rot, c_yaw), vmulq_s32(z_rot, s_yaw)), 16);
            int32x4_t z_cam = vshrq_n_s32(vsubq_s32(vmulq_s32(z_rot, c_yaw), vmulq_s32(x_rot, s_yaw)), 16);
            int32x4_t y_cam = vshrq_n_s32(vsubq_s32(vmulq_s32(y_rot, c_pitch), vmulq_s32(z_cam, s_pitch)), 16);
            int32x4_t z_fin = vshrq_n_s32(vaddq_s32(vmulq_s32(y_rot, s_pitch), vmulq_s32(z_cam, c_pitch)), 16);

            int32x4_t px = vshrq_n_s32(vmulq_s32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            int32x4_t py = vshrq_n_s32(vmulq_s32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);

            vst1q_s32(&screen_vertices_x[i], px);
            vst1q_s32(&screen_vertices_y[i], py);
            vst1q_s32(&screen_vertices_z[i], vsubq_s32(z_fin, v_mid));
        }
    }
#elif defined(TORIDRAW_ORTHO_SIMD_AVX2)
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

            __m256i x_rot = _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(xv, c_my), _mm256_mullo_epi32(zv, s_my)), 16);
            __m256i z_rot = _mm256_srai_epi32(_mm256_sub_epi32(_mm256_mullo_epi32(zv, c_my), _mm256_mullo_epi32(xv, s_my)), 16);
            x_rot = _mm256_add_epi32(x_rot, _mm256_set1_epi32(scene_x));
            __m256i y_rot = _mm256_add_epi32(yv, _mm256_set1_epi32(scene_y));
            z_rot = _mm256_add_epi32(z_rot, _mm256_set1_epi32(scene_z));

            __m256i x_cam = _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(x_rot, c_yaw), _mm256_mullo_epi32(z_rot, s_yaw)), 16);
            __m256i z_cam = _mm256_srai_epi32(_mm256_sub_epi32(_mm256_mullo_epi32(z_rot, c_yaw), _mm256_mullo_epi32(x_rot, s_yaw)), 16);
            __m256i y_cam = _mm256_srai_epi32(_mm256_sub_epi32(_mm256_mullo_epi32(y_rot, c_pitch), _mm256_mullo_epi32(z_cam, s_pitch)), 16);
            __m256i z_fin = _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(y_rot, s_pitch), _mm256_mullo_epi32(z_cam, c_pitch)), 16);

            __m256i px = _mm256_srai_epi32(_mm256_mullo_epi32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m256i py = _mm256_srai_epi32(_mm256_mullo_epi32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);

            _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], px);
            _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], py);
            _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], _mm256_sub_epi32(z_fin, v_mid));
        }
    }
#elif defined(TORIDRAW_ORTHO_SIMD_SSE)
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
            __m128i xv = _mm_set_epi32(vertex_x[i + 3], vertex_x[i + 2], vertex_x[i + 1], vertex_x[i]);
            __m128i yv = _mm_set_epi32(vertex_y[i + 3], vertex_y[i + 2], vertex_y[i + 1], vertex_y[i]);
            __m128i zv = _mm_set_epi32(vertex_z[i + 3], vertex_z[i + 2], vertex_z[i + 1], vertex_z[i]);

            __m128i x_rot = _mm_srai_epi32(_mm_add_epi32(mullo_epi32_sse(xv, c_my), mullo_epi32_sse(zv, s_my)), 16);
            __m128i z_rot = _mm_srai_epi32(_mm_sub_epi32(mullo_epi32_sse(zv, c_my), mullo_epi32_sse(xv, s_my)), 16);
            x_rot = _mm_add_epi32(x_rot, _mm_set1_epi32(scene_x));
            __m128i y_rot = _mm_add_epi32(yv, _mm_set1_epi32(scene_y));
            z_rot = _mm_add_epi32(z_rot, _mm_set1_epi32(scene_z));

            __m128i x_cam = _mm_srai_epi32(_mm_add_epi32(mullo_epi32_sse(x_rot, c_yaw), mullo_epi32_sse(z_rot, s_yaw)), 16);
            __m128i z_cam = _mm_srai_epi32(_mm_sub_epi32(mullo_epi32_sse(z_rot, c_yaw), mullo_epi32_sse(x_rot, s_yaw)), 16);
            __m128i y_cam = _mm_srai_epi32(_mm_sub_epi32(mullo_epi32_sse(y_rot, c_pitch), mullo_epi32_sse(z_cam, s_pitch)), 16);
            __m128i z_fin = _mm_srai_epi32(_mm_add_epi32(mullo_epi32_sse(y_rot, s_pitch), mullo_epi32_sse(z_cam, c_pitch)), 16);

            __m128i px = _mm_srai_epi32(mullo_epi32_sse(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m128i py = _mm_srai_epi32(mullo_epi32_sse(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);

            _mm_storeu_si128((__m128i*)&screen_vertices_x[i], px);
            _mm_storeu_si128((__m128i*)&screen_vertices_y[i], py);
            _mm_storeu_si128((__m128i*)&screen_vertices_z[i], _mm_sub_epi32(z_fin, v_mid));
        }
    }
#endif

    for( ; i < num_vertices; i++ )
    {
        int x_cam;
        int y_cam;
        int z_fin;
        toridraw_ortho_to_camera_space(
            vertex_x[i], vertex_y[i], vertex_z[i],
            cos_model_yaw, sin_model_yaw,
            scene_x, scene_y, scene_z,
            cos_camera_yaw, sin_camera_yaw,
            cos_camera_pitch, sin_camera_pitch,
            &x_cam, &y_cam, &z_fin);

        screen_vertices_x[i] = (x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** No camera-space output; marks vertices behind near_plane_z for the face test to DROP (see header). */
static inline void
project_vertices_array_ortho_fused_notex_clip(
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw)
{
    int const cos_camera_pitch = RSCacheDat2A_NoiseCosTable[camera_pitch];
    int const sin_camera_pitch = g_sin_table[camera_pitch];
    int const cos_camera_yaw = RSCacheDat2A_NoiseCosTable[camera_yaw];
    int const sin_camera_yaw = g_sin_table[camera_yaw];
    int const cos_model_yaw = RSCacheDat2A_NoiseCosTable[model_yaw];
    int const sin_model_yaw = g_sin_table[model_yaw];

    int i = 0;

#if defined(TORIDRAW_ORTHO_SIMD_NEON)
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

            int32x4_t x_cam = vshrq_n_s32(vaddq_s32(vmulq_s32(x_rot, c_yaw), vmulq_s32(z_rot, s_yaw)), 16);
            int32x4_t z_cam = vshrq_n_s32(vsubq_s32(vmulq_s32(z_rot, c_yaw), vmulq_s32(x_rot, s_yaw)), 16);
            int32x4_t y_cam = vshrq_n_s32(vsubq_s32(vmulq_s32(y_rot, c_pitch), vmulq_s32(z_cam, s_pitch)), 16);
            int32x4_t z_fin = vshrq_n_s32(vaddq_s32(vmulq_s32(y_rot, s_pitch), vmulq_s32(z_cam, c_pitch)), 16);

            int32x4_t px = vshrq_n_s32(vmulq_s32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            int32x4_t py = vshrq_n_s32(vmulq_s32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            px = vbslq_s32(vcltq_s32(z_fin, v_near), v_sentinel, px);

            vst1q_s32(&screen_vertices_x[i], px);
            vst1q_s32(&screen_vertices_y[i], py);
            vst1q_s32(&screen_vertices_z[i], vsubq_s32(z_fin, v_mid));
        }
    }
#elif defined(TORIDRAW_ORTHO_SIMD_AVX2)
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

            __m256i x_rot = _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(xv, c_my), _mm256_mullo_epi32(zv, s_my)), 16);
            __m256i z_rot = _mm256_srai_epi32(_mm256_sub_epi32(_mm256_mullo_epi32(zv, c_my), _mm256_mullo_epi32(xv, s_my)), 16);
            x_rot = _mm256_add_epi32(x_rot, _mm256_set1_epi32(scene_x));
            __m256i y_rot = _mm256_add_epi32(yv, _mm256_set1_epi32(scene_y));
            z_rot = _mm256_add_epi32(z_rot, _mm256_set1_epi32(scene_z));

            __m256i x_cam = _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(x_rot, c_yaw), _mm256_mullo_epi32(z_rot, s_yaw)), 16);
            __m256i z_cam = _mm256_srai_epi32(_mm256_sub_epi32(_mm256_mullo_epi32(z_rot, c_yaw), _mm256_mullo_epi32(x_rot, s_yaw)), 16);
            __m256i y_cam = _mm256_srai_epi32(_mm256_sub_epi32(_mm256_mullo_epi32(y_rot, c_pitch), _mm256_mullo_epi32(z_cam, s_pitch)), 16);
            __m256i z_fin = _mm256_srai_epi32(_mm256_add_epi32(_mm256_mullo_epi32(y_rot, s_pitch), _mm256_mullo_epi32(z_cam, c_pitch)), 16);

            __m256i px = _mm256_srai_epi32(_mm256_mullo_epi32(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m256i py = _mm256_srai_epi32(_mm256_mullo_epi32(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            px = _mm256_blendv_epi8(px, v_sentinel, _mm256_cmpgt_epi32(v_near, z_fin));

            _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], px);
            _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], py);
            _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], _mm256_sub_epi32(z_fin, v_mid));
        }
    }
#elif defined(TORIDRAW_ORTHO_SIMD_SSE)
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
            __m128i xv = _mm_set_epi32(vertex_x[i + 3], vertex_x[i + 2], vertex_x[i + 1], vertex_x[i]);
            __m128i yv = _mm_set_epi32(vertex_y[i + 3], vertex_y[i + 2], vertex_y[i + 1], vertex_y[i]);
            __m128i zv = _mm_set_epi32(vertex_z[i + 3], vertex_z[i + 2], vertex_z[i + 1], vertex_z[i]);

            __m128i x_rot = _mm_srai_epi32(_mm_add_epi32(mullo_epi32_sse(xv, c_my), mullo_epi32_sse(zv, s_my)), 16);
            __m128i z_rot = _mm_srai_epi32(_mm_sub_epi32(mullo_epi32_sse(zv, c_my), mullo_epi32_sse(xv, s_my)), 16);
            x_rot = _mm_add_epi32(x_rot, _mm_set1_epi32(scene_x));
            __m128i y_rot = _mm_add_epi32(yv, _mm_set1_epi32(scene_y));
            z_rot = _mm_add_epi32(z_rot, _mm_set1_epi32(scene_z));

            __m128i x_cam = _mm_srai_epi32(_mm_add_epi32(mullo_epi32_sse(x_rot, c_yaw), mullo_epi32_sse(z_rot, s_yaw)), 16);
            __m128i z_cam = _mm_srai_epi32(_mm_sub_epi32(mullo_epi32_sse(z_rot, c_yaw), mullo_epi32_sse(x_rot, s_yaw)), 16);
            __m128i y_cam = _mm_srai_epi32(_mm_sub_epi32(mullo_epi32_sse(y_rot, c_pitch), mullo_epi32_sse(z_cam, s_pitch)), 16);
            __m128i z_fin = _mm_srai_epi32(_mm_add_epi32(mullo_epi32_sse(y_rot, s_pitch), mullo_epi32_sse(z_cam, c_pitch)), 16);

            __m128i px = _mm_srai_epi32(mullo_epi32_sse(x_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            __m128i py = _mm_srai_epi32(mullo_epi32_sse(y_cam, v_zoom), TORIDRAW_ORTHO_ZOOM_SHIFT);
            {   /* and/andnot/or: a blend that is valid on plain SSE2 too. */
                __m128i m = _mm_cmplt_epi32(z_fin, v_near);
                px = _mm_or_si128(_mm_and_si128(m, v_sentinel), _mm_andnot_si128(m, px));
            }

            _mm_storeu_si128((__m128i*)&screen_vertices_x[i], px);
            _mm_storeu_si128((__m128i*)&screen_vertices_y[i], py);
            _mm_storeu_si128((__m128i*)&screen_vertices_z[i], _mm_sub_epi32(z_fin, v_mid));
        }
    }
#endif

    for( ; i < num_vertices; i++ )
    {
        int x_cam;
        int y_cam;
        int z_fin;
        toridraw_ortho_to_camera_space(
            vertex_x[i], vertex_y[i], vertex_z[i],
            cos_model_yaw, sin_model_yaw,
            scene_x, scene_y, scene_z,
            cos_camera_yaw, sin_camera_yaw,
            cos_camera_pitch, sin_camera_pitch,
            &x_cam, &y_cam, &z_fin);

        screen_vertices_x[i] = (z_fin < near_plane_z)
                                   ? TORIDRAW_SCREEN_X_NEAR_CLIPPED
                                   : ((x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT);
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** Full 6DOF parallel projection (with camera-space output).
 *  Scalar: model pitch/roll is the icon and spotanim path, not world geometry,
 *  so it is not worth a vector transform of its own. */
static inline void
project_vertices_array_ortho6_fused_noclip(
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex pv = project_orthographic(
            vertex_x[i], vertex_y[i], vertex_z[i],
            model_pitch, model_yaw, model_roll,
            scene_x, scene_y, scene_z,
            camera_pitch, camera_yaw, camera_roll);

        int x_cam = pv.x;
        int y_cam = pv.y;
        int z_fin = pv.z;

        orthographic_vertices_x[i] = x_cam;
        orthographic_vertices_y[i] = y_cam;
        orthographic_vertices_z[i] = z_fin;

        screen_vertices_x[i] = (x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** Full 6DOF parallel projection (with camera-space output, marks behind near_plane_z).
 *  Scalar: model pitch/roll is the icon and spotanim path, not world geometry,
 *  so it is not worth a vector transform of its own. */
static inline void
project_vertices_array_ortho6_fused_clip(
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex pv = project_orthographic(
            vertex_x[i], vertex_y[i], vertex_z[i],
            model_pitch, model_yaw, model_roll,
            scene_x, scene_y, scene_z,
            camera_pitch, camera_yaw, camera_roll);

        int x_cam = pv.x;
        int y_cam = pv.y;
        int z_fin = pv.z;

        orthographic_vertices_x[i] = x_cam;
        orthographic_vertices_y[i] = y_cam;
        orthographic_vertices_z[i] = z_fin;

        screen_vertices_x[i] = (z_fin < near_plane_z)
                                   ? TORIDRAW_SCREEN_X_NEAR_CLIPPED
                                   : ((x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT);
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** Full 6DOF parallel projection (no camera-space output).
 *  Scalar: model pitch/roll is the icon and spotanim path, not world geometry,
 *  so it is not worth a vector transform of its own. */
static inline void
project_vertices_array_ortho6_fused_notex_noclip(
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex pv = project_orthographic(
            vertex_x[i], vertex_y[i], vertex_z[i],
            model_pitch, model_yaw, model_roll,
            scene_x, scene_y, scene_z,
            camera_pitch, camera_yaw, camera_roll);

        int x_cam = pv.x;
        int y_cam = pv.y;
        int z_fin = pv.z;

        screen_vertices_x[i] = (x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** Full 6DOF parallel projection (no camera-space output, marks behind near_plane_z).
 *  Scalar: model pitch/roll is the icon and spotanim path, not world geometry,
 *  so it is not worth a vector transform of its own. */
static inline void
project_vertices_array_ortho6_fused_notex_clip(
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex pv = project_orthographic(
            vertex_x[i], vertex_y[i], vertex_z[i],
            model_pitch, model_yaw, model_roll,
            scene_x, scene_y, scene_z,
            camera_pitch, camera_yaw, camera_roll);

        int x_cam = pv.x;
        int y_cam = pv.y;
        int z_fin = pv.z;

        screen_vertices_x[i] = (z_fin < near_plane_z)
                                   ? TORIDRAW_SCREEN_X_NEAR_CLIPPED
                                   : ((x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT);
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

#endif /* PROJECTION_ORTHO_U_C */
