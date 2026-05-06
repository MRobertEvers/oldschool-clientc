#include "ginput.h"

#include <assert.h>

static void
push_event(
    struct GInput* input,
    struct GameInputEvent* event)
{
    assert(input->event_count < GAME_INPUT_MAX_EVENTS);
    input->events[input->event_count] = *event;
    input->event_count++;
}

static void
on_mousedown(
    struct GInput* input,
    struct GameInputEvent* ev)
{
    push_event(input, ev);
    int btn = ev->mouse_down.button;
    int mx  = ev->mouse_down.mouse_x;
    int my  = ev->mouse_down.mouse_y;

    input->raw_mouse.cursor_x       = mx;
    input->raw_mouse.cursor_y       = my;

    input->raw_mouse.buttons[btn].press_x         = mx;
    input->raw_mouse.buttons[btn].press_y         = my;
    input->raw_mouse.buttons[btn].press_timestamp = input->time;
    input->raw_mouse.buttons[btn].down            = true;
    input->raw_mouse.buttons[btn].press_this_frame = true;
}

static void
on_mouseup(
    struct GInput* input,
    struct GameInputEvent* ev)
{
    push_event(input, ev);
    int btn = ev->mouse_up.button;
    int mx  = ev->mouse_up.mouse_x;
    int my  = ev->mouse_up.mouse_y;

    input->raw_mouse.cursor_x = mx;
    input->raw_mouse.cursor_y = my;

    input->raw_mouse.buttons[btn].release_x         = mx;
    input->raw_mouse.buttons[btn].release_y         = my;
    input->raw_mouse.buttons[btn].release_this_frame = true;

    if( input->raw_mouse.buttons[btn].down )
    {
        input->raw_mouse.buttons[btn].click_this_frame  = true;
        input->raw_mouse.buttons[btn].click_press_x    = input->raw_mouse.buttons[btn].press_x;
        input->raw_mouse.buttons[btn].click_press_y    = input->raw_mouse.buttons[btn].press_y;
        input->raw_mouse.buttons[btn].click_release_x  = mx;
        input->raw_mouse.buttons[btn].click_release_y  = my;

        /* Synthetic CLICK event for any queue-based consumers. */
        struct GameInputEvent synthetic;
        synthetic.type               = TORIRSEV2_CLICK;
        synthetic.click.button       = btn;
        synthetic.click.start_mouse_x = input->raw_mouse.buttons[btn].press_x;
        synthetic.click.start_mouse_y = input->raw_mouse.buttons[btn].press_y;
        synthetic.click.end_mouse_x  = mx;
        synthetic.click.end_mouse_y  = my;
        push_event(input, &synthetic);
    }

    input->raw_mouse.buttons[btn].down            = false;
    input->raw_mouse.buttons[btn].press_timestamp = 0.0;
}

static void
on_mousemove(
    struct GInput* input,
    struct GameInputEvent* ev)
{
    push_event(input, ev);
    input->raw_mouse.cursor_x = ev->mouse_move.x;
    input->raw_mouse.cursor_y = ev->mouse_move.y;
}

static void
on_mousewheel(
    struct GInput* input,
    struct GameInputEvent* ev)
{
    push_event(input, ev);
    /* Accumulate wheel delta; cursor is updated to the wheel event position. */
    input->raw_mouse.wheel_delta += ev->mouse_wheel.wheel;
    input->raw_mouse.cursor_x    = ev->mouse_wheel.mouse_x;
    input->raw_mouse.cursor_y    = ev->mouse_wheel.mouse_y;
}

static void
on_keydown(
    struct GInput* input,
    struct GameInputEvent* ev)
{
    push_event(input, ev);
    struct GameInputEvent_KeyDown* event = &ev->key_down;
    input->key_states[event->key].pressed        = true;
    input->key_states[event->key].down           = true;
    input->key_states[event->key].down_timestamp = input->time;
}

static void
on_keyup(
    struct GInput* input,
    struct GameInputEvent* ev)
{
    push_event(input, ev);
    struct GameInputEvent_KeyUp* event = &ev->key_up;
    input->key_states[event->key].pressed        = false;
    input->key_states[event->key].down           = false;
    input->key_states[event->key].down_timestamp = 0.0f;
}

void
game_input_process_events(struct GInput* input)
{
    /* Clear per-frame flags before processing new events. */
    for( int i = 0; i < TORIRSM_COUNT; i++ )
    {
        input->raw_mouse.buttons[i].press_this_frame   = false;
        input->raw_mouse.buttons[i].release_this_frame = false;
        input->raw_mouse.buttons[i].click_this_frame   = false;
    }
    input->raw_mouse.wheel_delta = 0;
    for( int i = 0; i < TORIRSK_COUNT; i++ )
        input->key_states[i].pressed = false;

    for( int i = 0; i < input->in_event_count; i++ )
    {
        switch( input->in_events[i].type )
        {
        case TORIRSEV_KEY_DOWN:
            on_keydown(input, &input->in_events[i]);
            break;
        case TORIRSEV_KEY_UP:
            on_keyup(input, &input->in_events[i]);
            break;
        case TORIRSEV_MOUSE_DOWN:
            on_mousedown(input, &input->in_events[i]);
            break;
        case TORIRSEV_MOUSE_UP:
            on_mouseup(input, &input->in_events[i]);
            break;
        case TORIRSEV_MOUSE_MOVE:
            on_mousemove(input, &input->in_events[i]);
            break;
        case TORIRSEV_MOUSE_WHEEL:
            on_mousewheel(input, &input->in_events[i]);
            break;
        default:
            break;
        }
    }
}

bool
game_input_keydown_or_pressed(
    struct GInput* input,
    enum GameInputKeyCode key)
{
    if( input->key_states[key].down )
        return true;
    if( input->key_states[key].pressed )
        return true;
    return false;
}

void
game_input_frame_reset(struct GInput* input)
{
    input->event_count    = 0;
    input->in_event_count = 0;
}
