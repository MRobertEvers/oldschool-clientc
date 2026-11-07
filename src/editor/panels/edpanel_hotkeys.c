/*
 * Hotkeys: the editor's one action entry point and the panel that rebinds it
 * (docs/MAPED_HOTKEYS.md).
 *
 * Editor_HotkeyRun is what a File / Edit / View row, a Level button and a
 * chord all end in: each action is run here once, App-free. The App glue
 * (src/app/app_editor_hotkeys.c) resolves a frame's keys against
 * ui->hotkeys and calls it, running only the actions that need the world
 * itself (the camera, the eyedroppers, Select the square's locs, Apply to
 * selection, Paste at the hovered tile, the click accelerators).
 *
 * The panel (module "hotkeys", View > Hotkeys panel, F1; workbench 6.8,
 * design section 15 figure 5): a summary ("170 actions, 2 rebound, 0
 * conflicts"), a search box, the Group and Context filters, the changed and
 * conflicts chips, the Sort chips, Reset all, and one wide cells VLIST of
 * the actions -- name, group and context, the two chords as two capture
 * cells, the gesture. A click on a chord cell captures the next chord for
 * THAT slot (Editor_Hotkeys_Feed, fed by the App each frame): Esc cancels,
 * Backspace unbinds the slot, a modifier alone never binds (except for a
 * qualifier). A row whose chord clashes with another action's in a context
 * that can be live with it is marked in the alert ink, and so is the other.
 * A rebind bumps ui->hotkeys' serial: every menu row and tip re-labels on
 * its next tick and the App saves editor_hotkeys.ini.
 */

#include "edpanel_chips.h"
#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "edpanel_ops_stamps.h"
#include "edpanels.h"
#include "editor/builder/builder_panels.h"
#include "editor/editor_path.h"
#include "editor/live/live_panel.h"
#include "mem/torirs_mem.h"
#include "ui/torirs_chrome_scale_q.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/** The table's groups, in the order the action enum runs through them
 * (editor_hotkeys.h): Register walks the actions and opens the next group
 * at each change, so this is the enum's order, Palette after Camera. */
static const char* const k_groups[] = { "File", "Edit", "Place", "Tools", "Brush", "Level", "Camera", "Picker",
    "Palette", "View", "Graph" };
#define HOTKEYS_GROUPS ((int)(sizeof(k_groups) / sizeof(k_groups[0])))

#define HOTKEYS_CLASH_RGB 0xFF5555u
#define HOTKEYS_CAPTURE_RGB 0xFFFF00u

/* ---- the one entry point ------------------------------------------------------ */

/** The square File > Save as new copies: the picker's selection, else the
 *  first square open in the mirror. */
static int
hk_current_square(
    struct EditorUi* ui,
    char* out,
    int out_size)
{
    if( Editor_SquarePicker_SelectionKey(ui, out, out_size) )
        return 1;
    if( ui->editor )
    {
        struct Editor_Doc* doc = &ui->editor->doc;
        for( int i = 0; i < doc->square_count; i++ )
        {
            const char* storage;
            if( !doc->squares[i].loaded )
                continue;
            storage = Editor_DocSquareStorage(doc, &doc->squares[i]);
            if( storage && storage[0] )
                snprintf(out, (size_t)out_size, "%s/m%d_%d", storage, doc->squares[i].map_x, doc->squares[i].map_z);
            else
                snprintf(out, (size_t)out_size, "m%d_%d", doc->squares[i].map_x, doc->squares[i].map_z);
            return 1;
        }
    }
    return 0;
}

static void
hk_run_file(
    struct EditorUi* ui,
    enum Editor_HotkeyAction action)
{
    char key[EDITOR_DOC_KEY_MAX];

    if( action == EDITOR_HK_FILE_NEW )
        Editor_NewSquare_Open(
            ui, EDITOR_NEW_SQUARE_NEW, Editor_SquarePicker_SelectionKey(ui, key, (int)sizeof(key)) ? key : NULL);
    else if( action == EDITOR_HK_FILE_OPEN_SQUARES )
        Editor_SquarePicker_OpenSquares(ui);
    else if( action == EDITOR_HK_FILE_SAVE_ALL )
    {
        int const saved = ui->editor ? Editor_SaveAll(ui->editor) : -1;
        if( saved < 0 )
            Editor_UiLog(ui, "editor: read-only session, nothing saved");
        else
        {
            Editor_UiLog(ui, "editor: saved %d document(s) as text", saved);
            /* A scratch square is not saved but placed: the Square map asks
             * where (docs/MAPED_STATUS.md "Scratch squares"). */
            if( Editor_NewSquare_UnplacedScratch(ui, key) )
                Editor_SquarePicker_Place(ui, key, 1);
        }
    }
    else if( action == EDITOR_HK_FILE_SAVE_AS )
    {
        if( hk_current_square(ui, key, (int)sizeof(key)) )
        {
            /* A scratch square's "as new" is its Place. */
            if( strncmp(key, EDITOR_DOC_SCRATCH_STORAGE "/", sizeof(EDITOR_DOC_SCRATCH_STORAGE)) == 0 )
                Editor_SquarePicker_Place(ui, key, 1);
            else
                Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_SAVE_AS, key);
        }
        else
            Editor_UiLog(ui, "editor: open or select a square first");
    }
    else if( action == EDITOR_HK_FILE_BAKE )
    {
        if( ui->hooks.bake )
            ui->hooks.bake(ui->hooks_user);
        else
            Editor_UiLog(ui, "editor: this window cannot bake");
    }
    else if( action == EDITOR_HK_FILE_CHECK_SCRIPTS )
        (void)Editor_Triggers_Check(ui);
    else if( action == EDITOR_HK_FILE_RUN )
        (void)Editor_Live_Run(ui);
    else if( action == EDITOR_HK_FILE_PUBLISH )
        (void)Editor_Publish_Open(ui);
    else if( action == EDITOR_HK_FILE_SAVE_SCENE )
        (void)Editor_Scenes_Save(ui, NULL);
    else if( action == EDITOR_HK_FILE_SCRATCH_LANE )
        (void)Editor_Lanes_Scratch(ui);
    else
    {
        struct EditorUiGuard guard;
        assert(action == EDITOR_HK_FILE_CLOSE);
        memset(&guard, 0, sizeof(guard));
        guard.action = EDITOR_UI_GUARD_CLOSE;
        (void)Editor_UiGuarded(ui, &guard);
    }
}

/** The level a whole-square selection reads: the pinned level, else the
 *  selection's, else the latched tile's, else 0. */
static int
hk_selection_level(const struct EditorUi* ui)
{
    if( ui->tools.edit_level >= 0 )
        return ui->tools.edit_level;
    if( ui->tools.loc_count > 0 )
        return ui->tools.locs[0].level;
    if( ui->tools.tile_valid )
        return ui->tools.tile_level;
    return 0;
}

/** Add square `square`'s loc `loc` to the selection; 0 when it is full. */
static int
hk_select_add(
    struct EditorUi* ui,
    const struct Editor_Square* square,
    const struct Editor_Loc* loc)
{
    if( ui->tools.loc_count >= EDITOR_LOC_SELECTION_MAX )
        return 0;
    Editor_LocRefAt(&ui->tools.locs[ui->tools.loc_count], square->map_x * 64 + loc->x, square->map_z * 64 + loc->z,
        loc->level, loc->shape, loc->loc_id, loc->rotation & 3);
    ui->tools.loc_count++;
    return 1;
}

