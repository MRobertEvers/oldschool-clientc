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
 * The panel (module "hotkeys", View > Hotkeys panel, F1): a search box, the
 * capture prompt, Reset to defaults, then one TREE row per action under its
 * group's header -- "<name> (<context>)", a tab, and the chords, which the
 * chrome draws right-aligned. A click on a row captures the next chord for
 * it (Editor_Hotkeys_Feed, fed by the App each frame): Esc cancels,
 * Backspace unbinds, a modifier alone never binds (except for Fly faster).
 * A row whose chord clashes with another action's in a context that can be
 * live with it is red, and so is the other. A rebind bumps ui->hotkeys'
 * serial: every menu row and tip re-labels on its next tick and the App
 * saves editor_hotkeys.ini.
 */

#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "edpanels.h"
#include "editor/live/live_panel.h"
#include "mem/torirs_mem.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/** The doc's tables, in order. */
static const char* const k_groups[] = { "File", "Edit", "Place", "Palette", "Tools", "Brush", "Level", "Camera", "View" };
#define HOTKEYS_GROUPS ((int)(sizeof(k_groups) / sizeof(k_groups[0])))

#define HOTKEYS_CLASH_RGB 0xFF5555u
#define HOTKEYS_CAPTURE_RGB 0xFFFF00u

struct HotkeysPanel
{
    int panel;
    int input_search;
    int label_status;
    int button_reset;
    int group_rows[HOTKEYS_GROUPS];
    int rows[EDITOR_HK_COUNT];
    /** The action a click armed for capture, -1 none. */
    int capturing;
    /** What the rows were last built from. */
    uint32_t shown_serial;
    int shown_capturing;
    char shown_search[TORIRS_CHROME_INPUT_MAX];
    int built;
};

static struct HotkeysPanel*
hotkeys_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "hotkeys");
}

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
    {
        int map_x = -1;
        int map_z = -1;
        (void)Editor_SquarePicker_EmptyCell(ui, &map_x, &map_z);
        Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_NEW,
            Editor_SquarePicker_SelectionKey(ui, key, (int)sizeof(key)) ? key : NULL, map_x, map_z);
    }
    else if( action == EDITOR_HK_FILE_SAVE_ALL )
    {
        int const saved = ui->editor ? Editor_SaveAll(ui->editor) : -1;
        if( saved < 0 )
            Editor_UiLog(ui, "editor: read-only session, nothing saved");
        else
            Editor_UiLog(ui, "editor: saved %d document(s) as text", saved);
    }
    else if( action == EDITOR_HK_FILE_SAVE_AS )
    {
        if( hk_current_square(ui, key, (int)sizeof(key)) )
            Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_SAVE_AS, key, -1, -1);
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
    else
    {
        struct EditorUiGuard guard;
        assert(action == EDITOR_HK_FILE_CLOSE);
        memset(&guard, 0, sizeof(guard));
        guard.action = EDITOR_UI_GUARD_CLOSE;
        (void)Editor_UiGuarded(ui, &guard);
    }
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
        /* The locs; else the Spawns panel's selection. */
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
        if( tools->loc_count > 0 && tools->tool != EDITOR_TOOL_LOC_PLACE )
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
    if( action <= EDITOR_HK_FILE_PUBLISH )
    {
        hk_run_file(ui, action);
        return 1;
    }
    if( action <= EDITOR_HK_EDIT_MARK_AREA )
        return hk_run_edit(ui, action);
    if( action == EDITOR_HK_PLACE_FIT )
    {
        edp_place_fit_set(!edp_place_fit_enabled());
        Editor_UiLog(ui, "editor: fit walls to neighbours %s", edp_place_fit_enabled() ? "on" : "off");
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
            Editor_UiLog(ui, "editor: Above needs a pinned level (Ctrl+0..3)");
        else
            (void)edp_level_show_above(ui, !edp_level_showing_above(ui));
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
    /* The camera, the eyedroppers and the click accelerators: the App's. */
    return 0;
}

