/*
 * The square picker (docs/MAPED_DESIGN.html #squares): a map of every square
 * the content tree holds, one page per world-map area plus Unmapped, chosen
 * with the Area dropdown (a tab per area made a strip of ~50 overlapping
 * labels nobody could read).
 *
 *   the well       thumbnail cells in each square's average floor colour,
 *                  north up, map coordinates on the axes; wheel zooms about
 *                  the pointer, a drag pans
 *   hover          the readout names the square, its area, its spawns, its
 *                  storage
 *   click          opens the square in this window (through the save guard)
 *   shift-click    adds it to the open set
 *   right-click    Open, Open in new view, Copy to..., Delete (clear)
 *   empty cell     a click marks it; New creates there
 *   search         m50_50, "50 50", an absolute tile "3222 3218", or an area
 *                  or region name
 *   outlines       open squares white, unsaved ones in the accent, the
 *                  selection yellow, created-not-saved green, and every
 *                  view's window framed (STATE_VIEW_SQUARE)
 *   collection     the Collection dropdown shows only one named collection's
 *                  squares (the AREAS document, edpanel_areas.c), whatever
 *                  their area page; "all" is every square again
 *
 * The atlas is the catalog's (CATALOG_SQUARES), fetched the first time the
 * panel is shown and replaced whenever the server broadcasts a change (a
 * DOC_CREATE, a save that wrote squares). A server with no catalog binding
 * still gets a picker: the square list, every square unmapped.
 */

#include "edpanel_common.h"
#include "edpanel_picker.h"
#include "edpanels.h"
#include "editor/editor_areas.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The well's height when the panel is content-sized, and the least it shrinks
 * to when the layout gives the panel a height to fill (1x pixels). */
#define SQUARES_WELL_H 300
#define SQUARES_WELL_MIN_H 120
#define SQUARES_DRAG_SLOP 3
#define SQUARES_STORAGE_ROWS 32
/* "all" plus this many collections in the Collection dropdown. */
#define SQUARES_COLLECTION_ROWS 32

enum
{
    MENU_OPEN = 0,
    MENU_ADD,
    MENU_NEW_VIEW,
    MENU_COPY_TO,
    MENU_DELETE,
    MENU_COUNT
};
static const char* const k_menu_rows[MENU_COUNT] = {
    "Open", "Add to open set", "Open in new view", "Copy to...", "Delete (clear)...",
};

static const char* const k_loading_titles[1] = { "Squares" };

struct SquaresPanel
{
    struct Editor_Picker picker;
    struct Editor_PickerColors colors;

    int panel;
    int input_search;
    int dropdown_area;
    int dropdown_storage;
    int graph;
    int row_hover;
    int row_status;
    int button_open;
    int button_add;
    int button_new;
    int button_fit;
    int button_refresh;

    int menu_panel;
    int menu_items[MENU_COUNT];
    int menu_square;

    int confirm_panel;
    int confirm_row;
    int confirm_yes;
    int confirm_no;
    int confirm_square;

    int dropdown_collection;
    char collection_labels[SQUARES_COLLECTION_ROWS + 1][TORIRS_CHROME_LABEL_MAX];
    const char* collection_rows[SQUARES_COLLECTION_ROWS + 1];
    int collection_row_count;
    /** The AREAS parse the dropdown was built from (Editor_Areas_File). */
    uint32_t collection_serial;
    int collection_built;
    /** The collection the picker is filtered to ("" none). */
    char collection[EDITOR_AREA_NAME_MAX];

    char storage_labels[SQUARES_STORAGE_ROWS][TORIRS_CHROME_LABEL_MAX];
    const char* storage_rows[SQUARES_STORAGE_ROWS];
    int storage_row_count;

    int fetched;
    int want_refresh;
    int dirty;
    /** The user zoomed, panned or searched: the view is theirs, and a well
     *  that changes size keeps it instead of fitting the page again. */
    int user_view;

    /* Gesture state. */
    int pressing;
    int dragging;
    int press_x;
    int press_y;
    int last_x;
    int last_y;

    /* What the outlines were last drawn from. */
    uint32_t flags_hash;
    uint32_t frames_hash;
};

static struct SquaresPanel*
squares_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "squares");
}

/* ---- the atlas ------------------------------------------------------------------- */

