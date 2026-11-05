/*
 * The map editor's hotkeys, the App half (docs/MAPED_HOTKEYS.md "Rules").
 *
 * The ONE place that reads an editor key. Once per frame, ahead of the
 * developer overlay and every manifest debug hotkey:
 *
 *   1. the snapshot: this frame's key edges and held keys off
 *      LibToriRS_Input, and which contexts are live (W the pointer over the
 *      World panel's body, L locs selected, R a rectangle held, S a Spawns
 *      selection with no loc, N no loc);
 *   2. the Hotkeys panel's capture, when it is waiting for a chord: the
 *      frame's keys are its, nothing else sees them;
 *   3. the gates: the editor panel open, no text field, no model view, no
 *      menu drop or minimenu open -- else nothing resolves and nothing is
 *      claimed;
 *   4. Editor_HotkeysResolve against ui->hotkeys: the chords that fire, the
 *      held actions that are active (the camera, the click accelerators), and
 *      the keys the table CLAIMS -- bound in a live context, whether or not
 *      they acted -- which app_debug_key_down / app_debug_key_held then skip
 *      (Ctrl+S saves and never also flies the camera back);
 *   5. dispatch: each fired action through the function its menu row or
 *      button calls (Editor_HotkeyRun, edpanel_hotkeys.c), the App-bound ones
 *      here; a discrete action consumes the frame (`input_frame_consumed`),
 *      its key is marked in `hotkey_consumed` (re-marked after
 *      app_ui_hotkeys clears that array), and it is logged as
 *      "editor: <action> (<chord>)".
 *
 * The camera's held flight is read by app_world_camera_keys
 * (app_editor_hotkeys_active), the click accelerators by the world click;
 * they reach the same camera movement and placement functions the debug
 * flight and the tools do.
 *
 * editor_hotkeys.ini rides the App's task queue like the dock layout
 * (task_editor_hotkeys_io.h): read at boot, written when a binding changes,
 * flushed at shutdown from app_map_editor_layout_flush.
 */

#include "app/app_internal.h"
#include "editor/editor_hotkeys.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanel_ops.h"
#include "editor/panels/edpanels.h"
#include "editor/task_editor_hotkeys_io.h"

/* One editor window per process (as g_app_editor_layout): file-static,
 * never freed, so a load in flight at shutdown still lands somewhere. */
static struct
{
    /** This frame's resolution; `valid` while it belongs to this frame. */
    struct Editor_HotkeyFrame frame;
    int valid;
    /** The pointer when this frame resolved, for the "panel under the
     *  pointer" actions (pop out, close). */
    int mouse_x;
    int mouse_y;
    /** OSRS key codes of the keys that acted (hotkey_consumed's index). */
    unsigned char consumed_osrs[TORIRS_OSRSKEY_COUNT];
    int consumed_any;

    /* editor_hotkeys.ini: 0 not started, 1 reading, 2 loaded, 3 off. */
    int io_state;
    const char* path;
    struct EditorHotkeysLoad load;
    uint32_t saved_serial;
    /** The table as the file left it: the shutdown flush writes only when a
     *  binding changed since. */
    uint32_t loaded_serial;
} g_app_hk;

static void
app_hk_snapshot(
    struct App* app,
    struct EditorUi* ui,
    struct LibToriRS_Input* input,
    struct Editor_HotkeyInput* out)
{
    struct EditorUiRect const world = Editor_UiWorldRect(ui);
    int const mx = input->curr.mouse_x;
    int const my = input->curr.mouse_y;
    struct Editor_HotkeyContextFacts facts;

