#ifndef PROJECTION_ZDIV_SIMD_AVX_U_C
#define PROJECTION_ZDIV_SIMD_AVX_U_C

#if defined(__AVX2__) && !defined(AVX2_DISABLED)
#include <assert.h>
#include <immintrin.h>
#include <limits.h>

static inline void
projection_zdiv_avx2_apply(
    __m256i vz,
    __m256i vsx,
    __m256i vsy,
    __m256i v_near,
    __m256i v_mid,
    __m256i v_neg5000,
    __m256i v_neg5001,
    __m256i* out_vscreen_z,
    __m256i* out_final_x,
    __m256i* out_final_y)
{
    __m256i clipped_mask = _mm256_cmpgt_epi32(v_near, vz);

    __m256i vscreen_z = _mm256_sub_epi32(vz, v_mid);
    *out_vscreen_z = vscreen_z;

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

    *out_final_x = final_x;
    *out_final_y = final_y;
}

static inline void
projection_zdiv_tex_avx2_tail(
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
    assert(rem < 8);

    int z_pad = (near_plane_z < INT_MAX) ? (near_plane_z + 1) : near_plane_z;
    int zbuf[8];
    int xbuf[8];
    int ybuf[8];
    for( int j = 0; j < 8; j++ )
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

    __m256i vz = _mm256_loadu_si256((__m256i const*)zbuf);
    __m256i vsx = _mm256_loadu_si256((__m256i const*)xbuf);
    __m256i vsy = _mm256_loadu_si256((__m256i const*)ybuf);

    __m256i v_near = _mm256_set1_epi32(near_plane_z);
    __m256i v_mid = _mm256_set1_epi32(model_mid_z);
    __m256i v_neg5000 = _mm256_set1_epi32(-5000);
    __m256i v_neg5001 = _mm256_set1_epi32(-5001);

    __m256i vscreen_z;
    __m256i final_x;
    __m256i final_y;
    projection_zdiv_avx2_apply(
        vz, vsx, vsy, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

    int out_z[8];
    int out_x[8];
    int out_y[8];
    _mm256_storeu_si256((__m256i*)out_z, vscreen_z);
    _mm256_storeu_si256((__m256i*)out_x, final_x);
    _mm256_storeu_si256((__m256i*)out_y, final_y);
    for( int j = 0; j < rem; j++ )
    {
        screen_vertices_z[base + j] = out_z[j];
        screen_vertices_x[base + j] = out_x[j];
        screen_vertices_y[base + j] = out_y[j];
    }
}

static inline void
projection_zdiv_notex_avx2_tail(
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
    assert(rem < 8);

    int z_pad = (near_plane_z < INT_MAX) ? (near_plane_z + 1) : near_plane_z;
    int zbuf[8];
    int xbuf[8];
    int ybuf[8];
    for( int j = 0; j < 8; j++ )
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

    __m256i vz = _mm256_loadu_si256((__m256i const*)zbuf);
    __m256i vsx = _mm256_loadu_si256((__m256i const*)xbuf);
    __m256i vsy = _mm256_loadu_si256((__m256i const*)ybuf);

    __m256i v_near = _mm256_set1_epi32(near_plane_z);
    __m256i v_mid = _mm256_set1_epi32(model_mid_z);
    __m256i v_neg5000 = _mm256_set1_epi32(-5000);
    __m256i v_neg5001 = _mm256_set1_epi32(-5001);

    __m256i vscreen_z;
    __m256i final_x;
    __m256i final_y;
    projection_zdiv_avx2_apply(
        vz, vsx, vsy, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

    int out_z[8];
    int out_x[8];
    int out_y[8];
    _mm256_storeu_si256((__m256i*)out_z, vscreen_z);
    _mm256_storeu_si256((__m256i*)out_x, final_x);
    _mm256_storeu_si256((__m256i*)out_y, final_y);
    for( int j = 0; j < rem; j++ )
    {
        screen_vertices_z[base + j] = out_z[j];
        screen_vertices_x[base + j] = out_x[j];
        screen_vertices_y[base + j] = out_y[j];
    }
}

static inline void
projection_zdiv_tex_avx2(
    const int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int num_linear_slots,
    int model_mid_z,
    int near_plane_z)
{
    const int vsteps = 8;
    int i = 0;

    __m256i v_near = _mm256_set1_epi32(near_plane_z);
    __m256i v_mid = _mm256_set1_epi32(model_mid_z);
    __m256i v_neg5000 = _mm256_set1_epi32(-5000);
    __m256i v_neg5001 = _mm256_set1_epi32(-5001);

    for( ; i + vsteps - 1 < num_linear_slots; i += vsteps )
    {
        __m256i vz = _mm256_loadu_si256((__m256i const*)(orthographic_vertices_z + i));
        __m256i vsx = _mm256_loadu_si256((__m256i const*)(screen_vertices_x + i));
        __m256i vsy = _mm256_loadu_si256((__m256i const*)(screen_vertices_y + i));

        __m256i vscreen_z;
        __m256i final_x;
        __m256i final_y;
        projection_zdiv_avx2_apply(
            vz, vsx, vsy, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

        _mm256_storeu_si256((__m256i*)(screen_vertices_z + i), vscreen_z);
        _mm256_storeu_si256((__m256i*)(screen_vertices_x + i), final_x);
        _mm256_storeu_si256((__m256i*)(screen_vertices_y + i), final_y);
    }

    projection_zdiv_tex_avx2_tail(
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
projection_zdiv_notex_avx2(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int num_linear_slots,
    int model_mid_z,
    int near_plane_z)
{
    const int vsteps = 8;
    int i = 0;

    __m256i v_near = _mm256_set1_epi32(near_plane_z);
    __m256i v_mid = _mm256_set1_epi32(model_mid_z);
    __m256i v_neg5000 = _mm256_set1_epi32(-5000);
    __m256i v_neg5001 = _mm256_set1_epi32(-5001);

    for( ; i + vsteps - 1 < num_linear_slots; i += vsteps )
    {
        __m256i vz = _mm256_loadu_si256((__m256i const*)(screen_vertices_z + i));
        __m256i vsx = _mm256_loadu_si256((__m256i const*)(screen_vertices_x + i));
        __m256i vsy = _mm256_loadu_si256((__m256i const*)(screen_vertices_y + i));

        __m256i vscreen_z;
        __m256i final_x;
        __m256i final_y;
        projection_zdiv_avx2_apply(
            vz, vsx, vsy, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

        _mm256_storeu_si256((__m256i*)(screen_vertices_z + i), vscreen_z);
        _mm256_storeu_si256((__m256i*)(screen_vertices_x + i), final_x);
        _mm256_storeu_si256((__m256i*)(screen_vertices_y + i), final_y);
    }

    projection_zdiv_notex_avx2_tail(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        i,
        num_linear_slots - i,
        model_mid_z,
        near_plane_z);
}

#endif /* AVX2 */

#endif /* PROJECTION_ZDIV_SIMD_AVX_U_C */