static void
squares_point_widgets(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct Editor_Picker* picker = &self->picker;

    ToriRSChrome_DropdownSetOptions(
        ui->chrome, self->dropdown_area, picker->tab_titles, picker->tab_count, picker->active_tab);
}

static void
squares_storage_rows(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct Editor_Picker* picker = &self->picker;
    int n = 0;

    snprintf(self->storage_labels[n], TORIRS_CHROME_LABEL_MAX, "every storage");
    self->storage_rows[n] = self->storage_labels[n];
    n++;
    for( int i = 0; i < picker->storage_count && n < SQUARES_STORAGE_ROWS; i++ )
    {
        snprintf(self->storage_labels[n], TORIRS_CHROME_LABEL_MAX, "%s", picker->storages[i]);
        self->storage_rows[n] = self->storage_labels[n];
        n++;
    }
    self->storage_row_count = n;
    ToriRSChrome_DropdownSetOptions(
        ui->chrome, self->dropdown_storage, self->storage_rows, n, picker->storage_filter + 1);
}

/** The square list as an atlas: what a server without the catalog binding
 *  can still say. Every square in maps/, unmapped, no colour. */
static int
squares_fallback_atlas(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    static const char* const k_maps[1] = { "maps" };
    struct EditorHost_CatalogSquares atlas;
    struct EditorHost_CatalogSquare* rows;
    int* coords;
    int total = 0;
    int const max = 8192;

    if( !ui->editor || !ui->editor->host_open || !ui->editor->host.vtable->square_list )
        return 0;
    coords = ToriMem_Calloc(ToriMem_Editor(), (size_t)max * 2, sizeof(int));
    assert(coords);
    if( ui->editor->host.vtable->square_list(ui->editor->host.user_data, coords, max, &total)
        != EDITOR_HOST_OK )
    {
        ToriMem_Free(ToriMem_Editor(), coords);
        return 0;
    }
    if( total > max )
        total = max;
    rows = ToriMem_Calloc(ToriMem_Editor(), (size_t)(total + 1), sizeof(*rows));
    assert(rows);
    for( int i = 0; i < total; i++ )
    {
        rows[i].map_x = coords[i * 2];
        rows[i].map_z = coords[i * 2 + 1];
        rows[i].flags = TORIRSMAPED_SQUARE_HAS_JM2;
        rows[i].primary_area = -1;
    }
    memset(&atlas, 0, sizeof(atlas));
    atlas.storage_count = 1;
    atlas.storages = k_maps;
    atlas.square_count = total;
    atlas.squares = rows;
    if( Editor_PickerSetAtlas(&self->picker, &atlas) || !self->fetched )
        squares_point_widgets(ui, self);
    ToriMem_Free(ToriMem_Editor(), rows);
    ToriMem_Free(ToriMem_Editor(), coords);
    Editor_UiLog(ui, "editor: this server has no catalog; the picker lists %d squares, unmapped", total);
    return 1;
}

static void
squares_take_atlas(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    const struct EditorHost_CatalogSquares* atlas)
{
    if( Editor_PickerSetAtlas(&self->picker, atlas) || !self->fetched )
        squares_point_widgets(ui, self);
    else
        ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_area, self->picker.active_tab);
    squares_storage_rows(ui, self);
    self->fetched = 1;
    self->dirty = 1;
}

