#ifndef PROJECTION16_SIMD_AVX_U_C
#define PROJECTION16_SIMD_AVX_U_C

#if defined(__AVX2__) && !defined(AVX2_DISABLED)
#include <immintrin.h>
#include "projection_zdiv_simd.avx.u.c"

static inline void
project_vertices_array_avx2(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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

        __m256i xv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_x[i]));
        __m256i yv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_y[i]));
        __m256i zv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_z[i]));

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
        screen_x >>= 6;
        screen_y >>= 6;

        // So we can increase x_bits_max to 11 by reducing the angle scale by 1.

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
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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

        __m256i xv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_x[i]));
        __m256i yv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_y[i]));
        __m256i zv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_z[i]));

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
        screen_x >>= 6;
        screen_y >>= 6;

        // So we can increase x_bits_max to 11 by reducing the angle scale by 1.

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
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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

static inline void
project_vertices_array_avx2_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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
    for( ; i + 8 <= num_vertices; i += 8 )
    {
        __m256i xv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_x[i]));
        __m256i yv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_y[i]));
        __m256i zv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_z[i]));

        __m256i x_rotated = xv8;
        __m256i z_rotated = zv8;

        __m256i cos_model_yaw_v8 = _mm256_set1_epi32(cos_model_yaw);
        __m256i sin_model_yaw_v8 = _mm256_set1_epi32(sin_model_yaw);

        x_rotated = _mm256_add_epi32(
            _mm256_mullo_epi32(xv8, cos_model_yaw_v8), _mm256_mullo_epi32(zv8, sin_model_yaw_v8));
        x_rotated = _mm256_srai_epi32(x_rotated, 16);

        z_rotated = _mm256_sub_epi32(
            _mm256_mullo_epi32(zv8, cos_model_yaw_v8), _mm256_mullo_epi32(xv8, sin_model_yaw_v8));
        z_rotated = _mm256_srai_epi32(z_rotated, 16);

        x_rotated = _mm256_add_epi32(x_rotated, _mm256_set1_epi32(scene_x));
        __m256i y_rotated = _mm256_add_epi32(yv8, _mm256_set1_epi32(scene_y));
        z_rotated = _mm256_add_epi32(z_rotated, _mm256_set1_epi32(scene_z));

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

        // store z_final to screen_vertices_z as temporary
        _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], z_scene_final);

        __m256i cot_fov_half_ish15_v8 = _mm256_set1_epi32(cot_fov_half_ish15);
        __m256i xfov_ish15 = _mm256_mullo_epi32(x_scene, cot_fov_half_ish15_v8);
        __m256i yfov_ish15 = _mm256_mullo_epi32(y_scene, cot_fov_half_ish15_v8);

        __m256i x_scaled = _mm256_srai_epi32(xfov_ish15, 6);
        __m256i y_scaled = _mm256_srai_epi32(yfov_ish15, 6);

        _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], x_scaled);
        _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], y_scaled);
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

        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;

        int x_scene = x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw;
        x_scene >>= 16;
        int z_scene = z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw;
        z_scene >>= 16;

        int y_scene = y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch;
        y_scene >>= 16;
        int z_final_scene = y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch;
        z_final_scene >>= 16;

        screen_vertices_z[i] = z_final_scene;

        int screen_x = x_scene * cot_fov_half_ish15;
        int screen_y = y_scene * cot_fov_half_ish15;
        screen_x >>= 6;
        screen_y >>= 6;

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

