#ifndef TORIRS_INPUT_H
#define TORIRS_INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum LibToriRS_KeyCode
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

enum LibToriRS_MouseButton
{
    TORIRSM_UNKNOWN,
    TORIRSM_LEFT,
    TORIRSM_MIDDLE,
    TORIRSM_RIGHT,
    TORIRSM_COUNT
};

enum LibToriRS_KeyEventType
{
    TORIRSEV_KEY_DOWN,
    TORIRSEV_KEY_UP,
    TORIRSEV_KEY_COUNT
};

enum LibToriRS_MouseEventType
{
    TORIRSEV_MOUSE_DOWN,
    TORIRSEV_MOUSE_UP,
    TORIRSEV_MOUSE_MOVE,
    TORIRSEV_MOUSE_WHEEL,
    TORIRSEV_MOUSE_COUNT
};

struct LibToriRS_Input_State
{
    uint64_t time;
    int key_down[TORIRSK_COUNT];
    int key_up[TORIRSK_COUNT];
    int mouse_button_down[TORIRSM_COUNT];
    int mouse_button_up[TORIRSM_COUNT];

    int mouse_x;
    int mouse_y;
};

struct LibToriRS_Input
{
    struct LibToriRS_Input_State prev;
    struct LibToriRS_Input_State curr;

    int mouse_dx;
    int mouse_dy;

    int key_held[TORIRSK_COUNT];

    // --- ADDED: High-Level UI Tracking Data ---
    // Indexed by LibToriRS_MouseButton (usually you only care about TORIRSM_LEFT for UI)

    int press_origin_x[TORIRSM_COUNT];
    int press_origin_y[TORIRSM_COUNT];

    uint64_t last_press_time[TORIRSM_COUNT];
    int last_click_x[TORIRSM_COUNT];
    int last_click_y[TORIRSM_COUNT];

    int mouse_button_held[TORIRSM_COUNT];
    int drag_active[TORIRSM_COUNT];

    // --- ADDED: Synthesized Events (Calculated at the start of the frame) ---
    int is_click[TORIRSM_COUNT];
    int is_double_click[TORIRSM_COUNT];
    int is_dragging[TORIRSM_COUNT];
    int drag_start[TORIRSM_COUNT];
    int drag_end[TORIRSM_COUNT];

    // --- ADDED: Threshold Configurations ---
    uint64_t double_click_threshold_ms; // e.g., 0.3 seconds
    uint64_t drag_deadzone_pixels;      // e.g., 5 pixels
};

struct LibToriRS_Input*
LibToriRS_Input_New(void);

struct LibToriRS_Input*
LibToriRS_Input_Init(
    struct LibToriRS_Input* input,
    uint64_t time);

void
LibToriRS_Input_Free(struct LibToriRS_Input* input);

void
LibToriRS_Input_Begin(
    struct LibToriRS_Input* input,
    uint64_t time);

void
LibToriRS_Input_End(struct LibToriRS_Input* input);

void
LibToriRS_Input_PushKeyDown(
    struct LibToriRS_Input* input,
    enum LibToriRS_KeyCode key);

void
LibToriRS_Input_PushKeyUp(
    struct LibToriRS_Input* input,
    enum LibToriRS_KeyCode key);

void
LibToriRS_Input_PushMouseDown(
    struct LibToriRS_Input* input,
    enum LibToriRS_MouseButton button,
    int x,
    int y);

void
LibToriRS_Input_PushMouseUp(
    struct LibToriRS_Input* input,
    enum LibToriRS_MouseButton button,
    int x,
    int y);

void
LibToriRS_Input_PushMouseMove(
    struct LibToriRS_Input* input,
    int x,
    int y);

static inline bool
LibToriRS_Input_IsKeyDown(
    struct LibToriRS_Input* input,
    enum LibToriRS_KeyCode key)
{
    return input->curr.key_down[key];
}

static inline bool
LibToriRS_Input_IsKeyUp(
    struct LibToriRS_Input* input,
    enum LibToriRS_KeyCode key)
{
    return input->curr.key_up[key];
}

static inline bool
LibToriRS_Input_IsKeyHeld(
    struct LibToriRS_Input* input,
    enum LibToriRS_KeyCode key)
{
    return input->key_held[key] != 0;
}

static inline bool
LibToriRS_Input_IsMouseDown(
    struct LibToriRS_Input* input,
    enum LibToriRS_MouseButton button)
{
    return input->curr.mouse_button_down[button];
}

static inline bool
LibToriRS_Input_IsClick(
    struct LibToriRS_Input* input,
    enum LibToriRS_MouseButton button)
{
    return input->is_click[button] != 0;
}

static inline bool
LibToriRS_Input_IsDoubleClick(
    struct LibToriRS_Input* input,
    enum LibToriRS_MouseButton button)
{
    return input->is_double_click[button] != 0;
}

static inline bool
LibToriRS_Input_IsDragStart(
    struct LibToriRS_Input* input,
    enum LibToriRS_MouseButton button)
{
    return input->drag_start[button] != 0;
}

static inline bool
LibToriRS_Input_IsDragEnd(
    struct LibToriRS_Input* input,
    enum LibToriRS_MouseButton button)
{
    return input->drag_end[button] != 0;
}

static inline bool
LibToriRS_Input_IsDragging(
    struct LibToriRS_Input* input,
    enum LibToriRS_MouseButton button)
{
    return input->is_dragging[button] != 0;
}

#endif