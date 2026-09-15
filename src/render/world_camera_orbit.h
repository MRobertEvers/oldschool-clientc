#ifndef SRC_RENDER_WORLD_CAMERA_ORBIT_H
#define SRC_RENDER_WORLD_CAMERA_ORBIT_H

/*
 * The follow camera's own arithmetic: where it is pointed, how far back it
 * sits, and how it gets there.
 *
 * Every one of these is a rule a still frame cannot check. A camera at the
 * wrong distance looks like a camera at a different zoom; one whose pitch
 * clamp eases the wrong way looks like terrain popping; one whose key
 * velocities decay wrongly feels like a heavy mouse. None of them produces
 * anything a screenshot or a log will argue with.
 *
 * Nothing here reads a world or an entity. The caller finds the player, samples
 * the terrain and applies the eye; this decides what to apply.
 */

#include <stdbool.h>

/**
 * The 24.8 fixed point the terrain pitch clamp eases in.
 *
 * It is stored at 256x so a clamp can move by fractions of a pitch unit per
 * frame -- at whole units the ease below would round to zero and the clamp
 * would never move at all.
 */
#define WORLD_CAMERA_PITCH_CLAMP_SCALE 256

/**
 * What the revision says its camera may do. Every one of these is stated by
 * the profile rather than chosen here, because two lanes of this client
 * disagree about all of them and a constant would make one of them wrong.
 */
struct WorldCameraLimits
{
    /** The pitch band, in pitch units. Flattest is the smaller number. */
    int pitch_flattest;
    int pitch_steepest;
    /** Distance gained per unit of pitch -- the reference's `pitch * 3`. */
    int pitch_distance;
    /** The additive term, `[camera] rest=` as the wheel has left it. */
    int rest_zoom;
    /** The viewport-height zoom in 256ths, or 0 for a revision whose camera
     *  has none. A 2004 client has none, and applying one halves its scale. */
    int viewport_zoom_256;
    /** The device's own dolly, as a percentage. 100 is no dolly. */
    int distance_scale_percent;
    /** Nothing may come closer than this: past the near plane is not a closer
     *  view, it is a dropped one. */
    int near_plane_z;
};

/** Which of the four arrow keys are held. */
struct WorldCameraKeys
{
    bool left;
    bool right;
    bool up;
    bool down;
};

struct WorldCameraOrbit
{
    /** The eased anchor the eye is built around, in scene fine units. Float,
     *  and that matters -- see WorldCameraOrbit_StepAngles. */
    float anchor_x;
    float anchor_z;
    int yaw;
    int pitch;
    int yaw_velocity;
    int pitch_velocity;
    /** The terrain clamp, at WORLD_CAMERA_PITCH_CLAMP_SCALE. */
    int pitch_clamp;
};

void
WorldCameraOrbit_Reset(struct WorldCameraOrbit* orbit);

/** Hold a pitch inside the revision's band. */
int
WorldCameraLimits_ClampPitch(
    struct WorldCameraLimits const* limits,
    int pitch);

/**
 * Advance yaw and pitch for one client cycle from the keys that are held.
 *
 * An impulse-and-decay model, not a rate: a key adds toward its impulse by
 * half the difference each cycle and a released key halves what is left, so a
 * tap turns a little and a hold accelerates into a steady spin that coasts to
 * a stop. Replacing it with a fixed rate is a camera that starts and stops
 * dead, which reads as a dropped frame every time it is touched.
 *
 * The velocities are applied at HALF, which is where the actual turn rate
 * comes from; the impulses are the reference's own 24 and 12.
 */
void
WorldCameraOrbit_StepAngles(
    struct WorldCameraOrbit* orbit,
    struct WorldCameraKeys const* keys,
    struct WorldCameraLimits const* limits);

/**
 * Ease the terrain pitch clamp toward `wanted`, itself in pitch units.
 *
 * Asymmetric on purpose, and this is the rule worth the test: a RISING clamp
 * moves at a twenty-fourth and a FALLING one at an eightieth. The camera has
 * to clear a hill before the player reaches it, and has to come back down
 * slowly enough that cresting a ridge does not drop the view into the ground.
 * Made symmetric, one of those two is wrong whichever rate is chosen.
 *
 * `wanted` is clamped into the revision's own band first, so a profile that
 * states a flat camera cannot be tipped by a mountain.
 */
void
WorldCameraOrbit_EaseTerrainClamp(
    struct WorldCameraOrbit* orbit,
    struct WorldCameraLimits const* limits,
    int wanted);

/** The pitch the eye is actually built at: the orbit's, raised by the terrain
 *  clamp when the ground demands it. */
int
WorldCameraOrbit_EffectivePitch(
    struct WorldCameraOrbit const* orbit);

/**
 * How far back the eye sits for a given pitch.
 *
 * `pitch * pitch_distance + rest` is the reference's `pitch * 3 + 600`, both
 * terms stated by the profile. Then the viewport zoom, then the device's own
 * dolly over the WHOLE distance -- pitch term included, which is the point of
 * it, because the band under `rest` moves the additive term only and buys less
 * and less as the camera tips over.
 *
 * Floored at the near plane last, so no combination of the three can put the
 * eye inside it.
 */
int
WorldCameraOrbit_Distance(
    struct WorldCameraLimits const* limits,
    int pitch);

/*
 * The camera held across an offline world reload.
 *
 * Captured in ABSOLUTE fine coordinates, because the scene window can move: a
 * rebuild of the same region has the same base tile and the camera lands
 * exactly where it was, while opening a distant square shifts the base until
 * the held point falls outside the new scene entirely -- and then recentring
 * is the right answer. Which is why the restore is a CONTAINMENT TEST and not
 * a flag: "is the place I was looking at still in front of me".
 */
struct WorldCameraHold
{
    bool valid;
    int absolute_x;
    int absolute_z;
    int y;
    int pitch;
    int yaw;
};

/** Remember where the camera is, from a scene-local eye and its base tile. */
void
WorldCameraHold_Capture(
    struct WorldCameraHold* hold,
    int base_tile_x,
    int base_tile_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int pitch,
    int yaw);

/**
 * Put the camera back, if the new scene contains where it was.
 *
 * Returns false when there is nothing held or the held point is outside, and
 * then the caller centres instead. Spends the hold either way: a hold that
 * survived a failed restore would be applied to some later, unrelated load.
 */
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
    int* out_yaw);

#endif /* SRC_RENDER_WORLD_CAMERA_ORBIT_H */
