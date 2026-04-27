#ifndef TORIRS_PLATFORM_KIT_TRSPK_MATH_H
#define TORIRS_PLATFORM_KIT_TRSPK_MATH_H

#include "trspk_types.h"

#include "graphics/dash.h"
#include "graphics/uv_pnm.h"

#include <math.h>
#include <string.h>

#ifndef TRSPK_PI
#define TRSPK_PI 3.14159265358979323846f
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline float
trspk_vec3_dot(
    TRSPK_Vec3 a,
    TRSPK_Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline TRSPK_Vec3
trspk_vec3_cross(
    TRSPK_Vec3 a,
    TRSPK_Vec3 b)
{
    TRSPK_Vec3 o;
    o.x = a.y * b.z - a.z * b.y;
    o.y = a.z * b.x - a.x * b.z;
    o.z = a.x * b.y - a.y * b.x;
    return o;
}

static inline TRSPK_Vec3
trspk_vec3_normalize(TRSPK_Vec3 v)
{
    const float len = sqrtf(trspk_vec3_dot(v, v));
    if( len <= 0.000001f )
        return v;
    v.x /= len;
    v.y /= len;
    v.z /= len;
    return v;
}

static inline void
trspk_mat4_identity(float out[16])
{
    memset(out, 0, sizeof(float) * 16u);
    out[0] = 1.0f;
    out[5] = 1.0f;
    out[10] = 1.0f;
    out[15] = 1.0f;
}

static inline void
trspk_mat4_multiply(
    float out[16],
    const float a[16],
    const float b[16])
{
    float r[16];
    for( int row = 0; row < 4; ++row )
    {
        for( int col = 0; col < 4; ++col )
        {
            r[col + row * 4] = a[row * 4 + 0] * b[col + 0] + a[row * 4 + 1] * b[col + 4] +
                               a[row * 4 + 2] * b[col + 8] + a[row * 4 + 3] * b[col + 12];
        }
    }
    memcpy(out, r, sizeof(r));
}

static inline void
trspk_mat4_ortho(
    float out[16],
    float left,
    float right,
    float bottom,
    float top,
    float near_z,
    float far_z)
{
    trspk_mat4_identity(out);
    out[0] = 2.0f / (right - left);
    out[5] = 2.0f / (top - bottom);
    out[10] = -2.0f / (far_z - near_z);
    out[12] = -(right + left) / (right - left);
    out[13] = -(top + bottom) / (top - bottom);
    out[14] = -(far_z + near_z) / (far_z - near_z);
}

static inline void
trspk_mat4_perspective(
    float out[16],
    float fov_rad,
    float aspect,
    float near_z,
    float far_z)
{
    const float f = 1.0f / tanf(fov_rad * 0.5f);
    memset(out, 0, sizeof(float) * 16u);
    out[0] = f / aspect;
    out[5] = f;
    out[10] = (far_z + near_z) / (near_z - far_z);
    out[11] = -1.0f;
    out[14] = (2.0f * far_z * near_z) / (near_z - far_z);
}

static inline void
trspk_mat4_translate(
    float out[16],
    float x,
    float y,
    float z)
{
    trspk_mat4_identity(out);
    out[12] = x;
    out[13] = y;
    out[14] = z;
}

static inline void
trspk_mat4_scale(
    float out[16],
    float x,
    float y,
    float z)
{
    trspk_mat4_identity(out);
    out[0] = x;
    out[5] = y;
    out[10] = z;
}

static inline void
trspk_mat4_rotate_y(
    float out[16],
    float radians)
{
    const float c = cosf(radians);
    const float s = sinf(radians);
    trspk_mat4_identity(out);
    out[0] = c;
    out[2] = -s;
    out[8] = s;
    out[10] = c;
}

static inline void
trspk_mat4_look_at(
    float out[16],
    TRSPK_Vec3 eye,
    TRSPK_Vec3 center,
    TRSPK_Vec3 up)
{
    TRSPK_Vec3 f = { center.x - eye.x, center.y - eye.y, center.z - eye.z };
    f = trspk_vec3_normalize(f);
    TRSPK_Vec3 s = trspk_vec3_normalize(trspk_vec3_cross(f, up));
    TRSPK_Vec3 u = trspk_vec3_cross(s, f);

    trspk_mat4_identity(out);
    out[0] = s.x;
    out[4] = s.y;
    out[8] = s.z;
    out[1] = u.x;
    out[5] = u.y;
    out[9] = u.z;
    out[2] = -f.x;
    out[6] = -f.y;
    out[10] = -f.z;
    out[12] = -trspk_vec3_dot(s, eye);
    out[13] = -trspk_vec3_dot(u, eye);
    out[14] = trspk_vec3_dot(f, eye);
}

float
trspk_dash_yaw_to_radians(int32_t yaw_2048);

/** Match metal_texture_animation_signed / webgl1_texture_animation_signed (atlas tile encoding). */
float
trspk_texture_animation_signed(int animation_direction, int animation_speed);

struct GGame;

/** Logical 3D pass rect from game viewport (same rules as trspk_webgl1_compute_default_pass_logical). */
void
trspk_pass_logical_from_game(
    TRSPK_Rect* out,
    int window_width,
    int window_height,
    const struct GGame* game);

/**
 * Map logical (window-space top-left) rect to GL viewport pixels (origin lower-left).
 * On failure or invalid inputs, writes full drawable (0,0,fb_w,fb_h) in GL convention.
 */
void
trspk_logical_rect_to_gl_viewport_pixels(
    TRSPK_Rect* out_gl_lower_left,
    uint32_t fb_w,
    uint32_t fb_h,
    uint32_t win_w,
    uint32_t win_h,
    const TRSPK_Rect* logical);

static inline void
trspk_compute_view_matrix(
    float out[16],
    float cam_x,
    float cam_y,
    float cam_z,
    float pitch_rad,
    float yaw_rad)
{
    const float cp = cosf(pitch_rad);
    const float sp = sinf(pitch_rad);
    const float cy = cosf(yaw_rad);
    const float sy = sinf(yaw_rad);

    TRSPK_Vec3 eye = { cam_x, cam_y, cam_z };
    TRSPK_Vec3 forward = { sy * cp, -sp, cy * cp };
    TRSPK_Vec3 center = { eye.x + forward.x, eye.y + forward.y, eye.z + forward.z };
    TRSPK_Vec3 up = { 0.0f, 1.0f, 0.0f };
    trspk_mat4_look_at(out, eye, center, up);
}

static inline void
trspk_compute_projection_matrix(
    float out[16],
    float fov_rad,
    float width,
    float height)
{
    const float aspect = height > 0.0f ? width / height : 1.0f;
    trspk_mat4_perspective(out, fov_rad, aspect > 0.0f ? aspect : 1.0f, 50.0f, 10000.0f);
}

/**
 * Match platforms/common/pass_3d_builder/gpu_3d_cache2.h gpu3d_hsl16_to_float_rgba
 * (Dash palette via dash_hsl16_to_rgb, not generic HSL).
 */
static inline void
trspk_hsl16_to_rgba(
    uint16_t hsl16,
    float rgba_out[4],
    float alpha)
{
    const uint32_t rgb = (uint32_t)dash_hsl16_to_rgb((int)(hsl16 & 0xFFFFu));
    rgba_out[0] = (float)((rgb >> 16) & 0xFFu) / 255.0f;
    rgba_out[1] = (float)((rgb >> 8) & 0xFFu) / 255.0f;
    rgba_out[2] = (float)(rgb & 0xFFu) / 255.0f;
    rgba_out[3] = alpha;
}

static inline TRSPK_BakeTransform
trspk_bake_transform_identity(void)
{
    TRSPK_BakeTransform o;
    o.cos_yaw = 1.0f;
    o.sin_yaw = 0.0f;
    o.tx = 0.0f;
    o.ty = 0.0f;
    o.tz = 0.0f;
    o.identity = true;
    return o;
}

static inline TRSPK_BakeTransform
trspk_bake_transform_from_yaw_translate(
    int32_t world_x,
    int32_t world_y,
    int32_t world_z,
    int32_t yaw_r2pi2048)
{
    int32_t ymod = yaw_r2pi2048 % 2048;
    if( ymod < 0 )
        ymod += 2048;
    TRSPK_BakeTransform o = trspk_bake_transform_identity();
    o.identity = (world_x == 0 && world_y == 0 && world_z == 0 && ymod == 0);
    if( o.identity )
        return o;
    const float yaw_rad = trspk_dash_yaw_to_radians(yaw_r2pi2048);
    o.cos_yaw = cosf(yaw_rad);
    o.sin_yaw = sinf(yaw_rad);
    o.tx = (float)world_x;
    o.ty = (float)world_y;
    o.tz = (float)world_z;
    return o;
}

static inline void
trspk_bake_transform_apply(
    const TRSPK_BakeTransform* bake,
    float* vx,
    float* vy,
    float* vz)
{
    if( !bake || bake->identity )
        return;
    const float lx = *vx;
    const float lz = *vz;
    *vx = lx * bake->cos_yaw + lz * bake->sin_yaw + bake->tx;
    *vy = *vy + bake->ty;
    *vz = -lx * bake->sin_yaw + lz * bake->cos_yaw + bake->tz;
}

static inline void
trspk_uv_pnm_compute(
    TRSPK_UVFaceCoords* out,
    float p_x,
    float p_y,
    float p_z,
    float m_x,
    float m_y,
    float m_z,
    float n_x,
    float n_y,
    float n_z,
    float a_x,
    float a_y,
    float a_z,
    float b_x,
    float b_y,
    float b_z,
    float c_x,
    float c_y,
    float c_z)
{
    uv_pnm_compute(
        (struct UVFaceCoords*)out,
        p_x,
        p_y,
        p_z,
        m_x,
        m_y,
        m_z,
        n_x,
        n_y,
        n_z,
        a_x,
        a_y,
        a_z,
        b_x,
        b_y,
        b_z,
        c_x,
        c_y,
        c_z);
}

static inline void
trspk_upscale_64_to_128_rgba(
    const uint8_t* src64,
    uint8_t* dst128)
{
    for( uint32_t y = 0; y < 64u; ++y )
    {
        for( uint32_t x = 0; x < 64u; ++x )
        {
            const uint8_t* src = src64 + ((y * 64u + x) * 4u);
            for( uint32_t dy = 0; dy < 2u; ++dy )
            {
                for( uint32_t dx = 0; dx < 2u; ++dx )
                {
                    uint8_t* dst = dst128 + (((y * 2u + dy) * 128u + (x * 2u + dx)) * 4u);
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = src[3];
                }
            }
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif
