/*
 * File / Edit / View across the top of the window.
 *
 * Row order is the dispatch contract: each menu's rows and its handler sit
 * next to each other below, and the handler dispatches on the row's index.
 * The View menu lists every registered panel that has a title -- other
 * owners' panels included -- so it is filled on the first tick, after every
 * Register has run.
 *
 * After the menus, the ACTIVE LEVEL row: Auto 0 1 2 3 Above. Always on screen
 * (the bar is), never over the world. The pressed button is the edit level;
 * Above (only while a level is pinned) draws the levels over the pin too. It
 * writes the shared tool state through edp_level_* (edpanel_common.h), so the
 * terrain panel's radios, the view cap and the other windows follow; and it
 * follows them back -- a level pinned by a key, the terrain panel or another
 * window (STATE_EDIT_LEVEL) presses here on the next tick, and the World
 * panel's title names it ("World - level 2").
 */

#include "edpanel_common.h"
#include "edpanel_picker.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"
#include "ui/uitree_layout.h"

#include <stdio.h>
#include <string.h>

/* The File and Edit rows, in order: each row IS a hotkey action, so a row
 * and its chord run the same Editor_HotkeyRun (edpanel_hotkeys.c), and the
 * row's label is regenerated from the table (the chord right-aligned after a
 * tab) whenever a binding changes. */
static const enum Editor_HotkeyAction k_file_actions[] = {
    EDITOR_HK_FILE_NEW, EDITOR_HK_FILE_SAVE_ALL,     EDITOR_HK_FILE_SAVE_AS, EDITOR_HK_FILE_BAKE,
    EDITOR_HK_FILE_CHECK_SCRIPTS, EDITOR_HK_FILE_RUN, EDITOR_HK_FILE_PUBLISH, EDITOR_HK_FILE_CLOSE,
};
#define FILE_COUNT ((int)(sizeof(k_file_actions) / sizeof(k_file_actions[0])))
/* After the action rows: "Recent squares" (a header row; a click reopens
 * the newest), then one row per remembered square, newest first
 * (editor_recent.h). */
#define FILE_RECENT_HEADER FILE_COUNT
#define FILE_ROWS_MAX (FILE_COUNT + 1 + EDITOR_RECENT_MAX)

static const enum Editor_HotkeyAction k_edit_actions[] = {
    EDITOR_HK_EDIT_UNDO,     EDITOR_HK_EDIT_REDO,   EDITOR_HK_EDIT_DESELECT, EDITOR_HK_EDIT_DELETE,
    EDITOR_HK_EDIT_DUPLICATE, EDITOR_HK_EDIT_ROTATE, EDITOR_HK_EDIT_RESHAPE, EDITOR_HK_EDIT_COPY,
    EDITOR_HK_EDIT_PASTE,    EDITOR_HK_EDIT_FILL,   EDITOR_HK_EDIT_CLEAR,    EDITOR_HK_EDIT_MARK_AREA,
};
#define EDIT_COUNT ((int)(sizeof(k_edit_actions) / sizeof(k_edit_actions[0])))

/* After the panel rows: the layout's own two. */
enum
{
    VIEW_HIDE = 0,
    VIEW_COMPACT,
    VIEW_RESET_LAYOUT,
    VIEW_SNAP,
    VIEW_LAYOUT_COUNT
};

#define MENUBAR_VIEW_MAX (EDITOR_UI_PANELS_MAX + VIEW_LAYOUT_COUNT)

/* The Level row: Auto, then one button per plane. */
#define MENUBAR_LEVEL_BUTTONS (EDITOR_SQUARE_LEVELS + 1)
static const char* const k_level_buttons[MENUBAR_LEVEL_BUTTONS] = { "Auto", "0", "1", "2", "3" };
static const char* const k_level_tips[MENUBAR_LEVEL_BUTTONS] = {
    "Auto: edit the level under the pointer",
    "Pin level 0, hide the levels above",
    "Pin level 1, hide the levels above",
    "Pin level 2, hide the levels above",
    "Pin level 3",
};
static const enum Editor_HotkeyAction k_level_actions[MENUBAR_LEVEL_BUTTONS] = {
    EDITOR_HK_LEVEL_AUTO, EDITOR_HK_LEVEL_PIN0, EDITOR_HK_LEVEL_PIN1, EDITOR_HK_LEVEL_PIN2, EDITOR_HK_LEVEL_PIN3,
};

