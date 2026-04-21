#ifndef PROJECTION_SIMD_SSE41_U_C
#define PROJECTION_SIMD_SSE41_U_C

#if defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "sse2_41compat.h"
#include "projection_zdiv_simd.sse41.u.c"
#define PROJECTION_ZDIV_SSE_APPLY projection_zdiv_sse41_apply

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

static inline void
project_vertices_array_sse_notex(
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

        // store z_final to screen_vertices_z as temporary
        _mm_storeu_si128((__m128i*)&screen_vertices_z[i], z_scene_final);

        __m128i cot_fov_half_ish15_v4 = _mm_set1_epi32(cot_fov_half_ish15);

        __m128i xfov_ish15 = mullo_epi32_sse(x_scene, cot_fov_half_ish15_v4);
        __m128i yfov_ish15 = mullo_epi32_sse(y_scene, cot_fov_half_ish15_v4);

        __m128i x_scaled = _mm_srai_epi32(xfov_ish15, 6);
        __m128i y_scaled = _mm_srai_epi32(yfov_ish15, 6);

        _mm_storeu_si128((__m128i*)&screen_vertices_x[i], x_scaled);
        _mm_storeu_si128((__m128i*)&screen_vertices_y[i], y_scaled);
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

        screen_vertices_z[i] = z_final_scene;

        int screen_x = (x_scene * cot_fov_half_ish15) >> 15;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 15;

        screen_x = SCALE_UNIT(screen_x);
        screen_y = SCALE_UNIT(screen_y);

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

static inline void
project_vertices_array_noyaw_sse_notex(
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

        _mm_storeu_si128((__m128i*)&screen_vertices_z[i], z_scene_final);

        __m128i cot_fov_half_ish15_v4 = _mm_set1_epi32(cot_fov_half_ish15);

        __m128i xfov_ish15 = mullo_epi32_sse(x_scene, cot_fov_half_ish15_v4);
        __m128i yfov_ish15 = mullo_epi32_sse(y_scene, cot_fov_half_ish15_v4);

        __m128i x_scaled = _mm_srai_epi32(xfov_ish15, 6);
        __m128i y_scaled = _mm_srai_epi32(yfov_ish15, 6);

        _mm_storeu_si128((__m128i*)&screen_vertices_x[i], x_scaled);
        _mm_storeu_si128((__m128i*)&screen_vertices_y[i], y_scaled);
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

        screen_vertices_z[i] = z_final_scene;

        int screen_x = (x_scene * cot_fov_half_ish15) >> 15;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 15;

        screen_x = SCALE_UNIT(screen_x);
        screen_y = SCALE_UNIT(screen_y);

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

static inline void
project_vertices_array_notex(
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
        project_vertices_array_sse_notex(
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
        project_vertices_array_noyaw_sse_notex(
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

    // Second pass: z-division. screen_vertices_z currently holds raw z_final from the inner kernel.
    const int vsteps = 4;
    int i = 0;

#define SSE_FLOAT_RECIP_DIV_NOTEX
#ifdef SSE_FLOAT_RECIP_DIV_NOTEX
    for( ; i + vsteps - 1 < num_vertices; i += vsteps )
    {
        int vz_arr[4] = { screen_vertices_z[i],
                          screen_vertices_z[i + 1],
                          screen_vertices_z[i + 2],
                          screen_vertices_z[i + 3] };
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

        __m128 rcp_z = _mm_rcp_ps(fz);
        __m128 fdivx = _mm_mul_ps(fx, rcp_z);
        __m128 fdivy = _mm_mul_ps(fy, rcp_z);

        float fdivx_arr[4], fdivy_arr[4];
        _mm_storeu_ps(fdivx_arr, fdivx);
        _mm_storeu_ps(fdivy_arr, fdivy);

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
#endif

    for( ; i < num_vertices; i++ )
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

/* ---- Fused SSE2/SSE4.1: ortho + FOV + z-div ---- */
static inline void
project_vertices_array_fused_sse(
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

    __m128i v_near = _mm_set1_epi32(near_plane_z);
    __m128i v_mid = _mm_set1_epi32(model_mid_z);
    __m128i v_neg5000 = _mm_set1_epi32(-5000);
    __m128i v_neg5001 = _mm_set1_epi32(-5001);

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        __m128i xv4 = _mm_loadu_si128((__m128i*)&vertex_x[i]);
        __m128i yv4 = _mm_loadu_si128((__m128i*)&vertex_y[i]);
        __m128i zv4 = _mm_loadu_si128((__m128i*)&vertex_z[i]);

        __m128i cos_model_yaw_v4 = _mm_set1_epi32(cos_model_yaw);
        __m128i sin_model_yaw_v4 = _mm_set1_epi32(sin_model_yaw);

        __m128i x_rotated = _mm_add_epi32(
            mullo_epi32_sse(xv4, cos_model_yaw_v4), mullo_epi32_sse(zv4, sin_model_yaw_v4));
        x_rotated = _mm_srai_epi32(x_rotated, 16);
        __m128i z_rotated = _mm_sub_epi32(
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

        __m128i vscreen_z;
        __m128i final_x;
        __m128i final_y;
        PROJECTION_ZDIV_SSE_APPLY(
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
        _mm_storeu_si128((__m128i*)&screen_vertices_z[i], vscreen_z);
        _mm_storeu_si128((__m128i*)&screen_vertices_x[i], final_x);
        _mm_storeu_si128((__m128i*)&screen_vertices_y[i], final_y);
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

        orthographic_vertices_x[i] = x_scene;
        orthographic_vertices_y[i] = y_scene;
        orthographic_vertices_z[i] = z_final_scene;

        int screen_x = (x_scene * cot_fov_half_ish15) >> 15;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 15;
        screen_x = SCALE_UNIT(screen_x);
        screen_y = SCALE_UNIT(screen_y);

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
project_vertices_array_fused_noyaw_sse(
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

    __m128i v_near = _mm_set1_epi32(near_plane_z);
    __m128i v_mid = _mm_set1_epi32(model_mid_z);
    __m128i v_neg5000 = _mm_set1_epi32(-5000);
    __m128i v_neg5001 = _mm_set1_epi32(-5001);

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        __m128i xv4 = _mm_loadu_si128((__m128i*)&vertex_x[i]);
        __m128i yv4 = _mm_loadu_si128((__m128i*)&vertex_y[i]);
        __m128i zv4 = _mm_loadu_si128((__m128i*)&vertex_z[i]);

        __m128i x_rotated = _mm_add_epi32(xv4, _mm_set1_epi32(scene_x));
        __m128i y_rotated = _mm_add_epi32(yv4, _mm_set1_epi32(scene_y));
        __m128i z_rotated = _mm_add_epi32(zv4, _mm_set1_epi32(scene_z));

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

        __m128i vscreen_z;
        __m128i final_x;
        __m128i final_y;
        PROJECTION_ZDIV_SSE_APPLY(
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
        _mm_storeu_si128((__m128i*)&screen_vertices_z[i], vscreen_z);
        _mm_storeu_si128((__m128i*)&screen_vertices_x[i], final_x);
        _mm_storeu_si128((__m128i*)&screen_vertices_y[i], final_y);
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

        int screen_x = (x_scene * cot_fov_half_ish15) >> 15;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 15;
        screen_x = SCALE_UNIT(screen_x);
        screen_y = SCALE_UNIT(screen_y);

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
        project_vertices_array_fused_sse(
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
        project_vertices_array_fused_noyaw_sse(
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
project_vertices_array_fused_sse_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
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

    __m128i v_near = _mm_set1_epi32(near_plane_z);
    __m128i v_mid = _mm_set1_epi32(model_mid_z);
    __m128i v_neg5000 = _mm_set1_epi32(-5000);
    __m128i v_neg5001 = _mm_set1_epi32(-5001);

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        __m128i xv4 = _mm_loadu_si128((__m128i*)&vertex_x[i]);
        __m128i yv4 = _mm_loadu_si128((__m128i*)&vertex_y[i]);
        __m128i zv4 = _mm_loadu_si128((__m128i*)&vertex_z[i]);

        __m128i cos_model_yaw_v4 = _mm_set1_epi32(cos_model_yaw);
        __m128i sin_model_yaw_v4 = _mm_set1_epi32(sin_model_yaw);

        __m128i x_rotated = _mm_add_epi32(
            mullo_epi32_sse(xv4, cos_model_yaw_v4), mullo_epi32_sse(zv4, sin_model_yaw_v4));
        x_rotated = _mm_srai_epi32(x_rotated, 16);
        __m128i z_rotated = _mm_sub_epi32(
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

        __m128i cot_fov_half_ish15_v4 = _mm_set1_epi32(cot_fov_half_ish15);
        __m128i xfov_ish15 = mullo_epi32_sse(x_scene, cot_fov_half_ish15_v4);
        __m128i yfov_ish15 = mullo_epi32_sse(y_scene, cot_fov_half_ish15_v4);
        __m128i x_scaled = _mm_srai_epi32(xfov_ish15, 6);
        __m128i y_scaled = _mm_srai_epi32(yfov_ish15, 6);

        __m128i vscreen_z;
        __m128i final_x;
        __m128i final_y;
        PROJECTION_ZDIV_SSE_APPLY(
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
        _mm_storeu_si128((__m128i*)&screen_vertices_z[i], vscreen_z);
        _mm_storeu_si128((__m128i*)&screen_vertices_x[i], final_x);
        _mm_storeu_si128((__m128i*)&screen_vertices_y[i], final_y);
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

        int screen_x = (x_scene * cot_fov_half_ish15) >> 15;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 15;
        screen_x = SCALE_UNIT(screen_x);
        screen_y = SCALE_UNIT(screen_y);

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
project_vertices_array_fused_noyaw_sse_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
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

    __m128i v_near = _mm_set1_epi32(near_plane_z);
    __m128i v_mid = _mm_set1_epi32(model_mid_z);
    __m128i v_neg5000 = _mm_set1_epi32(-5000);
    __m128i v_neg5001 = _mm_set1_epi32(-5001);

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        __m128i xv4 = _mm_loadu_si128((__m128i*)&vertex_x[i]);
        __m128i yv4 = _mm_loadu_si128((__m128i*)&vertex_y[i]);
        __m128i zv4 = _mm_loadu_si128((__m128i*)&vertex_z[i]);

        __m128i x_rotated = _mm_add_epi32(xv4, _mm_set1_epi32(scene_x));
        __m128i y_rotated = _mm_add_epi32(yv4, _mm_set1_epi32(scene_y));
        __m128i z_rotated = _mm_add_epi32(zv4, _mm_set1_epi32(scene_z));

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

        __m128i cot_fov_half_ish15_v4 = _mm_set1_epi32(cot_fov_half_ish15);
        __m128i xfov_ish15 = mullo_epi32_sse(x_scene, cot_fov_half_ish15_v4);
        __m128i yfov_ish15 = mullo_epi32_sse(y_scene, cot_fov_half_ish15_v4);
        __m128i x_scaled = _mm_srai_epi32(xfov_ish15, 6);
        __m128i y_scaled = _mm_srai_epi32(yfov_ish15, 6);

        __m128i vscreen_z;
        __m128i final_x;
        __m128i final_y;
        PROJECTION_ZDIV_SSE_APPLY(
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
        _mm_storeu_si128((__m128i*)&screen_vertices_z[i], vscreen_z);
        _mm_storeu_si128((__m128i*)&screen_vertices_x[i], final_x);
        _mm_storeu_si128((__m128i*)&screen_vertices_y[i], final_y);
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

        int screen_x = (x_scene * cot_fov_half_ish15) >> 15;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 15;
        screen_x = SCALE_UNIT(screen_x);
        screen_y = SCALE_UNIT(screen_y);

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
        project_vertices_array_fused_sse_notex(
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
        project_vertices_array_fused_noyaw_sse_notex(
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

#endif /* PROJECTION_SIMD_SSE41_U_C */
