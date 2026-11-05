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
    /* Appended 2026-10-31 for the map editor's hotkey table (every key a
     * full editor binds: brackets for the brush, F-keys for the panels,
     * Home/End, the Alt chord). Each sizes three int arrays per input
     * snapshot and nothing else; every platform's key map, the manifest's
     * [debug:hotkeys] spelling and TORIRS_SIM_HOTKEY know them by the names
     * LibToriRS_KeyCodeName returns. */
    TORIRSK_PERIOD,
    TORIRSK_MINUS,
    TORIRSK_EQUALS,
    TORIRSK_LBRACKET,
    TORIRSK_RBRACKET,
    TORIRSK_SEMICOLON,
    TORIRSK_APOSTROPHE,
    TORIRSK_GRAVE,
    TORIRSK_SLASH,
    TORIRSK_BACKSLASH,
    TORIRSK_HOME,
    TORIRSK_END,
    TORIRSK_ALT,
    TORIRSK_F1,
    TORIRSK_F2,
    TORIRSK_F3,
    TORIRSK_F4,
    TORIRSK_F5,
    TORIRSK_F6,
    TORIRSK_F7,
    TORIRSK_F8,
    TORIRSK_F9,
    TORIRSK_F10,
    TORIRSK_F11,
    TORIRSK_F12,
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
 * The three gesture frames (TORIRS_CMD_GESTURE_PAN / _PINCH / _TWIST), as the
 * consumers read them. docs/GESTURES.md has the producers per platform.
 */
enum LibToriRS_GestureKind
{
    LIBTORIRS_GESTURE_PAN,
    LIBTORIRS_GESTURE_PINCH,
    LIBTORIRS_GESTURE_TWIST,
    LIBTORIRS_GESTURE_KIND_COUNT
};

/**
 * One gesture is a BEGIN, any number of UPDATEs and an END, per kind; a pan, a
 * pinch and a twist of the same two fingers are three gestures that overlap.
 * BEGIN carries the motion that made it a gesture (everything since the
 * fingers landed), UPDATE the motion since the previous frame of its kind,
 * END none (dx = dy = 0, scale 1, radians 0): it only says the fingers left,
 * or a wheel stream went quiet.
 */
enum LibToriRS_GesturePhase
{
    LIBTORIRS_GESTURE_BEGIN,
    LIBTORIRS_GESTURE_UPDATE,
    LIBTORIRS_GESTURE_END,
    LIBTORIRS_GESTURE_PHASE_COUNT
};

/** The same motion ALSO reached this frame the old way -- the integer wheel,
 *  TORIRS_CMD_INPUT_SCROLL or TORIRS_CMD_INPUT_PINCH -- so a consumer that
 *  takes the gesture drops those (a trackpad scroll, a ctrl+wheel, a browser
 *  pinch). Clear on a touchscreen's frames under ToriRS_TouchSetGestureFrames,
 *  which replace the synthesis instead of accompanying it, and on every
 *  TWIST: no legacy route carries a turn. */
#define LIBTORIRS_GESTURE_FLAG_ALSO_LEGACY 0x01u
/** Fingers on the glass (a touchscreen), not a trackpad or a wheel: the hint
 *  strip's "the last input was a touch". */
#define LIBTORIRS_GESTURE_FLAG_TOUCH 0x02u

/** Gesture frames kept per input frame. A touch produces at most one of each
 *  kind a tick; a wheel one per OS event, which at a trackpad's 120 Hz into a
 *  20 fps frame is six. Past the cap an UPDATE folds into the last entry of
 *  its kind (nothing is lost but the granularity); anything else is dropped
 *  and counted. */
#define LIBTORIRS_GESTURE_MAX 32

