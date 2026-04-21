#ifndef PROJECTION_SPARSE_U_C
#define PROJECTION_SPARSE_U_C

#include "dash_faceint.h"
#include "dash_vertexint.h"
#include "projection.h"
#include "projection.u.c"

#include <stdbool.h>

extern int g_tan_table[2048];

#if VERTEXINT_BITS == 16

/** Pass-1 FOV scale matches dense NEON `>> 6` after `x * cot_fov_half_ish15` (not `>> 15` + `<<
 * 9`). */
static inline void
projection16_sparse_slot_tex_fused(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int idx,
    int vx,
    int vy,
    int vz,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int cot_fov_half_ish15,
    int model_mid_z,
    int near_plane_z)
{
    struct ProjectedVertex pv;
    project_orthographic_fast(
        &pv, vx, vy, vz, model_yaw, scene_x, scene_y, scene_z, camera_pitch, camera_yaw);

    int x = pv.x;
    int y = pv.y;
    int z = pv.z;
    x *= cot_fov_half_ish15;
    y *= cot_fov_half_ish15;
    x >>= 6;
    y >>= 6;

    orthographic_vertices_x[idx] = pv.x;
    orthographic_vertices_y[idx] = pv.y;
    orthographic_vertices_z[idx] = pv.z;

    screen_vertices_z[idx] = z - model_mid_z;
    if( z < near_plane_z )
    {
        screen_vertices_x[idx] = -5000;
        screen_vertices_y[idx] = y;
    }
    else
    {
        screen_vertices_x[idx] = x / z;
        if( screen_vertices_x[idx] == -5000 )
            screen_vertices_x[idx] = -5001;
        screen_vertices_y[idx] = y / z;
    }
}

static inline void
projection16_sparse_slot_notex_fused(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int idx,
    int vx,
    int vy,
    int vz,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int cot_fov_half_ish15,
    int model_mid_z,
    int near_plane_z)
{
    struct ProjectedVertex pv;
    project_orthographic_fast(
        &pv, vx, vy, vz, model_yaw, scene_x, scene_y, scene_z, camera_pitch, camera_yaw);

    int x = pv.x;
    int y = pv.y;
    int z = pv.z;
    x *= cot_fov_half_ish15;
    y *= cot_fov_half_ish15;
    x >>= 6;
    y >>= 6;

    screen_vertices_z[idx] = z - model_mid_z;
    if( z < near_plane_z )
    {
        screen_vertices_x[idx] = -5000;
        screen_vertices_y[idx] = y;
    }
    else
    {
        screen_vertices_x[idx] = x / z;
        if( screen_vertices_x[idx] == -5000 )
            screen_vertices_x[idx] = -5001;
        screen_vertices_y[idx] = y / z;
    }
}

static inline void
projection16_sparse_slot_tex_dense_match(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int idx,
    int vx,
    int vy,
    int vz,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int cot_fov_half_ish15)
{
    struct ProjectedVertex pv;
    project_orthographic_fast(
        &pv, vx, vy, vz, model_yaw, scene_x, scene_y, scene_z, camera_pitch, camera_yaw);

    int x = pv.x;
    int y = pv.y;
    x *= cot_fov_half_ish15;
    y *= cot_fov_half_ish15;
    x >>= 6;
    y >>= 6;

    orthographic_vertices_x[idx] = pv.x;
    orthographic_vertices_y[idx] = pv.y;
    orthographic_vertices_z[idx] = pv.z;

    screen_vertices_x[idx] = x;
    screen_vertices_y[idx] = y;
}

static inline void
projection16_sparse_slot_notex_dense_match(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int idx,
    int vx,
    int vy,
    int vz,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int cot_fov_half_ish15)
{
    struct ProjectedVertex pv;
    project_orthographic_fast(
        &pv, vx, vy, vz, model_yaw, scene_x, scene_y, scene_z, camera_pitch, camera_yaw);

    int x = pv.x;
    int y = pv.y;
    int z = pv.z;
    x *= cot_fov_half_ish15;
    y *= cot_fov_half_ish15;
    x >>= 6;
    y >>= 6;

    screen_vertices_z[idx] = z;
    screen_vertices_x[idx] = x;
    screen_vertices_y[idx] = y;
}

