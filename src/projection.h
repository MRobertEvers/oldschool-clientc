#ifndef PROJECTION_H
#define PROJECTION_H

#define UNIT_SCALE (512)

struct ProjectedTriangle
{
    int x;
    int y;

    // z is the distance from the camera to the point
    int z;

    // If the projection is too close or behind the screen, clipped is nonzero.
    int clipped;
};

struct ProjectedTriangle project_orthographic(
    int x,
    int y,
    int z,
    int pitch_r2pi2048,
    int yaw_r2pi2048,
    int roll_r2pi2048,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch_r2pi2048,
    int camera_yaw_r2pi2048,
    int camera_roll_r2pi2048);

struct ProjectedTriangle project_perspective(
    int x,
    int y,
    int z,
    // FOV in units of (2π/2048) radians
    int fov_r2pi2048,
    int near_clip);

/**
 * Treats the camera as if it is at the origin (0, 0, 0)
 *
 * scene_x, scene_y, scene_z is the coordinates of the models origin relative to the camera.
 */
struct ProjectedTriangle project(
    int x1,
    int y1,
    int z1,
    int pitch_r2pi2048,
    int yaw_r2pi2048,
    int roll_r2pi2048,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch_r2pi2048,
    int camera_yaw_r2pi2048,
    int camera_roll_r2pi2048,
    // FOV in units of (2π/2048) radians
    int fov_r2pi2048,
    int near_clip,
    int screen_width,
    int screen_height);

void project_orthographic_fast(
    struct ProjectedTriangle* projected_triangle,
    int x,
    int y,
    int z,
    int yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw);

void project_perspective_fast(
    struct ProjectedTriangle* projected_triangle,
    int x,
    int y,
    int z,
    int fov, // FOV in units of (2π/2048) radians
    int near_clip);

/**
 * Omits model roll and pitch, and camera roll.
 */
void project_fast(
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
    int screen_height);

#endif
