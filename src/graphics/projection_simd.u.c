#ifndef PROJECTION_SIMD_U_C
#define PROJECTION_SIMD_U_C

#include "projection.h"
#include "projection.u.c"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

extern int g_tan_table[2048];
extern int g_cos_table[2048];
extern int g_sin_table[2048];

#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include <arm_neon.h>

static inline void
project_vertices_array_neon(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;

    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int sin_model_yaw = g_sin_table[model_yaw];
    int cos_model_yaw = g_cos_table[model_yaw];

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        // load 4 verts
        int32x4_t xv4 = vld1q_s32(&vertex_x[i]);
        int32x4_t yv4 = vld1q_s32(&vertex_y[i]);
        int32x4_t zv4 = vld1q_s32(&vertex_z[i]);

        int32x4_t x_rotated = xv4;
        int32x4_t z_rotated = zv4;

        int32x4_t c_my = vdupq_n_s32(cos_model_yaw);
        int32x4_t s_my = vdupq_n_s32(sin_model_yaw);

        // x' = (x*c + z*s)>>16
        int32x4_t x_tmp = vaddq_s32(vmulq_s32(xv4, c_my), vmulq_s32(zv4, s_my));
        x_rotated = vshrq_n_s32(x_tmp, 16);

        // z' = (z*c - x*s)>>16
        int32x4_t z_tmp = vsubq_s32(vmulq_s32(zv4, c_my), vmulq_s32(xv4, s_my));
        z_rotated = vshrq_n_s32(z_tmp, 16);

        // translate
        x_rotated = vaddq_s32(x_rotated, vdupq_n_s32(scene_x));
        int32x4_t y_rotated = vaddq_s32(yv4, vdupq_n_s32(scene_y));
        z_rotated = vaddq_s32(z_rotated, vdupq_n_s32(scene_z));

        // yaw
        int32x4_t c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t s_yaw = vdupq_n_s32(sin_camera_yaw);

        // x_scene = (x'*c + z'*s)>>16
        int32x4_t x_scene = vaddq_s32(vmulq_s32(x_rotated, c_yaw), vmulq_s32(z_rotated, s_yaw));
        x_scene = vshrq_n_s32(x_scene, 16);

        // z_scene = (z'*c - x'*s)>>16
        int32x4_t z_scene = vsubq_s32(vmulq_s32(z_rotated, c_yaw), vmulq_s32(x_rotated, s_yaw));
        z_scene = vshrq_n_s32(z_scene, 16);

        // pitch
        int32x4_t c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t s_pitch = vdupq_n_s32(sin_camera_pitch);

        // y_scene = (y'*c - z_scene*s)>>16
        int32x4_t y_scene = vsubq_s32(vmulq_s32(y_rotated, c_pitch), vmulq_s32(z_scene, s_pitch));
        y_scene = vshrq_n_s32(y_scene, 16);

        // z_final = (y'*s + z_scene*c)>>16
        int32x4_t z_final = vaddq_s32(vmulq_s32(y_rotated, s_pitch), vmulq_s32(z_scene, c_pitch));
        z_final = vshrq_n_s32(z_final, 16);

        // store orthographic
        vst1q_s32(&orthographic_vertices_x[i], x_scene);
        vst1q_s32(&orthographic_vertices_y[i], y_scene);
        vst1q_s32(&orthographic_vertices_z[i], z_final);

        // perspective scale (keeps your SSE path semantics: >>6 after mul with ish15)
        int32x4_t cot_v = vdupq_n_s32(cot_fov_half_ish15);
        int32x4_t xfov = vmulq_s32(x_scene, cot_v);
        int32x4_t yfov = vmulq_s32(y_scene, cot_v);

        int32x4_t x_scaled = vshrq_n_s32(xfov, 6);
        int32x4_t y_scaled = vshrq_n_s32(yfov, 6);

        vst1q_s32(&screen_vertices_x[i], x_scaled);
        vst1q_s32(&screen_vertices_y[i], y_scaled);
    }

    // scalar tail (matches your original)
    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];

        int x_rotated = x;
        int z_rotated = z;

        x_rotated = (x * cos_model_yaw + z * sin_model_yaw) >> 16;
        z_rotated = (z * cos_model_yaw - x * sin_model_yaw) >> 16;

        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;

        int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
        int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;

        int y_scene = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
        int z_final_scene = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;

        orthographic_vertices_x[i] = x_scene;
        orthographic_vertices_y[i] = y_scene;
        orthographic_vertices_z[i] = z_final_scene;

        int screen_x = (x_scene * cot_fov_half_ish15) >> 15;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 15;

        screen_x = SCALE_UNIT(screen_x);
        screen_y = SCALE_UNIT(screen_y);

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

static inline void
project_vertices_array_noyaw_neon(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;

    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        // load 4 verts
        int32x4_t xv4 = vld1q_s32(&vertex_x[i]);
        int32x4_t yv4 = vld1q_s32(&vertex_y[i]);
        int32x4_t zv4 = vld1q_s32(&vertex_z[i]);

        int32x4_t x_rotated = xv4;
        int32x4_t z_rotated = zv4;

        // translate
        x_rotated = vaddq_s32(x_rotated, vdupq_n_s32(scene_x));
        int32x4_t y_rotated = vaddq_s32(yv4, vdupq_n_s32(scene_y));
        z_rotated = vaddq_s32(z_rotated, vdupq_n_s32(scene_z));

        // yaw
        int32x4_t c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t s_yaw = vdupq_n_s32(sin_camera_yaw);

        // x_scene = (x'*c + z'*s)>>16
        int32x4_t x_scene = vaddq_s32(vmulq_s32(x_rotated, c_yaw), vmulq_s32(z_rotated, s_yaw));
        x_scene = vshrq_n_s32(x_scene, 16);

        // z_scene = (z'*c - x'*s)>>16
        int32x4_t z_scene = vsubq_s32(vmulq_s32(z_rotated, c_yaw), vmulq_s32(x_rotated, s_yaw));
        z_scene = vshrq_n_s32(z_scene, 16);

        // pitch
        int32x4_t c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t s_pitch = vdupq_n_s32(sin_camera_pitch);

        // y_scene = (y'*c - z_scene*s)>>16
        int32x4_t y_scene = vsubq_s32(vmulq_s32(y_rotated, c_pitch), vmulq_s32(z_scene, s_pitch));
        y_scene = vshrq_n_s32(y_scene, 16);

        // z_final = (y'*s + z_scene*c)>>16
        int32x4_t z_final = vaddq_s32(vmulq_s32(y_rotated, s_pitch), vmulq_s32(z_scene, c_pitch));
        z_final = vshrq_n_s32(z_final, 16);

        // store orthographic
        vst1q_s32(&orthographic_vertices_x[i], x_scene);
        vst1q_s32(&orthographic_vertices_y[i], y_scene);
        vst1q_s32(&orthographic_vertices_z[i], z_final);

        // perspective scale (keeps your SSE path semantics: >>6 after mul with ish15)
        int32x4_t cot_v = vdupq_n_s32(cot_fov_half_ish15);
        int32x4_t xfov = vmulq_s32(x_scene, cot_v);
        int32x4_t yfov = vmulq_s32(y_scene, cot_v);

        int32x4_t x_scaled = vshrq_n_s32(xfov, 6);
        int32x4_t y_scaled = vshrq_n_s32(yfov, 6);

        vst1q_s32(&screen_vertices_x[i], x_scaled);
        vst1q_s32(&screen_vertices_y[i], y_scaled);
    }

    // scalar tail (matches your original)
    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];

        int x_rotated = x;
        int z_rotated = z;

        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;

        int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
        int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;

        int y_scene = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
        int z_final_scene = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;

        orthographic_vertices_x[i] = x_scene;
        orthographic_vertices_y[i] = y_scene;
        orthographic_vertices_z[i] = z_final_scene;

        int screen_x = (x_scene * cot_fov_half_ish15) >> 15;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 15;

        screen_x = SCALE_UNIT(screen_x);
        screen_y = SCALE_UNIT(screen_y);

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

