#ifndef SRC_EDITOR_PANELS_EDPANEL_COMMON_H
#define SRC_EDITOR_PANELS_EDPANEL_COMMON_H

/* Private to src/editor/panels/: what every module repeats. */

#include "editor/editor.h"
#include "editor/editor_ui.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>

/* Default panel origins, in 1x chrome pixels (scaled by edp_px). Every panel
 * is draggable; these are where they open. */
#define EDP_RAIL_X 4
#define EDP_TOP_Y 28
#define EDP_COL_X 48
#define EDP_COL_W 196
#define EDP_PICKER_W 400

/* The default layout (editor_ui.c Editor_UiLayout), in 1x chrome pixels.
 *
 * EDP_GRID is the dock grid's cell: a dropped panel's origin and a resized
 * panel's size snap to it. 8 because every chrome metric is built on it at 1x
 * -- the 6 px pad plus the 1 px rule each side, the 18 px row plus its 3 px
 * gap is 21, the 32 px icon box plus its gap is 34 -- so one cell is about
 * one gap's slack: a panel never lands more than 4 px from where it was let
 * go, and two panels on grid lines are never one rule apart. 16 would visibly
 * jump a panel half a row. It scales with the chrome (16 px at 2x).
 *
 * The dock column takes EDP_DOCK_W_PERCENT of the surface's width, at least
 * EDP_DOCK_W_MIN (the 196 px the panels were authored at) and at most
 * EDP_DOCK_W_MAX (the picker's authored width). Each panel in it gets at
 * least EDP_DOCK_MIN_H (its title and about six rows; the rest scrolls). */
#define EDP_GRID 8
#define EDP_DOCK_W_PERCENT 22
#define EDP_DOCK_W_MIN 200
#define EDP_DOCK_W_MAX 400
#define EDP_DOCK_MIN_H 160
/* The tool rail: one column is the 32 px box plus 3 px of strip each side;
 * each further column adds a box and a 2 px gap (dbg_build_toolbar). */
#define EDP_RAIL_W 38
#define EDP_RAIL_PITCH 34

/** A 1x-authored layout constant at the chrome's current scale. */
static inline int
edp_px(
    const struct EditorUi* ui,
    int px)
{
    assert(ui);
    return px * ToriRSChrome_Scale(ui->chrome);
}

/** The surface's width in chrome pixels (0 before the first SetSurface). */
static inline int
edp_surface_w(const struct EditorUi* ui)
{
    assert(ui);
    return ui->chrome->surface_w;
}

/**
 * Submit `count` commands as ONE undo step and report what happened.
 * `what` names the edit for the console ("raise", "paste"). Returns how many
 * the authority accepted.
 */
int
edp_apply(
    struct EditorUi* ui,
    const struct Editor_Cmd* commands,
    int count,
    const char* what);

/** Open document (kind, key) in the mirror unless it already is. 0 when the
 *  session cannot (no editor, or the authority refused). */
int
edp_ensure_doc(
    struct EditorUi* ui,
    enum Editor_DocKind kind,
    const char* key);

/** Commands scratch shared by the modules: one tool action at a time. */
struct Editor_Cmd*
edp_scratch(int* out_capacity);

/* ---- shared actions (edpanel_actions.c) -------------------------------------
 *
 * What the menu bar, the locs panel, the rectangle panel and the world keys
 * all do, done once. Each builds commands with edpanel_ops.h, submits them as
 * one undo step, moves the selection to where the locs now are, and says what
 * happened in the console. Return the edits accepted.
 */
int
edp_locs_nudge(
    struct EditorUi* ui,
    int dx,
    int dz);

int
edp_locs_move_to(
    struct EditorUi* ui,
    int world_x,
    int world_z);

int
edp_locs_rotate(struct EditorUi* ui);

int
edp_locs_reshape(struct EditorUi* ui);

int
edp_locs_duplicate(struct EditorUi* ui);

int
edp_locs_delete(struct EditorUi* ui);

/* ---- a loc's shapes (edpanel_actions.c) -------------------------------------
 *
 * What the App's cache says loc `loc_id` can be placed as
 * (Editor_LocShapeMask), through ui->app_services.loc_record. 1 with
 * *out_mask when known; 0 when the record is not resident yet (asked for --
 * it answers a frame or two later). A window with no loc service (a
 * controller, a headless harness) answers 1 with every shape: it cannot
 * judge, and the authority accepts any shape (content ships hand-written
 * ones).
 */
