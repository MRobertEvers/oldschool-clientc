#include "input/torirs_touch.h"

#include "cmd/cmdbus.h"
#include "input/torirs_input.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static struct ToriRS_TouchFinger*
touch_find(struct ToriRS_Touch* touch, int64_t id)
{
    assert(touch);
    for( int i = 0; i < TORIRS_TOUCH_MAX; i++ )
        if( touch->finger[i].id == id )
            return &touch->finger[i];
    return NULL;
}

static int
touch_far(struct ToriRS_TouchFinger const* finger)
{
    int dx;
    int dy;

    assert(finger);
    dx = finger->x - finger->start_x;
    dy = finger->y - finger->start_y;
    return (dx * dx) + (dy * dy) > TORIRS_TOUCH_SLOP * TORIRS_TOUCH_SLOP;
}

/** Distance between the two live fingers, and their midpoint. -1 when there
 *  are not two. */
static int
touch_spread(struct ToriRS_Touch const* touch, int* out_mid_x, int* out_mid_y)
{
    struct ToriRS_TouchFinger const* first = NULL;
    struct ToriRS_TouchFinger const* second = NULL;
    long dx;
    long dy;
    long square;
    int root = 0;

    assert(touch);
    for( int i = 0; i < TORIRS_TOUCH_MAX; i++ )
    {
        if( touch->finger[i].id < 0 )
            continue;
        if( !first )
            first = &touch->finger[i];
        else if( !second )
            second = &touch->finger[i];
    }
    if( !first || !second )
        return -1;

    if( out_mid_x )
        *out_mid_x = (first->x + second->x) / 2;
    if( out_mid_y )
        *out_mid_y = (first->y + second->y) / 2;

    dx = first->x - second->x;
    dy = first->y - second->y;
    square = (dx * dx) + (dy * dy);
    /* Integer square root by walking: no libm on this path, and the answer is
     * only ever compared against a threshold. */
    while( (long)(root + 1) * (root + 1) <= square )
        root++;
    return root;
}

static void
touch_click(struct ToriRS_CmdBus* bus, int button, int x, int y)
{
    assert(bus);
    /* The move first, and it is not decoration: the client tracks hover from
     * pointer motion, so a button arriving somewhere the pointer has never
     * been is a click on whatever was under the LAST place it was. */
    CmdBus_PushMouseMove(bus, (int16_t)x, (int16_t)y);
    CmdBus_PushMouseButton(
        bus, TORIRS_CMD_INPUT_MOUSE_DOWN, (uint8_t)button, (int16_t)x, (int16_t)y);
    CmdBus_PushMouseButton(
        bus, TORIRS_CMD_INPUT_MOUSE_UP, (uint8_t)button, (int16_t)x, (int16_t)y);
}

static void
touch_release_pan(struct ToriRS_Touch* touch, struct ToriRS_CmdBus* bus)
{
    assert(touch);
    assert(bus);
    if( !touch->pan_key )
        return;
    CmdBus_PushKey(bus, TORIRS_CMD_INPUT_KEY_UP, touch->pan_key);
    touch->pan_key = 0;
}

/** Hold the arrow this pan is asking for, releasing whichever was held. */
static void
touch_hold_pan(struct ToriRS_Touch* touch, struct ToriRS_CmdBus* bus, uint8_t key)
{
    assert(touch);
    assert(bus);
    if( touch->pan_key == key )
        return;
    touch_release_pan(touch, bus);
    if( !key )
        return;
    touch->pan_key = key;
    CmdBus_PushKey(bus, TORIRS_CMD_INPUT_KEY_DOWN, key);
}

void
ToriRS_TouchReset(struct ToriRS_Touch* touch)
{
    assert(touch);
    memset(touch, 0, sizeof(*touch));
    for( int i = 0; i < TORIRS_TOUCH_MAX; i++ )
        touch->finger[i].id = -1;
    touch->pinch_distance = -1;
}