/**
 * One decoded gesture frame. Canvas pixels; (x, y) the centre the gesture is
 * about (the fingers' centroid, or the pointer on a trackpad).
 *
 *   PAN    dx, dy: how far the content under the gesture moves, +x right,
 *          +y down -- a touchscreen's fingers' own travel; a trackpad's
 *          scroll with the platform's direction setting applied. `fingers`
 *          is 2 for a trackpad scroll, the fingers down for a touch.
 *   PINCH  scale: the ratio since the previous frame, > 1 fingers apart.
 *   TWIST  radians: the turn since the previous frame, + clockwise on screen.
 *
 * The fields a kind does not use hold their identity (0, or scale 1).
 */
struct LibToriRS_Gesture
{
    uint8_t kind;    /* enum LibToriRS_GestureKind */
    uint8_t phase;   /* enum LibToriRS_GesturePhase */
    uint8_t fingers; /* PAN; 0 otherwise */
    uint8_t flags;   /* LIBTORIRS_GESTURE_FLAG_* */
    int x;
    int y;
    float dx;
    float dy;
    float scale;
    float radians;
};

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

    /**
     * This frame's pinch (TORIRS_CMD_INPUT_PINCH): the product of every pinch
     * step that arrived, 0 when none did (memset-to-zero is "no pinch"). > 1
     * is fingers apart. Centre in canvas pixels, the last step's.
     * `pinch_from_wheel`: the same motion is also in `mouse_wheel_y`.
     */
    float pinch_scale;
    int pinch_x;
    int pinch_y;
    int pinch_from_wheel;

    /**
     * The wheel's exact deltas this frame (TORIRS_CMD_INPUT_SCROLL), canvas
     * pixels, the platform's own direction setting applied: +y scroll up
     * (content down), +x scroll right (content left). `scroll_trackpad` when
     * any of them came from a trackpad; `scroll_px_per_notch` the last one's
     * (0 when no scroll arrived); `scroll_flipped` the platform's "natural"
     * flag (-scroll_dy then has mouse_wheel_y's sign).
     */
    float scroll_dx;
    float scroll_dy;
    int scroll_px_per_notch;
    int scroll_trackpad;
    int scroll_flipped;
};

struct LibToriRS_Input
{
    struct LibToriRS_Input_State prev;
    struct LibToriRS_Input_State curr;

    int mouse_dx;
    int mouse_dy;

    /**
     * No pointer is resting at mouse_x/y any more -- @see
     * LibToriRS_Input_PushMouseLeave.
     *
     * Zero is "a pointer is there", which is what a mouse host never has to
     * say and what memset-to-zero already means. mouse_x/y are deliberately
     * left where they were: this says the position is STALE, not that the
     * pointer moved somewhere else.
     */
    int mouse_pointer_absent;

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

    /* Gesture frames for this frame, in arrival order -- the same lifetime as
     * key_events above, for the same reason: cleared by Begin, kept by
     * Continue, never edge-compared. The consumers (the World camera, GRAPH
     * wells, the Preview, lists, sliders) walk it once a frame.
     * `gesture_dropped` counts this frame's frames that found the list full
     * and could not be folded into an UPDATE of their kind;
     * `gesture_dropped_total` the same since Init, for a diagnostic line. */
    struct LibToriRS_Gesture gestures[LIBTORIRS_GESTURE_MAX];
    int gesture_count;
    int gesture_dropped;
    long gesture_dropped_total;
    /* The last POINTER input was a finger (ToriRS_InputLastWasTouch, the
     * hint strip's words): set by a finger landing
     * (LibToriRS_Input_PushTouchBegan) and by a gesture frame flagged
     * LIBTORIRS_GESTURE_FLAG_TOUCH; cleared by a gesture frame without it (a
     * trackpad, a ctrl+wheel) and by a mouse move, click, wheel or scroll
     * while no finger is down (LibToriRS_Input_NoteDesktopPointer). Not
     * cleared by Begin -- it holds across quiet frames. Keys leave it alone:
     * a phone's soft keyboard types, and the strip names pointer gestures.
     * `touch_down`: a finger landed and the touch policy has not yet said it
     * left (TORIRS_CMD_INPUT_MOUSE_LEAVE), so the pointer commands between
     * are the finger's own. */
    int last_touch;
    int touch_down;

