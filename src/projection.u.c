#ifndef PROJECTION_U_C
#define PROJECTION_U_C

#include "projection.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern int g_cos_table[2048];
extern int g_sin_table[2048];
extern int g_tan_table[2048];

/**
 * Treats the camera as if it is at the origin (0, 0, 0)
 *
 * scene_x, scene_y, scene_z is the coordinates of the models origin relative to the camera.
 *
 * z points into the screen.
 * x points to the right.
 * y points up
 */
static inline struct ProjectedVertex
project_orthographic(
    int x,
    int y,
    int z,
    int pitch,
    int yaw,
    int roll,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    struct ProjectedVertex projected_vertex = { 0 };

    assert(camera_pitch >= 0 && camera_pitch < 2048);
    assert(camera_yaw >= 0 && camera_yaw < 2048);
    assert(camera_roll >= 0 && camera_roll < 2048);
    assert(yaw >= 0 && yaw < 2048);
    assert(pitch >= 0 && pitch < 2048);
    assert(roll >= 0 && roll < 2048);

    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];
    int cos_camera_roll = g_cos_table[camera_roll];
    int sin_camera_roll = g_sin_table[camera_roll];

    // Apply model rotation
    int sin_pitch = g_sin_table[pitch];
    int cos_pitch = g_cos_table[pitch];
    int sin_yaw = g_sin_table[yaw];
    int cos_yaw = g_cos_table[yaw];
    int sin_roll = g_sin_table[roll];
    int cos_roll = g_cos_table[roll];

    // Rotate around Y-axis (yaw)
    int x_rotated = x * cos_yaw + z * sin_yaw;
    x_rotated >>= 16;
    int z_rotated = z * cos_yaw - x * sin_yaw;
    z_rotated >>= 16;

    // Rotate around X-axis (pitch)
    int y_rotated = y * cos_pitch - z_rotated * sin_pitch;
    y_rotated >>= 16;
    int z_rotated2 = y * sin_pitch + z_rotated * cos_pitch;
    z_rotated2 >>= 16;

    // Rotate around Z-axis (roll)
    int x_final = x_rotated * cos_roll + y_rotated * sin_roll;
    x_final >>= 16;
    int y_final = y_rotated * cos_roll - x_rotated * sin_roll;
    y_final >>= 16;

    // Translate points relative to camera position
    x_final += scene_x;
    y_final += scene_y;
    z_rotated2 += scene_z;

    // Apply perspective rotation
    // First rotate around Y-axis (scene yaw)
    int x_scene = x_final * cos_camera_yaw + z_rotated2 * sin_camera_yaw;
    x_scene >>= 16;
    int z_scene = z_rotated2 * cos_camera_yaw - x_final * sin_camera_yaw;
    z_scene >>= 16;

    // Then rotate around X-axis (scene pitch)
    int y_scene = y_final * cos_camera_pitch - z_scene * sin_camera_pitch;
    y_scene >>= 16;
    int z_final_scene = y_final * sin_camera_pitch + z_scene * cos_camera_pitch;
    z_final_scene >>= 16;

    // Finally rotate around Z-axis (scene roll)
    int x_final_scene = x_scene * cos_camera_roll + y_scene * sin_camera_roll;
    x_final_scene >>= 16;
    int y_final_scene = y_scene * cos_camera_roll - x_scene * sin_camera_roll;
    y_final_scene >>= 16;

    projected_vertex.x = x_final_scene;
    projected_vertex.y = y_final_scene;
    projected_vertex.z = z_final_scene;

    return projected_vertex;
}

/**
 * Treats the camera as if it is at the origin (0, 0, 0)
 *
 * scene_x, scene_y, scene_z is the coordinates of the models origin relative to the camera.
 */
static inline struct ProjectedVertex
project_perspective(
    int x,
    int y,
    int z,
    int fov, // FOV in units of (2π/2048) radians
    int near_clip)
{
    struct ProjectedVertex projected_vertex = { 0 };

    // Perspective projection with FOV

    // z is the distance from the camera.
    // It is a judgement call to say when to cull the triangle, but
    // things you can consider are the average size of models.
    // It is up to the caller to cull the triangle if it is too close or behind the camera.
    // e.g. z <= 50
    // 1024 value is 2^10, so z must be > 5 bits.
    static const int z_clip_bits = 7;
    static const int clip_bits = 13;
    // clip_bits - z_clip_bits < 7; see math below
    // if( z < (1 << z_clip_bits)  || x < -(1 << clip_bits) || x > (1 << clip_bits) || y < -(1 <<
    // clip_bits) || y > (1 << clip_bits)  )
    // {
    //     projected_vertex.z = z;
    //     projected_vertex.clipped = 1;
    //     return projected_vertex;
    // }

    if( z < near_clip )
    {
        projected_vertex.z = z;
        projected_vertex.clipped = 1;
        return projected_vertex;
    }

    assert(z != 0);

    // Calculate FOV scale based on the angle using sin/cos tables
    // fov is in units of (2π/2048) radians
    // For perspective projection, we need tan(fov/2)
    // tan(x) = sin(x)/cos(x)
    int fov_half = fov >> 1; // fov/2

    // fov_scale = 1/tan(fov/2)
    // cot(x) = 1/tan(x)
    // cot(x) = tan(pi/2 - x)
    // cot(x + pi) = cot(x)
    assert(fov_half < 1536);
    // int cot_fov_half = g_tan_table[1536 - fov_half];

    // Apply FOV scaling to x and y coordinates
    // int fov_scale_ish16 = (cot_fov_half * UNIT_SCALE) / z;

    // Project to screen space with FOV
    // int screen_x = ((x * fov_scale_ish16) >> 16);
    // int screen_y = ((y * fov_scale_ish16) >> 16);

    int screen_x = SCALE_UNIT(x) / z;
    int screen_y = SCALE_UNIT(y) / z;

    // Set the projected triangle
    projected_vertex.x = screen_x;
    projected_vertex.y = screen_y;
    projected_vertex.z = z;
    projected_vertex.clipped = 0;

    return projected_vertex;
}

