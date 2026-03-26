#include "ginput.h"

#include <assert.h>

static void
push_event(
    struct GInput* input,
    struct GameInputEvent* event,
    enum GameInputEventType type)
{
    assert(input->event_count < GAME_INPUT_MAX_EVENTS);
    input->events[input->event_count] = *event;
    input->event_count++;
}

static void
on_mousedown(
    struct GInput* input,
    struct GameInputEvent_MouseDown* event)
{
    push_event(input, &event, TORIRSEV_MOUSE_DOWN);
    input->mouse_button_states[event->button].pressed = true;
    input->mouse_button_states[event->button].down_timestamp = input->time;
    input->mouse_button_states[event->button].down = true;
    input->mouse_button_states[event->button].x = event->mouse_x;
    input->mouse_button_states[event->button].y = event->mouse_y;
}

static void
on_mouseup(
    struct GInput* input,
    struct GameInputEvent_MouseUp* event)
{
    push_event(input, &event, TORIRSEV_MOUSE_UP);

    struct GameInputEvent synthetic_event;
    if( input->mouse_button_states[event->button].down )
    {
        synthetic_event.type = TORIRSEV2_CLICK;
        synthetic_event.click.button = event->button;
        synthetic_event.click.start_mouse_x = input->mouse_button_states[event->button].x;
        synthetic_event.click.start_mouse_y = input->mouse_button_states[event->button].y;
        synthetic_event.click.end_mouse_x = event->mouse_x;
        synthetic_event.click.end_mouse_y = event->mouse_y;
        push_event(input, &synthetic_event, TORIRSEV2_CLICK);
    }

    input->mouse_button_states[event->button].down = false;
    input->mouse_button_states[event->button].down_timestamp = 0.0f;
    input->mouse_button_states[event->button].x = 0;
    input->mouse_button_states[event->button].y = 0;
}

static void
on_mousemove(
    struct GInput* input,
    struct GameInputEvent_MouseMove* event)
{
    push_event(input, &event, TORIRSEV_MOUSE_MOVE);
    input->mouse_state.x = event->x;
    input->mouse_state.y = event->y;
}

static void
on_mousewheel(
    struct GInput* input,
    struct GameInputEvent_MouseWheel* event)
{
    push_event(input, &event, TORIRSEV_MOUSE_WHEEL);
}

static void
on_keydown(
    struct GInput* input,
    struct GameInputEvent_KeyDown* event)
{
    push_event(input, &event, TORIRSEV_KEY_DOWN);
    input->key_states[event->key].pressed = true;
    input->key_states[event->key].down = true;
    input->key_states[event->key].down_timestamp = input->time;
}

static void
on_keyup(
    struct GInput* input,
    struct GameInputEvent_KeyUp* event)
{
    push_event(input, &event, TORIRSEV_KEY_UP);
    input->key_states[event->key].pressed = false;
    input->key_states[event->key].down = false;
    input->key_states[event->key].down_timestamp = 0.0f;
}

void
game_input_process_events(struct GInput* input)
{
    for( int i = 0; i < TORIRSM_COUNT; i++ )
        input->mouse_button_states[i].pressed = false;
    for( int i = 0; i < TORIRSK_COUNT; i++ )
        input->key_states[i].pressed = false;

    for( int i = 0; i < input->in_event_count; i++ )
    {
        switch( input->in_events[i].type )
        {
        case TORIRSEV_KEY_DOWN:
            on_keydown(input, &input->in_events[i].key_down);
            break;
        case TORIRSEV_KEY_UP:
            on_keyup(input, &input->in_events[i].key_up);
            break;
        case TORIRSEV_MOUSE_DOWN:
            on_mousedown(input, &input->in_events[i].mouse_down);
            break;
        case TORIRSEV_MOUSE_UP:
            on_mouseup(input, &input->in_events[i].mouse_up);
            break;
        case TORIRSEV_MOUSE_MOVE:
            on_mousemove(input, &input->in_events[i].mouse_move);
            break;
        case TORIRSEV_MOUSE_WHEEL:
            on_mousewheel(input, &input->in_events[i].mouse_wheel);
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
    input->in_event_count = 0;
}