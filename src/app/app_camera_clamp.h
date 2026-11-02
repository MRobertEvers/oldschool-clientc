#ifndef SRC_APP_APP_CAMERA_CLAMP_H
#define SRC_APP_APP_CAMERA_CLAMP_H

/*
 * Keep the editor's free camera over the scene it can see.
 *
 * The free camera flies in scene fine units (128 per tile, the scene's own
 * origin at 0,0; y grows DOWNWARD, so a higher camera is a more negative y).
 * Nothing stopped it leaving the loaded squares, and a few seconds of W put
 * it over the void with nothing to draw and no landmark to fly back by
 * (owner, 2026-11-02: "prevent the camera from flying off the visible map").
 *
 * Pure, so the rule is testable without an App: x and z stay within the scene
 * extent plus a margin on each side; y stays between a ceiling (the highest
 * the camera may go) and a floor just above the ground under it.
 */
static inline int
app_camera_clamp_axis(int value, int lo, int hi)
{
    if( value < lo )
        return lo;
    if( value > hi )
        return hi;
    return value;
}

/**
 * Clamp a camera position in place. `extent` is the scene's size in fine
 * units, `margin` how far past its edge the camera may go, `ground_y` the
 * ground height under the (clamped) camera in the same y units, `above_min`
 * how far above that ground the camera must stay, and `ceiling_y` the most
 * negative (highest) y allowed. Returns 1 when something was clamped.
 */
static inline int
app_camera_clamp_scene(
    int* x,
    int* y,
    int* z,
    int extent,
    int margin,
    int ground_y,
    int above_min,
    int ceiling_y)
{
    int const nx = app_camera_clamp_axis(*x, -margin, extent + margin);
    int const nz = app_camera_clamp_axis(*z, -margin, extent + margin);
    int const ny = app_camera_clamp_axis(*y, ceiling_y, ground_y - above_min);
    int const changed = nx != *x || ny != *y || nz != *z;

    *x = nx;
    *y = ny;
    *z = nz;
    return changed;
}

#endif
