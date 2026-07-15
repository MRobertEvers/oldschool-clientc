#ifndef PROJECTION_ZDIV_SIMD_NEON_U_C
#define PROJECTION_ZDIV_SIMD_NEON_U_C

/* Second-pass z-div is int32 lane math on int* buffers; vertex width (16 vs 32) only affects pass 1. */
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include <arm_neon.h>
#include <assert.h>
#include <limits.h>

/* Float reciprocal + one Newton-Raphson step on vrecpeq_f32; truncating cvt matches SSE _mm_cvttps_epi32. */
static inline void
projection_zdiv_neon_apply(
    int32x4_t z_i,
    int32x4_t x_orig,
    int32x4_t y_orig,
    int32x4_t v_near,
    int32x4_t v_mid,
    int32x4_t v_neg5000,
    int32x4_t v_neg5001,
    int32x4_t* out_vscreen_z,
    int32x4_t* out_final_x,
    int32x4_t* out_final_y)
{
    int32x4_t vscreen_z = vsubq_s32(z_i, v_mid);
    *out_vscreen_z = vscreen_z;

    uint32x4_t clipped_mask = vcltq_s32(z_i, v_near);

    float32x4_t z_f = vcvtq_f32_s32(z_i);
    float32x4_t recip = vrecpeq_f32(z_f);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);

    float32x4_t x_f = vcvtq_f32_s32(x_orig);
    float32x4_t y_f = vcvtq_f32_s32(y_orig);
    float32x4_t fdivx = vmulq_f32(x_f, recip);
    float32x4_t fdivy = vmulq_f32(y_f, recip);

    int32x4_t x_div = vcvtq_s32_f32(fdivx);
    int32x4_t y_div = vcvtq_s32_f32(fdivy);

    uint32x4_t not_clipped = vmvnq_u32(clipped_mask);
    uint32x4_t eq_neg5000 = vceqq_s32(x_div, v_neg5000);
    uint32x4_t fix_mask = vandq_u32(eq_neg5000, not_clipped);
    int32x4_t x_adj = vbslq_s32(fix_mask, v_neg5001, x_div);
    int32x4_t final_x = vbslq_s32(clipped_mask, v_neg5000, x_adj);
    int32x4_t final_y = vbslq_s32(clipped_mask, y_orig, y_div);

    *out_final_x = final_x;
    *out_final_y = final_y;
}

/* Used by projection16_simd.u.c / projection_simd.u.c for fused 4-lane z-div. */
static inline void
projection_neon_zdiv_tex_4_at(
    const int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int i,
    int model_mid_z,
    int near_plane_z)
{
    int32x4_t z_i = vld1q_s32(&orthographic_vertices_z[i]);
    int32x4_t x = vld1q_s32(&screen_vertices_x[i]);
    int32x4_t y = vld1q_s32(&screen_vertices_y[i]);
    int32x4_t v_near = vdupq_n_s32(near_plane_z);
    int32x4_t v_mid = vdupq_n_s32(model_mid_z);
    int32x4_t v_neg5000 = vdupq_n_s32(-5000);
    int32x4_t v_neg5001 = vdupq_n_s32(-5001);
    int32x4_t vscreen_z;
    int32x4_t final_x;
    int32x4_t final_y;
    projection_zdiv_neon_apply(
        z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);
    vst1q_s32(&screen_vertices_z[i], vscreen_z);
    vst1q_s32(&screen_vertices_x[i], final_x);
    vst1q_s32(&screen_vertices_y[i], final_y);
}

static inline void
projection_neon_zdiv_notex_4_at(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int i,
    int model_mid_z,
    int near_plane_z)
{
    int32x4_t z_i = vld1q_s32(&screen_vertices_z[i]);
    int32x4_t x = vld1q_s32(&screen_vertices_x[i]);
    int32x4_t y = vld1q_s32(&screen_vertices_y[i]);
    int32x4_t v_near = vdupq_n_s32(near_plane_z);
    int32x4_t v_mid = vdupq_n_s32(model_mid_z);
    int32x4_t v_neg5000 = vdupq_n_s32(-5000);
    int32x4_t v_neg5001 = vdupq_n_s32(-5001);
    int32x4_t vscreen_z;
    int32x4_t final_x;
    int32x4_t final_y;
    projection_zdiv_neon_apply(
        z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);
    vst1q_s32(&screen_vertices_z[i], vscreen_z);
    vst1q_s32(&screen_vertices_x[i], final_x);
    vst1q_s32(&screen_vertices_y[i], final_y);
}

