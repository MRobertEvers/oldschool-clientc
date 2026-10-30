#include "editor_ui.h"

#include "editor.h"
#include "builder/builder_panels.h"
#include "iface/iface_panels.h"
#include "log/torirs_log.h"
#include "panels/edpanel_common.h"
#include "panels/edpanels.h"
#include "torirsmaped/torirs_maped.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* A view frame nobody has restated for this long is a window that closed. */
#define EDITOR_UI_FRAME_EXPIRE_MS 15000
/* Restate this window's frame this often, so a picker that joined late (or
 * expired it) gets it back. */
#define EDITOR_UI_FRAME_HEARTBEAT_MS 5000

static const char* const k_tool_names[EDITOR_TOOL_COUNT] = {
    "Select",       "Rectangle select", "Raise",        "Lower",
    "Flatten",      "Smooth",           "Paint underlay", "Paint overlay",
    "Paint flags",  "Place",            "Move",         "Rotate",
    "Delete",
};

const char*
Editor_ToolName(enum Editor_Tool tool)
{
    assert(tool >= 0);
    assert(tool < EDITOR_TOOL_COUNT);
    return k_tool_names[tool];
}

int
Editor_ToolIsBrush(enum Editor_Tool tool)
{
    return tool == EDITOR_TOOL_RAISE || tool == EDITOR_TOOL_LOWER || tool == EDITOR_TOOL_FLATTEN
           || tool == EDITOR_TOOL_SMOOTH || tool == EDITOR_TOOL_PAINT_UNDERLAY
           || tool == EDITOR_TOOL_PAINT_OVERLAY || tool == EDITOR_TOOL_PAINT_FLAGS;
}

void
Editor_UiInit(
    struct EditorUi* ui,
    struct ToriRSChrome* chrome,
    struct Editor* editor)
{
    assert(ui);
    assert(chrome);

    memset(ui, 0, sizeof(*ui));
    ui->chrome = chrome;
    ui->editor = editor;

    ui->tools.tool = EDITOR_TOOL_SELECT;
    ui->tools.brush.radius = 2;
    ui->tools.brush.falloff = EDITOR_BRUSH_FALLOFF_SMOOTH;
    ui->tools.brush.step = 2;
    ui->tools.brush.strength = 50;
    ui->tools.underlay_id = 0;
    ui->tools.overlay_none = 1;
    ui->tools.edit_level = -1;
    ui->tools.vis_level = -1;
    ui->tools.place_shape = 10;
    ui->tools.pick_kind = -1;
    ui->tools.pick_id = -1;

    /* Any number other views are unlikely to pick: the address of this
     * window's registry mixed with the clock. Positive, never 0 (0 is what a
     * publisher that sends no slot is filed under). */
    {
        uint32_t mix = (uint32_t)(uintptr_t)ui ^ (uint32_t)time(NULL) * 2654435761u;
        ui->view_slot = (int)(mix & 0x3fffffff) | 1;
    }
    ui->view_published[0] = -1;
}

void
Editor_UiFree(struct EditorUi* ui)
{
    assert(ui);

    for( int i = ui->panel_count - 1; i >= 0; i-- )
    {
        struct EditorUiPanel* panel = &ui->panels[i];
        if( panel->destroy )
            panel->destroy(ui, panel->self);
        panel->self = NULL;
    }
    ui->panel_count = 0;
    edp_scratch_free();
    edp_actions_free();
}

int
Editor_UiAddPanel(
    struct EditorUi* ui,
    const struct EditorUiPanel* panel)
{
    assert(ui);
    assert(panel);
    assert(panel->name);
    assert(ui->panel_count < EDITOR_UI_PANELS_MAX);
    assert(!Editor_UiFindPanel(ui, panel->name));

    ui->panels[ui->panel_count] = *panel;
    return ui->panel_count++;
}

struct EditorUiPanel*
Editor_UiFindPanel(
    struct EditorUi* ui,
    const char* name)
{
    assert(ui);
    assert(name);

    for( int i = 0; i < ui->panel_count; i++ )
        if( strcmp(ui->panels[i].name, name) == 0 )
            return &ui->panels[i];
    return NULL;
}