static inline void
project_vertices_array(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    if( model_yaw != 0 )
    {
        project_vertices_array_neon(
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            model_yaw,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
    else
    {
        project_vertices_array_noyaw_neon(
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }

    int i = 0;

    // Arm Neon Float Div is faster.
#define ARM_NEON_FLOAT_RECIP_DIV
#ifdef ARM_NEON_FLOAT_RECIP_DIV
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        // Load z values
        int32x4_t z_i = vld1q_s32(&orthographic_vertices_z[i]);

        // Compute screen_vertices_z = z - model_mid_z
        int32x4_t midz = vdupq_n_s32(model_mid_z);
        int32x4_t outz = vsubq_s32(z_i, midz);
        vst1q_s32(&screen_vertices_z[i], outz);

        // Mask for clipped vertices (z < near_plane_z)
        uint32x4_t clipped_mask = vcltq_s32(z_i, vdupq_n_s32(near_plane_z));

        // Convert z to float
        float32x4_t z_f = vcvtq_f32_s32(z_i);

        // Reciprocal estimate (1/z)
        float32x4_t recip = vrecpeq_f32(z_f);
        recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);
        recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip); // refine twice

        // Scale reciprocal into Q31 fixed-point
        float32x4_t scale = vdupq_n_f32((float)(1ll << 30)); // use Q30 for headroom
        float32x4_t recip_scaled = vmulq_f32(recip, scale);
        int32x4_t recip_q31 = vcvtq_s32_f32(recip_scaled);

        // Load x and y
        int32x4_t x = vld1q_s32(&screen_vertices_x[i]);
        int32x4_t y = vld1q_s32(&screen_vertices_y[i]);

        // Multiply by reciprocal (Q30) and shift down
        int64x2_t xl0 = vmull_s32(vget_low_s32(x), vget_low_s32(recip_q31));
        int64x2_t xl1 = vmull_s32(vget_high_s32(x), vget_high_s32(recip_q31));
        int64x2_t yl0 = vmull_s32(vget_low_s32(y), vget_low_s32(recip_q31));
        int64x2_t yl1 = vmull_s32(vget_high_s32(y), vget_high_s32(recip_q31));

        // Shift down by 30 to undo fixed-point scale
        int32x4_t x_div = vcombine_s32(vshrn_n_s64(xl0, 30), vshrn_n_s64(xl1, 30));
        int32x4_t y_div = vcombine_s32(vshrn_n_s64(yl0, 30), vshrn_n_s64(yl1, 30));

        // Apply clipping: x = -5000 if clipped
        int32x4_t neg5000 = vdupq_n_s32(-5000);
        x_div = vbslq_s32(clipped_mask, neg5000, x_div);

        // Store results
        vst1q_s32(&screen_vertices_x[i], x_div);
        vst1q_s32(&screen_vertices_y[i], y_div);
    }
#endif // ARM_NEON_FLOAT_DIV

    // Scalar fallback for leftovers
    for( ; i < num_vertices; i++ )
    {
        int z = orthographic_vertices_z[i];
        bool clipped = (z < near_plane_z);

        screen_vertices_z[i] = z - model_mid_z;

        if( clipped )
        {
            screen_vertices_x[i] = -5000;
        }
        else
        {
            screen_vertices_x[i] /= z;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] /= z;
        }
    }
}

#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include <immintrin.h>

static inline void
project_vertices_array_avx2(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;

    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];

    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int sin_model_yaw = g_sin_table[model_yaw];
    int cos_model_yaw = g_cos_table[model_yaw];

    // Handle remaining vertices with scalar code
    int i = 0;
    for( ; i + 8 <= num_vertices; i += 8 )
    {
        // int x = vertex_x[i];
        // int y = vertex_y[i];
        // int z = vertex_z[i];

        __m256i xv8 = _mm256_loadu_si256((__m256i*)&vertex_x[i]);
        __m256i yv8 = _mm256_loadu_si256((__m256i*)&vertex_y[i]);
        __m256i zv8 = _mm256_loadu_si256((__m256i*)&vertex_z[i]);

        // int x_rotated = x;
        // int z_rotated = z;

        __m256i x_rotated = xv8;
        __m256i z_rotated = zv8;

        // x_rotated = x * cos_model_yaw + z * sin_model_yaw;
        // x_rotated >>= 16;
        // z_rotated = z * cos_model_yaw - x * sin_model_yaw;
        // z_rotated >>= 16;
        __m256i cos_model_yaw_v8 = _mm256_set1_epi32(cos_model_yaw);
        __m256i sin_model_yaw_v8 = _mm256_set1_epi32(sin_model_yaw);

        x_rotated = _mm256_add_epi32(
            _mm256_mullo_epi32(xv8, cos_model_yaw_v8), _mm256_mullo_epi32(zv8, sin_model_yaw_v8));
        x_rotated = _mm256_srai_epi32(x_rotated, 16);

        z_rotated = _mm256_sub_epi32(
            _mm256_mullo_epi32(zv8, cos_model_yaw_v8), _mm256_mullo_epi32(xv8, sin_model_yaw_v8));
        z_rotated = _mm256_srai_epi32(z_rotated, 16);

        // Translate points relative to camera position
        // x_rotated += scene_x;
        // int y_rotated = y + scene_y;
        // z_rotated += scene_z;
        x_rotated = _mm256_add_epi32(x_rotated, _mm256_set1_epi32(scene_x));
        __m256i y_rotated = _mm256_add_epi32(yv8, _mm256_set1_epi32(scene_y));
        z_rotated = _mm256_add_epi32(z_rotated, _mm256_set1_epi32(scene_z));

        // // Apply perspective rotation
        // // First rotate around Y-axis (scene yaw)
        // // int x_scene = x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw;
        // // x_scene >>= 16;
        // // int z_scene = z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw;
        // // z_scene >>= 16;

        __m256i cos_camera_yaw_v8 = _mm256_set1_epi32(cos_camera_yaw);
        __m256i sin_camera_yaw_v8 = _mm256_set1_epi32(sin_camera_yaw);
        __m256i x_scene = _mm256_add_epi32(
            _mm256_mullo_epi32(x_rotated, cos_camera_yaw_v8),
            _mm256_mullo_epi32(z_rotated, sin_camera_yaw_v8));
        x_scene = _mm256_srai_epi32(x_scene, 16);
        __m256i z_scene = _mm256_sub_epi32(
            _mm256_mullo_epi32(z_rotated, cos_camera_yaw_v8),
            _mm256_mullo_epi32(x_rotated, sin_camera_yaw_v8));
        z_scene = _mm256_srai_epi32(z_scene, 16);

        // // Then rotate around X-axis (scene pitch)
        // // int y_scene = y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch;
        // // y_scene >>= 16;
        // // int z_final_scene = y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch;
        // // z_final_scene >>= 16;
        __m256i cos_camera_pitch_v8 = _mm256_set1_epi32(cos_camera_pitch);
        __m256i sin_camera_pitch_v8 = _mm256_set1_epi32(sin_camera_pitch);
        __m256i y_scene = _mm256_sub_epi32(
            _mm256_mullo_epi32(y_rotated, cos_camera_pitch_v8),
            _mm256_mullo_epi32(z_scene, sin_camera_pitch_v8));
        y_scene = _mm256_srai_epi32(y_scene, 16);
        __m256i z_scene_final = _mm256_add_epi32(
            _mm256_mullo_epi32(y_rotated, sin_camera_pitch_v8),
            _mm256_mullo_epi32(z_scene, cos_camera_pitch_v8));
        z_scene_final = _mm256_srai_epi32(z_scene_final, 16);

        // orthographic_vertices_x[i] = x_scene;
        // orthographic_vertices_y[i] = y_scene;
        // orthographic_vertices_z[i] = z_final_scene;
        _mm256_storeu_si256((__m256i*)&orthographic_vertices_x[i], x_scene);
        _mm256_storeu_si256((__m256i*)&orthographic_vertices_y[i], y_scene);
        _mm256_storeu_si256((__m256i*)&orthographic_vertices_z[i], z_scene_final);

        // x *= cot_fov_half_ish_scaled;
        // y *= cot_fov_half_ish_scaled;

        __m256i cot_fov_half_ish15_v8 = _mm256_set1_epi32(cot_fov_half_ish15);
        __m256i xfov_ish15 = _mm256_mullo_epi32(x_scene, cot_fov_half_ish15_v8);
        __m256i yfov_ish15 = _mm256_mullo_epi32(y_scene, cot_fov_half_ish15_v8);

        // x >>= 15;
        // y >>= 15;
        // // So we can increase x_bits_max to 11 by reducing the angle scale by 1.
        // // int screen_x = (x << UNIT_SCALE_SHIFT);
        // // int screen_y = (y << UNIT_SCALE_SHIFT);
        __m256i x_scaled = _mm256_srai_epi32(xfov_ish15, 6);
        __m256i y_scaled = _mm256_srai_epi32(yfov_ish15, 6);

        // // screen_vertices_x[i] = screen_x;
        // // screen_vertices_y[i] = screen_y;
        // // screen_vertices_z[i] = z;

        _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], x_scaled);
        _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], y_scaled);
        // _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], z_scene_final);
    }

    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];

        int x_rotated = x;
        int z_rotated = z;

        if( model_yaw != 0 )
        {
            x_rotated = x * cos_model_yaw + z * sin_model_yaw;
            x_rotated >>= 16;
            z_rotated = z * cos_model_yaw - x * sin_model_yaw;
            z_rotated >>= 16;
        }

        // Translate points relative to camera position
        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;

        // Apply perspective rotation
        // First rotate around Y-axis (scene yaw)
        int x_scene = x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw;
        x_scene >>= 16;
        int z_scene = z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw;
        z_scene >>= 16;

        // Then rotate around X-axis (scene pitch)
        int y_scene = y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch;
        y_scene >>= 16;
        int z_final_scene = y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch;
        z_final_scene >>= 16;

        // assert(orthographic_vertices_x[i] == x_scene);
        // assert(orthographic_vertices_y[i] == y_scene);
        // assert(orthographic_vertices_z[i] == z_final_scene);

        orthographic_vertices_x[i] = x_scene;
        orthographic_vertices_y[i] = y_scene;
        orthographic_vertices_z[i] = z_final_scene;

        int screen_x = x_scene * cot_fov_half_ish15;
        int screen_y = y_scene * cot_fov_half_ish15;
        screen_x >>= 15;
        screen_y >>= 15;

        // So we can increase x_bits_max to 11 by reducing the angle scale by 1.
        screen_x = SCALE_UNIT(screen_x);
        screen_y = SCALE_UNIT(screen_y);

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
        // screen_vertices_z[i] = -1;
    }
}

