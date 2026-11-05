/*
 * The World camera under gesture frames (editor/editor_world_gesture.h):
 * a fake frame list against a camera pose, no renderer. Checked through the
 * client's own projection (ToriRS_WorldProjectPoint): a two-finger pan keeps
 * the ground under the fingers and yaw and pitch; a pinch keeps the ground
 * point under the pinch and scales the eye distance by the ratio; a twist
 * keeps the look-at point centred and turns the scene with the fingers; a
 * one-finger pan is never the camera's; a recorded pinch replays to the same
 * pose -- recorded off the command bus and replayed from the file through the
 * input's own decode, not merely applied twice.
 */

#include "cmd/cmdbus.h"
#include "editor/editor_world_gesture.h"
#include "input/torirs_gesture.h"
#include "input/torirs_input_cmd.h"
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

/** Every frame the input decoded this host frame, through the World camera. */
static int
apply_decoded(
    const struct LibToriRS_Input* input,
    struct ToriDraw_Camera* camera,
    struct ToriDraw_Position* eye)
{
    const struct LibToriRS_Gesture* frames = NULL;
    int const count = ToriRS_InputGestures(input, &frames);
    int moved = 0;

    for( int i = 0; i < count; i++ )
        moved += EditorWorldGesture_Apply(camera, eye, &g_view, &frames[i]);
    return moved;
}

/** The 6.1 check "a recorded pinch replays to the same camera pose", end to
 *  end: the frames go onto the command bus with the recording on, the live
 *  run decodes them off the bus (ToriRS_Input_ApplyCmd) into the camera, and
 *  the replay pumps the recorded file (CmdReplay_PumpFrame) through the same
 *  decode into a second camera. The two poses are the same bit for bit, and
 *  every frame moved them. */