void*
Editor_UiPanelSelf(
    struct EditorUi* ui,
    const char* name)
{
    struct EditorUiPanel* panel = Editor_UiFindPanel(ui, name);
    return panel ? panel->self : NULL;
}

void
Editor_UiRegisterToolsPanels(struct EditorUi* ui)
{
    assert(ui);
    ui->panel_first = ui->chrome->panel_count;

    /* Order is the View menu's order and the dispatch order. The console goes
     * first so every later Register can already log into it; the modal pair
     * goes last so a modal's panel sits above the panels it guards. */
    Editor_Console_Register(ui);
    Editor_MenuBar_Register(ui);
    Editor_ToolRail_Register(ui);
    Editor_Terrain_Register(ui);
    Editor_Palette_Register(ui);
    Editor_RectSelect_Register(ui);
    Editor_SquarePicker_Register(ui);
    Editor_Locs_Register(ui);
    Editor_Spawns_Register(ui);
    if( ui->app )
    {
        Editor_Catalog_Register(ui);
        Editor_Inspector_Register(ui);
    }
    Editor_NewSquare_Register(ui);
    Editor_SaveGuard_Register(ui);
    /* The other phase-3 owners' modules: the interaction builders and the
     * shell editor (editor/builder/), the interface editor and the CS2 tools
     * (editor/iface/). They start hidden; the View menu opens them. The
     * views' thin toolbar is not a registry panel (editor_view.h). */
    Editor_LocBuilder_Register(ui);
    Editor_NpcBuilder_Register(ui);
    Editor_ShellEditor_Register(ui);
    Editor_Iface_Register(ui);
    Editor_Cs2_Register(ui);
    ui->panel_end = ui->chrome->panel_count;
    ui->layout_scale = ToriRSChrome_Scale(ui->chrome);
}

void
Editor_UiSetVisible(
    struct EditorUi* ui,
    int visible)
{
    assert(ui);

    ui->visible = visible ? 1 : 0;
    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct EditorUiPanel* panel = &ui->panels[i];
        if( panel->chrome_panel >= 0 && panel->title )
            ToriRSChrome_PanelSetVisible(ui->chrome, panel->chrome_panel, ui->visible);
        if( panel->set_visible )
            panel->set_visible(ui, panel->self, ui->visible);
    }
}

/* ---- view frames ----------------------------------------------------------- */

static void
ui_frames_expire(struct EditorUi* ui)
{
    int64_t const now = ui->chrome->now_ms;

    if( now <= 0 )
        return;
    for( int i = 0; i < EDITOR_UI_VIEW_FRAMES_MAX; i++ )
        if( ui->frames[i].used && ui->frames[i].slot != ui->view_slot
            && now - ui->frames[i].seen_ms > EDITOR_UI_FRAME_EXPIRE_MS )
            ui->frames[i].used = 0;
}

static void
ui_frame_note(
    struct EditorUi* ui,
    int slot,
    int map_x,
    int map_z,
    int w,
    int h)
{
    int free_at = -1;
    int oldest = 0;

    for( int i = 0; i < EDITOR_UI_VIEW_FRAMES_MAX; i++ )
    {
        if( ui->frames[i].used && ui->frames[i].slot == slot )
        {
            free_at = i;
            break;
        }
        if( !ui->frames[i].used && free_at < 0 )
            free_at = i;
        if( ui->frames[i].seen_ms < ui->frames[oldest].seen_ms )
            oldest = i;
    }
    if( free_at < 0 )
        free_at = oldest;
    ui->frames[free_at].used = 1;
    ui->frames[free_at].slot = slot;
    ui->frames[free_at].map_x = map_x;
    ui->frames[free_at].map_z = map_z;
    ui->frames[free_at].w = w > 0 ? w : 1;
    ui->frames[free_at].h = h > 0 ? h : 1;
    ui->frames[free_at].seen_ms = ui->chrome->now_ms;
}

/** This window's own frame: kept locally (it is ours whether or not the
 *  server echoes it) and published when it moved, or on the heartbeat. */
