#ifndef GINPUT_H
#define GINPUT_H

#include <stdbool.h>
#include <stdint.h>

enum GameInputEventType
{
    TORIRSEV_KEY_DOWN,
    TORIRSEV_KEY_UP,
    TORIRSEV_MOUSE_DOWN,
    TORIRSEV_MOUSE_UP,
    TORIRSEV_MOUSE_MOVE,
    TORIRSEV_MOUSE_WHEEL,

    TORIRSEV2_CLICK,
    TORIRSEV2_DRAG_START,
    TORIRSEV2_DRAG_END,
    TORIRSEV2_DRAG_MOVE,
    TORIRSEV2_DRAG_CANCEL,
};

enum GameInputKeyCode
{
    TORIRSK_UNKNOWN,
    TORIRSK_A,
    TORIRSK_B,
    TORIRSK_C,
    TORIRSK_D,
    TORIRSK_E,
    TORIRSK_F,
    TORIRSK_G,
    TORIRSK_H,
    TORIRSK_I,
    TORIRSK_J,
    TORIRSK_K,
    TORIRSK_L,
    TORIRSK_M,
    TORIRSK_N,
    TORIRSK_O,
    TORIRSK_P,
    TORIRSK_Q,
    TORIRSK_R,
    TORIRSK_S,
    TORIRSK_T,
    TORIRSK_U,
    TORIRSK_V,
    TORIRSK_W,
    TORIRSK_X,
    TORIRSK_Y,
    TORIRSK_Z,
    TORIRSK_0,
    TORIRSK_1,
    TORIRSK_2,
    TORIRSK_3,
    TORIRSK_4,
    TORIRSK_5,
    TORIRSK_6,
    TORIRSK_7,
    TORIRSK_8,
    TORIRSK_9,
    TORIRSK_ESCAPE,
    TORIRSK_RETURN,
    TORIRSK_BACKSPACE,
    TORIRSK_INSERT,
    TORIRSK_DELETE,
    TORIRSK_SHIFT,
    TORIRSK_CTRL,
    TORIRSK_TAB,
    TORIRSK_SPACE,
    TORIRSK_LEFT,
    TORIRSK_RIGHT,
    TORIRSK_UP,
    TORIRSK_DOWN,
    TORIRSK_PAGE_UP,
    TORIRSK_PAGE_DOWN,
    TORIRSK_COMMA,
    TORIRSK_COUNT
};

enum GameInputMouseButton
{
    TORIRSM_LEFT,
    TORIRSM_MIDDLE,
    TORIRSM_RIGHT,
    TORIRSM_COUNT
};

struct GameInputEvent_MouseMove
{
    int x;
    int y;
    int xrel;
    int yrel;
};

struct GameInputEvent_MouseWheel
{
    int wheel;
    int mouse_x;
    int mouse_y;
};

struct GameInputEvent_KeyDown
{
    int key;
};

struct GameInputEvent_KeyUp
{
    int key;
};

struct GameInputEvent_MouseDown
{
    int button;
    int mouse_x;
    int mouse_y;
};

struct GameInputEvent_MouseUp
{
    int button;
    int mouse_x;
    int mouse_y;
};

struct GameInputEvent2_Click
{
    int button;
    int start_mouse_x;
    int start_mouse_y;
    int end_mouse_x;
    int end_mouse_y;
};

struct GameInputEvent
{
    enum GameInputEventType type;
    union
    {
        struct GameInputEvent_MouseMove mouse_move;
        struct GameInputEvent_MouseWheel mouse_wheel;
        struct GameInputEvent_KeyDown key_down;
        struct GameInputEvent_KeyUp key_up;
        struct GameInputEvent_MouseDown mouse_down;
        struct GameInputEvent_MouseUp mouse_up;
        struct GameInputEvent2_Click click;
    };
};

struct GameInputKeyState
{
    bool pressed;
    bool down;
    double down_timestamp;
};

struct GameInputMouseButtonState
{
    bool pressed;
    bool down;
    double down_timestamp;
    int x;
    int y;
};

struct GameInputMouseState
{
    int x;
    int y;
};

struct GInput
{
    int quit;

    struct GameInputEvent in_events[128];
    int in_event_count;

    struct GameInputEvent events[155];
    int event_count;

    double time_delta_accumulator_seconds;
    double time;

    struct GameInputMouseState mouse_state;
    struct GameInputMouseButtonState mouse_button_states[TORIRSM_COUNT];
    struct GameInputKeyState key_states[TORIRSK_COUNT];
};

void
game_input_process_events(struct GInput* input);

bool
game_input_keydown_or_pressed(
    struct GInput* input,
    enum GameInputKeyCode key);

#endif