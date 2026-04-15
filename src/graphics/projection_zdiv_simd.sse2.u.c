#ifndef PROJECTION_ZDIV_SIMD_SSE2_U_C
#define PROJECTION_ZDIV_SIMD_SSE2_U_C

#if defined(__SSE2__) && !defined(SSE2_DISABLED) && !defined(__SSE4_1__)
#include <assert.h>
#include <emmintrin.h>
#include <limits.h>
#include <stdbool.h>
#include <xmmintrin.h>

/* vz_arr is depth for division (tex: ortho z; notex: raw z in screen_z). Writes only out_* . */
static inline void
projection_zdiv_sse2_four_lanes(
    const int vz_arr[4],
    const int vsx_arr[4],
    const int vsy_arr[4],
    int out_sx[4],
    int out_sy[4],
    int out_sz[4],
    int model_mid_z,
    int near_plane_z)
{
    __m128 fz = _mm_set_ps((float)vz_arr[3], (float)vz_arr[2], (float)vz_arr[1], (float)vz_arr[0]);
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

        out_sz[j] = z - model_mid_z;

        int divx_i = (int)fdivx_arr[j];
        int divy_i = (int)fdivy_arr[j];

        if( clipped )
        {
            out_sx[j] = -5000;
            out_sy[j] = vsy_arr[j];
        }
        else
        {
            if( divx_i == -5000 )
                divx_i = -5001;
            out_sx[j] = divx_i;
            out_sy[j] = divy_i;
        }
    }
}

static inline void
projection_zdiv_tex_sse2_tail(
    const int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int base,
    int rem,
    int model_mid_z,
    int near_plane_z)
{
    if( rem <= 0 )
        return;
    assert(rem < 4);

    int z_pad = (near_plane_z < INT_MAX) ? (near_plane_z + 1) : near_plane_z;
    int zbuf[4];
    int xbuf[4];
    int ybuf[4];
    for( int j = 0; j < 4; j++ )
    {
        if( j < rem )
        {
            zbuf[j] = orthographic_vertices_z[base + j];
            xbuf[j] = screen_vertices_x[base + j];
            ybuf[j] = screen_vertices_y[base + j];
        }
        else
        {
            zbuf[j] = z_pad;
            xbuf[j] = 0;
            ybuf[j] = 0;
        }
    }

    int out_x[4];
    int out_y[4];
    int out_z[4];
    projection_zdiv_sse2_four_lanes(
        zbuf, xbuf, ybuf, out_x, out_y, out_z, model_mid_z, near_plane_z);

    for( int j = 0; j < rem; j++ )
    {
        screen_vertices_z[base + j] = out_z[j];
        screen_vertices_x[base + j] = out_x[j];
        screen_vertices_y[base + j] = out_y[j];
    }
}

static inline void
projection_zdiv_notex_sse2_tail(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int base,
    int rem,
    int model_mid_z,
    int near_plane_z)
{
    if( rem <= 0 )
        return;
    assert(rem < 4);

    int z_pad = (near_plane_z < INT_MAX) ? (near_plane_z + 1) : near_plane_z;
    int zbuf[4];
    int xbuf[4];
    int ybuf[4];
    for( int j = 0; j < 4; j++ )
    {
        if( j < rem )
        {
            zbuf[j] = screen_vertices_z[base + j];
            xbuf[j] = screen_vertices_x[base + j];
            ybuf[j] = screen_vertices_y[base + j];
        }
        else
        {
            zbuf[j] = z_pad;
            xbuf[j] = 0;
            ybuf[j] = 0;
        }
    }

    int out_x[4];
    int out_y[4];
    int out_z[4];
    projection_zdiv_sse2_four_lanes(
        zbuf, xbuf, ybuf, out_x, out_y, out_z, model_mid_z, near_plane_z);

    for( int j = 0; j < rem; j++ )
    {
        screen_vertices_z[base + j] = out_z[j];
        screen_vertices_x[base + j] = out_x[j];
        screen_vertices_y[base + j] = out_y[j];
    }
}

static inline void
projection_zdiv_tex_sse2(
    const int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int num_linear_slots,
    int model_mid_z,
    int near_plane_z)
{
    const int vsteps = 4;
    int i = 0;

    for( ; i + vsteps - 1 < num_linear_slots; i += vsteps )
    {
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

        int out_x[4];
        int out_y[4];
        int out_z[4];
        projection_zdiv_sse2_four_lanes(
            vz_arr, vsx_arr, vsy_arr, out_x, out_y, out_z, model_mid_z, near_plane_z);
        for( int j = 0; j < 4; j++ )
        {
            screen_vertices_z[i + j] = out_z[j];
            screen_vertices_x[i + j] = out_x[j];
            screen_vertices_y[i + j] = out_y[j];
        }
    }

    projection_zdiv_tex_sse2_tail(
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        i,
        num_linear_slots - i,
        model_mid_z,
        near_plane_z);
}

static inline void
projection_zdiv_notex_sse2(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int num_linear_slots,
    int model_mid_z,
    int near_plane_z)
{
    const int vsteps = 4;
    int i = 0;

    for( ; i + vsteps - 1 < num_linear_slots; i += vsteps )
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

        int out_x[4];
        int out_y[4];
        int out_z[4];
        projection_zdiv_sse2_four_lanes(
            vz_arr, vsx_arr, vsy_arr, out_x, out_y, out_z, model_mid_z, near_plane_z);
        for( int j = 0; j < 4; j++ )
        {
            screen_vertices_z[i + j] = out_z[j];
            screen_vertices_x[i + j] = out_x[j];
            screen_vertices_y[i + j] = out_y[j];
        }
    }

    projection_zdiv_notex_sse2_tail(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        i,
        num_linear_slots - i,
        model_mid_z,
        near_plane_z);
}

#endif /* SSE2 without SSE4.1 */

#endif /* PROJECTION_ZDIV_SIMD_SSE2_U_C */
