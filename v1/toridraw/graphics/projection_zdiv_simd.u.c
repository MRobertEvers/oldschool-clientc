#ifndef PROJECTION_ZDIV_SIMD_U_C
#define PROJECTION_ZDIV_SIMD_U_C

#include <stdint.h>

#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "projection_zdiv_simd.neon.u.c"
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "projection_zdiv_simd.scalar.u.c"
#include "projection_zdiv_simd.avx.u.c"
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "projection_zdiv_simd.scalar.u.c"
#include "projection_zdiv_simd.sse41.u.c"
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "projection_zdiv_simd.scalar.u.c"
#include "projection_zdiv_simd.sse2.u.c"
#else
#include "projection_zdiv_simd.scalar.u.c"
#endif

static inline void
projection_zdiv_pass_tex(
    const int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int num_linear_slots,
    int model_mid_z,
    int near_plane_z)
{
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
    projection_zdiv_tex_neon(
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
    projection_zdiv_tex_avx2(
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
    projection_zdiv_tex_sse41(
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
    projection_zdiv_tex_sse2(
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
#else
    projection_zdiv_tex_scalar_range(
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        0,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
#endif
}

/* Reads/writes screen vertex buffers only; orthographic arrays are never touched. */
static inline void
projection_zdiv_pass_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int num_linear_slots,
    int model_mid_z,
    int near_plane_z)
{
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
    projection_zdiv_notex_neon(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
    projection_zdiv_notex_avx2(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
    projection_zdiv_notex_sse41(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
    projection_zdiv_notex_sse2(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
#else
    projection_zdiv_notex_scalar_range(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        0,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
#endif
}

#endif