static void
squares_fetch(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct EditorHost_CatalogSquares atlas;

    self->want_refresh = 0;
    if( !ui->editor || !ui->editor->host_open )
        return;
    if( !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
    {
        if( squares_fallback_atlas(ui, self) )
        {
            squares_storage_rows(ui, self);
            self->fetched = 1;
            self->dirty = 1;
        }
        return;
    }
    memset(&atlas, 0, sizeof(atlas));
    if( Editor_HostMapEdCatalogSquares(&ui->editor->host, &atlas) != EDITOR_HOST_OK )
    {
        Editor_UiLog(ui, "editor: the server did not answer CATALOG_SQUARES");
        return;
    }
    squares_take_atlas(ui, self, &atlas);
    Editor_HostCatalogSquaresFree(&atlas);
}

/* ---- the view --------------------------------------------------------------------- */

static int
squares_well_w(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    int const scale = ToriRSChrome_Scale(ui->chrome);
    int const w = ui->chrome->widgets[self->graph].w / (scale > 0 ? scale : 1);
    return w > 32 ? w : EDP_PICKER_W - 24;
}

static int
squares_well_h(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    int const scale = ToriRSChrome_Scale(ui->chrome);
    int const h = ui->chrome->widgets[self->graph].h / (scale > 0 ? scale : 1);
    return h > 32 ? h : SQUARES_WELL_H;
}

/**
 * The well fills the panel. Content-sized (no layout height), it is
 * SQUARES_WELL_H at the chrome's scale; given a height by the layout or a
 * resize, it takes what the other rows leave, down to SQUARES_WELL_MIN_H.
 * Converges in one Build: the other rows' height does not depend on it.
 */
static void
squares_size_well(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct ToriRSChromePanel const* p = &ui->chrome->panels[self->panel];
    struct ToriRSChromeWidget const* well = &ui->chrome->widgets[self->graph];
    int want = edp_px(ui, SQUARES_WELL_H);

    if( p->fixed_h > 0 && p->view_h > 0 && p->content_h > 0 )
    {
        want = p->view_h - (p->content_h - well->view_h);
        if( want < edp_px(ui, SQUARES_WELL_MIN_H) )
            want = edp_px(ui, SQUARES_WELL_MIN_H);
    }
    if( want != well->view_h )
        ToriRSChrome_SetCustomHeight(ui->chrome, self->graph, want);
}

/** EditorUiPanel::stretch: the well is the part that fills. */
static int
squares_stretch(
    struct EditorUi* ui,
    void* opaque,
    int* out_min_h,
    int* out_natural_h)
{
    struct SquaresPanel* self = opaque;

    assert(out_min_h);
    assert(out_natural_h);
    *out_min_h = edp_px(ui, SQUARES_WELL_MIN_H);
    *out_natural_h = edp_px(ui, SQUARES_WELL_H);
    return ui->chrome->widgets[self->graph].view_h;
}

static uint32_t
squares_hash_flags(const struct Editor_Picker* picker)
{
    uint32_t hash = 2166136261u;
    for( int i = 0; i < picker->square_count; i++ )
        hash = (hash ^ (picker->squares[i].flags + (uint32_t)i * 31u)) * 16777619u;
    return hash;
}

static uint32_t
squares_hash_frames(const struct EditorUi* ui)
{
    uint32_t hash = 2166136261u;
    for( int i = 0; i < EDITOR_UI_VIEW_FRAMES_MAX; i++ )
    {
        const struct EditorUiViewFrame* f = &ui->frames[i];
        if( !f->used )
            continue;
        hash = (hash ^ (uint32_t)(f->slot * 7 + f->map_x * 1031 + f->map_z * 31 + f->w * 3 + f->h))
               * 16777619u;
    }
    return hash;
}

static void
squares_readout(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct Editor_Picker* picker = &self->picker;
    char line[TORIRS_CHROME_LABEL_MAX];

    if( picker->hover >= 0 )
    {
        const struct Editor_PickerSquare* square = &picker->squares[picker->hover];
        const char* area = square->primary_area >= 0 ? picker->areas[square->primary_area].display_name
                                                     : "unmapped";
        snprintf(line, sizeof(line), "m%d_%d  %s  %d spawns  %s", square->map_x, square->map_z,
            area[0] ? area : picker->areas[square->primary_area].internal_name, square->spawn_count,
            picker->storages[square->storage]);
    }
    else if( picker->hover_valid )
        snprintf(line, sizeof(line), "m%d_%d  empty", picker->hover_map_x, picker->hover_map_z);
    else
        snprintf(line, sizeof(line), " ");
    ToriRSChrome_SetText(ui->chrome, self->row_hover, line);

    if( picker->filter_on )
        snprintf(line, sizeof(line), "collection %s: %d squares%s", self->collection, picker->filter_shown,
            picker->truncated ? "  (zoom in: not all drawn)" : "");
    else if( picker->tab_count > 0 )
        snprintf(line, sizeof(line), "%s: %d squares%s", picker->tabs[picker->active_tab].title,
            picker->tabs[picker->active_tab].count, picker->truncated ? "  (zoom in: not all drawn)" : "");
    else
        snprintf(line, sizeof(line), "no squares");
    ToriRSChrome_SetText(ui->chrome, self->row_status, line);
}

/** Point the picker at collection `self->collection` as the document now
 *  says it is (its squares may have changed); off when it is gone. */
static void
squares_apply_collection(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    const struct Editor_AreasFile* file)
{
    const char* keys[EDITOR_COLLECTION_SQUARES_MAX];
    int line = -1;

    if( self->collection[0] && file )
        line = Editor_AreasFindCollection(file, self->collection);
    if( line < 0 )
    {
        if( self->collection[0] )
            Editor_UiLog(ui, "editor: collection '%s' is gone; the picker shows every square", self->collection);
        self->collection[0] = '\0';
        if( self->picker.filter_on )
            Editor_PickerSetFilter(&self->picker, NULL, -1);
    }
    else
    {
        const struct Editor_Collection* coll = file->lines[line].collection;
        for( int i = 0; i < coll->square_count; i++ )
            keys[i] = coll->squares[i];
        Editor_PickerSetFilter(&self->picker, keys, coll->square_count);
    }
    self->dirty = 1;
}

/** The Collection dropdown's rows from the AREAS document: rebuilt when its
 *  parse moved, the chosen collection kept by name. */
static void
squares_collection_rows(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    uint32_t serial = 0;
    const struct Editor_AreasFile* file = Editor_Areas_File(ui, &serial);
    int selected = 0;
    int n = 0;

    if( self->collection_built && serial == self->collection_serial )
        return;
    snprintf(self->collection_labels[n], TORIRS_CHROME_LABEL_MAX, "all");
    self->collection_rows[n] = self->collection_labels[n];
    n++;
    for( int c = 0; file && c < Editor_AreasCollectionCount(file) && n <= SQUARES_COLLECTION_ROWS; c++ )
    {
        const struct Editor_Collection* coll = file->lines[Editor_AreasNthCollection(file, c)].collection;
        snprintf(self->collection_labels[n], TORIRS_CHROME_LABEL_MAX, "%s", coll->name);
        self->collection_rows[n] = self->collection_labels[n];
        if( strcmp(coll->name, self->collection) == 0 )
            selected = n;
        n++;
    }
    self->collection_row_count = n;
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_collection, self->collection_rows, n, selected);
    self->collection_serial = serial;
    self->collection_built = 1;
    if( self->collection[0] || self->picker.filter_on )
        squares_apply_collection(ui, self, file);
}

