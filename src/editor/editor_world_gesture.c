/*
 * The World panel's camera under a gesture frame. @see editor_world_gesture.h.
 *
 * Every move is stated against the ground plane the caller names, through
 * the client's own projection (ToriRS_WorldUnprojectPlane, the inverse of
 * ToriRS_WorldProjectPoint), so "the point stays under the fingers" holds in
 * the projection the World is drawn with rather than in a second idea of it.
 */
#include "editor/editor_world_gesture.h"

#include "render/torirs_world_projection.h"

#include <assert.h>
#include <math.h>

/** The ground point under canvas (x, y) for the pose. */
static int
world_gesture_ground(
    const struct ToriDraw_Camera* camera,
    const struct ToriDraw_Position* eye,
    const struct EditorWorldGestureView* view,
    double x,
    double y,
    double* out_x,
    double* out_z)
{
    /* The unproject takes integer pixels; the half-pixel the centre of a
     * pair can sit on is below anything a finger places. */
    return ToriRS_WorldUnprojectPlane(camera, eye, view->viewport_x, view->viewport_y, view->viewport_w,
        view->viewport_h, (int)lround(x), (int)lround(y), view->ground_y, out_x, out_z);
}

static int
world_gesture_pan(
    const struct ToriDraw_Camera* camera,
    struct ToriDraw_Position* eye,
    const struct EditorWorldGestureView* view,
    const struct ToriRS_GestureFrame* frame)
{
    double was_x;
    double was_z;
    double now_x;
    double now_z;
    int move_x;
    int move_z;

    /* The ground point that was under the fingers, and the one under them
     * now: the eye moves by their difference, so the first comes to where
     * the second is on the canvas. */
    if( !world_gesture_ground(camera, eye, view, frame->x - frame->dx, frame->y - frame->dy, &was_x, &was_z) )
        return 0;
    if( !world_gesture_ground(camera, eye, view, frame->x, frame->y, &now_x, &now_z) )
        return 0;
    move_x = (int)lround(was_x - now_x);
    move_z = (int)lround(was_z - now_z);
    if( move_x == 0 && move_z == 0 )
        return 0;
    eye->x += move_x;
    eye->z += move_z;
    return 1;
}

static int
world_gesture_pinch(
    const struct ToriDraw_Camera* camera,
    struct ToriDraw_Position* eye,
    const struct EditorWorldGestureView* view,
    const struct ToriRS_GestureFrame* frame)
{
    double ground_x;
    double ground_z;
    double off_x;
    double off_y;
    double off_z;
    double distance;
    double wanted;
    double ratio;
    struct ToriDraw_Position next = *eye;

    assert(frame->scale_q16 > 0);
    if( !world_gesture_ground(camera, eye, view, frame->x, frame->y, &ground_x, &ground_z) )
        return 0;
    off_x = (double)eye->x - ground_x;
    off_y = (double)eye->y - (double)view->ground_y;
    off_z = (double)eye->z - ground_z;
    distance = sqrt(off_x * off_x + off_y * off_y + off_z * off_z);
    if( distance <= 0.0 )
        return 0;
    /* Apart (> 1) is closer, by exactly the ratio. */
    wanted = distance * 65536.0 / (double)frame->scale_q16;
    if( wanted < EDITOR_WORLD_GESTURE_DISTANCE_MIN )
        wanted = EDITOR_WORLD_GESTURE_DISTANCE_MIN;
    if( wanted > EDITOR_WORLD_GESTURE_DISTANCE_MAX )
        wanted = EDITOR_WORLD_GESTURE_DISTANCE_MAX;
    ratio = wanted / distance;
    /* Along the point's own ray: the eye, the point and the pixel stay on one
     * line, so the point stays under the pinch whatever the pitch. */
    next.x = (int)lround(ground_x + off_x * ratio);
    next.y = (int)lround((double)view->ground_y + off_y * ratio);
    next.z = (int)lround(ground_z + off_z * ratio);
    if( next.x == eye->x && next.y == eye->y && next.z == eye->z )
        return 0;
    *eye = next;
    return 1;
}

int
EditorWorldGesture_TwistYaw(int radians_q16)
{
    double const steps = (double)radians_q16 / 65536.0 * 2048.0 / (2.0 * 3.14159265358979323846);

    /* The scene turns with the fingers. Carrying the eye round the look-at
     * point by a POSITIVE yaw step turns the ground clockwise on the canvas
     * (y down), so a clockwise twist is that step's own sign;
     * editor_world_gesture_test pins it with a point right of centre. */
    return (int)lround(steps);
}

static int
world_gesture_twist(
    struct ToriDraw_Camera* camera,
    struct ToriDraw_Position* eye,
    const struct EditorWorldGestureView* view,
    const struct ToriRS_GestureFrame* frame)
{
    int const step = EditorWorldGesture_TwistYaw(frame->radians_q16);
    double look_x;
    double look_z;
    double off_x;
    double off_z;
    double c;
    double s;

    if( step == 0 )
        return 0;
    /* The look-at point: the ground under the viewport's centre. */
    if( !world_gesture_ground(camera, eye, view, view->viewport_x + view->viewport_w / 2,
            view->viewport_y + view->viewport_h / 2, &look_x, &look_z) )
        return 0;
    /* Turning the yaw by `step` turns the view's forward (-sin, cos) by the
     * same rotation, so the eye's offset from the look-at point is turned by
     * it too and the point stays centred. The tables, not libm: the
     * projection that draws the result reads them. */
    c = (double)ToriDraw_Cos(step & 2047) / 65536.0;
    s = (double)ToriDraw_Sin(step & 2047) / 65536.0;
    off_x = (double)eye->x - look_x;
    off_z = (double)eye->z - look_z;
    eye->x = (int)lround(look_x + off_x * c - off_z * s);
    eye->z = (int)lround(look_z + off_z * c + off_x * s);
    camera->yaw = ToriDraw_AddAngle(camera->yaw, step);
    return 1;
}

int
EditorWorldGesture_Apply(
    struct ToriDraw_Camera* camera,
    struct ToriDraw_Position* eye,
    const struct EditorWorldGestureView* view,
    const struct ToriRS_GestureFrame* frame)
{
    assert(camera);
    assert(eye);
    assert(view);
    assert(frame);
    assert(view->viewport_w > 0);
    assert(view->viewport_h > 0);
    assert(frame->kind >= TORIRS_GESTURE_PAN);
    assert(frame->kind <= TORIRS_GESTURE_TWIST);

    if( camera->projection_mode == TORIDRAW_PROJECTION_MODE_PARALLEL )
        return 0;
    switch( frame->kind )
    {
    case TORIRS_GESTURE_PAN:
        /* One finger is the armed tool's: the marquee, a stroke. */
        if( frame->fingers < 2 )
            return 0;
        if( frame->dx == 0 && frame->dy == 0 )
            return 0;
        return world_gesture_pan(camera, eye, view, frame);
    case TORIRS_GESTURE_PINCH:
        return world_gesture_pinch(camera, eye, view, frame);
    case TORIRS_GESTURE_TWIST:
        return world_gesture_twist(camera, eye, view, frame);
    default:
        return 0;
    }
}