int
edp_loc_shapes(
    struct EditorUi* ui,
    int loc_id,
    uint32_t* out_mask);

/** 1 when loc `loc_id` has a model for `shape`; else 0 and one console line
 *  for `what` naming the shapes it does have (or that it is still loading). */
int
edp_loc_shape_check(
    struct EditorUi* ui,
    int loc_id,
    int shape,
    const char* what);

/** Keep the Place pose on a shape the catalog's loc pick has: a pick whose
 *  shapes do not include the pose (a new pick, a relayed STATE_TOOLS, the
 *  dropdown) snaps to its first shape. Returns 1 when it moved the pose.
 *  Run every frame by the Locs panel. */
int
edp_place_pose_follow(struct EditorUi* ui);

/** The Place pose's next shape among the pick's own shapes (Shift+Q with
 *  nothing selected), and its next quarter turn (Q). */
void
edp_place_reshape(struct EditorUi* ui);

void
edp_place_rotate(struct EditorUi* ui);

/** Give every selected loc `shape` (the shape strip): only those of the
 *  selected locs that have it change; the rest are named in the console. */
int
edp_locs_set_shape(
    struct EditorUi* ui,
    int shape);

/** Swap every selected loc to the catalog's loc pick (same tile, shape and
 *  rotation; a loc whose shape the pick lacks stays, named in the console). */
int
edp_locs_swap(struct EditorUi* ui);

/** Fit every selected wall to the walls of its own loc around it
 *  (Editor_LocFitWall): one step. */
int
edp_locs_fit(struct EditorUi* ui);

/** "Fit walls to neighbours" for the Place tool (default on): the place and
 *  the ghost take the fitted shape and rotation where a wall of the picked
 *  loc stands next to the hovered tile. One per window, not relayed. */
int
edp_place_fit_enabled(void);

void
edp_place_fit_set(int on);

/** The pose a place at (world_x, world_z, level) takes: the Place pose, or
 *  with fit on the fitted wall shape and rotation. What the click places and
 *  what the ghost shows. */
void
edp_place_pose_at(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level,
    int* out_shape,
    int* out_rotation);

/** The Locs panel's shape strip, for the App to render its thumbnails: the
 *  subject loc, its shapes in strip order, and the model view to fill (one
 *  sprite, the shapes side by side). 0 when the strip is empty. */
int
Editor_Locs_ShapeStrip(
    struct EditorUi* ui,
    int* out_loc_id,
    int out_shapes[23],
    int* out_view);

/** Place `loc_id` at a world tile; the placed loc becomes the selection. A
 *  shape the loc has no model for is refused with a console line. */
int
edp_locs_place(
    struct EditorUi* ui,
    int loc_id,
    int shape,
    int rotation,
    int world_x,
    int world_z,
    int level);

/** The rectangle tool's clipboard ops. Paste lands at the rectangle's SW
 *  corner when one is latched, else at (world_x, world_z). */
int
edp_rect_copy(struct EditorUi* ui);

int
edp_rect_paste(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level);

/** Paste at exactly (world_x, world_z, level), whatever the rectangle (the
 *  Paste hotkey: the hovered tile). */
int
edp_rect_paste_at(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level);

int
edp_rect_fill(struct EditorUi* ui);

int
edp_rect_clear(struct EditorUi* ui);

/** Whether the clipboard holds tiles, and its size. */
int
edp_rect_clip_size(
    int* out_w,
    int* out_h);

/** Clear the whole square (every tile absent, every loc gone): the picker's
 *  Delete. One undo step. The square must be open. */
int
edp_square_clear(
    struct EditorUi* ui,
    int map_x,
    int map_z);

struct Editor_Picker;

/** The square picker's model (fetching the atlas if it has not yet), for the
 *  New dialog's checks. NULL when there is no picker or no atlas. */
const struct Editor_Picker*
edp_squares_picker(struct EditorUi* ui);