struct MenuBarPanel
{
    int panel;
    int menu_file;
    int menu_edit;
    int menu_view;
    /** The View rows: panel titles, and which registry index each toggles. */
    char view_labels[MENUBAR_VIEW_MAX][TORIRS_CHROME_LABEL_MAX];
    const char* view_rows[MENUBAR_VIEW_MAX];
    int view_targets[MENUBAR_VIEW_MAX];
    int view_count;
    /** Rows that are panels; the layout rows follow them. */
    int view_panel_count;
    int view_built_for;
    int view_built_snap;
    /** Editor_UiCompact and compact_mode the Compact row was labelled from. */
    int view_built_compact;
    /** menubar_view_state() the rows were labelled from: a panel shown,
     *  closed or popped out relabels them. */
    uint32_t view_built_state;
    /** The File and Edit rows' labels (the dropdowns borrow them). */
    char file_labels[FILE_ROWS_MAX][TORIRS_CHROME_LABEL_MAX];
    const char* file_rows[FILE_ROWS_MAX];
    int file_count;
    /** ui->recent.serial the File rows were made from. */
    uint32_t recent_serial;
    char edit_labels[EDIT_COUNT][TORIRS_CHROME_LABEL_MAX];
    const char* edit_rows[EDIT_COUNT];
    /** ui->hotkeys.serial every label and tip was made from. */
    uint32_t hint_serial;
    /** The Level row (menubar_level_show). */
    int label_level;
    int level_buttons[MENUBAR_LEVEL_BUTTONS];
    int button_above;
    /** What the row and the World title were last set from; -2 = never. */
    int shown_level;
    int shown_vis;
};

/** A digest of which View-menu panels are open and which popped out: the
 *  rows say both. The registry, not the UI tree. */
static uint32_t
menubar_view_state(const struct EditorUi* ui)
{
    uint32_t h = 2166136261u;
    for( int i = 0; i < ui->panel_count; i++ )
    {
        int const panel = ui->panels[i].chrome_panel;
        int const open = panel >= 0 && ui->chrome->panels[panel].visible;
        h = (h ^ (uint32_t)(open | (ui->popped[i] << 1))) * 16777619u;
    }
    return h;
}

/**
 * A panel row's right-hand column: what is not open says so before the
 * chord ("Name\thidden  F3", "Name\tpopped out"), so the row still starts
 * with the panel's name and a closed panel is told from an open one.
 */
static void
menubar_mark_row(
    char* label,
    int size,
    const char* state)
{
    char const* tab = strchr(label, '\t');
    char row[TORIRS_CHROME_LABEL_MAX];

    if( tab )
        snprintf(row, sizeof(row), "%.*s\t%s  %s", (int)(tab - label), label, state, tab + 1);
    else
        snprintf(row, sizeof(row), "%s\t%s", label, state);
    snprintf(label, (size_t)size, "%s", row);
}