/** Ctrl+Shift+L: every loc of the catalog's picked loc id (else of the
 *  selection's first loc) in the open squares, on the selection's level. */
static int
hk_select_kind(struct EditorUi* ui)
{
    struct Editor_Doc* doc;
    int const level = hk_selection_level(ui);
    int loc_id = -1;
    int count = 0;
    int skipped = 0;

    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: no session to select in");
        return 0;
    }
    if( ui->tools.pick_kind == 0 && ui->tools.pick_id >= 0 )
        loc_id = ui->tools.pick_id;
    else if( ui->tools.loc_count > 0 )
        loc_id = ui->tools.locs[0].loc_id;
    if( loc_id < 0 )
    {
        Editor_UiLog(ui, "editor: pick a loc in the catalog (or select one) first");
        return 0;
    }
    doc = &ui->editor->doc;
    Editor_UiSelectLoc(ui, NULL);
    for( int i = 0; i < doc->square_count; i++ )
    {
        const struct Editor_Square* square = &doc->squares[i];
        if( !square->loaded )
            continue;
        for( int l = 0; l < square->loc_count; l++ )
        {
            const struct Editor_Loc* loc = &square->locs[l];
            if( loc->loc_id != loc_id || loc->level != level )
                continue;
            if( hk_select_add(ui, square, loc) )
                count++;
            else
                skipped++;
        }
    }
    if( ui->hooks.selection_changed )
        ui->hooks.selection_changed(ui->hooks_user);
    if( skipped )
        Editor_UiLog(ui, "editor: selected %d loc(s) of id %d on L%d (%d past the %d-loc selection left out)", count,
            loc_id, level, skipped, EDITOR_LOC_SELECTION_MAX);
    else
        Editor_UiLog(ui, "editor: selected %d loc(s) of id %d on L%d", count, loc_id, level);
    return count;
}

/** Ctrl+I: the open squares' locs on the selection's level that are NOT
 *  selected become the selection. */
static int
hk_invert_selection(struct EditorUi* ui)
{
    struct Editor_Doc* doc;
    struct Editor_LocRef was[EDITOR_LOC_SELECTION_MAX];
    int const was_count = ui->tools.loc_count;
    int const level = hk_selection_level(ui);
    int count = 0;
    int skipped = 0;

    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: no session to select in");
        return 0;
    }
    doc = &ui->editor->doc;
    memcpy(was, ui->tools.locs, (size_t)was_count * sizeof(was[0]));
    ui->tools.loc_count = 0;
    for( int i = 0; i < doc->square_count; i++ )
    {
        const struct Editor_Square* square = &doc->squares[i];
        if( !square->loaded )
            continue;
        for( int l = 0; l < square->loc_count; l++ )
        {
            const struct Editor_Loc* loc = &square->locs[l];
            struct Editor_LocRef ref;
            int selected = 0;
            if( loc->level != level )
                continue;
            Editor_LocRefAt(&ref, square->map_x * 64 + loc->x, square->map_z * 64 + loc->z, loc->level, loc->shape,
                loc->loc_id, loc->rotation & 3);
            for( int w = 0; w < was_count && !selected; w++ )
                selected = was[w].map_x == ref.map_x && was[w].map_z == ref.map_z && was[w].level == ref.level
                           && was[w].x == ref.x && was[w].z == ref.z && was[w].shape == ref.shape;
            if( selected )
                continue;
            if( hk_select_add(ui, square, loc) )
                count++;
            else
                skipped++;
        }
    }
    if( ui->hooks.selection_changed )
        ui->hooks.selection_changed(ui->hooks_user);
    if( skipped )
        Editor_UiLog(ui, "editor: inverted: %d loc(s) on L%d selected (%d past the %d-loc selection left out)", count,
            level, skipped, EDITOR_LOC_SELECTION_MAX);
    else
        Editor_UiLog(ui, "editor: inverted: %d loc(s) on L%d selected (%d deselected)", count, level, was_count);
    return count;
}

static int
hk_run_edit(
    struct EditorUi* ui,
    enum Editor_HotkeyAction action)
{
    struct EditorUiTools* tools = &ui->tools;