    memset(out, 0, sizeof(*out));
    out->now_ms = input->curr.time;
    for( int k = TORIRSK_UNKNOWN + 1; k < TORIRSK_COUNT; k++ )
    {
        out->down[k] = LibToriRS_Input_IsKeyDown(input, (enum LibToriRS_KeyCode)k) ? 1 : 0;
        out->held[k] = out->down[k] || LibToriRS_Input_IsKeyHeld(input, (enum LibToriRS_KeyCode)k);
    }
    /* The contexts from the facts, by the one rule set the status bar's hint
     * uses too (Editor_HotkeyContextsFrom); the mask is left on the EditorUi
     * so the hint shows the chords these keys resolve in. */
    memset(&facts, 0, sizeof(facts));
    facts.tool = (int)ui->tools.tool;
    facts.pointer_in_world = Editor_UiWorldShown(ui) && mx >= world.x && my >= world.y && mx < world.x + world.w
                             && my < world.y + world.h && !app_chrome_wants_pointer(app, mx, my);
    facts.loc_count = ui->tools.loc_count;
    facts.spawn_count = Editor_Spawns_SelectionCount(ui);
    facts.loc_pick = ui->tools.pick_kind == 0 && ui->tools.pick_id >= 0;
    facts.rect_valid = ui->tools.rect.valid;
    out->contexts = Editor_HotkeyContextsFrom(&facts);
    ui->hotkey_contexts = out->contexts;
    ui->hotkey_contexts_valid = 1;
}

static void
app_hk_consume_key(
    struct App* app,
    int key)
{
    int const osrs = app_keycode_osrs(key);
    if( osrs >= 0 && osrs < TORIRS_OSRSKEY_COUNT )
    {
        g_app_hk.consumed_osrs[osrs] = 1;
        app->hotkey_consumed[osrs] = 1;
        g_app_hk.consumed_any = 1;
    }
}

/* ---- the App-bound actions ----------------------------------------------------- */

/** The hovered tile, absolute, on the edit level's plane when one is pinned. */
static int
app_hk_hover(
    struct App* app,
    int* out_x,
    int* out_z,
    int* out_level)
{
    int scene_x;
    int scene_z;

    if( !app->world || !Editor_PanelHoverTile(&app->editor_panel, app, &scene_x, &scene_z) )
        return 0;
    *out_x = app->world->_base_tile_x + scene_x;
    *out_z = app->world->_base_tile_z + scene_z;
    *out_level = Editor_PanelEditLevel(&app->editor_panel, app);
    return 1;
}

/** I: the hovered tile's underlay, overlay, shape and flags into the picks. */
static void
app_hk_pick_paint(
    struct App* app,
    struct EditorUi* ui)
{
    struct Editor_Square* square;
    struct Editor_Tile const* tile;
    int x;
    int z;
    int level;

    if( !ui->editor || !app_hk_hover(app, &x, &z, &level) )
    {
        Editor_UiLog(ui, "editor: no tile under the pointer to pick from");
        return;
    }
    square = Editor_DocFindSquare(&ui->editor->doc, x / 64, z / 64);
    if( !square || !square->loaded )
    {
        Editor_UiLog(ui, "editor: m%d_%d is not open; nothing to pick", x / 64, z / 64);
        return;
    }
    tile = &square->tiles[Editor_TileIndex(x % 64, z % 64, level)];
    ui->tools.underlay_id = tile->underlay_id;
    ui->tools.overlay_none = !tile->has_overlay;
    ui->tools.overlay_id = tile->has_overlay ? tile->overlay_id : 0;
    ui->tools.overlay_shape = tile->has_overlay ? tile->shape : 0;
    ui->tools.overlay_rotation = tile->has_overlay ? tile->rotation & 3 : 0;
    ui->tools.flags_value = tile->settings & 0x0f;
    if( tile->has_overlay )
        Editor_UiLog(ui, "editor: picked %d,%d L%d: underlay %d, overlay %d shape %d rot %d, flags %d", x, z, level,
            tile->underlay_id, tile->overlay_id, tile->shape, tile->rotation & 3, tile->settings & 0x0f);
    else
        Editor_UiLog(ui, "editor: picked %d,%d L%d: underlay %d, no overlay, flags %d", x, z, level, tile->underlay_id,
            tile->settings & 0x0f);
}

/** Shift+I: the topmost loc on the hovered tile into the catalog's pick. */
static void
app_hk_pick_loc(
    struct App* app,
    struct EditorUi* ui)
{
    struct WorldEntity_Scenery* scenery;
    int scene_x;
    int scene_z;
    int level;
    int idx;

    if( !app->world || !Editor_PanelHoverTile(&app->editor_panel, app, &scene_x, &scene_z) )
    {
        Editor_UiStatus(ui, "Pick refused: no tile under the pointer to pick from");
        return;
    }
    level = Editor_PanelEditLevel(&app->editor_panel, app);
    idx = World_SceneryFindAt(app->world, scene_x, scene_z, level, -1);
    scenery = idx >= 0 ? World_EntityPoolGet(&app->world->entities.scenery, idx) : NULL;
    if( !scenery )
    {
        Editor_UiStatus(ui, "Pick refused: no loc on the hovered tile");
        return;
    }
    if( !Editor_UiFindPanel(ui, "catalog") )
    {
        Editor_UiStatus(ui, "Pick refused: this window has no catalog to pick into");
        return;
    }
    /* Its exact pose, Find set to its name, its row in view (workbench 6.2). */
    (void)Editor_Catalog_PickFromWorld(ui, scenery->loc_id, scenery->shape, scenery->angle & 3);
}

/** Ctrl+A: every loc of the hovered tile's square on the edit level. */
static void
app_hk_select_square(
    struct App* app,
    struct EditorUi* ui)
{
    struct Editor_Square* square;
    int x;
    int z;
    int level;
    int count = 0;
    int skipped = 0;

    if( !ui->editor || !app_hk_hover(app, &x, &z, &level) )
    {
        Editor_UiLog(ui, "editor: point at a square first");
        return;
    }
    square = Editor_DocFindSquare(&ui->editor->doc, x / 64, z / 64);
    if( !square || !square->loaded )
    {
        Editor_UiLog(ui, "editor: m%d_%d is not open", x / 64, z / 64);
        return;
    }
    Editor_UiSelectLoc(ui, NULL);
    for( int i = 0; i < square->loc_count; i++ )
    {
        struct Editor_Loc const* loc = &square->locs[i];
        if( loc->level != level )
            continue;
        if( ui->tools.loc_count >= EDITOR_LOC_SELECTION_MAX )
        {
            skipped++;
            continue;
        }
        Editor_LocRefAt(&ui->tools.locs[ui->tools.loc_count], square->map_x * 64 + loc->x, square->map_z * 64 + loc->z,
            loc->level, loc->shape, loc->loc_id, loc->rotation & 3);
        ui->tools.loc_count++;
        count++;
    }
    if( ui->hooks.selection_changed )
        ui->hooks.selection_changed(ui->hooks_user);
    if( skipped )
        Editor_UiLog(ui, "editor: selected %d loc(s) of m%d_%d L%d (%d past the %d-loc selection left out)", count,
            square->map_x, square->map_z, level, skipped, EDITOR_LOC_SELECTION_MAX);
    else
        Editor_UiLog(ui, "editor: selected %d loc(s) of m%d_%d L%d", count, square->map_x, square->map_z, level);
}

/** Home: the selection (first loc, else the rectangle's centre, else the
 *  latched tile), else the hovered square's centre. Ctrl+Home: the current
 *  square's centre (the latched tile's, else the hovered, else the first
 *  open). The Areas panel's Go to camera move. */
