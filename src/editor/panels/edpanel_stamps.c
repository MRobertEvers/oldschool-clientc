/*
 * Stamps (workbench 6.3, design #placing "Stamps"; module "stamps"): the
 * Catalog's Stamps kind. The stamps editor/stamps/index.txt lists, in a
 * VLIST with its search box (R3): Find filters by name as you type, the
 * count row says "3 of 12 stamps", Enter or a click on a row -- or Place --
 * arms the stamp for the Place tool, whose ghost then shows the whole set
 * (Q turns it as a unit, a click drops it as one undo step, Esc drops the
 * ghost). A stamp is made from a selection in the Locs panel (Save as
 * stamp...) or from a rectangle on every level in the Rectangle panel (a
 * whole building); the marquee's Ctrl+C / Ctrl+V is the unnamed one. Rename
 * and Delete act on the row picked (2026-11-11: a stamp could be made and
 * never renamed or removed, so every trial stayed in the list for good).
 *
 * Reached from the Catalog through its "Stamps..." chip (registered with
 * the Catalog, Editor_Stamps_CatalogChip) and the View menu. The list is
 * rebuilt only when the index's text changes (its revision) or the filter
 * does: nothing is parsed or allocated on a quiet frame.
 */

#include "edpanel_common.h"
#include "edpanel_ops_stamps.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <string.h>

#define STAMPS_LIST_ROWS 8
/** Names the panel lists at most (the index may hold more; the count row
 *  says so). */
#define STAMPS_SHOWN_MAX 1024

struct StampsPanel
{
    int panel;
    int input_find;
    int row_count;
    int list;
    int button_place;
    int button_refresh;
    int input_rename;
    int button_rename;
    int button_delete;
    int row_armed;

    struct Editor_StampsFile index;
    /** The index's revision and the filter the rows were built from. */
    uint32_t shown_revision;
    int shown_valid;
    char shown_filter[TORIRS_CHROME_INPUT_MAX];
    /** Rows on screen: indices of headers in `index` by nth. */
    int rows[STAMPS_SHOWN_MAX];
    int row_total;
    int listed;
    /** The index was asked for (once until Refresh).  */
    int index_tried;
    /** The armed stamp's summary as last shown. */
    char armed_text[TORIRS_CHROME_LABEL_MAX];
    /** Open at the last tick the editor showed (stamps_set_visible): what
     *  showing it again restores. 0 at boot, so the panel opens from the Catalog's
     *  "Stamps..." or the View menu, not with the editor. */
    int open_when_hidden;
    /** A stamp just renamed: its row is picked again once the index is
     *  re-read under the new name (2026-11-11: the rename left nothing
     *  picked, so Place or Delete straight after it was refused). */
    char select_after[EDITOR_STAMP_NAME_MAX];
};

/** The Catalog's chip (Editor_Stamps_CatalogChip), claimed by this module. */
static int g_catalog_chip = -1;

static int
stamps_contains(
    const char* name,
    const char* filter)
{
    size_t const n = strlen(filter);

    if( n == 0 )
        return 1;
    for( const char* at = name; *at; at++ )
    {
        size_t i = 0;
        while( i < n && at[i] )
        {
            char a = at[i];
            char b = filter[i];
            if( a >= 'A' && a <= 'Z' )
                a = (char)(a - 'A' + 'a');
            if( b >= 'A' && b <= 'Z' )
                b = (char)(b - 'A' + 'a');
            if( a != b )
                break;
            i++;
        }
        if( i == n )
            return 1;
    }
    return 0;
}

static void
stamps_rebuild(
    struct EditorUi* ui,
    struct StampsPanel* self,
    const char* filter,
    uint32_t revision)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    int const total = Editor_StampsHeaderCount(&self->index);
    int first;
    int shown;

    self->listed = 0;
    for( int i = 0; i < total && self->listed < STAMPS_SHOWN_MAX; i++ )
        if( stamps_contains(Editor_StampsHeaderName(&self->index, i), filter) )
            self->rows[self->listed++] = i;
    if( self->select_after[0] )
    {
        int picked = -1;
        for( int r = 0; r < self->listed && picked < 0; r++ )
            if( strcmp(Editor_StampsHeaderName(&self->index, self->rows[r]), self->select_after) == 0 )
                picked = r;
        if( picked >= 0 )
        {
            ToriRSChrome_VListSetSelected(ui->chrome, self->list, picked);
            self->select_after[0] = '\0';
        }
    }
    self->row_total = total;
    self->shown_revision = revision;
    self->shown_valid = 1;
    snprintf(self->shown_filter, sizeof(self->shown_filter), "%s", filter);
    ToriRSChrome_VListSetCount(ui->chrome, self->list, self->listed);
    if( ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        for( int r = first; r < first + shown; r++ )
            ToriRSChrome_VListSetRow(ui->chrome, self->list, r, "");
    if( total == 0 )
        snprintf(line, sizeof(line), "No stamps yet: Rectangle panel > Save as stamp");
    else if( filter[0] )
        snprintf(line, sizeof(line), "%d of %d stamps match \"%.24s\"", self->listed, total, filter);
    else
        snprintf(line, sizeof(line), "%d stamp%s", total, total == 1 ? "" : "s");
    ToriRSChrome_SetText(ui->chrome, self->row_count, line);
}

