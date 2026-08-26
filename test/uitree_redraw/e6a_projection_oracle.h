#ifndef UITREE_REDRAW_E6A_PROJECTION_ORACLE_H
#define UITREE_REDRAW_E6A_PROJECTION_ORACLE_H

#include "graphics/projection.h"
#include "toridraw_math.h"
#include "toridraw_types.h"

/*
 * Frozen test oracle copied from the integer camera/orbit math in app.c at
 * e6a4364. Keep this implementation independent from
 * src/render/torirs_world_projection.h: the baseline harness deliberately
 * compiles this copy while the candidate harness compiles the production
 * helper. Editing both implementations together would defeat the A/B oracle.
 */
static inline int
UITreeRedraw_E6AWorldProjectPoint(
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

    if( camera->proj_mode == TORIDRAW_PROJ_MODE_FOV )
        scale = toridraw_proj_scale_from_fov(camera->fov_rpi2048);
    else
        scale = camera->proj_scale;
    if( scale <= 0 )
        scale = TORIDRAW_PROJ_SCALE_DEFAULT;

    *out_x = viewport_x + viewport_w / 2 + dx * scale / dz;
    *out_y = viewport_y + viewport_h / 2 + dy * scale / dz;
    return 1;
}

static inline void
UITreeRedraw_E6AOrbitCameraEye(
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
