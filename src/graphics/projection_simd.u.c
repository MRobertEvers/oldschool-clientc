// for( int i = 0; i < num_vertices; i++ )
// {
//     project_orthographic_fast(
//         &projected_vertex,
//         vertex_x[i],
//         vertex_y[i],
//         vertex_z[i],
//         model_yaw,
//         scene_x,
//         scene_y,
//         scene_z,
//         camera_pitch,
//         camera_yaw);

//     int x = projected_vertex.x;
//     int y = projected_vertex.y;
//     int z = projected_vertex.z;

//     x *= cot_fov_half_ish_scaled;
//     y *= cot_fov_half_ish_scaled;
//     x >>= 16 - scale_angle;
//     y >>= 16 - scale_angle;

//     // So we can increase x_bits_max to 11 by reducing the angle scale by 1.
//     int screen_x = SCALE_UNIT(x);
//     int screen_y = SCALE_UNIT(y);
//     // screen_x *= cot_fov_half_ish_scaled;
//     // screen_y *= cot_fov_half_ish_scaled;
//     // screen_x >>= 16 - scale_angle;
//     // screen_y >>= 16 - scale_angle;

//     // Set the projected triangle

//     orthographic_vertices_x[i] = projected_vertex.x;
//     orthographic_vertices_y[i] = projected_vertex.y;
//     orthographic_vertices_z[i] = projected_vertex.z;

//     screen_vertices_x[i] = screen_x;
//     screen_vertices_y[i] = screen_y;
//     screen_vertices_z[i] = z;
// }

#ifndef PROJECTION_SIMD_U_C
#define PROJECTION_SIMD_U_C

#include "projection.h"

#include <assert.h>
#include <immintrin.h>
#include <stdbool.h>
#include <stdint.h>

extern int g_tan_table[2048];
extern int g_cos_table[2048];
extern int g_sin_table[2048];

/**
 * Treats the camera as if it is at the origin (0, 0, 0)
 *
 * scene_x, scene_y, scene_z is the coordinates of the models origin relative to the camera.
//  */
// static inline void
// project_perspective_fast(
//     struct ProjectedVertex* projected_vertex,
//     int x,
//     int y,
//     int z,
//     int fov, // FOV in units of (2π/2048) radians
//     int near_clip)
// {
//     // Perspective projection with FOV

//     // z is the distance from the camera.
//     // It is a judgement call to say when to cull the triangle, but
//     // things you can consider are the average size of models.
//     // It is up to the caller to cull the triangle if it is too close or behind the camera.
//     // e.g. z <= 50
//     // We have to check that x and y are within a reasonable range (see math below)
//     // to avoid overflow.
//     static const int z_clip_bits = 5;
//     static const int clip_bits = 11;
//     // // clip_bits - z_clip_bits < 7; see math below
//     // // TODO: Frustrum culling should clip the inputs x's and y's.
//     // if( z < (1 << z_clip_bits)  || x < -(1 << clip_bits) || x > (1 << clip_bits) || y < -(1 <<
//     // clip_bits) || y > (1 << clip_bits)  )
//     // {
//     //     memset(projected_vertex, 0x00, sizeof(*projected_vertex));
//     //     projected_vertex->z = z;
//     //     projected_vertex->clipped = 1;
//     //     return;
//     // }

//     if( z < near_clip )
//     {
//         // memset(projected_vertex, 0x00, sizeof(*projected_vertex));
//         projected_vertex->z = z;
//         projected_vertex->clipped = 1;
//         return;
//     }

//     assert(z != 0);

//     // Calculate FOV scale based on the angle using sin/cos tables
//     // fov is in units of (2π/2048) radians
//     // For perspective projection, we need tan(fov/2)
//     // tan(x) = sin(x)/cos(x)
//     int fov_half = fov >> 1; // fov/2

//     // fov_scale = 1/tan(fov/2)
//     // cot(x) = 1/tan(x)
//     // cot(x) = tan(pi/2 - x)
//     // cot(x + pi) = cot(x)
//     // assert(fov_half < 1536);
//     int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];

//     static const int scale_angle = 1;
//     int cot_fov_half_ish_scaled = cot_fov_half_ish16 >> scale_angle;

