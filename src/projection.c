#include "projection.h"

extern int g_cos_table[2048];
extern int g_sin_table[2048];

// Field of view parameters
const int Z_NEAR = 1 << 9;
const int Z_FAR = 1500 << 16;
const int ASPECT_RATIO = 1; // Aspect ratio shifted up by 16

/**
 * Treats the camera as if it is at the origin (0, 0, 0)
 *
 *
 */
struct ProjectedTriangle
project(
    int x1,
    int y1,
    int z1,
    int yaw,
    int pitch,
    int roll,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_yaw,
    int camera_pitch,
    int camera_roll,
    int fov, // FOV in units of (2π/2048) radians
    int screen_width,
    int screen_height)
{
    struct ProjectedTriangle projected_triangle;
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];
    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_roll = g_cos_table[camera_roll];
    int sin_camera_roll = g_sin_table[camera_roll];

    // Field of view parameters
    const int Z_NEAR = 1 << 9;
    const int Z_FAR = 1500 << 16;
    const int ASPECT_RATIO = 1; // Aspect ratio shifted up by 16

    // Apply model rotation
    int sin_yaw = g_sin_table[yaw];
    int cos_yaw = g_cos_table[yaw];
    int sin_pitch = g_sin_table[pitch];
    int cos_pitch = g_cos_table[pitch];
    int sin_roll = g_sin_table[roll];
    int cos_roll = g_cos_table[roll];

    // Rotate around Y-axis (yaw)
    int x1_rotated = x1 * cos_yaw - z1 * sin_yaw;
    x1_rotated >>= 16;
    int z1_rotated = x1 * sin_yaw + z1 * cos_yaw;
    z1_rotated >>= 16;

    // Rotate around X-axis (pitch)
    int y1_rotated = y1 * cos_pitch - z1_rotated * sin_pitch;
    y1_rotated >>= 16;
    int z1_rotated2 = y1 * sin_pitch + z1_rotated * cos_pitch;
    z1_rotated2 >>= 16;

    // Rotate around Z-axis (roll)
    int x1_final = x1_rotated * cos_roll - y1_rotated * sin_roll;
    x1_final >>= 16;
    int y1_final = x1_rotated * sin_roll + y1_rotated * cos_roll;
    y1_final >>= 16;

    // Translate points relative to camera position
    x1_final += scene_x;
    y1_final += scene_y;
    z1_rotated2 += scene_z;

    // Apply scene rotation
    // First rotate around Y-axis (scene yaw)
    int x1_scene = x1_final * cos_camera_yaw - z1_rotated2 * sin_camera_yaw;
    x1_scene >>= 16;
    int z1_scene = x1_final * sin_camera_yaw + z1_rotated2 * cos_camera_yaw;
    z1_scene >>= 16;

    // Then rotate around X-axis (scene pitch)
    int y1_scene = y1_final * cos_camera_pitch - z1_scene * sin_camera_pitch;
    y1_scene >>= 16;
    int z1_scene2 = y1_final * sin_camera_pitch + z1_scene * cos_camera_pitch;
    z1_scene2 >>= 16;

    // Finally rotate around Z-axis (scene roll)
    int x1_final_scene = x1_scene * cos_camera_roll - y1_scene * sin_camera_roll;
    x1_final_scene >>= 16;
    int y1_final_scene = x1_scene * sin_camera_roll + y1_scene * cos_camera_roll;
    y1_final_scene >>= 16;

    // Perspective projection with FOV
    int z1_final = z1_scene2;

    // Calculate FOV scale based on the angle using sin/cos tables
    // fov is in units of (2π/2048) radians
    // For perspective projection, we need tan(fov/2)
    // tan(x) = sin(x)/cos(x)
    int fov_half = fov >> 1; // fov/2
    int sin_fov_half = g_sin_table[fov_half];
    int cos_fov_half = g_cos_table[fov_half];
    // fov_scale = 1/tan(fov/2) = cos(fov/2)/sin(fov/2)
    int fov_scale = (cos_fov_half << 8) / sin_fov_half;

    // Apply FOV scaling to x and y coordinates
    int scale1 = (fov_scale * Z_NEAR) / z1_final;

    // Project to screen space with FOV
    // Shift down by 8 to account for the scale factor
    int screen_x1 = ((x1_final_scene * scale1 * ASPECT_RATIO) >> 8) + screen_width / 2;
    int screen_y1 = ((y1_final_scene * scale1) >> 8) + screen_height / 2;

    int a = (scene_z * cos_camera_yaw - scene_x * sin_camera_yaw) >> 16;
    int b = (scene_y * sin_camera_pitch + a * cos_camera_pitch) >> 16;

    // Set the projected triangle
    projected_triangle.x1 = screen_x1;
    projected_triangle.y1 = screen_y1;

    projected_triangle.depth1 = z1_final - b;

    return projected_triangle;
}
