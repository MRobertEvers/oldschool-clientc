/*
 * The gesture policy, without a window.
 *
 * What is worth pinning here is not that a finger moves a pointer -- that is a
 * pass-through -- but the four decisions the policy makes that a backend cannot
 * see it make:
 *
 *   1. A TAP is a left click and a DRAG is not. On a frame where the world
 *      fills the screen, a click the player did not mean is a walk across the
 *      map, so the slop boundary is the difference between aiming and moving.
 *   2. A HOLD is a right click, it fires while the finger is still down, and
 *      from then on the finger is CHOOSING from the menu it opened: its
 *      travel is the pointer and never a drag, and the release is the LEFT
 *      click that picks a row, where the finger lifted. A release with no
 *      travel lands on the popup's title bar, which the popup swallows.
 *   3. A SECOND finger cancels both. A pinch that left two taps behind would
 *      zoom and walk at once, and it is the one bug in this area that looks
 *      like the game misbehaving rather than the input.
 *   4. A pinch is the WHEEL and a two-finger pan is the ARROWS, and one pass
 *      does at most one of them -- fingers moving apart also move their
 *      midpoint, so a naive reading zooms and swings the camera together.
 *   5. WHICH BUTTON a drag holds, which is how "turn the camera" and "throw a
 *      scrollbar" are told apart -- and that a window drawn over the viewport
 *      wins, because the client draws its panels inside the world's rectangle
 *      and a rectangle alone cannot tell the two apart.
 *
 * The bus is a real ToriRS_CmdBus drained into a list, so what is asserted is
 * the commands the client would actually receive, in order.
 */

#include "cmd/cmdbus.h"
#include "input/torirs_input.h"
#include "input/torirs_touch.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

/** A window covering 0,0..200x100 of the canvas, for the overlay test. */
static int
overlay_top_left(void* user, int x, int y)
{
    (void)user;
    return x >= 0 && x < 200 && y >= 0 && y < 100;
}

#define CHECK(cond, what)                                         \
    do                                                            \
    {                                                             \
        g_checks++;                                               \
        if( !(cond) )                                             \
        {                                                         \
            g_failures++;                                         \
            printf("FAIL: %s (%s:%d)\n", (what), __FILE__, __LINE__); \
        }                                                         \
    } while( 0 )

struct Seen
{
    int downs;
    int ups;
    int moves;
    int wheels;
    int leaves;
    int key_downs;
    int key_ups;
    int last_button;
    int last_x;
    int last_y;
    int last_wheel;
    int last_key;
};

static void
drain(struct ToriRS_CmdBus* bus, struct Seen* seen)
{
    struct ToriRS_CmdHeader header;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];

    memset(seen, 0, sizeof(*seen));
    while( CmdBus_Pop(bus, &header, payload) )
    {
        switch( header.type )
        {
        case TORIRS_CMD_INPUT_MOUSE_DOWN:
        case TORIRS_CMD_INPUT_MOUSE_UP:
        {
            struct ToriRS_CmdMouseButton btn;

            memcpy(&btn, payload, sizeof(btn));
            if( header.type == TORIRS_CMD_INPUT_MOUSE_DOWN )
                seen->downs++;
            else
                seen->ups++;
            seen->last_button = btn.button;
            seen->last_x = btn.x;
            seen->last_y = btn.y;
            break;
        }
        case TORIRS_CMD_INPUT_MOUSE_MOVE:
        {
            struct ToriRS_CmdMouseMove move;

            memcpy(&move, payload, sizeof(move));
            seen->moves++;
            seen->last_x = move.x;
            seen->last_y = move.y;
            break;
        }
        case TORIRS_CMD_INPUT_MOUSE_WHEEL:
        {
            struct ToriRS_CmdMouseWheel wheel;

            memcpy(&wheel, payload, sizeof(wheel));
            seen->wheels++;
            seen->last_wheel = wheel.wheel_y;
            break;
        }
        case TORIRS_CMD_INPUT_MOUSE_LEAVE:
            seen->leaves++;
            break;
        case TORIRS_CMD_INPUT_KEY_DOWN:
        case TORIRS_CMD_INPUT_KEY_UP:
        {
            struct ToriRS_CmdKey key;

            memcpy(&key, payload, sizeof(key));
            if( header.type == TORIRS_CMD_INPUT_KEY_DOWN )
                seen->key_downs++;
            else
                seen->key_ups++;
            seen->last_key = key.keycode;
            break;
        }
        default:
            break;
        }
    }
}