    switch( action )
    {
    case EDITOR_HK_EDIT_UNDO:
        Editor_UiLog(ui, "editor: undo reverted %d edit(s)", ui->editor ? Editor_Undo(ui->editor) : 0);
        return 1;
    case EDITOR_HK_EDIT_REDO:
        Editor_UiLog(ui, "editor: redo reapplied %d edit(s)", ui->editor ? Editor_Redo(ui->editor) : 0);
        return 1;
    case EDITOR_HK_EDIT_DESELECT:
        /* A drag in progress is the App's to cancel first; here the
         * selection and the rectangle go. With neither, under the Place
         * tool, the ghost goes: back to Select, the catalog pick kept (0
         * arms Place on it again). */
        if( tools->loc_count == 0 && !tools->rect.valid && tools->tool == EDITOR_TOOL_LOC_PLACE )
        {
            Editor_UiSetTool(ui, EDITOR_TOOL_SELECT);
            Editor_UiLog(ui, "editor: placing cancelled (the catalog pick is kept)");
            return 1;
        }
        tools->loc_count = 0;
        tools->rect.valid = 0;
        if( ui->hooks.selection_changed )
            ui->hooks.selection_changed(ui->hooks_user);
        return 1;
    case EDITOR_HK_EDIT_DELETE:
        /* The dialogue graph's selected node while its well holds the
         * keyboard; else the locs; else the Spawns panel's selection. */
        if( Builder_GraphHotkey(ui, EDITOR_HK_EDIT_DELETE) )
            return 1;
        if( tools->loc_count == 0 && Editor_Spawns_SelectionCount(ui) > 0 )
            (void)Editor_Spawns_DeleteSelected(ui);
        else
            (void)edp_locs_delete(ui);
        return 1;
    case EDITOR_HK_EDIT_DUPLICATE:
        if( tools->loc_count == 0 && Editor_Spawns_SelectionCount(ui) > 0 )
            (void)Editor_Spawns_Duplicate(ui);
        else
            (void)edp_locs_duplicate(ui);
        return 1;
    case EDITOR_HK_EDIT_ROTATE:
        /* The selection; else the Place ghost's pose. */
        /* The Place tool's ghost while Place is armed, whatever is
         * selected; else the selection; else the pose. */
        /* An armed stamp turns as a unit (edpanel_ops_stamps.h). */
        if( edp_stamp_armed(ui) )
            edp_stamp_turn(ui, 1);
        else if( tools->loc_count > 0 && tools->tool != EDITOR_TOOL_LOC_PLACE )
            (void)edp_locs_rotate(ui);
        else
            edp_place_rotate(ui);
        return 1;
    case EDITOR_HK_EDIT_RESHAPE:
        /* Within the loc's own shapes, the selection's or the pick's. */
        if( tools->loc_count > 0 && tools->tool != EDITOR_TOOL_LOC_PLACE )
            (void)edp_locs_reshape(ui);
        else
            edp_place_reshape(ui);
        return 1;
    case EDITOR_HK_EDIT_NUDGE_N:
        (void)edp_locs_nudge(ui, 0, 1);
        return 1;
    case EDITOR_HK_EDIT_NUDGE_S:
        (void)edp_locs_nudge(ui, 0, -1);
        return 1;
    case EDITOR_HK_EDIT_NUDGE_W:
        (void)edp_locs_nudge(ui, -1, 0);
        return 1;
    case EDITOR_HK_EDIT_NUDGE_E:
        (void)edp_locs_nudge(ui, 1, 0);
        return 1;
    /* A zone at a time (design #placing): the swap rule as a one-tile
     * nudge. */
    case EDITOR_HK_EDIT_NUDGE_FAR_N:
        (void)edp_locs_nudge(ui, 0, 8);
        return 1;
    case EDITOR_HK_EDIT_NUDGE_FAR_S:
        (void)edp_locs_nudge(ui, 0, -8);
        return 1;
    case EDITOR_HK_EDIT_NUDGE_FAR_W:
        (void)edp_locs_nudge(ui, -8, 0);
        return 1;
    case EDITOR_HK_EDIT_NUDGE_FAR_E:
        (void)edp_locs_nudge(ui, 8, 0);
        return 1;
    case EDITOR_HK_EDIT_LEVEL_UP:
        (void)edp_locs_level_move(ui, 1);
        return 1;
    case EDITOR_HK_EDIT_LEVEL_DOWN:
        (void)edp_locs_level_move(ui, -1);
        return 1;
    case EDITOR_HK_EDIT_COPY:
        (void)edp_rect_copy(ui);
        return 1;
    case EDITOR_HK_EDIT_PASTE:
        /* The menu row: at the rectangle (the App's chord pastes at the
         * hovered tile instead). */
        (void)edp_rect_paste(ui, tools->rect.x0, tools->rect.z0, tools->rect.level);
        return 1;
    case EDITOR_HK_EDIT_FILL:
        (void)edp_rect_fill(ui);
        return 1;
    case EDITOR_HK_EDIT_CLEAR:
        (void)edp_rect_clear(ui);
        return 1;
    case EDITOR_HK_EDIT_MARK_AREA:
        if( Editor_Areas_NewFromRect(ui, NULL) )
            Editor_Areas_Show(ui);
        return 1;
    case EDITOR_HK_EDIT_SELECT_KIND:
        (void)hk_select_kind(ui);
        return 1;
    case EDITOR_HK_EDIT_INVERT:
        (void)hk_invert_selection(ui);
        return 1;
    case EDITOR_HK_EDIT_SELECT_SAME:
        /* With no world to point into (a menu row, a button): the primary
         * selected loc's id. The App's chord takes the hovered loc first. */
        if( tools->loc_count == 0 )
        {
            Editor_UiStatus(ui, "Select same refused: point at a loc or select one first");
            return 1;
        }
        (void)edp_select_same(ui, tools->locs[0].loc_id, tools->locs[0].level, EDITOR_LOC_SELECT_REPLACE);
        return 1;
    default:
        /* Apply to selection and Select the square's locs need the world. */
        return 0;
    }
}

int
Editor_HotkeyRun(
    struct EditorUi* ui,
    enum Editor_HotkeyAction action)
{
    const struct Editor_HotkeyActionInfo* info;