static void
menubar_build_view(
    struct EditorUi* ui,
    struct MenuBarPanel* self)
{
    self->view_count = 0;
    for( int i = 0; i < ui->panel_count && self->view_count < MENUBAR_VIEW_MAX - VIEW_LAYOUT_COUNT; i++ )
    {
        const struct EditorUiPanel* panel = &ui->panels[i];
        int action;
        if( !panel->title || panel->chrome_panel < 0 )
            continue;
        action = panel->name ? Editor_HotkeyForPanel(panel->name) : -1;
        if( action >= 0 )
            edp_hotkey_row(ui, action, panel->title, self->view_labels[self->view_count], TORIRS_CHROME_LABEL_MAX);
        else
            snprintf(self->view_labels[self->view_count], TORIRS_CHROME_LABEL_MAX, "%s", panel->title);
        if( ui->popped[i] )
            menubar_mark_row(self->view_labels[self->view_count], TORIRS_CHROME_LABEL_MAX, "popped out");
        else if( !ui->chrome->panels[panel->chrome_panel].visible )
            menubar_mark_row(self->view_labels[self->view_count], TORIRS_CHROME_LABEL_MAX, "hidden");
        self->view_rows[self->view_count] = self->view_labels[self->view_count];
        self->view_targets[self->view_count] = i;
        self->view_count++;
    }
    self->view_panel_count = self->view_count;
    edp_hotkey_row(ui, EDITOR_HK_VIEW_HIDE_PANELS, "Hide / show panels", self->view_labels[self->view_count],
        TORIRS_CHROME_LABEL_MAX);
    self->view_rows[self->view_count] = self->view_labels[self->view_count];
    self->view_targets[self->view_count++] = -1;
    {
        char compact[48];
        snprintf(compact, sizeof(compact), "Compact panels: %s%s", Editor_UiCompact(ui) ? "on" : "off",
            ui->compact_mode < 0 ? " (auto)" : "");
        edp_hotkey_row(ui, EDITOR_HK_VIEW_COMPACT, compact, self->view_labels[self->view_count], TORIRS_CHROME_LABEL_MAX);
    }
    self->view_rows[self->view_count] = self->view_labels[self->view_count];
    self->view_targets[self->view_count++] = -1;
    edp_hotkey_row(
        ui, EDITOR_HK_VIEW_RESET_LAYOUT, "Reset layout", self->view_labels[self->view_count], TORIRS_CHROME_LABEL_MAX);
    self->view_rows[self->view_count] = self->view_labels[self->view_count];
    self->view_targets[self->view_count++] = -1;
    {
        char snap[32];
        snprintf(snap, sizeof(snap), "Snap to grid: %s", ui->snap ? "on" : "off");
        edp_hotkey_row(ui, EDITOR_HK_VIEW_SNAP, snap, self->view_labels[self->view_count], TORIRS_CHROME_LABEL_MAX);
    }
    self->view_rows[self->view_count] = self->view_labels[self->view_count];
    self->view_targets[self->view_count++] = -1;
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->menu_view, self->view_rows, self->view_count, -1);
    self->view_built_for = ui->panel_count;
    self->view_built_snap = ui->snap;
    self->view_built_compact = Editor_UiCompact(ui) * 4 + ui->compact_mode + 1;
    self->view_built_state = menubar_view_state(ui);
}

/** The File and Edit rows and the Level tips, from the hotkey table. */
static void
menubar_hints(
    struct EditorUi* ui,
    struct MenuBarPanel* self)
{
    for( int i = 0; i < FILE_COUNT; i++ )
    {
        edp_hotkey_row(ui, k_file_actions[i], Editor_HotkeyInfo(k_file_actions[i])->name, self->file_labels[i],
            TORIRS_CHROME_LABEL_MAX);
        self->file_rows[i] = self->file_labels[i];
    }
    self->file_count = FILE_COUNT;
    if( ui->recent.count > 0 )
    {
        snprintf(self->file_labels[self->file_count], TORIRS_CHROME_LABEL_MAX, "Recent squares");
        self->file_rows[self->file_count] = self->file_labels[self->file_count];
        self->file_count++;
        for( int i = 0; i < ui->recent.count && self->file_count < FILE_ROWS_MAX; i++ )
        {
            snprintf(self->file_labels[self->file_count], TORIRS_CHROME_LABEL_MAX, "  %s", ui->recent.keys[i]);
            self->file_rows[self->file_count] = self->file_labels[self->file_count];
            self->file_count++;
        }
    }
    self->recent_serial = ui->recent.serial;
    for( int i = 0; i < EDIT_COUNT; i++ )
    {
        edp_hotkey_row(ui, k_edit_actions[i], Editor_HotkeyInfo(k_edit_actions[i])->name, self->edit_labels[i],
            TORIRS_CHROME_LABEL_MAX);
        self->edit_rows[i] = self->edit_labels[i];
    }
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->menu_file, self->file_rows, self->file_count, -1);
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->menu_edit, self->edit_rows, EDIT_COUNT, -1);
    for( int i = 0; i < MENUBAR_LEVEL_BUTTONS; i++ )
    {
        char tip[TORIRS_CHROME_LABEL_MAX];
        edp_hotkey_tip(ui, k_level_actions[i], k_level_tips[i], tip, (int)sizeof(tip));
        ToriRSChrome_SetTooltip(ui->chrome, self->level_buttons[i], tip);
    }
    {
        char tip[TORIRS_CHROME_LABEL_MAX];
        edp_hotkey_tip(ui, EDITOR_HK_LEVEL_ABOVE, "Pinned: also draw the levels above", tip, (int)sizeof(tip));
        ToriRSChrome_SetTooltip(ui->chrome, self->button_above, tip);
    }
    self->hint_serial = ui->hotkeys.serial;
}

