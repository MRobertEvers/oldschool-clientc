#ifndef PROJECTION_SIMD_U_C
#define PROJECTION_SIMD_U_C

#include "dash_faceint.h"
#include "dash_vertexint.h"
#include "projection.h"
#include "projection.u.c"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

extern int g_tan_table[2048];
extern int g_cos_table[2048];
extern int g_sin_table[2048];

// #define NEON_DISABLED
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "projection_simd.neon.u.c"
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "projection_simd.avx.u.c"
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "projection_simd.sse41.u.c"
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "projection_simd.sse2.u.c"
#elif defined(__SSE__) && !defined(SSE_DISABLED)
#include "projection_simd.sse_float.u.c"
#else
#include "projection_simd.scalar.u.c"
#endif

/**
 * Project vertices array with full 6DOF support (pitch, yaw, roll for model and camera)
 * Uses the full project_orthographic function instead of project_orthographic_fast
 * This function is available for all platforms, regardless of SIMD support
 */
static inline void
project_vertices_array6(
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
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    int fov_half = camera_fov >> 1;
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex projected_vertex;

        // Use full 6DOF projection
        projected_vertex = project_orthographic(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
            model_yaw,
            model_roll,
            scene_x,
            scene_y,
            scene_z,
            camera_pitch,
            camera_yaw,
            camera_roll);

        int x = projected_vertex.x;
        int y = projected_vertex.y;
        int z = projected_vertex.z;

        orthographic_vertices_x[i] = x;
        orthographic_vertices_y[i] = y;
        orthographic_vertices_z[i] = z;

        // Apply perspective projection
        if( z < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = -5000;
            screen_vertices_z[i] = z - model_mid_z;
        }
        else
        {
            x *= cot_fov_half_ish15;
            y *= cot_fov_half_ish15;
            x >>= 15;
            y >>= 15;

            screen_vertices_x[i] = SCALE_UNIT(x) / z;
            screen_vertices_y[i] = SCALE_UNIT(y) / z;
            screen_vertices_z[i] = z - model_mid_z;
        }
    }
}

/**
 * Project vertices array with full 6DOF support, without saving orthographic coordinates.
 * Use for non-textured models that do not need camera-space x/y per vertex.
 */
static inline void
project_vertices_array6_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    int fov_half = camera_fov >> 1;
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex projected_vertex;

        projected_vertex = project_orthographic(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
            model_yaw,
            model_roll,
            scene_x,
            scene_y,
            scene_z,
            camera_pitch,
            camera_yaw,
            camera_roll);

        int x = projected_vertex.x;
        int y = projected_vertex.y;
        int z = projected_vertex.z;

        if( z < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = -5000;
            screen_vertices_z[i] = z - model_mid_z;
        }
        else
        {
            x *= cot_fov_half_ish15;
            y *= cot_fov_half_ish15;
            x >>= 15;
            y >>= 15;

            screen_vertices_x[i] = SCALE_UNIT(x) / z;
            screen_vertices_y[i] = SCALE_UNIT(y) / z;
            screen_vertices_z[i] = z - model_mid_z;
        }
    }
}

/** Alias: full 6DOF path already performs orthographic projection + divide in one loop. */
static inline void
project_vertices_array6_fused(
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
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    project_vertices_array6(
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
        model_pitch,
        model_yaw,
        model_roll,
        model_mid_z,
        scene_x,
        scene_y,
        scene_z,
        near_plane_z,
        camera_fov,
        camera_pitch,
        camera_yaw,
        camera_roll);
}

static inline void
project_vertices_array6_fused_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    project_vertices_array6_notex(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices,
        model_pitch,
        model_yaw,
        model_roll,
        model_mid_z,
        scene_x,
        scene_y,
        scene_z,
        near_plane_z,
        camera_fov,
        camera_pitch,
        camera_yaw,
        camera_roll);
}
#endif