static void
app_hk_frame(
    struct App* app,
    struct EditorUi* ui,
    int square_only)
{
    int x = -1;
    int z = -1;
    int level = ui->tools.edit_level >= 0 ? ui->tools.edit_level : 0;
    int hx;
    int hz;
    int hl;
    const char* what = NULL;

    if( !square_only && ui->tools.loc_count > 0 )
    {
        Editor_LocRefWorld(&ui->tools.locs[0], &x, &z);
        level = ui->tools.locs[0].level;
        what = "the selection";
    }
    else if( !square_only && ui->tools.rect.valid )
    {
        x = (ui->tools.rect.x0 + ui->tools.rect.x1) / 2;
        z = (ui->tools.rect.z0 + ui->tools.rect.z1) / 2;
        level = ui->tools.rect.level;
        what = "the rectangle";
    }
    else if( !square_only && ui->tools.tile_valid )
    {
        x = ui->tools.tile_x;
        z = ui->tools.tile_z;
        level = ui->tools.tile_level;
        what = "the latched tile";
    }
    else
    {
        if( square_only && ui->tools.tile_valid )
        {
            x = ui->tools.tile_x;
            z = ui->tools.tile_z;
        }
        else if( app_hk_hover(app, &hx, &hz, &hl) )
        {
            x = hx;
            z = hz;
        }
        else if( ui->editor )
            for( int i = 0; i < ui->editor->doc.square_count && x < 0; i++ )
                if( ui->editor->doc.squares[i].loaded )
                {
                    x = ui->editor->doc.squares[i].map_x * 64;
                    z = ui->editor->doc.squares[i].map_z * 64;
                }
        if( x >= 0 )
        {
            x = (x / 64) * 64 + 32;
            z = (z / 64) * 64 + 32;
        }
        what = "the square's centre";
    }
    if( x < 0 )
    {
        Editor_UiLog(ui, "editor: nothing to frame");
        return;
    }
    if( Editor_Areas_CameraTo(ui, x, z, level) )
        Editor_UiLog(ui, "editor: camera on %s (%d,%d L%d)", what, x, z, level);
    else
        Editor_UiLog(ui, "editor: %d,%d is not in the loaded scene", x, z);
}

/** Escape: a drag in progress first (the stroke closes as one undo step,
 *  the rectangle drag is dropped), then the selection. */
static void
app_hk_deselect(
    struct App* app,
    struct EditorUi* ui)
{
    struct Editor_Panel* panel = &app->editor_panel;

    if( panel->stroke_active )
    {
        ui->tools.stroke_open = 0;
        Editor_StrokeEnd(app->editor);
        panel->stroke_active = 0;
        return;
    }
    if( panel->rect_dragging )
    {
        /* A marquee too: it is the rectangle, and selects nothing now. */
        panel->rect_dragging = 0;
        panel->marquee_active = 0;
        ui->tools.rect.valid = 0;
        return;
    }
    /* A Place run in progress placed nothing yet: dropped whole. */
    if( panel->run.active || panel->marquee_pending )
    {
        edp_run_cancel(&panel->run);
        panel->marquee_pending = 0;
        Editor_UiLog(ui, "editor: drag cancelled; nothing placed");
        return;
    }
    (void)Editor_HotkeyRun(ui, EDITOR_HK_EDIT_DESELECT);
}

/** Alt+A, select same: the hovered loc's id, else (no loc under the
 *  pointer) the primary selected loc's, which the menu row also takes. */
static void
app_hk_select_same(
    struct App* app,
    struct EditorUi* ui)
{
    struct Editor_Panel* panel = &app->editor_panel;
    int hx;
    int hz;

    if( Editor_PanelHoverTile(panel, app, &hx, &hz)
        && Editor_PanelSelectSameAt(panel, app, hx, hz, Editor_PanelEditLevel(panel, app), EDITOR_LOC_SELECT_REPLACE) )
        return;
    (void)Editor_HotkeyRun(ui, EDITOR_HK_EDIT_SELECT_SAME);
}

static void
app_hk_run(
    struct App* app,
    struct EditorUi* ui,
    enum Editor_HotkeyAction action)
{
    struct Editor_Panel* panel = &app->editor_panel;
    int x;
    int z;
    int level;