    assert(ui);
    info = Editor_HotkeyInfo(action);
    if( action <= EDITOR_HK_FILE_OPEN_SQUARES )
    {
        hk_run_file(ui, action);
        return 1;
    }
    if( action <= EDITOR_HK_EDIT_INVERT )
        return hk_run_edit(ui, action);
    if( action == EDITOR_HK_PLACE_FIT )
    {
        edp_place_fit_set(ui, !edp_place_fit_enabled(ui));
        Editor_UiLog(ui, "editor: fit walls to neighbours %s", edp_place_fit_enabled(ui) ? "on" : "off");
        return 1;
    }
    if( action == EDITOR_HK_PLACE_SWAP )
    {
        (void)edp_locs_swap(ui);
        return 1;
    }
    /* Place at the hovered tile needs the world: the App's. */
    if( action == EDITOR_HK_PLACE_HERE )
        return 0;
    if( action >= EDITOR_HK_TOOL_SELECT && action < EDITOR_HK_TOOL_SELECT + EDITOR_TOOL_COUNT )
    {
        Editor_UiSetTool(ui, (enum Editor_Tool)(action - EDITOR_HK_TOOL_SELECT));
        return 1;
    }
    if( action == EDITOR_HK_TOOL_PREVIOUS )
    {
        Editor_UiSetTool(ui, ui->previous_tool);
        return 1;
    }
    /* With the Path tool up, [ and ] are its width, half a tile a step. */
    if( (action == EDITOR_HK_BRUSH_RADIUS_DOWN || action == EDITOR_HK_BRUSH_RADIUS_UP)
        && ui->tools.tool == EDITOR_TOOL_PAINT_PATH )
    {
        int halves = ui->tools.path_width_halves + (action == EDITOR_HK_BRUSH_RADIUS_UP ? 1 : -1);
        halves = halves < 1 ? 1 : halves > (int)(EDITOR_PATH_WIDTH_MAX * 2) ? (int)(EDITOR_PATH_WIDTH_MAX * 2) : halves;
        ui->tools.path_width_halves = halves;
        Editor_UiLog(ui, "editor: path width %d.%d tiles", halves / 2, halves % 2 ? 5 : 0);
        return 1;
    }
    /* With Plateau up, Shift+[ and Shift+] are its skirt, a tile a step. */
    if( (action == EDITOR_HK_BRUSH_STRENGTH_DOWN || action == EDITOR_HK_BRUSH_STRENGTH_UP)
        && ui->tools.tool == EDITOR_TOOL_PLATEAU )
    {
        int skirt = ui->tools.plateau_skirt + (action == EDITOR_HK_BRUSH_STRENGTH_UP ? 1 : -1);
        ui->tools.plateau_skirt = skirt < 0 ? 0 : skirt > EDITOR_PLATEAU_SKIRT_MAX ? EDITOR_PLATEAU_SKIRT_MAX : skirt;
        Editor_UiLog(ui, "editor: plateau skirt %d tile%s", ui->tools.plateau_skirt,
            ui->tools.plateau_skirt == 1 ? "" : "s");
        return 1;
    }
    /* With Paint underlay up, Shift+[ and Shift+] are its mix, ten points a step. */
    if( (action == EDITOR_HK_BRUSH_STRENGTH_DOWN || action == EDITOR_HK_BRUSH_STRENGTH_UP)
        && ui->tools.tool == EDITOR_TOOL_PAINT_UNDERLAY )
    {
        int mix = ui->tools.paint_mix + (action == EDITOR_HK_BRUSH_STRENGTH_UP ? 10 : -10);
        ui->tools.paint_mix = mix < 10 ? 10 : mix > 100 ? 100 : mix;
        Editor_UiLog(ui, "editor: underlay mix %d%%%s", ui->tools.paint_mix,
            ui->tools.paint_mix == 100 ? " (every tile)" : "");
        return 1;
    }
    /* With Scatter up, Shift+[ and Shift+] are its density, five points a step. */
    if( (action == EDITOR_HK_BRUSH_STRENGTH_DOWN || action == EDITOR_HK_BRUSH_STRENGTH_UP)
        && ui->tools.tool == EDITOR_TOOL_SCATTER )
    {
        int density = ui->tools.scatter_density + (action == EDITOR_HK_BRUSH_STRENGTH_UP ? 5 : -5);
        ui->tools.scatter_density = density < 1 ? 1 : density > 100 ? 100 : density;
        Editor_UiLog(ui, "editor: scatter density %d%%", ui->tools.scatter_density);
        return 1;
    }
    if( action >= EDITOR_HK_BRUSH_RADIUS_DOWN && action <= EDITOR_HK_BRUSH_STEP_UP )
    {
        static const enum Edp_BrushField k_field[6] = { EDP_BRUSH_RADIUS, EDP_BRUSH_RADIUS, EDP_BRUSH_STRENGTH,
                                                        EDP_BRUSH_STRENGTH, EDP_BRUSH_STEP, EDP_BRUSH_STEP };
        static const char* const k_name[3] = { "radius", "step", "strength" };
        int const at = action - EDITOR_HK_BRUSH_RADIUS_DOWN;
        int const value = edp_brush_step(ui, k_field[at], (at & 1) ? 1 : -1);
        Editor_UiLog(ui, "editor: brush %s %d", k_name[k_field[at]], value);
        return 1;
    }
    if( action == EDITOR_HK_LEVEL_DOWN || action == EDITOR_HK_LEVEL_UP )
    {
        /* From Auto the step starts at level 0 here; the App passes the plane
         * under the pointer (app_editor_hotkeys.c runs these itself). */
        (void)edp_level_step(ui, action == EDITOR_HK_LEVEL_UP ? 1 : -1, 0);
        return 1;
    }
    if( action >= EDITOR_HK_LEVEL_PIN0 && action <= EDITOR_HK_LEVEL_PIN3 )
    {
        int const level = action - EDITOR_HK_LEVEL_PIN0;
        /* The same chord again goes back to Auto. */
        edp_level_set(ui, ui->tools.edit_level == level ? EDP_LEVEL_AUTO : level);
        return 1;
    }
    if( action == EDITOR_HK_LEVEL_AUTO )
    {
        edp_level_set(ui, EDP_LEVEL_AUTO);
        return 1;
    }
    if( action == EDITOR_HK_LEVEL_ABOVE )
    {
        if( ui->tools.edit_level < 0 )
            Editor_UiStatus(ui, "Show above refused: it needs a pinned level (Alt+0..3)");
        else
            (void)edp_level_show_above(ui, !edp_level_showing_above(ui));
        return 1;
    }
    if( action == EDITOR_HK_CAM_SCHEME )
    {
        enum Editor_CameraScheme const next = ui->recent.camera_scheme == EDITOR_CAMERA_SCHEME_UNITY
                                                  ? EDITOR_CAMERA_SCHEME_DEFAULT
                                                  : EDITOR_CAMERA_SCHEME_UNITY;
        Editor_RecentSetScheme(&ui->recent, next);
        if( next == EDITOR_CAMERA_SCHEME_UNITY )
            Editor_UiLog(ui, "editor: camera scheme Unity: right-drag looks, middle-drag (or Alt+Ctrl+drag) pans, "
                             "wheel zooms to the cursor, Alt+drag orbits the selection, F frames it");
        else
            Editor_UiLog(ui, "editor: camera scheme default: WASD/RF fly, arrows turn, middle-drag orbits, "
                             "wheel dollies");
        return 1;
    }
    if( action == EDITOR_HK_VIEW_GOTO )
    {
        /* The App prefills the hovered tile; here the latched tile, else
         * the first selected loc, else nothing. */
        char prefill[32] = "";
        if( ui->tools.tile_valid )
            snprintf(prefill, sizeof(prefill), "%d,%d", ui->tools.tile_x, ui->tools.tile_z);
        else if( ui->tools.loc_count > 0 )
        {
            int x;
            int z;
            Editor_LocRefWorld(&ui->tools.locs[0], &x, &z);
            snprintf(prefill, sizeof(prefill), "%d,%d", x, z);
        }
        if( !Editor_CmdPalette_OpenWith(ui, prefill) )
            Editor_UiStatus(ui, "Go to tile refused: this window has no palette");
        return 1;
    }
    if( action >= EDITOR_HK_PICKER_NORTH && action <= EDITOR_HK_PICKER_CLEAR_FIND )
        return Editor_SquarePicker_Key(ui, (int)action);
    /* Workbench 6.8: the workspaces, the command palette, and the chords
     * kept for what other slices build (each refusal says why, R8). */
    if( action >= EDITOR_HK_VIEW_WORKSPACE_BUILD && action <= EDITOR_HK_VIEW_WORKSPACE_PLAYTEST )
    {
        (void)Editor_UiSetWorkspace(ui, (int)(action - EDITOR_HK_VIEW_WORKSPACE_BUILD));
        return 1;
    }
    if( action == EDITOR_HK_VIEW_COMMAND_PALETTE )
    {
        if( !Editor_CmdPalette_Open(ui) )
            Editor_UiStatus(ui, "Command palette refused: this window has no palette");
        return 1;
    }
    if( action >= EDITOR_HK_VIEW_LAYER_LOCS && action <= EDITOR_HK_VIEW_LAYER_GRID )
    {
        Editor_UiStatus(ui, "%s refused: the World HUD's layer toggles are not built yet (the chord is kept for them)",
            info->name);
        return 1;
    }
    /* The dialogue graph's (workbench 6.6): Builder_GraphHotkey runs them
     * on the NPC builder's graph, or says why it cannot. */
    if( action == EDITOR_HK_GRAPH_AUTO_LAYOUT || action == EDITOR_HK_GRAPH_NEW_NODE )
    {
        (void)Builder_GraphHotkey(ui, action);
        return 1;
    }
    if( info->panel )
    {
        int const open = Editor_UiTogglePanel(ui, info->panel);
        if( open < 0 )
            Editor_UiLog(ui, "editor: this window has no %s panel", info->name);
        return 1;
    }
    if( action == EDITOR_HK_VIEW_HIDE_PANELS )
    {
        (void)Editor_UiToggleAllPanels(ui);
        return 1;
    }
    if( action == EDITOR_HK_VIEW_RESET_LAYOUT )
    {
        Editor_UiResetLayout(ui);
        Editor_UiLog(ui, "editor: layout reset for %dx%d", ui->chrome->surface_w, ui->chrome->surface_h);
        return 1;
    }
    if( action == EDITOR_HK_VIEW_SNAP )
    {
        Editor_UiSetSnap(ui, !ui->snap);
        return 1;
    }
    if( action == EDITOR_HK_VIEW_COMPACT )
    {
        int const on = !Editor_UiCompact(ui);
        Editor_UiSetCompact(ui, on);
        Editor_UiLog(ui, "editor: compact panels %s (View > Reset layout returns to automatic)", on ? "on" : "off");
        return 1;
    }
    /* View > Chrome scale: Auto, then the steps in order. The host applies
     * the scale on its next frame (main.c main_chrome_scale_q). */
    if( action >= EDITOR_HK_VIEW_SCALE_AUTO && action <= EDITOR_HK_VIEW_SCALE_300 )
    {
        int const step = action - EDITOR_HK_VIEW_SCALE_100;
        Editor_UiSetChromeScalePref(
            ui, action == EDITOR_HK_VIEW_SCALE_AUTO ? EDITOR_PREFS_SCALE_AUTO : ToriRSChromeScaleQ_Step(step));
        return 1;
    }
    if( action == EDITOR_HK_VIEW_TEXT_NORMAL || action == EDITOR_HK_VIEW_TEXT_LARGE )
    {
        Editor_UiSetTextLarge(ui, action == EDITOR_HK_VIEW_TEXT_LARGE);
        return 1;
    }
    /* View > Text rendering: the host hands it to the chrome on its next
     * frame (main.c main_chrome_scale_apply), beside the scale. */
    if( action == EDITOR_HK_VIEW_TEXT_BITMAP || action == EDITOR_HK_VIEW_TEXT_SMOOTH )
    {
        Editor_UiSetTextSmooth(ui, action == EDITOR_HK_VIEW_TEXT_SMOOTH);
        return 1;
    }
    /* The camera, the eyedroppers and the click accelerators: the App's. */
    return 0;
}

