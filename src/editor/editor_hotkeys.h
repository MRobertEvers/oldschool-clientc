#ifndef SRC_EDITOR_EDITOR_HOTKEYS_H
#define SRC_EDITOR_EDITOR_HOTKEYS_H

/**
 * The map editor's hotkeys: one table (docs/MAPED_HOTKEYS.md is the user's
 * reference and the spec).
 *
 * This module is the pure half. It owns
 *   - the ACTIONS (enum Editor_HotkeyAction): every menu row, tool, brush
 *     control, level button, camera move and View-menu panel the keyboard can
 *     reach, each with an ini id, a group, a display name, the row of the doc
 *     it belongs to, a context mask and a kind (edge, repeat, held);
 *   - the DEFAULT chords (two per action at most: Redo, Delete selection);
 *   - chord parse / format: the ini spelling `ctrl+shift+z` (key names are
 *     LibToriRS_KeyCodeName's) and the hint spelling "Ctrl+Shift+Z" ("Cmd"
 *     for Ctrl on Apple platforms, where the platform reports Command as Ctrl);
 *   - the `editor_hotkeys.ini` codec: only non-default bindings are written,
 *     `<action>=` for unbound; an unknown action or chord ignores that line and
 *     says so in the report, it never aborts;
 *   - conflicts: two actions whose contexts can be live at once share a chord;
 *   - Editor_HotkeysResolve: one frame's key edges and held keys, plus which
 *     contexts are live, in; the actions that fire, the held actions that are
 *     active, and the keys the table claims, out. No App: the App glue
 *     (src/app/app_editor_hotkeys.c) builds the snapshot and dispatches.
 *
 * Contract violations assert (CLAUDE.md).
 */

#include "input/torirs_input.h"

#include <stdint.h>

/* ---- contexts ---------------------------------------------------------------
 *
 * Where an action is live. G is always live while the editor is open; the
 * rest are states of the editor. L (locs selected) excludes S and N, which
 * both mean "no loc is selected": a chord may be reused across exclusive
 * contexts (the arrows nudge in L and turn the camera in N).
 */
#define EDITOR_HK_CTX_G (1u << 0)
/** The pointer is over the World panel's body. */
#define EDITOR_HK_CTX_W (1u << 1)
/** One or more locs are selected. */
#define EDITOR_HK_CTX_L (1u << 2)
/** The Rect tool holds a rectangle. */
#define EDITOR_HK_CTX_R (1u << 3)
/** The Spawns panel holds a selection and no loc is selected. */
#define EDITOR_HK_CTX_S (1u << 4)
/** No loc is selected. */
#define EDITOR_HK_CTX_N (1u << 5)
#define EDITOR_HK_CTX_ALL 0x3fu

/** Modifier bits of a chord. */
#define EDITOR_HK_MOD_CTRL 1u
#define EDITOR_HK_MOD_SHIFT 2u
#define EDITOR_HK_MOD_ALT 4u

enum Editor_HotkeyKind
{
    /** Fires on the key's down edge. */
    EDITOR_HK_EDGE = 0,
    /** Fires on the edge, then after EDITOR_HK_REPEAT_DELAY_MS every
     *  EDITOR_HK_REPEAT_PERIOD_MS while the chord stays held. */
    EDITOR_HK_REPEAT,
    /** Active for as long as the chord is held (the camera, the click
     *  accelerators). The fly-faster modifier may be held on top. */
    EDITOR_HK_HELD,
};

#define EDITOR_HK_REPEAT_DELAY_MS 300
#define EDITOR_HK_REPEAT_PERIOD_MS 50

