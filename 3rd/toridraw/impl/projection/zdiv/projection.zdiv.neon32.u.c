#ifndef PROJECTION_ZDIV_SIMD_NEON32_U_C
#define PROJECTION_ZDIV_SIMD_NEON32_U_C

/* Second-pass z-div is int32 lane math on int* buffers; vertex width (16 vs 32) only affects pass 1. */
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include <arm_neon.h>
#include <assert.h>

/* Float reciprocal + one Newton-Raphson step on vrecpeq_f32; truncating cvt matches SSE _mm_cvttps_epi32. */
/*
 * Written out twice on purpose. The near-clip question is answered once per
 * model, far above this (ToriDraw_Project), so these are reached only through
 * the family that already knows the answer — no flag to test, no branch to
 * predict per vertex. The no-clip copy keeps the full parameter list so the two
 * families stay call-compatible; its near-plane and sentinel vectors are simply
 * unused and vanish when it inlines.
 */
static inline void
projection_zdiv_neon_apply_clip(
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

static inline void
projection_zdiv_neon_apply_noclip(
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

    float32x4_t z_f = vcvtq_f32_s32(z_i);
    float32x4_t recip = vrecpeq_f32(z_f);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);

    float32x4_t x_f = vcvtq_f32_s32(x_orig);
    float32x4_t y_f = vcvtq_f32_s32(y_orig);
    float32x4_t fdivx = vmulq_f32(x_f, recip);
    float32x4_t fdivy = vmulq_f32(y_f, recip);

    int32x4_t x_div = vcvtq_s32_f32(fdivx);
    int32x4_t y_div = vcvtq_s32_f32(fdivy);

    *out_final_x = x_div;
    *out_final_y = y_div;
}

/* Used by projection16_simd.u.c / projection_simd.u.c for fused 4-lane z-div. */
static inline void
projection_neon_zdiv_tex_4_at_clip(
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
    projection_zdiv_neon_apply_clip(
        z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);
    vst1q_s32(&screen_vertices_z[i], vscreen_z);
    vst1q_s32(&screen_vertices_x[i], final_x);
    vst1q_s32(&screen_vertices_y[i], final_y);
}

static inline void
projection_neon_zdiv_tex_4_at_noclip(
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
    projection_zdiv_neon_apply_noclip(
        z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);
    vst1q_s32(&screen_vertices_z[i], vscreen_z);
    vst1q_s32(&screen_vertices_x[i], final_x);
    vst1q_s32(&screen_vertices_y[i], final_y);
}

static inline void
projection_neon_zdiv_notex_4_at_clip(
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
    projection_zdiv_neon_apply_clip(
        z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);
    vst1q_s32(&screen_vertices_z[i], vscreen_z);
    vst1q_s32(&screen_vertices_x[i], final_x);
    vst1q_s32(&screen_vertices_y[i], final_y);
}

static inline void
projection_neon_zdiv_notex_4_at_noclip(
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
    projection_zdiv_neon_apply_noclip(
        z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);
    vst1q_s32(&screen_vertices_z[i], vscreen_z);
    vst1q_s32(&screen_vertices_x[i], final_x);
    vst1q_s32(&screen_vertices_y[i], final_y);
}