/* ---- the panel ------------------------------------------------------------------
 *
 * Design section 15, figure 5 (workbench 6.8): one VLIST (wide cells) of every
 * action that passes the search and the filters, in the sort's order. A row
 * is the action's name, its group and context dimmed after it, its two chords
 * as two keycap CELLS (an empty slot is "-"), and its gesture spelling in the
 * right column. Clicking chord cell n captures the next chord into slot n
 * alone; Backspace unbinds that slot; Esc cancels. A clash with an action in
 * a context that can be live at once marks both rows "!" (the alert ink); the
 * row being captured is "*". Filters: Group and Context dropdowns, and the
 * changed / conflicts chips; Sort chips over the six columns (a second press
 * reverses). "Reset all" puts the table back.
 */

static int
hk_contains_ci(
    const char* haystack,
    const char* needle)
{
    size_t const n = strlen(needle);
    if( n == 0 )
        return 1;
    for( const char* h = haystack; *h; h++ )
    {
        size_t i = 0;
        while( i < n && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i]) )
            i++;
        if( i == n )
            return 1;
    }
    return 0;
}

static int
hk_compare_ci(
    const char* a,
    const char* b)
{
    for( ;; a++, b++ )
    {
        int const ca = tolower((unsigned char)*a);
        int const cb = tolower((unsigned char)*b);
        if( ca != cb || !ca )
            return ca - cb;
    }
}

/** Does `action` match the search text (group, name, context, chord, id,
 *  gesture)? */
static int
hk_matches(
    struct EditorUi* ui,
    int action,
    const char* search)
{
    const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(action);
    char ctx[32];
    char chords[48];

    if( !search[0] )
        return 1;
    Editor_HotkeyContextText(info->contexts, ctx, (int)sizeof(ctx));
    Editor_HotkeyDisplay(&ui->hotkeys, action, chords, (int)sizeof(chords));
    return hk_contains_ci(info->group, search) || hk_contains_ci(info->name, search) || hk_contains_ci(ctx, search)
           || hk_contains_ci(chords, search) || hk_contains_ci(info->id, search)
           || (info->gesture && hk_contains_ci(info->gesture, search));
}

static const char* const k_context_options[] = { "All contexts", "G", "W", "L", "R", "S", "N", "M", "P", "K", "Q",
    "D", "U" };
#define HOTKEYS_CONTEXT_OPTIONS ((int)(sizeof(k_context_options) / sizeof(k_context_options[0])))
_Static_assert(HOTKEYS_CONTEXT_OPTIONS == EDITOR_HK_CTX_COUNT + 1, "one context option per context, and All");

static const char* const k_sort_labels[EDITOR_HOTKEYS_COLUMNS] = { "group", "action", "context", "chord 1",
    "chord 2", "gesture" };

enum
{
    HOTKEYS_CHIP_CHANGED = 0,
    HOTKEYS_CHIP_CONFLICTS,
};

#define HOTKEYS_LIST_ROWS 20

struct HotkeysPanel
{
    int panel;
    int label_summary;
    int input_search;
    int drop_group;
    int drop_context;
    struct Edp_ChipStrip filter_strip;
    struct Edp_ChipStrip sort_strip;
    int button_reset;
    int list;
    int label_status;
    /** The group dropdown's rows: "All groups", then the table's groups. */
    const char* group_options[HOTKEYS_GROUPS + 1];
    /** The action a click armed for capture, -1 none, and the slot. */
    int capturing;
    int capture_slot;
    /** The filters and the sort. `group` -1 any; `context` an
     *  EDITOR_HK_CTX_* bit, 0 any. */
    int filter_group;
    unsigned filter_context;
    int filter_changed;
    int filter_conflicts;
    int sort_column;
    int sort_reverse;
    /** The listed actions, in order. */
    int order[EDITOR_HK_COUNT];
    int order_count;
    /** What the list was last built from, and the rows' window then. */
    uint32_t shown_serial;
    int shown_capturing;
    int shown_slot;
    uint32_t shown_filters;
    char shown_search[TORIRS_CHROME_INPUT_MAX];
    int shown_first;
    int built;
    int hover_row;
};

static struct HotkeysPanel*
hotkeys_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "hotkeys");
}

/** The sort key of `action` in `column`, for the comparison. */
static void
hk_sort_key(
    struct EditorUi* ui,
    int action,
    int column,
    char* out,
    int out_size)
{
    const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(action);
    out[0] = '\0';
    if( column == EDITOR_HOTKEYS_COL_ACTION )
        snprintf(out, (size_t)out_size, "%s", info->name);
    else if( column == EDITOR_HOTKEYS_COL_CONTEXT )
        Editor_HotkeyContextLetters(info->contexts, out, out_size);
    else if( column == EDITOR_HOTKEYS_COL_CHORD1 || column == EDITOR_HOTKEYS_COL_CHORD2 )
        Editor_ChordDisplay(ui->hotkeys.bind[action].chord[column - EDITOR_HOTKEYS_COL_CHORD1], out, out_size);
    else if( column == EDITOR_HOTKEYS_COL_GESTURE )
        snprintf(out, (size_t)out_size, "%s", info->gesture ? info->gesture : "");
}

/** Does `a` sort before `b`? Group: the table's order. Any other column:
 *  its text, case-blind, an empty one (unbound, no gesture) last whichever
 *  way the column runs; ties keep the table's order. */
static int
hk_sorts_before(
    struct EditorUi* ui,
    struct HotkeysPanel* self,
    int a,
    int b)
{
    char ka[64];
    char kb[64];
    int c;

    if( self->sort_column == EDITOR_HOTKEYS_COL_GROUP )
        return self->sort_reverse ? a > b : a < b;
    hk_sort_key(ui, a, self->sort_column, ka, (int)sizeof(ka));
    hk_sort_key(ui, b, self->sort_column, kb, (int)sizeof(kb));
    if( !ka[0] != !kb[0] )
        return ka[0] != '\0';
    c = hk_compare_ci(ka, kb);
    if( c == 0 )
        return a < b;
    return self->sort_reverse ? c > 0 : c < 0;
}