static inline void
project_vertices_array_noyaw_avx2_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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

    int i = 0;
    for( ; i + 8 <= num_vertices; i += 8 )
    {
        __m256i xv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_x[i]));
        __m256i yv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_y[i]));
        __m256i zv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_z[i]));

        __m256i x_rotated = xv8;
        __m256i z_rotated = zv8;

        x_rotated = _mm256_add_epi32(x_rotated, _mm256_set1_epi32(scene_x));
        __m256i y_rotated = _mm256_add_epi32(yv8, _mm256_set1_epi32(scene_y));
        z_rotated = _mm256_add_epi32(z_rotated, _mm256_set1_epi32(scene_z));

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

        _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], z_scene_final);

        __m256i cot_fov_half_ish15_v8 = _mm256_set1_epi32(cot_fov_half_ish15);
        __m256i xfov_ish15 = _mm256_mullo_epi32(x_scene, cot_fov_half_ish15_v8);
        __m256i yfov_ish15 = _mm256_mullo_epi32(y_scene, cot_fov_half_ish15_v8);

        __m256i x_scaled = _mm256_srai_epi32(xfov_ish15, 6);
        __m256i y_scaled = _mm256_srai_epi32(yfov_ish15, 6);

        _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], x_scaled);
        _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], y_scaled);
    }

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

        int x_scene = x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw;
        x_scene >>= 16;
        int z_scene = z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw;
        z_scene >>= 16;

        int y_scene = y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch;
        y_scene >>= 16;
        int z_final_scene = y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch;
        z_final_scene >>= 16;

        screen_vertices_z[i] = z_final_scene;

        int screen_x = x_scene * cot_fov_half_ish15;
        int screen_y = y_scene * cot_fov_half_ish15;
        screen_x >>= 6;
        screen_y >>= 6;

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