static void
stamps_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct StampsPanel* self = opaque;
    struct Editor_DocHandle handle;
    const struct Editor_Stamp* armed;
    const char* filter;
    char line[TORIRS_CHROME_LABEL_MAX];
    int first;
    int shown;

    /* What showing the editor again restores (stamps_set_visible): read here,
     * while the editor shows, because hiding it hides this panel first. */
    if( ui->visible )
        self->open_when_hidden = ui->chrome->panels[self->panel].visible;
    armed = edp_stamp_armed(ui);
    if( armed )
        /* Short enough for the row whatever the stamp holds (the status bar
         * said its pieces when it was armed). */
        snprintf(line, sizeof(line), "Placing %.24s (%d locs), turned %d", armed->name,
            Editor_StampCount(armed, EDITOR_STAMP_LOC), armed->turned * 90);
    else
        snprintf(line, sizeof(line), "Nothing armed: pick a stamp, then Place");
    if( strcmp(line, self->armed_text) != 0 )
    {
        snprintf(self->armed_text, sizeof(self->armed_text), "%s", line);
        ToriRSChrome_SetText(ui->chrome, self->row_armed, line);
    }
    if( !ui->editor || !ui->chrome->panels[self->panel].visible )
        return;
    /* The index document: opened once, re-read when its text changes. */
    if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, EDITOR_STAMPS_INDEX_KEY, &handle) || !handle.text
        || !handle.text->loaded )
    {
        /* Asked once per showing (Refresh asks again): a session that
         * cannot open it is not asked every frame. */
        if( self->index_tried )
            return;
        self->index_tried = 1;
        (void)edp_stamps_list(ui, &self->index);
        if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, EDITOR_STAMPS_INDEX_KEY, &handle) || !handle.text )
            return;
        self->shown_valid = 0;
    }
    filter = ToriRSChrome_Text(ui->chrome, self->input_find);
    if( !filter )
        filter = "";
    if( !self->shown_valid || handle.text->revision != self->shown_revision )
    {
        (void)Editor_StampsParse(&self->index, handle.text->text, handle.text->length);
        stamps_rebuild(ui, self, filter, handle.text->revision);
    }
    else if( strcmp(filter, self->shown_filter) != 0 )
        stamps_rebuild(ui, self, filter, handle.text->revision);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->listed; r++ )
        ToriRSChrome_VListSetRow(ui->chrome, self->list, r, Editor_StampsHeaderName(&self->index, self->rows[r]));
}

static void
stamps_arm_row(
    struct EditorUi* ui,
    struct StampsPanel* self,
    int row)
{
    const char* name;

    if( row < 0 || row >= self->listed )
    {
        Editor_UiStatus(ui, "Place stamp refused: pick a stamp in the list first");
        return;
    }
    name = Editor_StampsHeaderName(&self->index, self->rows[row]);
    if( name )
        (void)edp_stamp_arm(ui, name);
}

/** The name of the row picked in the list, or NULL with a sentence. */
static const char*
stamps_picked(
    struct EditorUi* ui,
    struct StampsPanel* self,
    const char* what)
{
    int const row = ToriRSChrome_VListSelected(ui->chrome, self->list);

    if( row < 0 || row >= self->listed )
    {
        Editor_UiStatus(ui, "%s refused: pick a stamp in the list first", what);
        return NULL;
    }
    return Editor_StampsHeaderName(&self->index, self->rows[row]);
}

int
Editor_Stamps_RunAction(
    struct EditorUi* ui,
    enum Editor_HotkeyAction action)
{
    struct StampsPanel* self;
    char name[EDITOR_STAMP_NAME_MAX];
    const char* picked;