/**
 * Treats the camera as if it is at the origin (0, 0, 0)
 *
 * scene_x, scene_y, scene_z is the coordinates of the models origin relative to the camera.
 *
 */
static inline struct ProjectedVertex
project(
    int x,
    int y,
    int z,
    int pitch,
    int yaw,
    int roll,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov, // FOV in units of (2π/2048) radians
    int near_clip,
    int screen_width,
    int screen_height)
{
    struct ProjectedVertex projected_vertex;

    projected_vertex = project_orthographic(
        x,
        y,
        z,
        pitch,
        yaw,
        roll,
        scene_x,
        scene_y,
        scene_z,
        camera_pitch,
        camera_yaw,
        camera_roll);

    projected_vertex = project_perspective(
        projected_vertex.x, projected_vertex.y, projected_vertex.z, fov, near_clip);

    return projected_vertex;
}

static inline void
project_orthographic_fast(
    struct ProjectedVertex* projected_vertex,
    int x,
    int y,
    int z,
    int yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw)
{
    assert(camera_pitch >= 0 && camera_pitch < 2048);
    assert(camera_yaw >= 0 && camera_yaw < 2048);
    assert(yaw >= 0 && yaw < 2048);

    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int x_rotated = x;
    int z_rotated = z;
    if( yaw != 0 )
    {
        int sin_yaw = g_sin_table[yaw];
        int cos_yaw = g_cos_table[yaw];
        x_rotated = x * cos_yaw + z * sin_yaw;
        x_rotated >>= 16;
        z_rotated = z * cos_yaw - x * sin_yaw;
        z_rotated >>= 16;
    }

    // Translate points relative to camera position
    x_rotated += scene_x;
    int y_rotated = y + scene_y;
    z_rotated += scene_z;

    // Apply perspective rotation
    // First rotate around Y-axis (scene yaw)
    int x_scene = x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw;
    x_scene >>= 16;
    int z_scene = z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw;
    z_scene >>= 16;

    // Then rotate around X-axis (scene pitch)
    int y_scene = y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch;
    y_scene >>= 16;
    int z_final_scene = y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch;
    z_final_scene >>= 16;

    projected_vertex->x = x_scene;
    projected_vertex->y = y_scene;
    projected_vertex->z = z_final_scene;
}

static inline int
project_scale_unit(int p, int fov)
{
    int fov_half = fov >> 1;
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];

    static const int scale_angle = 6;
    int cot_fov_half_ish_scaled = cot_fov_half_ish16 >> scale_angle;

    // Apply FOV scaling to x and y coordinates

    // unit scale 9, angle scale 16
    // then 6 bits of valid x/z. (31 - 25 = 6), signed int.

    // if valid x is -Screen_width/2 to Screen_width/2
    // And the max resolution we allow is 1600 (either dimension)
    // then x_bits_max = 10; because 2^10 = 1024 > (1600/2)

    // If we have 6 bits of valid x then x_bits_max - z_clip_bits < 6
    // i.e. x/z < 2^6 or x/z < 64

    // Suppose we allow z > 16, so z_clip_bits = 4
    // then x_bits_max < 10, so 2^9, which is 512

    p *= cot_fov_half_ish_scaled;
    p >>= 16 - scale_angle;

    return SCALE_UNIT(p);
}

