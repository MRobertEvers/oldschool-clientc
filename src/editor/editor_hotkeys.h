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
 * rest are states of the editor. A chord may be reused across exclusive
 * contexts: the arrows nudge in M and turn the camera in N; Return applies
 * the tool in L and places in P.
 *
 *   L excludes S and P (both say no loc is selected);
 *   M (locs selected AND the Select or Move tool armed) excludes N (no loc
 *   selected, OR another tool armed), S and P.
 *
 * With the Place tool (or a brush, Rect, Rotate, Delete) armed and locs
 * selected, the arrows turn the camera: only Select and Move nudge.
 *
 *   K (the picker has focus) excludes every other context, G included:
 *   while the Squares well holds the keyboard it is a field, the App's
 *   dispatcher resolves nothing (the text-field gate) and the panel resolves
 *   the well's keys against the K rows alone (Editor_HotkeysMatch). So the
 *   arrows, Return, Shift+Return, Backspace and Escape are the picker's
 *   there and the World's everywhere else.
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
/** No loc is selected, or the armed tool is neither Select nor Move. */
#define EDITOR_HK_CTX_N (1u << 5)
/** Locs are selected and the Select or Move tool is armed (the nudges). */
#define EDITOR_HK_CTX_M (1u << 6)
/** The Place tool is armed with a loc pick and no loc is selected. */
#define EDITOR_HK_CTX_P (1u << 7)
/** The Squares picker has focus (its well holds the keyboard; workbench
 *  6.4). Never set by Editor_HotkeyContextsFrom: the panel resolves it. */
#define EDITOR_HK_CTX_K (1u << 8)
#define EDITOR_HK_CTX_COUNT 9
#define EDITOR_HK_CTX_ALL 0x1ffu

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
    EDITOR_HK_FILE_PUBLISH,
    /* Workbench 6.5: save the session's state as a scene (the Scenes panel),
     * make a scratch lane from main and switch to it (the Lanes panel). */
    EDITOR_HK_FILE_SAVE_SCENE,
    EDITOR_HK_FILE_SCRATCH_LANE,
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
    /** Every loc of the hovered (else the primary selected) loc's id in the
     *  open squares on the edit level: the double-click's chord. */
    EDITOR_HK_EDIT_SELECT_SAME,
    EDITOR_HK_EDIT_NUDGE_N,
    EDITOR_HK_EDIT_NUDGE_S,
    EDITOR_HK_EDIT_NUDGE_W,
    EDITOR_HK_EDIT_NUDGE_E,
    /** Workbench 6.3: the nudges by a zone (8 tiles), and the selection a
     *  level up / down (refused where it cannot land). */
    EDITOR_HK_EDIT_NUDGE_FAR_N,
    EDITOR_HK_EDIT_NUDGE_FAR_S,
    EDITOR_HK_EDIT_NUDGE_FAR_W,
    EDITOR_HK_EDIT_NUDGE_FAR_E,
    EDITOR_HK_EDIT_LEVEL_UP,
    EDITOR_HK_EDIT_LEVEL_DOWN,
    EDITOR_HK_EDIT_COPY,
    EDITOR_HK_EDIT_PASTE,
    EDITOR_HK_EDIT_FILL,
    EDITOR_HK_EDIT_CLEAR,
    EDITOR_HK_EDIT_MARK_AREA,
    /* Place: the Place tool's own keys (its ghost's Q / Shift+Q are Edit's
     * Rotate and Reshape, which turn the pose while Place is armed). */
    EDITOR_HK_PLACE_HERE,
    EDITOR_HK_PLACE_FIT,
    EDITOR_HK_PLACE_SWAP,
    /** Held: a Place drag (a wall run) keeps to one axis (a qualifier). */
    EDITOR_HK_PLACE_CONSTRAIN,
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
    EDITOR_HK_TOOL_PLACE_PLAYER,
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
    /* Picker (context K, workbench 6.4): the focused cell north / south /
     * west / east, open it, add it to the open set, back to the previous
     * square of the open set, clear Find. Run by the Squares panel
     * (Editor_SquarePicker_Key), in this order. */
    EDITOR_HK_PICKER_NORTH,
    EDITOR_HK_PICKER_SOUTH,
    EDITOR_HK_PICKER_WEST,
    EDITOR_HK_PICKER_EAST,
    EDITOR_HK_PICKER_OPEN,
    EDITOR_HK_PICKER_ADD,
    EDITOR_HK_PICKER_BACK,
    EDITOR_HK_PICKER_CLEAR_FIND,
    /* View: one per View-menu panel module, then the layout rows. */
    /** The Catalog palette's slots (Shift+1..9, Shift+0), add the pick,
     *  next / previous palette (src/editor/panels/edpanel_catalog_palette.c). */
    EDITOR_HK_PALETTE_SLOT_1,
    EDITOR_HK_PALETTE_SLOT_2,
    EDITOR_HK_PALETTE_SLOT_3,
    EDITOR_HK_PALETTE_SLOT_4,
    EDITOR_HK_PALETTE_SLOT_5,
    EDITOR_HK_PALETTE_SLOT_6,
    EDITOR_HK_PALETTE_SLOT_7,
    EDITOR_HK_PALETTE_SLOT_8,
    EDITOR_HK_PALETTE_SLOT_9,
    EDITOR_HK_PALETTE_SLOT_10,
    EDITOR_HK_PALETTE_ADD_PICK,
    EDITOR_HK_PALETTE_NEXT,
    EDITOR_HK_PALETTE_PREVIOUS,
    EDITOR_HK_VIEW_HOTKEYS,
    EDITOR_HK_VIEW_TERRAIN,
    EDITOR_HK_VIEW_PALETTE,
    EDITOR_HK_VIEW_SQUARES,
    EDITOR_HK_VIEW_LOCS,
    EDITOR_HK_VIEW_SPAWNS,
    EDITOR_HK_VIEW_INSPECTOR,
    EDITOR_HK_VIEW_CATALOG,
    EDITOR_HK_VIEW_PREVIEW,
    EDITOR_HK_VIEW_CATALOG_PALETTE,
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
    EDITOR_HK_VIEW_ACCOUNTS,
    EDITOR_HK_VIEW_TRIGGERS,
    EDITOR_HK_VIEW_SCENES,
    EDITOR_HK_VIEW_ENCOUNTER,
    EDITOR_HK_VIEW_HIDE_PANELS,
    EDITOR_HK_VIEW_RESET_LAYOUT,
    EDITOR_HK_VIEW_SNAP,
    /** Hold: panels snap to the grid while this is down, whatever the toggle. */
    EDITOR_HK_VIEW_SNAP_HOLD,
    /** Pop the panel under the pointer out into its own window (World when
     *  over none); hide the panel under the pointer. */
    EDITOR_HK_VIEW_POPOUT,
    EDITOR_HK_VIEW_CLOSE_PANEL,
    /** View > Chrome scale (workbench 6.0b): Auto, then the design's steps
     *  in order (ui/torirs_chrome_scale_q.h); View > Text size Normal /
     *  Large; View > Text rendering bitmap at whole scales / smooth always.
     *  Unbound by default. */
    EDITOR_HK_VIEW_SCALE_AUTO,
    EDITOR_HK_VIEW_SCALE_100,
    EDITOR_HK_VIEW_SCALE_125,
    EDITOR_HK_VIEW_SCALE_150,
    EDITOR_HK_VIEW_SCALE_175,
    EDITOR_HK_VIEW_SCALE_200,
    EDITOR_HK_VIEW_SCALE_250,
    EDITOR_HK_VIEW_SCALE_300,
    EDITOR_HK_VIEW_TEXT_NORMAL,
    EDITOR_HK_VIEW_TEXT_LARGE,
    EDITOR_HK_VIEW_TEXT_BITMAP,
    EDITOR_HK_VIEW_TEXT_SMOOTH,
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
    /** The gesture that does the same on a trackpad or a touchscreen (design
     *  section 14's table, section 15's "gesture" column): "2 fingers" (the
     *  World's pan: the fly moves), "pinch" (the eye distance), "twist" (the
     *  turn); NULL where none does. The hint strip spells it while the last
     *  input was a finger. */
    const char* gesture;
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

