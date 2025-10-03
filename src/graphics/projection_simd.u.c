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

#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) )
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
#define ARM_NEON_FLOAT_DIV
#ifdef ARM_NEON_FLOAT_DIV
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

#elif defined(__AVX2__)
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
#ifdef AVX2_FLOAT_DIV
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

        // perform float division
        __m256 fdivx = _mm256_div_ps(fx, fz);
        __m256 fdivy = _mm256_div_ps(_mm256_cvtepi32_ps(vsy), fz);

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

#elif defined(__SSE2__)

#include <emmintrin.h>

// Emulate _mm_mullo_epi32(a, b) using SSE2
static inline __m128i
mullo_epi32_sse2(__m128i a, __m128i b)
{
    // Multiply low dwords (elements 0 and 2)
    __m128i prod02 = _mm_mul_epu32(a, b);

    // Shuffle to get elements 1 and 3 into low positions, then multiply
    __m128i a13 = _mm_srli_si128(a, 4);
    __m128i b13 = _mm_srli_si128(b, 4);
    __m128i prod13 = _mm_mul_epu32(a13, b13);

    // Extract low 32 bits of each product
    __m128i prod02_lo = _mm_shuffle_epi32(prod02, _MM_SHUFFLE(0, 0, 2, 0));
    __m128i prod13_lo = _mm_shuffle_epi32(prod13, _MM_SHUFFLE(0, 0, 2, 0));

    // Interleave results: prod02 gives results for elements 0,2
    // prod13 gives results for elements 1,3
    __m128i prod01 = _mm_unpacklo_epi32(prod02_lo, prod13_lo);
    __m128i prod23 = _mm_unpackhi_epi64(prod02_lo, prod13_lo);

    return _mm_unpacklo_epi64(prod01, prod23);
}

// blendv_epi8(a, b, mask) = (a & ~mask) | (b & mask)
__m128i
blendv_sse2(__m128i a, __m128i b, __m128i mask)
{
    __m128i t1 = _mm_andnot_si128(mask, a);
    __m128i t2 = _mm_and_si128(mask, b);
    return _mm_or_si128(t1, t2);
}

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
            _mm_mullo_epi32(xv4, cos_model_yaw_v4), _mm_mullo_epi32(zv4, sin_model_yaw_v4));
        x_rotated = _mm_srai_epi32(x_rotated, 16);

        z_rotated = _mm_sub_epi32(
            _mm_mullo_epi32(zv4, cos_model_yaw_v4), _mm_mullo_epi32(xv4, sin_model_yaw_v4));
        z_rotated = _mm_srai_epi32(z_rotated, 16);

        x_rotated = _mm_add_epi32(x_rotated, _mm_set1_epi32(scene_x));
        __m128i y_rotated = _mm_add_epi32(yv4, _mm_set1_epi32(scene_y));
        z_rotated = _mm_add_epi32(z_rotated, _mm_set1_epi32(scene_z));

        __m128i cos_camera_yaw_v4 = _mm_set1_epi32(cos_camera_yaw);
        __m128i sin_camera_yaw_v4 = _mm_set1_epi32(sin_camera_yaw);

        __m128i x_scene = _mm_add_epi32(
            mullo_epi32_sse2(x_rotated, cos_camera_yaw_v4),
            mullo_epi32_sse2(z_rotated, sin_camera_yaw_v4));
        x_scene = _mm_srai_epi32(x_scene, 16);

        __m128i z_scene = _mm_sub_epi32(
            mullo_epi32_sse2(z_rotated, cos_camera_yaw_v4),
            mullo_epi32_sse2(x_rotated, sin_camera_yaw_v4));
        z_scene = _mm_srai_epi32(z_scene, 16);

        __m128i cos_camera_pitch_v4 = _mm_set1_epi32(cos_camera_pitch);
        __m128i sin_camera_pitch_v4 = _mm_set1_epi32(sin_camera_pitch);

        __m128i y_scene = _mm_sub_epi32(
            mullo_epi32_sse2(y_rotated, cos_camera_pitch_v4),
            mullo_epi32_sse2(z_scene, sin_camera_pitch_v4));
        y_scene = _mm_srai_epi32(y_scene, 16);

        __m128i z_scene_final = _mm_add_epi32(
            mullo_epi32_sse2(y_rotated, sin_camera_pitch_v4),
            mullo_epi32_sse2(z_scene, cos_camera_pitch_v4));
        z_scene_final = _mm_srai_epi32(z_scene_final, 16);

        _mm_storeu_si128((__m128i*)&orthographic_vertices_x[i], x_scene);
        _mm_storeu_si128((__m128i*)&orthographic_vertices_y[i], y_scene);
        _mm_storeu_si128((__m128i*)&orthographic_vertices_z[i], z_scene_final);

        __m128i cot_fov_half_ish15_v4 = _mm_set1_epi32(cot_fov_half_ish15);

        __m128i xfov_ish15 = _mm_mullo_epi32(x_scene, cot_fov_half_ish15_v4);
        __m128i yfov_ish15 = _mm_mullo_epi32(y_scene, cot_fov_half_ish15_v4);

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
            _mm_mullo_epi32(x_rotated, cos_camera_yaw_v4),
            _mm_mullo_epi32(z_rotated, sin_camera_yaw_v4));
        x_scene = _mm_srai_epi32(x_scene, 16);

        __m128i z_scene = _mm_sub_epi32(
            _mm_mullo_epi32(z_rotated, cos_camera_yaw_v4),
            _mm_mullo_epi32(x_rotated, sin_camera_yaw_v4));
        z_scene = _mm_srai_epi32(z_scene, 16);

        __m128i cos_camera_pitch_v4 = _mm_set1_epi32(cos_camera_pitch);
        __m128i sin_camera_pitch_v4 = _mm_set1_epi32(sin_camera_pitch);

        __m128i y_scene = _mm_sub_epi32(
            _mm_mullo_epi32(y_rotated, cos_camera_pitch_v4),
            _mm_mullo_epi32(z_scene, sin_camera_pitch_v4));
        y_scene = _mm_srai_epi32(y_scene, 16);

        __m128i z_scene_final = _mm_add_epi32(
            _mm_mullo_epi32(y_rotated, sin_camera_pitch_v4),
            _mm_mullo_epi32(z_scene, cos_camera_pitch_v4));
        z_scene_final = _mm_srai_epi32(z_scene_final, 16);

        _mm_storeu_si128((__m128i*)&orthographic_vertices_x[i], x_scene);
        _mm_storeu_si128((__m128i*)&orthographic_vertices_y[i], y_scene);
        _mm_storeu_si128((__m128i*)&orthographic_vertices_z[i], z_scene_final);

        __m128i cot_fov_half_ish15_v4 = _mm_set1_epi32(cot_fov_half_ish15);

        __m128i xfov_ish15 = _mm_mullo_epi32(x_scene, cot_fov_half_ish15_v4);
        __m128i yfov_ish15 = _mm_mullo_epi32(y_scene, cot_fov_half_ish15_v4);

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

