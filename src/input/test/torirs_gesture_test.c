/*
 * The gesture frames (TORIRS_CMD_GESTURE_PAN / _PINCH / _TWIST), from the
 * producers' streams to the list the consumers drain.
 *
 * What is pinned here:
 *
 *   1. The frames RECORD AND REPLAY: encoded, teed to a recording, pumped
 *      back and decoded, they are the same LibToriRS_Gesture values bit for
 *      bit as the live bus gave -- which is what "a recorded pinch replays
 *      to the same camera pose" rests on.
 *   2. The decode refuses frames no encoder writes, and the per-frame list
 *      has the key_events lifetime (Begin clears, Continue keeps), folds an
 *      UPDATE into its kind when full and counts what it cannot fold.
 *   3. The stream's phases: BEGIN once, UPDATE after, END when told or when
 *      quiet; a pan whose fingers change is a new pan; the pair recognizer
 *      begins each kind at its own threshold and carries everything before.
 *   4. The touch policy under ToriRS_TouchSetGestureFrames: two fingers
 *      apart are PINCH frames, travelling together PAN frames, turning TWIST
 *      frames, three fingers a pan of three -- and not one wheel notch or
 *      arrow key among them. Without the flag the same fingers are the
 *      notches and keys they always were, and no gesture frame at all.
 *   5. TORIRS_SIM_GESTURE's grammar and the phases a run of entries makes.
 *
 * The bus is a real ToriRS_CmdBus and the input a real LibToriRS_Input, so
 * what is asserted is what the App would read.
 */

#include "cmd/cmdbus.h"
#include "input/torirs_gesture.h"
#include "input/torirs_input.h"
#include "input/torirs_input_cmd.h"
#include "input/torirs_touch.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond, what)                                             \
    do                                                                \
    {                                                                 \
        g_checks++;                                                   \
        if( !(cond) )                                                 \
        {                                                             \
            g_failures++;                                             \
            printf("FAIL: %s (%s:%d)\n", (what), __FILE__, __LINE__); \
        }                                                             \
    } while( 0 )

/** The legacy commands a drain saw beside the gestures. */
struct Legacy
{
    int wheels;
    int key_downs;
    int key_ups;
    int downs;
    int ups;
    int last_button;
    int pinches;
};

/* One host frame: Begin, every command applied, End -- main.c's boundary. */
static void
drain_frame(
    struct ToriRS_CmdBus* bus,
    struct LibToriRS_Input* input,
    uint64_t now,
    struct Legacy* legacy)
{
    struct ToriRS_CmdHeader header;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];

    if( legacy )
        memset(legacy, 0, sizeof(*legacy));
    LibToriRS_Input_Begin(input, now);
    while( CmdBus_Pop(bus, &header, payload) )
    {
        if( legacy )
        {
            if( header.type == TORIRS_CMD_INPUT_MOUSE_WHEEL )
                legacy->wheels++;
            else if( header.type == TORIRS_CMD_INPUT_KEY_DOWN )
                legacy->key_downs++;
            else if( header.type == TORIRS_CMD_INPUT_KEY_UP )
                legacy->key_ups++;
            else if( header.type == TORIRS_CMD_INPUT_PINCH )
                legacy->pinches++;
            else if( header.type == TORIRS_CMD_INPUT_MOUSE_DOWN || header.type == TORIRS_CMD_INPUT_MOUSE_UP )
            {
                struct ToriRS_CmdMouseButton btn;
                memcpy(&btn, payload, sizeof(btn));
                if( header.type == TORIRS_CMD_INPUT_MOUSE_DOWN )
                    legacy->downs++;
                else
                    legacy->ups++;
                legacy->last_button = btn.button;
            }
        }
        ToriRS_Input_ApplyCmd(input, &header, payload);
    }
    LibToriRS_Input_End(input);
}

/** How many of this frame's gestures are `kind` in `phase` (-1: any). */
static int
count_of(struct LibToriRS_Input const* input, int kind, int phase)
{
    int n = 0;
    for( int i = 0; i < input->gesture_count; i++ )
        if( input->gestures[i].kind == kind && (phase < 0 || input->gestures[i].phase == phase) )
            n++;
    return n;
}

/** The first of this frame's gestures of `kind`, or NULL. */
static struct LibToriRS_Gesture const*
first_of(struct LibToriRS_Input const* input, int kind)
{
    for( int i = 0; i < input->gesture_count; i++ )
        if( input->gestures[i].kind == kind )
            return &input->gestures[i];
    return NULL;
}

/* Sum of a kind's motion this frame: pan travel, pinch product, twist turn. */
static void
motion_of(
    struct LibToriRS_Input const* input,
    int kind,
    double* out_dx,
    double* out_dy,
    double* out_scale,
    double* out_radians)
{
    *out_dx = 0.0;
    *out_dy = 0.0;
    *out_scale = 1.0;
    *out_radians = 0.0;
    for( int i = 0; i < input->gesture_count; i++ )
    {
        struct LibToriRS_Gesture const* g = &input->gestures[i];
        if( g->kind != kind )
            continue;
        *out_dx += g->dx;
        *out_dy += g->dy;
        *out_scale *= g->scale;
        *out_radians += g->radians;
    }
}