/* ---- the panel ------------------------------------------------------------------ */

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

/** Does `action` match the search text (group, name, context, chord, id)? */
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
           || hk_contains_ci(chords, search) || hk_contains_ci(info->id, search);
}

static void
hotkeys_status(
    struct EditorUi* ui,
    struct HotkeysPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    if( self->capturing >= 0 )
        snprintf(line, sizeof(line), "%s: press a chord... Esc cancels, Backspace unbinds",
            Editor_HotkeyInfo(self->capturing)->name);
    else
    {
        int pairs[2];
        int const clashes = Editor_HotkeysConflicts(&ui->hotkeys, pairs, 1);
        if( clashes > 0 )
            snprintf(line, sizeof(line), "%d clash(es) in red; click a chord to rebind", clashes);
        else
            snprintf(line, sizeof(line), "Click a chord to rebind it");
    }
    ToriRSChrome_SetText(ui->chrome, self->label_status, line);
    ToriRSChrome_SetColor(ui->chrome, self->label_status, self->capturing >= 0 ? HOTKEYS_CAPTURE_RGB : 0);
}

static void
hotkeys_rows(
    struct EditorUi* ui,
    struct HotkeysPanel* self,
    const char* search)
{
    int group_shown[HOTKEYS_GROUPS];

    memset(group_shown, 0, sizeof(group_shown));
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(a);
        char ctx[32];
        char chords[48];
        char label[TORIRS_CHROME_LABEL_MAX];
        int const shown = hk_matches(ui, a, search);
        uint32_t colour = 0;

        Editor_HotkeyContextText(info->contexts, ctx, (int)sizeof(ctx));
        Editor_HotkeyDisplay(&ui->hotkeys, a, chords, (int)sizeof(chords));
        if( self->capturing == a )
        {
            snprintf(label, sizeof(label), "%s (%s)\tpress a chord...", info->name, ctx);
            colour = HOTKEYS_CAPTURE_RGB;
        }
        else
        {
            snprintf(label, sizeof(label), "%s (%s)\t%s", info->name, ctx, chords[0] ? chords : "unbound");
            if( Editor_HotkeysClash(&ui->hotkeys, a) >= 0 )
                colour = HOTKEYS_CLASH_RGB;
        }
        ToriRSChrome_SetLabel(ui->chrome, self->rows[a], label);
        ToriRSChrome_SetColor(ui->chrome, self->rows[a], colour);
        ToriRSChrome_SetHidden(ui->chrome, self->rows[a], !shown);
        for( int g = 0; g < HOTKEYS_GROUPS; g++ )
            if( shown && strcmp(info->group, k_groups[g]) == 0 )
                group_shown[g] = 1;
    }
    for( int g = 0; g < HOTKEYS_GROUPS; g++ )
        ToriRSChrome_SetHidden(ui->chrome, self->group_rows[g], !group_shown[g]);
}

static void
hotkeys_refresh(
    struct EditorUi* ui,
    struct HotkeysPanel* self)
{
    const char* search = ToriRSChrome_Text(ui->chrome, self->input_search);
    if( !search )
        search = "";
    if( self->built && self->shown_serial == ui->hotkeys.serial && self->shown_capturing == self->capturing
        && strcmp(self->shown_search, search) == 0 )
        return;
    hotkeys_rows(ui, self, search);
    hotkeys_status(ui, self);
    self->shown_serial = ui->hotkeys.serial;
    self->shown_capturing = self->capturing;
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

static int
hotkeys_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct HotkeysPanel* self = opaque;

