#include "input/torirs_input.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int
distance_squared(
    int ax,
    int ay,
    int bx,
    int by)
{
    long dx = (long)ax - (long)bx;
    long dy = (long)ay - (long)by;
    long sq = dx * dx + dy * dy;
    if( sq > (long)2147483647 )
        return 2147483647;
    return (int)sq;
}

static int
deadzone_squared(struct LibToriRS_Input* input)
{
    uint64_t d = input->drag_deadzone_pixels;
    return (int)(d * d);
}

static void
clear_gesture_oneshots(struct LibToriRS_Input* input)
{
    for( int b = 0; b < TORIRSM_COUNT; b++ )
    {
        input->is_click[b] = 0;
        input->is_double_click[b] = 0;
        input->drag_start[b] = 0;
        input->drag_end[b] = 0;
    }
}

static void
try_start_drag(
    struct LibToriRS_Input* input,
    enum LibToriRS_MouseButton button)
{
    if( button <= TORIRSM_UNKNOWN || button >= TORIRSM_COUNT )
        return;
    if( !input->mouse_button_held[button] || input->drag_active[button] )
        return;
    uint64_t const elapsed_ms = input->curr.time - input->press_origin_time[button];
    if( input->drag_dead_time_ms > 0 && elapsed_ms < input->drag_dead_time_ms )
        return;
    int dsq = distance_squared(
        input->curr.mouse_x,
        input->curr.mouse_y,
        input->press_origin_x[button],
        input->press_origin_y[button]);
    if( dsq > deadzone_squared(input) )
    {
        input->drag_active[button] = 1;
        input->drag_start[button] = 1;
    }
}

struct LibToriRS_Input*
LibToriRS_Input_New(void)
{
    struct LibToriRS_Input* input = malloc(sizeof(struct LibToriRS_Input));
    assert(input);
    memset(input, 0, sizeof(struct LibToriRS_Input));
    return input;
}

struct LibToriRS_Input*
LibToriRS_Input_Init(
    struct LibToriRS_Input* input,
    uint64_t time)
{
    assert(input);
    memset(input, 0, sizeof(struct LibToriRS_Input));
    input->curr.time = time;
    input->prev.time = time;
    input->double_click_threshold_ms = 400;
    input->drag_deadzone_pixels = LIBTORIRS_INPUT_DEFAULT_DRAG_DEADZONE_PX;
    input->drag_dead_time_ms = 0;
    return input;
}

void
LibToriRS_Input_SetDragThresholds(
    struct LibToriRS_Input* input,
    uint64_t zone_pixels,
    uint64_t time_ms)
{
    assert(input);
    input->drag_deadzone_pixels = zone_pixels;
    input->drag_dead_time_ms = time_ms;
}

void
LibToriRS_Input_Free(struct LibToriRS_Input* input)
{
    if( !input )
        return;
    free(input);
}

void
LibToriRS_Input_Begin(
    struct LibToriRS_Input* input,
    uint64_t time)
{
    assert(input);
    input->prev = input->curr;
    input->curr = (struct LibToriRS_Input_State){ 0 };

    input->curr.time = time;
    input->curr.mouse_x = input->prev.mouse_x;
    input->curr.mouse_y = input->prev.mouse_y;

    clear_gesture_oneshots(input);

    input->key_event_count = 0;
    memset(input->osrs_key_pressed, 0, sizeof(input->osrs_key_pressed));
    memset(input->osrs_key_released, 0, sizeof(input->osrs_key_released));
}

void
LibToriRS_Input_Continue(
    struct LibToriRS_Input* input,
    uint64_t time)
{
    assert(input);
    /* App_RunOnce has not reached interaction yet. Preserve curr, gesture
     * one-shots, and queued keys; clearing them would turn a settlement wait
     * into a lost click/key. New platform events may still be appended before
     * End() on this host turn. */
    input->curr.time = time;
}

void
LibToriRS_Input_End(struct LibToriRS_Input* input)
{
    assert(input);

    input->mouse_dx = input->curr.mouse_x - input->prev.mouse_x;
    input->mouse_dy = input->curr.mouse_y - input->prev.mouse_y;

    for( int b = TORIRSM_LEFT; b < TORIRSM_COUNT; b++ )
        input->is_dragging[b] = (input->mouse_button_held[b] && input->drag_active[b]) ? 1 : 0;

    for( int k = TORIRSK_UNKNOWN; k < TORIRSK_COUNT; k++ )
    {
        if( input->curr.key_up[k] )
            input->key_held[k] = 0;
        else if( input->curr.key_down[k] || input->key_held[k] )
            input->key_held[k] = 1;
    }
}

void
LibToriRS_Input_PushKeyDown(
    struct LibToriRS_Input* input,
    enum LibToriRS_KeyCode key)
{
    assert(input);
    input->curr.key_down[key] = 1;
}

void
LibToriRS_Input_PushKeyUp(
    struct LibToriRS_Input* input,
    enum LibToriRS_KeyCode key)
{
    assert(input);
    input->curr.key_up[key] = 1;
}

void
LibToriRS_Input_PushKeyEvent(
    struct LibToriRS_Input* input,
    int key_typed,
    int key_pressed,
    int is_repeat)
{
    assert(input);
    if( input->key_event_count >= LIBTORIRS_KEY_EVENT_MAX )
        return;
    input->key_events[input->key_event_count].key_typed = key_typed;
    input->key_events[input->key_event_count].key_pressed = key_pressed;
    input->key_events[input->key_event_count].is_repeat = is_repeat ? 1 : 0;
    input->key_event_count++;
}