    switch( action )
    {
    case EDITOR_HK_EDIT_DESELECT:
        app_hk_deselect(app, ui);
        break;
    case EDITOR_HK_EDIT_APPLY:
        Editor_PanelApplyToSelection(panel, app);
        break;
    case EDITOR_HK_PLACE_HERE:
    {
        int hx;
        int hz;
        if( Editor_PanelHoverTile(panel, app, &hx, &hz) )
            (void)Editor_PanelPlaceLocAt(panel, app, hx, hz, Editor_PanelEditLevel(panel, app));
        else
            Editor_UiLog(ui, "editor: point at the tile to place on");
        app->need_redraw = 1;
        break;
    }
    case EDITOR_HK_EDIT_SELECT_SQUARE:
        app_hk_select_square(app, ui);
        break;
    case EDITOR_HK_EDIT_SELECT_SAME:
        app_hk_select_same(app, ui);
        break;
    case EDITOR_HK_EDIT_PASTE:
        if( app_hk_hover(app, &x, &z, &level) )
            (void)edp_rect_paste_at(ui, x, z, level);
        else
            Editor_UiLog(ui, "editor: point at the tile to paste at");
        break;
    case EDITOR_HK_TOOL_PICK_PAINT:
        app_hk_pick_paint(app, ui);
        break;
    case EDITOR_HK_TOOL_PICK_LOC:
        app_hk_pick_loc(app, ui);
        break;
    case EDITOR_HK_LEVEL_DOWN:
    case EDITOR_HK_LEVEL_UP:
    {
        int const before = ui->tools.edit_level;
        level = edp_level_step(ui, action == EDITOR_HK_LEVEL_UP ? 1 : -1, Editor_PanelEditLevel(panel, app));
        if( level != before )
            Editor_UiLog(ui, "editor: level %d pinned (Ctrl+%d again: Auto)", level, level);
        break;
    }
    case EDITOR_HK_CAM_FRAME:
        app_hk_frame(app, ui, 0);
        break;
    case EDITOR_HK_CAM_FRAME_SQUARE:
        app_hk_frame(app, ui, 1);
        break;
    case EDITOR_HK_VIEW_POPOUT:
    {
        /* The panel under the pointer, else the World: a pop-out pressed
         * over the 3D view or over nothing pops the view out. */
        const char* module = Editor_UiPanelAt(ui, g_app_hk.mouse_x, g_app_hk.mouse_y);
        (void)Editor_UiPanelPopOut(ui, module ? module : "world");
        break;
    }
    case EDITOR_HK_PALETTE_SLOT_1:
    case EDITOR_HK_PALETTE_SLOT_2:
    case EDITOR_HK_PALETTE_SLOT_3:
    case EDITOR_HK_PALETTE_SLOT_4:
    case EDITOR_HK_PALETTE_SLOT_5:
    case EDITOR_HK_PALETTE_SLOT_6:
    case EDITOR_HK_PALETTE_SLOT_7:
    case EDITOR_HK_PALETTE_SLOT_8:
    case EDITOR_HK_PALETTE_SLOT_9:
    case EDITOR_HK_PALETTE_SLOT_10:
        (void)Editor_CatalogPalette_UseSlot(ui, (int)(action - EDITOR_HK_PALETTE_SLOT_1) + 1);
        break;
    case EDITOR_HK_PALETTE_ADD_PICK:
        (void)Editor_CatalogPalette_AddPick(ui, 0);
        break;
    case EDITOR_HK_PALETTE_NEXT:
        (void)Editor_CatalogPalette_Step(ui, 1);
        break;
    case EDITOR_HK_PALETTE_PREVIOUS:
        (void)Editor_CatalogPalette_Step(ui, -1);
        break;
    case EDITOR_HK_VIEW_CLOSE_PANEL:
    {
        /* A panel under the pointer is open by definition, so the View
         * toggle hides it; over no panel the key does nothing. */
        const char* module = Editor_UiPanelAt(ui, g_app_hk.mouse_x, g_app_hk.mouse_y);
        if( module )
            (void)Editor_UiTogglePanel(ui, module);
        break;
    }
    default:
        (void)Editor_HotkeyRun(ui, action);
        if( action >= EDITOR_HK_LEVEL_PIN0 && action <= EDITOR_HK_LEVEL_AUTO )
        {
            if( ui->tools.edit_level < 0 )
                Editor_UiLog(ui, "editor: level Auto (the plane under the pointer)");
            else
                Editor_UiLog(ui, "editor: level %d pinned", ui->tools.edit_level);
        }
        break;
    }
}