    if( widget == self->button_reset )
    {
        Editor_HotkeysResetDefaults(&ui->hotkeys);
        self->capturing = -1;
        Editor_UiLog(ui, "editor: every hotkey back to its default");
        hotkeys_refresh(ui, self);
        return 1;
    }
    if( widget == self->input_search )
        return 1;
    for( int g = 0; g < HOTKEYS_GROUPS; g++ )
        if( widget == self->group_rows[g] )
            return 1;
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
        if( widget == self->rows[a] )
        {
            Editor_Hotkeys_BeginCapture(ui, a);
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

void
Editor_Hotkeys_BeginCapture(
    struct EditorUi* ui,
    enum Editor_HotkeyAction action)
{
    struct HotkeysPanel* self;

    assert(ui);
    assert((int)action >= 0);
    assert(action < EDITOR_HK_COUNT);
    self = hotkeys_self(ui);
    assert(self);
    self->capturing = action;
    hotkeys_refresh(ui, self);
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

    assert(ui);
    assert(input);
    self = hotkeys_self(ui);
    if( !self || self->capturing < 0 )
        return 0;
    action = self->capturing;
    mods = Editor_HotkeyHeldMods(input);
    if( input->down[TORIRSK_ESCAPE] && mods == 0 )
    {
        self->capturing = -1;
        Editor_UiLog(ui, "editor: rebinding %s cancelled", Editor_HotkeyInfo(action)->name);
    }
    else if( input->down[TORIRSK_BACKSPACE] && mods == 0 )
    {
        Editor_HotkeysSet(&ui->hotkeys, action, NULL, 0);
        self->capturing = -1;
        Editor_UiLog(ui, "editor: %s unbound", Editor_HotkeyInfo(action)->name);
    }
    else if( Editor_HotkeysCapture(input,
                                   action == EDITOR_HK_CAM_FAST || action == EDITOR_HK_VIEW_SNAP_HOLD,
                                   &chord) )
    {
        char text[32];
        int other;
        Editor_HotkeysSet(&ui->hotkeys, action, &chord, 1);
        self->capturing = -1;
        Editor_ChordDisplay(chord, text, (int)sizeof(text));
        other = Editor_HotkeysClash(&ui->hotkeys, action);
        if( other >= 0 )
            Editor_UiLog(ui, "editor: %s bound to %s -- clashes with %s (this binding wins until one changes)",
                Editor_HotkeyInfo(action)->name, text, Editor_HotkeyInfo(other)->name);
        else
            Editor_UiLog(ui, "editor: %s bound to %s", Editor_HotkeyInfo(action)->name, text);
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

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X), edp_px(ui, EDP_TOP_Y), edp_px(ui, 360),
        "Hotkeys");
    assert(self->panel >= 0);
    self->input_search = ToriRSChrome_TextInput(chrome, self->panel, "Search", "");
    ToriRSChrome_SetTooltip(chrome, self->input_search, "Filter by group, name, context or chord");
    self->label_status = ToriRSChrome_Label(chrome, self->panel, "");
    self->button_reset = ToriRSChrome_Button(chrome, self->panel, "Reset to defaults");
    ToriRSChrome_SetTooltip(chrome, self->button_reset, "Every action back to the default chords");
    for( int g = 0; g < HOTKEYS_GROUPS; g++ )
        self->group_rows[g] = -1;
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(a);
        if( group < 0 || strcmp(k_groups[group], info->group) != 0 )
        {
            group++;
            assert(group < HOTKEYS_GROUPS);
            /* The table is grouped in the doc's order. */
            assert(strcmp(k_groups[group], info->group) == 0);
            self->group_rows[group] =
                ToriRSChrome_TreeRow(chrome, self->panel, k_groups[group], 0, TORIRS_CHROME_TREE_OPEN);
            assert(self->group_rows[group] >= 0);
        }
        self->rows[a] = ToriRSChrome_TreeRow(chrome, self->panel, info->name, 1, TORIRS_CHROME_TREE_LEAF);
        assert(self->rows[a] >= 0);
    }
    assert(group == HOTKEYS_GROUPS - 1);
    ToriRSChrome_PanelSetFixedHeight(chrome, self->panel, edp_px(ui, 420));
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "hotkeys";
    desc.title = "Hotkeys";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = hotkeys_tick;
    desc.activate = hotkeys_activate;
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