static void
squares_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct SquaresPanel* self = opaque;
    struct Editor_Picker* picker = &self->picker;
    struct EditorHost_CatalogSquares atlas;

    if( !ui->chrome->panels[self->panel].visible )
        return;
    if( !self->fetched || self->want_refresh )
        squares_fetch(ui, self);
    if( ui->editor && ui->editor->host_open )
    {
        memset(&atlas, 0, sizeof(atlas));
        if( Editor_HostMapEdCatalogSquaresChanged(&ui->editor->host, &atlas) )
        {
            squares_take_atlas(ui, self, &atlas);
            Editor_HostCatalogSquaresFree(&atlas);
        }
    }
    if( !self->fetched )
        return;
    squares_collection_rows(ui, self);

    squares_size_well(ui, self);
    /* Fitted until the user moves the view: the first frame the well has a
     * size, and again whenever the layout changes that size. */
    if( !self->user_view && ui->chrome->widgets[self->graph].w > 0
        && (picker->well_w != squares_well_w(ui, self) || picker->well_h != squares_well_h(ui, self)) )
    {
        Editor_PickerFit(picker, squares_well_w(ui, self), squares_well_h(ui, self));
        self->dirty = 1;
    }

    if( ui->editor )
        Editor_PickerNoteDocument(picker, &ui->editor->doc);
    {
        uint32_t const flags = squares_hash_flags(picker);
        uint32_t const frames = squares_hash_frames(ui);
        if( flags != self->flags_hash || frames != self->frames_hash )
        {
            self->flags_hash = flags;
            self->frames_hash = frames;
            self->dirty = 1;
        }
    }
    if( !self->dirty )
        return;
    Editor_PickerDraw(
        picker, ui->chrome, self->graph, squares_well_w(ui, self), squares_well_h(ui, self),
        &self->colors, ui->frames, EDITOR_UI_VIEW_FRAMES_MAX, ui->view_slot);
    squares_readout(ui, self);
    self->dirty = 0;
}