static void
ui_publish_view(struct EditorUi* ui)
{
    int map_x;
    int map_z;
    int w;
    int h;
    int64_t const now = ui->chrome->now_ms;

    if( !ui->hooks.view_window || !ui->hooks.view_window(ui->hooks_user, &map_x, &map_z, &w, &h) )
        return;
    ui_frame_note(ui, ui->view_slot, map_x, map_z, w, h);

    if( ui->view_published[0] == map_x && ui->view_published[1] == map_z
        && ui->view_published[2] == w && ui->view_published[3] == h
        && (now <= 0 || now - ui->view_published_ms < EDITOR_UI_FRAME_HEARTBEAT_MS) )
        return;

    {
        int32_t values[5] = { map_x, map_z, w, h, ui->view_slot };
        if( Editor_UiPublish(ui, TORIRSMAPED_STATE_VIEW_SQUARE, values, 5) || !ui->editor )
        {
            ui->view_published[0] = map_x;
            ui->view_published[1] = map_z;
            ui->view_published[2] = w;
            ui->view_published[3] = h;
            ui->view_published_ms = now;
        }
    }
}

void
Editor_UiPublishView(struct EditorUi* ui)
{
    assert(ui);
    ui_frames_expire(ui);
    ui_publish_view(ui);
}

/* ---- the default arrangement --------------------------------------------- */

/** Stack the visible panels named in `names` top-down at column `x`, from
 *  `y`, each below the last one's laid-out height. */
static void
ui_stack_column(
    struct EditorUi* ui,
    const char* const* names,
    int count,
    int x,
    int y)
{
    int const gap = edp_px(ui, 6);

    for( int i = 0; i < count; i++ )
    {
        struct EditorUiPanel* panel = Editor_UiFindPanel(ui, names[i]);
        struct ToriRSChromeRect rect;

        if( !panel || panel->chrome_panel < 0 || !ui->chrome->panels[panel->chrome_panel].visible )
            continue;
        ToriRSChrome_PanelMove(ui->chrome, panel->chrome_panel, x, y);
        rect = ToriRSChrome_PanelRect(ui->chrome, panel->chrome_panel);
        y += rect.h + gap;
    }
}

/**
 * The modules size and place their windows in chrome pixels of the scale at
 * registration; the chrome stores those numbers as given. When the scale
 * changes, scale every window the modules made by the same ratio, so a panel
 * built for 1x does not clip the 2x widgets drawn inside it, then arrange
 * them again.
 */
static void
ui_follow_scale(struct EditorUi* ui)
{
    int const scale = ToriRSChrome_Scale(ui->chrome);
    int const was = ui->layout_scale;

    if( was <= 0 || scale == was )
        return;
    for( int i = ui->panel_first; i < ui->panel_end && i < ui->chrome->panel_count; i++ )
    {
        struct ToriRSChromePanel const* p = &ui->chrome->panels[i];
        if( p->fixed_w > 0 )
            ToriRSChrome_PanelSetFixedWidth(ui->chrome, i, p->fixed_w * scale / was);
        if( p->fixed_h > 0 )
            ToriRSChrome_PanelSetFixedHeight(ui->chrome, i, p->fixed_h * scale / was);
        ToriRSChrome_PanelMove(ui->chrome, i, p->x * scale / was, p->y * scale / was);
    }
    ui->layout_scale = scale;
    ui->laid_out = 0;
}

/** Move the panel registered as `name` to (x, y) when there is one. */
static void
ui_place(
    struct EditorUi* ui,
    const char* name,
    int x,
    int y)
{
    struct EditorUiPanel* panel = Editor_UiFindPanel(ui, name);

    if( panel && panel->chrome_panel >= 0 )
        ToriRSChrome_PanelMove(ui->chrome, panel->chrome_panel, x, y);
}

/**
 * The panels' first positions. Each module places its window when it
 * registers, before the surface has a size and before any panel has a
 * height, so the column below each one and the right edge are guesses
 * there. Once both are known this arranges them once: the brush panel at
 * the left beside the tool rail; the catalog and the palette in the next
 * column with the output console at its foot; the picker and the inspector
 * against the right edge; and the panels that open on demand (the loc
 * selection, the rectangle tool, spawns) in a column of their own, so
 * opening one covers the world rather than another panel. A panel the user
 * drags stays put.
 */
