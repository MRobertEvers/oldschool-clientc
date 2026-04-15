#ifndef PROJECTION_ZDIV_SIMD_NEON_U_C
#define PROJECTION_ZDIV_SIMD_NEON_U_C

/* Second-pass z-div is int32 lane math on int* buffers; vertex width (16 vs 32) only affects pass 1. */
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include <arm_neon.h>
#include <assert.h>
#include <limits.h>

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

    int32x4_t midz = vdupq_n_s32(model_mid_z);
    int32x4_t outz = vsubq_s32(z_i, midz);
    vst1q_s32(&screen_vertices_z[i], outz);

    uint32x4_t clipped_mask = vcltq_s32(z_i, vdupq_n_s32(near_plane_z));

    float32x4_t z_f = vcvtq_f32_s32(z_i);
    float32x4_t recip = vrecpeq_f32(z_f);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);

    float32x4_t scale = vdupq_n_f32((float)(1ll << 30));
    float32x4_t recip_scaled = vmulq_f32(recip, scale);
    int32x4_t recip_q31 = vcvtq_s32_f32(recip_scaled);

    int32x4_t x = vld1q_s32(&screen_vertices_x[i]);
    int32x4_t y = vld1q_s32(&screen_vertices_y[i]);

    int64x2_t xl0 = vmull_s32(vget_low_s32(x), vget_low_s32(recip_q31));
    int64x2_t xl1 = vmull_s32(vget_high_s32(x), vget_high_s32(recip_q31));
    int64x2_t yl0 = vmull_s32(vget_low_s32(y), vget_low_s32(recip_q31));
    int64x2_t yl1 = vmull_s32(vget_high_s32(y), vget_high_s32(recip_q31));

    int32x4_t x_div = vcombine_s32(vshrn_n_s64(xl0, 30), vshrn_n_s64(xl1, 30));
    int32x4_t y_div = vcombine_s32(vshrn_n_s64(yl0, 30), vshrn_n_s64(yl1, 30));

    int32x4_t neg5000 = vdupq_n_s32(-5000);
    x_div = vbslq_s32(clipped_mask, neg5000, x_div);

    vst1q_s32(&screen_vertices_x[i], x_div);
    vst1q_s32(&screen_vertices_y[i], y_div);
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

    int32x4_t midz = vdupq_n_s32(model_mid_z);
    int32x4_t outz = vsubq_s32(z_i, midz);

    uint32x4_t clipped_mask = vcltq_s32(z_i, vdupq_n_s32(near_plane_z));

    float32x4_t z_f = vcvtq_f32_s32(z_i);
    float32x4_t recip = vrecpeq_f32(z_f);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);

    float32x4_t scale = vdupq_n_f32((float)(1ll << 30));
    float32x4_t recip_scaled = vmulq_f32(recip, scale);
    int32x4_t recip_q31 = vcvtq_s32_f32(recip_scaled);

    int32x4_t x = vld1q_s32(&screen_vertices_x[i]);
    int32x4_t y = vld1q_s32(&screen_vertices_y[i]);

    int64x2_t xl0 = vmull_s32(vget_low_s32(x), vget_low_s32(recip_q31));
    int64x2_t xl1 = vmull_s32(vget_high_s32(x), vget_high_s32(recip_q31));
    int64x2_t yl0 = vmull_s32(vget_low_s32(y), vget_low_s32(recip_q31));
    int64x2_t yl1 = vmull_s32(vget_high_s32(y), vget_high_s32(recip_q31));

    int32x4_t x_div = vcombine_s32(vshrn_n_s64(xl0, 30), vshrn_n_s64(xl1, 30));
    int32x4_t y_div = vcombine_s32(vshrn_n_s64(yl0, 30), vshrn_n_s64(yl1, 30));

    int32x4_t neg5000 = vdupq_n_s32(-5000);
    x_div = vbslq_s32(clipped_mask, neg5000, x_div);

    vst1q_s32(&screen_vertices_x[i], x_div);
    vst1q_s32(&screen_vertices_y[i], y_div);
    vst1q_s32(&screen_vertices_z[i], outz);
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
    assert(rem <= 3);

    int z_pad = (near_plane_z < INT_MAX) ? (near_plane_z + 1) : near_plane_z;
    int32x4_t z_i = vdupq_n_s32(z_pad);
    int32x4_t x = vdupq_n_s32(0);
    int32x4_t y = vdupq_n_s32(0);
    if( rem == 1 )
    {
        z_i = vsetq_lane_s32(orthographic_vertices_z[base], z_i, 0);
        x = vsetq_lane_s32(screen_vertices_x[base], x, 0);
        y = vsetq_lane_s32(screen_vertices_y[base], y, 0);
    }
    else if( rem == 2 )
    {
        z_i = vsetq_lane_s32(orthographic_vertices_z[base], z_i, 0);
        z_i = vsetq_lane_s32(orthographic_vertices_z[base + 1], z_i, 1);
        x = vsetq_lane_s32(screen_vertices_x[base], x, 0);
        x = vsetq_lane_s32(screen_vertices_x[base + 1], x, 1);
        y = vsetq_lane_s32(screen_vertices_y[base], y, 0);
        y = vsetq_lane_s32(screen_vertices_y[base + 1], y, 1);
    }
    else
    {
        z_i = vsetq_lane_s32(orthographic_vertices_z[base], z_i, 0);
        z_i = vsetq_lane_s32(orthographic_vertices_z[base + 1], z_i, 1);
        z_i = vsetq_lane_s32(orthographic_vertices_z[base + 2], z_i, 2);
        x = vsetq_lane_s32(screen_vertices_x[base], x, 0);
        x = vsetq_lane_s32(screen_vertices_x[base + 1], x, 1);
        x = vsetq_lane_s32(screen_vertices_x[base + 2], x, 2);
        y = vsetq_lane_s32(screen_vertices_y[base], y, 0);
        y = vsetq_lane_s32(screen_vertices_y[base + 1], y, 1);
        y = vsetq_lane_s32(screen_vertices_y[base + 2], y, 2);
    }

    int32x4_t midz = vdupq_n_s32(model_mid_z);
    int32x4_t outz = vsubq_s32(z_i, midz);

    uint32x4_t clipped_mask = vcltq_s32(z_i, vdupq_n_s32(near_plane_z));

    float32x4_t z_f = vcvtq_f32_s32(z_i);
    float32x4_t recip = vrecpeq_f32(z_f);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);

    float32x4_t scale = vdupq_n_f32((float)(1ll << 30));
    float32x4_t recip_scaled = vmulq_f32(recip, scale);
    int32x4_t recip_q31 = vcvtq_s32_f32(recip_scaled);

    int64x2_t xl0 = vmull_s32(vget_low_s32(x), vget_low_s32(recip_q31));
    int64x2_t xl1 = vmull_s32(vget_high_s32(x), vget_high_s32(recip_q31));
    int64x2_t yl0 = vmull_s32(vget_low_s32(y), vget_low_s32(recip_q31));
    int64x2_t yl1 = vmull_s32(vget_high_s32(y), vget_high_s32(recip_q31));

    int32x4_t x_div = vcombine_s32(vshrn_n_s64(xl0, 30), vshrn_n_s64(xl1, 30));
    int32x4_t y_div = vcombine_s32(vshrn_n_s64(yl0, 30), vshrn_n_s64(yl1, 30));

    int32x4_t neg5000 = vdupq_n_s32(-5000);
    x_div = vbslq_s32(clipped_mask, neg5000, x_div);

    if( rem >= 1 )
    {
        screen_vertices_x[base] = vgetq_lane_s32(x_div, 0);
        screen_vertices_y[base] = vgetq_lane_s32(y_div, 0);
        screen_vertices_z[base] = vgetq_lane_s32(outz, 0);
    }
    if( rem >= 2 )
    {
        screen_vertices_x[base + 1] = vgetq_lane_s32(x_div, 1);
        screen_vertices_y[base + 1] = vgetq_lane_s32(y_div, 1);
        screen_vertices_z[base + 1] = vgetq_lane_s32(outz, 1);
    }
    if( rem >= 3 )
    {
        screen_vertices_x[base + 2] = vgetq_lane_s32(x_div, 2);
        screen_vertices_y[base + 2] = vgetq_lane_s32(y_div, 2);
        screen_vertices_z[base + 2] = vgetq_lane_s32(outz, 2);
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
    assert(rem <= 3);

    int z_pad = (near_plane_z < INT_MAX) ? (near_plane_z + 1) : near_plane_z;
    int32x4_t z_i = vdupq_n_s32(z_pad);
    int32x4_t x = vdupq_n_s32(0);
    int32x4_t y = vdupq_n_s32(0);
    if( rem == 1 )
    {
        z_i = vsetq_lane_s32(screen_vertices_z[base], z_i, 0);
        x = vsetq_lane_s32(screen_vertices_x[base], x, 0);
        y = vsetq_lane_s32(screen_vertices_y[base], y, 0);
    }
    else if( rem == 2 )
    {
        z_i = vsetq_lane_s32(screen_vertices_z[base], z_i, 0);
        z_i = vsetq_lane_s32(screen_vertices_z[base + 1], z_i, 1);
        x = vsetq_lane_s32(screen_vertices_x[base], x, 0);
        x = vsetq_lane_s32(screen_vertices_x[base + 1], x, 1);
        y = vsetq_lane_s32(screen_vertices_y[base], y, 0);
        y = vsetq_lane_s32(screen_vertices_y[base + 1], y, 1);
    }
    else
    {
        z_i = vsetq_lane_s32(screen_vertices_z[base], z_i, 0);
        z_i = vsetq_lane_s32(screen_vertices_z[base + 1], z_i, 1);
        z_i = vsetq_lane_s32(screen_vertices_z[base + 2], z_i, 2);
        x = vsetq_lane_s32(screen_vertices_x[base], x, 0);
        x = vsetq_lane_s32(screen_vertices_x[base + 1], x, 1);
        x = vsetq_lane_s32(screen_vertices_x[base + 2], x, 2);
        y = vsetq_lane_s32(screen_vertices_y[base], y, 0);
        y = vsetq_lane_s32(screen_vertices_y[base + 1], y, 1);
        y = vsetq_lane_s32(screen_vertices_y[base + 2], y, 2);
    }

    int32x4_t midz = vdupq_n_s32(model_mid_z);
    int32x4_t outz = vsubq_s32(z_i, midz);

    uint32x4_t clipped_mask = vcltq_s32(z_i, vdupq_n_s32(near_plane_z));

    float32x4_t z_f = vcvtq_f32_s32(z_i);
    float32x4_t recip = vrecpeq_f32(z_f);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);

    float32x4_t scale = vdupq_n_f32((float)(1ll << 30));
    float32x4_t recip_scaled = vmulq_f32(recip, scale);
    int32x4_t recip_q31 = vcvtq_s32_f32(recip_scaled);

    int64x2_t xl0 = vmull_s32(vget_low_s32(x), vget_low_s32(recip_q31));
    int64x2_t xl1 = vmull_s32(vget_high_s32(x), vget_high_s32(recip_q31));
    int64x2_t yl0 = vmull_s32(vget_low_s32(y), vget_low_s32(recip_q31));
    int64x2_t yl1 = vmull_s32(vget_high_s32(y), vget_high_s32(recip_q31));

    int32x4_t x_div = vcombine_s32(vshrn_n_s64(xl0, 30), vshrn_n_s64(xl1, 30));
    int32x4_t y_div = vcombine_s32(vshrn_n_s64(yl0, 30), vshrn_n_s64(yl1, 30));

    int32x4_t neg5000 = vdupq_n_s32(-5000);
    x_div = vbslq_s32(clipped_mask, neg5000, x_div);

    if( rem >= 1 )
    {
        screen_vertices_x[base] = vgetq_lane_s32(x_div, 0);
        screen_vertices_y[base] = vgetq_lane_s32(y_div, 0);
        screen_vertices_z[base] = vgetq_lane_s32(outz, 0);
    }
    if( rem >= 2 )
    {
        screen_vertices_x[base + 1] = vgetq_lane_s32(x_div, 1);
        screen_vertices_y[base + 1] = vgetq_lane_s32(y_div, 1);
        screen_vertices_z[base + 1] = vgetq_lane_s32(outz, 1);
    }
    if( rem >= 3 )
    {
        screen_vertices_x[base + 2] = vgetq_lane_s32(x_div, 2);
        screen_vertices_y[base + 2] = vgetq_lane_s32(y_div, 2);
        screen_vertices_z[base + 2] = vgetq_lane_s32(outz, 2);
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
    int zi = 0;
    for( ; zi + 4 <= num_linear_slots; zi += 4 )
    {
        projection_neon_zdiv_tex_4_at(
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            zi,
            model_mid_z,
            near_plane_z);
    }
    if( zi < num_linear_slots )
    {
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
    int zi = 0;
    for( ; zi + 4 <= num_linear_slots; zi += 4 )
    {
        projection_neon_zdiv_notex_4_at(
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            zi,
            model_mid_z,
            near_plane_z);
    }
    if( zi < num_linear_slots )
    {
        projection_neon_zdiv_notex_tail(
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            zi,
            num_linear_slots - zi,
            model_mid_z,
            near_plane_z);
    }
}

#endif /* NEON */

#endif /* PROJECTION_ZDIV_SIMD_NEON_U_C */
