/*
 * The scripted camera's state and arithmetic (render/cam_script.h).
 *
 * What each CAM_* packet sets and what it cancels, the easing curves at their
 * ends and middles, the rate walk and the yaw seam, the timed moves and turns
 * read back at a cycle, the glide home that ends only when every view has
 * arrived, the shake roll shared between paints of one frame, the free
 * camera's focus, and the event ring the quest driver reads.
 *
 * Run: make -C src test-cam-script
 */

#include "render/cam_script.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* Big enough that a stack copy is a bad idea. */
static struct CamScript g_cam;

static void
fresh(void)
{
    memset(&g_cam, 0, sizeof(g_cam));
}

static int
near_double(double a, double b)
{
    return fabs(a - b) < 1e-9;
}

static void
test_ease_ends_and_middles(void)
{
    for( int easing = 0; easing <= 28; easing++ )
    {
        TEST_ASSERT(CamScript_Ease(0, 30, easing) == 0.0, "every curve starts at 0");
        TEST_ASSERT(CamScript_Ease(30, 30, easing) == 1.0, "every curve ends at 1");
        TEST_ASSERT(CamScript_Ease(-5, 30, easing) == 0.0, "before the start is the start");
        TEST_ASSERT(CamScript_Ease(99, 30, easing) == 1.0, "past the end is the end");
    }
    TEST_ASSERT(CamScript_Ease(0, 0, 0) == 1.0, "no duration is already there");
    TEST_ASSERT(near_double(CamScript_Ease(15, 30, 0), 0.5), "linear is half way at half time");
    TEST_ASSERT(near_double(CamScript_Ease(15, 30, 4), 0.25), "quad in is a quarter at half time");
    TEST_ASSERT(near_double(CamScript_Ease(15, 30, 5), 0.75), "quad out is three quarters");
    TEST_ASSERT(near_double(CamScript_Ease(15, 30, 3), 0.5), "sine in-out is half way");
    TEST_ASSERT(near_double(CamScript_Ease(10, 30, 99), 10.0 / 30.0), "an unknown id is linear");
    TEST_ASSERT(CamScript_Ease(5, 30, 22) < 0.0, "back in dips below the start");
}

static void
test_walk_never_overshoots(void)
{
    TEST_ASSERT(CamScript_Walk(0, 1000, 10, 0) == 10, "a flat rate steps by the rate");
    TEST_ASSERT(CamScript_Walk(0, 1000, 0, 100) == 100, "rate2 is a thousandth of what is left");
    TEST_ASSERT(CamScript_Walk(0, 1000, 5, 100) == 105, "and the two add");
    TEST_ASSERT(CamScript_Walk(995, 1000, 10, 0) == 1000, "a step past the target stops on it");
    TEST_ASSERT(CamScript_Walk(1000, 0, 10, 0) == 990, "downward too");
    TEST_ASSERT(CamScript_Walk(5, 0, 10, 0) == 0, "and stops on it");
    TEST_ASSERT(CamScript_Walk(7, 7, 10, 500) == 7, "arrived stays");
    TEST_ASSERT(CamScript_Walk(0, 1000, 0, 0) == 0, "zero rates hold still");
}

static void
test_walk_yaw_takes_the_short_way(void)
{
    TEST_ASSERT(CamScript_WalkYaw(2040, 10, 4, 0) == 2044, "across the seam forwards");
    TEST_ASSERT(CamScript_WalkYaw(2046, 10, 20, 0) == 10, "and stops on the far side");
    TEST_ASSERT(CamScript_WalkYaw(10, 2040, 4, 0) == 6, "backwards across it");
    TEST_ASSERT(CamScript_WalkYaw(2, 2040, 20, 0) == 2040, "stopping on the target");
    TEST_ASSERT(CamScript_WalkYaw(100, 100, 50, 50) == 100, "arrived stays");
}