/* ---- 1. record and replay ------------------------------------------------ */

static void
push_sample_frames(struct ToriRS_CmdBus* bus)
{
    CmdBus_PushFrame(bus, 1000);
    CmdBus_PushGesturePan(
        bus, 120, 80, 3.25, -1.5, 2, LIBTORIRS_GESTURE_BEGIN, LIBTORIRS_GESTURE_FLAG_ALSO_LEGACY);
    CmdBus_PushGesturePinch(bus, 121, 81, 1.125, LIBTORIRS_GESTURE_BEGIN, LIBTORIRS_GESTURE_FLAG_TOUCH);
    CmdBus_PushGestureTwist(bus, 122, 82, -0.375, LIBTORIRS_GESTURE_BEGIN, 0);
    /* A ratio with no exact 16.16 spelling, so the replay has to reproduce
     * the rounding rather than merely the value. */
    CmdBus_PushGesturePinch(bus, 121, 81, 2.0 / 3.0, LIBTORIRS_GESTURE_UPDATE, LIBTORIRS_GESTURE_FLAG_TOUCH);
    CmdBus_PushFrame(bus, 1016);
    CmdBus_PushGesturePan(bus, 130, 90, 0.0, 0.0, 2, LIBTORIRS_GESTURE_END, LIBTORIRS_GESTURE_FLAG_ALSO_LEGACY);
    CmdBus_PushGesturePinch(bus, 131, 91, 1.0, LIBTORIRS_GESTURE_END, LIBTORIRS_GESTURE_FLAG_TOUCH);
    CmdBus_PushGestureTwist(bus, 132, 92, 0.0, LIBTORIRS_GESTURE_END, 0);
}

static void
test_record_replay(char const* record_path)
{
    struct ToriRS_CmdBus bus;
    struct LibToriRS_Input live[2];
    struct LibToriRS_Input replay[2];
    FILE* f;
    uint64_t now = 0;

    CmdBus_Init(&bus);
    CHECK(CmdBus_RecordOpen(&bus, record_path), "the recording opens");
    push_sample_frames(&bus);
    CmdBus_RecordClose(&bus);

    /* Live: everything up to the second FRAME is the first host frame. */
    LibToriRS_Input_Init(&live[0], 0);
    LibToriRS_Input_Init(&live[1], 0);
    {
        struct ToriRS_CmdHeader header;
        uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
        int frame = -1;

        while( CmdBus_Pop(&bus, &header, payload) )
        {
            if( header.type == TORIRS_CMD_FRAME )
            {
                if( frame >= 0 )
                    LibToriRS_Input_End(&live[frame]);
                frame++;
                LibToriRS_Input_Begin(&live[frame], (uint64_t)frame);
                continue;
            }
            ToriRS_Input_ApplyCmd(&live[frame], &header, payload);
        }
        LibToriRS_Input_End(&live[frame]);
    }

    LibToriRS_Input_Init(&replay[0], 0);
    LibToriRS_Input_Init(&replay[1], 0);
    f = CmdReplay_Open(record_path);
    CHECK(f != NULL, "the recording replays");
    if( !f )
        return;
    for( int frame = 0; frame < 2; frame++ )
    {
        CmdBus_Init(&bus);
        CHECK(CmdReplay_PumpFrame(f, &bus, &now), "a recorded frame pumps");
        drain_frame(&bus, &replay[frame], (uint64_t)frame, NULL);
    }
    CHECK(!CmdReplay_PumpFrame(f, &bus, &now), "and there are exactly two");
    fclose(f);

    CHECK(replay[0].gesture_count == 4, "the first frame holds its four gesture frames");
    CHECK(replay[1].gesture_count == 3, "the second its three ENDs");
    for( int frame = 0; frame < 2; frame++ )
        CHECK(
            replay[frame].gesture_count == live[frame].gesture_count &&
                memcmp(
                    replay[frame].gestures,
                    live[frame].gestures,
                    sizeof(struct LibToriRS_Gesture) * (size_t)live[frame].gesture_count) == 0,
            "the replay decodes to the live values bit for bit");

    {
        struct LibToriRS_Gesture const* pan = &replay[0].gestures[0];
        struct LibToriRS_Gesture const* pinch = &replay[0].gestures[1];
        struct LibToriRS_Gesture const* twist = &replay[0].gestures[2];
        struct LibToriRS_Gesture const* third = &replay[0].gestures[3];

        CHECK(pan->kind == LIBTORIRS_GESTURE_PAN && pan->phase == LIBTORIRS_GESTURE_BEGIN, "a PAN BEGIN");
        CHECK(pan->x == 120 && pan->y == 80, "at its centre");
        CHECK(pan->dx == 3.25f && pan->dy == -1.5f, "its travel, exact in 16.16");
        CHECK(pan->fingers == 2, "with its fingers");
        CHECK(pan->flags == LIBTORIRS_GESTURE_FLAG_ALSO_LEGACY, "and its flags");
        CHECK(pan->scale == 1.0f && pan->radians == 0.0f, "a pan's unused fields hold their identities");
        CHECK(pinch->kind == LIBTORIRS_GESTURE_PINCH && pinch->scale == 1.125f, "a PINCH's ratio");
        CHECK(pinch->flags == LIBTORIRS_GESTURE_FLAG_TOUCH, "flagged a touch");
        CHECK(pinch->dx == 0.0f && pinch->fingers == 0, "a pinch carries no travel");
        CHECK(twist->kind == LIBTORIRS_GESTURE_TWIST && twist->radians == -0.375f, "a TWIST's turn, signed");
        CHECK(third->scale == 43691.0f / 65536.0f, "2/3 is 43690.67 steps of 16.16, rounded to 43691");
        CHECK(replay[1].gestures[0].phase == LIBTORIRS_GESTURE_END, "the END comes back an END");
        CHECK(replay[1].gestures[0].dx == 0.0f, "with no travel");
        CHECK(replay[1].gestures[1].scale == 1.0f, "and a pinch END scale 1");
    }
}