static inline void
project_vertices_array_noyaw_avx2(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;

    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];

    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int sin_model_yaw = g_sin_table[model_yaw];
    int cos_model_yaw = g_cos_table[model_yaw];

    // Handle remaining vertices with scalar code
    int i = 0;
    for( ; i + 8 <= num_vertices; i += 8 )
    {
        // int x = vertex_x[i];
        // int y = vertex_y[i];
        // int z = vertex_z[i];

        __m256i xv8 = _mm256_loadu_si256((__m256i*)&vertex_x[i]);
        __m256i yv8 = _mm256_loadu_si256((__m256i*)&vertex_y[i]);
        __m256i zv8 = _mm256_loadu_si256((__m256i*)&vertex_z[i]);

        // int x_rotated = x;
        // int z_rotated = z;

        __m256i x_rotated = xv8;
        __m256i z_rotated = zv8;

        // if( model_yaw != 0 )
        // {
        //     // x_rotated = x * cos_model_yaw + z * sin_model_yaw;
        //     // x_rotated >>= 16;
        //     // z_rotated = z * cos_model_yaw - x * sin_model_yaw;
        //     // z_rotated >>= 16;
        //     __m256i cos_model_yaw_v8 = _mm256_set1_epi32(cos_model_yaw);
        //     __m256i sin_model_yaw_v8 = _mm256_set1_epi32(sin_model_yaw);

        //     x_rotated = _mm256_add_epi32(
        //         _mm256_mullo_epi32(xv8, cos_model_yaw_v8),
        //         _mm256_mullo_epi32(zv8, sin_model_yaw_v8));
        //     x_rotated = _mm256_srai_epi32(x_rotated, 16);

        //     z_rotated = _mm256_sub_epi32(
        //         _mm256_mullo_epi32(zv8, cos_model_yaw_v8),
        //         _mm256_mullo_epi32(xv8, sin_model_yaw_v8));
        //     z_rotated = _mm256_srai_epi32(z_rotated, 16);
        // }

        // Translate points relative to camera position
        // x_rotated += scene_x;
        // int y_rotated = y + scene_y;
        // z_rotated += scene_z;
        x_rotated = _mm256_add_epi32(x_rotated, _mm256_set1_epi32(scene_x));
        __m256i y_rotated = _mm256_add_epi32(yv8, _mm256_set1_epi32(scene_y));
        z_rotated = _mm256_add_epi32(z_rotated, _mm256_set1_epi32(scene_z));

        // // Apply perspective rotation
        // // First rotate around Y-axis (scene yaw)
        // // int x_scene = x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw;
        // // x_scene >>= 16;
        // // int z_scene = z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw;
        // // z_scene >>= 16;

        __m256i cos_camera_yaw_v8 = _mm256_set1_epi32(cos_camera_yaw);
        __m256i sin_camera_yaw_v8 = _mm256_set1_epi32(sin_camera_yaw);
        __m256i x_scene = _mm256_add_epi32(
            _mm256_mullo_epi32(x_rotated, cos_camera_yaw_v8),
            _mm256_mullo_epi32(z_rotated, sin_camera_yaw_v8));
        x_scene = _mm256_srai_epi32(x_scene, 16);
        __m256i z_scene = _mm256_sub_epi32(
            _mm256_mullo_epi32(z_rotated, cos_camera_yaw_v8),
            _mm256_mullo_epi32(x_rotated, sin_camera_yaw_v8));
        z_scene = _mm256_srai_epi32(z_scene, 16);

        // // Then rotate around X-axis (scene pitch)
        // // int y_scene = y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch;
        // // y_scene >>= 16;
        // // int z_final_scene = y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch;
        // // z_final_scene >>= 16;
        __m256i cos_camera_pitch_v8 = _mm256_set1_epi32(cos_camera_pitch);
        __m256i sin_camera_pitch_v8 = _mm256_set1_epi32(sin_camera_pitch);
        __m256i y_scene = _mm256_sub_epi32(
            _mm256_mullo_epi32(y_rotated, cos_camera_pitch_v8),
            _mm256_mullo_epi32(z_scene, sin_camera_pitch_v8));
        y_scene = _mm256_srai_epi32(y_scene, 16);
        __m256i z_scene_final = _mm256_add_epi32(
            _mm256_mullo_epi32(y_rotated, sin_camera_pitch_v8),
            _mm256_mullo_epi32(z_scene, cos_camera_pitch_v8));
        z_scene_final = _mm256_srai_epi32(z_scene_final, 16);

        // orthographic_vertices_x[i] = x_scene;
        // orthographic_vertices_y[i] = y_scene;
        // orthographic_vertices_z[i] = z_final_scene;
        _mm256_storeu_si256((__m256i*)&orthographic_vertices_x[i], x_scene);
        _mm256_storeu_si256((__m256i*)&orthographic_vertices_y[i], y_scene);
        _mm256_storeu_si256((__m256i*)&orthographic_vertices_z[i], z_scene_final);

        // x *= cot_fov_half_ish_scaled;
        // y *= cot_fov_half_ish_scaled;

        __m256i cot_fov_half_ish15_v8 = _mm256_set1_epi32(cot_fov_half_ish15);
        __m256i xfov_ish15 = _mm256_mullo_epi32(x_scene, cot_fov_half_ish15_v8);
        __m256i yfov_ish15 = _mm256_mullo_epi32(y_scene, cot_fov_half_ish15_v8);

        // x >>= 15;
        // y >>= 15;
        // // So we can increase x_bits_max to 11 by reducing the angle scale by 1.
        // // int screen_x = (x << UNIT_SCALE_SHIFT);
        // // int screen_y = (y << UNIT_SCALE_SHIFT);
        __m256i x_scaled = _mm256_srai_epi32(xfov_ish15, 6);
        __m256i y_scaled = _mm256_srai_epi32(yfov_ish15, 6);

        // // screen_vertices_x[i] = screen_x;
        // // screen_vertices_y[i] = screen_y;
        // // screen_vertices_z[i] = z;

        _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], x_scaled);
        _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], y_scaled);
        // _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], z_scene_final);
    }

    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];

        int x_rotated = x;
        int z_rotated = z;

        // if( model_yaw != 0 )
        // {
        //     x_rotated = x * cos_model_yaw + z * sin_model_yaw;
        //     x_rotated >>= 16;
        //     z_rotated = z * cos_model_yaw - x * sin_model_yaw;
        //     z_rotated >>= 16;
        // }

        // Translate points relative to camera position
        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;

        // Apply perspective rotation
        // First rotate around Y-axis (scene yaw)
        int x_scene = x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw;
        x_scene >>= 16;
        int z_scene = z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw;
        z_scene >>= 16;

        // Then rotate around X-axis (scene pitch)
        int y_scene = y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch;
        y_scene >>= 16;
        int z_final_scene = y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch;
        z_final_scene >>= 16;

        // assert(orthographic_vertices_x[i] == x_scene);
        // assert(orthographic_vertices_y[i] == y_scene);
        // assert(orthographic_vertices_z[i] == z_final_scene);

        orthographic_vertices_x[i] = x_scene;
        orthographic_vertices_y[i] = y_scene;
        orthographic_vertices_z[i] = z_final_scene;

        int screen_x = x_scene * cot_fov_half_ish15;
        int screen_y = y_scene * cot_fov_half_ish15;
        screen_x >>= 15;
        screen_y >>= 15;

        // So we can increase x_bits_max to 11 by reducing the angle scale by 1.
        screen_x = SCALE_UNIT(screen_x);
        screen_y = SCALE_UNIT(screen_y);

        // assert(screen_vertices_x[i] == screen_x);
        // assert(screen_vertices_y[i] == screen_y);

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
        // screen_vertices_z[i] = -1;
    }
}