static void
test_moveto_and_lookat_set_their_half(void)
{
    fresh();
    g_cam.move_cycles.active = 1;
    g_cam.smooth_reset = 1;
    TEST_ASSERT(!CamScript_MoveTo(&g_cam, 10, 20, 300, 4, 50), "a rate2 under 100 glides");
    TEST_ASSERT(g_cam.scripted && !g_cam.smooth_reset, "the script is up, the glide cancelled");
    TEST_ASSERT(!g_cam.move_cycles.active, "a timed move is cancelled");
    TEST_ASSERT(g_cam.last_move_height == 300, "the height is what a relative move adds to");
    {
        int x, z, h, r, r2;
        CamScript_MoveTarget(&g_cam, &x, &z, &h);
        CamScript_MoveRates(&g_cam, &r, &r2);
        TEST_ASSERT(x == 10 && z == 20 && h == 300, "the move target reads back");
        TEST_ASSERT(r == 4 && r2 == 50, "and its rates");
    }
    TEST_ASSERT(CamScript_MoveTo(&g_cam, 1, 2, 3, 0, 100), "a rate2 of 100 cuts");

    fresh();
    g_cam.rotate_cycles.active = 1;
    g_cam.look_cycles.active = 1;
    g_cam.move_rate = 9;
    TEST_ASSERT(CamScript_LookAt(&g_cam, 5, 6, 7, 8, 120), "the look cuts at 100 or more");
    TEST_ASSERT(!g_cam.rotate_cycles.active && !g_cam.look_cycles.active, "the timed turns stop");
    TEST_ASSERT(g_cam.move_rate == 9, "the move half is left as it is");
    {
        int x, z, h;
        CamScript_LookTarget(&g_cam, &x, &z, &h);
        TEST_ASSERT(x == 5 && z == 6 && h == 7, "the look target reads back");
    }
}

static void
test_move_cycles_read_back_at_a_cycle(void)
{
    int x = -1, z = -1, y = -1, ignore = -1;

    fresh();
    TEST_ASSERT(!CamScript_MoveCyclesAt(&g_cam, 0, &x, &z, &y, &ignore), "no move, no answer");
    TEST_ASSERT(x == -1, "and the outputs are left alone");
    CamScript_MoveCycles(&g_cam, 0, 0, 100, 1000, 2000, 300, 1, 50, 100, 0);
    TEST_ASSERT(g_cam.scripted, "a timed move puts the script up");
    TEST_ASSERT(CamScript_MoveCyclesAt(&g_cam, 100, &x, &z, &y, &ignore), "it is running");
    TEST_ASSERT(x == 500 && z == 1000 && y == 200, "half way at half time");
    TEST_ASSERT(ignore == 1, "and says how to read y");
    CamScript_MoveCyclesAt(&g_cam, 500, &x, &z, &y, &ignore);
    TEST_ASSERT(x == 1000 && z == 2000 && y == 300, "held at the end once done");

    CamScript_MoveArc(&g_cam, 1000, 0);
    TEST_ASSERT(g_cam.move_cycles.arc, "a running move takes the arc");
    CamScript_MoveCyclesAt(&g_cam, 50, &x, &z, &y, &ignore);
    TEST_ASSERT(abs(x) <= 1 && abs(z) <= 1, "the arc starts at the start");

    fresh();
    CamScript_MoveArc(&g_cam, 1000, 0);
    TEST_ASSERT(!g_cam.move_cycles.arc, "a move that is not running takes no arc");

    fresh();
    g_cam.last_move_height = 40;
    TEST_ASSERT(CamScript_MoveHeightFor(&g_cam, 10, 1) == 50, "relative adds to the last moveto");
    TEST_ASSERT(CamScript_MoveHeightFor(&g_cam, 10, 0) == 10, "absolute is the height");
    g_cam.look_height = 70;
    TEST_ASSERT(CamScript_LookHeightFor(&g_cam, 5, 1) == 75, "relative adds to the look height");
}

static void
test_turns_restart_one_axis(void)
{
    int pitch = -1, yaw = -1;

    fresh();
    g_cam.look_cycles.active = 1;
    CamScript_StartYaw(&g_cam, 100, 150, 0, 30, 0);
    CamScript_RotateBegun(&g_cam);
    TEST_ASSERT(CamScript_Rotating(&g_cam), "the turn runs");
    TEST_ASSERT(!g_cam.look_cycles.active, "and the look ease is off");
    TEST_ASSERT(!CamScript_RotatePitchAt(&g_cam, 15, &pitch), "an axis not started is not easing");
    TEST_ASSERT(CamScript_RotateYawAt(&g_cam, 15, &yaw) && yaw == 125, "the yaw is half way");

    CamScript_StartYawShortWay(&g_cam, 100, 1900, 0, 30, 0);
    TEST_ASSERT(g_cam.rotate_cycles.yaw.to == 1900 - 2048, "the short way round goes negative");
    CamScript_RotateYawAt(&g_cam, 30, &yaw);
    TEST_ASSERT(yaw == 1900, "and lands in 0..2047");
    CamScript_StartYaw(&g_cam, 100, 1900, 0, 30, 0);
    TEST_ASSERT(g_cam.rotate_cycles.yaw.to == 1900, "a delta names its own direction");

    fresh();
    CamScript_RotateCycles(&g_cam, 200, 300, 10, 2000, 55, 0, 10, 0);
    TEST_ASSERT(g_cam.rotate_cycles.pitch.to == 300, "both axes start");
    TEST_ASSERT(g_cam.rotate_cycles.yaw.to == 2000 - 2048, "the yaw the short way");
    TEST_ASSERT(g_cam.look_height == 55, "the look height is the one the packet means");
    TEST_ASSERT(CamScript_RotatePitchAt(&g_cam, 5, &pitch) && pitch == 250, "pitch half way");
}