/** Press the row's button for the edit level, light Above, and name the
 *  level in the World panel's title. */
static void
menubar_level_show(
    struct EditorUi* ui,
    struct MenuBarPanel* self)
{
    struct EditorUiPanel* world = Editor_UiFindPanel(ui, "world");
    int const level = ui->tools.edit_level;

    for( int i = 0; i < MENUBAR_LEVEL_BUTTONS; i++ )
        ToriRSChrome_SetChecked(ui->chrome, self->level_buttons[i], i == level + 1);
    ToriRSChrome_SetChecked(ui->chrome, self->button_above, edp_level_showing_above(ui));
    ToriRSChrome_SetDisabled(ui->chrome, self->button_above, level < 0);
    if( world && world->chrome_panel >= 0 )
    {
        char title[TORIRS_CHROME_LABEL_MAX];
        if( level < 0 )
            snprintf(title, sizeof(title), "World");
        else if( edp_level_showing_above(ui) )
            snprintf(title, sizeof(title), "World - level %d (levels above shown)", level);
        else
            snprintf(title, sizeof(title), "World - level %d", level);
        ToriRSChrome_PanelSetTitle(ui->chrome, world->chrome_panel, title);
    }
    self->shown_level = level;
    self->shown_vis = ui->tools.vis_level;
}

static void
menubar_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct MenuBarPanel* self = opaque;

    int const width = ui->chrome->surface_w > 0 ? ui->chrome->surface_w : UITREE_LAYOUT_ROOT_W;

    if( self->hint_serial != ui->hotkeys.serial )
    {
        menubar_hints(ui, self);
        menubar_build_view(ui, self);
    }
    else if( self->recent_serial != ui->recent.serial )
        menubar_hints(ui, self);
    if( self->view_built_for != ui->panel_count || self->view_built_snap != ui->snap
        || self->view_built_compact != Editor_UiCompact(ui) * 4 + ui->compact_mode + 1
        || self->view_built_state != menubar_view_state(ui) )
        menubar_build_view(ui, self);
    /* A level changed behind the row's back -- another window
     * (STATE_EDIT_LEVEL lands in edit_level alone) or a key: the cap follows
     * it as it does a press here. Idempotent after edp_level_set, which has
     * already moved the cap. */
    if( self->shown_level != ui->tools.edit_level && self->shown_level >= -1 )
        ui->tools.vis_level =
            edp_level_follow_vis(self->shown_level, ui->tools.vis_level, ui->tools.edit_level);
    if( self->shown_level != ui->tools.edit_level || self->shown_vis != ui->tools.vis_level )
        menubar_level_show(ui, self);
    /* The bar spans whatever the screen is now. */
    if( ui->chrome->panels[self->panel].fixed_w != width )
        ToriRSChrome_PanelSetFixedWidth(ui->chrome, self->panel, width);
}

/** File > Recent squares > row `index` (0 newest): open it as the picker's
 *  Open does (the save guard first), in its storage. */
static void
menubar_open_recent(
    struct EditorUi* ui,
    int index)
{
    struct EditorUiGuard guard;
    const struct Editor_Picker* picker;

    memset(&guard, 0, sizeof(guard));
    if( !Editor_RecentEntry(&ui->recent, index, &guard.map_x, &guard.map_z, guard.storage, (int)sizeof(guard.storage)) )
        return;
    guard.action = EDITOR_UI_GUARD_OPEN_SQUARE;
    picker = edp_squares_picker(ui);
    guard.storage_index = picker ? Editor_PickerStorageIndex(picker, guard.storage[0] ? guard.storage : "maps") : -1;
    (void)Editor_UiGuarded(ui, &guard);
}