static inline void
project_vertices_array(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)

{
    if( model_yaw != 0 )
    {
        project_vertices_array_avx2(
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            model_yaw,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
    else
    {
        project_vertices_array_noyaw_avx2(
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            model_yaw,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }

    const int vsteps = 8; // AVX2 processes 8x int32 per vector
    int i = 0;

    // Benchmarking shows that AVX2_FLOAT_DIV is faster on newer platforms.
    // On my Intel Core i5 4210U @ 1.70GHz, AVX2_FLOAT_DIV runs the lumbridge scene
    // at about 6ms while without it runs about 5ms.
    // Intel Core i5 4210U @ 1.70GHz
    // AVX2_FLOAT_DIV: 6ms
    // AVX2: 5ms
    // No SIMD: 5.2ms
    // On my Intel Core Ultra 7 258V, AVX2_FLOAT_DIV runs the lumbridge scene
    // 1.67ms on Windows GCC and 1.72ms without.
    //
    // Update: 10/07/2025 - Fast reciprocal division is substantially faster on the Core Ultra 7
    // 258V. This is closer to what ARM Neon Float Div does, which is something I didn't benchmark.
#define AVX2_FLOAT_RECIP_DIV
#ifdef AVX2_FLOAT_RECIP_DIV
    __m256i v_near = _mm256_set1_epi32(near_plane_z);
    __m256i v_mid = _mm256_set1_epi32(model_mid_z);
    __m256i v_neg5000 = _mm256_set1_epi32(-5000);
    __m256i v_neg5001 = _mm256_set1_epi32(-5001);

    for( ; i + vsteps - 1 < num_vertices; i += vsteps )
    {
        // load z
        __m256i vz = _mm256_loadu_si256((__m256i const*)(orthographic_vertices_z + i));

        // clipped mask: z < near_plane_z  <=> near > z
        __m256i clipped_mask = _mm256_cmpgt_epi32(v_near, vz); // 0xFFFFFFFF where clipped

        // screen_vertices_z = z - model_mid_z
        __m256i vscreen_z = _mm256_sub_epi32(vz, v_mid);
        _mm256_storeu_si256((__m256i*)(screen_vertices_z + i), vscreen_z);

        // load original screen x,y (these are the numerators pre-division)
        __m256i vsx = _mm256_loadu_si256((__m256i const*)(screen_vertices_x + i));
        __m256i vsy = _mm256_loadu_si256((__m256i const*)(screen_vertices_y + i));

        // Convert to float for division (we'll truncate back to int)
        __m256 fx = _mm256_cvtepi32_ps(vsx);
        __m256 fz = _mm256_cvtepi32_ps(vz);

        // perform float division using reciprocal approximation
        __m256 rcp_z = _mm256_rcp_ps(fz);
        __m256 fdivx = _mm256_mul_ps(fx, rcp_z);
        __m256 fdivy = _mm256_mul_ps(_mm256_cvtepi32_ps(vsy), rcp_z);

        // convert back to int with truncation (matches C integer division trunc towards zero)
        __m256i divx_i = _mm256_cvttps_epi32(fdivx);
        __m256i divy_i = _mm256_cvttps_epi32(fdivy);

        // For x: if divx_i == -5000 (and only for non-clipped lanes), change to -5001.
        __m256i eq_neg5000 = _mm256_cmpeq_epi32(divx_i, v_neg5000);
        // mask for non-clipped lanes:
        __m256i not_clipped_mask =
            _mm256_xor_si256(clipped_mask, _mm256_set1_epi32(-1)); // ~clipped_mask

        // eq_and_not_clipped = eq_neg5000 & not_clipped_mask
        __m256i eq_and_not_clipped = _mm256_and_si256(eq_neg5000, not_clipped_mask);

        // where eq_and_not_clipped set -5001, else keep divx_i
        // blend: result_x_adj = (eq_and_not_clipped) ? v_neg5001 : divx_i
        __m256i result_x_adj = _mm256_blendv_epi8(divx_i, v_neg5001, eq_and_not_clipped);

        // Now final x: clipped ? -5000 : result_x_adj
        __m256i final_x = _mm256_blendv_epi8(result_x_adj, v_neg5000, clipped_mask);

        // For y: if clipped, keep original vsy; else use divy_i
        __m256i final_y = _mm256_blendv_epi8(divy_i, vsy, clipped_mask);

        // store final results
        _mm256_storeu_si256((__m256i*)(screen_vertices_x + i), final_x);
        _mm256_storeu_si256((__m256i*)(screen_vertices_y + i), final_y);
    }
#endif // AVX2_FLOAT_DIV

    // tail scalar for remaining elements
    for( ; i < num_vertices; ++i )
    {
        int z = orthographic_vertices_z[i];

        bool clipped = false;
        if( z < near_plane_z )
            clipped = true;

        screen_vertices_z[i] = z - model_mid_z;

        if( clipped )
        {
            screen_vertices_x[i] = -5000;
            // screen_vertices_y stays as-is
        }
        else
        {
            // integer division
            screen_vertices_x[i] = screen_vertices_x[i] / z;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_vertices_y[i] / z;
        }
    }
}

#elif (defined(__SSE2__) || defined(__SSE4_1__)) && !defined(SSE2_DISABLED)
#include "sse2_41compat.h"

static inline void
project_vertices_array_sse(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;

    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int sin_model_yaw = g_sin_table[model_yaw];
    int cos_model_yaw = g_cos_table[model_yaw];

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        __m128i xv4 = _mm_loadu_si128((__m128i*)&vertex_x[i]);
        __m128i yv4 = _mm_loadu_si128((__m128i*)&vertex_y[i]);
        __m128i zv4 = _mm_loadu_si128((__m128i*)&vertex_z[i]);

        __m128i x_rotated = xv4;
        __m128i z_rotated = zv4;

        __m128i cos_model_yaw_v4 = _mm_set1_epi32(cos_model_yaw);
        __m128i sin_model_yaw_v4 = _mm_set1_epi32(sin_model_yaw);

        x_rotated = _mm_add_epi32(
            mullo_epi32_sse(xv4, cos_model_yaw_v4), mullo_epi32_sse(zv4, sin_model_yaw_v4));
        x_rotated = _mm_srai_epi32(x_rotated, 16);

        z_rotated = _mm_sub_epi32(
            mullo_epi32_sse(zv4, cos_model_yaw_v4), mullo_epi32_sse(xv4, sin_model_yaw_v4));
        z_rotated = _mm_srai_epi32(z_rotated, 16);

        x_rotated = _mm_add_epi32(x_rotated, _mm_set1_epi32(scene_x));
        __m128i y_rotated = _mm_add_epi32(yv4, _mm_set1_epi32(scene_y));
        z_rotated = _mm_add_epi32(z_rotated, _mm_set1_epi32(scene_z));

        __m128i cos_camera_yaw_v4 = _mm_set1_epi32(cos_camera_yaw);
        __m128i sin_camera_yaw_v4 = _mm_set1_epi32(sin_camera_yaw);

        __m128i x_scene = _mm_add_epi32(
            mullo_epi32_sse(x_rotated, cos_camera_yaw_v4),
            mullo_epi32_sse(z_rotated, sin_camera_yaw_v4));
        x_scene = _mm_srai_epi32(x_scene, 16);

        __m128i z_scene = _mm_sub_epi32(
            mullo_epi32_sse(z_rotated, cos_camera_yaw_v4),
            mullo_epi32_sse(x_rotated, sin_camera_yaw_v4));
        z_scene = _mm_srai_epi32(z_scene, 16);

        __m128i cos_camera_pitch_v4 = _mm_set1_epi32(cos_camera_pitch);
        __m128i sin_camera_pitch_v4 = _mm_set1_epi32(sin_camera_pitch);

        __m128i y_scene = _mm_sub_epi32(
            mullo_epi32_sse(y_rotated, cos_camera_pitch_v4),
            mullo_epi32_sse(z_scene, sin_camera_pitch_v4));
        y_scene = _mm_srai_epi32(y_scene, 16);

        __m128i z_scene_final = _mm_add_epi32(
            mullo_epi32_sse(y_rotated, sin_camera_pitch_v4),
            mullo_epi32_sse(z_scene, cos_camera_pitch_v4));
        z_scene_final = _mm_srai_epi32(z_scene_final, 16);

        _mm_storeu_si128((__m128i*)&orthographic_vertices_x[i], x_scene);
        _mm_storeu_si128((__m128i*)&orthographic_vertices_y[i], y_scene);
        _mm_storeu_si128((__m128i*)&orthographic_vertices_z[i], z_scene_final);

        __m128i cot_fov_half_ish15_v4 = _mm_set1_epi32(cot_fov_half_ish15);

        __m128i xfov_ish15 = mullo_epi32_sse(x_scene, cot_fov_half_ish15_v4);
        __m128i yfov_ish15 = mullo_epi32_sse(y_scene, cot_fov_half_ish15_v4);

        __m128i x_scaled = _mm_srai_epi32(xfov_ish15, 6);
        __m128i y_scaled = _mm_srai_epi32(yfov_ish15, 6);

        _mm_storeu_si128((__m128i*)&screen_vertices_x[i], x_scaled);
        _mm_storeu_si128((__m128i*)&screen_vertices_y[i], y_scaled);
    }

    // scalar fallback
    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];

        int x_rotated = x;
        int z_rotated = z;

        x_rotated = (x * cos_model_yaw + z * sin_model_yaw) >> 16;
        z_rotated = (z * cos_model_yaw - x * sin_model_yaw) >> 16;

        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;

        int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
        int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;

        int y_scene = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
        int z_final_scene = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;

        orthographic_vertices_x[i] = x_scene;
        orthographic_vertices_y[i] = y_scene;
        orthographic_vertices_z[i] = z_final_scene;

        int screen_x = (x_scene * cot_fov_half_ish15) >> 15;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 15;

        screen_x = SCALE_UNIT(screen_x);
        screen_y = SCALE_UNIT(screen_y);

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

static inline void
project_vertices_array_noyaw_sse(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;

    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        __m128i xv4 = _mm_loadu_si128((__m128i*)&vertex_x[i]);
        __m128i yv4 = _mm_loadu_si128((__m128i*)&vertex_y[i]);
        __m128i zv4 = _mm_loadu_si128((__m128i*)&vertex_z[i]);

        __m128i x_rotated = xv4;
        __m128i z_rotated = zv4;

        x_rotated = _mm_add_epi32(x_rotated, _mm_set1_epi32(scene_x));
        __m128i y_rotated = _mm_add_epi32(yv4, _mm_set1_epi32(scene_y));
        z_rotated = _mm_add_epi32(z_rotated, _mm_set1_epi32(scene_z));

        __m128i cos_camera_yaw_v4 = _mm_set1_epi32(cos_camera_yaw);
        __m128i sin_camera_yaw_v4 = _mm_set1_epi32(sin_camera_yaw);

        __m128i x_scene = _mm_add_epi32(
            mullo_epi32_sse(x_rotated, cos_camera_yaw_v4),
            mullo_epi32_sse(z_rotated, sin_camera_yaw_v4));
        x_scene = _mm_srai_epi32(x_scene, 16);

        __m128i z_scene = _mm_sub_epi32(
            mullo_epi32_sse(z_rotated, cos_camera_yaw_v4),
            mullo_epi32_sse(x_rotated, sin_camera_yaw_v4));
        z_scene = _mm_srai_epi32(z_scene, 16);

        __m128i cos_camera_pitch_v4 = _mm_set1_epi32(cos_camera_pitch);
        __m128i sin_camera_pitch_v4 = _mm_set1_epi32(sin_camera_pitch);

        __m128i y_scene = _mm_sub_epi32(
            mullo_epi32_sse(y_rotated, cos_camera_pitch_v4),
            mullo_epi32_sse(z_scene, sin_camera_pitch_v4));
        y_scene = _mm_srai_epi32(y_scene, 16);

        __m128i z_scene_final = _mm_add_epi32(
            mullo_epi32_sse(y_rotated, sin_camera_pitch_v4),
            mullo_epi32_sse(z_scene, cos_camera_pitch_v4));
        z_scene_final = _mm_srai_epi32(z_scene_final, 16);

        _mm_storeu_si128((__m128i*)&orthographic_vertices_x[i], x_scene);
        _mm_storeu_si128((__m128i*)&orthographic_vertices_y[i], y_scene);
        _mm_storeu_si128((__m128i*)&orthographic_vertices_z[i], z_scene_final);

        __m128i cot_fov_half_ish15_v4 = _mm_set1_epi32(cot_fov_half_ish15);

        __m128i xfov_ish15 = mullo_epi32_sse(x_scene, cot_fov_half_ish15_v4);
        __m128i yfov_ish15 = mullo_epi32_sse(y_scene, cot_fov_half_ish15_v4);

        __m128i x_scaled = _mm_srai_epi32(xfov_ish15, 6);
        __m128i y_scaled = _mm_srai_epi32(yfov_ish15, 6);

        _mm_storeu_si128((__m128i*)&screen_vertices_x[i], x_scaled);
        _mm_storeu_si128((__m128i*)&screen_vertices_y[i], y_scaled);
    }

    // scalar fallback
    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];

        int x_rotated = x;
        int z_rotated = z;

        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;

        int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
        int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;

        int y_scene = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
        int z_final_scene = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;

        orthographic_vertices_x[i] = x_scene;
        orthographic_vertices_y[i] = y_scene;
        orthographic_vertices_z[i] = z_final_scene;

        int screen_x = (x_scene * cot_fov_half_ish15) >> 15;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 15;

        screen_x = SCALE_UNIT(screen_x);
        screen_y = SCALE_UNIT(screen_y);

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

static inline void
project_vertices_array(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    if( model_yaw != 0 )
    {
        project_vertices_array_sse(
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            model_yaw,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
    else
    {
        project_vertices_array_noyaw_sse(
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }

    const int vsteps = 4; // 128-bit holds 4x int32
    int i = 0;

#define SSE_FLOAT_RECIP_DIV
#ifdef SSE_FLOAT_RECIP_DIV
    // SSE1 compatible - use float operations and manual conversion
    for( ; i + vsteps - 1 < num_vertices; i += vsteps )
    {
        // Load and process using floats and manual conversion for SSE1
        int vz_arr[4] = { orthographic_vertices_z[i],
                          orthographic_vertices_z[i + 1],
                          orthographic_vertices_z[i + 2],
                          orthographic_vertices_z[i + 3] };
        int vsx_arr[4] = { screen_vertices_x[i],
                           screen_vertices_x[i + 1],
                           screen_vertices_x[i + 2],
                           screen_vertices_x[i + 3] };
        int vsy_arr[4] = { screen_vertices_y[i],
                           screen_vertices_y[i + 1],
                           screen_vertices_y[i + 2],
                           screen_vertices_y[i + 3] };

        __m128 fz =
            _mm_set_ps((float)vz_arr[3], (float)vz_arr[2], (float)vz_arr[1], (float)vz_arr[0]);
        __m128 fx =
            _mm_set_ps((float)vsx_arr[3], (float)vsx_arr[2], (float)vsx_arr[1], (float)vsx_arr[0]);
        __m128 fy =
            _mm_set_ps((float)vsy_arr[3], (float)vsy_arr[2], (float)vsy_arr[1], (float)vsy_arr[0]);

        // perform float division using reciprocal approximation
        __m128 rcp_z = _mm_rcp_ps(fz);
        __m128 fdivx = _mm_mul_ps(fx, rcp_z);
        __m128 fdivy = _mm_mul_ps(fy, rcp_z);

        // Store results as float temporarily
        float fdivx_arr[4], fdivy_arr[4];
        _mm_storeu_ps(fdivx_arr, fdivx);
        _mm_storeu_ps(fdivy_arr, fdivy);

        // Process each element (SSE1 doesn't have integer operations)
        for( int j = 0; j < 4; j++ )
        {
            int z = vz_arr[j];
            bool clipped = (z < near_plane_z);

            screen_vertices_z[i + j] = z - model_mid_z;

            int divx_i = (int)fdivx_arr[j];
            int divy_i = (int)fdivy_arr[j];

            if( clipped )
            {
                screen_vertices_x[i + j] = -5000;
                screen_vertices_y[i + j] = vsy_arr[j];
            }
            else
            {
                if( divx_i == -5000 )
                    divx_i = -5001;
                screen_vertices_x[i + j] = divx_i;
                screen_vertices_y[i + j] = divy_i;
            }
        }
    }
#endif // SSE_FLOAT_DIV

    for( ; i < num_vertices; i++ )
    {
        int z = orthographic_vertices_z[i];

        bool clipped = false;
        if( z < near_plane_z )
            clipped = true;

        // If vertex is too close to camera, set it to a large negative value
        // This will cause it to be clipped in the rasterization step
        // screen_vertices_z[i] = projected_vertex.z - mid_z;
        screen_vertices_z[i] = z - model_mid_z;

        if( clipped )
        {
            screen_vertices_x[i] = -5000;
        }
        else
        {
            screen_vertices_x[i] = screen_vertices_x[i] / z;
            // TODO: The actual renderer from the deob marks that a face was clipped.
            // so it doesn't have to worry about a value actually being -5000.
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_vertices_y[i] / z;
        }
    }
}

// __SSE2__
#elif defined(__SSE__) && !defined(SSE_DISABLED)
#include <xmmintrin.h>

static inline void
project_vertices_array_sse_float(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];

    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    const float inv_scale = 1.0f / 65535.0f;
    __m128 v_cos_camera_pitch = _mm_set1_ps(g_cos_table[camera_pitch] * inv_scale);
    __m128 v_sin_camera_pitch = _mm_set1_ps(g_sin_table[camera_pitch] * inv_scale);
    __m128 v_cos_camera_yaw = _mm_set1_ps(g_cos_table[camera_yaw] * inv_scale);
    __m128 v_sin_camera_yaw = _mm_set1_ps(g_sin_table[camera_yaw] * inv_scale);
    __m128 v_cos_model_yaw = _mm_set1_ps(g_cos_table[model_yaw] * inv_scale);
    __m128 v_sin_model_yaw = _mm_set1_ps(g_sin_table[model_yaw] * inv_scale);
    __m128 v_cot_fov = _mm_set1_ps(cot_fov_half_ish16 * inv_scale);

    __m128 v_512 = _mm_set1_ps((float)UNIT_SCALE);

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        // Load vertices and convert to float (SSE1 compatible)
        __m128 xv4 = _mm_set_ps(
            (float)vertex_x[i + 3],
            (float)vertex_x[i + 2],
            (float)vertex_x[i + 1],
            (float)vertex_x[i]);
        __m128 yv4 = _mm_set_ps(
            (float)vertex_y[i + 3],
            (float)vertex_y[i + 2],
            (float)vertex_y[i + 1],
            (float)vertex_y[i]);
        __m128 zv4 = _mm_set_ps(
            (float)vertex_z[i + 3],
            (float)vertex_z[i + 2],
            (float)vertex_z[i + 1],
            (float)vertex_z[i]);

        __m128 x_rotated =
            _mm_add_ps(_mm_mul_ps(xv4, v_cos_model_yaw), _mm_mul_ps(zv4, v_sin_model_yaw));
        __m128 z_rotated =
            _mm_sub_ps(_mm_mul_ps(zv4, v_cos_model_yaw), _mm_mul_ps(xv4, v_sin_model_yaw));

        __m128 x_trans = _mm_add_ps(x_rotated, _mm_set1_ps((float)scene_x));
        __m128 y_trans = _mm_add_ps(yv4, _mm_set1_ps((float)scene_y));
        __m128 z_trans = _mm_add_ps(z_rotated, _mm_set1_ps((float)scene_z));

        // Camera yaw rotation
        __m128 x_scene = _mm_add_ps(
            _mm_mul_ps(x_trans, v_cos_camera_yaw), _mm_mul_ps(z_trans, v_sin_camera_yaw));
        __m128 z_scene = _mm_sub_ps(
            _mm_mul_ps(z_trans, v_cos_camera_yaw), _mm_mul_ps(x_trans, v_sin_camera_yaw));

        // Camera pitch rotation
        __m128 y_scene = _mm_sub_ps(
            _mm_mul_ps(y_trans, v_cos_camera_pitch), _mm_mul_ps(z_scene, v_sin_camera_pitch));
        __m128 z_final = _mm_add_ps(
            _mm_mul_ps(y_trans, v_sin_camera_pitch), _mm_mul_ps(z_scene, v_cos_camera_pitch));

        // Store orthographic vertices (convert and store manually for SSE1)
        float x_scene_arr[4], y_scene_arr[4], z_final_arr[4];
        _mm_storeu_ps(x_scene_arr, x_scene);
        _mm_storeu_ps(y_scene_arr, y_scene);
        _mm_storeu_ps(z_final_arr, z_final);

        orthographic_vertices_x[i] = (int)x_scene_arr[0];
        orthographic_vertices_x[i + 1] = (int)x_scene_arr[1];
        orthographic_vertices_x[i + 2] = (int)x_scene_arr[2];
        orthographic_vertices_x[i + 3] = (int)x_scene_arr[3];

        orthographic_vertices_y[i] = (int)y_scene_arr[0];
        orthographic_vertices_y[i + 1] = (int)y_scene_arr[1];
        orthographic_vertices_y[i + 2] = (int)y_scene_arr[2];
        orthographic_vertices_y[i + 3] = (int)y_scene_arr[3];

        orthographic_vertices_z[i] = (int)z_final_arr[0];
        orthographic_vertices_z[i + 1] = (int)z_final_arr[1];
        orthographic_vertices_z[i + 2] = (int)z_final_arr[2];
        orthographic_vertices_z[i + 3] = (int)z_final_arr[3];

        __m128 x_proj = _mm_mul_ps(x_scene, v_cot_fov);
        __m128 y_proj = _mm_mul_ps(y_scene, v_cot_fov);

        // Multiply by 512 instead of shifting (SSE1 compatible)
        __m128 x_final_f = _mm_mul_ps(x_proj, v_512);
        __m128 y_final_f = _mm_mul_ps(y_proj, v_512);

        // Store results (convert and store manually for SSE1)
        float x_final_arr[4], y_final_arr[4];
        _mm_storeu_ps(x_final_arr, x_final_f);
        _mm_storeu_ps(y_final_arr, y_final_f);

        screen_vertices_x[i] = (int)x_final_arr[0];
        screen_vertices_x[i + 1] = (int)x_final_arr[1];
        screen_vertices_x[i + 2] = (int)x_final_arr[2];
        screen_vertices_x[i + 3] = (int)x_final_arr[3];

        screen_vertices_y[i] = (int)y_final_arr[0];
        screen_vertices_y[i + 1] = (int)y_final_arr[1];
        screen_vertices_y[i + 2] = (int)y_final_arr[2];
        screen_vertices_y[i + 3] = (int)y_final_arr[3];
    }

    // Scalar fallback for remaining vertices
    for( ; i < num_vertices; i++ )
    {
        float x = (float)vertex_x[i];
        float y = (float)vertex_y[i];
        float z = (float)vertex_z[i];

        // Model yaw rotation
        float cos_model_yaw_f = g_cos_table[model_yaw] * inv_scale;
        float sin_model_yaw_f = g_sin_table[model_yaw] * inv_scale;
        float x_rotated = x * cos_model_yaw_f + z * sin_model_yaw_f;
        float z_rotated = z * cos_model_yaw_f - x * sin_model_yaw_f;

        // Translate
        float x_trans = x_rotated + (float)scene_x;
        float y_trans = y + (float)scene_y;
        float z_trans = z_rotated + (float)scene_z;

        // Camera yaw rotation
        float cos_camera_yaw_f = g_cos_table[camera_yaw] * inv_scale;
        float sin_camera_yaw_f = g_sin_table[camera_yaw] * inv_scale;
        float x_scene = x_trans * cos_camera_yaw_f + z_trans * sin_camera_yaw_f;
        float z_scene = z_trans * cos_camera_yaw_f - x_trans * sin_camera_yaw_f;

        // Camera pitch rotation
        float cos_camera_pitch_f = g_cos_table[camera_pitch] * inv_scale;
        float sin_camera_pitch_f = g_sin_table[camera_pitch] * inv_scale;
        float y_scene = y_trans * cos_camera_pitch_f - z_scene * sin_camera_pitch_f;
        float z_final = y_trans * sin_camera_pitch_f + z_scene * cos_camera_pitch_f;

        orthographic_vertices_x[i] = (int)x_scene;
        orthographic_vertices_y[i] = (int)y_scene;
        orthographic_vertices_z[i] = (int)z_final;

        // Perspective projection
        float cot_fov_f = cot_fov_half_ish16 * inv_scale;
        float x_proj = x_scene * cot_fov_f;
        float y_proj = y_scene * cot_fov_f;

        screen_vertices_x[i] = SCALE_UNIT((int)x_proj);
        screen_vertices_y[i] = SCALE_UNIT((int)y_proj);
    }
}

static inline void
project_vertices_array_sse_float_noyaw(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];

    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    const float inv_scale = 1.0f / 65535.0f;
    __m128 v_cos_camera_pitch = _mm_set1_ps(g_cos_table[camera_pitch] * inv_scale);
    __m128 v_sin_camera_pitch = _mm_set1_ps(g_sin_table[camera_pitch] * inv_scale);
    __m128 v_cos_camera_yaw = _mm_set1_ps(g_cos_table[camera_yaw] * inv_scale);
    __m128 v_sin_camera_yaw = _mm_set1_ps(g_sin_table[camera_yaw] * inv_scale);
    __m128 v_cot_fov = _mm_set1_ps(cot_fov_half_ish16 * inv_scale);

    __m128 v_512 = _mm_set1_ps((float)UNIT_SCALE);

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        // Load vertices and convert to float (SSE1 compatible)
        __m128 xv4 = _mm_set_ps(
            (float)vertex_x[i + 3],
            (float)vertex_x[i + 2],
            (float)vertex_x[i + 1],
            (float)vertex_x[i]);
        __m128 yv4 = _mm_set_ps(
            (float)vertex_y[i + 3],
            (float)vertex_y[i + 2],
            (float)vertex_y[i + 1],
            (float)vertex_y[i]);
        __m128 zv4 = _mm_set_ps(
            (float)vertex_z[i + 3],
            (float)vertex_z[i + 2],
            (float)vertex_z[i + 1],
            (float)vertex_z[i]);

        __m128 x_rotated = xv4;
        __m128 z_rotated = zv4;

        __m128 x_trans = _mm_add_ps(x_rotated, _mm_set1_ps((float)scene_x));
        __m128 y_trans = _mm_add_ps(yv4, _mm_set1_ps((float)scene_y));
        __m128 z_trans = _mm_add_ps(z_rotated, _mm_set1_ps((float)scene_z));

        // Camera yaw rotation
        __m128 x_scene = _mm_add_ps(
            _mm_mul_ps(x_trans, v_cos_camera_yaw), _mm_mul_ps(z_trans, v_sin_camera_yaw));
        __m128 z_scene = _mm_sub_ps(
            _mm_mul_ps(z_trans, v_cos_camera_yaw), _mm_mul_ps(x_trans, v_sin_camera_yaw));

        // Camera pitch rotation
        __m128 y_scene = _mm_sub_ps(
            _mm_mul_ps(y_trans, v_cos_camera_pitch), _mm_mul_ps(z_scene, v_sin_camera_pitch));
        __m128 z_final = _mm_add_ps(
            _mm_mul_ps(y_trans, v_sin_camera_pitch), _mm_mul_ps(z_scene, v_cos_camera_pitch));

        // Store orthographic vertices (convert and store manually for SSE1)
        float x_scene_arr[4], y_scene_arr[4], z_final_arr[4];
        _mm_storeu_ps(x_scene_arr, x_scene);
        _mm_storeu_ps(y_scene_arr, y_scene);
        _mm_storeu_ps(z_final_arr, z_final);

        orthographic_vertices_x[i] = (int)x_scene_arr[0];
        orthographic_vertices_x[i + 1] = (int)x_scene_arr[1];
        orthographic_vertices_x[i + 2] = (int)x_scene_arr[2];
        orthographic_vertices_x[i + 3] = (int)x_scene_arr[3];

        orthographic_vertices_y[i] = (int)y_scene_arr[0];
        orthographic_vertices_y[i + 1] = (int)y_scene_arr[1];
        orthographic_vertices_y[i + 2] = (int)y_scene_arr[2];
        orthographic_vertices_y[i + 3] = (int)y_scene_arr[3];

        orthographic_vertices_z[i] = (int)z_final_arr[0];
        orthographic_vertices_z[i + 1] = (int)z_final_arr[1];
        orthographic_vertices_z[i + 2] = (int)z_final_arr[2];
        orthographic_vertices_z[i + 3] = (int)z_final_arr[3];

        __m128 x_proj = _mm_mul_ps(x_scene, v_cot_fov);
        __m128 y_proj = _mm_mul_ps(y_scene, v_cot_fov);

        // Multiply by 512 instead of shifting (SSE1 compatible)
        __m128 x_final_f = _mm_mul_ps(x_proj, v_512);
        __m128 y_final_f = _mm_mul_ps(y_proj, v_512);

        // Store results (convert and store manually for SSE1)
        float x_final_arr[4], y_final_arr[4];
        _mm_storeu_ps(x_final_arr, x_final_f);
        _mm_storeu_ps(y_final_arr, y_final_f);

        screen_vertices_x[i] = (int)x_final_arr[0];
        screen_vertices_x[i + 1] = (int)x_final_arr[1];
        screen_vertices_x[i + 2] = (int)x_final_arr[2];
        screen_vertices_x[i + 3] = (int)x_final_arr[3];

        screen_vertices_y[i] = (int)y_final_arr[0];
        screen_vertices_y[i + 1] = (int)y_final_arr[1];
        screen_vertices_y[i + 2] = (int)y_final_arr[2];
        screen_vertices_y[i + 3] = (int)y_final_arr[3];
    }

    // Scalar fallback for remaining vertices
    for( ; i < num_vertices; i++ )
    {
        float x = (float)vertex_x[i];
        float y = (float)vertex_y[i];
        float z = (float)vertex_z[i];

        // Model yaw rotation
        float x_rotated = x;
        float z_rotated = z;

        // Translate
        float x_trans = x_rotated + (float)scene_x;
        float y_trans = y + (float)scene_y;
        float z_trans = z_rotated + (float)scene_z;

        // Camera yaw rotation
        float cos_camera_yaw_f = g_cos_table[camera_yaw] * inv_scale;
        float sin_camera_yaw_f = g_sin_table[camera_yaw] * inv_scale;
        float x_scene = x_trans * cos_camera_yaw_f + z_trans * sin_camera_yaw_f;
        float z_scene = z_trans * cos_camera_yaw_f - x_trans * sin_camera_yaw_f;

        // Camera pitch rotation
        float cos_camera_pitch_f = g_cos_table[camera_pitch] * inv_scale;
        float sin_camera_pitch_f = g_sin_table[camera_pitch] * inv_scale;
        float y_scene = y_trans * cos_camera_pitch_f - z_scene * sin_camera_pitch_f;
        float z_final = y_trans * sin_camera_pitch_f + z_scene * cos_camera_pitch_f;

        orthographic_vertices_x[i] = (int)x_scene;
        orthographic_vertices_y[i] = (int)y_scene;
        orthographic_vertices_z[i] = (int)z_final;

        // Perspective projection
        float cot_fov_f = cot_fov_half_ish16 * inv_scale;
        float x_proj = x_scene * cot_fov_f;
        float y_proj = y_scene * cot_fov_f;

        screen_vertices_x[i] = SCALE_UNIT((int)x_proj);
        screen_vertices_y[i] = SCALE_UNIT((int)y_proj);
    }
}