static void
test_look_cycles_end_on_arrival(void)
{
    int x, z, h, rate, rate2;

    fresh();
    g_cam.look_lx = 0;
    g_cam.look_lz = 0;
    g_cam.look_height = 100;
    g_cam.rotate_cycles.active = 1;
    CamScript_LookCycles(&g_cam, 10, 0, 50, 1, 0, 20, 0);
    TEST_ASSERT(g_cam.scripted && !g_cam.rotate_cycles.active, "a look ease stops the turn");
    TEST_ASSERT(g_cam.look_lx == 10 && g_cam.look_height == 150, "the target it finishes on");
    TEST_ASSERT(CamScript_LookCyclesStep(&g_cam, 10, &x, &z, &h), "it runs");
    TEST_ASSERT(x == 64 + 640 && z == 64 && h == 125, "half way between the two points");
    CamScript_LookRates(&g_cam, &rate, &rate2);
    TEST_ASSERT(rate == CAM_SCRIPT_LOOK_CYCLES_FLAT_RATE, "the turn follows at the flat rate");
    TEST_ASSERT(g_cam.look_cycles.active, "not over before its duration");
    CamScript_LookCyclesStep(&g_cam, 20, &x, &z, &h);
    TEST_ASSERT(!g_cam.look_cycles.active, "over once its duration has passed");
    TEST_ASSERT(!CamScript_LookCyclesStep(&g_cam, 21, &x, &z, &h), "and then it answers no");
}

static void
test_resets_and_the_glide(void)
{
    fresh();
    TEST_ASSERT(!CamScript_SmoothReset(&g_cam), "a script that is not up has nothing to glide");
    TEST_ASSERT(!g_cam.smooth_reset, "so no glide starts");

    CamScript_MoveTo(&g_cam, 1, 2, 3, 4, 5);
    CamScript_Shake(&g_cam, 2, 1, 2, 3);
    g_cam.rotate_cycles.active = 1;
    TEST_ASSERT(CamScript_SmoothReset(&g_cam), "a script that is up glides");
    TEST_ASSERT(g_cam.scripted && g_cam.smooth_reset, "still scripted while it glides");
    TEST_ASSERT(!CamScript_HoldsEye(&g_cam), "but the follow camera computes home");
    TEST_ASSERT(!g_cam.rotate_cycles.active && !g_cam.shake[2], "eases and shakes off");
    TEST_ASSERT(g_cam.move_rate == 4, "the rates stay for the glide");

    CamScript_GlideFrameBegin(&g_cam);
    CamScript_GlideArrived(&g_cam);
    CamScript_GlideEnd(&g_cam, 2);
    TEST_ASSERT(g_cam.scripted, "one of two views home is not the end");
    CamScript_GlideFrameBegin(&g_cam);
    CamScript_GlideArrived(&g_cam);
    CamScript_GlideArrived(&g_cam);
    CamScript_GlideEnd(&g_cam, 2);
    TEST_ASSERT(!g_cam.scripted && !g_cam.smooth_reset, "every view home releases the script");
    TEST_ASSERT(g_cam.move_rate == 0 && g_cam.move_lx == 0, "and the release zeroes the targets");

    fresh();
    CamScript_LookAt(&g_cam, 1, 2, 3, 4, 5);
    CamScript_GlideArrived(&g_cam);
    CamScript_GlideEnd(&g_cam, 1);
    TEST_ASSERT(g_cam.scripted, "no glide, nothing ends");

    CamScript_Shake(&g_cam, 0, 1, 1, 1);
    CamScript_Reset(&g_cam);
    TEST_ASSERT(!g_cam.scripted && !g_cam.shake[0], "CAM_RESET drops the script and the shakes");
    TEST_ASSERT(g_cam.look_rate == 0 && g_cam.look_height == 0, "and zeroes the look half");

    fresh();
    CamScript_MoveTo(&g_cam, 7, 8, 9, 10, 11);
    CamScript_Shake(&g_cam, 4, 1, 1, 1);
    CamScript_Rebase(&g_cam);
    TEST_ASSERT(!g_cam.scripted && !g_cam.shake[4], "a rebase drops the script and the shakes");
    TEST_ASSERT(g_cam.move_lx == 7 && g_cam.move_rate == 10, "and keeps the targets and rates");
}