static int
menubar_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct MenuBarPanel* self = opaque;
    int row;

    for( int i = 0; i < MENUBAR_LEVEL_BUTTONS; i++ )
        if( widget == self->level_buttons[i] )
        {
            edp_level_set(ui, i - 1);
            menubar_level_show(ui, self);
            return 1;
        }
    if( widget == self->button_above )
    {
        (void)edp_level_show_above(ui, !edp_level_showing_above(ui));
        menubar_level_show(ui, self);
        return 1;
    }
    if( widget != self->menu_file && widget != self->menu_edit && widget != self->menu_view )
        return 0;
    row = ToriRSChrome_DropdownSelected(ui->chrome, widget);
    if( row < 0 )
        return 1;
    if( widget == self->menu_file && row < FILE_COUNT )
        (void)Editor_HotkeyRun(ui, k_file_actions[row]);
    else if( widget == self->menu_file && row < self->file_count )
        menubar_open_recent(ui, row == FILE_RECENT_HEADER ? 0 : row - FILE_RECENT_HEADER - 1);
    else if( widget == self->menu_edit && row < EDIT_COUNT )
        (void)Editor_HotkeyRun(ui, k_edit_actions[row]);
    else if( widget == self->menu_view && row == self->view_panel_count + VIEW_HIDE )
        (void)Editor_HotkeyRun(ui, EDITOR_HK_VIEW_HIDE_PANELS);
    else if( widget == self->menu_view && row == self->view_panel_count + VIEW_COMPACT )
    {
        (void)Editor_HotkeyRun(ui, EDITOR_HK_VIEW_COMPACT);
        menubar_build_view(ui, self);
    }
    else if( widget == self->menu_view && row == self->view_panel_count + VIEW_RESET_LAYOUT )
        (void)Editor_HotkeyRun(ui, EDITOR_HK_VIEW_RESET_LAYOUT);
    else if( widget == self->menu_view && row == self->view_panel_count + VIEW_SNAP )
    {
        (void)Editor_HotkeyRun(ui, EDITOR_HK_VIEW_SNAP);
        menubar_build_view(ui, self);
    }
    else if( widget == self->menu_view && row < self->view_panel_count )
    {
        struct EditorUiPanel* target = &ui->panels[self->view_targets[row]];
        int const panel = target->chrome_panel;
        ToriRSChrome_PanelSetVisible(ui->chrome, panel, !ui->chrome->panels[panel].visible);
    }
    return 1;
}

static void
menubar_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct MenuBarPanel* self = opaque;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, visible);
}

static void
menubar_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_MenuBar_Register(struct EditorUi* ui)
{
    struct MenuBarPanel* self;
    struct EditorUiPanel desc;

    assert(ui);

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->view_built_for = -1;
    self->panel =
        ToriRSChrome_PanelAdd(ui->chrome, TORIRS_CHROME_PANEL_MENUBAR, 0, 0, UITREE_LAYOUT_ROOT_W, "");
    assert(self->panel >= 0);
    self->menu_file = ToriRSChrome_MenuDrop(ui->chrome, self->panel, "File", NULL, 0);
    self->menu_edit = ToriRSChrome_MenuDrop(ui->chrome, self->panel, "Edit", NULL, 0);
    self->menu_view = ToriRSChrome_MenuDrop(ui->chrome, self->panel, "View", NULL, 0);
    self->label_level = ToriRSChrome_Label(ui->chrome, self->panel, "   Level");
    for( int i = 0; i < MENUBAR_LEVEL_BUTTONS; i++ )
        self->level_buttons[i] = ToriRSChrome_MenuItem(ui->chrome, self->panel, k_level_buttons[i]);
    self->button_above = ToriRSChrome_MenuItem(ui->chrome, self->panel, "Above");
    menubar_hints(ui, self);
    self->shown_level = -2;
    self->shown_vis = -2;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "menubar";
    /* Shown and hidden with the editor, but not a View-menu row: hiding the
     * menu from the menu would leave no way back. */
    desc.title = NULL;
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = menubar_tick;
    desc.activate = menubar_activate;
    desc.destroy = menubar_destroy;
    desc.set_visible = menubar_set_visible;
    Editor_UiAddPanel(ui, &desc);
}