_Static_assert(EDITOR_UI_LAYOUT_PANELS_MAX >= TORIRS_CHROME_MAX_PANELS,
    "the arrangement tracks every chrome window");

/** Remember the arrangement just made, so a later surface change can tell
 *  whether the user has rearranged anything since. */
static void
ui_layout_note(struct EditorUi* ui)
{
    ui->layout_surface_w = ui->chrome->surface_w;
    ui->layout_surface_h = ui->chrome->surface_h;
    for( int i = ui->panel_first; i < ui->panel_end && i < ui->chrome->panel_count; i++ )
    {
        ui->placed_x[i - ui->panel_first] = ui->chrome->panels[i].x;
        ui->placed_y[i - ui->panel_first] = ui->chrome->panels[i].y;
    }
}

/** The surface changed size after the arrangement: arrange again, unless a
 *  window has been dragged since (a centred modal moves by itself). */
static void
ui_follow_surface(struct EditorUi* ui)
{
    if( !ui->laid_out )
        return;
    if( ui->chrome->surface_w == ui->layout_surface_w && ui->chrome->surface_h == ui->layout_surface_h )
        return;
    for( int i = ui->panel_first; i < ui->panel_end && i < ui->chrome->panel_count; i++ )
    {
        struct ToriRSChromePanel const* p = &ui->chrome->panels[i];
        if( !p->modal
            && (p->x != ui->placed_x[i - ui->panel_first] || p->y != ui->placed_y[i - ui->panel_first]) )
        {
            ui->layout_surface_w = ui->chrome->surface_w;
            ui->layout_surface_h = ui->chrome->surface_h;
            return;
        }
    }
    ui->laid_out = 0;
}

static void
ui_default_layout(struct EditorUi* ui)
{
    static const char* const left[] = { "terrain" };
    static const char* const middle[] = { "catalog", "palette" };
    static const char* const right[] = { "squares", "inspector" };
    struct EditorUiPanel* squares;
    struct EditorUiPanel* console;
    int const top = edp_px(ui, EDP_TOP_Y);
    int const middle_x = edp_px(ui, EDP_COL_X + EDP_COL_W + 8);
    int const popup_x = edp_px(ui, EDP_COL_X + 2 * (EDP_COL_W + 8) + 4);
    int right_x;

    if( ui->laid_out || ui->chrome->surface_w <= 0 )
        return;
    squares = Editor_UiFindPanel(ui, "squares");
    if( squares && squares->chrome_panel >= 0
        && ToriRSChrome_PanelRect(ui->chrome, squares->chrome_panel).h <= 0 )
        return; /* not laid out yet: next frame */
    ui->laid_out = 1;

    right_x = edp_surface_w(ui) - edp_px(ui, EDP_PICKER_W + 8);
    /* A narrow surface (a 2x scale on a laptop window) cannot hold four
     * columns: the picker keeps the right edge, covering what it must,
     * rather than leaving the surface. */
    if( right_x < 0 )
        right_x = 0;
    ui_stack_column(ui, left, 1, edp_px(ui, EDP_COL_X), top);
    ui_stack_column(ui, middle, 2, middle_x, top);
    ui_stack_column(ui, right, 2, right_x, top);
    ui_place(ui, "locs", popup_x, top);
    ui_place(ui, "rect", popup_x, top);
    ui_place(ui, "spawns", popup_x, top);

    console = Editor_UiFindPanel(ui, "console");
    if( console && console->chrome_panel >= 0 )
    {
        int const h = ToriRSChrome_PanelRect(ui->chrome, console->chrome_panel).h;
        int y = ui->chrome->surface_h - h - edp_px(ui, 8);
        if( y < top )
            y = top;
        ToriRSChrome_PanelMove(ui->chrome, console->chrome_panel, middle_x, y);
    }
    ui_layout_note(ui);
}

/* ---- the frame ------------------------------------------------------------- */