static int
hk_group_index(const char* group)
{
    for( int g = 0; g < HOTKEYS_GROUPS; g++ )
        if( strcmp(k_groups[g], group) == 0 )
            return g;
    return -1;
}

/** The listed actions: search, filters, then the sort (an insertion sort:
 *  under two hundred actions, and only when something changed). */
static void
hotkeys_order(
    struct EditorUi* ui,
    struct HotkeysPanel* self,
    const char* search)
{
    self->order_count = 0;
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(a);
        int at;
        if( !hk_matches(ui, a, search) )
            continue;
        if( self->filter_group >= 0 && hk_group_index(info->group) != self->filter_group )
            continue;
        if( self->filter_context && !(info->contexts & self->filter_context) )
            continue;
        if( self->filter_changed && Editor_HotkeysIsDefault(&ui->hotkeys, a) )
            continue;
        if( self->filter_conflicts && Editor_HotkeysClash(&ui->hotkeys, a) < 0 )
            continue;
        at = self->order_count++;
        while( at > 0 && hk_sorts_before(ui, self, a, self->order[at - 1]) )
        {
            self->order[at] = self->order[at - 1];
            at--;
        }
        self->order[at] = a;
    }
}

/** One row's cells: no well, the name, "<group> <context>", the two chords
 *  as two keycaps ("-" an empty slot, "press..." the one being captured),
 *  the gesture, and the mark. */
static void
hotkeys_row_text(
    struct EditorUi* ui,
    struct HotkeysPanel* self,
    int action,
    char* out,
    int out_size)
{
    const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(action);
    char ctx[32];
    char chord[EDITOR_HK_CHORDS][32];
    const char* mark = "";

    Editor_HotkeyContextLetters(info->contexts, ctx, (int)sizeof(ctx));
    for( int c = 0; c < EDITOR_HK_CHORDS; c++ )
    {
        Editor_ChordDisplay(ui->hotkeys.bind[action].chord[c], chord[c], (int)sizeof(chord[c]));
        if( !chord[c][0] )
            snprintf(chord[c], sizeof(chord[c]), "-");
        if( self->capturing == action && self->capture_slot == c )
            snprintf(chord[c], sizeof(chord[c]), "press...");
    }
    if( self->capturing == action )
        mark = "*";
    else if( Editor_HotkeysClash(&ui->hotkeys, action) >= 0 )
        mark = "!";
    snprintf(out, (size_t)out_size, "-%c%s%c%s %s%c%s %s%c%s%c%s", TORIRS_CHROME_CELL_SEP, info->name,
        TORIRS_CHROME_CELL_SEP, info->group, ctx, TORIRS_CHROME_CELL_SEP, chord[0], chord[1], TORIRS_CHROME_CELL_SEP,
        info->gesture ? info->gesture : "", TORIRS_CHROME_CELL_SEP, mark);
}

static void
hotkeys_status(
    struct EditorUi* ui,
    struct HotkeysPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    int pairs[2];
    int const clashes = Editor_HotkeysConflicts(&ui->hotkeys, pairs, 1);
    int rebound = 0;

    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
        rebound += !Editor_HotkeysIsDefault(&ui->hotkeys, a);
    snprintf(line, sizeof(line), "%d actions, %d rebound, %d conflict%s (%d shown)", EDITOR_HK_COUNT, rebound,
        clashes, clashes == 1 ? "" : "s", self->order_count);
    ToriRSChrome_SetText(ui->chrome, self->label_summary, line);
    ToriRSChrome_SetColor(ui->chrome, self->label_summary, clashes > 0 ? HOTKEYS_CLASH_RGB : 0);
    if( self->capturing >= 0 )
        snprintf(line, sizeof(line), "%s: press chord %d",
            Editor_HotkeyInfo(self->capturing)->name, self->capture_slot + 1);
    else
        snprintf(line, sizeof(line), "Click a chord cell to capture; Bksp unbinds it; Esc cancels");
    ToriRSChrome_SetText(ui->chrome, self->label_status, line);
    ToriRSChrome_SetColor(ui->chrome, self->label_status, self->capturing >= 0 ? HOTKEYS_CAPTURE_RGB : 0);
}

/** The chips: the two filter toggles (an x on a lit one), the sort. */
static void
hotkeys_chips(
    struct EditorUi* ui,
    struct HotkeysPanel* self)
{
    edp_chips_begin(&self->filter_strip);
    (void)edp_chips_push(&self->filter_strip, "changed", "Only the actions rebound from their default",
        self->filter_changed, self->filter_changed);
    (void)edp_chips_push(&self->filter_strip, "conflicts", "Only the actions whose chord clashes",
        self->filter_conflicts, self->filter_conflicts);
    edp_chips_end(ui, &self->filter_strip);
    edp_chips_hover(ui, &self->filter_strip);

    edp_chips_begin(&self->sort_strip);
    for( int c = 0; c < EDITOR_HOTKEYS_COLUMNS; c++ )
    {
        char label[EDP_CHIP_LABEL_MAX];
        char tip[EDP_CHIP_TIP_MAX];
        int const on = self->sort_column == c;
        snprintf(label, sizeof(label), "%s%s", k_sort_labels[c], on && self->sort_reverse ? " (desc)" : "");
        snprintf(tip, sizeof(tip), "Sort by %s (again: the other way)", k_sort_labels[c]);
        (void)edp_chips_push(&self->sort_strip, label, tip, on, 0);
    }
    edp_chips_end(ui, &self->sort_strip);
    edp_chips_hover(ui, &self->sort_strip);
}

static uint32_t
hotkeys_filter_digest(const struct HotkeysPanel* self)
{
    return (uint32_t)(self->filter_group + 1) | ((uint32_t)self->filter_context << 8)
           | ((uint32_t)self->filter_changed << 20) | ((uint32_t)self->filter_conflicts << 21)
           | ((uint32_t)self->sort_column << 22) | ((uint32_t)self->sort_reverse << 26);
}

/** The rows on screen (compare-then-set) and the hovered row's tooltip. */
static void
hotkeys_window(
    struct EditorUi* ui,
    struct HotkeysPanel* self,
    int force)
{
    int first = 0;
    int count = 0;
    int cell = 0;
    int hover;

    (void)ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &count);
    if( force || first != self->shown_first || ToriRSChrome_VListNeedsRows(ui->chrome, self->list) )
    {
        for( int r = first; r < first + count && r < self->order_count; r++ )
        {
            char text[TORIRS_CHROME_INPUT_MAX];
            hotkeys_row_text(ui, self, self->order[r], text, (int)sizeof(text));
            ToriRSChrome_VListSetRow(ui->chrome, self->list, r, text);
        }
        self->shown_first = first;
    }
    /* Hover tells (R5) and nothing is cut silently (R2): the row's tooltip
     * says the action whole, its context in words and its chords. */
    hover = ToriRSChrome_VListHoverRow(ui->chrome, self->list, &cell);
    if( hover != self->hover_row || force )
    {
        char tip[TORIRS_CHROME_LABEL_MAX];
        tip[0] = '\0';
        if( hover >= 0 && hover < self->order_count )
        {
            const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(self->order[hover]);
            int const other = Editor_HotkeysClash(&ui->hotkeys, self->order[hover]);
            if( other >= 0 )
                snprintf(tip, sizeof(tip), "Clashes with %s", Editor_HotkeyInfo(other)->name);
            else
                snprintf(tip, sizeof(tip), "%s (%s)", info->name, info->id);
        }
        ToriRSChrome_SetTooltip(ui->chrome, self->list, tip);
        self->hover_row = hover;
    }
}