enum Editor_HotkeyAction
{
    /* File */
    EDITOR_HK_FILE_NEW = 0,
    EDITOR_HK_FILE_SAVE_ALL,
    EDITOR_HK_FILE_SAVE_AS,
    EDITOR_HK_FILE_BAKE,
    EDITOR_HK_FILE_CLOSE,
    /* The Triggers panel's Check (sscompile over the scripts on disk) and the
     * Live panel's RUN. */
    EDITOR_HK_FILE_CHECK_SCRIPTS,
    EDITOR_HK_FILE_RUN,
    /* Edit */
    EDITOR_HK_EDIT_UNDO,
    EDITOR_HK_EDIT_REDO,
    EDITOR_HK_EDIT_DESELECT,
    EDITOR_HK_EDIT_DELETE,
    EDITOR_HK_EDIT_DUPLICATE,
    EDITOR_HK_EDIT_ROTATE,
    EDITOR_HK_EDIT_RESHAPE,
    EDITOR_HK_EDIT_APPLY,
    EDITOR_HK_EDIT_SELECT_SQUARE,
    EDITOR_HK_EDIT_NUDGE_N,
    EDITOR_HK_EDIT_NUDGE_S,
    EDITOR_HK_EDIT_NUDGE_W,
    EDITOR_HK_EDIT_NUDGE_E,
    EDITOR_HK_EDIT_COPY,
    EDITOR_HK_EDIT_PASTE,
    EDITOR_HK_EDIT_FILL,
    EDITOR_HK_EDIT_CLEAR,
    EDITOR_HK_EDIT_MARK_AREA,
    /* Tools: the first EDITOR_TOOL_COUNT follow enum Editor_Tool's order. */
    EDITOR_HK_TOOL_SELECT,
    EDITOR_HK_TOOL_RECT,
    EDITOR_HK_TOOL_RAISE,
    EDITOR_HK_TOOL_LOWER,
    EDITOR_HK_TOOL_FLATTEN,
    EDITOR_HK_TOOL_SMOOTH,
    EDITOR_HK_TOOL_PAINT_UNDERLAY,
    EDITOR_HK_TOOL_PAINT_OVERLAY,
    EDITOR_HK_TOOL_PAINT_FLAGS,
    EDITOR_HK_TOOL_PLACE,
    EDITOR_HK_TOOL_MOVE,
    EDITOR_HK_TOOL_ROTATE,
    EDITOR_HK_TOOL_DELETE,
    EDITOR_HK_TOOL_PREVIOUS,
    EDITOR_HK_TOOL_PICK_PAINT,
    EDITOR_HK_TOOL_PICK_LOC,
    EDITOR_HK_HOLD_PLACE,
    EDITOR_HK_HOLD_DELETE,
    EDITOR_HK_HOLD_CLEAR,
    /* Brush */
    EDITOR_HK_BRUSH_RADIUS_DOWN,
    EDITOR_HK_BRUSH_RADIUS_UP,
    EDITOR_HK_BRUSH_STRENGTH_DOWN,
    EDITOR_HK_BRUSH_STRENGTH_UP,
    EDITOR_HK_BRUSH_STEP_DOWN,
    EDITOR_HK_BRUSH_STEP_UP,
    /* Level */
    EDITOR_HK_LEVEL_DOWN,
    EDITOR_HK_LEVEL_UP,
    EDITOR_HK_LEVEL_PIN0,
    EDITOR_HK_LEVEL_PIN1,
    EDITOR_HK_LEVEL_PIN2,
    EDITOR_HK_LEVEL_PIN3,
    EDITOR_HK_LEVEL_AUTO,
    EDITOR_HK_LEVEL_ABOVE,
    /* Camera */
    EDITOR_HK_CAM_FORWARD,
    EDITOR_HK_CAM_BACK,
    EDITOR_HK_CAM_LEFT,
    EDITOR_HK_CAM_RIGHT,
    EDITOR_HK_CAM_DOWN,
    EDITOR_HK_CAM_UP,
    EDITOR_HK_CAM_FAST,
    EDITOR_HK_CAM_TURN_LEFT,
    EDITOR_HK_CAM_TURN_RIGHT,
    EDITOR_HK_CAM_TILT_UP,
    EDITOR_HK_CAM_TILT_DOWN,
    EDITOR_HK_CAM_FRAME,
    EDITOR_HK_CAM_FRAME_SQUARE,
    /* View: one per View-menu panel module, then the layout rows. */
    EDITOR_HK_VIEW_HOTKEYS,
    EDITOR_HK_VIEW_TERRAIN,
    EDITOR_HK_VIEW_PALETTE,
    EDITOR_HK_VIEW_SQUARES,
    EDITOR_HK_VIEW_LOCS,
    EDITOR_HK_VIEW_SPAWNS,
    EDITOR_HK_VIEW_INSPECTOR,
    EDITOR_HK_VIEW_CATALOG,
    EDITOR_HK_VIEW_PREVIEW,
    EDITOR_HK_VIEW_CONSOLE,
    EDITOR_HK_VIEW_HISTORY,
    EDITOR_HK_VIEW_AREAS,
    EDITOR_HK_VIEW_WORLD,
    EDITOR_HK_VIEW_TOOLRAIL,
    EDITOR_HK_VIEW_RECT,
    EDITOR_HK_VIEW_RECORD,
    EDITOR_HK_VIEW_LOCBUILDER,
    EDITOR_HK_VIEW_NPCBUILDER,
    EDITOR_HK_VIEW_SHELL,
    EDITOR_HK_VIEW_IFACE,
    EDITOR_HK_VIEW_CS2,
    EDITOR_HK_VIEW_LIVE,
    EDITOR_HK_VIEW_TRIGGERS,
    EDITOR_HK_VIEW_HIDE_PANELS,
    EDITOR_HK_VIEW_RESET_LAYOUT,
    EDITOR_HK_VIEW_SNAP,
    /** Hold: panels snap to the grid while this is down, whatever the toggle. */
    EDITOR_HK_VIEW_SNAP_HOLD,
    /** Pop the panel under the pointer out into its own window (World when
     *  over none); hide the panel under the pointer. */
    EDITOR_HK_VIEW_POPOUT,
    EDITOR_HK_VIEW_CLOSE_PANEL,
    EDITOR_HK_COUNT
};

