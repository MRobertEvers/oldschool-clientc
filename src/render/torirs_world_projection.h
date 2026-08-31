#ifndef TORIRS_WORLD_PROJECTION_H
#define TORIRS_WORLD_PROJECTION_H

#include "impl/projection/projection.scalar_reference.h"
#include "toridraw_math.h"
#include "toridraw_types.h"

/**
 * Pure camera-relative world-point projection shared by App overlays and the
 * deterministic UITree redraw harness.
 *
 * Coordinates use the client's world convention (up is negative Y). The
 * caller owns world/terrain bounds checks and supplies the absolute point and
 * viewport. `near_plane_z` is explicit so this helper cannot silently disagree
 * with the renderer configuration.
 */
static inline int
ToriRS_WorldProjectPoint(
    struct ToriDraw_Camera const* camera,
    struct ToriDraw_Position const* eye,
    int viewport_x,
    int viewport_y,
    int viewport_w,
    int viewport_h,
    int near_plane_z,
    int world_x,
    int world_y,
    int world_z,
    int* out_x,
    int* out_y)
{
    int dx, dy, dz, tmp;
    int sin_pitch, cos_pitch, sin_yaw, cos_yaw;
    int scale;

    if( !camera || !eye || !out_x || !out_y || viewport_w <= 0 || viewport_h <= 0 )
        return 0;

    dx = world_x - eye->x;
    dy = world_y - eye->y;
    dz = world_z - eye->z;

    sin_pitch = ToriDraw_Sin(camera->pitch);
    cos_pitch = ToriDraw_Cos(camera->pitch);
    sin_yaw = ToriDraw_Sin(camera->yaw);
    cos_yaw = ToriDraw_Cos(camera->yaw);

    tmp = (dz * sin_yaw + dx * cos_yaw) >> 16;
    dz = (dz * cos_yaw - dx * sin_yaw) >> 16;
    dx = tmp;

    tmp = (dy * cos_pitch - dz * sin_pitch) >> 16;
    dz = (dy * sin_pitch + dz * cos_pitch) >> 16;
    dy = tmp;

    if( near_plane_z <= 0 )
        near_plane_z = 50;
    if( dz < near_plane_z )
        return 0;

    if( camera->projection_mode == TORIDRAW_PROJECTION_MODE_FOV )
        scale = toridraw_projection_scale_from_fov(camera->fov_rpi2048);
    else
        scale = camera->projection_scale;
    if( scale <= 0 )
        scale = TORIDRAW_PROJECTION_SCALE_DEFAULT;

    *out_x = viewport_x + viewport_w / 2 + dx * scale / dz;
    *out_y = viewport_y + viewport_h / 2 + dy * scale / dz;
    return 1;
}

/** Construct the reference follow-camera eye around an already-settled orbit
 * anchor. `pitch`, `yaw`, and `distance` use the same integer units as App. */
static inline void
ToriRS_OrbitCameraEye(
    int target_x,
    int target_y,
    int target_z,
    int pitch,
    int yaw,
    int distance,
    struct ToriDraw_Position* out_eye)
{
    int off_x = 0;
    int off_y = 0;
    int off_z = distance;
    int inv_pitch = (2048 - pitch) & 0x7ff;
    int inv_yaw = (2048 - yaw) & 0x7ff;

    if( !out_eye )
        return;
    if( inv_pitch != 0 )
    {
        int sin = ToriDraw_Sin(inv_pitch);
        int cos = ToriDraw_Cos(inv_pitch);
        int tmp = (off_y * cos - distance * sin) >> 16;
        off_z = (off_y * sin + distance * cos) >> 16;
        off_y = tmp;
    }
    if( inv_yaw != 0 )
    {
        int sin = ToriDraw_Sin(inv_yaw);
        int cos = ToriDraw_Cos(inv_yaw);
        int tmp = (off_z * sin + off_x * cos) >> 16;
        off_z = (off_z * cos - off_x * sin) >> 16;
        off_x = tmp;
    }

    out_eye->x = target_x - off_x;
    out_eye->y = target_y - off_y;
    out_eye->z = target_z - off_z;
}

#endif
