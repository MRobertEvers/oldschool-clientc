#ifndef PROJECTION_ZDIV_SIMD_SSE2_U_C
#define PROJECTION_ZDIV_SIMD_SSE2_U_C

#if defined(__SSE2__) && !defined(SSE2_DISABLED) && !defined(__SSE4_1__)
#include <assert.h>
#include <emmintrin.h>
#include <limits.h>
#include <xmmintrin.h>

/* vz is depth for division (tex: ortho z; notex: raw z in screen_z). Mirrors SSE4.1 apply; blends use AND/ANDNOT/OR only. */
static inline void
projection_zdiv_sse2_apply(
    __m128i vz,
    __m128i vsx,
    __m128i vsy,
    __m128i v_near,
    __m128i v_mid,
    __m128i v_neg5000,
    __m128i v_neg5001,
    __m128i* out_vscreen_z,
    __m128i* out_final_x,
    __m128i* out_final_y)
{
    __m128i clipped_mask = _mm_cmplt_epi32(vz, v_near);

    __m128i vscreen_z = _mm_sub_epi32(vz, v_mid);
    *out_vscreen_z = vscreen_z;

    __m128 fx = _mm_cvtepi32_ps(vsx);
    __m128 fz = _mm_cvtepi32_ps(vz);

    __m128 rcp_z = _mm_rcp_ps(fz);
    __m128 fdivx = _mm_mul_ps(fx, rcp_z);
    __m128 fdivy = _mm_mul_ps(_mm_cvtepi32_ps(vsy), rcp_z);

    __m128i divx_i = _mm_cvttps_epi32(fdivx);
    __m128i divy_i = _mm_cvttps_epi32(fdivy);

    __m128i eq_neg5000 = _mm_cmpeq_epi32(divx_i, v_neg5000);
    __m128i not_clipped_mask = _mm_xor_si128(clipped_mask, _mm_set1_epi32(-1));
    __m128i fix_mask = _mm_and_si128(eq_neg5000, not_clipped_mask);
    __m128i result_x_adj = _mm_or_si128(
        _mm_and_si128(fix_mask, v_neg5001), _mm_andnot_si128(fix_mask, divx_i));
    __m128i final_x = _mm_or_si128(
        _mm_and_si128(clipped_mask, v_neg5000), _mm_andnot_si128(clipped_mask, result_x_adj));
    __m128i final_y =
        _mm_or_si128(_mm_and_si128(clipped_mask, vsy), _mm_andnot_si128(clipped_mask, divy_i));

    *out_final_x = final_x;
    *out_final_y = final_y;
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

    __m128i vz = _mm_loadu_si128((__m128i const*)zbuf);
    __m128i vsx = _mm_loadu_si128((__m128i const*)xbuf);
    __m128i vsy = _mm_loadu_si128((__m128i const*)ybuf);

    __m128i v_near = _mm_set1_epi32(near_plane_z);
    __m128i v_mid = _mm_set1_epi32(model_mid_z);
    __m128i v_neg5000 = _mm_set1_epi32(-5000);
    __m128i v_neg5001 = _mm_set1_epi32(-5001);

    __m128i vscreen_z;
    __m128i final_x;
    __m128i final_y;
    projection_zdiv_sse2_apply(
        vz, vsx, vsy, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

    int out_z[4];
    int out_x[4];
    int out_y[4];
    _mm_storeu_si128((__m128i*)out_z, vscreen_z);
    _mm_storeu_si128((__m128i*)out_x, final_x);
    _mm_storeu_si128((__m128i*)out_y, final_y);
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

    __m128i vz = _mm_loadu_si128((__m128i const*)zbuf);
    __m128i vsx = _mm_loadu_si128((__m128i const*)xbuf);
    __m128i vsy = _mm_loadu_si128((__m128i const*)ybuf);

    __m128i v_near = _mm_set1_epi32(near_plane_z);
    __m128i v_mid = _mm_set1_epi32(model_mid_z);
    __m128i v_neg5000 = _mm_set1_epi32(-5000);
    __m128i v_neg5001 = _mm_set1_epi32(-5001);

    __m128i vscreen_z;
    __m128i final_x;
    __m128i final_y;
    projection_zdiv_sse2_apply(
        vz, vsx, vsy, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

    int out_z[4];
    int out_x[4];
    int out_y[4];
    _mm_storeu_si128((__m128i*)out_z, vscreen_z);
    _mm_storeu_si128((__m128i*)out_x, final_x);
    _mm_storeu_si128((__m128i*)out_y, final_y);
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

    __m128i v_near = _mm_set1_epi32(near_plane_z);
    __m128i v_mid = _mm_set1_epi32(model_mid_z);
    __m128i v_neg5000 = _mm_set1_epi32(-5000);
    __m128i v_neg5001 = _mm_set1_epi32(-5001);

    for( ; i + vsteps - 1 < num_linear_slots; i += vsteps )
    {
        __m128i vz = _mm_loadu_si128((__m128i const*)(orthographic_vertices_z + i));
        __m128i vsx = _mm_loadu_si128((__m128i const*)(screen_vertices_x + i));
        __m128i vsy = _mm_loadu_si128((__m128i const*)(screen_vertices_y + i));

        __m128i vscreen_z;
        __m128i final_x;
        __m128i final_y;
        projection_zdiv_sse2_apply(
            vz, vsx, vsy, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

        _mm_storeu_si128((__m128i*)(screen_vertices_z + i), vscreen_z);
        _mm_storeu_si128((__m128i*)(screen_vertices_x + i), final_x);
        _mm_storeu_si128((__m128i*)(screen_vertices_y + i), final_y);
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

    __m128i v_near = _mm_set1_epi32(near_plane_z);
    __m128i v_mid = _mm_set1_epi32(model_mid_z);
    __m128i v_neg5000 = _mm_set1_epi32(-5000);
    __m128i v_neg5001 = _mm_set1_epi32(-5001);

    for( ; i + vsteps - 1 < num_linear_slots; i += vsteps )
    {
        __m128i vz = _mm_loadu_si128((__m128i const*)(screen_vertices_z + i));
        __m128i vsx = _mm_loadu_si128((__m128i const*)(screen_vertices_x + i));
        __m128i vsy = _mm_loadu_si128((__m128i const*)(screen_vertices_y + i));

        __m128i vscreen_z;
        __m128i final_x;
        __m128i final_y;
        projection_zdiv_sse2_apply(
            vz, vsx, vsy, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

        _mm_storeu_si128((__m128i*)(screen_vertices_z + i), vscreen_z);
        _mm_storeu_si128((__m128i*)(screen_vertices_x + i), final_x);
        _mm_storeu_si128((__m128i*)(screen_vertices_y + i), final_y);
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