//     // Apply FOV scaling to x and y coordinates

//     // unit scale 9, angle scale 16
//     // then 6 bits of valid x/z. (31 - 25 = 6), signed int.

//     // if valid x is -Screen_width/2 to Screen_width/2
//     // And the max resolution we allow is 1600 (either dimension)
//     // then x_bits_max = 10; because 2^10 = 1024 > (1600/2)

//     // If we have 6 bits of valid x then x_bits_max - z_clip_bits < 6
//     // i.e. x/z < 2^6 or x/z < 64

//     // Suppose we allow z > 16, so z_clip_bits = 4
//     // then x_bits_max < 10, so 2^9, which is 512

//     x *= cot_fov_half_ish_scaled;
//     y *= cot_fov_half_ish_scaled;
//     x >>= 16 - scale_angle;
//     y >>= 16 - scale_angle;

//     // So we can increase x_bits_max to 11 by reducing the angle scale by 1.
//     int screen_x = SCALE_UNIT(x) / z;
//     int screen_y = SCALE_UNIT(y) / z;
//     // screen_x *= cot_fov_half_ish_scaled;
//     // screen_y *= cot_fov_half_ish_scaled;
//     // screen_x >>= 16 - scale_angle;
//     // screen_y >>= 16 - scale_angle;

//     // Set the projected triangle
//     projected_vertex->x = screen_x;
//     projected_vertex->y = screen_y;
//     projected_vertex->z = z;
//     projected_vertex->clipped = 0;
// }

// static inline void
// project_orthographic_fast(
//     struct ProjectedVertex* projected_vertex,
//     int x,
//     int y,
//     int z,
//     int yaw,
//     int scene_x,
//     int scene_y,
//     int scene_z,
//     int camera_pitch,
//     int camera_yaw)
// {
//     int cos_camera_pitch = g_cos_table[camera_pitch];
//     int sin_camera_pitch = g_sin_table[camera_pitch];
//     int cos_camera_yaw = g_cos_table[camera_yaw];
//     int sin_camera_yaw = g_sin_table[camera_yaw];

//     int x_rotated = x;
//     int z_rotated = z;
//     if( yaw != 0 )
//     {
//         int sin_yaw = g_sin_table[yaw];
//         int cos_yaw = g_cos_table[yaw];
//         x_rotated = x * cos_yaw + z * sin_yaw;
//         x_rotated >>= 16;
//         z_rotated = z * cos_yaw - x * sin_yaw;
//         z_rotated >>= 16;
//     }

//     // Translate points relative to camera position
//     x_rotated += scene_x;
//     int y_rotated = y + scene_y;
//     z_rotated += scene_z;

//     // Apply perspective rotation
//     // First rotate around Y-axis (scene yaw)
//     int x_scene = x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw;
//     x_scene >>= 16;
//     int z_scene = z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw;
//     z_scene >>= 16;

//     // Then rotate around X-axis (scene pitch)
//     int y_scene = y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch;
//     y_scene >>= 16;
//     int z_final_scene = y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch;
//     z_final_scene >>= 16;

//     projected_vertex->x = x_scene;
//     projected_vertex->y = y_scene;
//     projected_vertex->z = z_final_scene;
// }

void
project_vertices_array_noyaw(
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
    int near_clip,
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

    static const int scale_angle = 1;
    int cot_fov_half_ish_scaled = cot_fov_half_ish16 >> scale_angle;

    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int sin_model_yaw = g_sin_table[model_yaw];
    int cos_model_yaw = g_cos_table[model_yaw];

    // Handle remaining vertices with scalar code
    int i = 0;
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

        orthographic_vertices_x[i] = x_scene;
        orthographic_vertices_y[i] = y_scene;
        orthographic_vertices_z[i] = z_final_scene;

        x *= cot_fov_half_ish_scaled;
        y *= cot_fov_half_ish_scaled;
        x >>= 15;
        y >>= 15;

        // So we can increase x_bits_max to 11 by reducing the angle scale by 1.
        int screen_x = (x << UNIT_SCALE_SHIFT);
        int screen_y = (y << UNIT_SCALE_SHIFT);

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
        screen_vertices_z[i] = z;
    }
}

void
project_vertices_array_simd(
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

#endif