/** Can a state make both masks live at once? (L excludes S and N; K
 *  excludes every other.) */
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

/**
 * A QUALIFIER: a held action whose chord may be a modifier key alone (Fly
 * faster, Snap while dragging, Constrain a drag). It fires nothing of its own;
 * it changes how the gesture it names goes -- the flight, a panel drag, a
 * Place drag -- so two qualifiers on the same bare modifier are not a clash:
 * Shift both speeds the flight and keeps a wall run straight.
 */
int
Editor_HotkeyIsQualifier(enum Editor_HotkeyAction action);

/** The first action that shares a chord with `action` in a context that can
 *  be live with it, or -1 (two qualifiers on one bare modifier excepted). */
int
Editor_HotkeysClash(
    const struct Editor_Hotkeys* hk,
    enum Editor_HotkeyAction action);

/** The action `chord` fires in `contexts` (G is NOT implied: the picker
 *  asks with K alone), or -1. The first chord or the second, as the
 *  resolver reads them; a clash goes to the later binding. */
int
Editor_HotkeysMatch(
    const struct Editor_Hotkeys* hk,
    struct Editor_Chord chord,
    unsigned contexts);

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

/**
 * The FIRST chord of each of `actions`, compacted the way the hint strip
 * spells a row of several: the modifier prefix they all share said once
 * ("Shift+[ ]" for Shift+[ and Shift+]), the four arrow keys as "arrows".
 * One action gives its first chord alone ("Q"), the keycap a control wears
 * (workbench 6.0: ToriRSChrome_ButtonSetChord, the tool rail's badges, the
 * Terrain sliders' "Radius  [ ]"). Unbound actions are skipped; "" when
 * none is bound. 1 <= count <= EDITOR_HK_HINT_ACTIONS.
 */