static void
hotkeys_refresh(
    struct EditorUi* ui,
    struct HotkeysPanel* self)
{
    const char* search = ToriRSChrome_Text(ui->chrome, self->input_search);
    uint32_t const filters = hotkeys_filter_digest(self);

    if( !search )
        search = "";
    if( self->built && self->shown_serial == ui->hotkeys.serial && self->shown_capturing == self->capturing
        && self->shown_slot == self->capture_slot && self->shown_filters == filters
        && strcmp(self->shown_search, search) == 0 )
    {
        hotkeys_window(ui, self, 0);
        return;
    }
    hotkeys_order(ui, self, search);
    ToriRSChrome_VListSetCount(ui->chrome, self->list, self->order_count);
    hotkeys_window(ui, self, 1);
    hotkeys_status(ui, self);
    hotkeys_chips(ui, self);
    self->shown_serial = ui->hotkeys.serial;
    self->shown_capturing = self->capturing;
    self->shown_slot = self->capture_slot;
    self->shown_filters = filters;
    snprintf(self->shown_search, sizeof(self->shown_search), "%s", search);
    self->built = 1;
}

static void
hotkeys_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct HotkeysPanel* self = opaque;
    /* A capture ends when the panel closes. */
    if( self->capturing >= 0 && !ui->chrome->panels[self->panel].visible )
        self->capturing = -1;
    if( !ui->chrome->panels[self->panel].visible && self->built )
        return;
    hotkeys_refresh(ui, self);
}

static void
hotkeys_reset_all(
    struct EditorUi* ui,
    struct HotkeysPanel* self)
{
    Editor_HotkeysResetDefaults(&ui->hotkeys);
    self->capturing = -1;
    Editor_UiLog(ui, "editor: every hotkey back to its default");
    hotkeys_refresh(ui, self);
}

static int
hotkeys_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct HotkeysPanel* self = opaque;

    if( widget == self->button_reset )
    {
        hotkeys_reset_all(ui, self);
        return 1;
    }
    if( widget == self->input_search )
        return 1;
    if( widget == self->drop_group )
    {
        int const row = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        self->filter_group = row > 0 ? row - 1 : -1;
        hotkeys_refresh(ui, self);
        return 1;
    }
    if( widget == self->drop_context )
    {
        int const row = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        self->filter_context = row > 0 ? 1u << (row - 1) : 0;
        hotkeys_refresh(ui, self);
        return 1;
    }
    if( widget == self->list )
    {
        int const row = ToriRSChrome_VListSelected(ui->chrome, widget);
        int const cell = ToriRSChrome_VListPickedCell(ui->chrome, widget);
        /* A chord cell captures into its slot; the row body only selects. */
        if( row >= 0 && row < self->order_count && cell >= 1 && cell <= EDITOR_HK_CHORDS )
            Editor_Hotkeys_BeginCaptureSlot(ui, self->order[row], cell - 1);
        return 1;
    }
    return 0;
}

static int
hotkeys_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct HotkeysPanel* self = opaque;
    int on_x = 0;
    int index;

    if( event->widget == self->filter_strip.graph )
    {
        if( event->kind != TORIRS_CHROME_EVENT_GRAPH_PRESS )
            return 1;
        index = edp_chips_key_of(&self->filter_strip, event->key, &on_x);
        if( index == HOTKEYS_CHIP_CHANGED )
            self->filter_changed = on_x ? 0 : !self->filter_changed;
        else if( index == HOTKEYS_CHIP_CONFLICTS )
            self->filter_conflicts = on_x ? 0 : !self->filter_conflicts;
        hotkeys_refresh(ui, self);
        return 1;
    }
    if( event->widget == self->sort_strip.graph )
    {
        if( event->kind != TORIRS_CHROME_EVENT_GRAPH_PRESS )
            return 1;
        index = edp_chips_key_of(&self->sort_strip, event->key, &on_x);
        if( index >= 0 && index < EDITOR_HOTKEYS_COLUMNS )
            Editor_Hotkeys_SetSort(ui, index, index == self->sort_column ? !self->sort_reverse : 0);
        return 1;
    }
    return 0;
}

static void
hotkeys_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct HotkeysPanel* self = opaque;
    (void)visible;
    /* Not part of the default set: the View menu (F1) opens it. */
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
    self->capturing = -1;
}

static void
hotkeys_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

int
Editor_Hotkeys_Capturing(struct EditorUi* ui)
{
    struct HotkeysPanel* self;

    assert(ui);
    self = hotkeys_self(ui);
    return self ? self->capturing : -1;
}

int
Editor_Hotkeys_CaptureSlot(struct EditorUi* ui)
{
    struct HotkeysPanel* self;

    assert(ui);
    self = hotkeys_self(ui);
    return self && self->capturing >= 0 ? self->capture_slot : -1;
}

void
Editor_Hotkeys_BeginCapture(
    struct EditorUi* ui,
    enum Editor_HotkeyAction action)
{
    Editor_Hotkeys_BeginCaptureSlot(ui, action, 0);
}

void
Editor_Hotkeys_BeginCaptureSlot(
    struct EditorUi* ui,
    enum Editor_HotkeyAction action,
    int slot)
{
    struct HotkeysPanel* self;

    assert(ui);
    assert((int)action >= 0);
    assert(action < EDITOR_HK_COUNT);
    assert(slot >= 0);
    assert(slot < EDITOR_HK_CHORDS);
    self = hotkeys_self(ui);
    assert(self);
    self->capturing = action;
    self->capture_slot = slot;
    hotkeys_refresh(ui, self);
}

void
Editor_Hotkeys_SetSort(
    struct EditorUi* ui,
    int column,
    int reverse)
{
    struct HotkeysPanel* self;

    assert(ui);
    assert(column >= 0);
    assert(column < EDITOR_HOTKEYS_COLUMNS);
    self = hotkeys_self(ui);
    assert(self);
    self->sort_column = column;
    self->sort_reverse = reverse ? 1 : 0;
    hotkeys_refresh(ui, self);
}

void
Editor_Hotkeys_SetFilter(
    struct EditorUi* ui,
    const char* group,
    unsigned context,
    int changed,
    int conflicts)
{
    struct HotkeysPanel* self;

    assert(ui);
    self = hotkeys_self(ui);
    assert(self);
    self->filter_group = group ? hk_group_index(group) : -1;
    /* A group the table does not have is a caller's typo. */
    assert(!group || self->filter_group >= 0);
    self->filter_context = context;
    self->filter_changed = changed ? 1 : 0;
    self->filter_conflicts = conflicts ? 1 : 0;
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->drop_group, self->filter_group + 1);
    {
        int option = 0;
        for( int i = 0; i < EDITOR_HK_CTX_COUNT; i++ )
            if( context == (1u << i) )
                option = i + 1;
        ToriRSChrome_DropdownSetSelected(ui->chrome, self->drop_context, option);
    }
    hotkeys_refresh(ui, self);
}

