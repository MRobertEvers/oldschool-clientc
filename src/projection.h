#ifndef PROJECTION_H
#define PROJECTION_H

struct ProjectedTriangle
{
    int x1;
    int y1;

    // z1 is the distance from the camera to the point
    int z1;
};

/**
 * Treats the camera as if it is at the origin (0, 0, 0)
 *
 * scene_x, scene_y, scene_z is the coordinates of the models origin relative to the camera.
 */
struct ProjectedTriangle project(
    int x1,
    int y1,
    int z1,
    int yaw_r2pi2048,
    int pitch_r2pi2048,
    int roll_r2pi2048,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_yaw_r2pi2048,
    int camera_pitch_r2pi2048,
    int camera_roll_r2pi2048,
    // FOV in units of (2π/2048) radians
    int fov_r2pi2048,
    int screen_width,
    int screen_height);

#endif