/* ---- actions ------------------------------------------------------------------------ */

static void
squares_storage_of(
    const struct Editor_Picker* picker,
    int square,
    char out[EDITOR_DOC_STORAGE_MAX])
{
    const char* storage = picker->storages[picker->squares[square].storage];
    snprintf(out, EDITOR_DOC_STORAGE_MAX, "%s", strcmp(storage, "maps") == 0 ? "" : storage);
}

static void
squares_open(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int square,
    int add)
{
    struct Editor_Picker* picker = &self->picker;
    struct EditorUiGuard guard;

    assert(square >= 0);
    assert(square < picker->square_count);

    memset(&guard, 0, sizeof(guard));
    guard.action = EDITOR_UI_GUARD_OPEN_SQUARE;
    guard.map_x = picker->squares[square].map_x;
    guard.map_z = picker->squares[square].map_z;
    squares_storage_of(picker, square, guard.storage);
    if( add )
    {
        /* Adding keeps what is open: nothing unsaved is at stake. */
        if( ui->hooks.open_square )
            ui->hooks.open_square(ui->hooks_user, guard.map_x, guard.map_z, guard.storage, 1);
        else
            Editor_UiLog(ui, "editor: this window cannot open squares");
        return;
    }
    (void)Editor_UiGuarded(ui, &guard);
}

static void
squares_menu_show(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int square,
    int well_x,
    int well_y)
{
    int const scale = ToriRSChrome_Scale(ui->chrome);
    struct ToriRSChromeWidget const* well = &ui->chrome->widgets[self->graph];
    char title[TORIRS_CHROME_LABEL_MAX];

    self->menu_square = square;
    snprintf(title, sizeof(title), "m%d_%d", self->picker.squares[square].map_x,
        self->picker.squares[square].map_z);
    ToriRSChrome_PanelSetTitle(ui->chrome, self->menu_panel, title);
    ToriRSChrome_PanelMove(ui->chrome, self->menu_panel, well->x + well_x * scale, well->y + well_y * scale);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->menu_panel, 1);
}

static void
squares_menu_hide(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    if( ui->chrome->panels[self->menu_panel].visible )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->menu_panel, 0);
}

static void
squares_click(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int x,
    int y)
{
    struct Editor_Picker* picker = &self->picker;
    int const square = Editor_PickerSquareAt(picker, x, y);
    int map_x;
    int map_z;

    if( square >= 0 )
    {
        Editor_PickerSelect(picker, square, ui->shift_held);
        squares_open(ui, self, square, ui->shift_held);
    }
    else if( Editor_PickerCellAt(picker, x, y, &map_x, &map_z) )
    {
        Editor_PickerSelect(picker, -1, 0);
        picker->empty_valid = 1;
        picker->empty_map_x = map_x;
        picker->empty_map_z = map_z;
        Editor_UiLog(ui, "editor: m%d_%d is empty; New creates it here", map_x, map_z);
    }
    self->dirty = 1;
}

static int
squares_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct SquaresPanel* self = opaque;
    struct Editor_Picker* picker = &self->picker;

    if( event->widget != self->graph )
        return 0;
    if( !self->fetched )
        return 1;

    switch( event->kind )
    {
    case TORIRS_CHROME_EVENT_GRAPH_PRESS:
        squares_menu_hide(ui, self);
        self->pressing = 1;
        self->dragging = 0;
        self->press_x = self->last_x = event->x;
        self->press_y = self->last_y = event->y;
        break;
    case TORIRS_CHROME_EVENT_GRAPH_DRAG:
        if( !self->pressing )
            break;
        if( !self->dragging
            && (abs(event->x - self->press_x) > SQUARES_DRAG_SLOP
                || abs(event->y - self->press_y) > SQUARES_DRAG_SLOP) )
            self->dragging = 1;
        if( self->dragging )
        {
            Editor_PickerPan(picker, event->x - self->last_x, event->y - self->last_y);
            self->user_view = 1;
            self->dirty = 1;
        }
        self->last_x = event->x;
        self->last_y = event->y;
        break;
    case TORIRS_CHROME_EVENT_GRAPH_RELEASE:
        if( self->pressing && !self->dragging )
            squares_click(ui, self, self->press_x, self->press_y);
        self->pressing = 0;
        self->dragging = 0;
        break;
    case TORIRS_CHROME_EVENT_GRAPH_MENU:
    {
        int const square = Editor_PickerSquareAt(picker, event->x, event->y);
        if( square >= 0 )
        {
            Editor_PickerSelect(picker, square, 0);
            squares_menu_show(ui, self, square, event->x, event->y);
            self->dirty = 1;
        }
        break;
    }
    case TORIRS_CHROME_EVENT_GRAPH_HOVER:
        if( Editor_PickerHover(picker, event->x, event->y) )
            self->dirty = 1;
        break;
    case TORIRS_CHROME_EVENT_GRAPH_WHEEL:
        Editor_PickerZoom(picker, event->value, event->x, event->y);
        self->user_view = 1;
        self->dirty = 1;
        break;
    default:
        break;
    }
    return 1;
}