static inline void
projection_neon_zdiv_tex_tail_clip(
    const int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int base,
    int rem,
    int model_mid_z,
    int near_plane_z)
{
    assert(rem >= 0);
    if( rem == 0 )
        return;
    assert(rem < 4);

    for( int j = 0; j < rem; j++ )
    {
        int i = base + j;
        int z = orthographic_vertices_z[i];

        screen_vertices_z[i] = z - model_mid_z;

        if( z < near_plane_z )
        {
            screen_vertices_x[i] = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
        }
        else
        {
            screen_vertices_x[i] = screen_vertices_x[i] / z;
            if( screen_vertices_x[i] == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
                screen_vertices_x[i] = TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE;
            screen_vertices_y[i] = screen_vertices_y[i] / z;
        }
    }
}

static inline void
projection_neon_zdiv_tex_tail_noclip(
    const int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int base,
    int rem,
    int model_mid_z,
    int near_plane_z)
{
    assert(rem >= 0);
    if( rem == 0 )
        return;
    assert(rem < 4);
    (void)near_plane_z;

    for( int j = 0; j < rem; j++ )
    {
        int i = base + j;
        int z = orthographic_vertices_z[i];

        screen_vertices_z[i] = z - model_mid_z;
        screen_vertices_x[i] = screen_vertices_x[i] / z;
        screen_vertices_y[i] = screen_vertices_y[i] / z;
    }
}

static inline void
projection_neon_zdiv_notex_tail_clip(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int base,
    int rem,
    int model_mid_z,
    int near_plane_z)
{
    assert(rem >= 0);
    if( rem == 0 )
        return;
    assert(rem < 4);

    for( int j = 0; j < rem; j++ )
    {
        int i = base + j;
        int z = screen_vertices_z[i];

        screen_vertices_z[i] = z - model_mid_z;

        if( z < near_plane_z )
        {
            screen_vertices_x[i] = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
        }
        else
        {
            screen_vertices_x[i] = screen_vertices_x[i] / z;
            if( screen_vertices_x[i] == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
                screen_vertices_x[i] = TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE;
            screen_vertices_y[i] = screen_vertices_y[i] / z;
        }
    }
}

static inline void
projection_neon_zdiv_notex_tail_noclip(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int base,
    int rem,
    int model_mid_z,
    int near_plane_z)
{
    assert(rem >= 0);
    if( rem == 0 )
        return;
    assert(rem < 4);
    (void)near_plane_z;

    for( int j = 0; j < rem; j++ )
    {
        int i = base + j;
        int z = screen_vertices_z[i];

        screen_vertices_z[i] = z - model_mid_z;
        screen_vertices_x[i] = screen_vertices_x[i] / z;
        screen_vertices_y[i] = screen_vertices_y[i] / z;
    }
}

static inline void
projection_zdiv_tex_neon_clip(
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
        projection_zdiv_neon_apply_clip(
            z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

        vst1q_s32(&screen_vertices_z[zi], vscreen_z);
        vst1q_s32(&screen_vertices_x[zi], final_x);
        vst1q_s32(&screen_vertices_y[zi], final_y);
    }

    projection_neon_zdiv_tex_tail_clip(
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
projection_zdiv_tex_neon_noclip(
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
        projection_zdiv_neon_apply_noclip(
            z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

        vst1q_s32(&screen_vertices_z[zi], vscreen_z);
        vst1q_s32(&screen_vertices_x[zi], final_x);
        vst1q_s32(&screen_vertices_y[zi], final_y);
    }

    projection_neon_zdiv_tex_tail_noclip(
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
projection_zdiv_notex_neon_clip(
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
        projection_zdiv_neon_apply_clip(
            z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

        vst1q_s32(&screen_vertices_z[zi], vscreen_z);
        vst1q_s32(&screen_vertices_x[zi], final_x);
        vst1q_s32(&screen_vertices_y[zi], final_y);
    }

    projection_neon_zdiv_notex_tail_clip(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        zi,
        num_linear_slots - zi,
        model_mid_z,
        near_plane_z);
}

static inline void
projection_zdiv_notex_neon_noclip(
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
        projection_zdiv_neon_apply_noclip(
            z_i, x, y, v_near, v_mid, v_neg5000, v_neg5001, &vscreen_z, &final_x, &final_y);

        vst1q_s32(&screen_vertices_z[zi], vscreen_z);
        vst1q_s32(&screen_vertices_x[zi], final_x);
        vst1q_s32(&screen_vertices_y[zi], final_y);
    }

    projection_neon_zdiv_notex_tail_noclip(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        zi,
        num_linear_slots - zi,
        model_mid_z,
        near_plane_z);
}

#endif /* NEON */

#endif /* PROJECTION_ZDIV_SIMD_NEON32_U_C */