/* ---- 2. decode guards and the list ---------------------------------------- */

static void
test_decode_and_list(void)
{
    struct ToriRS_CmdBus bus;
    struct LibToriRS_Input input;
    struct ToriRS_CmdGesturePan pan;
    struct ToriRS_CmdGesturePinch pinch;

    LibToriRS_Input_Init(&input, 0);
    CmdBus_Init(&bus);

    /* Frames only a corrupt recording holds. */
    memset(&pan, 0, sizeof(pan));
    pan.fingers = 2;
    pan.phase = 7;
    CmdBus_Push(&bus, TORIRS_CMD_GESTURE_PAN, &pan, sizeof(pan));
    pan.phase = LIBTORIRS_GESTURE_UPDATE;
    pan.fingers = 0;
    CmdBus_Push(&bus, TORIRS_CMD_GESTURE_PAN, &pan, sizeof(pan));
    memset(&pinch, 0, sizeof(pinch));
    pinch.scale_q16 = 0;
    CmdBus_Push(&bus, TORIRS_CMD_GESTURE_PINCH, &pinch, sizeof(pinch));
    pinch.scale_q16 = 65536;
    CmdBus_Push(&bus, TORIRS_CMD_GESTURE_PINCH, &pinch, sizeof(pinch) - 1);
    drain_frame(&bus, &input, 1, NULL);
    CHECK(input.gesture_count == 0, "a bad phase, no fingers, a zero ratio and a short payload decode to nothing");

    {
        struct ToriRS_CmdHeader header = { TORIRS_CMD_GESTURE_TWIST, sizeof(struct ToriRS_CmdGestureTwist) };
        struct ToriRS_CmdGestureTwist twist;
        memset(&twist, 0, sizeof(twist));
        CHECK(
            ToriRS_Input_ApplyCmd(&input, &header, (uint8_t const*)&twist) == 1,
            "a gesture frame is an input command to the App's drain");
    }

    /* The key_events lifetime. */
    LibToriRS_Input_Begin(&input, 2);
    CmdBus_PushGestureTwist(&bus, 1, 2, 0.5, LIBTORIRS_GESTURE_BEGIN, 0);
    drain_frame(&bus, &input, 3, NULL);
    CHECK(input.gesture_count == 1, "this frame's gesture is in the list");
    LibToriRS_Input_Continue(&input, 4);
    CHECK(input.gesture_count == 1, "Continue keeps it for the frame that consumes it");
    LibToriRS_Input_Begin(&input, 5);
    CHECK(input.gesture_count == 0, "Begin clears it");

    /* Full: an UPDATE folds into its kind, a boundary is dropped and counted. */
    LibToriRS_Input_Begin(&input, 6);
    CmdBus_Init(&bus);
    CmdBus_PushGesturePan(&bus, 0, 0, 1.0, 0.0, 2, LIBTORIRS_GESTURE_BEGIN, 0);
    for( int i = 1; i < LIBTORIRS_GESTURE_MAX; i++ )
        CmdBus_PushGesturePan(&bus, 0, 0, 1.0, 0.0, 2, LIBTORIRS_GESTURE_UPDATE, 0);
    CmdBus_PushGesturePan(&bus, 9, 9, 2.0, 1.0, 2, LIBTORIRS_GESTURE_UPDATE, 0);
    CmdBus_PushGesturePinch(&bus, 0, 0, 2.0, LIBTORIRS_GESTURE_BEGIN, 0);
    CmdBus_PushGesturePinch(&bus, 0, 0, 2.0, LIBTORIRS_GESTURE_UPDATE, 0);
    CmdBus_PushGesturePan(&bus, 9, 9, 0.0, 0.0, 2, LIBTORIRS_GESTURE_END, 0);
    {
        struct ToriRS_CmdHeader header;
        uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
        while( CmdBus_Pop(&bus, &header, payload) )
            ToriRS_Input_ApplyCmd(&input, &header, payload);
    }
    {
        double dx;
        double dy;
        double scale;
        double radians;
        motion_of(&input, LIBTORIRS_GESTURE_PAN, &dx, &dy, &scale, &radians);
        CHECK(input.gesture_count == LIBTORIRS_GESTURE_MAX, "the list stops at its cap");
        CHECK(dx == (double)LIBTORIRS_GESTURE_MAX + 2.0 && dy == 1.0, "the UPDATE past it folded into the pan, travel kept");
        CHECK(input.gestures[LIBTORIRS_GESTURE_MAX - 1].x == 9, "at the newer centre");
        CHECK(input.gesture_dropped == 3, "a BEGIN, an UPDATE with no open entry of its kind and an END are dropped");
        CHECK(input.gesture_dropped_total == 3, "and counted since Init");
    }
    LibToriRS_Input_Begin(&input, 7);
    CHECK(input.gesture_dropped == 0 && input.gesture_dropped_total == 3, "a frame's drops reset, the total does not");
}