/** Chords one action may carry. */
#define EDITOR_HK_CHORDS 2

/** A key and its modifiers. key TORIRSK_UNKNOWN (0) = no chord. */
struct Editor_Chord
{
    uint8_t key;
    uint8_t mods;
};

struct Editor_HotkeyActionInfo
{
    /** The ini spelling ("edit.undo"); listed in docs/MAPED_HOTKEYS.md. */
    const char* id;
    /** The doc table it sits in ("Edit"). */
    const char* group;
    /** What the Hotkeys panel calls it ("Undo"). */
    const char* name;
    /** The text of its row in docs/MAPED_HOTKEYS.md (the doc test). */
    const char* doc;
    /** EDITOR_HK_CTX_* mask. */
    unsigned contexts;
    enum Editor_HotkeyKind kind;
    /** Default chords in the ini spelling, NULL = none. */
    const char* defaults[EDITOR_HK_CHORDS];
    /** View actions: the registry module the action shows / hides. */
    const char* panel;
};

struct Editor_HotkeyBinding
{
    struct Editor_Chord chord[EDITOR_HK_CHORDS];
    /** When it was bound: the later binding wins a clash. 0 = default. */
    uint32_t stamp;
};

/** The live table plus the resolver's repeat state. */
struct Editor_Hotkeys
{
    struct Editor_HotkeyBinding bind[EDITOR_HK_COUNT];
    uint32_t stamp_next;
    /** Bumps on every binding change: menus and tips re-label from it. */
    uint32_t serial;
    /** Repeaters: the chord is being held since its edge, and when it fires
     *  next. */
    unsigned char repeat_held[EDITOR_HK_COUNT];
    uint64_t repeat_next_ms[EDITOR_HK_COUNT];
};

/* ---- the table ------------------------------------------------------------- */

const struct Editor_HotkeyActionInfo*
Editor_HotkeyInfo(enum Editor_HotkeyAction action);

/** The action whose id is `id`, or -1. */
int
Editor_HotkeyFind(const char* id);

/** The View action that shows or hides registry module `panel`, or -1. */
int
Editor_HotkeyForPanel(const char* panel);

/** "G", "W", "L, S", "no loc": the context mask as the panel prints it. */
void
Editor_HotkeyContextText(
    unsigned contexts,
    char* out,
    int out_size);

/** Can a state make both masks live at once? (L excludes S and N.) */
int
Editor_HotkeyContextsOverlap(
    unsigned a,
    unsigned b);

/** Defaults; repeat state cleared. */
void
Editor_HotkeysInit(struct Editor_Hotkeys* hk);

/** Every action back to its default chords (bumps the serial). */
void
Editor_HotkeysResetDefaults(struct Editor_Hotkeys* hk);

/** Bind `action` to `count` chords (0 = unbound), stamped now: it wins any
 *  clash until the other side is rebound. Bumps the serial. */
void
Editor_HotkeysSet(
    struct Editor_Hotkeys* hk,
    enum Editor_HotkeyAction action,
    const struct Editor_Chord* chords,
    int count);

/** Same chords as the default (order included). */
int
Editor_HotkeysIsDefault(
    const struct Editor_Hotkeys* hk,
    enum Editor_HotkeyAction action);

/** The first action that shares a chord with `action` in a context that can
 *  be live with it, or -1. */
int
Editor_HotkeysClash(
    const struct Editor_Hotkeys* hk,
    enum Editor_HotkeyAction action);

/** Every clashing pair (a < b) into out[2*i], out[2*i+1]; returns how many. */
int
Editor_HotkeysConflicts(
    const struct Editor_Hotkeys* hk,
    int* out_pairs,
    int max_pairs);

/* ---- chords ------------------------------------------------------------------ */

/** "ctrl+shift+z", "f1", "shift" (a modifier alone names that key). Case
 *  insensitive; "cmd" is "ctrl". 0 on anything else. */
int
Editor_ChordParse(
    const char* text,
    struct Editor_Chord* out);

/** The ini spelling. "" for no chord. */
void
Editor_ChordFormat(
    struct Editor_Chord chord,
    char* out,
    int out_size);