/* ---- the active level (edpanel_common.c) -----------------------------------
 *
 * The plane every world tool edits. `tools.edit_level` -1 (Auto) follows the
 * plane under the pointer, as the editor always has; 0..3 PINS a plane: the
 * click, the hover, the brush footprint, the ghosts, the rectangle and the
 * inspector all take that plane, and the view's level cap (`tools.vis_level`)
 * follows it, so the planes above are not drawn (roofs off) and the pinned
 * one is what the pointer meets. "Above" keeps the pin but draws every level
 * (vis cap -1) for context. The control is the menu bar's Level row
 * (edpanel_menubar.c); the keys are hotkey actions (PageUp / PageDown step,
 * Ctrl+0..3 pin, Ctrl+4 Auto, Ctrl+5 Above; edpanel_hotkeys.c). The level is shared Client state
 * (STATE_EDIT_LEVEL): another window's change lands in `edit_level` through
 * Editor_UiApplyState, and the menu bar's tick makes the cap follow it.
 */
#define EDP_LEVEL_AUTO (-1)

/** The view cap a change of the edit level from `old_edit` (with cap
 *  `old_vis`) to `new_edit` leaves: Auto draws every level (-1); a pin caps
 *  the view at itself, unless the old pin was already showing everything
 *  ("Above"), which a step keeps. */
int
edp_level_follow_vis(
    int old_edit,
    int old_vis,
    int new_edit);

/** Pin `level` (0..3) or EDP_LEVEL_AUTO: the edit level, the cap that
 *  follows it, and STATE_EDIT_LEVEL published to the Client's other windows. */
void
edp_level_set(
    struct EditorUi* ui,
    int level);

/** Step the pinned level by `delta`, clamped to 0..3. From Auto the step
 *  starts at `auto_level`, the plane under the pointer. Returns the level. */
int
edp_level_step(
    struct EditorUi* ui,
    int delta,
    int auto_level);

/** Pinned: 1 draws every level above the pin too (cap -1), 0 caps the view at
 *  the pin again. Under Auto it does nothing and returns 0. */
int
edp_level_show_above(
    struct EditorUi* ui,
    int on);

/** 1 while a level is pinned and every level is drawn. */
int
edp_level_showing_above(const struct EditorUi* ui);

/** The plane a world click or the hover edits: the pinned level, else the
 *  plane under the pointer (`hover_level`), clamped to 0..3. */
int
edp_level_click(
    const struct EditorUi* ui,
    int hover_level);

/* ---- the brush steppers and the hints (edpanel_common.c) ------------------- */

enum Edp_BrushField
{
    EDP_BRUSH_RADIUS = 0,
    EDP_BRUSH_STEP,
    EDP_BRUSH_STRENGTH,
};

/** Step one brush control by `delta` notches (radius and step by 1, strength
 *  by 5), clamped to its slider's range; the Terrain sliders follow on their
 *  next tick. Returns the new value. */
int
edp_brush_step(
    struct EditorUi* ui,
    enum Edp_BrushField field,
    int delta);

/** A menu row: `text`, a tab, and the action's chords ("Undo\tCtrl+Z"); the
 *  chrome draws the part after the tab right-aligned. `text` alone when
 *  the action is unbound. */
void
edp_hotkey_row(
    const struct EditorUi* ui,
    enum Editor_HotkeyAction action,
    const char* text,
    char* out,
    int out_size);

/** A tooltip: `text` then "  [<chords>]"; `text` alone when unbound. */
void
edp_hotkey_tip(
    const struct EditorUi* ui,
    enum Editor_HotkeyAction action,
    const char* text,
    char* out,
    int out_size);

/**
 * The chord a control wears (workbench 6.0, design section 15 surfaces 2 and
 * 3), from the live table's display spelling: `widget` (a BUTTON) wears
 * `action`'s first chord as a keycap after its caption, and its tooltip ends
 * with every chord ("Delete, Backspace") when there are more than the one it
 * wears. "" when the action is unbound. A module calls it at register and
 * again whenever ui->hotkeys.serial moves, so a rebind relabels it on the
 * next frame.
 */
void
edp_button_chord(
    struct EditorUi* ui,
    int widget,
    enum Editor_HotkeyAction action);

/** The same for an ICONBUTTON: the first chord as the badge in its icon
 *  box's corner, every chord after its tooltip. */
void
edp_iconbutton_chord(
    struct EditorUi* ui,
    int widget,
    enum Editor_HotkeyAction action);

/** Only the tooltip's chord, for a control that wears none (a slider, a
 *  command icon): every chord of `action`, "" when unbound. */
void
edp_tooltip_chord(
    struct EditorUi* ui,
    int widget,
    enum Editor_HotkeyAction action);

/** Release the scratch and the clipboard (Editor_UiFree). */
void
edp_scratch_free(void);

void
edp_actions_free(void);

#endif