static inline void
project_vertices_array_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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
        project_vertices_array_avx2_notex(
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
        project_vertices_array_noyaw_avx2_notex(
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

    // Second pass: z-division. screen_vertices_z currently holds raw z_final from the inner kernel.
    const int vsteps = 8;
    int i = 0;

#define AVX2_FLOAT_RECIP_DIV_NOTEX
#ifdef AVX2_FLOAT_RECIP_DIV_NOTEX
    __m256i v_near = _mm256_set1_epi32(near_plane_z);
    __m256i v_mid = _mm256_set1_epi32(model_mid_z);
    __m256i v_neg5000 = _mm256_set1_epi32(-5000);
    __m256i v_neg5001 = _mm256_set1_epi32(-5001);

    for( ; i + vsteps - 1 < num_vertices; i += vsteps )
    {
        __m256i vz = _mm256_loadu_si256((__m256i const*)(screen_vertices_z + i));

        __m256i clipped_mask = _mm256_cmpgt_epi32(v_near, vz);

        __m256i vscreen_z = _mm256_sub_epi32(vz, v_mid);
        _mm256_storeu_si256((__m256i*)(screen_vertices_z + i), vscreen_z);

        __m256i vsx = _mm256_loadu_si256((__m256i const*)(screen_vertices_x + i));
        __m256i vsy = _mm256_loadu_si256((__m256i const*)(screen_vertices_y + i));

        __m256 fx = _mm256_cvtepi32_ps(vsx);
        __m256 fz = _mm256_cvtepi32_ps(vz);

        __m256 rcp_z = _mm256_rcp_ps(fz);
        __m256 fdivx = _mm256_mul_ps(fx, rcp_z);
        __m256 fdivy = _mm256_mul_ps(_mm256_cvtepi32_ps(vsy), rcp_z);

        __m256i divx_i = _mm256_cvttps_epi32(fdivx);
        __m256i divy_i = _mm256_cvttps_epi32(fdivy);

        __m256i eq_neg5000 = _mm256_cmpeq_epi32(divx_i, v_neg5000);
        __m256i not_clipped_mask = _mm256_xor_si256(clipped_mask, _mm256_set1_epi32(-1));
        __m256i eq_and_not_clipped = _mm256_and_si256(eq_neg5000, not_clipped_mask);
        __m256i result_x_adj = _mm256_blendv_epi8(divx_i, v_neg5001, eq_and_not_clipped);
        __m256i final_x = _mm256_blendv_epi8(result_x_adj, v_neg5000, clipped_mask);
        __m256i final_y = _mm256_blendv_epi8(divy_i, vsy, clipped_mask);

        _mm256_storeu_si256((__m256i*)(screen_vertices_x + i), final_x);
        _mm256_storeu_si256((__m256i*)(screen_vertices_y + i), final_y);
    }
#endif

    for( ; i < num_vertices; ++i )
    {
        int z = screen_vertices_z[i];

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

/* ---- Fused AVX2: ortho + FOV + z-div in one pass ---- */
static inline void
project_vertices_array_fused_avx2(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int near_plane_z,
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

    __m256i v_near = _mm256_set1_epi32(near_plane_z);
    __m256i v_mid = _mm256_set1_epi32(model_mid_z);
    __m256i v_neg5000 = _mm256_set1_epi32(-5000);
    __m256i v_neg5001 = _mm256_set1_epi32(-5001);

    int i = 0;
    for( ; i + 8 <= num_vertices; i += 8 )
    {
        __m256i xv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_x[i]));
        __m256i yv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_y[i]));
        __m256i zv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_z[i]));

        __m256i cos_model_yaw_v8 = _mm256_set1_epi32(cos_model_yaw);
        __m256i sin_model_yaw_v8 = _mm256_set1_epi32(sin_model_yaw);

        __m256i x_rotated = _mm256_add_epi32(
            _mm256_mullo_epi32(xv8, cos_model_yaw_v8), _mm256_mullo_epi32(zv8, sin_model_yaw_v8));
        x_rotated = _mm256_srai_epi32(x_rotated, 16);
        __m256i z_rotated = _mm256_sub_epi32(
            _mm256_mullo_epi32(zv8, cos_model_yaw_v8), _mm256_mullo_epi32(xv8, sin_model_yaw_v8));
        z_rotated = _mm256_srai_epi32(z_rotated, 16);

        x_rotated = _mm256_add_epi32(x_rotated, _mm256_set1_epi32(scene_x));
        __m256i y_rotated = _mm256_add_epi32(yv8, _mm256_set1_epi32(scene_y));
        z_rotated = _mm256_add_epi32(z_rotated, _mm256_set1_epi32(scene_z));

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

        _mm256_storeu_si256((__m256i*)&orthographic_vertices_x[i], x_scene);
        _mm256_storeu_si256((__m256i*)&orthographic_vertices_y[i], y_scene);
        _mm256_storeu_si256((__m256i*)&orthographic_vertices_z[i], z_scene_final);

        __m256i cot_fov_half_ish15_v8 = _mm256_set1_epi32(cot_fov_half_ish15);
        __m256i xfov_ish15 = _mm256_mullo_epi32(x_scene, cot_fov_half_ish15_v8);
        __m256i yfov_ish15 = _mm256_mullo_epi32(y_scene, cot_fov_half_ish15_v8);
        __m256i x_scaled = _mm256_srai_epi32(xfov_ish15, 6);
        __m256i y_scaled = _mm256_srai_epi32(yfov_ish15, 6);

        __m256i vscreen_z;
        __m256i final_x;
        __m256i final_y;
        projection_zdiv_avx2_apply(
            z_scene_final,
            x_scaled,
            y_scaled,
            v_near,
            v_mid,
            v_neg5000,
            v_neg5001,
            &vscreen_z,
            &final_x,
            &final_y);
        _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], vscreen_z);
        _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], final_x);
        _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], final_y);
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

        int screen_x = x_scene * cot_fov_half_ish15;
        int screen_y = y_scene * cot_fov_half_ish15;
        screen_x >>= 6;
        screen_y >>= 6;

        screen_vertices_z[i] = z_final_scene - model_mid_z;
        if( z_final_scene < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = screen_y;
        }
        else
        {
            screen_vertices_x[i] = screen_x / z_final_scene;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_y / z_final_scene;
        }
    }
}