void
Editor_HotkeysJoin(
    const struct Editor_Hotkeys* hk,
    const int* actions,
    int count,
    char* out,
    int out_size);

/** The key a chord's modifier bit stands for (TORIRSK_CTRL ...), and back. */
int
Editor_HotkeyKeyIsModifier(int key);

/* ---- the hint strip ------------------------------------------------------------
 *
 * The status bar's left track (design #workbench "Status bar and hint strip",
 * #hotkeys surface 4): the live chords of the ARMED tool, as (chord, verb)
 * runs. One source -- the chords are read from the live table on every call,
 * so a rebind relabels the strip on the next frame -- and the rows are the
 * table's, filtered by the contexts live now: Delete appears once a loc is
 * selected, the nudges while Select or Move holds a selection, Copy and Fill
 * while the Rect tool holds a rectangle.
 *
 * `tool` is an enum Editor_Tool value (editor_ui.h, which includes this
 * header; it asserts the two orders agree): the first EDITOR_HK_HINT_TOOLS
 * EDITOR_HK_TOOL_* actions arm the tools in that order.
 */
#define EDITOR_HK_HINT_TOOLS (EDITOR_HK_TOOL_PLACE_PLAYER - EDITOR_HK_TOOL_SELECT + 1)
/** Rows one tool's strip holds at most (Select's nine). */
#define EDITOR_HK_HINT_MAX 10
/** Actions one row reads its chords from at most (the four nudges). */
#define EDITOR_HK_HINT_ACTIONS 4
#define EDITOR_HK_HINT_CHORD_MAX 48

struct Editor_HotkeyHint
{
    /** What to press, as the strip prints it: the mouse word ("click",
     *  "drag"), then the live chords ("click/Space, Enter", "Shift+[ ]",
     *  "arrows" for the four nudges on the arrow keys). With `touch`, the
     *  finger's word alone ("tap", "drag", "2 fingers", "pinch", "hold"). */
    char chord[EDITOR_HK_HINT_CHORD_MAX];
    /** What it does, one short word or two ("rotate", "add loc"). Static. */
    const char* verb;
    /** The first table action the row reads its chords from, -1 for a row
     *  that is the mouse alone (click tile, drag marquee). */
    int action;
};

/**
 * The live hint rows for `tool` under `contexts` (EDITOR_HK_CTX_*; G is
 * implied), in the tool's fixed order, into `out_rows` (at most `max`).
 * A row whose actions are unbound or not live keeps only its mouse word, and
 * is dropped when it has none. Returns how many were written.
 *
 * `touch` (the last input was a finger): the gesture words replace the
 * mouse words. The tool's pointer rows say the finger's ("tap tile", "drag
 * marquee"; a row with no finger spelling, Shift+click, is dropped, and so
 * are the keyboard's), then the camera's from the table's gesture spellings
 * and the long press: "2 fingers" pan, "pinch" zoom, "twist" turn, "hold"
 * menu. A camera action with no gesture spelling keeps its row out.
 *
 * The rows, per tool (chords are the defaults; the table decides):
 *
 *   Select        click tile, Shift+click add loc, drag marquee,
 *                 Ctrl+A square (W), arrows nudge (M), Q rotate,
 *                 Shift+Q reshape, E swap (L), Del delete (L, S)
 *   Place         click/Space place (P), Q turn, Shift+Q reshape, G fit,
 *                 E swap (L), Esc drop
 *   the brushes   drag paint, [ ] radius, Shift+[ ] strength, Ctrl+[ ] step
 *   Move          drag/arrows move (M), Ctrl+D dup (L, S)
 *   Rotate        click rotate
 *   Delete        click delete
 *   Rect          drag rectangle, Ctrl+C copy (R), Ctrl+V paste (W),
 *                 Ctrl+Shift+F fill (R), Shift+Del clear (R)
 *   Place player  click put player
 */
int
Editor_HotkeysHint(
    const struct Editor_Hotkeys* hk,
    unsigned contexts,
    int tool,
    int touch,
    struct Editor_HotkeyHint* out_rows,
    int max);

/** The editor state the contexts are derived from: what the App's key
 *  snapshot (src/app/app_editor_hotkeys.c) and the status bar's hint both
 *  read off the registry's shared tools. */
struct Editor_HotkeyContextFacts
{
    /** enum Editor_Tool. */
    int tool;
    /** The pointer is over the World panel's body and no chrome wants it. */
    int pointer_in_world;
    int loc_count;
    /** The Spawns panel's selection (0 while it is closed; behind a tab of
     *  its strip it counts: Editor_Spawns_SelectionCount). */
    int spawn_count;
    /** The catalog's pick is a loc (kind 0, id >= 0). */
    int loc_pick;
    /** The Rect tool holds a rectangle. */
    int rect_valid;
};

/** The EDITOR_HK_CTX_* mask `facts` make live (G always): the rules at the
 *  top of this header, in one place. */
unsigned
Editor_HotkeyContextsFrom(const struct Editor_HotkeyContextFacts* facts);

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