int
Editor_Hotkeys_Rows(
    struct EditorUi* ui,
    int* out,
    int max)
{
    struct HotkeysPanel* self;

    assert(ui);
    assert(out || max == 0);
    self = hotkeys_self(ui);
    assert(self);
    hotkeys_refresh(ui, self);
    for( int i = 0; i < self->order_count && i < max; i++ )
        out[i] = self->order[i];
    return self->order_count;
}

int
Editor_Hotkeys_ListWidget(struct EditorUi* ui)
{
    struct HotkeysPanel* self;

    assert(ui);
    self = hotkeys_self(ui);
    return self ? self->list : -1;
}

void
Editor_Hotkeys_ResetAll(struct EditorUi* ui)
{
    struct HotkeysPanel* self;

    assert(ui);
    self = hotkeys_self(ui);
    assert(self);
    hotkeys_reset_all(ui, self);
}

int
Editor_Hotkeys_Feed(
    struct EditorUi* ui,
    const struct Editor_HotkeyInput* input)
{
    struct HotkeysPanel* self;
    struct Editor_Chord chord;
    unsigned mods;
    int action;
    int slot;

    assert(ui);
    assert(input);
    self = hotkeys_self(ui);
    if( !self || self->capturing < 0 )
        return 0;
    action = self->capturing;
    slot = self->capture_slot;
    mods = Editor_HotkeyHeldMods(input);
    if( input->down[TORIRSK_ESCAPE] && mods == 0 )
    {
        self->capturing = -1;
        Editor_UiLog(ui, "editor: rebinding %s cancelled", Editor_HotkeyInfo(action)->name);
    }
    else if( input->down[TORIRSK_BACKSPACE] && mods == 0 )
    {
        struct Editor_Chord const none = { 0, 0 };
        /* That slot alone: the other chord stays. */
        Editor_HotkeysSetSlot(&ui->hotkeys, action, slot, none);
        self->capturing = -1;
        Editor_UiLog(ui, "editor: %s chord %d unbound", Editor_HotkeyInfo(action)->name, slot + 1);
    }
    else if( Editor_HotkeysCapture(input, Editor_HotkeyIsQualifier(action), &chord) )
    {
        char text[32];
        int other;
        Editor_HotkeysSetSlot(&ui->hotkeys, action, slot, chord);
        self->capturing = -1;
        Editor_ChordDisplay(chord, text, (int)sizeof(text));
        other = Editor_HotkeysClash(&ui->hotkeys, action);
        if( other >= 0 )
            Editor_UiLog(ui, "editor: %s chord %d bound to %s -- clashes with %s (this binding wins until one changes)",
                Editor_HotkeyInfo(action)->name, slot + 1, text, Editor_HotkeyInfo(other)->name);
        else
            Editor_UiLog(ui, "editor: %s chord %d bound to %s", Editor_HotkeyInfo(action)->name, slot + 1, text);
    }
    hotkeys_refresh(ui, self);
    return 1;
}

void
Editor_Hotkeys_Register(struct EditorUi* ui)
{
    struct HotkeysPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;
    int group = -1;

    assert(ui);
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->capturing = -1;
    self->shown_capturing = -2;
    self->filter_group = -1;
    self->sort_column = EDITOR_HOTKEYS_COL_GROUP;
    self->shown_first = -1;
    self->hover_row = -2;

    /* The table is grouped in the doc's order, the groups in k_groups'. */
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(a);
        if( group < 0 || strcmp(k_groups[group], info->group) != 0 )
        {
            group++;
            assert(group < HOTKEYS_GROUPS);
            assert(strcmp(k_groups[group], info->group) == 0);
        }
    }
    assert(group == HOTKEYS_GROUPS - 1);
    self->group_options[0] = "All groups";
    for( int g = 0; g < HOTKEYS_GROUPS; g++ )
        self->group_options[g + 1] = k_groups[g];

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X), edp_px(ui, EDP_TOP_Y), edp_px(ui, 520),
        "Hotkeys");
    assert(self->panel >= 0);
    self->label_summary = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_search = ToriRSChrome_TextInput(chrome, self->panel, "Search", "");
    ToriRSChrome_SetTooltip(chrome, self->input_search, "Filter by group, action, context, chord or gesture");
    self->drop_group =
        ToriRSChrome_Dropdown(chrome, self->panel, "Group", self->group_options, HOTKEYS_GROUPS + 1, 0);
    self->drop_context =
        ToriRSChrome_Dropdown(chrome, self->panel, "Context", k_context_options, HOTKEYS_CONTEXT_OPTIONS, 0);
    ToriRSChrome_SetTooltip(chrome, self->drop_context, "Only the actions live in this context");
    edp_chips_init(ui, &self->filter_strip, self->panel, 1);
    edp_chips_init(ui, &self->sort_strip, self->panel, 1);
    self->button_reset = ToriRSChrome_Button(chrome, self->panel, "Reset all");
    ToriRSChrome_SetTooltip(chrome, self->button_reset, "Every action back to the default chords");
    self->list = ToriRSChrome_VListCellsWide(chrome, self->panel,
        "-" TORIRS_CHROME_CELL_SEP_STR "action" TORIRS_CHROME_CELL_SEP_STR "group, context" TORIRS_CHROME_CELL_SEP_STR
        "chord 1  chord 2" TORIRS_CHROME_CELL_SEP_STR "gesture",
        HOTKEYS_LIST_ROWS, EDITOR_HK_COUNT);
    assert(self->list >= 0);
    self->label_status = ToriRSChrome_Label(chrome, self->panel, "");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "hotkeys";
    desc.title = "Hotkeys";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = hotkeys_tick;
    desc.activate = hotkeys_activate;
    desc.event = hotkeys_event;
    desc.set_visible = hotkeys_set_visible;
    desc.destroy = hotkeys_destroy;
    Editor_UiAddPanel(ui, &desc);
}

/*
 * View > Snap to grid is the toggle the layout file remembers; the hold key
 * (view.snap_hold) forces the chrome's snap for as long as it is down without
 * touching that toggle, so the saved layout keeps the user's setting and a
 * one-off aligned drop costs one held key. Re-applied only when the
 * effective value changes: ToriRSChrome_SetSnap is cheap, but the pattern of
 * writing a setting every frame is what hides a stuck key.
 */
void
Editor_HotkeySnapHold(
    struct EditorUi* ui,
    int held)
{
    static int last_effective = -1;
    int effective;

    assert(ui);
    effective = (ui->snap || held) ? 1 : 0;
    if( effective == last_effective )
        return;
    last_effective = effective;
    ToriRSChrome_SetSnap(
        ui->chrome, effective ? edp_px(ui, EDP_GRID) : 0, effective ? edp_px(ui, EDP_GRID) : 0);
}