static inline void
project_vertices_array_fused_noyaw_avx2(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_mid_z,
    int near_plane_z,
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

    __m256i v_near = _mm256_set1_epi32(near_plane_z);
    __m256i v_mid = _mm256_set1_epi32(model_mid_z);
    __m256i v_neg5000 = _mm256_set1_epi32(-5000);
    __m256i v_neg5001 = _mm256_set1_epi32(-5001);

    int i = 0;
    for( ; i + 8 <= num_vertices; i += 8 )
    {
        __m256i xv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_x[i]));
        __m256i yv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_y[i]));
        __m256i zv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_z[i]));

        __m256i x_rotated = _mm256_add_epi32(xv8, _mm256_set1_epi32(scene_x));
        __m256i y_rotated = _mm256_add_epi32(yv8, _mm256_set1_epi32(scene_y));
        __m256i z_rotated = _mm256_add_epi32(zv8, _mm256_set1_epi32(scene_z));

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

        _mm256_storeu_si256((__m256i*)&orthographic_vertices_x[i], x_scene);
        _mm256_storeu_si256((__m256i*)&orthographic_vertices_y[i], y_scene);
        _mm256_storeu_si256((__m256i*)&orthographic_vertices_z[i], z_scene_final);

        __m256i cot_fov_half_ish15_v8 = _mm256_set1_epi32(cot_fov_half_ish15);
        __m256i xfov_ish15 = _mm256_mullo_epi32(x_scene, cot_fov_half_ish15_v8);
        __m256i yfov_ish15 = _mm256_mullo_epi32(y_scene, cot_fov_half_ish15_v8);
        __m256i x_scaled = _mm256_srai_epi32(xfov_ish15, 6);
        __m256i y_scaled = _mm256_srai_epi32(yfov_ish15, 6);

        __m256i vscreen_z;
        __m256i final_x;
        __m256i final_y;
        projection_zdiv_avx2_apply(
            z_scene_final,
            x_scaled,
            y_scaled,
            v_near,
            v_mid,
            v_neg5000,
            v_neg5001,
            &vscreen_z,
            &final_x,
            &final_y);
        _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], vscreen_z);
        _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], final_x);
        _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], final_y);
    }

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

        int screen_x = x_scene * cot_fov_half_ish15;
        int screen_y = y_scene * cot_fov_half_ish15;
        screen_x >>= 6;
        screen_y >>= 6;

        screen_vertices_z[i] = z_final_scene - model_mid_z;
        if( z_final_scene < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = screen_y;
        }
        else
        {
            screen_vertices_x[i] = screen_x / z_final_scene;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_y / z_final_scene;
        }
    }
}

static inline void
project_vertices_array_fused(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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
        project_vertices_array_fused_avx2(
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
            model_mid_z,
            near_plane_z,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
    else
    {
        project_vertices_array_fused_noyaw_avx2(
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
            model_mid_z,
            near_plane_z,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
}

static inline void
project_vertices_array_fused_avx2_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int near_plane_z,
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

    __m256i v_near = _mm256_set1_epi32(near_plane_z);
    __m256i v_mid = _mm256_set1_epi32(model_mid_z);
    __m256i v_neg5000 = _mm256_set1_epi32(-5000);
    __m256i v_neg5001 = _mm256_set1_epi32(-5001);

    int i = 0;
    for( ; i + 8 <= num_vertices; i += 8 )
    {
        __m256i xv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_x[i]));
        __m256i yv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_y[i]));
        __m256i zv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_z[i]));

        __m256i cos_model_yaw_v8 = _mm256_set1_epi32(cos_model_yaw);
        __m256i sin_model_yaw_v8 = _mm256_set1_epi32(sin_model_yaw);

        __m256i x_rotated = _mm256_add_epi32(
            _mm256_mullo_epi32(xv8, cos_model_yaw_v8), _mm256_mullo_epi32(zv8, sin_model_yaw_v8));
        x_rotated = _mm256_srai_epi32(x_rotated, 16);
        __m256i z_rotated = _mm256_sub_epi32(
            _mm256_mullo_epi32(zv8, cos_model_yaw_v8), _mm256_mullo_epi32(xv8, sin_model_yaw_v8));
        z_rotated = _mm256_srai_epi32(z_rotated, 16);

        x_rotated = _mm256_add_epi32(x_rotated, _mm256_set1_epi32(scene_x));
        __m256i y_rotated = _mm256_add_epi32(yv8, _mm256_set1_epi32(scene_y));
        z_rotated = _mm256_add_epi32(z_rotated, _mm256_set1_epi32(scene_z));

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

        __m256i cot_fov_half_ish15_v8 = _mm256_set1_epi32(cot_fov_half_ish15);
        __m256i xfov_ish15 = _mm256_mullo_epi32(x_scene, cot_fov_half_ish15_v8);
        __m256i yfov_ish15 = _mm256_mullo_epi32(y_scene, cot_fov_half_ish15_v8);
        __m256i x_scaled = _mm256_srai_epi32(xfov_ish15, 6);
        __m256i y_scaled = _mm256_srai_epi32(yfov_ish15, 6);

        __m256i vscreen_z;
        __m256i final_x;
        __m256i final_y;
        projection_zdiv_avx2_apply(
            z_scene_final,
            x_scaled,
            y_scaled,
            v_near,
            v_mid,
            v_neg5000,
            v_neg5001,
            &vscreen_z,
            &final_x,
            &final_y);
        _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], vscreen_z);
        _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], final_x);
        _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], final_y);
    }

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

        int screen_x = x_scene * cot_fov_half_ish15;
        int screen_y = y_scene * cot_fov_half_ish15;
        screen_x >>= 6;
        screen_y >>= 6;

        screen_vertices_z[i] = z_final_scene - model_mid_z;
        if( z_final_scene < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = screen_y;
        }
        else
        {
            screen_vertices_x[i] = screen_x / z_final_scene;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_y / z_final_scene;
        }
    }
}

