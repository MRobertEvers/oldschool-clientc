#ifndef PROJECTION16_SIMD_U_C
#define PROJECTION16_SIMD_U_C

#include "dash_faceint.h"
#include "dash_vertexint.h"
#include "projection.h"
/* Scalar helpers live in projection.u.c (also pulled in by render_*.u.c before this TU). */
#include "projection.u.c"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

// This was turning out slower than the scalar version, so we're disabling it for now.
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "projection16_simd.neon.u.c"
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "projection16_simd.avx.u.c"
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "projection16_simd.sse41.u.c"
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "projection16_simd.sse2.u.c"
#elif defined(__SSE__) && !defined(SSE_DISABLED)
#include "projection16_simd.sse_float.u.c"
#else
#include "projection16_simd.scalar.u.c"
#endif

/**
 * Project vertices array with full 6DOF support (pitch, yaw, roll for model and camera)
 * Uses the full project_orthographic function instead of project_orthographic_fast
 * This function is available for all platforms, regardless of SIMD support
 */
static inline void
project_vertices_array6_clip(
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
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    int cot_fov_half_ish16 = camera_cot16;
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
            x >>= 6;
            y >>= 6;

            screen_vertices_x[i] = x / z;
            screen_vertices_y[i] = y / z;
            screen_vertices_z[i] = z - model_mid_z;
        }
    }
}

static inline void
project_vertices_array6_noclip(
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
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    int cot_fov_half_ish16 = camera_cot16;
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
            x >>= 6;
            y >>= 6;

            screen_vertices_x[i] = x / z;
            screen_vertices_y[i] = y / z;
            screen_vertices_z[i] = z - model_mid_z;
        }
    }
}

/**
 * Project vertices array with full 6DOF support, without saving orthographic coordinates.
 * Use for non-textured models that do not need camera-space x/y per vertex.
 */
static inline void
project_vertices_array6_notex_clip(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    int cot_fov_half_ish16 = camera_cot16;
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
            x >>= 6;
            y >>= 6;

            screen_vertices_x[i] = x / z;
            screen_vertices_y[i] = y / z;
            screen_vertices_z[i] = z - model_mid_z;
        }
    }
}

static inline void
project_vertices_array6_notex_noclip(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    int cot_fov_half_ish16 = camera_cot16;
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
            x >>= 6;
            y >>= 6;

            screen_vertices_x[i] = x / z;
            screen_vertices_y[i] = y / z;
            screen_vertices_z[i] = z - model_mid_z;
        }
    }
}

static inline void
project_vertices_array6_fused_clip(
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
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    project_vertices_array6_clip(
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
        camera_cot16,
        camera_pitch,
        camera_yaw,
        camera_roll);
}

static inline void
project_vertices_array6_fused_noclip(
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
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    project_vertices_array6_noclip(
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
        camera_cot16,
        camera_pitch,
        camera_yaw,
        camera_roll);
}

static inline void
project_vertices_array6_fused_notex_clip(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    project_vertices_array6_notex_clip(
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
        camera_cot16,
        camera_pitch,
        camera_yaw,
        camera_roll);
}

static inline void
project_vertices_array6_fused_notex_noclip(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    project_vertices_array6_notex_noclip(
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
        camera_cot16,
        camera_pitch,
        camera_yaw,
        camera_roll);
}

static inline void
project_vertices_array_pitchyaw_fused_clip(
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
    int model_pitch,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw)
{
    int cot_fov_half_ish16 = camera_cot16;
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex projected_vertex;
        project_orthographic_fast_pitchyaw(
            &projected_vertex,
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
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
        x >>= 6;
        y >>= 6;

        int screen_x = x;
        int screen_y = y;

        orthographic_vertices_x[i] = projected_vertex.x;
        orthographic_vertices_y[i] = projected_vertex.y;
        orthographic_vertices_z[i] = projected_vertex.z;

        screen_vertices_z[i] = z - model_mid_z;
        if( z < near_plane_z )
        {
            screen_vertices_x[i] = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
            screen_vertices_y[i] = screen_y;
        }
        else
        {
            screen_vertices_x[i] = screen_x / z;
            if( screen_vertices_x[i] == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
                screen_vertices_x[i] = TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE;
            screen_vertices_y[i] = screen_y / z;
        }
    }
}

static inline void
project_vertices_array_pitchyaw_fused_noclip(
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
    int model_pitch,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw)
{
    int cot_fov_half_ish16 = camera_cot16;
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex projected_vertex;
        project_orthographic_fast_pitchyaw(
            &projected_vertex,
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
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
        x >>= 6;
        y >>= 6;

        int screen_x = x;
        int screen_y = y;

        orthographic_vertices_x[i] = projected_vertex.x;
        orthographic_vertices_y[i] = projected_vertex.y;
        orthographic_vertices_z[i] = projected_vertex.z;

        screen_vertices_z[i] = z - model_mid_z;
        screen_vertices_x[i] = screen_x / z;
        screen_vertices_y[i] = screen_y / z;
    }
}

static inline void
project_vertices_array_pitchyaw_fused_notex_clip(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw)
{
    int cot_fov_half_ish16 = camera_cot16;
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex projected_vertex;
        project_orthographic_fast_pitchyaw(
            &projected_vertex,
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
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
        x >>= 6;
        y >>= 6;

        int screen_x = x;
        int screen_y = y;

        screen_vertices_z[i] = z - model_mid_z;
        if( z < near_plane_z )
        {
            screen_vertices_x[i] = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
            screen_vertices_y[i] = screen_y;
        }
        else
        {
            screen_vertices_x[i] = screen_x / z;
            if( screen_vertices_x[i] == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
                screen_vertices_x[i] = TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE;
            screen_vertices_y[i] = screen_y / z;
        }
    }
}

static inline void
project_vertices_array_pitchyaw_fused_notex_noclip(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw)
{
    int cot_fov_half_ish16 = camera_cot16;
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex projected_vertex;
        project_orthographic_fast_pitchyaw(
            &projected_vertex,
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
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
        x >>= 6;
        y >>= 6;

        int screen_x = x;
        int screen_y = y;

        screen_vertices_z[i] = z - model_mid_z;
        screen_vertices_x[i] = screen_x / z;
        screen_vertices_y[i] = screen_y / z;
    }
}
#endif