/* ---- 3. the stream --------------------------------------------------------- */

static void
test_stream(void)
{
    struct ToriRS_CmdBus bus;
    struct LibToriRS_Input input;
    struct ToriRS_GestureStream stream;

    LibToriRS_Input_Init(&input, 0);
    CmdBus_Init(&bus);
    ToriRS_GestureStreamReset(&stream);

    ToriRS_GestureStreamPan(&stream, &bus, 10, 10, 1.0, 0.0, 2, 0, 100);
    ToriRS_GestureStreamPan(&stream, &bus, 11, 10, 1.0, 0.0, 2, 0, 110);
    ToriRS_GestureStreamPan(&stream, &bus, 12, 10, 1.0, 0.0, 2, 0, 120);
    drain_frame(&bus, &input, 1, NULL);
    CHECK(count_of(&input, LIBTORIRS_GESTURE_PAN, LIBTORIRS_GESTURE_BEGIN) == 1, "the first pan step BEGINs");
    CHECK(count_of(&input, LIBTORIRS_GESTURE_PAN, LIBTORIRS_GESTURE_UPDATE) == 2, "the rest UPDATE");

    ToriRS_GestureStreamPan(&stream, &bus, 12, 10, 0.0, 3.0, 3, 0, 130);
    drain_frame(&bus, &input, 2, NULL);
    CHECK(
        input.gesture_count == 2 && input.gestures[0].phase == LIBTORIRS_GESTURE_END &&
            input.gestures[0].fingers == 2 && input.gestures[1].phase == LIBTORIRS_GESTURE_BEGIN &&
            input.gestures[1].fingers == 3,
        "a third finger ends the two-finger pan and begins a three-finger one");

    ToriRS_GestureStreamIdle(&stream, &bus, 130 + TORIRS_GESTURE_IDLE_END_MS - 1, TORIRS_GESTURE_IDLE_END_MS);
    drain_frame(&bus, &input, 3, NULL);
    CHECK(input.gesture_count == 0, "a stream quiet for less than the idle time is still going");
    ToriRS_GestureStreamIdle(&stream, &bus, 130 + TORIRS_GESTURE_IDLE_END_MS, TORIRS_GESTURE_IDLE_END_MS);
    drain_frame(&bus, &input, 4, NULL);
    CHECK(
        input.gesture_count == 1 && input.gestures[0].phase == LIBTORIRS_GESTURE_END && input.gestures[0].x == 12,
        "past it, it ENDs at its last centre");
    ToriRS_GestureStreamEnd(&stream, &bus, LIBTORIRS_GESTURE_PAN);
    ToriRS_GestureStreamEndAll(&stream, &bus);
    drain_frame(&bus, &input, 5, NULL);
    CHECK(input.gesture_count == 0, "an END is idempotent");

    /* The pair recognizer: each kind at its own threshold, with everything
     * before it in the BEGIN. */
    ToriRS_GestureStreamReset(&stream);
    ToriRS_GestureStreamPair(&stream, &bus, 50, 50, 4.0, 0.0, 1.03, 0.05, 2, 0, 200);
    ToriRS_GestureStreamPair(&stream, &bus, 50, 50, 4.0, 0.0, 1.03, 0.05, 2, 0, 210);
    drain_frame(&bus, &input, 6, NULL);
    CHECK(input.gesture_count == 0, "8 px, 6% and 0.1 rad are all still under their thresholds");
    ToriRS_GestureStreamPair(&stream, &bus, 50, 50, 4.0, 0.0, 1.03, 0.0, 2, 0, 220);
    drain_frame(&bus, &input, 7, NULL);
    {
        struct LibToriRS_Gesture const* pan = first_of(&input, LIBTORIRS_GESTURE_PAN);
        struct LibToriRS_Gesture const* pinch = first_of(&input, LIBTORIRS_GESTURE_PINCH);
        CHECK(pan && pan->phase == LIBTORIRS_GESTURE_BEGIN && fabs(pan->dx - 12.0) < 1e-3, "12 px begins the pan with all 12");
        CHECK(
            pinch && pinch->phase == LIBTORIRS_GESTURE_BEGIN && fabs(pinch->scale - 1.03 * 1.03 * 1.03) < 1e-4,
            "9% begins the pinch with the whole product");
        CHECK(count_of(&input, LIBTORIRS_GESTURE_TWIST, -1) == 0, "the twist has not begun");
    }
    ToriRS_GestureStreamPair(&stream, &bus, 51, 50, 1.0, 0.0, 1.0, 0.0, 2, 0, 230);
    drain_frame(&bus, &input, 8, NULL);
    CHECK(
        count_of(&input, LIBTORIRS_GESTURE_PAN, LIBTORIRS_GESTURE_UPDATE) == 1 && input.gesture_count == 1,
        "a begun pan reports a 1 px step; an unmoved pinch reports nothing");
    ToriRS_GestureStreamEndAll(&stream, &bus);
    drain_frame(&bus, &input, 9, NULL);
    CHECK(
        count_of(&input, LIBTORIRS_GESTURE_PAN, LIBTORIRS_GESTURE_END) == 1 &&
            count_of(&input, LIBTORIRS_GESTURE_PINCH, LIBTORIRS_GESTURE_END) == 1 && input.gesture_count == 2,
        "EndAll ends the two begun kinds and only those");
    ToriRS_GestureStreamPair(&stream, &bus, 50, 50, 0.0, 0.0, 1.0, 0.1, 2, 0, 240);
    drain_frame(&bus, &input, 10, NULL);
    CHECK(input.gesture_count == 0, "and forgets the twist's pending 0.1 rad");
}

