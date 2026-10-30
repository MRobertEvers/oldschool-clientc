/*
 * File / Edit / View across the top of the window.
 *
 * Row order is the dispatch contract: each menu's rows and its handler sit
 * next to each other below, and the handler dispatches on the row's index.
 * The View menu lists every registered panel that has a title -- other
 * owners' panels included -- so it is filled on the first tick, after every
 * Register has run.
 */

#include "edpanel_common.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"
#include "ui/uitree_layout.h"

#include <stdio.h>
#include <string.h>

enum
{
    FILE_NEW = 0,
    FILE_SAVE_ALL,
    FILE_SAVE_AS,
    FILE_BAKE,
    FILE_CLOSE,
    FILE_COUNT
};
static const char* const k_file_rows[FILE_COUNT] = {
    "New square...", "Save all", "Save square as new...", "Bake cache (slow)", "Close editor",
};

enum
{
    EDIT_UNDO = 0,
    EDIT_REDO,
    EDIT_DESELECT,
    EDIT_DELETE,
    EDIT_DUPLICATE,
    EDIT_ROTATE,
    EDIT_RESHAPE,
    EDIT_COPY,
    EDIT_PASTE,
    EDIT_FILL,
    EDIT_CLEAR,
    EDIT_COUNT
};
static const char* const k_edit_rows[EDIT_COUNT] = {
    "Undo",           "Redo",           "Deselect",       "Delete selection  [Del]",
    "Duplicate  [D]", "Rotate  [R]",    "Reshape  [S]",   "Copy tiles",
    "Paste tiles",    "Fill tiles",     "Clear tiles",
};

#define MENUBAR_VIEW_MAX EDITOR_UI_PANELS_MAX

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
    int view_built_for;
};

/** The square File > Save as new copies: the picker's selection, else the
 *  first square open in the mirror. */
static int
menubar_current_square(
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
                snprintf(out, (size_t)out_size, "%s/m%d_%d", storage, doc->squares[i].map_x,
                    doc->squares[i].map_z);
            else
                snprintf(out, (size_t)out_size, "m%d_%d", doc->squares[i].map_x,
                    doc->squares[i].map_z);
            return 1;
        }
    }
    return 0;
}

static void
menubar_build_view(
    struct EditorUi* ui,
    struct MenuBarPanel* self)
{
    self->view_count = 0;
    for( int i = 0; i < ui->panel_count && self->view_count < MENUBAR_VIEW_MAX; i++ )
    {
        const struct EditorUiPanel* panel = &ui->panels[i];
        if( !panel->title || panel->chrome_panel < 0 )
            continue;
        snprintf(self->view_labels[self->view_count], TORIRS_CHROME_LABEL_MAX, "%s", panel->title);
        self->view_rows[self->view_count] = self->view_labels[self->view_count];
        self->view_targets[self->view_count] = i;
        self->view_count++;
    }
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->menu_view, self->view_rows, self->view_count, -1);
    self->view_built_for = ui->panel_count;
}

static void
menubar_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct MenuBarPanel* self = opaque;

    if( self->view_built_for != ui->panel_count )
        menubar_build_view(ui, self);
    /* The bar spans whatever the screen is now. */
    if( ui->chrome->panels[self->panel].fixed_w != UITREE_LAYOUT_ROOT_W )
        ToriRSChrome_PanelSetFixedWidth(ui->chrome, self->panel, UITREE_LAYOUT_ROOT_W);
}

static void
menubar_file(
    struct EditorUi* ui,
    int row)
{
    char key[EDITOR_DOC_KEY_MAX];

    if( row == FILE_NEW )
    {
        int map_x = -1;
        int map_z = -1;
        (void)Editor_SquarePicker_EmptyCell(ui, &map_x, &map_z);
        Editor_NewSquare_Open(
            ui, EDITOR_NEW_SQUARE_NEW,
            Editor_SquarePicker_SelectionKey(ui, key, (int)sizeof(key)) ? key : NULL, map_x, map_z);
    }
    else if( row == FILE_SAVE_ALL )
    {
        int const saved = ui->editor ? Editor_SaveAll(ui->editor) : -1;
        if( saved < 0 )
            Editor_UiLog(ui, "editor: read-only session, nothing saved");
        else
            Editor_UiLog(ui, "editor: saved %d document(s) as text", saved);
    }
    else if( row == FILE_SAVE_AS )
    {
        if( menubar_current_square(ui, key, (int)sizeof(key)) )
            Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_SAVE_AS, key, -1, -1);
        else
            Editor_UiLog(ui, "editor: open or select a square first");
    }
    else if( row == FILE_BAKE )
    {
        if( ui->hooks.bake )
            ui->hooks.bake(ui->hooks_user);
        else
            Editor_UiLog(ui, "editor: this window cannot bake");
    }
    else if( row == FILE_CLOSE )
    {
        struct EditorUiGuard guard;
        memset(&guard, 0, sizeof(guard));
        guard.action = EDITOR_UI_GUARD_CLOSE;
        (void)Editor_UiGuarded(ui, &guard);
    }
}

static void
menubar_edit(
    struct EditorUi* ui,
    int row)
{
    if( row == EDIT_UNDO )
        Editor_UiLog(ui, "editor: undo reverted %d edit(s)", ui->editor ? Editor_Undo(ui->editor) : 0);
    else if( row == EDIT_REDO )
        Editor_UiLog(ui, "editor: redo reapplied %d edit(s)", ui->editor ? Editor_Redo(ui->editor) : 0);
    else if( row == EDIT_DESELECT )
    {
        ui->tools.loc_count = 0;
        ui->tools.rect.valid = 0;
        if( ui->hooks.selection_changed )
            ui->hooks.selection_changed(ui->hooks_user);
    }
    else if( row == EDIT_DELETE )
        (void)edp_locs_delete(ui);
    else if( row == EDIT_DUPLICATE )
        (void)edp_locs_duplicate(ui);
    else if( row == EDIT_ROTATE )
        (void)edp_locs_rotate(ui);
    else if( row == EDIT_RESHAPE )
        (void)edp_locs_reshape(ui);
    else if( row == EDIT_COPY )
        (void)edp_rect_copy(ui);
    else if( row == EDIT_PASTE )
        (void)edp_rect_paste(ui, ui->tools.rect.x0, ui->tools.rect.z0, ui->tools.rect.level);
    else if( row == EDIT_FILL )
        (void)edp_rect_fill(ui);
    else if( row == EDIT_CLEAR )
        (void)edp_rect_clear(ui);
}

static int
menubar_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct MenuBarPanel* self = opaque;
    int row;

    if( widget != self->menu_file && widget != self->menu_edit && widget != self->menu_view )
        return 0;
    row = ToriRSChrome_DropdownSelected(ui->chrome, widget);
    if( row < 0 )
        return 1;
    if( widget == self->menu_file && row < FILE_COUNT )
        menubar_file(ui, row);
    else if( widget == self->menu_edit && row < EDIT_COUNT )
        menubar_edit(ui, row);
    else if( widget == self->menu_view && row < self->view_count )
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
    self->menu_file = ToriRSChrome_MenuDrop(ui->chrome, self->panel, "File", k_file_rows, FILE_COUNT);
    self->menu_edit = ToriRSChrome_MenuDrop(ui->chrome, self->panel, "Edit", k_edit_rows, EDIT_COUNT);
    self->menu_view = ToriRSChrome_MenuDrop(ui->chrome, self->panel, "View", NULL, 0);
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