    /* Key state indexed by OSRS internal code, backing the CS2 KEYHELD and
     * KEYPRESSED opcodes. Updated for character keys too, unlike key_events,
     * where a character event replaces the key-code event. */
    unsigned char osrs_key_held[TORIRS_OSRSKEY_COUNT];
    unsigned char osrs_key_pressed[TORIRS_OSRSKEY_COUNT];
    /* Released-this-frame edge, the mirror of osrs_key_pressed. Drives the
     * on_key_up component hook (reference class162.method5439, the key-up list
     * the widget dispatch walks); nothing else needs a release edge, since
     * KEYHELD reads osrs_key_held. */
    unsigned char osrs_key_released[TORIRS_OSRSKEY_COUNT];

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

/**
 * The config spelling of a key: "a".."z", "0".."9", "escape", "return",
 * "backspace", "insert", "delete", "shift", "ctrl", "alt", "tab", "space",
 * "left", "right", "up", "down", "pageup", "pagedown", "home", "end",
 * "comma", "period", "minus", "equals", "lbracket", "rbracket", "semicolon",
 * "apostrophe", "grave", "slash", "backslash", "f1".."f12". NULL for
 * TORIRSK_UNKNOWN and anything past the enum. One vocabulary for the
 * manifest's [debug:hotkeys], the editor's hotkeys file and
 * TORIRS_SIM_HOTKEY.
 */
char const*
LibToriRS_KeyCodeName(enum LibToriRS_KeyCode key);

/** The inverse, case-insensitive; "enter" and "page_up"/"page_down" are
 *  accepted as well. TORIRSK_UNKNOWN for an unnamed key. */
enum LibToriRS_KeyCode
LibToriRS_KeyCodeFromName(char const* name);

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

/** A pinch step: multiplies curr.pinch_scale, centre (x, y). scale > 0. */
void
LibToriRS_Input_PushPinch(
    struct LibToriRS_Input* input,
    float scale,
    int x,
    int y,
    int from_wheel);

/** A precise scroll: adds to curr.scroll_dx/dy. px_per_notch > 0. */
void
LibToriRS_Input_PushScroll(
    struct LibToriRS_Input* input,
    float dx,
    float dy,
    int px_per_notch,
    int trackpad,
    int flipped);

/** A finger landed (TORIRS_CMD_INPUT_TOUCH_BEGAN): @see last_touch. */
void
LibToriRS_Input_PushTouchBegan(struct LibToriRS_Input* input);

/** A mouse move, click, wheel or scroll arrived off the bus: unless a finger
 *  is down (its own synthesized pointer), the last pointer input is a mouse.
 *  @see last_touch. */
void
LibToriRS_Input_NoteDesktopPointer(struct LibToriRS_Input* input);

/**
 * Append one gesture frame to this frame's list (@see struct
 * LibToriRS_Gesture). The kind, the phase and the identities of the fields
 * the kind does not use are the caller's contract: a PINCH's scale is > 0, an
 * END carries no motion. Full: an UPDATE folds into the last entry of its kind
 * when that one is a BEGIN or an UPDATE; otherwise the frame is dropped and
 * counted.
 */
void
LibToriRS_Input_PushGesture(
    struct LibToriRS_Input* input,
    struct LibToriRS_Gesture const* gesture);

/**
 * The pointer stopped resting on the surface: the finger lifted, or the cursor
 * left the window.
 *
 * Raises mouse_pointer_absent and touches nothing else. Any later move, press
 * or release lowers it again, so a host that never calls this is unaffected.
 *
 * mouse_x/y keep their values on purpose. A touch tap's popup is anchored at
 * the tap, the minimenu dismisses itself when the pointer wanders off it
 * (interact_minimenu's close-on-leave), and hit tests re-read the position
 * while the popup is up -- moving the pointer to a nowhere coordinate to say
 * "no pointer" would close the menu the tap just opened.
 */
void
LibToriRS_Input_PushMouseLeave(struct LibToriRS_Input* input);

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