/* ---- editor_hotkeys.ini ------------------------------------------------------------ */

static void
app_hk_io_tick(
    struct App* app,
    struct EditorUi* ui)
{
    if( g_app_hk.io_state == 3 )
        return;
    if( g_app_hk.io_state == 0 )
    {
        g_app_hk.path = Editor_HotkeysPath();
        if( !g_app_hk.path || !app->runner.queue )
        {
            g_app_hk.io_state = 3;
            return;
        }
        ToriRS_TaskQueue_Add(app->runner.queue, CreateTask_EditorHotkeysLoad(&g_app_hk.load, g_app_hk.path));
        g_app_hk.io_state = 1;
        return;
    }
    if( g_app_hk.io_state == 1 )
    {
        if( !g_app_hk.load.done )
            return;
        if( g_app_hk.load.found )
        {
            memcpy(ui->hotkeys.bind, g_app_hk.load.hotkeys.bind, sizeof(ui->hotkeys.bind));
            if( g_app_hk.load.hotkeys.stamp_next > ui->hotkeys.stamp_next )
                ui->hotkeys.stamp_next = g_app_hk.load.hotkeys.stamp_next;
            memset(ui->hotkeys.repeat_held, 0, sizeof(ui->hotkeys.repeat_held));
            ui->hotkeys.serial++;
            Editor_UiLog(ui, "editor: hotkeys from %s (%d rebound, %d line(s) ignored)", g_app_hk.path,
                g_app_hk.load.report.applied, g_app_hk.load.report.ignored);
            for( int i = 0; i < g_app_hk.load.report.line_count; i++ )
                Editor_UiLog(ui, "editor: %s: %s", g_app_hk.path, g_app_hk.load.report.lines[i]);
        }
        g_app_hk.saved_serial = ui->hotkeys.serial;
        g_app_hk.loaded_serial = ui->hotkeys.serial;
        g_app_hk.io_state = 2;
        return;
    }
    if( ui->hotkeys.serial != g_app_hk.saved_serial )
    {
        ToriRS_TaskQueue_Add(app->runner.queue, CreateTask_EditorHotkeysSave(&ui->hotkeys, g_app_hk.path));
        g_app_hk.saved_serial = ui->hotkeys.serial;
    }
}

void
app_editor_hotkeys_flush(struct App* app)
{
    struct EditorUi* ui;
    struct ToriRS_IOBatch* io;
    struct ToriRS_Task* task;
    int guard = 0;

    assert(app);
    ui = app->editor_panel.ui;
    /* A save the tick queued last frame may still sit in the queue, which
     * is freed, not stepped: write by hand when anything changed since the
     * file was read. */
    if( g_app_hk.io_state != 2 || !ui || ui->hotkeys.serial == g_app_hk.loaded_serial )
        return;
    io = ToriRS_IOBatch_New();
    task = CreateTask_EditorHotkeysSave(&ui->hotkeys, g_app_hk.path);
    io->task = task;
    while( task_run(task, io) == PT_YIELDED && guard++ < 8 )
        Platform_IO_Process(app->runner.px, io);
    task_free(task);
    ToriRS_IOBatch_Free(io);
    g_app_hk.saved_serial = ui->hotkeys.serial;
}

/* ---- the frame ------------------------------------------------------------------ */

void
app_editor_hotkeys_frame(
    struct App* app,
    struct LibToriRS_Input* input)
{
    struct EditorUi* ui;
    struct Editor_HotkeyInput snapshot;

    assert(app);
    assert(input);
    g_app_hk.valid = 0;
    memset(g_app_hk.consumed_osrs, 0, sizeof(g_app_hk.consumed_osrs));
    g_app_hk.consumed_any = 0;
    if( !app->editor || !app->editor_panel.built || !app->editor_panel.ui )
        return;
    ui = app->editor_panel.ui;
    app_hk_io_tick(app, ui);
    if( !app->editor_panel.visible )
        return;

