/**
 * Face-indexed sparse projection must match dense project_vertices_array on the same
 * per-slot vertex sequence (slot f*3+s uses corner vertex_faces_{a,b,c}[f]).
 *
 * Build (from repo root):
 *   cc -std=c11 -O2 -I src/graphics -I src \
 *     scripts/benchmarks/test_sparse_projection.c src/graphics/shared_tables.c \
 *     -o build/test_sparse_projection -lm
 *
 * (This file #includes projection_zdiv_simd.u.c before projection16_simd.u.c,
 *  then projection_sparse.u.c — same rule as dash.c for 16-bit.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dash_faceint.h"
#include "dash_vertexint.h"
#include "projection.h"
#include "shared_tables.h"

#include "../../src/graphics/projection_zdiv_simd.u.c"
#include "../../src/graphics/projection16_simd.u.c"
#include "../../src/graphics/projection_sparse.u.c"

/* 15 slots exercises sparse z-div tails (remainder mod 8 and mod 4). */
#define NF 5
#define NSLOT (NF * 3)

static void
fill_dense_from_faces(
    vertexint_t* vx_out,
    vertexint_t* vy_out,
    vertexint_t* vz_out,
    const vertexint_t* vx,
    const vertexint_t* vy,
    const vertexint_t* vz,
    const faceint_t* fa,
    const faceint_t* fb,
    const faceint_t* fc,
    int num_faces)
{
    for( int f = 0; f < num_faces; f++ )
    {
        int ia = (int)fa[f];
        int ib = (int)fb[f];
        int ic = (int)fc[f];
        vx_out[f * 3 + 0] = vx[ia];
        vy_out[f * 3 + 0] = vy[ia];
        vz_out[f * 3 + 0] = vz[ia];
        vx_out[f * 3 + 1] = vx[ib];
        vy_out[f * 3 + 1] = vy[ib];
        vz_out[f * 3 + 1] = vz[ib];
        vx_out[f * 3 + 2] = vx[ic];
        vy_out[f * 3 + 2] = vy[ic];
        vz_out[f * 3 + 2] = vz[ic];
    }
}

