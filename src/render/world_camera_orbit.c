#include "render/world_camera_orbit.h"

#include <assert.h>
#include <string.h>

/* The reference's own impulses (Client-TS camFollow). Yaw turns twice as fast
 * as pitch tips, which is what makes looking around feel like turning and
 * looking up feel like leaning. */
enum
{
    WORLD_CAMERA_YAW_IMPULSE = 24,
    WORLD_CAMERA_PITCH_IMPULSE = 12,
    /* A clamp that has to rise clears a hill; one that falls is only tidying
     * up after one. The first cannot be late and the second must not be
     * sudden, so they are not the same number. */
    WORLD_CAMERA_CLAMP_RISE = 24,
    WORLD_CAMERA_CLAMP_FALL = 80,
};

void
WorldCameraOrbit_Reset(struct WorldCameraOrbit* orbit)
{
    assert(orbit);
    memset(orbit, 0, sizeof(*orbit));
}

int
WorldCameraLimits_ClampPitch(
    struct WorldCameraLimits const* limits,
    int pitch)
{
    assert(limits);
    if( pitch < limits->pitch_flattest )
        return limits->pitch_flattest;
    if( pitch > limits->pitch_steepest )
        return limits->pitch_steepest;
    return pitch;
}

void
WorldCameraOrbit_StepAngles(
    struct WorldCameraOrbit* orbit,
    struct WorldCameraKeys const* keys,
    struct WorldCameraLimits const* limits)
{
    assert(orbit);
    assert(keys);
    assert(limits);

    /* Toward the impulse by half the gap while held, halving what is left when
     * released. Both directions held is the same as neither: left is tested
     * first and right's branch is an else, so the camera does not fight
     * itself. */
    if( keys->left )
        orbit->yaw_velocity += (-orbit->yaw_velocity - WORLD_CAMERA_YAW_IMPULSE) / 2;
    else if( keys->right )
        orbit->yaw_velocity += (WORLD_CAMERA_YAW_IMPULSE - orbit->yaw_velocity) / 2;
    else
        orbit->yaw_velocity = orbit->yaw_velocity / 2;

    if( keys->up )
        orbit->pitch_velocity += (WORLD_CAMERA_PITCH_IMPULSE - orbit->pitch_velocity) / 2;
    else if( keys->down )
        orbit->pitch_velocity += (-orbit->pitch_velocity - WORLD_CAMERA_PITCH_IMPULSE) / 2;
    else
        orbit->pitch_velocity = orbit->pitch_velocity / 2;

    /* Yaw wraps -- there is no far side of a turn -- while pitch is held in
     * the revision's band, because there IS a far side of looking up. */
    orbit->yaw = (orbit->yaw + orbit->yaw_velocity / 2) & 0x7ff;
    orbit->pitch = WorldCameraLimits_ClampPitch(limits, orbit->pitch + orbit->pitch_velocity / 2);
}

void
WorldCameraOrbit_EaseTerrainClamp(
    struct WorldCameraOrbit* orbit,
    struct WorldCameraLimits const* limits,
    int wanted)
{
    int target;

    assert(orbit);
    assert(limits);

    target = WorldCameraLimits_ClampPitch(limits, wanted) * WORLD_CAMERA_PITCH_CLAMP_SCALE;
    if( target > orbit->pitch_clamp )
        orbit->pitch_clamp += (target - orbit->pitch_clamp) / WORLD_CAMERA_CLAMP_RISE;
    else if( target < orbit->pitch_clamp )
        orbit->pitch_clamp += (target - orbit->pitch_clamp) / WORLD_CAMERA_CLAMP_FALL;
}

int
WorldCameraOrbit_EffectivePitch(struct WorldCameraOrbit const* orbit)
{
    int const floor_pitch = orbit->pitch_clamp / WORLD_CAMERA_PITCH_CLAMP_SCALE;

    assert(orbit);
    return floor_pitch > orbit->pitch ? floor_pitch : orbit->pitch;
}

int
WorldCameraOrbit_Distance(
    struct WorldCameraLimits const* limits,
    int pitch)
{
    int distance;

    assert(limits);

    distance = pitch * limits->pitch_distance + limits->rest_zoom;
    if( limits->viewport_zoom_256 > 0 )
        distance = distance * limits->viewport_zoom_256 / 256;
    if( limits->distance_scale_percent != 100 )
        distance = distance * limits->distance_scale_percent / 100;
    /* Last, so nothing above it can put the eye inside the near plane. */
    if( distance < limits->near_plane_z )
        distance = limits->near_plane_z;
    return distance;
}

void
WorldCameraHold_Capture(
    struct WorldCameraHold* hold,
    int base_tile_x,
    int base_tile_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int pitch,
    int yaw)
{
    assert(hold);

    hold->valid = true;
    hold->absolute_x = base_tile_x * 128 + scene_x;
    hold->absolute_z = base_tile_z * 128 + scene_z;
    hold->y = scene_y;
    hold->pitch = pitch;
    hold->yaw = yaw;
}

bool
WorldCameraHold_Restore(
    struct WorldCameraHold* hold,
    int base_tile_x,
    int base_tile_z,
    int scene_size_tiles,
    int* out_scene_x,
    int* out_scene_y,
    int* out_scene_z,
    int* out_pitch,
    int* out_yaw)
{
    int scene_x;
    int scene_z;
    int extent;

    assert(hold);
    assert(out_scene_x);
    assert(out_scene_y);
    assert(out_scene_z);
    assert(out_pitch);
    assert(out_yaw);

    if( !hold->valid )
        return false;
    /* Spent whether or not it fits. A hold that survived a failed restore
     * would be applied to some later, unrelated load -- the camera jumping to
     * where it was two worlds ago. */
    hold->valid = false;

    scene_x = hold->absolute_x - base_tile_x * 128;
    scene_z = hold->absolute_z - base_tile_z * 128;
    extent = scene_size_tiles * 128;
    if( scene_x < 0 || scene_x >= extent || scene_z < 0 || scene_z >= extent )
        return false;

    *out_scene_x = scene_x;
    *out_scene_z = scene_z;
    *out_scene_y = hold->y;
    *out_pitch = hold->pitch;
    *out_yaw = hold->yaw;
    return true;
}