#ifdef SSE_FLOAT_DIV

    __m128i v_near = _mm_set1_epi32(near_plane_z);
    __m128i v_mid = _mm_set1_epi32(model_mid_z);
    __m128i v_neg5000 = _mm_set1_epi32(-5000);
    __m128i v_neg5001 = _mm_set1_epi32(-5001);
    __m128i v_allones = _mm_set1_epi32(-1);

    for( ; i + vsteps - 1 < num_vertices; i += vsteps )
    {
        // load z
        __m128i vz = _mm_loadu_si128((__m128i const*)(orthographic_vertices_z + i));

        // clipped mask: z < near_plane_z  <=> near > z
        __m128i clipped_mask = _mm_cmpgt_epi32(v_near, vz); // 0xFFFFFFFF where clipped

        // screen_vertices_z = z - model_mid_z
        __m128i vscreen_z = _mm_sub_epi32(vz, v_mid);
        _mm_storeu_si128((__m128i*)(screen_vertices_z + i), vscreen_z);

        // load original screen x,y (pre-division numerators)
        __m128i vsx = _mm_loadu_si128((__m128i const*)(screen_vertices_x + i));
        __m128i vsy = _mm_loadu_si128((__m128i const*)(screen_vertices_y + i));

        // Convert to float for division
        __m128 fx = _mm_cvtepi32_ps(vsx);
        __m128 fy = _mm_cvtepi32_ps(vsy);
        __m128 fz = _mm_cvtepi32_ps(vz);

        // perform float division
        __m128 fdivx = _mm_div_ps(fx, fz);
        __m128 fdivy = _mm_div_ps(fy, fz);

        // convert back to int (truncate toward zero)
        __m128i divx_i = _mm_cvttps_epi32(fdivx);
        __m128i divy_i = _mm_cvttps_epi32(fdivy);

        // check where divx_i == -5000
        __m128i eq_neg5000 = _mm_cmpeq_epi32(divx_i, v_neg5000);

        // mask for non-clipped lanes
        __m128i not_clipped_mask = _mm_xor_si128(clipped_mask, v_allones);

        // eq_and_not_clipped = eq_neg5000 & not_clipped_mask
        __m128i eq_and_not_clipped = _mm_and_si128(eq_neg5000, not_clipped_mask);

        // if eq_and_not_clipped → -5001, else divx_i
        __m128i result_x_adj = _mm_or_si128(
            _mm_and_si128(eq_and_not_clipped, v_neg5001),
            _mm_andnot_si128(eq_and_not_clipped, divx_i));

        // final x: if clipped → -5000, else result_x_adj
        __m128i final_x = _mm_or_si128(
            _mm_and_si128(clipped_mask, v_neg5000), _mm_andnot_si128(clipped_mask, result_x_adj));

        // final y: if clipped → vsy, else divy_i
        __m128i final_y =
            _mm_or_si128(_mm_and_si128(clipped_mask, vsy), _mm_andnot_si128(clipped_mask, divy_i));

        // store final results
        _mm_storeu_si128((__m128i*)(screen_vertices_x + i), final_x);
        _mm_storeu_si128((__m128i*)(screen_vertices_y + i), final_y);
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

#else

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