static inline void
project_vertices_array_sparse(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    const faceint_t* vertex_faces_a,
    const faceint_t* vertex_faces_b,
    const faceint_t* vertex_faces_c,
    int num_faces,
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

    int num_linear_slots = num_faces * 3;

    for( int f = 0; f < num_faces; f++ )
    {
        int base = f * 3;
        int vi[3] = {
            (int)vertex_faces_a[f],
            (int)vertex_faces_b[f],
            (int)vertex_faces_c[f],
        };
        for( int s = 0; s < 3; s++ )
        {
            int v = vi[s];
            projection16_sparse_slot_tex_dense_match(
                orthographic_vertices_x,
                orthographic_vertices_y,
                orthographic_vertices_z,
                screen_vertices_x,
                screen_vertices_y,
                base + s,
                (int)vertex_x[v],
                (int)vertex_y[v],
                (int)vertex_z[v],
                model_yaw,
                scene_x,
                scene_y,
                scene_z,
                camera_pitch,
                camera_yaw,
                cot_fov_half_ish15);
        }
    }

    projection_zdiv_pass_tex(
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
}

static inline void
project_vertices_array_notex_sparse(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    const faceint_t* vertex_faces_a,
    const faceint_t* vertex_faces_b,
    const faceint_t* vertex_faces_c,
    int num_faces,
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

    int num_linear_slots = num_faces * 3;

    for( int f = 0; f < num_faces; f++ )
    {
        int base = f * 3;
        int vi[3] = {
            (int)vertex_faces_a[f],
            (int)vertex_faces_b[f],
            (int)vertex_faces_c[f],
        };
        for( int s = 0; s < 3; s++ )
        {
            int v = vi[s];
            projection16_sparse_slot_notex_dense_match(
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                base + s,
                (int)vertex_x[v],
                (int)vertex_y[v],
                (int)vertex_z[v],
                model_yaw,
                scene_x,
                scene_y,
                scene_z,
                camera_pitch,
                camera_yaw,
                cot_fov_half_ish15);
        }
    }

    projection_zdiv_pass_notex(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
}

static inline void
project_vertices_array_sparse_fused(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    const faceint_t* vertex_faces_a,
    const faceint_t* vertex_faces_b,
    const faceint_t* vertex_faces_c,
    int num_faces,
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

    for( int f = 0; f < num_faces; f++ )
    {
        int base = f * 3;
        int vi[3] = {
            (int)vertex_faces_a[f],
            (int)vertex_faces_b[f],
            (int)vertex_faces_c[f],
        };
        for( int s = 0; s < 3; s++ )
        {
            int v = vi[s];
            projection16_sparse_slot_tex_fused(
                orthographic_vertices_x,
                orthographic_vertices_y,
                orthographic_vertices_z,
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                base + s,
                (int)vertex_x[v],
                (int)vertex_y[v],
                (int)vertex_z[v],
                model_yaw,
                scene_x,
                scene_y,
                scene_z,
                camera_pitch,
                camera_yaw,
                cot_fov_half_ish15,
                model_mid_z,
                near_plane_z);
        }
    }
}

static inline void
project_vertices_array_sparse_fused_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    const faceint_t* vertex_faces_a,
    const faceint_t* vertex_faces_b,
    const faceint_t* vertex_faces_c,
    int num_faces,
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

    for( int f = 0; f < num_faces; f++ )
    {
        int base = f * 3;
        int vi[3] = {
            (int)vertex_faces_a[f],
            (int)vertex_faces_b[f],
            (int)vertex_faces_c[f],
        };
        for( int s = 0; s < 3; s++ )
        {
            int v = vi[s];
            projection16_sparse_slot_notex_fused(
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                base + s,
                (int)vertex_x[v],
                (int)vertex_y[v],
                (int)vertex_z[v],
                model_yaw,
                scene_x,
                scene_y,
                scene_z,
                camera_pitch,
                camera_yaw,
                cot_fov_half_ish15,
                model_mid_z,
                near_plane_z);
        }
    }
}

#else /* VERTEXINT_BITS == 32 */

static inline void
projection_sparse_slot_tex(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int idx,
    int vx,
    int vy,
    int vz,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int cot_fov_half_ish15)
{
    struct ProjectedVertex projected_vertex;
    project_orthographic_fast(
        &projected_vertex,
        vx,
        vy,
        vz,
        model_yaw,
        scene_x,
        scene_y,
        scene_z,
        camera_pitch,
        camera_yaw);

    int x = projected_vertex.x;
    int y = projected_vertex.y;

    x *= cot_fov_half_ish15;
    y *= cot_fov_half_ish15;
    x >>= 6;
    y >>= 6;

    orthographic_vertices_x[idx] = projected_vertex.x;
    orthographic_vertices_y[idx] = projected_vertex.y;
    orthographic_vertices_z[idx] = projected_vertex.z;

    screen_vertices_x[idx] = x;
    screen_vertices_y[idx] = y;
}

static inline void
projection_sparse_slot_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int idx,
    int vx,
    int vy,
    int vz,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int cot_fov_half_ish15)
{
    struct ProjectedVertex projected_vertex;
    project_orthographic_fast(
        &projected_vertex,
        vx,
        vy,
        vz,
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

    screen_vertices_z[idx] = z;
    screen_vertices_x[idx] = x;
    screen_vertices_y[idx] = y;
}

static inline void
projection_sparse_slot_tex_fused(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int idx,
    int vx,
    int vy,
    int vz,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int cot_fov_half_ish15,
    int model_mid_z,
    int near_plane_z)
{
    struct ProjectedVertex projected_vertex;
    project_orthographic_fast(
        &projected_vertex,
        vx,
        vy,
        vz,
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

    orthographic_vertices_x[idx] = projected_vertex.x;
    orthographic_vertices_y[idx] = projected_vertex.y;
    orthographic_vertices_z[idx] = projected_vertex.z;

    screen_vertices_z[idx] = z - model_mid_z;
    if( z < near_plane_z )
    {
        screen_vertices_x[idx] = -5000;
        screen_vertices_y[idx] = y;
    }
    else
    {
        screen_vertices_x[idx] = x / z;
        if( screen_vertices_x[idx] == -5000 )
            screen_vertices_x[idx] = -5001;
        screen_vertices_y[idx] = y / z;
    }
}

static inline void
projection_sparse_slot_notex_fused(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int idx,
    int vx,
    int vy,
    int vz,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int cot_fov_half_ish15,
    int model_mid_z,
    int near_plane_z)
{
    struct ProjectedVertex projected_vertex;
    project_orthographic_fast(
        &projected_vertex,
        vx,
        vy,
        vz,
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

    screen_vertices_z[idx] = z - model_mid_z;
    if( z < near_plane_z )
    {
        screen_vertices_x[idx] = -5000;
        screen_vertices_y[idx] = y;
    }
    else
    {
        screen_vertices_x[idx] = x / z;
        if( screen_vertices_x[idx] == -5000 )
            screen_vertices_x[idx] = -5001;
        screen_vertices_y[idx] = y / z;
    }
}

static inline void
project_vertices_array_sparse(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    const faceint_t* vertex_faces_a,
    const faceint_t* vertex_faces_b,
    const faceint_t* vertex_faces_c,
    int num_faces,
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

    for( int f = 0; f < num_faces; f++ )
    {
        int base = f * 3;
        int vi[3] = {
            (int)vertex_faces_a[f],
            (int)vertex_faces_b[f],
            (int)vertex_faces_c[f],
        };
        for( int s = 0; s < 3; s++ )
        {
            int v = vi[s];
            projection_sparse_slot_tex(
                orthographic_vertices_x,
                orthographic_vertices_y,
                orthographic_vertices_z,
                screen_vertices_x,
                screen_vertices_y,
                base + s,
                (int)vertex_x[v],
                (int)vertex_y[v],
                (int)vertex_z[v],
                model_yaw,
                scene_x,
                scene_y,
                scene_z,
                camera_pitch,
                camera_yaw,
                cot_fov_half_ish15);
        }
    }

    int num_linear_slots = num_faces * 3;
    projection_zdiv_pass_tex(
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
}

static inline void
project_vertices_array_notex_sparse(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    const faceint_t* vertex_faces_a,
    const faceint_t* vertex_faces_b,
    const faceint_t* vertex_faces_c,
    int num_faces,
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

    for( int f = 0; f < num_faces; f++ )
    {
        int base = f * 3;
        int vi[3] = {
            (int)vertex_faces_a[f],
            (int)vertex_faces_b[f],
            (int)vertex_faces_c[f],
        };
        for( int s = 0; s < 3; s++ )
        {
            int v = vi[s];
            projection_sparse_slot_notex(
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                base + s,
                (int)vertex_x[v],
                (int)vertex_y[v],
                (int)vertex_z[v],
                model_yaw,
                scene_x,
                scene_y,
                scene_z,
                camera_pitch,
                camera_yaw,
                cot_fov_half_ish15);
        }
    }

    int num_linear_slots = num_faces * 3;
    projection_zdiv_pass_notex(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
}

static inline void
project_vertices_array_sparse_fused(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    const faceint_t* vertex_faces_a,
    const faceint_t* vertex_faces_b,
    const faceint_t* vertex_faces_c,
    int num_faces,
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

    for( int f = 0; f < num_faces; f++ )
    {
        int base = f * 3;
        int vi[3] = {
            (int)vertex_faces_a[f],
            (int)vertex_faces_b[f],
            (int)vertex_faces_c[f],
        };
        for( int s = 0; s < 3; s++ )
        {
            int v = vi[s];
            projection_sparse_slot_tex_fused(
                orthographic_vertices_x,
                orthographic_vertices_y,
                orthographic_vertices_z,
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                base + s,
                (int)vertex_x[v],
                (int)vertex_y[v],
                (int)vertex_z[v],
                model_yaw,
                scene_x,
                scene_y,
                scene_z,
                camera_pitch,
                camera_yaw,
                cot_fov_half_ish15,
                model_mid_z,
                near_plane_z);
        }
    }
}

static inline void
project_vertices_array_sparse_fused_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    const faceint_t* vertex_faces_a,
    const faceint_t* vertex_faces_b,
    const faceint_t* vertex_faces_c,
    int num_faces,
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

    for( int f = 0; f < num_faces; f++ )
    {
        int base = f * 3;
        int vi[3] = {
            (int)vertex_faces_a[f],
            (int)vertex_faces_b[f],
            (int)vertex_faces_c[f],
        };
        for( int s = 0; s < 3; s++ )
        {
            int v = vi[s];
            projection_sparse_slot_notex_fused(
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                base + s,
                (int)vertex_x[v],
                (int)vertex_y[v],
                (int)vertex_z[v],
                model_yaw,
                scene_x,
                scene_y,
                scene_z,
                camera_pitch,
                camera_yaw,
                cot_fov_half_ish15,
                model_mid_z,
                near_plane_z);
        }
    }
}

#endif /* VERTEXINT_BITS */

/**
 * Like project_vertices_array6, face-indexed. Three slots per face (f*3 + s).
 */
static inline void
project_vertices_array6_sparse(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    const faceint_t* vertex_faces_a,
    const faceint_t* vertex_faces_b,
    const faceint_t* vertex_faces_c,
    int num_faces,
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

    for( int f = 0; f < num_faces; f++ )
    {
        int base = f * 3;
        int vi[3] = {
            (int)vertex_faces_a[f],
            (int)vertex_faces_b[f],
            (int)vertex_faces_c[f],
        };

        for( int s = 0; s < 3; s++ )
        {
            int v = vi[s];
            struct ProjectedVertex projected_vertex;

            projected_vertex = project_orthographic(
                vertex_x[v],
                vertex_y[v],
                vertex_z[v],
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

            int idx = base + s;
            orthographic_vertices_x[idx] = x;
            orthographic_vertices_y[idx] = y;
            orthographic_vertices_z[idx] = z;

            if( z < near_plane_z )
            {
                screen_vertices_x[idx] = -5000;
                screen_vertices_y[idx] = -5000;
                screen_vertices_z[idx] = z - model_mid_z;
            }
            else
            {
                x *= cot_fov_half_ish15;
                y *= cot_fov_half_ish15;
                x >>= 6;
                y >>= 6;

                screen_vertices_x[idx] = x / z;
                screen_vertices_y[idx] = y / z;
                screen_vertices_z[idx] = z - model_mid_z;
            }
        }
    }
}

/**
 * Like project_vertices_array6_notex, face-indexed. Three slots per face (f*3 + s).
 */
static inline void
project_vertices_array6_notex_sparse(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    const faceint_t* vertex_faces_a,
    const faceint_t* vertex_faces_b,
    const faceint_t* vertex_faces_c,
    int num_faces,
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

    for( int f = 0; f < num_faces; f++ )
    {
        int base = f * 3;
        int vi[3] = {
            (int)vertex_faces_a[f],
            (int)vertex_faces_b[f],
            (int)vertex_faces_c[f],
        };

        for( int s = 0; s < 3; s++ )
        {
            int v = vi[s];
            struct ProjectedVertex projected_vertex;

            projected_vertex = project_orthographic(
                vertex_x[v],
                vertex_y[v],
                vertex_z[v],
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

            int idx = base + s;

            if( z < near_plane_z )
            {
                screen_vertices_x[idx] = -5000;
                screen_vertices_y[idx] = -5000;
                screen_vertices_z[idx] = z - model_mid_z;
            }
            else
            {
                x *= cot_fov_half_ish15;
                y *= cot_fov_half_ish15;
                x >>= 6;
                y >>= 6;

                screen_vertices_x[idx] = x / z;
                screen_vertices_y[idx] = y / z;
                screen_vertices_z[idx] = z - model_mid_z;
            }
        }
    }
}

/** Alias: 6DOF sparse path already fuses projection + divide per slot (no separate z-pass). */
static inline void
project_vertices_array6_sparse_fused(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    const faceint_t* vertex_faces_a,
    const faceint_t* vertex_faces_b,
    const faceint_t* vertex_faces_c,
    int num_faces,
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
    project_vertices_array6_sparse(
        orthographic_vertices_x,
        orthographic_vertices_y,
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        vertex_faces_a,
        vertex_faces_b,
        vertex_faces_c,
        num_faces,
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
project_vertices_array6_sparse_fused_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    const faceint_t* vertex_faces_a,
    const faceint_t* vertex_faces_b,
    const faceint_t* vertex_faces_c,
    int num_faces,
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
    project_vertices_array6_notex_sparse(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        vertex_faces_a,
        vertex_faces_b,
        vertex_faces_c,
        num_faces,
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

#endif /* PROJECTION_SPARSE_U_C */
