#ifndef SRC_EDITOR_EDITOR_WORLD_GESTURE_H
#define SRC_EDITOR_EDITOR_WORLD_GESTURE_H

/*
 * The World panel's camera under a gesture frame (workbench design section
 * 14, the World column): the pure half, a camera pose and a frame in, the
 * new pose out. No App, no renderer, no world: the caller says where the
 * ground is under the gesture (EditorWorldGestureView::ground_y) and holds
 * the result over the scene as it holds every other move
 * (app_camera_keep_over_scene).
 *
 *   PAN (two fingers)  the look-at point moves along the ground plane: the
 *                      ground point under the fingers stays under them, the
 *                      eye translates with it, yaw and pitch are kept.
 *   PINCH              the eye distance changes about the ground point under
 *                      the gesture, which stays under it: the eye moves along
 *                      that point's own ray, by the ratio (x2 apart is half
 *                      the distance).
 *   TWIST              yaw turns about the look-at point (the ground under
 *                      the viewport's centre), which stays centred; the scene
 *                      turns with the fingers.
 *
 * A one-finger PAN is never the camera's: with the editor open it is the
 * armed tool's drag (the marquee, a brush stroke), so this ignores it.
 *
 * The free camera only, under a perspective projection (SCALE or FOV): the
 * orbit follow camera's anchor is the player's, and a parallel projection
 * has no eye distance to change.
 */

#include "input/torirs_gesture.h"
#include "toridraw_types.h"

/** Nearest and farthest a pinch takes the eye from the ground point, fine
 *  units (a tile is 128): half a tile, and a hundred tiles. */
#define EDITOR_WORLD_GESTURE_DISTANCE_MIN 64
#define EDITOR_WORLD_GESTURE_DISTANCE_MAX 12800

struct EditorWorldGestureView
{
    /** The world viewport on the canvas, the projection's centre box. */
    int viewport_x;
    int viewport_y;
    int viewport_w;
    int viewport_h;
    /** The ground's height (world y, up is negative) under the gesture: the
     *  plane a pan slides along and a pinch closes on. */
    int ground_y;
};

/**
 * Apply `frame` to the pose (`camera`'s yaw and pitch, `eye`). Returns 1 when
 * the pose moved; 0 for a frame the World does not take (a one-finger pan,
 * a ray that misses the ground plane, a parallel camera) or one too small to
 * move it, and then the pose is untouched.
 */
int
EditorWorldGesture_Apply(
    struct ToriDraw_Camera* camera,
    struct ToriDraw_Position* eye,
    const struct EditorWorldGestureView* view,
    const struct ToriRS_GestureFrame* frame);

/** A TWIST's radians (q16) as the yaw step it turns, 2048 a turn, rounded;
 *  positive (clockwise on the canvas) turns the scene clockwise. */
int
EditorWorldGesture_TwistYaw(int radians_q16);

#endif /* SRC_EDITOR_EDITOR_WORLD_GESTURE_H */
