/*
 * The World camera under gesture frames (editor/editor_world_gesture.h):
 * a fake frame list against a camera pose, no renderer. Checked through the
 * client's own projection (ToriRS_WorldProjectPoint): a two-finger pan keeps
 * the ground under the fingers and yaw and pitch; a pinch keeps the ground
 * point under the pinch and scales the eye distance by the ratio; a twist
 * keeps the look-at point centred and turns the scene with the fingers; a
 * one-finger pan is never the camera's; a recorded pinch replays to the same
 * pose.
 */

#include "editor/editor_world_gesture.h"
#include "render/torirs_world_projection.h"
#include "toridraw_math.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failed++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

#define VIEW_W 800
#define VIEW_H 600

static struct EditorWorldGestureView g_view = { 0, 0, VIEW_W, VIEW_H, 0 };

static void
pose_init(
    struct ToriDraw_Camera* camera,
    struct ToriDraw_Position* eye)
{
    memset(camera, 0, sizeof(*camera));
    memset(eye, 0, sizeof(*eye));
    camera->projection_mode = TORIDRAW_PROJECTION_MODE_SCALE;
    camera->projection_scale = 512;
    camera->pitch = 256;
    camera->yaw = 300;
    eye->x = 6400;
    eye->y = -900;
    eye->z = 6400;
}

/** A decoded frame (struct LibToriRS_Gesture), the ratio and the turn in
 *  q16 as the bus carries them; a 0 ratio is the identity, 1. */
static struct LibToriRS_Gesture
frame(int kind, int x, int y, int dx, int dy, int fingers, int scale_q16, int radians_q16)
{
    struct LibToriRS_Gesture f;

    memset(&f, 0, sizeof(f));
    f.kind = (uint8_t)kind;
    f.x = x;
    f.y = y;
    f.dx = (float)dx;
    f.dy = (float)dy;
    f.fingers = (uint8_t)fingers;
    f.scale = scale_q16 > 0 ? (float)scale_q16 / 65536.0f : 1.0f;
    f.radians = (float)radians_q16 / 65536.0f;
    f.phase = LIBTORIRS_GESTURE_UPDATE;
    return f;
}

/** The ground point under canvas (x, y), or 0. */
static int
ground_at(
    const struct ToriDraw_Camera* camera,
    const struct ToriDraw_Position* eye,
    int x,
    int y,
    double* out_x,
    double* out_z)
{
    return ToriRS_WorldUnprojectPlane(camera, eye, 0, 0, VIEW_W, VIEW_H, x, y, g_view.ground_y, out_x, out_z);
}

/** Where ground point (wx, wz) is drawn, in canvas px. */
static int
project(
    const struct ToriDraw_Camera* camera,
    const struct ToriDraw_Position* eye,
    double wx,
    double wz,
    int* out_x,
    int* out_y)
{
    return ToriRS_WorldProjectPoint(camera, eye, 0, 0, VIEW_W, VIEW_H, 50, (int)lround(wx), g_view.ground_y,
        (int)lround(wz), out_x, out_y);
}