static void
squares_search(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct Editor_PickerSearch result;
    const char* text = ToriRSChrome_Text(ui->chrome, self->input_search);

    if( !Editor_PickerSearch(&self->picker, text, &result) )
    {
        Editor_UiLog(ui, "editor: nothing matches '%s' (try m50_50, 50 50, 3222 3218, or an area)", text);
        return;
    }
    Editor_PickerShow(&self->picker, &result);
    self->user_view = 1;
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_area, self->picker.active_tab);
    if( result.kind == EDITOR_PICKER_SEARCH_AREA )
        Editor_UiLog(ui, "editor: %s", self->picker.tabs[self->picker.active_tab].title);
    else if( result.square >= 0 )
        Editor_UiLog(ui, "editor: found m%d_%d", result.map_x, result.map_z);
    else
        Editor_UiLog(ui, "editor: m%d_%d does not exist yet; New creates it", result.map_x, result.map_z);
    self->dirty = 1;
}

static void
squares_confirm_delete(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int square)
{
    char line[TORIRS_CHROME_LABEL_MAX];

    self->confirm_square = square;
    snprintf(line, sizeof(line), "Clear every tile and loc of m%d_%d?", self->picker.squares[square].map_x,
        self->picker.squares[square].map_z);
    ToriRSChrome_SetText(ui->chrome, self->confirm_row, line);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->confirm_panel, 1);
}

static int
squares_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct SquaresPanel* self = opaque;
    struct Editor_Picker* picker = &self->picker;
    char key[EDITOR_DOC_KEY_MAX];

    for( int i = 0; i < MENU_COUNT; i++ )
    {
        int const square = self->menu_square;
        if( widget != self->menu_items[i] )
            continue;
        squares_menu_hide(ui, self);
        if( square < 0 || square >= picker->square_count )
            return 1;
        Editor_PickerSquareKey(picker, square, key);
        if( i == MENU_OPEN )
            squares_open(ui, self, square, 0);
        else if( i == MENU_ADD )
            squares_open(ui, self, square, 1);
        else if( i == MENU_NEW_VIEW )
        {
            if( ui->hooks.open_in_new_view )
                ui->hooks.open_in_new_view(
                    ui->hooks_user, picker->squares[square].map_x, picker->squares[square].map_z);
            else
                Editor_UiLog(ui, "editor: this window cannot open views");
        }
        else if( i == MENU_COPY_TO )
            Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_COPY_TO, key, -1, -1);
        else if( i == MENU_DELETE )
            squares_confirm_delete(ui, self, square);
        return 1;
    }

    if( widget == self->confirm_yes || widget == self->confirm_no )
    {
        int const square = self->confirm_square;
        ToriRSChrome_PanelSetVisible(ui->chrome, self->confirm_panel, 0);
        if( widget == self->confirm_yes && square >= 0 && square < picker->square_count )
        {
            Editor_PickerSquareKey(picker, square, key);
            if( edp_ensure_doc(ui, EDITOR_DOC_SQUARE, key) )
                (void)edp_square_clear(
                    ui, picker->squares[square].map_x, picker->squares[square].map_z);
            else
                Editor_UiLog(ui, "editor: could not open %s to clear it", key);
        }
        return 1;
    }

    if( widget == self->dropdown_area )
    {
        int const tab = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        if( tab >= 0 && tab < picker->tab_count )
        {
            Editor_PickerSetTab(picker, tab);
            self->user_view = 0;
            self->dirty = 1;
        }
        return 1;
    }
    if( widget == self->dropdown_collection )
    {
        int const row = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        (void)Editor_SquarePicker_SetCollection(
            ui, row > 0 && row < self->collection_row_count ? self->collection_labels[row] : "");
        return 1;
    }
    if( widget == self->dropdown_storage )
    {
        int const row = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        picker->storage_filter = row > 0 && row - 1 < picker->storage_count ? row - 1 : -1;
        self->dirty = 1;
        return 1;
    }
    if( widget == self->input_search )
    {
        squares_search(ui, self);
        return 1;
    }
    if( widget == self->button_open || widget == self->button_add )
    {
        if( picker->focus < 0 )
            Editor_UiLog(ui, "editor: click a square first");
        else
            squares_open(ui, self, picker->focus, widget == self->button_add);
        return 1;
    }
    if( widget == self->button_new )
    {
        int map_x = picker->empty_valid ? picker->empty_map_x : -1;
        int map_z = picker->empty_valid ? picker->empty_map_z : -1;
        if( picker->focus >= 0 )
            Editor_PickerSquareKey(picker, picker->focus, key);
        Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_NEW, picker->focus >= 0 ? key : NULL, map_x, map_z);
        return 1;
    }
    if( widget == self->button_fit )
    {
        Editor_PickerFit(picker, squares_well_w(ui, self), squares_well_h(ui, self));
        self->user_view = 0;
        self->dirty = 1;
        return 1;
    }
    if( widget == self->button_refresh )
    {
        self->want_refresh = 1;
        return 1;
    }
    return 0;
}

