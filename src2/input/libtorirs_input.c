#include "libtorirs_input.h"

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
    if( !input )
        return NULL;
    memset(input, 0, sizeof(struct LibToriRS_Input));
    return input;
}

struct LibToriRS_Input*
LibToriRS_Input_Init(
    struct LibToriRS_Input* input,
    uint64_t time)
{
    if( !input )
        return NULL;
    memset(input, 0, sizeof(struct LibToriRS_Input));
    input->curr.time = time;
    input->prev.time = time;
    input->double_click_threshold_ms = 400;
    input->drag_deadzone_pixels = 5;
    return input;
}

void
LibToriRS_Input_Free(struct LibToriRS_Input* input)
{
    if( !input )
        return;
    free(input);
    input = NULL;
}

void
LibToriRS_Input_Begin(
    struct LibToriRS_Input* input,
    uint64_t time)
{
    if( !input )
        return;
    input->prev = input->curr;
    input->curr = (struct LibToriRS_Input_State){ 0 };

    input->curr.time = time;
    input->curr.mouse_x = input->prev.mouse_x;
    input->curr.mouse_y = input->prev.mouse_y;

    clear_gesture_oneshots(input);
}

void
LibToriRS_Input_End(struct LibToriRS_Input* input)
{
    if( !input )
        return;

    input->mouse_dx = input->curr.mouse_x - input->prev.mouse_x;
    input->mouse_dy = input->curr.mouse_y - input->prev.mouse_y;

    for( int b = TORIRSM_LEFT; b < TORIRSM_COUNT; b++ )
        input->is_dragging[b] = (input->mouse_button_held[b] && input->drag_active[b]) ? 1 : 0;
}

void
LibToriRS_Input_PushKeyDown(
    struct LibToriRS_Input* input,
    enum LibToriRS_KeyCode key)
{
    if( !input )
        return;
    input->curr.key_down[key] = 1;
}

void
LibToriRS_Input_PushKeyUp(
    struct LibToriRS_Input* input,
    enum LibToriRS_KeyCode key)
{
    if( !input )
        return;
    input->curr.key_up[key] = 1;
}

void
LibToriRS_Input_PushMouseDown(
    struct LibToriRS_Input* input,
    enum LibToriRS_MouseButton button,
    int x,
    int y)
{
    if( !input || button <= TORIRSM_UNKNOWN || button >= TORIRSM_COUNT )
        return;
    input->curr.mouse_x = x;
    input->curr.mouse_y = y;
    input->curr.mouse_button_down[button] = 1;
    input->mouse_button_held[button] = 1;
    input->press_origin_x[button] = x;
    input->press_origin_y[button] = y;
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
    if( !input || button <= TORIRSM_UNKNOWN || button >= TORIRSM_COUNT )
        return;
    input->curr.mouse_x = x;
    input->curr.mouse_y = y;
    input->curr.mouse_button_up[button] = 1;

    if( input->drag_active[button] )
        input->drag_end[button] = 1;
    else if( input->mouse_button_held[button] )
    {
        uint64_t prev_t = input->last_press_time[button];
        int prox_sq = deadzone_squared(input);
        int dist_prev_sq =
            distance_squared(x, y, input->last_click_x[button], input->last_click_y[button]);
        int is_double = 0;
        if( prev_t != 0 && (input->curr.time - prev_t) < input->double_click_threshold_ms &&
            dist_prev_sq <= prox_sq )
            is_double = 1;

        if( is_double )
        {
            input->is_double_click[button] = 1;
            input->is_click[button] = 0;
        }
        else
        {
            input->is_click[button] = 1;
            input->is_double_click[button] = 0;
        }
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
    if( !input )
        return;
    input->curr.mouse_x = x;
    input->curr.mouse_y = y;
    for( int b = TORIRSM_LEFT; b < TORIRSM_COUNT; b++ )
        try_start_drag(input, (enum LibToriRS_MouseButton)b);
}