static inline void
projection_neon_zdiv_tex_tail(
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

    int32x4_t z_i = vld1q_s32(zbuf);
    int32x4_t x = vld1q_s32(xbuf);
    int32x4_t y = vld1q_s32(ybuf);

    int32x4_t v_near = vdupq_n_s32(near_plane_z);
    int32x4_t v_mid = vdupq_n_s32(model_mid_z);
    int32x4_t v_neg5000 = vdupq_n_s32(-5000);
    int32x4_t v_neg5001 = vdupq_n_s32(-5001);

    int32x4_t vscreen_z;
    int32x4_t final_x;
    int32x4_t final_y;
    projection_zdiv_neon_apply(
        z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

    int out_z[4];
    int out_x[4];
    int out_y[4];
    vst1q_s32(out_z, vscreen_z);
    vst1q_s32(out_x, final_x);
    vst1q_s32(out_y, final_y);
    for( int j = 0; j < rem; j++ )
    {
        screen_vertices_z[base + j] = out_z[j];
        screen_vertices_x[base + j] = out_x[j];
        screen_vertices_y[base + j] = out_y[j];
    }
}

static inline void
projection_neon_zdiv_notex_tail(
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

    int32x4_t z_i = vld1q_s32(zbuf);
    int32x4_t x = vld1q_s32(xbuf);
    int32x4_t y = vld1q_s32(ybuf);

    int32x4_t v_near = vdupq_n_s32(near_plane_z);
    int32x4_t v_mid = vdupq_n_s32(model_mid_z);
    int32x4_t v_neg5000 = vdupq_n_s32(-5000);
    int32x4_t v_neg5001 = vdupq_n_s32(-5001);

    int32x4_t vscreen_z;
    int32x4_t final_x;
    int32x4_t final_y;
    projection_zdiv_neon_apply(
        z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

    int out_z[4];
    int out_x[4];
    int out_y[4];
    vst1q_s32(out_z, vscreen_z);
    vst1q_s32(out_x, final_x);
    vst1q_s32(out_y, final_y);
    for( int j = 0; j < rem; j++ )
    {
        screen_vertices_z[base + j] = out_z[j];
        screen_vertices_x[base + j] = out_x[j];
        screen_vertices_y[base + j] = out_y[j];
    }
}

static inline void
projection_zdiv_tex_neon(
    const int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int num_linear_slots,
    int model_mid_z,
    int near_plane_z)
{
    const int vsteps = 4;
    int zi = 0;

    int32x4_t v_near = vdupq_n_s32(near_plane_z);
    int32x4_t v_mid = vdupq_n_s32(model_mid_z);
    int32x4_t v_neg5000 = vdupq_n_s32(-5000);
    int32x4_t v_neg5001 = vdupq_n_s32(-5001);

    for( ; zi + vsteps - 1 < num_linear_slots; zi += vsteps )
    {
        int32x4_t z_i = vld1q_s32(&orthographic_vertices_z[zi]);
        int32x4_t x = vld1q_s32(&screen_vertices_x[zi]);
        int32x4_t y = vld1q_s32(&screen_vertices_y[zi]);

        int32x4_t vscreen_z;
        int32x4_t final_x;
        int32x4_t final_y;
        projection_zdiv_neon_apply(
            z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

        vst1q_s32(&screen_vertices_z[zi], vscreen_z);
        vst1q_s32(&screen_vertices_x[zi], final_x);
        vst1q_s32(&screen_vertices_y[zi], final_y);
    }

    projection_neon_zdiv_tex_tail(
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        zi,
        num_linear_slots - zi,
        model_mid_z,
        near_plane_z);
}

static inline void
projection_zdiv_notex_neon(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int num_linear_slots,
    int model_mid_z,
    int near_plane_z)
{
    const int vsteps = 4;
    int zi = 0;

    int32x4_t v_near = vdupq_n_s32(near_plane_z);
    int32x4_t v_mid = vdupq_n_s32(model_mid_z);
    int32x4_t v_neg5000 = vdupq_n_s32(-5000);
    int32x4_t v_neg5001 = vdupq_n_s32(-5001);

    for( ; zi + vsteps - 1 < num_linear_slots; zi += vsteps )
    {
        int32x4_t z_i = vld1q_s32(&screen_vertices_z[zi]);
        int32x4_t x = vld1q_s32(&screen_vertices_x[zi]);
        int32x4_t y = vld1q_s32(&screen_vertices_y[zi]);

        int32x4_t vscreen_z;
        int32x4_t final_x;
        int32x4_t final_y;
        projection_zdiv_neon_apply(
            z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

        vst1q_s32(&screen_vertices_z[zi], vscreen_z);
        vst1q_s32(&screen_vertices_x[zi], final_x);
        vst1q_s32(&screen_vertices_y[zi], final_y);
    }

    projection_neon_zdiv_notex_tail(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        zi,
        num_linear_slots - zi,
        model_mid_z,
        near_plane_z);
}

#endif /* NEON */

#endif /* PROJECTION_ZDIV_SIMD_NEON_U_C */