/** The hint spelling: "Ctrl+Shift+Z" ("Cmd+Shift+Z" on Apple), "[", "PgUp". */
void
Editor_ChordDisplay(
    struct Editor_Chord chord,
    char* out,
    int out_size);

/** Every chord of `action` for a hint, ", "-separated; "" when unbound. */
void
Editor_HotkeyDisplay(
    const struct Editor_Hotkeys* hk,
    enum Editor_HotkeyAction action,
    char* out,
    int out_size);

/** The key a chord's modifier bit stands for (TORIRSK_CTRL ...), and back. */
int
Editor_HotkeyKeyIsModifier(int key);

/* ---- editor_hotkeys.ini ------------------------------------------------------------ */

/** `TORIRS_EDITOR_HOTKEYS` unset: this name (cwd, IndexedDB on the web). */
#define EDITOR_HOTKEYS_DEFAULT_PATH "editor_hotkeys.ini"
#define EDITOR_HOTKEYS_VERSION 1

/** What a decode skipped, for the console. */
#define EDITOR_HOTKEYS_REPORT_MAX 8
#define EDITOR_HOTKEYS_REPORT_LINE 112

struct Editor_HotkeysReport
{
    int applied;
    int ignored;
    /** The first EDITOR_HOTKEYS_REPORT_MAX ignored lines, explained. */
    char lines[EDITOR_HOTKEYS_REPORT_MAX][EDITOR_HOTKEYS_REPORT_LINE];
    int line_count;
};

/** The path rule of TORIRS_EDITOR_LAYOUT: env unset = the default name,
 *  empty = NULL (persistence off), else the value. */
const char*
Editor_HotkeysPath(void);

/**
 * `size` bytes of ini text onto the DEFAULTS: `hk` is reset first, then each
 * `<action> = <chord>[, <chord>]` line rebinds (later lines stamped later).
 * Comments (; #), blank lines, `[hotkeys]` and `version` are skipped; a line
 * in another section, an unknown action, an unparsable chord or more than
 * EDITOR_HK_CHORDS chords is ignored and explained in `report` (may be NULL).
 * Returns the lines applied.
 */
int
Editor_HotkeysDecode(
    struct Editor_Hotkeys* hk,
    const void* data,
    int size,
    struct Editor_HotkeysReport* report);

/** The bindings that differ from the defaults, as ini text. `*out_data` is
 *  ToriMem_Process()'s and the caller's; NUL-terminated, not counted. */
void
Editor_HotkeysEncode(
    const struct Editor_Hotkeys* hk,
    char** out_data,
    int* out_size);

/* ---- one frame ---------------------------------------------------------------- */

/** The frame's keyboard as the App saw it, and the live contexts. */
struct Editor_HotkeyInput
{
    uint64_t now_ms;
    /** Down edges this frame. */
    unsigned char down[TORIRSK_COUNT];
    /** Held now (an edge is held too). */
    unsigned char held[TORIRSK_COUNT];
    /** EDITOR_HK_CTX_* live this frame (G is implied). */
    unsigned contexts;
};

#define EDITOR_HK_FIRED_MAX 16

struct Editor_HotkeyFrame
{
    /** Edge and repeat actions that fire, in table order, and the chord
     *  each fired on. */
    int count;
    int fired[EDITOR_HK_FIRED_MAX];
    struct Editor_Chord fired_chord[EDITOR_HK_FIRED_MAX];
    /** Held actions active this frame. */
    unsigned char active[EDITOR_HK_COUNT];
    /** Keys whose chord under the held modifiers is bound in a live
     *  context, whether or not it acted: withheld from the debug hotkeys. */
    unsigned char claimed[TORIRSK_COUNT];
    /** Keys of the actions that fired or are active this frame. */
    unsigned char consumed[TORIRSK_COUNT];
};

/** Modifier bits held in `in` (a modifier key's own bit is not counted
 *  against a chord on that key). */
unsigned
Editor_HotkeyHeldMods(const struct Editor_HotkeyInput* in);

void
Editor_HotkeysResolve(
    struct Editor_Hotkeys* hk,
    const struct Editor_HotkeyInput* in,
    struct Editor_HotkeyFrame* out);

/**
 * The capture half of the Hotkeys panel: the chord pressed this frame, if
 * any. A modifier alone never counts unless `allow_modifier` (the fly-faster
 * action is bound to a modifier). Returns 1 with `*out` set, 0 for nothing.
 */
int
Editor_HotkeysCapture(
    const struct Editor_HotkeyInput* in,
    int allow_modifier,
    struct Editor_Chord* out);

#endif