static void
squares_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct SquaresPanel* self = opaque;
    if( !visible )
    {
        squares_menu_hide(ui, self);
        ToriRSChrome_PanelSetVisible(ui->chrome, self->confirm_panel, 0);
    }
    self->dirty = 1;
}

static void
squares_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct SquaresPanel* self = opaque;
    (void)ui;
    Editor_PickerFree(&self->picker);
    ToriMem_Free(ToriMem_Editor(), self);
}

/* ---- entry points ------------------------------------------------------------------- */

int
Editor_SquarePicker_SelectionKey(
    struct EditorUi* ui,
    char* out,
    int out_size)
{
    struct SquaresPanel* self;
    char key[EDITOR_DOC_KEY_MAX];

    assert(ui);
    assert(out);
    assert(out_size > 0);

    self = squares_self(ui);
    if( !self || self->picker.focus < 0 || self->picker.focus >= self->picker.square_count )
        return 0;
    Editor_PickerSquareKey(&self->picker, self->picker.focus, key);
    snprintf(out, (size_t)out_size, "%s", key);
    return 1;
}

int
Editor_SquarePicker_EmptyCell(
    struct EditorUi* ui,
    int* out_map_x,
    int* out_map_z)
{
    struct SquaresPanel* self;

    assert(ui);
    assert(out_map_x);
    assert(out_map_z);

    self = squares_self(ui);
    if( !self || !self->picker.empty_valid )
        return 0;
    *out_map_x = self->picker.empty_map_x;
    *out_map_z = self->picker.empty_map_z;
    return 1;
}

int
Editor_SquarePicker_SetCollection(
    struct EditorUi* ui,
    const char* name)
{
    struct SquaresPanel* self;

    assert(ui);
    self = squares_self(ui);
    assert(self);
    snprintf(self->collection, sizeof(self->collection), "%s", name ? name : "");
    self->user_view = 0;
    if( !self->fetched )
        squares_fetch(ui, self);
    squares_apply_collection(ui, self, Editor_Areas_File(ui, NULL));
    self->collection_built = 0;
    squares_collection_rows(ui, self);
    if( self->fetched && ui->chrome->widgets[self->graph].w > 0 )
        Editor_PickerFit(&self->picker, squares_well_w(ui, self), squares_well_h(ui, self));
    if( self->collection[0] )
        Editor_UiLog(ui, "editor: the picker shows collection %s (%d squares)", self->collection,
            self->picker.filter_shown);
    return self->picker.filter_on;
}

