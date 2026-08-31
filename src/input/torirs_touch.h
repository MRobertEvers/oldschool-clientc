#ifndef TORIRS_TOUCH_H
#define TORIRS_TOUCH_H

#include <stdint.h>

struct ToriRS_CmdBus;

/*
 * Fingers, turned into the input this client already understands.
 *
 * ## Why the policy lives here and not in the backends
 *
 * Two things deliver touches -- SDL2 (macOS, Linux and the web) and the Win32
 * window (Windows) -- and they agree about nothing except that a finger has an
 * id, a position and three things it can be doing. Everything that MATTERS is
 * the part after that: how long a press has to be held before it is a
 * right-click, how far it may wander first and still count as a tap, whether
 * two fingers are a zoom or a camera turn. Put that in each backend and it is
 * written twice, drifts once, and then a long press means one thing on a phone
 * and another in a browser.
 *
 * So the backends do the one job they cannot share -- getting a finger's
 * position into CANVAS pixels, which is a different calculation for a letterbox
 * blit than for a stretched DIB -- and hand it here. What comes out the far end
 * is mouse and key commands on the ordinary bus, which is why none of the
 * client above this knows a touch happened at all, and why a touch session is
 * recordable and replayable exactly like a mouse one.
 *
 * ## What the gestures are
 *
 *   tap          press and release inside the slop, before the hold time:
 *                a left click where the finger was.
 *   long press   held inside the slop past the hold time: a RIGHT click, which
 *                is the minimenu. It fires while the finger is still down --
 *                that is what makes it feel like a press rather than a delay --
 *                and the release that follows is swallowed.
 *   drag         moved beyond the slop: pointer moves and NO click. A finger
 *                that wanders is somebody scrolling or aiming, and the one
 *                thing it must not do is walk the player somewhere.
 *   pinch        two fingers, distance changing: the wheel, which is the zoom.
 *   two-finger   two fingers moving together: arrow keys, which is the camera.
 *                Held down while the pan continues and released with the
 *                fingers, so a long swing turns as far as it is swung.
 */

/** Fingers tracked at once. Two gestures need two; the rest are followed so
 *  that lifting one of three does not look like the start of a new tap. */
#define TORIRS_TOUCH_MAX 8

/** Held this long inside the slop and it is a right click. */
#define TORIRS_TOUCH_HOLD_MS 400

/** A tap may wander this far, in CANVAS pixels, and still be a tap. Generous
 *  because a finger is not a mouse: the contact patch rolls as it lifts. */
#define TORIRS_TOUCH_SLOP 12

/**
 * Pinch distance must change by this much, in CANVAS pixels, before it is a
 * zoom rather than two fingers resting unevenly.
 *
 * 16 and not 48. The threshold is doing two jobs -- rejecting a tremor, and
 * setting the gearing, because it is also the distance between notches -- and
 * at 48 the second one swamped the first: a pinch across half the screen was
 * seven notches, so the zoom arrived in visible steps and a small adjustment
 * arrived not at all. 16 is still well past the noise of two resting fingers
 * and gives three times the travel, which is what makes it feel continuous.
 */
#define TORIRS_TOUCH_PINCH_STEP 16

/** A two-finger pan must travel this far before it turns the camera. */
#define TORIRS_TOUCH_PAN_STEP 40

enum ToriRS_TouchPhase
{
    TORIRS_TOUCH_BEGAN,
    TORIRS_TOUCH_MOVED,
    TORIRS_TOUCH_ENDED
};

struct ToriRS_TouchFinger
{
    /** The backend's own id. -1 when the slot is free. */
    int64_t id;
    int start_x;
    int start_y;
    int x;
    int y;
    uint64_t began_ms;
    /** Wandered beyond the slop, so it can never become a tap or a hold. */
    int dragging;
    /** The long press already fired for this finger. */
    int held;
};

struct ToriRS_Touch
{
    struct ToriRS_TouchFinger finger[TORIRS_TOUCH_MAX];
    int count;
    /*
     * The world viewport, in CANVAS pixels, or w/h <= 0 when the caller has not
     * said. A one-finger drag that STARTED inside it turns the camera instead
     * of moving the pointer -- @see ToriRS_TouchSetViewport.
     */
    int view_x;
    int view_y;
    int view_w;
    int view_h;
    /** A one-finger camera drag is in progress; the middle button is held. */
    int cam_drag;
    /** Pinch/pan baselines, taken when the second finger lands. */
    int pinch_distance;
    int pan_x;
    int pan_y;
    /** The arrow key a two-finger pan is currently holding, or 0. */
    uint8_t pan_key;
    /** A gesture that is no longer a candidate for anything: set when a second
     *  finger joins, so lifting them does not emit two stray taps. */
    int multi;
};

void
ToriRS_TouchReset(struct ToriRS_Touch* touch);

/**
 * Where the 3D world is on the canvas, so a drag that begins there can turn the
 * camera rather than drag the pointer across the interface.
 *
 * Set every frame by whatever knows the viewport's box; w/h <= 0 disables the
 * gesture, which is what a client with no world on screen wants.
 *
 * WHY THE CAMERA DRAG IS A MIDDLE-BUTTON DRAG
 *
 * The desktop already turns the camera by dragging with the middle button
 * (app_world_camera_mouse), including all the parts that are easy to get wrong:
 * the revision's `[camera] controls=` gate, the follow-cam split, and the
 * screen-space sign convention that keeps free and orbit cameras agreeing. A
 * finger drag therefore SYNTHESISES that gesture rather than reimplementing it,
 * so both inputs are the same code path and cannot drift.
 */
void
ToriRS_TouchSetViewport(struct ToriRS_Touch* touch, int x, int y, int w, int h);

/**
 * One finger event, in CANVAS pixels.
 *
 * `id` is the backend's, and only has to be stable from BEGAN to ENDED for one
 * finger; SDL's is a device-wide handle and Win32's is a per-contact word, and
 * neither is compared against anything but the ids already here.
 */
void
ToriRS_TouchEvent(
    struct ToriRS_Touch* touch,
    struct ToriRS_CmdBus* bus,
    enum ToriRS_TouchPhase phase,
    int64_t id,
    int canvas_x,
    int canvas_y,
    uint64_t now_ms);

/**
 * Give the hold timer a chance to fire.
 *
 * A long press has to become a right click while the finger is still STILL,
 * and a finger that is not moving generates no events -- so without this the
 * menu would only ever open on the next twitch. Call once a frame.
 */
void
ToriRS_TouchTick(
    struct ToriRS_Touch* touch,
    struct ToriRS_CmdBus* bus,
    uint64_t now_ms);

#endif