static void
test_the_shake_roll_is_shared_per_frame(void)
{
    int jitter = 12345;

    fresh();
    CamScript_Shake(&g_cam, 1, 0, 0, 100);
    TEST_ASSERT(CamScript_ShakeOn(&g_cam, 1) && !CamScript_ShakeOn(&g_cam, 0), "one axis shakes");
    CamScript_RollShake(&g_cam, 1, 7);
    TEST_ASSERT(g_cam.shake_cycle[1] == 1, "a roll advances the axis's cycle");
    CamScript_RollShake(&g_cam, 1, 7);
    TEST_ASSERT(g_cam.shake_cycle[1] == 1, "a shared paint of the same frame reuses the roll");
    CamScript_RollShake(&g_cam, 0, 7);
    TEST_ASSERT(g_cam.shake_cycle[1] == 2, "an unshared paint always rolls");
    CamScript_RollShake(&g_cam, 1, 8);
    TEST_ASSERT(g_cam.shake_cycle[1] == 3, "a new frame rolls");
    TEST_ASSERT(!CamScript_ShakeJitter(&g_cam, 0, &jitter), "a still axis has no jitter");
    TEST_ASSERT(jitter == 12345, "and leaves the output alone");
    TEST_ASSERT(CamScript_ShakeJitter(&g_cam, 1, &jitter) && jitter == 0, "spread 0, amplitude 0");

    fresh();
    CamScript_Shake(&g_cam, 3, 0, 50, 100);
    g_cam.shake_cycle[3] = 2;
    CamScript_RollShake(&g_cam, 0, 1);
    CamScript_ShakeJitter(&g_cam, 3, &jitter);
    TEST_ASSERT(jitter == (int)(sin(2.0) * 50), "the sine at the axis's cycle and speed");
    CamScript_StopShakes(&g_cam);
    TEST_ASSERT(!CamScript_ShakeOn(&g_cam, 3), "stopped");
}

static void
test_unlock_mode_and_sync(void)
{
    fresh();
    TEST_ASSERT(CamScript_SetUnlock(&g_cam, 1), "a new value is a change");
    TEST_ASSERT(!CamScript_SetUnlock(&g_cam, 1), "the same value is not");
    TEST_ASSERT(CamScript_Unlocked(&g_cam), "and it reads back");

    g_cam.oculus_y_valid = 1;
    CamScript_SetMode(&g_cam, 1);
    TEST_ASSERT(CamScript_FreeCamera(&g_cam), "mode 1 is the free camera");
    TEST_ASSERT(!g_cam.oculus_y_valid, "and its focus height is forgotten");
    CamScript_OculusSync(&g_cam, 5);
    TEST_ASSERT(CamScript_OculusRecentrePending(&g_cam), "a new sync brings the focus back");
    g_cam.oculus_recentre = 0;
    CamScript_OculusSync(&g_cam, 5);
    TEST_ASSERT(!CamScript_OculusRecentrePending(&g_cam), "the same sync does not");

    g_cam.oculus_y_valid = 1;
    g_cam.oculus_y = -40;
    g_cam.oculus_recentre = 1;
    CamScript_OculusRecentred(&g_cam, -90);
    TEST_ASSERT(g_cam.oculus_y == -90 && !g_cam.oculus_recentre, "the focus comes down to ground");
    g_cam.oculus_recentre = 1;
    CamScript_OculusRecentred(&g_cam, -10);
    TEST_ASSERT(g_cam.oculus_y == -90, "but is never pushed up by it");

    CamScript_ExitFreeCamera(&g_cam);
    TEST_ASSERT(!CamScript_FreeCamera(&g_cam) && g_cam.oculus_y_valid, "Escape leaves only the mode");

    CamScript_SetMode(&g_cam, 1);
    CamScript_OculusSync(&g_cam, 9);
    CamScript_Logout(&g_cam);
    TEST_ASSERT(!g_cam.cam_mode && !g_cam.oculus_sync && !g_cam.oculus_recentre, "a logout clears");
}