/** The pinch and the pan, from whatever the two live fingers are doing. */
static void
touch_two_finger(struct ToriRS_Touch* touch, struct ToriRS_CmdBus* bus)
{
    int mid_x = 0;
    int mid_y = 0;
    int spread;

    assert(touch);
    assert(bus);

    spread = touch_spread(touch, &mid_x, &mid_y);
    if( spread < 0 )
        return;
    if( touch->pinch_distance < 0 )
    {
        touch->pinch_distance = spread;
        touch->pan_x = mid_x;
        touch->pan_y = mid_y;
        return;
    }

    /*
     * Pinch first, and only ONE of the two per pass.
     *
     * Two fingers moving apart also move their midpoint, so a pinch read as a
     * pan as well would zoom and swing the camera at once. Distance is the more
     * deliberate of the two, so it wins the pass.
     */
    if( abs(spread - touch->pinch_distance) >= TORIRS_TOUCH_PINCH_STEP )
    {
        CmdBus_PushMouseWheel(bus, spread > touch->pinch_distance ? (int16_t)-1 : (int16_t)1);
        touch->pinch_distance = spread;
        touch->pan_x = mid_x;
        touch->pan_y = mid_y;
        return;
    }

    {
        int const dx = mid_x - touch->pan_x;
        int const dy = mid_y - touch->pan_y;

        if( abs(dx) >= TORIRS_TOUCH_PAN_STEP && abs(dx) >= abs(dy) )
        {
            /* Dragged right turns the camera LEFT, the way a hand on a globe
             * moves the globe and not the eye. */
            touch_hold_pan(touch, bus, (uint8_t)(dx > 0 ? TORIRSK_LEFT : TORIRSK_RIGHT));
            touch->pan_x = mid_x;
            touch->pan_y = mid_y;
        }
        else if( abs(dy) >= TORIRS_TOUCH_PAN_STEP )
        {
            touch_hold_pan(touch, bus, (uint8_t)(dy > 0 ? TORIRSK_UP : TORIRSK_DOWN));
            touch->pan_x = mid_x;
            touch->pan_y = mid_y;
        }
    }
}

void
ToriRS_TouchEvent(
    struct ToriRS_Touch* touch,
    struct ToriRS_CmdBus* bus,
    enum ToriRS_TouchPhase phase,
    int64_t id,
    int canvas_x,
    int canvas_y,
    uint64_t now_ms)
{
    struct ToriRS_TouchFinger* finger;

    assert(touch);
    assert(bus);

    if( phase == TORIRS_TOUCH_BEGAN )
    {
        /*
         * A backend that begins an id it never ended is describing a driver,
         * not making a mistake -- a finger lost to a focus change never sends
         * its up. Reuse the slot rather than leaking it.
         */
        finger = touch_find(touch, id);
        if( !finger )
        {
            finger = touch_find(touch, -1);
            if( !finger )
                return; /* more fingers than we follow: ignore the extras */
            touch->count++;
        }
        finger->id = id;
        finger->start_x = canvas_x;
        finger->start_y = canvas_y;
        finger->x = canvas_x;
        finger->y = canvas_y;
        finger->began_ms = now_ms;
        finger->dragging = 0;
        finger->held = 0;
        if( touch->count >= 2 )
        {
            /*
             * A second finger ends the first one's candidacy for a tap.
             *
             * Otherwise a pinch leaves two taps behind when the fingers come
             * up, and on a frame where the world fills the screen those are two
             * clicks into the scene -- the player walks off in the middle of a
             * zoom.
             */
            touch->multi = 1;
            touch->pinch_distance = -1;
            for( int i = 0; i < TORIRS_TOUCH_MAX; i++ )
                touch->finger[i].dragging = 1;
        }
        else
        {
            /* Hover follows the finger down, so the client knows what is under
             * it before anything is pressed. */
            CmdBus_PushMouseMove(bus, (int16_t)canvas_x, (int16_t)canvas_y);
        }
        return;
    }

    finger = touch_find(touch, id);
    if( !finger )
        return;

    finger->x = canvas_x;
    finger->y = canvas_y;

    if( phase == TORIRS_TOUCH_MOVED )
    {
        if( touch->count >= 2 )
        {
            touch_two_finger(touch, bus);
            return;
        }
        if( !finger->dragging && touch_far(finger) )
            finger->dragging = 1;
        /* The pointer follows the finger whether or not this became a drag:
         * what a drag withholds is the CLICK, not the position. */
        CmdBus_PushMouseMove(bus, (int16_t)canvas_x, (int16_t)canvas_y);
        return;
    }

    /* ENDED */
    if( !touch->multi && !finger->dragging && !finger->held )
        touch_click(bus, TORIRSM_LEFT, canvas_x, canvas_y);

    finger->id = -1;
    if( touch->count > 0 )
        touch->count--;
    if( touch->count < 2 )
    {
        touch_release_pan(touch, bus);
        touch->pinch_distance = -1;
    }
    if( touch->count == 0 )
        touch->multi = 0;
}

void
ToriRS_TouchTick(
    struct ToriRS_Touch* touch,
    struct ToriRS_CmdBus* bus,
    uint64_t now_ms)
{
    assert(touch);
    assert(bus);

    if( touch->count != 1 || touch->multi )
        return;
    for( int i = 0; i < TORIRS_TOUCH_MAX; i++ )
    {
        struct ToriRS_TouchFinger* finger = &touch->finger[i];

        if( finger->id < 0 || finger->held || finger->dragging )
            continue;
        if( now_ms - finger->began_ms < TORIRS_TOUCH_HOLD_MS )
            continue;
        /* Fired while the finger is still down -- that is what makes a long
         * press feel like a press and not a slow tap. The release that follows
         * is swallowed by the `held` flag. */
        finger->held = 1;
        touch_click(bus, TORIRSM_RIGHT, finger->x, finger->y);
    }
}