int
main(void)
{
    init_sin_table();
    init_cos_table();
    init_tan_table();

    vertexint_t vx[8] = { -100, 100, 100, -100, 0, 50, -30, 80 };
    vertexint_t vy[8] = { 0, 0, 120, 120, -50, 20, 40, -10 };
    vertexint_t vz[8] = { 200, 200, 250, 250, 180, 220, 300, 160 };

    faceint_t fa[NF] = { 0, 1, 2, 3, 4 };
    faceint_t fb[NF] = { 1, 2, 3, 4, 5 };
    faceint_t fc[NF] = { 2, 3, 4, 5, 6 };

    vertexint_t vxd[NSLOT], vyd[NSLOT], vzd[NSLOT];
    fill_dense_from_faces(vxd, vyd, vzd, vx, vy, vz, fa, fb, fc, NF);

    int ox_d[NSLOT], oy_d[NSLOT], oz_d[NSLOT];
    int sx_d[NSLOT], sy_d[NSLOT], sz_d[NSLOT];
    int ox_s[NSLOT], oy_s[NSLOT], oz_s[NSLOT];
    int sx_s[NSLOT], sy_s[NSLOT], sz_s[NSLOT];

    int model_yaw = 128;
    int mid_z = 200;
    int scene_x = 10, scene_y = -20, scene_z = 400;
    int near_z = 50;
    int fov = 512;
    int pitch = 256, yaw_cam = 384;

    memset(ox_d, 0, sizeof(ox_d));
    project_vertices_array(
        ox_d,
        oy_d,
        oz_d,
        sx_d,
        sy_d,
        sz_d,
        vxd,
        vyd,
        vzd,
        NSLOT,
        model_yaw,
        mid_z,
        scene_x,
        scene_y,
        scene_z,
        near_z,
        fov,
        pitch,
        yaw_cam);

    memset(ox_s, 0, sizeof(ox_s));
    project_vertices_array_sparse(
        ox_s,
        oy_s,
        oz_s,
        sx_s,
        sy_s,
        sz_s,
        vx,
        vy,
        vz,
        fa,
        fb,
        fc,
        NF,
        model_yaw,
        mid_z,
        scene_x,
        scene_y,
        scene_z,
        near_z,
        fov,
        pitch,
        yaw_cam);

    for( int i = 0; i < NSLOT; i++ )
    {
        if( ox_d[i] != ox_s[i] || oy_d[i] != oy_s[i] || oz_d[i] != oz_s[i] || sx_d[i] != sx_s[i]
            || sy_d[i] != sy_s[i] || sz_d[i] != sz_s[i] )
        {
            fprintf(
                stderr,
                "sparse tex vs dense mismatch at %d: dense (%d,%d,%d)(%d,%d,%d) sparse (%d,%d,%d)(%d,%d,%d)\n",
                i,
                ox_d[i],
                oy_d[i],
                oz_d[i],
                sx_d[i],
                sy_d[i],
                sz_d[i],
                ox_s[i],
                oy_s[i],
                oz_s[i],
                sx_s[i],
                sy_s[i],
                sz_s[i]);
            return 1;
        }
    }

    memset(sx_d, 0, sizeof(sx_d));
    project_vertices_array_notex(sx_d, sy_d, sz_d, vxd, vyd, vzd, NSLOT, model_yaw, mid_z, scene_x, scene_y, scene_z, near_z, fov, pitch, yaw_cam);

    memset(sx_s, 0, sizeof(sx_s));
    project_vertices_array_notex_sparse(
        sx_s,
        sy_s,
        sz_s,
        vx,
        vy,
        vz,
        fa,
        fb,
        fc,
        NF,
        model_yaw,
        mid_z,
        scene_x,
        scene_y,
        scene_z,
        near_z,
        fov,
        pitch,
        yaw_cam);

    for( int i = 0; i < NSLOT; i++ )
    {
        if( sx_d[i] != sx_s[i] || sy_d[i] != sy_s[i] || sz_d[i] != sz_s[i] )
        {
            fprintf(
                stderr,
                "sparse notex vs dense mismatch at %d: dense (%d,%d,%d) sparse (%d,%d,%d)\n",
                i,
                sx_d[i],
                sy_d[i],
                sz_d[i],
                sx_s[i],
                sy_s[i],
                sz_s[i]);
            return 1;
        }
    }

    model_yaw = 0;
    project_vertices_array(
        ox_d,
        oy_d,
        oz_d,
        sx_d,
        sy_d,
        sz_d,
        vxd,
        vyd,
        vzd,
        NSLOT,
        model_yaw,
        mid_z,
        scene_x,
        scene_y,
        scene_z,
        near_z,
        fov,
        pitch,
        yaw_cam);

    memset(ox_s, 0, sizeof(ox_s));
    project_vertices_array_sparse(
        ox_s,
        oy_s,
        oz_s,
        sx_s,
        sy_s,
        sz_s,
        vx,
        vy,
        vz,
        fa,
        fb,
        fc,
        NF,
        model_yaw,
        mid_z,
        scene_x,
        scene_y,
        scene_z,
        near_z,
        fov,
        pitch,
        yaw_cam);

    for( int i = 0; i < NSLOT; i++ )
    {
        if( ox_d[i] != ox_s[i] || oy_d[i] != oy_s[i] || oz_d[i] != oz_s[i] || sx_d[i] != sx_s[i]
            || sy_d[i] != sy_s[i] || sz_d[i] != sz_s[i] )
        {
            fprintf(stderr, "sparse tex no-yaw vs dense mismatch at %d\n", i);
            return 1;
        }
    }

    printf("sparse projection matches dense: OK (%d slots)\n", NSLOT);
    return 0;
}