/* ---- 4. the touch policy ---------------------------------------------------- */

/* Two fingers down and a baseline tick, in a fresh policy. */
static void
two_down(
    struct ToriRS_Touch* touch,
    struct ToriRS_CmdBus* bus,
    struct LibToriRS_Input* input,
    int gesture_frames,
    int ax,
    int ay,
    int bx,
    int by)
{
    CmdBus_Init(bus);
    ToriRS_TouchReset(touch);
    ToriRS_TouchSetGestureFrames(touch, gesture_frames);
    ToriRS_TouchEvent(touch, bus, TORIRS_TOUCH_BEGAN, 1, ax, ay, 0);
    ToriRS_TouchEvent(touch, bus, TORIRS_TOUCH_BEGAN, 2, bx, by, 5);
    ToriRS_TouchEvent(touch, bus, TORIRS_TOUCH_MOVED, 2, bx, by, 10);
    ToriRS_TouchTick(touch, bus, 10);
    drain_frame(bus, input, 10, NULL);
}

static void
test_touch(void)
{
    struct ToriRS_CmdBus bus;
    struct ToriRS_Touch touch;
    struct LibToriRS_Input input;
    struct Legacy legacy;
    double product = 1.0;
    int ended = 0;

    LibToriRS_Input_Init(&input, 0);

    /* ---- a pinch, under the flag: PINCH frames and no wheel ---- */
    two_down(&touch, &bus, &input, 1, 300, 200, 400, 200);
    CHECK(input.gesture_count == 0, "two fingers resting are no gesture");
    for( int step = 1; step <= 6; step++ )
    {
        ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 300 - step * 10, 200, 10 + step * 16);
        ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 2, 400 + step * 10, 200, 12 + step * 16);
        ToriRS_TouchTick(&touch, &bus, 16 + step * 16);
        drain_frame(&bus, &input, 16 + step * 16, &legacy);
        CHECK(legacy.wheels == 0, "the pinch turns no wheel");
        CHECK(legacy.key_downs == 0, "and holds no key");
        CHECK(count_of(&input, LIBTORIRS_GESTURE_PINCH, -1) == 1, "one PINCH a tick");
        CHECK(count_of(&input, LIBTORIRS_GESTURE_PAN, -1) == 0, "a symmetric pinch does not pan");
        CHECK(count_of(&input, LIBTORIRS_GESTURE_TWIST, -1) == 0, "or twist");
        if( step == 1 )
        {
            struct LibToriRS_Gesture const* g = first_of(&input, LIBTORIRS_GESTURE_PINCH);
            CHECK(g && g->phase == LIBTORIRS_GESTURE_BEGIN, "100 to 120 px is past 8%: the pinch begins");
            CHECK(g && fabs(g->scale - 1.2) < 1e-4, "with the ratio 1.2");
            CHECK(g && g->x == 350 && g->y == 200, "about the fingers' centroid");
            CHECK(g && (g->flags & LIBTORIRS_GESTURE_FLAG_TOUCH), "flagged a touch");
            CHECK(g && !(g->flags & LIBTORIRS_GESTURE_FLAG_ALSO_LEGACY), "and not doubled by a legacy command");
        }
        else
            CHECK(count_of(&input, LIBTORIRS_GESTURE_PINCH, LIBTORIRS_GESTURE_UPDATE) == 1, "then UPDATEs");
        product *= first_of(&input, LIBTORIRS_GESTURE_PINCH)->scale;
    }
    CHECK(fabs(product - 2.2) < 1e-3, "the ratios multiply to the whole spread, 100 to 220 px");
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 2, 460, 200, 200);
    drain_frame(&bus, &input, 200, &legacy);
    ended = count_of(&input, LIBTORIRS_GESTURE_PINCH, LIBTORIRS_GESTURE_END);
    CHECK(ended == 1, "a finger lifting ENDs the pinch");
    CHECK(legacy.downs == 0 && legacy.ups == 0, "and the pair leaves no tap behind");
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 1, 240, 200, 210);
    ToriRS_TouchTick(&touch, &bus, 220);
    drain_frame(&bus, &input, 210, &legacy);
    CHECK(input.gesture_count == 0 && legacy.downs == 0, "the second lift is nothing more");

    /* ---- the same pinch without the flag: the wheel, as it always was ---- */
    two_down(&touch, &bus, &input, 0, 300, 200, 400, 200);
    {
        int wheels = 0;
        int gestures = 0;
        for( int step = 1; step <= 6; step++ )
        {
            ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 300 - step * 10, 200, 10 + step * 16);
            ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 2, 400 + step * 10, 200, 12 + step * 16);
            ToriRS_TouchTick(&touch, &bus, 16 + step * 16);
            drain_frame(&bus, &input, 16 + step * 16, &legacy);
            wheels += legacy.wheels;
            gestures += input.gesture_count;
        }
        CHECK(wheels > 0, "without the flag a pinch is wheel notches");
        CHECK(gestures == 0, "and no gesture frame at all");
    }

    /* ---- two fingers travelling together: PAN, no arrow key ---- */
    two_down(&touch, &bus, &input, 1, 300, 200, 360, 200);
    {
        double dx_sum = 0.0;
        int begins = 0;
        for( int step = 1; step <= 5; step++ )
        {
            ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 300 + step * 8, 200 + step * 2, 10 + step * 16);
            ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 2, 360 + step * 8, 200 + step * 2, 12 + step * 16);
            ToriRS_TouchTick(&touch, &bus, 16 + step * 16);
            drain_frame(&bus, &input, 16 + step * 16, &legacy);
            CHECK(legacy.key_downs == 0 && legacy.wheels == 0, "a pan holds no arrow and turns no wheel");
            CHECK(count_of(&input, LIBTORIRS_GESTURE_PINCH, -1) == 0, "a pan that keeps its spread does not pinch");
            begins += count_of(&input, LIBTORIRS_GESTURE_PAN, LIBTORIRS_GESTURE_BEGIN);
            if( step == 1 )
                CHECK(count_of(&input, LIBTORIRS_GESTURE_PAN, -1) == 0, "8 px of travel is under the start");
            if( step == 2 )
            {
                struct LibToriRS_Gesture const* g = first_of(&input, LIBTORIRS_GESTURE_PAN);
                CHECK(g && g->phase == LIBTORIRS_GESTURE_BEGIN, "16 px begins it");
                CHECK(g && g->dx == 16.0f && g->dy == 4.0f, "with all the travel since the fingers landed");
                CHECK(g && g->fingers == 2, "two fingers");
                CHECK(g && g->x == 346 && g->y == 204, "at the centroid");
            }
            {
                double dx;
                double dy;
                double scale;
                double radians;
                motion_of(&input, LIBTORIRS_GESTURE_PAN, &dx, &dy, &scale, &radians);
                dx_sum += dx;
            }
        }
        CHECK(begins == 1, "one BEGIN");
        CHECK(dx_sum == 40.0, "the travel adds up to the fingers' 40 px");
        /* Lifted 10 px on from its last move, between two ticks. */
        ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 1, 350, 210, 120);
        drain_frame(&bus, &input, 120, &legacy);
        CHECK(
            input.gesture_count == 2 && input.gestures[0].phase == LIBTORIRS_GESTURE_UPDATE &&
                input.gestures[0].dx == 5.0f,
            "the lifting finger's last travel goes out first (half its 10 px, at the centroid)");
        CHECK(
            count_of(&input, LIBTORIRS_GESTURE_PINCH, -1) == 0,
            "and its roll, 60 to 50 px of spread, begins no pinch on the way out");
        CHECK(count_of(&input, LIBTORIRS_GESTURE_PAN, LIBTORIRS_GESTURE_END) == 1, "then the lift ENDs it");
        CHECK(legacy.key_ups == 0, "and releases no key, because none was held");
    }

    /* ---- the same pan without the flag: an arrow key, as it always was ---- */
    two_down(&touch, &bus, &input, 0, 300, 200, 360, 200);
    {
        int key_downs = 0;
        int gestures = 0;
        /* Each finger by less than the pinch step at a time, so the spread
         * never looks like a pinch to the notch synthesis either. */
        for( int step = 1; step <= 6; step++ )
        {
            ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 300 + step * 10, 200, 10 + step * 16);
            ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 2, 360 + step * 10, 200, 12 + step * 16);
            ToriRS_TouchTick(&touch, &bus, 16 + step * 16);
            drain_frame(&bus, &input, 16 + step * 16, &legacy);
            key_downs += legacy.key_downs;
            gestures += input.gesture_count;
        }
        CHECK(key_downs == 1, "without the flag a pan holds an arrow key");
        CHECK(gestures == 0, "and is no gesture frame");
    }

    /* ---- turning: TWIST, clockwise positive ---- */
    two_down(&touch, &bus, &input, 1, 300, 200, 400, 200);
    {
        double turned = 0.0;
        /* Finger 2 swings about the centre (350, 200) on a 50 px radius;
         * finger 1 opposite it. Canvas y is down, so +angle is clockwise. */
        for( int step = 1; step <= 4; step++ )
        {
            double const a = 0.1 * step;
            int const ox = (int)floor(50.0 * cos(a) + 0.5);
            int const oy = (int)floor(50.0 * sin(a) + 0.5);
            double dx;
            double dy;
            double scale;
            double radians;
            ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 350 - ox, 200 - oy, 10 + step * 16);
            ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 2, 350 + ox, 200 + oy, 12 + step * 16);
            ToriRS_TouchTick(&touch, &bus, 16 + step * 16);
            drain_frame(&bus, &input, 16 + step * 16, &legacy);
            CHECK(legacy.key_downs == 0 && legacy.wheels == 0, "a twist synthesises nothing");
            CHECK(count_of(&input, LIBTORIRS_GESTURE_PAN, -1) == 0, "turning about the centre does not pan");
            CHECK(count_of(&input, LIBTORIRS_GESTURE_PINCH, -1) == 0, "or pinch");
            motion_of(&input, LIBTORIRS_GESTURE_TWIST, &dx, &dy, &scale, &radians);
            turned += radians;
            if( step == 1 )
                CHECK(count_of(&input, LIBTORIRS_GESTURE_TWIST, -1) == 0, "0.1 rad is under the start");
            if( step == 2 )
                CHECK(
                    count_of(&input, LIBTORIRS_GESTURE_TWIST, LIBTORIRS_GESTURE_BEGIN) == 1,
                    "0.2 rad begins it");
        }
        CHECK(fabs(turned - 0.4) < 0.03, "the turns add up to the fingers' 0.4 rad, clockwise positive");
        ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 2, 396, 219, 120);
        drain_frame(&bus, &input, 120, &legacy);
        CHECK(count_of(&input, LIBTORIRS_GESTURE_TWIST, LIBTORIRS_GESTURE_END) == 1, "the lift ENDs the twist");
    }

    /* ---- a third finger: a new pan, of three ---- */
    two_down(&touch, &bus, &input, 1, 300, 200, 360, 200);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 320, 200, 20);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 2, 380, 200, 22);
    ToriRS_TouchTick(&touch, &bus, 26);
    drain_frame(&bus, &input, 26, &legacy);
    CHECK(count_of(&input, LIBTORIRS_GESTURE_PAN, LIBTORIRS_GESTURE_BEGIN) == 1, "two fingers pan");
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 3, 350, 260, 30);
    ToriRS_TouchTick(&touch, &bus, 32);
    drain_frame(&bus, &input, 32, &legacy);
    CHECK(
        count_of(&input, LIBTORIRS_GESTURE_PAN, LIBTORIRS_GESTURE_END) == 1 && input.gesture_count == 1,
        "a third finger landing ENDs the two-finger pan, and its landing is not motion");
    for( int step = 1; step <= 2; step++ )
    {
        ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 320, 200 + step * 10, 40 + step * 16);
        ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 2, 380, 200 + step * 10, 41 + step * 16);
        ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 3, 350, 260 + step * 10, 42 + step * 16);
        ToriRS_TouchTick(&touch, &bus, 44 + step * 16);
        drain_frame(&bus, &input, 44 + step * 16, &legacy);
    }
    {
        struct LibToriRS_Gesture const* g = first_of(&input, LIBTORIRS_GESTURE_PAN);
        CHECK(g && g->fingers == 3, "three fingers dragging are a pan of three (the touch pitch)");
        CHECK(g && g->dy == 20.0f, "the centroid's 20 px");
    }

    /* ---- the flag off in the middle of a gesture ends it on the next tick ---- */
    ToriRS_TouchSetGestureFrames(&touch, 0);
    ToriRS_TouchTick(&touch, &bus, 100);
    drain_frame(&bus, &input, 100, &legacy);
    CHECK(count_of(&input, LIBTORIRS_GESTURE_PAN, LIBTORIRS_GESTURE_END) == 1, "turning the flag off ends the open pan");

    /* ---- a tap is still a tap under the flag ---- */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchSetGestureFrames(&touch, 1);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 100, 80, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 1, 102, 81, 90);
    drain_frame(&bus, &input, 90, &legacy);
    CHECK(legacy.downs == 1 && legacy.ups == 1 && legacy.last_button == TORIRSM_LEFT, "a tap is a left click under the flag");
    CHECK(input.gesture_count == 0, "and no gesture");
}