    assert(ui);
    if( action == EDITOR_HK_STAMP_SAVE_RECT )
    {
        (void)Editor_RectSelect_SaveStamp(ui);
        return 1;
    }
    if( action != EDITOR_HK_STAMP_RENAME && action != EDITOR_HK_STAMP_DELETE )
        return 0;
    self = Editor_UiPanelSelf(ui, "stamps");
    if( !self )
        return 1;
    picked = stamps_picked(ui, self, action == EDITOR_HK_STAMP_DELETE ? "Delete stamp" : "Rename stamp");
    if( !picked )
        return 1;
    /* A copy: the edit rewrites the index the name points into. */
    snprintf(name, sizeof(name), "%s", picked);
    if( action == EDITOR_HK_STAMP_DELETE )
        (void)edp_stamp_delete(ui, name);
    else
    {
        const char* to = ToriRSChrome_Text(ui->chrome, self->input_rename);
        if( !to || !to[0] )
            Editor_UiStatus(ui, "Rename stamp refused: type the new name in New name first");
        else if( edp_stamp_rename(ui, name, to) )
        {
            snprintf(self->select_after, sizeof(self->select_after), "%s", to);
            ToriRSChrome_SetText(ui->chrome, self->input_rename, "");
        }
    }
    /* The list follows the index's revision on its next tick (the renamed
     * stamp picked again there). */
    ToriRSChrome_VListSetSelected(ui->chrome, self->list, -1);
    return 1;
}

static int
stamps_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct StampsPanel* self = opaque;

    if( widget == g_catalog_chip )
    {
        if( !ui->chrome->panels[self->panel].visible )
            (void)Editor_UiTogglePanel(ui, "stamps");
        return 1;
    }
    if( widget == self->list || widget == self->button_place )
    {
        stamps_arm_row(ui, self, ToriRSChrome_VListSelected(ui->chrome, self->list));
        return 1;
    }
    if( widget == self->button_refresh )
    {
        self->index_tried = 0;
        (void)edp_stamps_list(ui, &self->index);
        self->shown_valid = 0;
        return 1;
    }
    if( widget == self->button_rename || widget == self->input_rename )
        return Editor_Stamps_RunAction(ui, EDITOR_HK_STAMP_RENAME);
    if( widget == self->button_delete )
        return Editor_Stamps_RunAction(ui, EDITOR_HK_STAMP_DELETE);
    if( widget == self->input_find )
        return 1;
    return 0;
}

/* Showing the editor shows every titled panel (Editor_UiSetVisible), and the
 * first layout hides only the default column's: an on-demand panel keeps
 * itself closed, as Record and Triggers do. Hiding the editor (Tab) and
 * showing it again leaves it as the user had it. */
static void
stamps_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct StampsPanel* self = opaque;

    if( !visible )
        return;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, self->open_when_hidden);
}

static void
stamps_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct StampsPanel* self = opaque;
    (void)ui;
    Editor_StampsFree(&self->index);
    g_catalog_chip = -1;
    ToriMem_Free(ToriMem_Editor(), self);
}

void
Editor_Stamps_CatalogChip(
    struct EditorUi* ui,
    int catalog_panel)
{
    assert(ui);
    assert(catalog_panel >= 0);
    g_catalog_chip = ToriRSChrome_Button(ui->chrome, catalog_panel, "Stamps...");
    assert(g_catalog_chip >= 0);
    ToriRSChrome_SetTooltip(ui->chrome, g_catalog_chip, "Saved stamps: place a set of locs as one piece");
}

void
Editor_Stamps_Register(struct EditorUi* ui)
{
    struct StampsPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    Editor_StampsInit(&self->index);

    self->panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8),
        edp_px(ui, 460), edp_px(ui, 240), "Stamps");
    assert(self->panel >= 0);
    self->input_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find, "part of a stamp's name");
    self->row_count = ToriRSChrome_Label(chrome, self->panel, "");
    self->list = ToriRSChrome_VList(chrome, self->panel, "", STAMPS_LIST_ROWS, 0);
    ToriRSChrome_SetTooltip(chrome, self->list, "Enter or click: place this stamp");
    self->button_place = ToriRSChrome_Button(chrome, self->panel, "Place");
    ToriRSChrome_SetTooltip(chrome, self->button_place, "Arm the stamp: the ghost shows it, Q turns it, click drops it");
    self->button_refresh = ToriRSChrome_Button(chrome, self->panel, "Refresh");
    ToriRSChrome_SetTooltip(chrome, self->button_refresh, "Read editor/stamps/index.txt again");
    self->input_rename = ToriRSChrome_TextInput(chrome, self->panel, "New name", "");
    ToriRSChrome_SetTooltip(chrome, self->input_rename, "The picked stamp's new name, then Rename");
    self->button_rename = ToriRSChrome_Button(chrome, self->panel, "Rename");
    ToriRSChrome_SetTooltip(chrome, self->button_rename, "Give the picked stamp the name in New name");
    self->button_delete = ToriRSChrome_Button(chrome, self->panel, "Delete");
    ToriRSChrome_SetTooltip(chrome, self->button_delete, "Delete the picked stamp; Ctrl+Z brings it back");
    self->row_armed = ToriRSChrome_Label(chrome, self->panel, "");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "stamps";
    desc.title = "Stamps";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = stamps_tick;
    desc.activate = stamps_activate;
    desc.set_visible = stamps_set_visible;
    desc.destroy = stamps_destroy;
    Editor_UiAddPanel(ui, &desc);
}