static inline int
project_divide(int p, int z, int fov)
{
    int fov_half = fov >> 1;
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];

    static const int scale_angle = 1;
    int cot_fov_half_ish_scaled = cot_fov_half_ish16 >> scale_angle;

    // Apply FOV scaling to x and y coordinates

    // unit scale 9, angle scale 16
    // then 6 bits of valid x/z. (31 - 25 = 6), signed int.

    // if valid x is -Screen_width/2 to Screen_width/2
    // And the max resolution we allow is 1600 (either dimension)
    // then x_bits_max = 10; because 2^10 = 1024 > (1600/2)

    // If we have 6 bits of valid x then x_bits_max - z_clip_bits < 6
    // i.e. x/z < 2^6 or x/z < 64

    // Suppose we allow z > 16, so z_clip_bits = 4
    // then x_bits_max < 10, so 2^9, which is 512

    p *= cot_fov_half_ish_scaled;
    p >>= 16 - scale_angle;

    return SCALE_UNIT(p) / z;
}

/**
 * Treats the camera as if it is at the origin (0, 0, 0)
 *
 * scene_x, scene_y, scene_z is the coordinates of the models origin relative to the camera.
 */
static inline void
project_perspective_fast(
    struct ProjectedVertex* projected_vertex,
    int x,
    int y,
    int z,
    int fov, // FOV in units of (2π/2048) radians
    int near_clip)
{
    // Perspective projection with FOV

    // z is the distance from the camera.
    // It is a judgement call to say when to cull the triangle, but
    // things you can consider are the average size of models.
    // It is up to the caller to cull the triangle if it is too close or behind the camera.
    // e.g. z <= 50
    // We have to check that x and y are within a reasonable range (see math below)
    // to avoid overflow.
    static const int z_clip_bits = 5;
    static const int clip_bits = 11;
    // // clip_bits - z_clip_bits < 7; see math below
    // // TODO: Frustrum culling should clip the inputs x's and y's.
    // if( z < (1 << z_clip_bits)  || x < -(1 << clip_bits) || x > (1 << clip_bits) || y < -(1 <<
    // clip_bits) || y > (1 << clip_bits)  )
    // {
    //     memset(projected_vertex, 0x00, sizeof(*projected_vertex));
    //     projected_vertex->z = z;
    //     projected_vertex->clipped = 1;
    //     return;
    // }

    if( z < near_clip )
    {
        memset(projected_vertex, 0x00, sizeof(*projected_vertex));
        projected_vertex->z = z;
        projected_vertex->clipped = 1;
        return;
    }

    assert(z != 0);

    // Calculate FOV scale based on the angle using sin/cos tables
    // fov is in units of (2π/2048) radians
    // For perspective projection, we need tan(fov/2)
    // tan(x) = sin(x)/cos(x)
    int fov_half = fov >> 1; // fov/2

    // fov_scale = 1/tan(fov/2)
    // cot(x) = 1/tan(x)
    // cot(x) = tan(pi/2 - x)
    // cot(x + pi) = cot(x)
    // assert(fov_half < 1536);
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];

    static const int scale_angle = 1;
    int cot_fov_half_ish_scaled = cot_fov_half_ish16 >> scale_angle;

    // Apply FOV scaling to x and y coordinates

    // unit scale 9, angle scale 16
    // then 6 bits of valid x/z. (31 - 25 = 6), signed int.

    // if valid x is -Screen_width/2 to Screen_width/2
    // And the max resolution we allow is 1600 (either dimension)
    // then x_bits_max = 10; because 2^10 = 1024 > (1600/2)

    // If we have 6 bits of valid x then x_bits_max - z_clip_bits < 6
    // i.e. x/z < 2^6 or x/z < 64

    // Suppose we allow z > 16, so z_clip_bits = 4
    // then x_bits_max < 10, so 2^9, which is 512

    x *= cot_fov_half_ish_scaled;
    y *= cot_fov_half_ish_scaled;
    x >>= 16 - scale_angle;
    y >>= 16 - scale_angle;

    // So we can increase x_bits_max to 11 by reducing the angle scale by 1.
    int screen_x = SCALE_UNIT(x) / z;
    int screen_y = SCALE_UNIT(y) / z;
    // screen_x *= cot_fov_half_ish_scaled;
    // screen_y *= cot_fov_half_ish_scaled;
    // screen_x >>= 16 - scale_angle;
    // screen_y >>= 16 - scale_angle;

    // Set the projected triangle
    projected_vertex->x = screen_x;
    projected_vertex->y = screen_y;
    projected_vertex->z = z;
    projected_vertex->clipped = 0;
}

static inline void
project_fast(
    struct ProjectedVertex* projected_vertex,
    int x,
    int y,
    int z,
    int yaw_r2pi2048,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch_r2pi2048,
    int camera_yaw_r2pi2048,
    int fov_r2pi2048,
    int near_clip,
    int screen_width,
    int screen_height)
{
    project_orthographic_fast(
        projected_vertex,
        x,
        y,
        z,
        yaw_r2pi2048,
        scene_x,
        scene_y,
        scene_z,
        camera_pitch_r2pi2048,
        camera_yaw_r2pi2048);

    project_perspective_fast(
        projected_vertex,
        projected_vertex->x,
        projected_vertex->y,
        projected_vertex->z,
        fov_r2pi2048,
        near_clip);
}

#endif