const char*
Editor_SquarePicker_Collection(struct EditorUi* ui)
{
    struct SquaresPanel* self;

    assert(ui);
    self = squares_self(ui);
    assert(self);
    return self->collection;
}

void
Editor_SquarePicker_Refresh(struct EditorUi* ui)
{
    struct SquaresPanel* self;

    assert(ui);
    self = squares_self(ui);
    if( self )
        self->want_refresh = 1;
}

const struct Editor_Picker*
edp_squares_picker(struct EditorUi* ui)
{
    struct SquaresPanel* self;

    assert(ui);
    self = squares_self(ui);
    if( !self )
        return NULL;
    if( !self->fetched )
        squares_fetch(ui, self);
    return self->fetched ? &self->picker : NULL;
}

void
Editor_SquarePicker_Register(struct EditorUi* ui)
{
    struct SquaresPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;
    int x;

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    Editor_PickerInit(&self->picker);
    Editor_PickerDefaultColors(&self->colors);
    self->colors.dirty = chrome->theme.accent ? chrome->theme.accent : self->colors.dirty;
    self->menu_square = -1;
    self->confirm_square = -1;

    x = edp_surface_w(ui) > edp_px(ui, EDP_PICKER_W + 8)
            ? edp_surface_w(ui) - edp_px(ui, EDP_PICKER_W + 8)
            : edp_px(ui, EDP_COL_X + EDP_COL_W + 8);
    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, x, edp_px(ui, EDP_TOP_Y), edp_px(ui, EDP_PICKER_W), "Squares");
    assert(self->panel >= 0);
    self->input_search = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(chrome, self->input_search, "m50_50, 50 50, 3222 3218, or an area name; Enter");
    self->dropdown_area = ToriRSChrome_Dropdown(chrome, self->panel, "Area", k_loading_titles, 1, 0);
    self->dropdown_storage = ToriRSChrome_Dropdown(chrome, self->panel, "Storage", NULL, 0, -1);
    self->dropdown_collection = ToriRSChrome_Dropdown(chrome, self->panel, "Collection", NULL, 0, -1);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_collection, "Only the squares of one named collection (Areas)");
    self->graph = ToriRSChrome_Graph(chrome, self->panel, "", edp_px(ui, SQUARES_WELL_H));
    self->row_hover = ToriRSChrome_Label(chrome, self->panel, " ");
    self->row_status = ToriRSChrome_Label(chrome, self->panel, "");
    self->button_open = ToriRSChrome_Button(chrome, self->panel, "Open");
    self->button_add = ToriRSChrome_Button(chrome, self->panel, "Add to open set");
    self->button_new = ToriRSChrome_Button(chrome, self->panel, "New square...");
    ToriRSChrome_SetTooltip(chrome, self->button_new,
        "Create a square: its storage (maps/ or a lane) and its template");
    self->button_fit = ToriRSChrome_Button(chrome, self->panel, "Fit");
    self->button_refresh = ToriRSChrome_Button(chrome, self->panel, "Refresh");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    self->menu_panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_MENU, 0, 0, 0, "Square");
    assert(self->menu_panel >= 0);
    for( int i = 0; i < MENU_COUNT; i++ )
        self->menu_items[i] = ToriRSChrome_MenuItem(chrome, self->menu_panel, k_menu_rows[i]);
    ToriRSChrome_PanelSetVisible(chrome, self->menu_panel, 0);

    self->confirm_panel = ToriRSChrome_ModalAdd(chrome, "Delete square", edp_px(ui, 260));
    assert(self->confirm_panel >= 0);
    self->confirm_row = ToriRSChrome_Label(chrome, self->confirm_panel, "");
    ToriRSChrome_Label(chrome, self->confirm_panel, "One undo step; nothing is written until Save.");
    self->confirm_yes = ToriRSChrome_Button(chrome, self->confirm_panel, "Clear it");
    self->confirm_no = ToriRSChrome_Button(chrome, self->confirm_panel, "Cancel");
    ToriRSChrome_PanelSetVisible(chrome, self->confirm_panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "squares";
    desc.title = "Squares";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = squares_tick;
    desc.activate = squares_activate;
    desc.event = squares_event;
    desc.set_visible = squares_set_visible;
    desc.destroy = squares_destroy;
    desc.stretch = squares_stretch;
    Editor_UiAddPanel(ui, &desc);
}
