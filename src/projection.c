#include "projection.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

extern int g_cos_table[2048];
extern int g_sin_table[2048];
extern int g_tan_table[2048];

/**
 * Treats the camera as if it is at the origin (0, 0, 0)
 *
 * scene_x, scene_y, scene_z is the coordinates of the models origin relative to the camera.
 *
 * z points away from the screen.
 * x points to the right.
 * y points down
 *
 *
 *
 */
struct ProjectedTriangle
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
    struct ProjectedTriangle projected_triangle = { 0 };

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

    projected_triangle.x = x_final_scene;
    projected_triangle.y = y_final_scene;
    projected_triangle.z = z_final_scene;

    return projected_triangle;
}

/**
 * Treats the camera as if it is at the origin (0, 0, 0)
 *
 * scene_x, scene_y, scene_z is the coordinates of the models origin relative to the camera.
 */
struct ProjectedTriangle
project_perspective(
    int x,
    int y,
    int z,
    int fov, // FOV in units of (2π/2048) radians
    int near_clip)
{
    struct ProjectedTriangle projected_triangle = { 0 };

    // Perspective projection with FOV

    // z is the distance from the camera.
    // It is a judgement call to say when to cull the triangle, but
    // things you can consider are the average size of models.
    // It is up to the caller to cull the triangle if it is too close or behind the camera.
    // e.g. z <= 50
    if( z < near_clip )
    {
        memset(&projected_triangle, 0x00, sizeof(projected_triangle));
        projected_triangle.z = z;
        projected_triangle.clipped = 1;
        return projected_triangle;
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
    int cot_fov_half = g_tan_table[1536 - fov_half];

    // Apply FOV scaling to x and y coordinates
    long long fov_scale_ish16 = (cot_fov_half * UNIT_SCALE) / z;

    // Project to screen space with FOV
    long long screen_x = ((x * fov_scale_ish16) >> 16);
    long long screen_y = ((y * fov_scale_ish16) >> 16);

    // Set the projected triangle
    projected_triangle.x = screen_x;
    projected_triangle.y = screen_y;
    projected_triangle.z = z;
    projected_triangle.clipped = 0;

    return projected_triangle;
}

/**
 * Treats the camera as if it is at the origin (0, 0, 0)
 *
 * scene_x, scene_y, scene_z is the coordinates of the models origin relative to the camera.
 *
 */
struct ProjectedTriangle
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
    struct ProjectedTriangle projected_triangle;

    projected_triangle = project_orthographic(
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

    projected_triangle = project_perspective(
        projected_triangle.x, projected_triangle.y, projected_triangle.z, fov, near_clip);

    return projected_triangle;
}

void
project_orthographic_fast(
    struct ProjectedTriangle* projected_triangle,
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

    projected_triangle->x = x_scene;
    projected_triangle->y = y_scene;
    projected_triangle->z = z_final_scene;
}

/**
 * Treats the camera as if it is at the origin (0, 0, 0)
 *
 * scene_x, scene_y, scene_z is the coordinates of the models origin relative to the camera.
 */
void
project_perspective_fast(
    struct ProjectedTriangle* projected_triangle,
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
    if( z < near_clip )
    {
        memset(projected_triangle, 0x00, sizeof(*projected_triangle));
        projected_triangle->z = z;
        projected_triangle->clipped = 1;
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
    assert(fov_half < 1536);
    int cot_fov_half = g_tan_table[1536 - fov_half];

    // Apply FOV scaling to x and y coordinates
    int fov_scale_ish16 = (cot_fov_half * UNIT_SCALE) / z;

    // Project to screen space with FOV
    int screen_x = ((x * fov_scale_ish16) >> 16);
    int screen_y = ((y * fov_scale_ish16) >> 16);

    // Set the projected triangle
    projected_triangle->x = screen_x;
    projected_triangle->y = screen_y;
    projected_triangle->z = z;
    projected_triangle->clipped = 0;
}

void
project_fast(
    struct ProjectedTriangle* projected_triangle,
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
        projected_triangle,
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
        projected_triangle,
        projected_triangle->x,
        projected_triangle->y,
        projected_triangle->z,
        fov_r2pi2048,
        near_clip);
}