void
LibToriRS_Input_SetOsrsKeyState(
    struct LibToriRS_Input* input,
    int osrs_key,
    int down,
    int pressed_edge)
{
    assert(input);
    if( osrs_key < 0 || osrs_key >= TORIRS_OSRSKEY_COUNT )
        return;
    if( !down && input->osrs_key_held[osrs_key] )
        input->osrs_key_released[osrs_key] = 1;
    input->osrs_key_held[osrs_key] = down ? 1 : 0;
    if( down && pressed_edge )
        input->osrs_key_pressed[osrs_key] = 1;
}

void
LibToriRS_Input_ClearKeys(struct LibToriRS_Input* input)
{
    assert(input);
    input->key_event_count = 0;
    memset(input->key_held, 0, sizeof(input->key_held));
    memset(input->curr.key_down, 0, sizeof(input->curr.key_down));
    memset(input->curr.key_up, 0, sizeof(input->curr.key_up));
    memset(input->osrs_key_held, 0, sizeof(input->osrs_key_held));
    memset(input->osrs_key_pressed, 0, sizeof(input->osrs_key_pressed));
    /* Focus loss is not a key-up the scripts should see: the reference stops
     * receiving events entirely, so drop the release edges too rather than
     * synthesising a burst of on_key_up dispatches. */
    memset(input->osrs_key_released, 0, sizeof(input->osrs_key_released));
}

void
LibToriRS_Input_PushMouseDown(
    struct LibToriRS_Input* input,
    enum LibToriRS_MouseButton button,
    int x,
    int y)
{
    assert(input);
    if( button <= TORIRSM_UNKNOWN || button >= TORIRSM_COUNT )
        return;
    input->curr.mouse_x = x;
    input->curr.mouse_y = y;
    input->curr.mouse_button_down[button] = 1;
    input->mouse_button_held[button] = 1;
    input->press_origin_x[button] = x;
    input->press_origin_y[button] = y;
    input->press_origin_time[button] = input->curr.time;
    input->drag_active[button] = 0;
    try_start_drag(input, button);
}

void
LibToriRS_Input_PushMouseUp(
    struct LibToriRS_Input* input,
    enum LibToriRS_MouseButton button,
    int x,
    int y)
{
    assert(input);
    if( button <= TORIRSM_UNKNOWN || button >= TORIRSM_COUNT )
        return;
    input->curr.mouse_x = x;
    input->curr.mouse_y = y;
    input->curr.mouse_button_up[button] = 1;

    if( input->mouse_button_held[button] )
    {
        uint64_t prev_t = input->last_press_time[button];
        int prox_sq = deadzone_squared(input);
        int dist_prev_sq =
            distance_squared(x, y, input->last_click_x[button], input->last_click_y[button]);
        int is_double = 0;
        if( prev_t != 0 && (input->curr.time - prev_t) < input->double_click_threshold_ms &&
            dist_prev_sq <= prox_sq )
            is_double = 1;

        if( input->drag_active[button] )
            input->drag_end[button] = 1;

        /* EVERY release of a held button is a click, exactly like the reference
         * (GameShell sets nextMouseClickButton on every press and the mainloop
         * consumes it unconditionally — no debounce, and nothing between press
         * and release can cancel it).
         *
         * Two things used to suppress is_click here and both were torirs-only
         * divergences that read to a player as "the click did nothing":
         *
         *  - is_double_click. Removed earlier (§29a): it ate every other click
         *    of a rapid double-click. Still computed, still informational, still
         *    must NOT gate is_click.
         *  - drag_active, i.e. this layer's 5px pointer deadzone. A release that
         *    had drifted more than 5px from the press became drag_end *instead
         *    of* a click, and left_click_miss (walk-here / world default op) is
         *    gated on is_click — so a click made while the hand was still moving
         *    silently did nothing, and only clicking from a dead stop worked.
         *    The deadzone is not a click policy: its only consumer is
         *    bridge_input_to_uitree's fallback UP delivery, and the UI drag
         *    machine runs on its own thresholds (see interact_drag). So report
         *    both — drag_end for the gesture, is_click for the click — and let
         *    UITree_InteractFrame decide whether a real drag consumed it. */
        input->is_click[button] = 1;
        input->is_double_click[button] = is_double;
        input->last_press_time[button] = input->curr.time;
        input->last_click_x[button] = x;
        input->last_click_y[button] = y;
    }

    input->mouse_button_held[button] = 0;
    input->drag_active[button] = 0;
}

void
LibToriRS_Input_PushMouseMove(
    struct LibToriRS_Input* input,
    int x,
    int y)
{
    assert(input);
    input->curr.mouse_x = x;
    input->curr.mouse_y = y;
    for( int b = TORIRSM_LEFT; b < TORIRSM_COUNT; b++ )
        try_start_drag(input, (enum LibToriRS_MouseButton)b);
}

void
LibToriRS_Input_PushMouseWheel(
    struct LibToriRS_Input* input,
    int wheel_y)
{
    assert(input);
    input->curr.mouse_wheel_y += wheel_y;
}