int
main(void)
{
    struct ToriRS_CmdBus bus;
    struct ToriRS_Touch touch;
    struct Seen seen;

    /* ---- a tap is a left click, where the finger LANDED ---- */
    /* A finger rolls as it comes off the glass -- by up to the slop, which is
     * a quarter of a 40-pixel button. The person aimed at where it landed. */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 100, 80, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 1, 101 + 8, 81 + 8, 90);
    drain(&bus, &seen);
    CHECK(seen.downs == 1 && seen.ups == 1, "a tap is one press and one release");
    CHECK(seen.last_button == TORIRSM_LEFT, "and the button is the left one");
    CHECK(seen.last_x == 100 && seen.last_y == 80, "at the point the finger landed, not where it rolled to");

    /* ---- a drag is NOT a click ---- */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 100, 80, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 100 + TORIRS_TOUCH_SLOP + 4, 80, 40);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 1, 100 + TORIRS_TOUCH_SLOP + 4, 80, 80);
    drain(&bus, &seen);
    CHECK(seen.downs == 0 && seen.ups == 0, "a finger that wandered does not click");
    CHECK(seen.moves > 0, "but the pointer still followed it");

    /* ---- inside the slop it is still a tap ---- */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 100, 80, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 100 + TORIRS_TOUCH_SLOP - 2, 80, 40);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 1, 100 + TORIRS_TOUCH_SLOP - 2, 80, 80);
    drain(&bus, &seen);
    CHECK(seen.downs == 1, "a finger that rolled a little still taps");

    /* ---- a hold is a right click, and it fires while the finger is down ---- */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 60, 40, 0);
    ToriRS_TouchTick(&touch, &bus, TORIRS_TOUCH_HOLD_MS - 1);
    drain(&bus, &seen);
    CHECK(seen.downs == 0, "nothing fires before the hold time");
    ToriRS_TouchTick(&touch, &bus, TORIRS_TOUCH_HOLD_MS + 1);
    drain(&bus, &seen);
    CHECK(seen.downs == 1 && seen.last_button == TORIRSM_RIGHT, "holding is a right click");
    /* and the release is the left click that chooses from the menu it opened */
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 1, 60, 40, TORIRS_TOUCH_HOLD_MS + 50);
    drain(&bus, &seen);
    CHECK(
        seen.downs == 1 && seen.ups == 1 && seen.last_button == TORIRSM_LEFT,
        "lifting after a hold is a left click");
    CHECK(seen.last_x == 60 && seen.last_y == 40, "the click is where the finger lifted");

    /* ---- after a hold the finger slides over the rows: moves, never a drag ---- */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchSetViewport(&touch, 0, 0, 500, 300);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 60, 40, 0);
    ToriRS_TouchTick(&touch, &bus, TORIRS_TOUCH_HOLD_MS + 1);
    drain(&bus, &seen);
    CHECK(seen.downs == 1 && seen.last_button == TORIRSM_RIGHT, "hold on the world opens the menu");
    ToriRS_TouchEvent(
        &touch, &bus, TORIRS_TOUCH_MOVED, 1, 60, 40 + TORIRS_TOUCH_SLOP * 4, TORIRS_TOUCH_HOLD_MS + 40);
    drain(&bus, &seen);
    CHECK(seen.moves == 1, "the slide moves the pointer");
    CHECK(seen.downs == 0, "and holds no button: it is not a camera drag");
    ToriRS_TouchEvent(
        &touch, &bus, TORIRS_TOUCH_ENDED, 1, 60, 40 + TORIRS_TOUCH_SLOP * 4, TORIRS_TOUCH_HOLD_MS + 80);
    drain(&bus, &seen);
    CHECK(
        seen.downs == 1 && seen.ups == 1 && seen.last_button == TORIRSM_LEFT,
        "lifting on a row is the click that picks it");
    CHECK(seen.last_y == 40 + TORIRS_TOUCH_SLOP * 4, "at the row, not where the hold began");
    ToriRS_TouchSetViewport(&touch, 0, 0, 0, 0);

    /* ---- a hold only fires if the finger stayed put ---- */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 60, 40, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 60 + TORIRS_TOUCH_SLOP + 8, 40, 10);
    ToriRS_TouchTick(&touch, &bus, TORIRS_TOUCH_HOLD_MS + 1);
    drain(&bus, &seen);
    CHECK(seen.downs == 0, "a finger that wandered never becomes a hold");

    /* ---- a second finger cancels the first one's tap ---- */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 100, 100, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 2, 200, 100, 10);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 1, 100, 100, 60);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 2, 200, 100, 70);
    drain(&bus, &seen);
    CHECK(seen.downs == 0 && seen.ups == 0, "a pinch leaves no taps behind it");

    /* ---- and neither finger can become a hold either ---- */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 100, 100, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 2, 200, 100, 10);
    ToriRS_TouchTick(&touch, &bus, TORIRS_TOUCH_HOLD_MS + 100);
    drain(&bus, &seen);
    CHECK(seen.downs == 0, "two fingers resting are not a long press");

    /* ---- fingers apart is a zoom ---- */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 300, 200, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 2, 400, 200, 5);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 2, 400, 200, 10); /* baseline */
    drain(&bus, &seen);
    ToriRS_TouchEvent(
        &touch, &bus, TORIRS_TOUCH_MOVED, 2, 400 + TORIRS_TOUCH_PINCH_STEP + 8, 200, 40);
    drain(&bus, &seen);
    CHECK(seen.wheels == 1, "spreading two fingers turns the wheel once");
    CHECK(seen.last_wheel < 0, "and spreading zooms the one way");

    /* ---- two fingers travelling together turn the camera ---- */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 300, 200, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 2, 360, 200, 5);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 2, 360, 200, 10); /* baseline */
    drain(&bus, &seen);
    /* both fingers slide right by the same amount: the spread is unchanged, so
     * this is a pan and not a pinch */
    ToriRS_TouchEvent(
        &touch, &bus, TORIRS_TOUCH_MOVED, 1, 300 + (TORIRS_TOUCH_PAN_STEP * 2), 200, 40);
    ToriRS_TouchEvent(
        &touch, &bus, TORIRS_TOUCH_MOVED, 2, 360 + (TORIRS_TOUCH_PAN_STEP * 2), 200, 45);
    drain(&bus, &seen);
    CHECK(seen.wheels == 0, "a pan that keeps its spread does not zoom");
    CHECK(seen.key_downs == 1, "it holds an arrow key instead");
    CHECK(seen.last_key == TORIRSK_LEFT, "dragging right turns the camera left");
    /* and lifting a finger lets the key go, rather than latching it forever */
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 2, 0, 0, 90);
    drain(&bus, &seen);
    CHECK(seen.key_ups == 1, "and lifting releases it");

    /* ---- a drag on the world turns the camera: the MIDDLE button ---- */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchSetViewport(&touch, 0, 0, 500, 300);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 300, 200, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 300 + TORIRS_TOUCH_SLOP + 4, 200, 40);
    drain(&bus, &seen);
    CHECK(seen.downs == 1, "a drag presses a button once it passes the slop");
    CHECK(seen.last_button == TORIRSM_MIDDLE, "and on the world that button is the middle one");

    /*
     * ---- a drag on a WINDOW drawn over the world is the window's ----
     *
     * The panel, the developer chrome and the plugin window are all drawn
     * inside the viewport's rectangle. Read by the rectangle alone this is the
     * camera; the camera then refuses it (its own gate asks whether the chrome
     * owns the pointer) and the finger drives nothing at all, which is what
     * made the plugin panel's scrollbar impossible to drag by touch.
     */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchSetViewport(&touch, 0, 0, 500, 300);
    ToriRS_TouchSetOverlayTest(&touch, overlay_top_left, NULL);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 40, 50, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 40, 50 + TORIRS_TOUCH_SLOP + 4, 40);
    drain(&bus, &seen);
    CHECK(seen.downs == 1, "a drag on a window over the world still presses");
    CHECK(seen.last_button == TORIRSM_LEFT, "and that button is the left one");

    /* and the window is not the whole viewport: outside it the camera is back */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchSetViewport(&touch, 0, 0, 500, 300);
    ToriRS_TouchSetOverlayTest(&touch, overlay_top_left, NULL);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 300, 200, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 300, 200 + TORIRS_TOUCH_SLOP + 4, 40);
    drain(&bus, &seen);
    CHECK(seen.last_button == TORIRSM_MIDDLE, "a drag beside the window is still the camera");

    /* the press lands where the finger LANDED, not where it crossed the slop:
     * a scrollbar grip is grabbed by the point it was touched */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchSetViewport(&touch, 0, 0, 500, 300);
    ToriRS_TouchSetOverlayTest(&touch, overlay_top_left, NULL);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 40, 50, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 40, 50 + TORIRS_TOUCH_SLOP + 4, 40);
    {
        struct ToriRS_CmdHeader header;
        uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
        int press_x = -1;
        int press_y = -1;

        while( CmdBus_Pop(&bus, &header, payload) )
        {
            if( header.type == TORIRS_CMD_INPUT_MOUSE_DOWN )
            {
                struct ToriRS_CmdMouseButton btn;

                memcpy(&btn, payload, sizeof(btn));
                press_x = btn.x;
                press_y = btn.y;
            }
        }
        CHECK(press_x == 40 && press_y == 50, "the press is at the point the finger landed on");
    }

    /*
     * ---- the pointer leaves with the finger, but not in the tap's own frame
     *
     * A mouse leaves a cursor behind and everything hover means keeps
     * describing that spot; a finger leaves nothing, so the client is told the
     * position went stale. The DELAY is the load-bearing half: the tap is
     * dispatched against the previous frame's world pick, so a leave arriving
     * in the same batch as the click would take the "Walk here" row and the
     * target out from under the click it belongs to.
     */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 100, 80, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 1, 100, 80, 90);
    drain(&bus, &seen);
    CHECK(seen.downs == 1 && seen.leaves == 0, "the tap's own frame carries no leave");
    ToriRS_TouchTick(&touch, &bus, 100);
    drain(&bus, &seen);
    CHECK(seen.leaves == 0, "nor the frame the click is still being dispatched in");
    ToriRS_TouchTick(&touch, &bus, 116);
    drain(&bus, &seen);
    CHECK(seen.leaves == 1, "then the pointer is gone");
    ToriRS_TouchTick(&touch, &bus, 132);
    ToriRS_TouchTick(&touch, &bus, 148);
    drain(&bus, &seen);
    CHECK(seen.leaves == 0, "and it is said once, not every idle frame");

    /* a finger back down before the leave was ever sent cancels it: the
     * pointer is there again, and saying it left would blank the hover the
     * new touch just established */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 100, 80, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 1, 100, 80, 90);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 2, 120, 90, 100);
    ToriRS_TouchTick(&touch, &bus, 116);
    ToriRS_TouchTick(&touch, &bus, 132);
    drain(&bus, &seen);
    CHECK(seen.leaves == 0, "a finger back down cancels the pending leave");

    /* a drag ends the same way -- the finger that was holding the camera is
     * gone too, and its last position is no more a hover than a tap's */
    CmdBus_Init(&bus);
    ToriRS_TouchReset(&touch);
    ToriRS_TouchSetViewport(&touch, 0, 0, 500, 300);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_BEGAN, 1, 300, 200, 0);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_MOVED, 1, 300, 200 + TORIRS_TOUCH_SLOP + 4, 40);
    ToriRS_TouchEvent(&touch, &bus, TORIRS_TOUCH_ENDED, 1, 300, 200 + TORIRS_TOUCH_SLOP + 4, 80);
    ToriRS_TouchTick(&touch, &bus, 96);
    ToriRS_TouchTick(&touch, &bus, 112);
    drain(&bus, &seen);
    CHECK(seen.leaves == 1, "a drag's finger leaves too");

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