static void
test_replay(const char* record_path)
{
    static const struct
    {
        int kind;
        int x;
        int y;
        double dx;
        double dy;
        double scale;
        double radians;
    } k_recorded[] = {
        { LIBTORIRS_GESTURE_PINCH, 512, 300, 0, 0, 1.0681, 0 },  { LIBTORIRS_GESTURE_PAN, 512, 300, 6.25, -3.5, 1, 0 },
        { LIBTORIRS_GESTURE_PINCH, 518, 297, 0, 0, 1.0869, 0 },  { LIBTORIRS_GESTURE_TWIST, 518, 297, 0, 0, 1, 0.0458 },
        { LIBTORIRS_GESTURE_PINCH, 520, 290, 0, 0, 2.0 / 3.0, 0 }, { LIBTORIRS_GESTURE_PAN, 520, 290, -11, 9.75, 1, 0 },
        { LIBTORIRS_GESTURE_PINCH, 509, 299, 0, 0, 1.0071, 0 },  { LIBTORIRS_GESTURE_TWIST, 509, 299, 0, 0, 1, -0.1373 },
    };
    int const count = (int)(sizeof(k_recorded) / sizeof(k_recorded[0]));
    struct ToriRS_CmdBus bus;
    struct LibToriRS_Input input;
    struct ToriDraw_Camera camera[2];
    struct ToriDraw_Position eye[2];
    int moved[2] = { 0, 0 };
    uint64_t now = 0;
    FILE* f;

    /* Live: two frames a host frame, recorded as they go on the bus. */
    CmdBus_Init(&bus);
    CHECK(CmdBus_RecordOpen(&bus, record_path), "the recording opens");
    for( int i = 0; i < count; i++ )
    {
        int const phase = i < 4 ? LIBTORIRS_GESTURE_BEGIN : LIBTORIRS_GESTURE_UPDATE;
        if( i % 2 == 0 )
            CmdBus_PushFrame(&bus, 1000 + (uint64_t)i * 8);
        if( k_recorded[i].kind == LIBTORIRS_GESTURE_PAN )
            CmdBus_PushGesturePan(&bus, (int16_t)k_recorded[i].x, (int16_t)k_recorded[i].y, k_recorded[i].dx,
                k_recorded[i].dy, 2, phase, LIBTORIRS_GESTURE_FLAG_TOUCH);
        else if( k_recorded[i].kind == LIBTORIRS_GESTURE_PINCH )
            CmdBus_PushGesturePinch(&bus, (int16_t)k_recorded[i].x, (int16_t)k_recorded[i].y, k_recorded[i].scale,
                phase, LIBTORIRS_GESTURE_FLAG_TOUCH);
        else
            CmdBus_PushGestureTwist(&bus, (int16_t)k_recorded[i].x, (int16_t)k_recorded[i].y, k_recorded[i].radians,
                phase, LIBTORIRS_GESTURE_FLAG_TOUCH);
    }
    CmdBus_RecordClose(&bus);

    pose_init(&camera[0], &eye[0]);
    LibToriRS_Input_Init(&input, 0);
    {
        struct ToriRS_CmdHeader header;
        uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
        int open = 0;

        while( CmdBus_Pop(&bus, &header, payload) )
        {
            if( header.type == TORIRS_CMD_FRAME )
            {
                if( open )
                {
                    moved[0] += apply_decoded(&input, &camera[0], &eye[0]);
                    LibToriRS_Input_End(&input);
                }
                LibToriRS_Input_Begin(&input, now++);
                open = 1;
                continue;
            }
            (void)ToriRS_Input_ApplyCmd(&input, &header, payload);
        }
        if( open )
        {
            moved[0] += apply_decoded(&input, &camera[0], &eye[0]);
            LibToriRS_Input_End(&input);
        }
    }

    /* Replay: the file, a host frame at a time, through the same decode. */
    pose_init(&camera[1], &eye[1]);
    LibToriRS_Input_Init(&input, 0);
    f = CmdReplay_Open(record_path);
    CHECK(f != NULL, "the recording replays");
    if( !f )
        return;
    for( int frames = 0;; frames++ )
    {
        struct ToriRS_CmdHeader header;
        uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];

        CmdBus_Init(&bus);
        if( !CmdReplay_PumpFrame(f, &bus, &now) )
        {
            CHECK(frames == count / 2, "the recording holds its %d host frames (%d)", count / 2, frames);
            break;
        }
        LibToriRS_Input_Begin(&input, now);
        while( CmdBus_Pop(&bus, &header, payload) )
            (void)ToriRS_Input_ApplyCmd(&input, &header, payload);
        moved[1] += apply_decoded(&input, &camera[1], &eye[1]);
        LibToriRS_Input_End(&input);
    }
    fclose(f);
    remove(record_path);

    CHECK(moved[0] == count && moved[1] == count, "every recorded frame moved the camera (live %d, replay %d)",
        moved[0], moved[1]);
    CHECK(memcmp(&eye[0], &eye[1], sizeof(eye[0])) == 0 && camera[0].yaw == camera[1].yaw &&
              camera[0].pitch == camera[1].pitch,
        "a recorded pinch replays to the same pose (eye %d,%d,%d / %d,%d,%d)", eye[0].x, eye[0].y, eye[0].z, eye[1].x,
        eye[1].y, eye[1].z);
    {
        struct ToriDraw_Camera fresh;
        struct ToriDraw_Position fresh_eye;
        pose_init(&fresh, &fresh_eye);
        CHECK(memcmp(&eye[0], &fresh_eye, sizeof(fresh_eye)) != 0 && camera[0].yaw != fresh.yaw,
            "and that pose is not the one it started from");
    }
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
main(
    int argc,
    char** argv)
{
    ToriDraw_InitMath();
    test_pan();
    test_pinch();
    test_twist();
    test_replay(argc > 1 ? argv[1] : "editor_world_gesture_test.trscmd");
    test_parallel();
    printf("editor_world_gesture_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
