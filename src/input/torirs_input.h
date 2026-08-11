#ifndef SRC_INPUT_TORIRS_INPUT_H
#define SRC_INPUT_TORIRS_INPUT_H

#include "torirs_keymap.h"

#include <stdbool.h>
#include <stdint.h>

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

#define LIBTORIRS_KEY_EVENT_MAX 32

/**
 * One keyboard event as a CS2 onKey handler sees it.
 *
 * The field naming follows the reference (InputManager.onKeyDown and
 * Cs2Vm.substituteMagicArgs) and is INVERTED relative to canonical OSRS
 * clientscript naming: `key_typed` carries the OSRS internal KEY CODE, and
 * `key_pressed` carries the typed CHARACTER. Both ends of the reference are
 * inverted together, which is why cache scripts work -- correcting only one
 * side would silently break every onKey handler.
 *
 * A single event carries exactly one of the two, never both:
 *   character key: key_typed = -1,       key_pressed = charcode (>= 32)
 *   other key:     key_typed = osrs code, key_pressed = 0
 */
struct LibToriRS_KeyEvent
{
    int key_typed;
    int key_pressed;
    /** 1 when this came from OS auto-repeat rather than a fresh press. The
     *  reference does not expose this (it forwards browser repeats as ordinary
     *  keydowns), but op-key bindings need it to honour SETOPKEYIGNOREHELD. */
    int is_repeat;
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
    int mouse_wheel_y;
};

struct LibToriRS_Input
{
    struct LibToriRS_Input_State prev;
    struct LibToriRS_Input_State curr;

    int mouse_dx;
    int mouse_dy;

    int key_held[TORIRSK_COUNT];

    int press_origin_x[TORIRSM_COUNT];
    int press_origin_y[TORIRSM_COUNT];
    uint64_t press_origin_time[TORIRSM_COUNT];

    uint64_t last_press_time[TORIRSM_COUNT];
    int last_click_x[TORIRSM_COUNT];
    int last_click_y[TORIRSM_COUNT];

    int mouse_button_held[TORIRSM_COUNT];
    int drag_active[TORIRSM_COUNT];

    int is_click[TORIRSM_COUNT];
    int is_double_click[TORIRSM_COUNT];
    int is_dragging[TORIRSM_COUNT];
    int drag_start[TORIRSM_COUNT];
    int drag_end[TORIRSM_COUNT];

    /* Keyboard events for this frame, in arrival order. Lives here rather than
     * in LibToriRS_Input_State because Begin() does `prev = curr`, and an
     * ordered queue has no meaningful prev/curr edge semantics -- it is simply
     * cleared each frame, matching the reference's onFrameEnd. Unconsumed keys
     * are dropped; overflow past the cap is dropped silently. */
    struct LibToriRS_KeyEvent key_events[LIBTORIRS_KEY_EVENT_MAX];
    int key_event_count;

    /* Key state indexed by OSRS internal code, backing the CS2 KEYHELD and
     * KEYPRESSED opcodes. Updated for character keys too, unlike key_events,
     * where a character event replaces the key-code event. */
    unsigned char osrs_key_held[TORIRS_OSRSKEY_COUNT];
    unsigned char osrs_key_pressed[TORIRS_OSRSKEY_COUNT];

    uint64_t double_click_threshold_ms;
    uint64_t drag_deadzone_pixels;
    uint64_t drag_dead_time_ms;
};

#define LIBTORIRS_INPUT_DEFAULT_DRAG_DEADZONE_PX 5u

void
LibToriRS_Input_SetDragThresholds(
    struct LibToriRS_Input* input,
    uint64_t zone_pixels,
    uint64_t time_ms);

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

/** Continue an input frame which App_RunOnce could not consume. Updates the
 * clock while retaining mouse/key one-shots and queued key events. */
void
LibToriRS_Input_Continue(
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

/**
 * Append one keyboard event for this frame.
 *
 * Callers must honour the mutual-exclusion rule described on
 * struct LibToriRS_KeyEvent: either key_typed >= 0 with key_pressed == 0, or
 * key_typed == -1 with key_pressed >= 32. Drops silently once the queue is
 * full, mirroring how the rest of this module handles per-frame overflow.
 */
void
LibToriRS_Input_PushKeyEvent(
    struct LibToriRS_Input* input,
    int key_typed,
    int key_pressed,
    int is_repeat);

/**
 * Mark an OSRS-coded key held (down != 0) or released, backing KEYHELD and
 * KEYPRESSED. `pressed_edge` additionally records a this-frame press; pass 0
 * for OS auto-repeat so KEYPRESSED stays a true edge.
 */
void
LibToriRS_Input_SetOsrsKeyState(
    struct LibToriRS_Input* input,
    int osrs_key,
    int down,
    int pressed_edge);

/**
 * Drop all keyboard state: pending events, edges, and held flags.
 *
 * Needed on window focus loss. The OS stops delivering the matching key-up once
 * focus is gone, so without this a key held at focus-out latches forever.
 */
void
LibToriRS_Input_ClearKeys(struct LibToriRS_Input* input);

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

void
LibToriRS_Input_PushMouseWheel(
    struct LibToriRS_Input* input,
    int wheel_y);

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
LibToriRS_Input_IsMouseHeld(
    struct LibToriRS_Input* input,
    enum LibToriRS_MouseButton button)
{
    return input->mouse_button_held[button] != 0;
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