void
Editor_UiTick(struct EditorUi* ui)
{
    struct ToriRSChromeEvent event;
    int activated;

    assert(ui);

    if( !ui->visible )
        return;
    ui_follow_scale(ui);
    ui_follow_surface(ui);
    ui_default_layout(ui);

    /* ONE activation per frame, and it is either one module's or nobody's
     * here. Unclaimed, it goes back where it was: another consumer of this
     * chrome instance later in the frame (the loc editor) may own it. */
    activated = ToriRSChrome_TakeActivated(ui->chrome);
    if( activated >= 0 )
    {
        int claimed = 0;
        for( int i = 0; i < ui->panel_count && !claimed; i++ )
            if( ui->panels[i].activate )
                claimed = ui->panels[i].activate(ui, ui->panels[i].self, activated);
        if( !claimed )
            ui->chrome->activated = activated;
    }

    while( ToriRSChrome_TakeEvent(ui->chrome, &event) )
    {
        for( int i = 0; i < ui->panel_count; i++ )
            if( ui->panels[i].event && ui->panels[i].event(ui, ui->panels[i].self, &event) )
                break;
    }
    if( ToriRSChrome_TakeEventOverflow(ui->chrome) )
        Editor_UiLog(ui, "editor: chrome event queue overflowed; some gestures were dropped");

    ui_frames_expire(ui);
    ui_publish_view(ui);

    for( int i = 0; i < ui->panel_count; i++ )
        if( ui->panels[i].tick )
            ui->panels[i].tick(ui, ui->panels[i].self);
}

void
Editor_UiApplyState(
    struct EditorUi* ui,
    uint32_t key,
    const int32_t* values,
    int count)
{
    assert(ui);
    assert(values || count == 0);

    ui->applying_state = 1;
    if( key == TORIRSMAPED_STATE_VIEW_SQUARE && count >= 2 )
    {
        int const slot = count >= 5 ? values[4] : 0;
        ui_frame_note(
            ui, slot, values[0], values[1], count >= 4 ? values[2] : 1, count >= 4 ? values[3] : 1);
    }
    else if( key == TORIRSMAPED_STATE_TOOL && count >= 1 && values[0] >= 0
             && values[0] < EDITOR_TOOL_COUNT )
        ui->tools.tool = (enum Editor_Tool)values[0];
    else if( key == TORIRSMAPED_STATE_EDIT_LEVEL && count >= 1 && values[0] >= -1 && values[0] <= 3 )
        ui->tools.edit_level = values[0];

    for( int i = 0; i < ui->panel_count; i++ )
        if( ui->panels[i].on_state )
            ui->panels[i].on_state(ui, ui->panels[i].self, key, values, count);
    ui->applying_state = 0;
}

int
Editor_UiPublish(
    struct EditorUi* ui,
    uint32_t key,
    const int32_t* values,
    int count)
{
    assert(ui);
    assert(values || count == 0);

    if( ui->applying_state || !ui->editor )
        return 0;
    return Editor_StateSet(ui->editor, key, values, count);
}

void
Editor_UiSetTool(
    struct EditorUi* ui,
    enum Editor_Tool tool)
{
    int32_t value;

    assert(ui);
    assert(tool >= 0);
    assert(tool < EDITOR_TOOL_COUNT);

    if( ui->tools.tool == tool )
        return;
    ui->tools.tool = tool;
    ui->tools.flatten_valid = 0;
    value = (int32_t)tool;
    (void)Editor_UiPublish(ui, TORIRSMAPED_STATE_TOOL, &value, 1);
}

/* ---- console --------------------------------------------------------------- */