/* ---- 5. TORIRS_SIM_GESTURE -------------------------------------------------- */

static void
test_sim(void)
{
    struct ToriRS_SimGesture entries[8];
    struct ToriRS_SimGestureState state;
    struct ToriRS_CmdBus bus;
    struct LibToriRS_Input input;
    char err[160];
    char const* text = "10,pinch,320,240,1.5;11,pinch,320,240,1.25;10,pan,100,50,4,-2;12,touch-twist,5,6,-0.25;"
                       "11,pan3,1,2,0,9";
    int count;

    CHECK(ToriRS_SimGestureCapacity(text) == 5, "capacity is one more than the semicolons");
    count = ToriRS_SimGestureParse(text, entries, 8, err, sizeof(err));
    CHECK(count == 5, "five entries parse");
    CHECK(entries[0].kind == LIBTORIRS_GESTURE_PINCH && entries[0].a == 1.5, "a pinch and its ratio");
    CHECK(entries[2].kind == LIBTORIRS_GESTURE_PAN && entries[2].fingers == 2 && entries[2].b == -2.0, "pan: two fingers, dx and dy");
    CHECK(entries[3].flags == LIBTORIRS_GESTURE_FLAG_TOUCH && entries[3].kind == LIBTORIRS_GESTURE_TWIST, "touch-twist");
    CHECK(entries[4].fingers == 3, "pan3 is three fingers");

    {
        static char const* const bad[] = {
            "x,pan,1,2,3,4", "10,spin,1,2,3", "10,pan,1,2,3", "10,pinch,1,2,0", "10,pinch,1,2,2,3",
            "10,pan,1,2,3,4,5", "10,pan0,1,2,3,4", "10,pinch,1", "-1,twist,1,2,0.1",
        };
        for( size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++ )
        {
            int const n = ToriRS_SimGestureParse(bad[i], entries, 8, err, sizeof(err));
            CHECK(n == -1, bad[i]);
            CHECK(strstr(err, "bad entry") != NULL, "and the reason names the entry");
        }
        CHECK(ToriRS_SimGestureParse("", entries, 8, err, sizeof(err)) == 0, "an empty variable is no entries");
    }

    count = ToriRS_SimGestureParse(text, entries, 8, err, sizeof(err));
    ToriRS_SimGestureStateInit(&state);
    LibToriRS_Input_Init(&input, 0);
    CmdBus_Init(&bus);
    for( long frame = 9; frame <= 14; frame++ )
    {
        ToriRS_SimGestureStep(entries, count, &state, &bus, frame);
        ToriRS_SimGestureStep(entries, count, &state, &bus, frame); /* a held frame */
        drain_frame(&bus, &input, (uint64_t)frame, NULL);
        if( frame == 9 )
            CHECK(input.gesture_count == 0, "nothing before its frame");
        if( frame == 10 )
        {
            CHECK(input.gesture_count == 2, "frame 10: the pinch and the pan, once each though stepped twice");
            CHECK(count_of(&input, LIBTORIRS_GESTURE_PINCH, LIBTORIRS_GESTURE_BEGIN) == 1, "the pinch BEGINs");
            CHECK(first_of(&input, LIBTORIRS_GESTURE_PAN)->dx == 4.0f, "the pan's dx");
        }
        if( frame == 11 )
        {
            CHECK(count_of(&input, LIBTORIRS_GESTURE_PINCH, LIBTORIRS_GESTURE_UPDATE) == 1, "the next frame's pinch UPDATEs");
            CHECK(first_of(&input, LIBTORIRS_GESTURE_PINCH)->scale == 1.25f, "by its own ratio");
            CHECK(
                input.gesture_count == 3 && input.gestures[1].phase == LIBTORIRS_GESTURE_END &&
                    input.gestures[2].phase == LIBTORIRS_GESTURE_BEGIN && input.gestures[2].fingers == 3,
                "pan3 ends the two-finger pan and begins its own");
        }
        if( frame == 12 )
        {
            CHECK(count_of(&input, LIBTORIRS_GESTURE_PINCH, LIBTORIRS_GESTURE_END) == 1, "the run's END the frame after");
            CHECK(count_of(&input, LIBTORIRS_GESTURE_PAN, LIBTORIRS_GESTURE_END) == 1, "and the pan's");
            CHECK(
                count_of(&input, LIBTORIRS_GESTURE_TWIST, LIBTORIRS_GESTURE_BEGIN) == 1 &&
                    (first_of(&input, LIBTORIRS_GESTURE_TWIST)->flags & LIBTORIRS_GESTURE_FLAG_TOUCH),
                "the touch twist begins, flagged");
        }
        if( frame == 13 )
            CHECK(
                input.gesture_count == 1 && count_of(&input, LIBTORIRS_GESTURE_TWIST, LIBTORIRS_GESTURE_END) == 1,
                "and ends");
        if( frame == 14 )
            CHECK(input.gesture_count == 0, "then nothing");
    }
}

int
main(int argc, char** argv)
{
    char const* record_path = argc > 1 ? argv[1] : "torirs_gesture_test.trscmd";

    test_record_replay(record_path);
    test_decode_and_list();
    test_stream();
    test_touch();
    test_sim();
    remove(record_path);

    printf("torirs_gesture_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