    app_hk_snapshot(app, ui, input, &snapshot);
    /* Shift is the multi-select modifier of a click (the picker's add-click,
     * the loc shift-click), whatever the gates say. */
    ui->shift_held = snapshot.held[TORIRSK_SHIFT] ? 1 : 0;

    /* The Hotkeys panel waiting for a chord owns the keyboard. */
    if( Editor_Hotkeys_Capturing(ui) >= 0 )
    {
        int any = 0;
        (void)Editor_Hotkeys_Feed(ui, &snapshot);
        memset(&g_app_hk.frame, 0, sizeof(g_app_hk.frame));
        for( int k = TORIRSK_UNKNOWN + 1; k < TORIRSK_COUNT; k++ )
        {
            if( !snapshot.held[k] )
                continue;
            g_app_hk.frame.claimed[k] = 1;
            app_hk_consume_key(app, k);
            any |= snapshot.down[k];
        }
        g_app_hk.valid = 1;
        if( any )
        {
            app->input_frame_consumed = 1;
            app->need_redraw = 1;
        }
        return;
    }

    if( app_text_input_focused(app) || app_modelview_focused(app) || ui->chrome->dropdown_open >= 0
        || app->frame_view->minimenu->visible )
    {
        /* Nothing resolves, but a repeater must not fire the moment the
         * gate lifts on a key that is still held from before it. */
        memset(ui->hotkeys.repeat_held, 0, sizeof(ui->hotkeys.repeat_held));
        Editor_HotkeySnapHold(ui, 0);
        return;
    }

    Editor_HotkeysResolve(&ui->hotkeys, &snapshot, &g_app_hk.frame);
    g_app_hk.valid = 1;
    g_app_hk.mouse_x = input->curr.mouse_x;
    g_app_hk.mouse_y = input->curr.mouse_y;
    for( int k = TORIRSK_UNKNOWN + 1; k < TORIRSK_COUNT; k++ )
        if( g_app_hk.frame.consumed[k] )
            app_hk_consume_key(app, k);

    for( int i = 0; i < g_app_hk.frame.count; i++ )
    {
        enum Editor_HotkeyAction const action = g_app_hk.frame.fired[i];
        struct Editor_Chord const chord = g_app_hk.frame.fired_chord[i];

        /* A repeat is not logged: twenty lines a second bury the console. */
        if( snapshot.down[chord.key] )
        {
            char text[32];
            Editor_ChordDisplay(chord, text, (int)sizeof(text));
            Editor_UiLog(ui, "editor: %s (%s)", Editor_HotkeyInfo(action)->name, text);
        }
        app_hk_run(app, ui, action);
        app->input_frame_consumed = 1;
        app->need_redraw = 1;
    }
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
        if( g_app_hk.frame.active[a] )
            app->need_redraw = 1;
    Editor_HotkeySnapHold(ui, g_app_hk.frame.active[EDITOR_HK_VIEW_SNAP_HOLD]);
}

int
app_editor_hotkeys_claimed(
    struct App const* app,
    enum LibToriRS_KeyCode key)
{
    (void)app;
    if( !g_app_hk.valid || (int)key <= TORIRSK_UNKNOWN || key >= TORIRSK_COUNT )
        return 0;
    return g_app_hk.frame.claimed[key];
}

int
app_editor_hotkeys_active(
    struct App const* app,
    enum Editor_HotkeyAction action)
{
    assert((int)action >= 0);
    assert(action < EDITOR_HK_COUNT);
    (void)app;
    return g_app_hk.valid && g_app_hk.frame.active[action];
}

int
app_editor_hotkeys_owns_camera(struct App const* app)
{
    assert(app);
    return app->editor && app->editor_panel.built && app->editor_panel.visible && app->editor_panel.ui;
}

void
app_editor_hotkeys_mark_consumed(struct App* app)
{
    assert(app);
    if( !g_app_hk.consumed_any )
        return;
    for( int k = 0; k < TORIRS_OSRSKEY_COUNT; k++ )
        if( g_app_hk.consumed_osrs[k] )
            app->hotkey_consumed[k] = 1;
}