static double
eye_distance(
    const struct ToriDraw_Position* eye,
    double gx,
    double gz)
{
    double const dx = eye->x - gx;
    double const dy = eye->y - g_view.ground_y;
    double const dz = eye->z - gz;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static void
test_pan(void)
{
    struct ToriDraw_Camera camera;
    struct ToriDraw_Position eye;
    struct ToriDraw_Position before;
    struct LibToriRS_Gesture f;
    double gx;
    double gz;
    int sx;
    int sy;

    pose_init(&camera, &eye);
    /* The ground under where the fingers were: after the frame it is under
     * where they are. */
    CHECK(ground_at(&camera, &eye, 450 - 30, 350 + 20, &gx, &gz), "the probe ray meets the ground");
    f = frame(LIBTORIRS_GESTURE_PAN, 450, 350, 30, -20, 2, 0, 0);
    before = eye;
    CHECK(EditorWorldGesture_Apply(&camera, &eye, &g_view, &f) == 1, "a two-finger pan moves the camera");
    CHECK(project(&camera, &eye, gx, gz, &sx, &sy), "the point is still in front");
    CHECK(abs(sx - 450) <= 1 && abs(sy - 350) <= 1, "the ground stays under the fingers (%d,%d)", sx, sy);
    CHECK(camera.yaw == 300 && camera.pitch == 256, "yaw and pitch are kept");
    CHECK(eye.y == before.y, "the eye slides along the ground plane, its height kept");

    /* Fingers down the canvas pull the ground toward the camera: the eye
     * moves away from where it looks (the look-at point moves back). */
    {
        double look0_x;
        double look0_z;
        double look1_x;
        double look1_z;
        CHECK(ground_at(&camera, &eye, VIEW_W / 2, VIEW_H / 2, &look0_x, &look0_z), "look-at");
        f = frame(LIBTORIRS_GESTURE_PAN, 400, 300, 0, 40, 2, 0, 0);
        CHECK(EditorWorldGesture_Apply(&camera, &eye, &g_view, &f) == 1, "a downward pan moves it");
        CHECK(ground_at(&camera, &eye, VIEW_W / 2, VIEW_H / 2, &look1_x, &look1_z), "look-at after");
        CHECK(look1_x != look0_x || look1_z != look0_z, "the look-at point moved along the ground");
    }

    /* One finger is the armed tool's drag (the marquee, a stroke). */
    before = eye;
    f = frame(LIBTORIRS_GESTURE_PAN, 450, 350, 30, -20, 1, 0, 0);
    CHECK(EditorWorldGesture_Apply(&camera, &eye, &g_view, &f) == 0, "a one-finger pan is not the camera's");
    CHECK(memcmp(&eye, &before, sizeof(eye)) == 0, "and leaves the pose alone");
}

static void
test_pinch(void)
{
    struct ToriDraw_Camera camera;
    struct ToriDraw_Position eye;
    struct LibToriRS_Gesture f;
    double gx;
    double gz;
    double d0;
    double d1;
    int sx;
    int sy;

    pose_init(&camera, &eye);
    CHECK(ground_at(&camera, &eye, 560, 410, &gx, &gz), "the pinch's ground point");
    d0 = eye_distance(&eye, gx, gz);
    f = frame(LIBTORIRS_GESTURE_PINCH, 560, 410, 0, 0, 2, 2 * 65536, 0);
    CHECK(EditorWorldGesture_Apply(&camera, &eye, &g_view, &f) == 1, "a pinch moves the camera");
    d1 = eye_distance(&eye, gx, gz);
    CHECK(fabs(d1 - d0 / 2.0) <= 2.0, "x2 apart is half the distance (%.1f -> %.1f)", d0, d1);
    CHECK(project(&camera, &eye, gx, gz, &sx, &sy), "the point is in front");
    CHECK(abs(sx - 560) <= 1 && abs(sy - 410) <= 1, "the ground point stays under the pinch (%d,%d)", sx, sy);
    CHECK(camera.yaw == 300 && camera.pitch == 256, "a pinch turns nothing");

    f = frame(LIBTORIRS_GESTURE_PINCH, 560, 410, 0, 0, 2, 65536 / 2, 0);
    CHECK(EditorWorldGesture_Apply(&camera, &eye, &g_view, &f) == 1, "fingers together go back");
    CHECK(fabs(eye_distance(&eye, gx, gz) - d0) <= 3.0, "to the distance it started at");

    /* Clamped at both ends, and the point still under the pinch. */
    for( int i = 0; i < 40; i++ )
    {
        f = frame(LIBTORIRS_GESTURE_PINCH, 560, 410, 0, 0, 2, 3 * 65536, 0);
        (void)EditorWorldGesture_Apply(&camera, &eye, &g_view, &f);
    }
    CHECK(eye_distance(&eye, gx, gz) >= EDITOR_WORLD_GESTURE_DISTANCE_MIN - 2.0,
        "never closer than the near clamp (%.1f)", eye_distance(&eye, gx, gz));
    f = frame(LIBTORIRS_GESTURE_PINCH, 560, 410, 0, 0, 2, 3 * 65536, 0);
    CHECK(EditorWorldGesture_Apply(&camera, &eye, &g_view, &f) == 0, "at the clamp a pinch in moves nothing");
    for( int i = 0; i < 40; i++ )
    {
        f = frame(LIBTORIRS_GESTURE_PINCH, 560, 410, 0, 0, 2, 65536 / 3, 0);
        (void)EditorWorldGesture_Apply(&camera, &eye, &g_view, &f);
    }
    CHECK(eye_distance(&eye, gx, gz) <= EDITOR_WORLD_GESTURE_DISTANCE_MAX + 2.0, "never farther than the far clamp");
}

static void
test_twist(void)
{
    struct ToriDraw_Camera camera;
    struct ToriDraw_Position eye;
    struct LibToriRS_Gesture f;
    double lx;
    double lz;
    double px;
    double pz;
    int sx;
    int sy;
    int step;

    pose_init(&camera, &eye);
    CHECK(ground_at(&camera, &eye, VIEW_W / 2, VIEW_H / 2, &lx, &lz), "the look-at point");
    /* A ground point right of the centre: a clockwise twist (positive)
     * carries it down the canvas, clockwise about the centre. */
    CHECK(ground_at(&camera, &eye, VIEW_W / 2 + 150, VIEW_H / 2, &px, &pz), "a point right of it");
    step = EditorWorldGesture_TwistYaw(6554 / 65536.0);
    CHECK(step != 0, "a tenth of a radian is a yaw step (%d)", step);
    f = frame(LIBTORIRS_GESTURE_TWIST, 400, 300, 0, 0, 2, 0, 6554);
    CHECK(EditorWorldGesture_Apply(&camera, &eye, &g_view, &f) == 1, "a twist turns the camera");
    CHECK(camera.yaw == ((300 + step) & 2047), "by that step of yaw");
    CHECK(camera.pitch == 256, "pitch kept");
    CHECK(project(&camera, &eye, lx, lz, &sx, &sy), "look-at in front");
    CHECK(abs(sx - VIEW_W / 2) <= 1 && abs(sy - VIEW_H / 2) <= 1, "the look-at point stays centred (%d,%d)", sx,
        sy);
    CHECK(project(&camera, &eye, px, pz, &sx, &sy), "the side point in front");
    CHECK(sy > VIEW_H / 2, "the scene turns with the fingers: right of centre goes down (%d)", sy);
    CHECK(EditorWorldGesture_TwistYaw(-6554 / 65536.0) == -step, "the other way is the other sign");
    f = frame(LIBTORIRS_GESTURE_TWIST, 400, 300, 0, 0, 2, 0, 10);
    CHECK(EditorWorldGesture_Apply(&camera, &eye, &g_view, &f) == 0, "a twist under half a step moves nothing");
}

/** The 6.1 check "a recorded pinch replays to the same camera pose": the
 *  same frames from the same pose land on the same pose, bit for bit. */
static void
test_replay(void)
{
    static const struct
    {
        int kind;
        int x;
        int y;
        int dx;
        int dy;
        int scale_q16;
        int radians_q16;
    } k_recorded[] = {
        { LIBTORIRS_GESTURE_PINCH, 512, 300, 0, 0, 70000, 0 },  { LIBTORIRS_GESTURE_PAN, 512, 300, 6, -3, 0, 0 },
        { LIBTORIRS_GESTURE_PINCH, 518, 297, 0, 0, 71234, 0 },  { LIBTORIRS_GESTURE_TWIST, 518, 297, 0, 0, 0, 3000 },
        { LIBTORIRS_GESTURE_PINCH, 520, 290, 0, 0, 60000, 0 },  { LIBTORIRS_GESTURE_PAN, 520, 290, -11, 9, 0, 0 },
        { LIBTORIRS_GESTURE_PINCH, 509, 299, 0, 0, 66000, 0 },  { LIBTORIRS_GESTURE_TWIST, 509, 299, 0, 0, 0, -9000 },
    };
    struct ToriDraw_Camera camera[2];
    struct ToriDraw_Position eye[2];
    int moved = 0;

    for( int run = 0; run < 2; run++ )
    {
        pose_init(&camera[run], &eye[run]);
        for( size_t i = 0; i < sizeof(k_recorded) / sizeof(k_recorded[0]); i++ )
        {
            struct LibToriRS_Gesture f = frame(k_recorded[i].kind, k_recorded[i].x, k_recorded[i].y,
                k_recorded[i].dx, k_recorded[i].dy, 2, k_recorded[i].scale_q16, k_recorded[i].radians_q16);
            moved += EditorWorldGesture_Apply(&camera[run], &eye[run], &g_view, &f);
        }
    }
    CHECK(moved == 2 * 8, "every recorded frame moved the camera (%d)", moved);
    CHECK(memcmp(&eye[0], &eye[1], sizeof(eye[0])) == 0 && camera[0].yaw == camera[1].yaw &&
              camera[0].pitch == camera[1].pitch,
        "a recorded pinch replays to the same pose");
}

static void
test_parallel(void)
{
    struct ToriDraw_Camera camera;
    struct ToriDraw_Position eye;
    struct LibToriRS_Gesture f;

    pose_init(&camera, &eye);
    camera.projection_mode = TORIDRAW_PROJECTION_MODE_PARALLEL;
    f = frame(LIBTORIRS_GESTURE_PINCH, 400, 300, 0, 0, 2, 2 * 65536, 0);
    CHECK(EditorWorldGesture_Apply(&camera, &eye, &g_view, &f) == 0, "a parallel camera has no eye distance");
}

int
main(void)
{
    ToriDraw_InitMath();
    test_pan();
    test_pinch();
    test_twist();
    test_replay();
    test_parallel();
    printf("editor_world_gesture_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
