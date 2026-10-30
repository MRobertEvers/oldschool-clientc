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

/** Release the scratch and the clipboard (Editor_UiFree). */
void
edp_scratch_free(void);

void
edp_actions_free(void);

#endif