static inline void
project_vertices_array(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    // First do the projection using our float SSE implementation
    if( model_yaw != 0 )
    {
        project_vertices_array_sse_float(
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            model_yaw,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
    else
    {
        project_vertices_array_sse_float_noyaw(
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }

    // Then handle z-clipping and perspective division
    const int vsteps = 4; // SSE processes 4x float32 per vector
    int i = 0;

    // SSE1 Float reciprocal division is enabled by default
    // because SSE1 uses floats anyway.

    for( ; i + vsteps - 1 < num_vertices; i += vsteps )
    {
        // Load and process using floats and manual conversion for SSE1
        int vz_arr[4] = { orthographic_vertices_z[i],
                          orthographic_vertices_z[i + 1],
                          orthographic_vertices_z[i + 2],
                          orthographic_vertices_z[i + 3] };
        int vsx_arr[4] = { screen_vertices_x[i],
                           screen_vertices_x[i + 1],
                           screen_vertices_x[i + 2],
                           screen_vertices_x[i + 3] };
        int vsy_arr[4] = { screen_vertices_y[i],
                           screen_vertices_y[i + 1],
                           screen_vertices_y[i + 2],
                           screen_vertices_y[i + 3] };

        __m128 fz =
            _mm_set_ps((float)vz_arr[3], (float)vz_arr[2], (float)vz_arr[1], (float)vz_arr[0]);
        __m128 fx =
            _mm_set_ps((float)vsx_arr[3], (float)vsx_arr[2], (float)vsx_arr[1], (float)vsx_arr[0]);
        __m128 fy =
            _mm_set_ps((float)vsy_arr[3], (float)vsy_arr[2], (float)vsy_arr[1], (float)vsy_arr[0]);

        // perform float division using reciprocal approximation
        __m128 rcp_z = _mm_rcp_ps(fz);
        __m128 fdivx = _mm_mul_ps(fx, rcp_z);
        __m128 fdivy = _mm_mul_ps(fy, rcp_z);

        // Store results as float temporarily
        float fdivx_arr[4], fdivy_arr[4];
        _mm_storeu_ps(fdivx_arr, fdivx);
        _mm_storeu_ps(fdivy_arr, fdivy);

        // Process each element (SSE1 doesn't have integer operations)
        for( int j = 0; j < 4; j++ )
        {
            int z = vz_arr[j];
            bool clipped = (z < near_plane_z);

            screen_vertices_z[i + j] = z - model_mid_z;

            int divx_i = (int)fdivx_arr[j];
            int divy_i = (int)fdivy_arr[j];

            if( clipped )
            {
                screen_vertices_x[i + j] = -5000;
                screen_vertices_y[i + j] = vsy_arr[j];
            }
            else
            {
                if( divx_i == -5000 )
                    divx_i = -5001;
                screen_vertices_x[i + j] = divx_i;
                screen_vertices_y[i + j] = divy_i;
            }
        }
    }

    // Handle remaining vertices
    for( ; i < num_vertices; i++ )
    {
        int z = orthographic_vertices_z[i];

        bool clipped = false;
        if( z < near_plane_z )
            clipped = true;

        screen_vertices_z[i] = z - model_mid_z;

        if( clipped )
        {
            screen_vertices_x[i] = -5000;
        }
        else
        {
            screen_vertices_x[i] = screen_vertices_x[i] / z;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_vertices_y[i] / z;
        }
    }
}

#else // __SSE_

static inline void
project_vertices_array(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;

    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];

    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    // Checked on 09/15/2025, this does get vectorized on Mac with clang, using arm neon. It also
    // gets vectorized on Windows and Linux with GCC. Tested on 09/18/2025, that the hand-rolled
    // Simd above is faster than the compilers
    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex projected_vertex;
        project_orthographic_fast(
            &projected_vertex,
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_yaw,
            scene_x,
            scene_y,
            scene_z,
            camera_pitch,
            camera_yaw);

        int x = projected_vertex.x;
        int y = projected_vertex.y;
        int z = projected_vertex.z;

        x *= cot_fov_half_ish15;
        y *= cot_fov_half_ish15;
        x >>= 15;
        y >>= 15;

        int screen_x = SCALE_UNIT(x);
        int screen_y = SCALE_UNIT(y);

        orthographic_vertices_x[i] = projected_vertex.x;
        orthographic_vertices_y[i] = projected_vertex.y;
        orthographic_vertices_z[i] = projected_vertex.z;

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }

    for( int i = 0; i < num_vertices; i++ )
    {
        int z = orthographic_vertices_z[i];

        bool clipped = false;
        if( z < near_plane_z )
            clipped = true;

        // If vertex is too close to camera, set it to a large negative value
        // This will cause it to be clipped in the rasterization step
        // screen_vertices_z[i] = projected_vertex.z - mid_z;
        screen_vertices_z[i] = z - model_mid_z;

        if( clipped )
        {
            screen_vertices_x[i] = -5000;
        }
        else
        {
            screen_vertices_x[i] = screen_vertices_x[i] / z;
            // TODO: The actual renderer from the deob marks that a face was clipped.
            // so it doesn't have to worry about a value actually being -5000.
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_vertices_y[i] / z;
        }
    }
}
#endif

#endif