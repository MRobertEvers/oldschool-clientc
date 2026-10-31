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

/** Place `loc_id` at a world tile; the placed loc becomes the selection. */
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
 * (edpanel_menubar.c); the keys are the App's (PageUp / PageDown step,
 * Ctrl+0..3 pin, src/app/app_map_editor.c). The level is shared Client state
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

/** Release the scratch and the clipboard (Editor_UiFree). */
void
edp_scratch_free(void);

void
edp_actions_free(void);

#endif
