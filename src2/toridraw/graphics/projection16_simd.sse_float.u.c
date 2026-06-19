#ifndef PROJECTION16_SIMD_SSE_FLOAT_U_C
#define PROJECTION16_SIMD_SSE_FLOAT_U_C

#if defined(__SSE__) && !defined(SSE_DISABLED)
#include <xmmintrin.h>

static inline void
project_vertices_array_sse_float(
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
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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

static inline void
project_vertices_array_sse_float_notex(
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

        __m128 x_scene = _mm_add_ps(
            _mm_mul_ps(x_trans, v_cos_camera_yaw), _mm_mul_ps(z_trans, v_sin_camera_yaw));
        __m128 z_scene = _mm_sub_ps(
            _mm_mul_ps(z_trans, v_cos_camera_yaw), _mm_mul_ps(x_trans, v_sin_camera_yaw));

        __m128 y_scene = _mm_sub_ps(
            _mm_mul_ps(y_trans, v_cos_camera_pitch), _mm_mul_ps(z_scene, v_sin_camera_pitch));
        __m128 z_final = _mm_add_ps(
            _mm_mul_ps(y_trans, v_sin_camera_pitch), _mm_mul_ps(z_scene, v_cos_camera_pitch));

        // store z_final to screen_vertices_z as temporary
        float z_final_arr[4];
        _mm_storeu_ps(z_final_arr, z_final);
        screen_vertices_z[i] = (int)z_final_arr[0];
        screen_vertices_z[i + 1] = (int)z_final_arr[1];
        screen_vertices_z[i + 2] = (int)z_final_arr[2];
        screen_vertices_z[i + 3] = (int)z_final_arr[3];

        __m128 x_proj = _mm_mul_ps(x_scene, v_cot_fov);
        __m128 y_proj = _mm_mul_ps(y_scene, v_cot_fov);

        __m128 x_final_f = _mm_mul_ps(x_proj, v_512);
        __m128 y_final_f = _mm_mul_ps(y_proj, v_512);

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

    for( ; i < num_vertices; i++ )
    {
        float x = (float)vertex_x[i];
        float y = (float)vertex_y[i];
        float z = (float)vertex_z[i];

        float cos_model_yaw_f = g_cos_table[model_yaw] * inv_scale;
        float sin_model_yaw_f = g_sin_table[model_yaw] * inv_scale;
        float x_rotated = x * cos_model_yaw_f + z * sin_model_yaw_f;
        float z_rotated = z * cos_model_yaw_f - x * sin_model_yaw_f;

        float x_trans = x_rotated + (float)scene_x;
        float y_trans = y + (float)scene_y;
        float z_trans = z_rotated + (float)scene_z;

        float cos_camera_yaw_f = g_cos_table[camera_yaw] * inv_scale;
        float sin_camera_yaw_f = g_sin_table[camera_yaw] * inv_scale;
        float x_scene = x_trans * cos_camera_yaw_f + z_trans * sin_camera_yaw_f;
        float z_scene = z_trans * cos_camera_yaw_f - x_trans * sin_camera_yaw_f;

        float cos_camera_pitch_f = g_cos_table[camera_pitch] * inv_scale;
        float sin_camera_pitch_f = g_sin_table[camera_pitch] * inv_scale;
        float y_scene = y_trans * cos_camera_pitch_f - z_scene * sin_camera_pitch_f;
        float z_final = y_trans * sin_camera_pitch_f + z_scene * cos_camera_pitch_f;

        screen_vertices_z[i] = (int)z_final;

        float cot_fov_f = cot_fov_half_ish16 * inv_scale;
        float x_proj = x_scene * cot_fov_f;
        float y_proj = y_scene * cot_fov_f;

        screen_vertices_x[i] = SCALE_UNIT((int)x_proj);
        screen_vertices_y[i] = SCALE_UNIT((int)y_proj);
    }
}

static inline void
project_vertices_array_sse_float_noyaw_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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

        __m128 x_scene = _mm_add_ps(
            _mm_mul_ps(x_trans, v_cos_camera_yaw), _mm_mul_ps(z_trans, v_sin_camera_yaw));
        __m128 z_scene = _mm_sub_ps(
            _mm_mul_ps(z_trans, v_cos_camera_yaw), _mm_mul_ps(x_trans, v_sin_camera_yaw));

        __m128 y_scene = _mm_sub_ps(
            _mm_mul_ps(y_trans, v_cos_camera_pitch), _mm_mul_ps(z_scene, v_sin_camera_pitch));
        __m128 z_final = _mm_add_ps(
            _mm_mul_ps(y_trans, v_sin_camera_pitch), _mm_mul_ps(z_scene, v_cos_camera_pitch));

        float z_final_arr[4];
        _mm_storeu_ps(z_final_arr, z_final);
        screen_vertices_z[i] = (int)z_final_arr[0];
        screen_vertices_z[i + 1] = (int)z_final_arr[1];
        screen_vertices_z[i + 2] = (int)z_final_arr[2];
        screen_vertices_z[i + 3] = (int)z_final_arr[3];

        __m128 x_proj = _mm_mul_ps(x_scene, v_cot_fov);
        __m128 y_proj = _mm_mul_ps(y_scene, v_cot_fov);

        __m128 x_final_f = _mm_mul_ps(x_proj, v_512);
        __m128 y_final_f = _mm_mul_ps(y_proj, v_512);

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

    for( ; i < num_vertices; i++ )
    {
        float x = (float)vertex_x[i];
        float y = (float)vertex_y[i];
        float z = (float)vertex_z[i];

        float x_rotated = x;
        float z_rotated = z;

        float x_trans = x_rotated + (float)scene_x;
        float y_trans = y + (float)scene_y;
        float z_trans = z_rotated + (float)scene_z;

        float cos_camera_yaw_f = g_cos_table[camera_yaw] * inv_scale;
        float sin_camera_yaw_f = g_sin_table[camera_yaw] * inv_scale;
        float x_scene = x_trans * cos_camera_yaw_f + z_trans * sin_camera_yaw_f;
        float z_scene = z_trans * cos_camera_yaw_f - x_trans * sin_camera_yaw_f;

        float cos_camera_pitch_f = g_cos_table[camera_pitch] * inv_scale;
        float sin_camera_pitch_f = g_sin_table[camera_pitch] * inv_scale;
        float y_scene = y_trans * cos_camera_pitch_f - z_scene * sin_camera_pitch_f;
        float z_final = y_trans * sin_camera_pitch_f + z_scene * cos_camera_pitch_f;

        screen_vertices_z[i] = (int)z_final;

        float cot_fov_f = cot_fov_half_ish16 * inv_scale;
        float x_proj = x_scene * cot_fov_f;
        float y_proj = y_scene * cot_fov_f;

        screen_vertices_x[i] = SCALE_UNIT((int)x_proj);
        screen_vertices_y[i] = SCALE_UNIT((int)y_proj);
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
        project_vertices_array_sse_float_notex(
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
        project_vertices_array_sse_float_noyaw_notex(
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

/* Fused SSE1 float path (ortho + FOV + z-div without intermediate screen store) */

static inline void
project_vertices_array_fused_sse_float(
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

        int vz_arr[4] = {
            (int)z_final_arr[0], (int)z_final_arr[1], (int)z_final_arr[2], (int)z_final_arr[3] };
        int vsx_arr[4] = {
            (int)x_final_arr[0], (int)x_final_arr[1], (int)x_final_arr[2], (int)x_final_arr[3] };
        int vsy_arr[4] = {
            (int)y_final_arr[0], (int)y_final_arr[1], (int)y_final_arr[2], (int)y_final_arr[3] };

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

        int sx = SCALE_UNIT((int)x_proj);
        int sy = SCALE_UNIT((int)y_proj);
        int zi = (int)z_final;

        screen_vertices_z[i] = zi - model_mid_z;
        if( zi < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = sy;
        }
        else
        {
            screen_vertices_x[i] = sx / zi;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = sy / zi;
        }
    }
}

static inline void
project_vertices_array_fused_sse_float_noyaw(
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

        int vz_arr[4] = {
            (int)z_final_arr[0], (int)z_final_arr[1], (int)z_final_arr[2], (int)z_final_arr[3] };
        int vsx_arr[4] = {
            (int)x_final_arr[0], (int)x_final_arr[1], (int)x_final_arr[2], (int)x_final_arr[3] };
        int vsy_arr[4] = {
            (int)y_final_arr[0], (int)y_final_arr[1], (int)y_final_arr[2], (int)y_final_arr[3] };

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

        int sx = SCALE_UNIT((int)x_proj);
        int sy = SCALE_UNIT((int)y_proj);
        int zi = (int)z_final;

        screen_vertices_z[i] = zi - model_mid_z;
        if( zi < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = sy;
        }
        else
        {
            screen_vertices_x[i] = sx / zi;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = sy / zi;
        }
    }
}

static inline void
project_vertices_array_fused_sse_float_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_mid_z,
    int near_plane_z,
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

        __m128 x_scene = _mm_add_ps(
            _mm_mul_ps(x_trans, v_cos_camera_yaw), _mm_mul_ps(z_trans, v_sin_camera_yaw));
        __m128 z_scene = _mm_sub_ps(
            _mm_mul_ps(z_trans, v_cos_camera_yaw), _mm_mul_ps(x_trans, v_sin_camera_yaw));

        __m128 y_scene = _mm_sub_ps(
            _mm_mul_ps(y_trans, v_cos_camera_pitch), _mm_mul_ps(z_scene, v_sin_camera_pitch));
        __m128 z_final = _mm_add_ps(
            _mm_mul_ps(y_trans, v_sin_camera_pitch), _mm_mul_ps(z_scene, v_cos_camera_pitch));

        // store z_final to screen_vertices_z as temporary
        float z_final_arr[4];
        _mm_storeu_ps(z_final_arr, z_final);
        screen_vertices_z[i] = (int)z_final_arr[0];
        screen_vertices_z[i + 1] = (int)z_final_arr[1];
        screen_vertices_z[i + 2] = (int)z_final_arr[2];
        screen_vertices_z[i + 3] = (int)z_final_arr[3];

        __m128 x_proj = _mm_mul_ps(x_scene, v_cot_fov);
        __m128 y_proj = _mm_mul_ps(y_scene, v_cot_fov);

        __m128 x_final_f = _mm_mul_ps(x_proj, v_512);
        __m128 y_final_f = _mm_mul_ps(y_proj, v_512);

        float x_final_arr[4], y_final_arr[4];
        _mm_storeu_ps(x_final_arr, x_final_f);
        _mm_storeu_ps(y_final_arr, y_final_f);

        int vz_arr[4] = {
            (int)z_final_arr[0], (int)z_final_arr[1], (int)z_final_arr[2], (int)z_final_arr[3] };
        int vsx_arr[4] = {
            (int)x_final_arr[0], (int)x_final_arr[1], (int)x_final_arr[2], (int)x_final_arr[3] };
        int vsy_arr[4] = {
            (int)y_final_arr[0], (int)y_final_arr[1], (int)y_final_arr[2], (int)y_final_arr[3] };

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

    for( ; i < num_vertices; i++ )
    {
        float x = (float)vertex_x[i];
        float y = (float)vertex_y[i];
        float z = (float)vertex_z[i];

        float cos_model_yaw_f = g_cos_table[model_yaw] * inv_scale;
        float sin_model_yaw_f = g_sin_table[model_yaw] * inv_scale;
        float x_rotated = x * cos_model_yaw_f + z * sin_model_yaw_f;
        float z_rotated = z * cos_model_yaw_f - x * sin_model_yaw_f;

        float x_trans = x_rotated + (float)scene_x;
        float y_trans = y + (float)scene_y;
        float z_trans = z_rotated + (float)scene_z;

        float cos_camera_yaw_f = g_cos_table[camera_yaw] * inv_scale;
        float sin_camera_yaw_f = g_sin_table[camera_yaw] * inv_scale;
        float x_scene = x_trans * cos_camera_yaw_f + z_trans * sin_camera_yaw_f;
        float z_scene = z_trans * cos_camera_yaw_f - x_trans * sin_camera_yaw_f;

        float cos_camera_pitch_f = g_cos_table[camera_pitch] * inv_scale;
        float sin_camera_pitch_f = g_sin_table[camera_pitch] * inv_scale;
        float y_scene = y_trans * cos_camera_pitch_f - z_scene * sin_camera_pitch_f;
        float z_final = y_trans * sin_camera_pitch_f + z_scene * cos_camera_pitch_f;

        screen_vertices_z[i] = (int)z_final;

        float cot_fov_f = cot_fov_half_ish16 * inv_scale;
        float x_proj = x_scene * cot_fov_f;
        float y_proj = y_scene * cot_fov_f;

        int sx = SCALE_UNIT((int)x_proj);
        int sy = SCALE_UNIT((int)y_proj);
        int zi = (int)z_final;

        screen_vertices_z[i] = zi - model_mid_z;
        if( zi < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = sy;
        }
        else
        {
            screen_vertices_x[i] = sx / zi;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = sy / zi;
        }
    }
}

static inline void
project_vertices_array_fused_sse_float_noyaw_notex(
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

        __m128 x_scene = _mm_add_ps(
            _mm_mul_ps(x_trans, v_cos_camera_yaw), _mm_mul_ps(z_trans, v_sin_camera_yaw));
        __m128 z_scene = _mm_sub_ps(
            _mm_mul_ps(z_trans, v_cos_camera_yaw), _mm_mul_ps(x_trans, v_sin_camera_yaw));

        __m128 y_scene = _mm_sub_ps(
            _mm_mul_ps(y_trans, v_cos_camera_pitch), _mm_mul_ps(z_scene, v_sin_camera_pitch));
        __m128 z_final = _mm_add_ps(
            _mm_mul_ps(y_trans, v_sin_camera_pitch), _mm_mul_ps(z_scene, v_cos_camera_pitch));

        float z_final_arr[4];
        _mm_storeu_ps(z_final_arr, z_final);
        screen_vertices_z[i] = (int)z_final_arr[0];
        screen_vertices_z[i + 1] = (int)z_final_arr[1];
        screen_vertices_z[i + 2] = (int)z_final_arr[2];
        screen_vertices_z[i + 3] = (int)z_final_arr[3];

        __m128 x_proj = _mm_mul_ps(x_scene, v_cot_fov);
        __m128 y_proj = _mm_mul_ps(y_scene, v_cot_fov);

        __m128 x_final_f = _mm_mul_ps(x_proj, v_512);
        __m128 y_final_f = _mm_mul_ps(y_proj, v_512);

        float x_final_arr[4], y_final_arr[4];
        _mm_storeu_ps(x_final_arr, x_final_f);
        _mm_storeu_ps(y_final_arr, y_final_f);

        int vz_arr[4] = {
            (int)z_final_arr[0], (int)z_final_arr[1], (int)z_final_arr[2], (int)z_final_arr[3] };
        int vsx_arr[4] = {
            (int)x_final_arr[0], (int)x_final_arr[1], (int)x_final_arr[2], (int)x_final_arr[3] };
        int vsy_arr[4] = {
            (int)y_final_arr[0], (int)y_final_arr[1], (int)y_final_arr[2], (int)y_final_arr[3] };

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

    for( ; i < num_vertices; i++ )
    {
        float x = (float)vertex_x[i];
        float y = (float)vertex_y[i];
        float z = (float)vertex_z[i];

        float x_rotated = x;
        float z_rotated = z;

        float x_trans = x_rotated + (float)scene_x;
        float y_trans = y + (float)scene_y;
        float z_trans = z_rotated + (float)scene_z;

        float cos_camera_yaw_f = g_cos_table[camera_yaw] * inv_scale;
        float sin_camera_yaw_f = g_sin_table[camera_yaw] * inv_scale;
        float x_scene = x_trans * cos_camera_yaw_f + z_trans * sin_camera_yaw_f;
        float z_scene = z_trans * cos_camera_yaw_f - x_trans * sin_camera_yaw_f;

        float cos_camera_pitch_f = g_cos_table[camera_pitch] * inv_scale;
        float sin_camera_pitch_f = g_sin_table[camera_pitch] * inv_scale;
        float y_scene = y_trans * cos_camera_pitch_f - z_scene * sin_camera_pitch_f;
        float z_final = y_trans * sin_camera_pitch_f + z_scene * cos_camera_pitch_f;

        screen_vertices_z[i] = (int)z_final;

        float cot_fov_f = cot_fov_half_ish16 * inv_scale;
        float x_proj = x_scene * cot_fov_f;
        float y_proj = y_scene * cot_fov_f;

        int sx = SCALE_UNIT((int)x_proj);
        int sy = SCALE_UNIT((int)y_proj);
        int zi = (int)z_final;

        screen_vertices_z[i] = zi - model_mid_z;
        if( zi < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = sy;
        }
        else
        {
            screen_vertices_x[i] = sx / zi;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = sy / zi;
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
        project_vertices_array_fused_sse_float(
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
    else
    {
        project_vertices_array_fused_sse_float_noyaw(
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
        project_vertices_array_fused_sse_float_notex(
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
        project_vertices_array_fused_sse_float_noyaw_notex(
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

#endif /* PROJECTION16_SIMD_SSE_FLOAT_U_C */