static void
test_the_free_camera_focus_moves(void)
{
    int step = -1, angle = -2;

    fresh();
    TEST_ASSERT(CamScript_OculusFocusY(&g_cam, -300) == -300, "the first height is the ground's");
    TEST_ASSERT(CamScript_OculusFocusY(&g_cam, -999) == -300, "then it is the focus's own");

    TEST_ASSERT(!CamScript_OculusStep(&g_cam, &step, &angle), "no keys, no movement");
    TEST_ASSERT(step == 0 && angle == -1, "and no heading");

    CamScript_OculusKeys(&g_cam, 1, 0, 0, 1, 0, 1, 0);
    TEST_ASSERT(CamScript_OculusStep(&g_cam, &step, &angle), "W+D moves");
    TEST_ASSERT(angle == 256, "forward and right");
    TEST_ASSERT(g_cam.oculus_speed == 24 && step == 1, "the speed eases up by an eighth");
    TEST_ASSERT(g_cam.oculus_y == -299, "F lowers the focus by the step");
    for( int i = 0; i < 20; i++ )
        CamScript_OculusStep(&g_cam, &step, &angle);
    TEST_ASSERT(g_cam.oculus_speed == 192, "up to the held speed and no further");

    CamScript_OculusKeys(&g_cam, 0, 0, 0, 0, 0, 0, 0);
    CamScript_OculusStep(&g_cam, &step, &angle);
    TEST_ASSERT(g_cam.oculus_speed == 153 && angle == 256, "released, it coasts the same way");

    fresh();
    g_cam.oculus_y_valid = 1;
    g_cam.oculus_y = -2;
    CamScript_OculusKeys(&g_cam, 0, 0, 0, 0, 0, 1, 1);
    for( int i = 0; i < 10; i++ )
        CamScript_OculusStep(&g_cam, &step, &angle);
    TEST_ASSERT(g_cam.oculus_y == 0, "the focus never goes above 0");
    TEST_ASSERT(angle == -1, "a height-only move has no heading");
}

static void
test_the_event_ring(void)
{
    struct CamScriptEvent const* event;

    fresh();
    TEST_ASSERT(CamScript_Serial(&g_cam) == 0, "nothing recorded yet");
    CamScript_Record(&g_cam, CAM_SCRIPT_OP_MOVETO, 3200, 3201, 400, 5, 6, 77);
    TEST_ASSERT(CamScript_Serial(&g_cam) == 1, "one recorded");
    event = CamScript_Event(&g_cam, 1);
    TEST_ASSERT(event->serial == 1 && event->op == CAM_SCRIPT_OP_MOVETO, "the event and its op");
    TEST_ASSERT(event->world_x == 3200 && event->world_z == 3201, "its world tile");
    TEST_ASSERT(event->height == 400 && event->rate == 5 && event->rate2 == 6, "its values");
    TEST_ASSERT(event->tick == 77, "and the tick the caller gave");

    for( int i = 0; i < CAM_SCRIPT_EVENTS; i++ )
        CamScript_Record(&g_cam, CAM_SCRIPT_OP_SHAKE, -1, -1, i, 0, 0, 0);
    TEST_ASSERT(CamScript_Serial(&g_cam) == CAM_SCRIPT_EVENTS + 1, "the serial counts every one");
    event = CamScript_Event(&g_cam, CamScript_Serial(&g_cam));
    TEST_ASSERT(event->serial == CAM_SCRIPT_EVENTS + 1, "the newest is where the serial says");
    TEST_ASSERT(event->height == CAM_SCRIPT_EVENTS - 1, "and is the last one recorded");
    event = CamScript_Event(&g_cam, 1);
    TEST_ASSERT(event->serial != 1, "a serial older than the ring has been written over");
}

int
main(void)
{
    test_ease_ends_and_middles();
    test_walk_never_overshoots();
    test_walk_yaw_takes_the_short_way();
    test_moveto_and_lookat_set_their_half();
    test_move_cycles_read_back_at_a_cycle();
    test_turns_restart_one_axis();
    test_look_cycles_end_on_arrival();
    test_resets_and_the_glide();
    test_the_shake_roll_is_shared_per_frame();
    test_unlock_mode_and_sync();
    test_the_free_camera_focus_moves();
    test_the_event_ring();
    if( g_failures )
    {
        printf("cam_script: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("cam_script: all checks passed\n");
    return 0;
}