void
Editor_UiLogV(
    struct EditorUi* ui,
    const char* format,
    va_list args)
{
    char* line;

    assert(ui);
    assert(format);

    line = ui->console[ui->console_head];
    vsnprintf(line, EDITOR_UI_CONSOLE_LINE_MAX, format, args);
    /* One line per entry: a trailing newline is the caller's habit from
     * TORIRS_LOG, not part of the message. */
    {
        size_t length = strlen(line);
        while( length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r') )
            line[--length] = '\0';
    }
    TORIRS_LOG("%s\n", line);
    ui->console_head = (ui->console_head + 1) % EDITOR_UI_CONSOLE_LINES;
    if( ui->console_count < EDITOR_UI_CONSOLE_LINES )
        ui->console_count++;
    ui->console_serial++;
}

void
Editor_UiLog(
    struct EditorUi* ui,
    const char* format,
    ...)
{
    va_list args;
    va_start(args, format);
    Editor_UiLogV(ui, format, args);
    va_end(args);
}

const char*
Editor_UiConsoleLine(
    const struct EditorUi* ui,
    int index)
{
    assert(ui);

    if( index < 0 || index >= ui->console_count )
        return NULL;
    return ui->console[(ui->console_head - 1 - index + EDITOR_UI_CONSOLE_LINES)
                       % EDITOR_UI_CONSOLE_LINES];
}

/* ---- the save guard -------------------------------------------------------- */

void
Editor_UiRunGuarded(
    struct EditorUi* ui,
    const struct EditorUiGuard* guard)
{
    assert(ui);
    assert(guard);

    switch( guard->action )
    {
    case EDITOR_UI_GUARD_OPEN_SQUARE:
        if( ui->hooks.open_square )
            ui->hooks.open_square(ui->hooks_user, guard->map_x, guard->map_z, guard->storage, 0);
        else
            Editor_UiLog(ui, "editor: this window cannot open squares");
        break;
    case EDITOR_UI_GUARD_CLOSE:
        if( ui->hooks.close_editor )
            ui->hooks.close_editor(ui->hooks_user);
        break;
    case EDITOR_UI_GUARD_NONE:
        break;
    }
}

int
Editor_UiGuarded(
    struct EditorUi* ui,
    const struct EditorUiGuard* guard)
{
    assert(ui);
    assert(guard);
    assert(guard->action != EDITOR_UI_GUARD_NONE);

    if( !ui->editor || !Editor_DocHasUnsaved(&ui->editor->doc) )
    {
        Editor_UiRunGuarded(ui, guard);
        return 1;
    }
    /* The save guard module shows its modal from its tick when this is set
     * (edpanel_saveguard.c); without that module the action is refused
     * rather than run over unsaved work. */
    ui->guard = *guard;
    if( !Editor_UiFindPanel(ui, "save_guard") )
    {
        Editor_UiLog(ui, "editor: save or undo your changes first");
        memset(&ui->guard, 0, sizeof(ui->guard));
    }
    return 0;
}

/* ---- the loc multi-selection ------------------------------------------------ */

static int
loc_ref_same(
    const struct Editor_LocRef* a,
    const struct Editor_LocRef* b)
{
    return a->map_x == b->map_x && a->map_z == b->map_z && a->level == b->level && a->x == b->x
           && a->z == b->z && a->shape == b->shape;
}

int
Editor_UiFindLoc(
    const struct EditorUi* ui,
    const struct Editor_LocRef* ref)
{
    assert(ui);
    assert(ref);

    for( int i = 0; i < ui->tools.loc_count; i++ )
        if( loc_ref_same(&ui->tools.locs[i], ref) )
            return i;
    return -1;
}

void
Editor_UiSelectLoc(
    struct EditorUi* ui,
    const struct Editor_LocRef* ref)
{
    assert(ui);

    ui->tools.loc_count = 0;
    if( ref )
    {
        ui->tools.locs[0] = *ref;
        ui->tools.loc_count = 1;
    }
}

int
Editor_UiToggleLoc(
    struct EditorUi* ui,
    const struct Editor_LocRef* ref)
{
    int at;

    assert(ui);
    assert(ref);

    at = Editor_UiFindLoc(ui, ref);
    if( at >= 0 )
    {
        memmove(
            &ui->tools.locs[at], &ui->tools.locs[at + 1],
            (size_t)(ui->tools.loc_count - at - 1) * sizeof(ui->tools.locs[0]));
        ui->tools.loc_count--;
        return 0;
    }
    if( ui->tools.loc_count >= EDITOR_LOC_SELECTION_MAX )
    {
        Editor_UiLog(ui, "editor: the selection holds %d locs at most", EDITOR_LOC_SELECTION_MAX);
        return 0;
    }
    ui->tools.locs[ui->tools.loc_count++] = *ref;
    return 1;
}