static inline void
project_vertices_array_fused_noyaw_avx2_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_mid_z,
    int near_plane_z,
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

    __m256i v_near = _mm256_set1_epi32(near_plane_z);
    __m256i v_mid = _mm256_set1_epi32(model_mid_z);
    __m256i v_neg5000 = _mm256_set1_epi32(-5000);
    __m256i v_neg5001 = _mm256_set1_epi32(-5001);

    int i = 0;
    for( ; i + 8 <= num_vertices; i += 8 )
    {
        __m256i xv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_x[i]));
        __m256i yv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_y[i]));
        __m256i zv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_z[i]));

        __m256i x_rotated = _mm256_add_epi32(xv8, _mm256_set1_epi32(scene_x));
        __m256i y_rotated = _mm256_add_epi32(yv8, _mm256_set1_epi32(scene_y));
        __m256i z_rotated = _mm256_add_epi32(zv8, _mm256_set1_epi32(scene_z));

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

        __m256i cot_fov_half_ish15_v8 = _mm256_set1_epi32(cot_fov_half_ish15);
        __m256i xfov_ish15 = _mm256_mullo_epi32(x_scene, cot_fov_half_ish15_v8);
        __m256i yfov_ish15 = _mm256_mullo_epi32(y_scene, cot_fov_half_ish15_v8);
        __m256i x_scaled = _mm256_srai_epi32(xfov_ish15, 6);
        __m256i y_scaled = _mm256_srai_epi32(yfov_ish15, 6);

        __m256i vscreen_z;
        __m256i final_x;
        __m256i final_y;
        projection_zdiv_avx2_apply(
            z_scene_final,
            x_scaled,
            y_scaled,
            v_near,
            v_mid,
            v_neg5000,
            v_neg5001,
            &vscreen_z,
            &final_x,
            &final_y);
        _mm256_storeu_si256((__m256i*)&screen_vertices_z[i], vscreen_z);
        _mm256_storeu_si256((__m256i*)&screen_vertices_x[i], final_x);
        _mm256_storeu_si256((__m256i*)&screen_vertices_y[i], final_y);
    }

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

        int screen_x = x_scene * cot_fov_half_ish15;
        int screen_y = y_scene * cot_fov_half_ish15;
        screen_x >>= 6;
        screen_y >>= 6;

        screen_vertices_z[i] = z_final_scene - model_mid_z;
        if( z_final_scene < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = screen_y;
        }
        else
        {
            screen_vertices_x[i] = screen_x / z_final_scene;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_y / z_final_scene;
        }
    }
}

static inline void
project_vertices_array_fused_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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
        project_vertices_array_fused_avx2_notex(
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            model_yaw,
            model_mid_z,
            near_plane_z,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
    else
    {
        project_vertices_array_fused_noyaw_avx2_notex(
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            model_mid_z,
            near_plane_z,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
}
#endif

#endif /* PROJECTION16_SIMD_AVX_U_C */
