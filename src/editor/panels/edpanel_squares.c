/*
 * The square picker (docs/MAPED_DESIGN.html #squares): a map of every square
 * the content tree holds, one page per world-map area plus Unmapped, chosen
 * with the Area dropdown (a tab per area made a strip of ~50 overlapping
 * labels nobody could read).
 *
 *   the well       the world map of the page's area (the CS2 world map's
 *                  own picture, editor_worldmap.h) under the square grid,
 *                  squares the page does not show dimmed; without the App's
 *                  map services (headless, web) thumbnail cells in each
 *                  square's average floor colour. North up, map coordinates
 *                  on the axes; wheel zooms about the pointer, a drag pans
 *   hover          the readout names the square, its area, its spawns, its
 *                  storage
 *   click          opens the square in this window (through the save guard)
 *   shift-click    adds it to the open set
 *   right-click    Open, Open in new view, Copy to..., Delete (clear)
 *   empty cell     a click opens New square on it
 *   Find           m50_50, "50 50", an absolute tile "3222 3218" (pinned),
 *                  an area or region name, a collection name, lane:<name>,
 *                  instance: (edpanel_picker_find.h); a cell is shown at
 *                  least EDITOR_PICKER_ZOOM_SHOW px a square and focused
 *   keys           with the well focused (context K, docs/MAPED_HOTKEYS.md
 *                  "Picker"): the arrows move the focused cell (outlined
 *                  white), Enter opens it, Shift+Enter adds it, Backspace
 *                  goes to the previous square of the open set, Escape
 *                  clears Find; + and - zoom
 *   open-set strip one chip per square this window holds, in open order, a
 *                  dot on an unsaved one: click goes to it, Shift+click
 *                  closes it through the save guard, a drag reorders; chips
 *                  past the width fold into "+N" (the rest in its tooltip)
 *   detail pane    under the atlas: the focused (else hovered) square's key,
 *                  area, tiles, contents, state, collections, its actions,
 *                  and the last EDITOR_PICKER_RECENT_MAX squares opened
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
#include "edpanel_picker_find.h"
#include "edpanel_picker_nav.h"
#include "edpanels.h"
#include "editor/editor_areas.h"
#include "editor/editor_worldmap.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "ui/torirs_chrome_metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The well's height when the panel is content-sized, and the least it shrinks
 * to when the layout gives the panel a height to fill (1x pixels). The least
 * is what lets the picker into the 1280x800 column under Terrain and Palette
 * (758 px between the menu bar and the status bar, 160 each): the rows above
 * the well and the folded pane take 349, and at 120 the picker asked for 31 px
 * more than was left and the layout hid it. 88 is eleven grid rows; the
 * spare height the layout shares out goes to the well first. */
#define SQUARES_WELL_H 300
#define SQUARES_WELL_MIN_H 88
/* A press that moves further than this is a pan and never a click. */
#define SQUARES_DRAG_SLOP 3
/* Two clicks on one square this close are a double-click. */
#define SQUARES_DOUBLE_TICKS 30
#define SQUARES_DOUBLE_MS 450
/* TORIRS_EDITOR_PICKER waits this many ticks for the layout. */
#define SQUARES_ENV_TICKS 20
#define SQUARES_STORAGE_ROWS 32
/* "all" plus this many collections in the Collection dropdown. */
#define SQUARES_COLLECTION_ROWS 32
/* The open-set strip (1x px): its height, a chip's text at most, a chip's
 * padding either side, the gap between chips, the unsaved dot. */
#define SQUARES_STRIP_H 18
#define SQUARES_CHIP_TEXT_W 120
#define SQUARES_CHIP_PAD 5
#define SQUARES_CHIP_GAP 4
#define SQUARES_CHIP_DOT 4
/* The strip's GRAPH keys: a chip is 1 + its index, the "+N" chip this. */
#define SQUARES_KEY_MORE 1000
/* A pane row's text at most (the chrome's text limit). */
#define SQUARES_PANE_TEXT_MAX TORIRS_CHROME_INPUT_MAX
/* The detail pane's rows past its heading (the six key / value rows, the
 * five actions past Open and Add, Recent) fold away when the panel is given
 * a height that would leave the atlas less than this (1x px): the heading,
 * Open and Add stay, the atlas keeps its room. */
#define SQUARES_PANE_WELL_MIN_H 200
#define SQUARES_PANE_ACTIONS 5
/* What the folding rows take (1x px): the chrome's KEY_VALUE rows, then the
 * buttons and the dropdown, each with the row gap. */
#define SQUARES_PANE_FOLD_H                                                                                      \
    ((EDITOR_SQUARES_PANE_ROWS - 1) * (TORIRS_CHROME_M_DATA_ROW_H + TORIRS_CHROME_M_ROW_GAP)                     \
     + (SQUARES_PANE_ACTIONS + 1) * (TORIRS_CHROME_M_ROW_H + TORIRS_CHROME_M_ROW_GAP))

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

enum
{
    SQUARES_AREA_PAGE = 0,
    SQUARES_AREA_LANE,
    SQUARES_AREA_INSTANCE,
};

/* The detail pane's rows (EDITOR_SQUARES_PANE_ROWS): the heading, then these
 * keys. */
static const char* const k_pane_keys[EDITOR_SQUARES_PANE_ROWS] = {
    "", "Square", "Area", "Tiles", "Contents", "State", "Collections",
};

struct SquaresPanel
{
    struct Editor_Picker picker;
    struct Editor_PickerColors colors;
    /** The world map under the grid (editor_worldmap.h). */
    struct Editor_WorldMap map;
    /** TORIRS_EDITOR_PICKER was applied (once). */
    int env_done;
    /** Ticks seen, and the last click (for a double-click). */
    int ticks;
    int click_valid;
    int click_map_x;
    int click_map_z;
    int click_tick;
    int click_ms;
    int click_opened_here;

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

    /* Find (edpanel_picker_find.h): the tile the last tile search pinned. */
    int pin_valid;
    int pin_x;
    int pin_z;
    int pin_level;

    /** The Area dropdown's rows: the atlas's pages, then "lane: <name>" per
     *  lane storage, then "Instance space"; rebuilt with the atlas (the
     *  dropdown borrows them). */
    char (*area_labels)[TORIRS_CHROME_LABEL_MAX];
    const char** area_rows;
    int area_row_count;
    int area_row_cap;
    /** What the dropdown shows: a page (SQUARES_AREA_PAGE), a lane's squares
     *  (`area_lane` its storage), the instance space. */
    int area_mode;
    int area_lane;

    /* The open-set strip: a GRAPH under the atlas. */
    int strip;
    struct Editor_PickerOpenSet open_set;
    struct Editor_PickerStripLayout strip_layout;
    int strip_pressing;
    int strip_dragging;
    int strip_press_chip;
    int strip_press_x;
    int strip_hover_key;
    int strip_dirty;
    int strip_track;

    /* The detail pane: a heading and six key / value rows, each fitted to
     * its track with the full text as its tooltip when cut. */
    int pane_rows[EDITOR_SQUARES_PANE_ROWS];
    char pane_full[EDITOR_SQUARES_PANE_ROWS][SQUARES_PANE_TEXT_MAX];
    int pane_track[EDITOR_SQUARES_PANE_ROWS];
    uint32_t pane_hash;
    int button_new_view;
    int button_copy;
    int button_instance;
    int button_clear;
    int button_delete;
    /** The rows past the heading are shown (squares_size_well folds them
     *  when the panel is short). */
    int pane_open;
    struct Editor_PickerRecent recent;
    int dropdown_recent;
    char recent_labels[EDITOR_PICKER_RECENT_MAX + 1][TORIRS_CHROME_LABEL_MAX];
    const char* recent_rows[EDITOR_PICKER_RECENT_MAX + 1];
    int recent_dirty;
    /** The table the Open / Add keycaps were read from. */
    uint32_t hotkeys_serial;

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

static void
squares_click(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int x,
    int y);
static int
squares_strip_w(
    struct EditorUi* ui,
    struct SquaresPanel* self);
static void
squares_draw_strip(
    struct EditorUi* ui,
    struct SquaresPanel* self);
static void
squares_recent_rows(
    struct EditorUi* ui,
    struct SquaresPanel* self);
static void
squares_pane_update(
    struct EditorUi* ui,
    struct SquaresPanel* self);

static struct SquaresPanel*
squares_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "squares");
}

int
Editor_Squares_StorageName(
    struct EditorUi* ui,
    int storage_index,
    char out[EDITOR_DOC_STORAGE_MAX])
{
    struct SquaresPanel* self;
    const char* storage;

    assert(ui);
    assert(out);
    out[0] = '\0';
    self = squares_self(ui);
    if( !self || storage_index < 0 || storage_index >= self->picker.storage_count )
        return 0;
    storage = self->picker.storages[storage_index];
    snprintf(out, EDITOR_DOC_STORAGE_MAX, "%s", strcmp(storage, "maps") == 0 ? "" : storage);
    return 1;
}

/* ---- the atlas ------------------------------------------------------------------- */

/** The storage the Area dropdown's lane row `row` stands for, or -1 when
 *  the row is no lane's. */
static int
squares_area_row_lane(
    const struct SquaresPanel* self,
    int row)
{
    const struct Editor_Picker* picker = &self->picker;
    char lane[EDITOR_PICKER_NAME_MAX];
    int at = picker->tab_count;

    for( int s = 0; s < picker->storage_count; s++ )
    {
        if( !Editor_PickerLaneName(picker->storages[s], lane) )
            continue;
        if( at == row )
            return s;
        at++;
    }
    return -1;
}

/** The Area dropdown's row for what the picker shows now. */
static int
squares_area_row(const struct SquaresPanel* self)
{
    if( self->area_mode == SQUARES_AREA_INSTANCE )
        return self->area_row_count - 1;
    if( self->area_mode == SQUARES_AREA_LANE )
        for( int row = self->picker.tab_count; row < self->area_row_count; row++ )
            if( squares_area_row_lane(self, row) == self->area_lane )
                return row;
    return self->picker.active_tab;
}

static void
squares_area_show(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_area, squares_area_row(self));
}

/**
 * The Area dropdown (design section 5): the surface, each dungeon map, each
 * instance map, Unmapped (the atlas's pages), then each lane's squares as
 * "lane: <name>", then "Instance space". Built when the atlas arrives, never
 * per frame; the dropdown borrows the rows.
 */
static void
squares_point_widgets(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct Editor_Picker* picker = &self->picker;
    int const need = picker->tab_count + picker->storage_count + 1;
    char lane[EDITOR_PICKER_NAME_MAX];
    int n = 0;

    if( need > self->area_row_cap )
    {
        ToriMem_Free(ToriMem_Editor(), self->area_labels);
        ToriMem_Free(ToriMem_Editor(), (void*)self->area_rows);
        self->area_labels = ToriMem_Calloc(ToriMem_Editor(), (size_t)need, sizeof(*self->area_labels));
        assert(self->area_labels);
        self->area_rows = ToriMem_Calloc(ToriMem_Editor(), (size_t)need, sizeof(*self->area_rows));
        assert(self->area_rows);
        self->area_row_cap = need;
    }
    for( int t = 0; t < picker->tab_count; t++ )
    {
        snprintf(self->area_labels[n], TORIRS_CHROME_LABEL_MAX, "%s", picker->tab_titles[t]);
        self->area_rows[n] = self->area_labels[n];
        n++;
    }
    for( int s = 0; s < picker->storage_count; s++ )
    {
        if( !Editor_PickerLaneName(picker->storages[s], lane) )
            continue;
        snprintf(self->area_labels[n], TORIRS_CHROME_LABEL_MAX, "lane: %s", lane);
        self->area_rows[n] = self->area_labels[n];
        n++;
    }
    /* The picker's own "Instance space" page is one of the tabs above once
     * an atlas came (Editor_PickerInstanceTab); the row of its own is only
     * for a picker without one. */
    if( Editor_PickerInstanceTab(picker) < 0 )
    {
        snprintf(self->area_labels[n], TORIRS_CHROME_LABEL_MAX, "Instance space");
        self->area_rows[n] = self->area_labels[n];
        n++;
    }
    self->area_row_count = n;
    /* A lane the atlas no longer holds: back to its page. */
    if( self->area_mode == SQUARES_AREA_LANE
        && (self->area_lane >= picker->storage_count || picker->storage_filter != self->area_lane) )
        self->area_mode = SQUARES_AREA_PAGE;
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_area, self->area_rows, n, squares_area_row(self));
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
    /* The lane rows follow the storages, so the rows are rebuilt with every
     * atlas (a new lane is a new row), not only when the pages changed. */
    (void)Editor_PickerSetAtlas(&self->picker, atlas);
    squares_point_widgets(ui, self);
    squares_storage_rows(ui, self);
    self->strip_dirty = 1;
    self->pane_hash = 0;
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
/** Show or fold the pane's rows past its heading. */
static void
squares_pane_fold(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int open)
{
    int const rows[] = {
        self->button_new_view, self->button_copy, self->button_instance,
        self->button_clear,    self->button_delete, self->dropdown_recent,
    };

    _Static_assert(sizeof(rows) / sizeof(rows[0]) == SQUARES_PANE_ACTIONS + 1, "the folding rows");
    self->pane_open = open;
    for( int row = 1; row < EDITOR_SQUARES_PANE_ROWS; row++ )
        ToriRSChrome_SetHidden(ui->chrome, self->pane_rows[row], !open);
    for( size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++ )
        ToriRSChrome_SetHidden(ui->chrome, rows[i], !open);
    self->pane_hash = 0;
}

/**
 * The well fills the panel. Content-sized (no layout height), it is
 * SQUARES_WELL_H at the chrome's scale and the pane is open; given a height
 * by the layout or a resize, it takes what the other rows leave, down to
 * SQUARES_WELL_MIN_H, and the pane folds when it would leave the atlas less
 * than SQUARES_PANE_WELL_MIN_H (the default layout's docked column at 1x).
 * Converges in one Build: the other rows' height does not depend on it, and
 * the fold is judged on the well the panel would have with the pane folded,
 * whichever it is now, so it does not flip back and forth.
 */
static void
squares_size_well(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct ToriRSChromePanel const* p = &ui->chrome->panels[self->panel];
    struct ToriRSChromeWidget const* well = &ui->chrome->widgets[self->graph];
    int want = edp_px(ui, SQUARES_WELL_H);
    int open = 1;

    if( p->fixed_h > 0 && p->view_h > 0 && p->content_h > 0 )
    {
        int const pane = edp_px(ui, SQUARES_PANE_FOLD_H);
        int const folded = p->view_h - (p->content_h - well->view_h) + (self->pane_open ? pane : 0);
        open = folded - pane >= edp_px(ui, SQUARES_PANE_WELL_MIN_H);
        want = folded - (open ? pane : 0);
        if( want < edp_px(ui, SQUARES_WELL_MIN_H) )
            want = edp_px(ui, SQUARES_WELL_MIN_H);
    }
    if( open != self->pane_open )
        squares_pane_fold(ui, self, open);
    if( want != well->view_h )
        ToriRSChrome_SetCustomHeight(ui->chrome, self->graph, want);
}

/** EditorUiPanel::stretch: the well is the part that fills, and the detail
 *  pane's folding rows go with it (squares_size_well folds them when the
 *  height is short). So the least is the well's least with the pane folded,
 *  and the asking height has the pane open: measured with the pane open, the
 *  picker's least was taller than the 1280x800 column could give after
 *  Terrain and Palette, and the default layout hid the picker. */
static int
squares_stretch(
    struct EditorUi* ui,
    void* opaque,
    int* out_min_h,
    int* out_natural_h)
{
    struct SquaresPanel* self = opaque;
    int const pane = edp_px(ui, SQUARES_PANE_FOLD_H);

    assert(out_min_h);
    assert(out_natural_h);
    *out_min_h = edp_px(ui, SQUARES_WELL_MIN_H);
    *out_natural_h = edp_px(ui, SQUARES_WELL_H) + pane;
    return ui->chrome->widgets[self->graph].view_h + (self->pane_open ? pane : 0);
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
    /* What the item budget dropped (Editor_Picker.dropped, the drawing
     * half's count; the whole line goes to the console once per change). */
    {
        char tip[TORIRS_CHROME_LABEL_MAX];
        size_t used = 0;
        tip[0] = '\0';
        for( int k = 0; picker->truncated && k < EDITOR_PICKER_DROP_KINDS && used + 1 < sizeof(tip); k++ )
        {
            if( !picker->dropped[k] )
                continue;
            used += (size_t)snprintf(tip + used, sizeof(tip) - used, "%s%d %s", used ? ", " : "not drawn: ",
                picker->dropped[k], Editor_PickerDropName((enum Editor_PickerDropKind)k));
        }
        ToriRSChrome_SetTooltip(ui->chrome, self->row_status, tip);
    }
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

/**
 * TORIRS_EDITOR_PICKER, for a photograph: once the atlas is in and the well
 * has a size, comma-separated, in order:
 *
 *   <area>        the page (an area or region name as Find takes it; "-"
 *                 keeps it)
 *   fit           Fit the page
 *   zoom=<px>     px a square (real, clamped to the zoom range)
 *   at=<x>:<z>    centre square (x, z)
 *   click=<x>:<z> click square (x, z) (as a plain click: an empty one opens
 *                 New square); repeatable, each a separate click
 *   new           click the centred square
 *
 * The older short form `<area>,<zoom>,<x>,<z>[,new]` (one number the zoom,
 * two the square, three both) still works.
 */
static void
squares_env_click(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int map_x,
    int map_z)
{
    int x;
    int y;
    int w;
    int h;

    Editor_PickerCellBox(&self->picker, map_x, map_z, &x, &y, &w, &h);
    /* Two env clicks are two separate clicks, never a double-click. */
    self->click_valid = 0;
    squares_click(ui, self, x + w / 2, y + h / 2);
}

static void
squares_apply_env(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct Editor_Picker* picker = &self->picker;
    const char* env;
    char text[256];
    char* save = NULL;
    double numbers[3];
    int n = 0;
    int clicks[8][2];
    int click_count = 0;
    int click_new = 0;
    int centre_valid = 0;
    int centre_x = 0;
    int centre_z = 0;
    double zoom = 0;
    int fit = 0;
    char area[EDITOR_PICKER_NAME_MAX] = { 0 };

    /* After the layout has settled (a saved layout resizes the panel a few
     * ticks in): a zoom set before that would be for another well. */
    if( self->env_done || picker->tab_count == 0 || picker->well_w <= 0 || self->ticks < SQUARES_ENV_TICKS )
        return;
    self->env_done = 1;
    env = getenv("TORIRS_EDITOR_PICKER");
    if( !env || !env[0] )
        return;
    snprintf(text, sizeof(text), "%s", env);
    for( char* token = strtok_r(text, ",", &save); token; token = strtok_r(NULL, ",", &save) )
    {
        char* end = NULL;
        double const value = strtod(token, &end);
        int x;
        int z;
        if( end && end != token && *end == '\0' && n < 3 )
            numbers[n++] = value;
        else if( strcmp(token, "new") == 0 )
            click_new = 1;
        else if( strcmp(token, "fit") == 0 )
            fit = 1;
        else if( strncmp(token, "zoom=", 5) == 0 )
            zoom = strtod(token + 5, NULL);
        else if( sscanf(token, "at=%d:%d", &x, &z) == 2 )
        {
            centre_valid = 1;
            centre_x = x;
            centre_z = z;
        }
        else if( sscanf(token, "click=%d:%d", &x, &z) == 2 && click_count < 8 )
        {
            clicks[click_count][0] = x;
            clicks[click_count][1] = z;
            click_count++;
        }
        else if( !area[0] )
            snprintf(area, sizeof(area), "%s", token);
    }
    if( n == 1 || n == 3 )
        zoom = numbers[0];
    if( n >= 2 )
    {
        centre_valid = 1;
        centre_x = (int)numbers[n - 2];
        centre_z = (int)numbers[n - 1];
    }
    if( area[0] && strcmp(area, "-") != 0 )
    {
        struct Editor_PickerSearch result;
        if( Editor_PickerSearch(picker, area, &result) && result.kind == EDITOR_PICKER_SEARCH_AREA )
        {
            Editor_PickerSetTab(picker, result.tab);
            self->area_mode = SQUARES_AREA_PAGE;
            squares_area_show(ui, self);
        }
        else
            Editor_UiLog(ui, "editor: TORIRS_EDITOR_PICKER: no area '%s'", area);
    }
    if( fit )
        Editor_PickerFit(picker, 0, 0);
    if( zoom > 0 )
    {
        const struct Editor_PickerTab* tab = &picker->tabs[picker->active_tab];
        Editor_PickerZoomTo(picker, zoom, (EDITOR_PICKER_LEFT + picker->well_w) / 2,
            (EDITOR_PICKER_TOP + picker->well_h) / 2);
        /* The page's middle at the new zoom, until a square is named. */
        Editor_PickerCentre(picker, (tab->min_x + tab->max_x) / 2, (tab->min_z + tab->max_z) / 2);
    }
    if( centre_valid )
        Editor_PickerCentre(picker, centre_x, centre_z);
    for( int i = 0; i < click_count; i++ )
        squares_env_click(ui, self, clicks[i][0], clicks[i][1]);
    if( click_new && centre_valid )
        squares_env_click(ui, self, centre_x, centre_z);
    /* A bare `fit` stays fitted, as the Fit button leaves it. */
    self->user_view = zoom > 0 || centre_valid || click_count > 0 || !fit;
    self->dirty = 1;
    Editor_UiLog(ui, "editor: TORIRS_EDITOR_PICKER=%s (%s, %.1f px a square)", env,
        picker->tabs[picker->active_tab].title, picker->zoom);
}

/** The keycaps of the pane's Open and Add (the Picker rows) and of New
 *  square, from the live table: a rebind relabels them on the next frame. */
static void
squares_chords(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    edp_button_chord(ui, self->button_open, EDITOR_HK_PICKER_OPEN);
    edp_button_chord(ui, self->button_add, EDITOR_HK_PICKER_ADD);
    edp_button_chord(ui, self->button_new, EDITOR_HK_FILE_NEW);
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

    self->ticks++;
    squares_apply_env(ui, self);
    if( Editor_WorldMapUpdate(&self->map, &ui->app_services, picker) )
        self->dirty = 1;

    if( ui->editor )
    {
        const struct EditorUiViewFrame* own = NULL;
        for( int f = 0; f < EDITOR_UI_VIEW_FRAMES_MAX; f++ )
            if( ui->frames[f].used && ui->frames[f].slot == ui->view_slot )
                own = &ui->frames[f];
        Editor_PickerNoteDocument(picker, &ui->editor->doc, own);
    }
    {
        uint32_t const flags = squares_hash_flags(picker);
        uint32_t const frames = squares_hash_frames(ui);
        if( flags != self->flags_hash || frames != self->frames_hash )
        {
            self->flags_hash = flags;
            self->frames_hash = frames;
            self->dirty = 1;
            /* What this window holds moved with the flags: the strip
             * follows, in open order. */
            if( Editor_PickerOpenSetSync(&self->open_set, picker) )
                self->strip_dirty = 1;
        }
    }
    if( self->hotkeys_serial != ui->hotkeys.serial )
    {
        self->hotkeys_serial = ui->hotkeys.serial;
        squares_chords(ui, self);
    }
    if( self->strip_dirty || squares_strip_w(ui, self) != self->strip_track )
        squares_draw_strip(ui, self);
    squares_recent_rows(ui, self);
    squares_pane_update(ui, self);
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
    guard.storage_index = picker->squares[square].storage;
    squares_storage_of(picker, square, guard.storage);
    {
        char key[EDITOR_DOC_KEY_MAX];
        Editor_PickerSquareKey(picker, square, key);
        Editor_PickerRecentPush(&self->recent, key);
        self->recent_dirty = 1;
    }
    if( add )
    {
        /* Adding keeps what is open: nothing unsaved is at stake. */
        Editor_UiOpenSquare(ui, guard.map_x, guard.map_z, guard.storage, guard.storage_index, 1);
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
    int map_x = -1;
    int map_z = -1;
    int const on_cell = Editor_PickerCellAt(picker, x, y, &map_x, &map_z);
    int const now_ms = (int)ui->chrome->now_ms;
    int const twice = on_cell && self->click_valid && map_x == self->click_map_x && map_z == self->click_map_z
                      && self->ticks - self->click_tick <= SQUARES_DOUBLE_TICKS
                      && (ui->chrome->now_ms == 0 || now_ms - self->click_ms <= SQUARES_DOUBLE_MS);

    /* The second click of a double-click opens the square in place; the
     * first did already unless it was a shift-click. */
    self->click_valid = !twice;
    self->click_map_x = map_x;
    self->click_map_z = map_z;
    self->click_tick = self->ticks;
    self->click_ms = now_ms;
    if( twice )
    {
        if( square >= 0 && !self->click_opened_here )
        {
            Editor_PickerSelect(picker, square, 0);
            squares_open(ui, self, square, 0);
        }
        self->dirty = 1;
        return;
    }
    self->click_opened_here = square >= 0 && !ui->shift_held;

    if( square >= 0 )
    {
        Editor_PickerSelect(picker, square, ui->shift_held);
        squares_open(ui, self, square, ui->shift_held);
    }
    else if( on_cell )
    {
        Editor_PickerSelect(picker, -1, 0);
        picker->empty_valid = 1;
        picker->empty_map_x = map_x;
        picker->empty_map_z = map_z;
        Editor_UiLog(ui, "editor: m%d_%d is empty: New square", map_x, map_z);
        Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_NEW, NULL, map_x, map_z);
    }
    self->dirty = 1;
}

/* ---- Find ----------------------------------------------------------------------- */

/** The tab a whole-world view stands on: the Full map page, else Unmapped,
 *  else the page shown. */
static int
squares_world_tab(const struct Editor_Picker* picker)
{
    for( int t = 0; t < picker->tab_count; t++ )
        if( picker->tabs[t].area == EDITOR_PICKER_TAB_FULL )
            return t;
    for( int t = 0; t < picker->tab_count; t++ )
        if( picker->tabs[t].area == EDITOR_PICKER_TAB_UNMAPPED )
            return t;
    return picker->active_tab;
}

/** Back to a page from a lane or the instance space: the lane's storage
 *  filter goes with it. */
static void
squares_leave_area_mode(struct SquaresPanel* self)
{
    if( self->area_mode == SQUARES_AREA_LANE )
        self->picker.storage_filter = -1;
    self->area_mode = SQUARES_AREA_PAGE;
}

/** Frame squares [x0, x1] x [z0, z1] in the well, edge to edge in the
 *  narrower direction. */
static void
squares_frame_rect(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int x0,
    int x1,
    int z0,
    int z1)
{
    struct Editor_Picker* picker = &self->picker;
    int const avail_w = squares_well_w(ui, self) - EDITOR_PICKER_LEFT;
    int const avail_h = squares_well_h(ui, self) - EDITOR_PICKER_TOP;
    double const zx = (double)avail_w / (double)(x1 - x0 + 1);
    double const zz = (double)avail_h / (double)(z1 - z0 + 1);

    picker->well_w = squares_well_w(ui, self);
    picker->well_h = squares_well_h(ui, self);
    Editor_PickerZoomTo(picker, zx < zz ? zx : zz, (EDITOR_PICKER_LEFT + picker->well_w) / 2,
        (EDITOR_PICKER_TOP + picker->well_h) / 2);
    Editor_PickerCentre(picker, (x0 + x1) / 2, (z0 + z1) / 2);
}

static void
squares_show_lane(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int storage)
{
    struct Editor_Picker* picker = &self->picker;
    char lane[EDITOR_PICKER_NAME_MAX];
    int count = 0;
    int x0 = 1 << 30;
    int x1 = -1;
    int z0 = 1 << 30;
    int z1 = -1;

    assert(storage >= 0);
    assert(storage < picker->storage_count);
    if( picker->tab_count > 0 )
        Editor_PickerSetTab(picker, squares_world_tab(picker));
    picker->storage_filter = storage;
    self->area_mode = SQUARES_AREA_LANE;
    self->area_lane = storage;
    for( int i = 0; i < picker->square_count; i++ )
    {
        const struct Editor_PickerSquare* square = &picker->squares[i];
        if( square->storage != storage )
            continue;
        count++;
        x0 = square->map_x < x0 ? square->map_x : x0;
        x1 = square->map_x > x1 ? square->map_x : x1;
        z0 = square->map_z < z0 ? square->map_z : z0;
        z1 = square->map_z > z1 ? square->map_z : z1;
    }
    if( count > 0 )
        squares_frame_rect(ui, self, x0, x1, z0, z1);
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_storage, storage + 1);
    squares_area_show(ui, self);
    (void)Editor_PickerLaneName(picker->storages[storage], lane);
    Editor_UiLog(ui, "editor: lane %s: %d square%s", lane, count, count == 1 ? "" : "s");
    self->user_view = 1;
    self->dirty = 1;
}

static void
squares_show_instance(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct Editor_Picker* picker = &self->picker;
    int const instance_tab = Editor_PickerInstanceTab(picker);
    int count = 0;

    squares_leave_area_mode(self);
    if( instance_tab >= 0 )
    {
        /* The picker's Instance space page (workbench 6.4, the drawing
         * half) frames the whole space and hatches where no square is. */
        Editor_PickerSetTab(picker, instance_tab);
        squares_area_show(ui, self);
        Editor_UiLog(ui, "editor: %s", picker->tabs[picker->active_tab].title);
        self->user_view = 0;
        self->dirty = 1;
        return;
    }
    if( picker->tab_count > 0 )
        Editor_PickerSetTab(picker, squares_world_tab(picker));
    self->area_mode = SQUARES_AREA_INSTANCE;
    squares_frame_rect(ui, self, EDITOR_PICKER_INSTANCE_X0, EDITOR_PICKER_INSTANCE_X1, EDITOR_PICKER_INSTANCE_Z0,
        EDITOR_PICKER_INSTANCE_Z1);
    for( int i = 0; i < picker->square_count; i++ )
    {
        const struct Editor_PickerSquare* square = &picker->squares[i];
        count += square->map_x >= EDITOR_PICKER_INSTANCE_X0 && square->map_x <= EDITOR_PICKER_INSTANCE_X1
                 && square->map_z >= EDITOR_PICKER_INSTANCE_Z0 && square->map_z <= EDITOR_PICKER_INSTANCE_Z1;
    }
    squares_area_show(ui, self);
    Editor_UiLog(ui, "editor: instance space m%d_%d .. m%d_%d: %d square%s", EDITOR_PICKER_INSTANCE_X0,
        EDITOR_PICKER_INSTANCE_Z0, EDITOR_PICKER_INSTANCE_X1, EDITOR_PICKER_INSTANCE_Z1, count, count == 1 ? "" : "s");
    self->user_view = 1;
    self->dirty = 1;
}

/** The collection names the Collection dropdown holds (the AREAS document's,
 *  as last parsed), into `names`. */
static int
squares_collection_names(
    const struct SquaresPanel* self,
    const char* names[SQUARES_COLLECTION_ROWS])
{
    int n = 0;
    for( int row = 1; row < self->collection_row_count && n < SQUARES_COLLECTION_ROWS; row++ )
        names[n++] = self->collection_labels[row];
    return n;
}

static int
squares_find(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    const char* text)
{
    struct Editor_Picker* picker = &self->picker;
    struct Editor_PickerFindQuery query;
    struct Editor_PickerFindMatch match;
    const char* names[SQUARES_COLLECTION_ROWS];
    int const name_count = squares_collection_names(self, names);

    if( !Editor_PickerFindParse(text, &query) )
    {
        Editor_UiStatus(ui, "Find: '%s' is not a square, tile or name (m50_50, 50 50, 3222 3218, Lumbridge, "
                            "lane:name, instance:)", text);
        return 0;
    }
    if( !Editor_PickerFindResolve(picker, &query, names, name_count, &match) )
    {
        if( query.kind == EDITOR_PICKER_FIND_LANE )
            Editor_UiStatus(ui, "Find: no lane is called '%s'", query.name);
        else
            Editor_UiStatus(ui, "Find: no area, region or collection is called '%s'", query.name);
        return 0;
    }
    self->user_view = 1;
    switch( match.kind )
    {
    case EDITOR_PICKER_MATCH_CELL:
    {
        struct Editor_PickerSearch result;
        memset(&result, 0, sizeof(result));
        result.kind = match.pin_valid ? EDITOR_PICKER_SEARCH_TILE : EDITOR_PICKER_SEARCH_SQUARE;
        result.map_x = match.map_x;
        result.map_z = match.map_z;
        result.tile_x = match.pin_x;
        result.tile_z = match.pin_z;
        result.level = match.pin_level;
        result.square = match.square;
        result.tab = match.tab;
        /* A lane's page keeps showing a square of the lane; anything else
         * goes back to the page the square is on. */
        if( self->area_mode == SQUARES_AREA_INSTANCE
            || (self->area_mode == SQUARES_AREA_LANE
                && (match.square < 0 || picker->squares[match.square].storage != self->area_lane)) )
        {
            int const was_lane = self->area_mode == SQUARES_AREA_LANE;
            squares_leave_area_mode(self);
            if( was_lane )
                ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_storage, 0);
        }
        /* Shown at least EDITOR_PICKER_ZOOM_SHOW px a square, centred,
         * focused (an empty cell is the New dialog's prefill). */
        picker->well_w = squares_well_w(ui, self);
        picker->well_h = squares_well_h(ui, self);
        Editor_PickerShow(picker, &result);
        self->pin_valid = match.pin_valid;
        self->pin_x = match.pin_x;
        self->pin_z = match.pin_z;
        self->pin_level = match.pin_level;
        squares_area_show(ui, self);
        /* The arrows go on from here. */
        (void)ToriRSChrome_FocusWidget(ui->chrome, self->graph, 0);
        if( match.pin_valid )
            Editor_UiLog(ui, "editor: tile %d,%d (level %d) pinned in m%d_%d", match.pin_x, match.pin_z,
                match.pin_level, match.map_x, match.map_z);
        else if( match.square >= 0 )
            Editor_UiLog(ui, "editor: found m%d_%d", match.map_x, match.map_z);
        else
            Editor_UiLog(ui, "editor: m%d_%d does not exist yet; New creates it", match.map_x, match.map_z);
        break;
    }
    case EDITOR_PICKER_MATCH_AREA:
        squares_leave_area_mode(self);
        Editor_PickerSetTab(picker, match.tab);
        squares_area_show(ui, self);
        Editor_UiLog(ui, "editor: %s", picker->tabs[picker->active_tab].title);
        break;
    case EDITOR_PICKER_MATCH_COLLECTION:
    {
        char name[EDITOR_AREA_NAME_MAX];
        snprintf(name, sizeof(name), "%s", names[match.collection]);
        (void)Editor_SquarePicker_SetCollection(ui, name);
        break;
    }
    case EDITOR_PICKER_MATCH_LANE:
        squares_show_lane(ui, self, match.storage);
        break;
    case EDITOR_PICKER_MATCH_INSTANCE:
        squares_show_instance(ui, self);
        break;
    case EDITOR_PICKER_MATCH_NONE:
        break;
    }
    self->dirty = 1;
    self->pane_hash = 0;
    return 1;
}

static void
squares_clear_find(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    int const had = ToriRSChrome_Text(ui->chrome, self->input_search)[0] != '\0' || self->pin_valid;

    ToriRSChrome_SetText(ui->chrome, self->input_search, "");
    self->pin_valid = 0;
    self->pane_hash = 0;
    self->dirty = 1;
    if( had )
        Editor_UiLog(ui, "editor: Find cleared");
}

/* ---- the open set and its strip -------------------------------------------------- */

/** Text measured as the strip draws it (the row face), in logical px: a
 *  GRAPH's items are laid out in them. */
static int
squares_measure_logical(
    void* user,
    const char* text)
{
    struct EditorUi* ui = user;
    int const q = ui->chrome->scale_q > 0 ? ui->chrome->scale_q : 4;
    int const px = ToriRSChrome_MeasureTextQ(ui->chrome->theme.font_row, q, text);
    return (px * 4 + q - 1) / q;
}

/** The same in device px: the pane's rows are measured against the widget's
 *  own laid-out width. */
static int
squares_measure_device(
    void* user,
    const char* text)
{
    struct EditorUi* ui = user;
    return ToriRSChrome_MeasureTextQ(ui->chrome->theme.font_row, ui->chrome->scale_q, text);
}

static int
squares_more_width(
    void* user,
    int hidden)
{
    char text[16];
    snprintf(text, sizeof(text), "+%d", hidden);
    return squares_measure_logical(user, text) + 2 * SQUARES_CHIP_PAD;
}

/** Chip `i`'s text as drawn (fitted to SQUARES_CHIP_TEXT_W) and its width. */
static int
squares_chip(
    struct EditorUi* ui,
    const struct SquaresPanel* self,
    int i,
    char out[EDITOR_PICKER_NAME_MAX])
{
    char text[EDITOR_PICKER_NAME_MAX];
    const struct Editor_PickerOpenItem* item = &self->open_set.items[i];

    Editor_PickerChipText(item->key, text);
    (void)Editor_PickerFitText(text, SQUARES_CHIP_TEXT_W, squares_measure_logical, ui, out, EDITOR_PICKER_NAME_MAX);
    return 2 * SQUARES_CHIP_PAD + squares_measure_logical(ui, out)
           + (item->dirty ? SQUARES_CHIP_DOT + SQUARES_CHIP_GAP : 0);
}

static int
squares_strip_w(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    int const scale = ToriRSChrome_Scale(ui->chrome);
    int const w = ui->chrome->widgets[self->strip].w / (scale > 0 ? scale : 1);
    return w > 32 ? w : EDP_PICKER_W - 24;
}

/** The strip's tooltip for GRAPH key `key` (0 the bare strip). */
static void
squares_strip_tooltip(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int key)
{
    char tip[TORIRS_CHROME_LABEL_MAX];

    if( key == SQUARES_KEY_MORE )
    {
        /* The chips the strip folded, by name, as many as the tooltip holds. */
        int used = snprintf(tip, sizeof(tip), "also open:");
        for( int i = self->strip_layout.shown; i < self->open_set.count; i++ )
        {
            char text[EDITOR_PICKER_NAME_MAX];
            int const room = (int)sizeof(tip) - used;
            Editor_PickerChipText(self->open_set.items[i].key, text);
            if( (int)strlen(text) + 2 + (i + 1 < self->open_set.count ? 4 : 0) >= room )
            {
                snprintf(tip + used, (size_t)room, " ...");
                break;
            }
            used += snprintf(tip + used, (size_t)room, " %s%s", text, self->open_set.items[i].dirty ? "*" : "");
        }
    }
    else if( key >= 1 && key <= self->open_set.count )
    {
        const struct Editor_PickerOpenItem* item = &self->open_set.items[key - 1];
        char text[EDITOR_PICKER_NAME_MAX];
        Editor_PickerChipText(item->key, text);
        snprintf(tip, sizeof(tip), "%s%s: click go, Shift+click close", text, item->dirty ? " (unsaved)" : "");
    }
    else
        snprintf(tip, sizeof(tip), "open set: click go, Shift+click close, drag reorders");
    ToriRSChrome_SetTooltip(ui->chrome, self->strip, tip);
}

static void
squares_draw_strip(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    int widths[EDITOR_PICKER_OPEN_SET_MAX];
    char texts[EDITOR_PICKER_OPEN_SET_MAX][EDITOR_PICKER_NAME_MAX];
    int const count = self->open_set.count;
    int const track = squares_strip_w(ui, self);
    int const h = SQUARES_STRIP_H;
    int const baseline = h - 5;
    struct Editor_PickerStripLayout* lay = &self->strip_layout;

    for( int i = 0; i < count; i++ )
        widths[i] = squares_chip(ui, self, i, texts[i]);
    Editor_PickerStripLay(widths, count, track, SQUARES_CHIP_GAP, squares_more_width, ui, lay);
    self->strip_track = track;

    ToriRSChrome_GraphBegin(ui->chrome, self->strip);
    ToriRSChrome_GraphRect(ui->chrome, self->strip, 0, 0, track, h, self->colors.background, 1, 0);
    if( count == 0 )
        ToriRSChrome_GraphText(
            ui->chrome, self->strip, SQUARES_CHIP_PAD, baseline, self->colors.empty, "nothing open in this window");
    for( int i = 0; i < lay->shown; i++ )
    {
        const struct Editor_PickerOpenItem* item = &self->open_set.items[i];
        uint32_t const edge = item->dirty ? self->colors.dirty : item->here ? self->colors.open : self->colors.empty;
        int const x = lay->x[i];
        ToriRSChrome_GraphRect(ui->chrome, self->strip, x, 1, lay->w[i], h - 2, self->colors.grid, 1, i + 1);
        ToriRSChrome_GraphRect(ui->chrome, self->strip, x, 1, lay->w[i], h - 2, edge, 0, i + 1);
        ToriRSChrome_GraphText(ui->chrome, self->strip, x + SQUARES_CHIP_PAD, baseline, self->colors.text, texts[i]);
        if( item->dirty )
            ToriRSChrome_GraphRect(ui->chrome, self->strip, x + lay->w[i] - SQUARES_CHIP_PAD - SQUARES_CHIP_DOT,
                (h - SQUARES_CHIP_DOT) / 2, SQUARES_CHIP_DOT, SQUARES_CHIP_DOT, self->colors.dirty, 1, i + 1);
    }
    if( lay->hidden > 0 )
    {
        char more[16];
        snprintf(more, sizeof(more), "+%d", lay->hidden);
        ToriRSChrome_GraphRect(ui->chrome, self->strip, lay->more_x, 1, lay->more_w, h - 2, self->colors.grid, 1,
            SQUARES_KEY_MORE);
        ToriRSChrome_GraphText(
            ui->chrome, self->strip, lay->more_x + SQUARES_CHIP_PAD, baseline, self->colors.text, more);
    }
    ToriRSChrome_GraphEnd(ui->chrome, self->strip);
    squares_strip_tooltip(ui, self, self->strip_hover_key);
    self->strip_dirty = 0;
}

/** The chip at strip x `x` (-1 none, SQUARES_KEY_MORE the "+N"). */
static int
squares_chip_at(
    const struct SquaresPanel* self,
    int x)
{
    const struct Editor_PickerStripLayout* lay = &self->strip_layout;
    for( int i = 0; i < lay->shown; i++ )
        if( x >= lay->x[i] && x < lay->x[i] + lay->w[i] )
            return i;
    if( lay->hidden > 0 && x >= lay->more_x && x < lay->more_x + lay->more_w )
        return SQUARES_KEY_MORE;
    return -1;
}

/** Where a chip dragged to strip x `x` lands. */
static int
squares_chip_slot(
    const struct SquaresPanel* self,
    int x)
{
    const struct Editor_PickerStripLayout* lay = &self->strip_layout;
    for( int i = 0; i < lay->shown; i++ )
        if( x < lay->x[i] + lay->w[i] / 2 )
            return i;
    return lay->shown > 0 ? lay->shown - 1 : 0;
}

/** Go to open square `i`: focus it in the atlas, the camera to its centre
 *  (a square not in this window's world is added to it). */
static void
squares_go(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int i)
{
    struct Editor_Picker* picker = &self->picker;
    const struct Editor_PickerOpenItem* item;
    int const level = ui->tools.edit_level < 0 ? 0 : ui->tools.edit_level;

    assert(i >= 0);
    assert(i < self->open_set.count);
    item = &self->open_set.items[i];
    Editor_PickerFocusSet(picker, item->map_x, item->map_z);
    Editor_PickerKeepInView(picker, item->map_x, item->map_z);
    self->user_view = 1;
    self->dirty = 1;
    if( !item->here )
    {
        if( picker->focus >= 0 )
        {
            Editor_UiStatus(ui, "%s is not in this window's world: adding it", item->key);
            squares_open(ui, self, picker->focus, 1);
        }
        return;
    }
    if( !ui->app_services.camera_to_tile )
    {
        Editor_UiStatus(ui, "Go to %s refused: this window has no world camera", item->key);
        return;
    }
    if( ui->app_services.camera_to_tile(ui->app_services.user_data,
            item->map_x * EDITOR_SQUARE_X + EDITOR_SQUARE_X / 2, item->map_z * EDITOR_SQUARE_Z + EDITOR_SQUARE_Z / 2,
            level) )
        Editor_UiLog(ui, "editor: camera at the centre of %s", item->key);
    else
        Editor_UiStatus(ui, "Go to %s refused: its centre is not in the scene yet", item->key);
}

/** Close open square `i` through the save guard. */
static void
squares_close_chip(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int i)
{
    const struct Editor_PickerOpenItem* item;
    struct EditorUiGuard guard;
    int here = 0;
    int square;

    assert(i >= 0);
    assert(i < self->open_set.count);
    item = &self->open_set.items[i];
    for( int k = 0; k < self->open_set.count; k++ )
        here += self->open_set.items[k].here;
    if( !item->here )
    {
        Editor_UiStatus(ui, "Close refused: %s is not in this window's world (its unsaved edits stay until saved)",
            item->key);
        return;
    }
    if( here <= 1 )
    {
        Editor_UiStatus(ui, "Close refused: %s is the only square this window shows; open another first", item->key);
        return;
    }
    square = Editor_PickerFind(&self->picker, item->map_x, item->map_z, -1);
    memset(&guard, 0, sizeof(guard));
    guard.action = EDITOR_UI_GUARD_CLOSE_SQUARE;
    guard.map_x = item->map_x;
    guard.map_z = item->map_z;
    guard.storage_index = square >= 0 ? self->picker.squares[square].storage : -1;
    if( square >= 0 )
        squares_storage_of(&self->picker, square, guard.storage);
    (void)Editor_UiGuarded(ui, &guard);
}

static void
squares_strip_event(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    const struct ToriRSChromeEvent* event)
{
    switch( event->kind )
    {
    case TORIRS_CHROME_EVENT_GRAPH_PRESS:
        self->strip_pressing = 1;
        self->strip_dragging = 0;
        self->strip_press_x = event->x;
        self->strip_press_chip = squares_chip_at(self, event->x);
        break;
    case TORIRS_CHROME_EVENT_GRAPH_DRAG:
        if( self->strip_pressing && self->strip_press_chip >= 0 && self->strip_press_chip != SQUARES_KEY_MORE
            && abs(event->x - self->strip_press_x) > SQUARES_DRAG_SLOP )
            self->strip_dragging = 1;
        break;
    case TORIRS_CHROME_EVENT_GRAPH_RELEASE:
    {
        int const chip = self->strip_press_chip;
        int const pressed = self->strip_pressing;
        int const dragged = self->strip_dragging;
        self->strip_pressing = 0;
        self->strip_dragging = 0;
        if( !pressed || chip < 0 )
            break;
        if( dragged )
        {
            int const to = squares_chip_slot(self, event->x);
            if( to != chip )
            {
                Editor_PickerOpenSetMove(&self->open_set, chip, to);
                self->strip_dirty = 1;
            }
            break;
        }
        if( chip == SQUARES_KEY_MORE )
        {
            /* The folded chips are named in the tooltip; a click goes to the
             * first of them. */
            if( self->strip_layout.shown < self->open_set.count )
                squares_go(ui, self, self->strip_layout.shown);
            break;
        }
        if( ui->shift_held )
            squares_close_chip(ui, self, chip);
        else
            squares_go(ui, self, chip);
        break;
    }
    case TORIRS_CHROME_EVENT_GRAPH_HOVER:
        if( event->key != self->strip_hover_key )
        {
            self->strip_hover_key = event->key;
            squares_strip_tooltip(ui, self, event->key);
        }
        break;
    default:
        break;
    }
}

/* ---- the detail pane ------------------------------------------------------------- */

static int
squares_in_instance_space(
    int map_x,
    int map_z)
{
    return map_x >= EDITOR_PICKER_INSTANCE_X0 && map_x <= EDITOR_PICKER_INSTANCE_X1
           && map_z >= EDITOR_PICKER_INSTANCE_Z0 && map_z <= EDITOR_PICKER_INSTANCE_Z1;
}

/** "Lumbridge (surface)": the most specific area claiming square `index`,
 *  and whether others do too. */
static void
squares_area_text(
    const struct Editor_Picker* picker,
    int index,
    char* out,
    int out_size)
{
    const struct Editor_PickerSquare* square = &picker->squares[index];
    const struct Editor_PickerArea* area;

    if( square->primary_area < 0 )
    {
        snprintf(out, (size_t)out_size, "unmapped%s",
            squares_in_instance_space(square->map_x, square->map_z) ? " (instance space)" : "");
        return;
    }
    area = &picker->areas[square->primary_area];
    snprintf(out, (size_t)out_size, "%s%s%s", area->display_name[0] ? area->display_name : area->internal_name,
        area->is_main ? " (surface)" : "", square->area_count > 1 ? " and more" : "");
}

/** The collections of the AREAS document holding `key`, comma-separated. */
static void
squares_collections_text(
    struct EditorUi* ui,
    const char* key,
    char* out,
    int out_size)
{
    const struct Editor_AreasFile* file = Editor_Areas_File(ui, NULL);
    int used = 0;

    out[0] = '\0';
    for( int c = 0; file && c < Editor_AreasCollectionCount(file); c++ )
    {
        const struct Editor_Collection* coll = file->lines[Editor_AreasNthCollection(file, c)].collection;
        for( int k = 0; k < coll->square_count; k++ )
        {
            if( strcmp(coll->squares[k], key) != 0 )
                continue;
            if( used < out_size )
                used += snprintf(out + used, (size_t)(out_size - used), "%s%s", used ? ", " : "", coll->name);
            break;
        }
    }
    if( !out[0] )
        snprintf(out, (size_t)out_size, "none");
}

/** What this window's mirror says of square (map_x, map_z) when it holds
 *  it: locs, spawns per kind, the levels used. 0 when it is not loaded. */
static int
squares_contents_text(
    struct EditorUi* ui,
    int map_x,
    int map_z,
    int spawn_count,
    char* out,
    int out_size)
{
    struct Editor_Square* square;
    struct Editor_DocHandle handle;
    char key[EDITOR_DOC_KEY_MAX];
    unsigned levels = 0;
    int npcs = -1;
    int objs = 0;
    int used;

    if( !ui->editor )
        return 0;
    square = Editor_DocFindSquare(&ui->editor->doc, map_x, map_z);
    if( !square || !square->loaded )
        return 0;
    for( int i = 0; i < square->loc_count; i++ )
        if( square->locs[i].level >= 0 && square->locs[i].level < EDITOR_SQUARE_LEVELS )
            levels |= 1u << square->locs[i].level;
    for( int level = 0; level < EDITOR_SQUARE_LEVELS; level++ )
    {
        if( levels & (1u << level) )
            continue;
        for( int t = 0; t < EDITOR_SQUARE_X * EDITOR_SQUARE_Z; t++ )
        {
            const struct Editor_Tile* tile = &square->tiles[level * EDITOR_SQUARE_X * EDITOR_SQUARE_Z + t];
            if( tile->underlay_id || tile->has_overlay )
            {
                levels |= 1u << level;
                break;
            }
        }
    }
    Editor_SquareKey(key, map_x, map_z);
    if( Editor_DocFind(&ui->editor->doc, EDITOR_DOC_SPAWN, key, &handle) && handle.spawn->loaded )
    {
        npcs = 0;
        for( int i = 0; i < handle.spawn->file.line_count; i++ )
        {
            const struct Editor_SpawnLine* line = &handle.spawn->file.lines[i];
            if( line->kind != EDITOR_SPAWN_LINE_ROW || line->row.suppress )
                continue;
            if( line->row.is_obj )
                objs++;
            else
                npcs++;
        }
    }
    used = snprintf(out, (size_t)out_size, "%d locs", square->loc_count);
    if( npcs >= 0 )
        used += snprintf(out + used, (size_t)(out_size - used), ", %d npc, %d obj spawns", npcs, objs);
    else
        used += snprintf(out + used, (size_t)(out_size - used), ", %d spawns", spawn_count);
    for( int level = 0; level < EDITOR_SQUARE_LEVELS && used < out_size; level++ )
        if( levels & (1u << level) )
            used += snprintf(out + used, (size_t)(out_size - used), " L%d", level);
    return 1;
}

/** "open in this window, unsaved", "in another window", "on disk". */
static void
squares_state_text(
    struct EditorUi* ui,
    const struct SquaresPanel* self,
    int index,
    int map_x,
    int map_z,
    char* out,
    int out_size)
{
    uint32_t const flags = index >= 0 ? self->picker.squares[index].flags : 0;
    int other = 0;
    int used = 0;

    for( int f = 0; f < EDITOR_UI_VIEW_FRAMES_MAX; f++ )
    {
        const struct EditorUiViewFrame* frame = &ui->frames[f];
        if( frame->used && frame->slot != ui->view_slot && map_x >= frame->map_x && map_x < frame->map_x + frame->w
            && map_z >= frame->map_z && map_z < frame->map_z + frame->h )
            other = 1;
    }
    out[0] = '\0';
    if( index < 0 )
    {
        snprintf(out, (size_t)out_size, "no square here: New or Instance can make one");
        return;
    }
    if( flags & EDITOR_PICKER_OPEN_HERE )
        used += snprintf(out + used, (size_t)(out_size - used), "open in this window");
    if( flags & (EDITOR_PICKER_DIRTY_HERE | TORIRSMAPED_SQUARE_DIRTY) )
        used += snprintf(out + used, (size_t)(out_size - used), "%sunsaved", used ? ", " : "");
    if( flags & TORIRSMAPED_SQUARE_CREATED )
        used += snprintf(out + used, (size_t)(out_size - used), "%screated, not saved", used ? ", " : "");
    if( other )
        used += snprintf(out + used, (size_t)(out_size - used), "%sin another window", used ? ", " : "");
    if( !used )
        snprintf(out, (size_t)out_size, "on disk, not open");
}

/** Fit pane row `row`'s full text to its track; a cut row's tooltip is the
 *  full text (shortened by hand past the tooltip's limit). */
static void
squares_pane_set(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int row)
{
    struct ToriRSChromeWidget const* w = &ui->chrome->widgets[self->pane_rows[row]];
    int const q = ui->chrome->scale_q;
    int const pad = ToriRSChrome_ScalePx(q, TORIRS_CHROME_M_FIELD_PAD_X);
    int width = w->w > 0 ? w->w : edp_px(ui, EDP_PICKER_W - 24);
    char shown[SQUARES_PANE_TEXT_MAX];
    char tip[TORIRS_CHROME_LABEL_MAX];
    int cut;

    /* A key / value row: the key at the left pad, the value right-aligned
     * before the right pad, a name gap between them (the chrome's KEY_VALUE
     * row); the heading: its text between the two pads. */
    width -= 2 * pad;
    if( row > 0 )
        width -= squares_measure_device(ui, k_pane_keys[row]) + ToriRSChrome_ScalePx(q, TORIRS_CHROME_M_ROW_NAME_GAP);
    self->pane_track[row] = width;
    cut = Editor_PickerFitText(self->pane_full[row], width, squares_measure_device, ui, shown, (int)sizeof(shown));
    ToriRSChrome_SetText(ui->chrome, self->pane_rows[row], shown);
    if( !cut )
        tip[0] = '\0';
    else if( strlen(self->pane_full[row]) < sizeof(tip) )
        snprintf(tip, sizeof(tip), "%s", self->pane_full[row]);
    else
        snprintf(tip, sizeof(tip), "%.*s...", (int)sizeof(tip) - 4, self->pane_full[row]);
    ToriRSChrome_SetTooltip(ui->chrome, self->pane_rows[row], tip);
}

/** The pane answers for the focused cell, else the hovered one. */
static int
squares_pane_cell(
    const struct SquaresPanel* self,
    int* out_index,
    int* out_map_x,
    int* out_map_z)
{
    const struct Editor_Picker* picker = &self->picker;

    if( Editor_PickerFocusCell(picker, out_map_x, out_map_z) )
    {
        *out_index = picker->focus;
        return 1;
    }
    if( picker->hover_valid )
    {
        *out_index = picker->hover;
        *out_map_x = picker->hover_map_x;
        *out_map_z = picker->hover_map_z;
        return 1;
    }
    return 0;
}

/** What the pane was made from: when it moves, the rows are made again. */
static uint32_t
squares_pane_key(
    struct EditorUi* ui,
    const struct SquaresPanel* self)
{
    uint32_t hash = 2166136261u;
    int index = -1;
    int map_x = -1;
    int map_z = -1;
    int const valid = squares_pane_cell(self, &index, &map_x, &map_z);
    int const values[] = {
        valid,
        index,
        map_x,
        map_z,
        index >= 0 ? (int)self->picker.squares[index].flags : 0,
        self->pin_valid,
        self->pin_x,
        self->pin_z,
        ui->chrome->widgets[self->pane_rows[1]].w,
        ui->chrome->widgets[self->pane_rows[0]].w,
        ui->chrome->scale_q,
        (int)self->collection_serial,
        (int)self->picker.atlas_serial,
    };
    for( size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++ )
        hash = (hash ^ (uint32_t)values[i]) * 16777619u;
    return hash | 1u;
}

static void
squares_pane_update(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    const struct Editor_Picker* picker = &self->picker;
    uint32_t const hash = squares_pane_key(ui, self);
    char key[EDITOR_DOC_KEY_MAX];
    char area[EDITOR_PICKER_NAME_MAX * 2];
    char contents[SQUARES_PANE_TEXT_MAX];
    char state[SQUARES_PANE_TEXT_MAX];
    int index = -1;
    int map_x = 0;
    int map_z = 0;

    if( hash == self->pane_hash )
        return;
    self->pane_hash = hash;
    if( !squares_pane_cell(self, &index, &map_x, &map_z) )
    {
        snprintf(self->pane_full[0], SQUARES_PANE_TEXT_MAX, "Click a square or Find one; the arrows move");
        for( int row = 1; row < EDITOR_SQUARES_PANE_ROWS; row++ )
            snprintf(self->pane_full[row], SQUARES_PANE_TEXT_MAX, "-");
    }
    else
    {
        int const x0 = map_x * EDITOR_SQUARE_X;
        int const z0 = map_z * EDITOR_SQUARE_Z;
        int const pinned = self->pin_valid && self->pin_x / EDITOR_SQUARE_X == map_x
                           && self->pin_z / EDITOR_SQUARE_Z == map_z;
        if( index >= 0 )
        {
            Editor_PickerSquareKey(picker, index, key);
            squares_area_text(picker, index, area, (int)sizeof(area));
        }
        else
        {
            snprintf(key, sizeof(key), "m%d_%d", map_x, map_z);
            snprintf(area, sizeof(area), "%s",
                squares_in_instance_space(map_x, map_z) ? "instance space, free" : "no square at these coordinates");
        }
        squares_state_text(ui, self, index, map_x, map_z, state, (int)sizeof(state));
        if( index < 0 )
            snprintf(contents, sizeof(contents), "-");
        else if( !squares_contents_text(
                     ui, map_x, map_z, picker->squares[index].spawn_count, contents, (int)sizeof(contents)) )
            snprintf(contents, sizeof(contents), "%d spawns (open it for its locs and levels)",
                picker->squares[index].spawn_count);
        snprintf(self->pane_full[0], SQUARES_PANE_TEXT_MAX, "%s - %s - %s", area, key, state);
        if( index >= 0 )
            snprintf(self->pane_full[1], SQUARES_PANE_TEXT_MAX, "%s  %s/", key,
                picker->storages[picker->squares[index].storage]);
        else
            snprintf(self->pane_full[1], SQUARES_PANE_TEXT_MAX, "%s", key);
        snprintf(self->pane_full[2], SQUARES_PANE_TEXT_MAX, "%s", area);
        if( pinned )
            snprintf(self->pane_full[3], SQUARES_PANE_TEXT_MAX, "%d..%d, %d..%d, pin %d,%d L%d", x0,
                x0 + EDITOR_SQUARE_X - 1, z0, z0 + EDITOR_SQUARE_Z - 1, self->pin_x, self->pin_z, self->pin_level);
        else
            snprintf(self->pane_full[3], SQUARES_PANE_TEXT_MAX, "%d..%d, %d..%d", x0, x0 + EDITOR_SQUARE_X - 1, z0,
                z0 + EDITOR_SQUARE_Z - 1);
        snprintf(self->pane_full[4], SQUARES_PANE_TEXT_MAX, "%s", contents);
        snprintf(self->pane_full[5], SQUARES_PANE_TEXT_MAX, "%s", state);
        if( index >= 0 )
            squares_collections_text(ui, key, self->pane_full[6], SQUARES_PANE_TEXT_MAX);
        else
            snprintf(self->pane_full[6], SQUARES_PANE_TEXT_MAX, "-");
    }
    for( int row = 0; row < EDITOR_SQUARES_PANE_ROWS; row++ )
        squares_pane_set(ui, self, row);
}

/** The Recent dropdown's rows from the list (when it moved). */
static void
squares_recent_rows(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    int n = 0;

    if( !self->recent_dirty )
        return;
    self->recent_dirty = 0;
    snprintf(self->recent_labels[n], TORIRS_CHROME_LABEL_MAX, "%s", self->recent.count ? "go to..." : "none yet");
    self->recent_rows[n] = self->recent_labels[n];
    n++;
    for( int i = 0; i < self->recent.count; i++ )
    {
        char text[EDITOR_PICKER_NAME_MAX];
        Editor_PickerChipText(self->recent.keys[i], text);
        snprintf(self->recent_labels[n], TORIRS_CHROME_LABEL_MAX, "%s", text);
        self->recent_rows[n] = self->recent_labels[n];
        n++;
    }
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_recent, self->recent_rows, n, 0);
}

/* ---- keys ------------------------------------------------------------------------ */

/** Backspace: the open square before the focused one (the last when the
 *  focus is none of them). */
static void
squares_back(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    char key[EDITOR_DOC_KEY_MAX];
    int at = -1;

    if( self->open_set.count == 0 )
    {
        Editor_UiStatus(ui, "Back refused: this window holds no square yet");
        return;
    }
    if( self->picker.focus >= 0 )
    {
        Editor_PickerSquareKey(&self->picker, self->picker.focus, key);
        at = Editor_PickerOpenSetFind(&self->open_set, key);
    }
    at = at <= 0 ? self->open_set.count - 1 : at - 1;
    squares_go(ui, self, at);
}

static void
squares_open_focus(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int add)
{
    struct Editor_Picker* picker = &self->picker;
    int map_x;
    int map_z;

    if( picker->focus >= 0 )
    {
        squares_open(ui, self, picker->focus, add);
        return;
    }
    if( !Editor_PickerFocusCell(picker, &map_x, &map_z) )
    {
        Editor_UiStatus(ui, "%s refused: no square is focused; click one or use the arrows", add ? "Add" : "Open");
        return;
    }
    if( add )
    {
        Editor_UiStatus(ui, "Add refused: m%d_%d is empty; Enter makes it with New square", map_x, map_z);
        return;
    }
    Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_NEW, NULL, map_x, map_z);
}

static void
squares_key(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int action)
{
    static const int k_step[4][2] = { { 0, 1 }, { 0, -1 }, { -1, 0 }, { 1, 0 } };

    if( action >= EDITOR_HK_PICKER_NORTH && action <= EDITOR_HK_PICKER_EAST )
    {
        int const at = action - EDITOR_HK_PICKER_NORTH;
        if( Editor_PickerFocusStep(&self->picker, k_step[at][0], k_step[at][1]) )
            self->user_view = 1;
    }
    else if( action == EDITOR_HK_PICKER_OPEN || action == EDITOR_HK_PICKER_ADD )
        squares_open_focus(ui, self, action == EDITOR_HK_PICKER_ADD);
    else if( action == EDITOR_HK_PICKER_BACK )
        squares_back(ui, self);
    else if( action == EDITOR_HK_PICKER_CLEAR_FIND )
        squares_clear_find(ui, self);
    self->dirty = 1;
}

/** A GRAPH_KEY of the well as a chord: the chrome's key (negated), with the
 *  modifiers the presentation knew or the host's shift. TORIRSK_UNKNOWN for
 *  a key the table cannot name. */
static struct Editor_Chord
squares_key_chord(
    const struct EditorUi* ui,
    const struct ToriRSChromeEvent* event)
{
    struct Editor_Chord chord;
    int const value = event->value;

    memset(&chord, 0, sizeof(chord));
    if( value == -TORIRS_CHROME_KEY_UP )
        chord.key = TORIRSK_UP;
    else if( value == -TORIRS_CHROME_KEY_DOWN )
        chord.key = TORIRSK_DOWN;
    else if( value == -TORIRS_CHROME_KEY_LEFT )
        chord.key = TORIRSK_LEFT;
    else if( value == -TORIRS_CHROME_KEY_RIGHT )
        chord.key = TORIRSK_RIGHT;
    else if( value == -TORIRS_CHROME_KEY_ENTER )
        chord.key = TORIRSK_RETURN;
    else if( value == -TORIRS_CHROME_KEY_BACKSPACE )
        chord.key = TORIRSK_BACKSPACE;
    else if( value == -TORIRS_CHROME_KEY_ESCAPE )
        chord.key = TORIRSK_ESCAPE;
    chord.mods = (uint8_t)((event->col & (int)(EDITOR_HK_MOD_CTRL | EDITOR_HK_MOD_SHIFT | EDITOR_HK_MOD_ALT))
                           | (ui->shift_held ? (int)EDITOR_HK_MOD_SHIFT : 0));
    return chord;
}

static int
squares_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct SquaresPanel* self = opaque;
    struct Editor_Picker* picker = &self->picker;

    if( event->widget == self->strip )
    {
        if( self->fetched )
            squares_strip_event(ui, self, event);
        return 1;
    }
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
        /* A mouse wheel: x1.25 a notch about the cursor. */
        Editor_PickerZoom(picker, event->value, event->x, event->y);
        self->user_view = 1;
        self->dirty = 1;
        break;
    case TORIRS_CHROME_EVENT_GRAPH_PINCH:
        /* A pinch (or ctrl+wheel): continuous, by exactly its ratio, about
         * its centre -- a 2x spread is a 2x zoom. */
        if( event->value > 0 )
        {
            Editor_PickerZoomTo(
                picker, picker->zoom * ((double)event->value / 65536.0), event->x, event->y);
            self->user_view = 1;
            self->dirty = 1;
        }
        break;
    case TORIRS_CHROME_EVENT_GRAPH_SCROLL:
        /* A two-finger scroll pans, never zooms: the map moves the way the
         * platform scrolls a document (its natural-scrolling setting is in
         * the deltas already), so scroll up moves the map down. */
        if( event->dx != 0 || event->dy != 0 )
        {
            Editor_PickerPanBy(picker, -(double)event->dx / 256.0, (double)event->dy / 256.0);
            self->user_view = 1;
            self->dirty = 1;
        }
        break;
    case TORIRS_CHROME_EVENT_GRAPH_KEY:
    {
        /* The well has focus (context K): + / - zoom about its middle, the
         * rest are the table's Picker rows -- the arrows move the focused
         * cell, Enter opens, Shift+Enter adds, Backspace goes back, Escape
         * clears Find -- so a rebind re-routes them. */
        int const mid_x = (EDITOR_PICKER_LEFT + picker->well_w) / 2;
        int const mid_y = (EDITOR_PICKER_TOP + picker->well_h) / 2;
        if( event->value == '+' || event->value == '=' )
            Editor_PickerZoom(picker, -1, mid_x, mid_y);
        else if( event->value == '-' || event->value == '_' )
            Editor_PickerZoom(picker, 1, mid_x, mid_y);
        else
        {
            int const action = Editor_HotkeysMatch(&ui->hotkeys, squares_key_chord(ui, event), EDITOR_HK_CTX_K);
            if( action >= 0 )
                squares_key(ui, self, action);
            break;
        }
        self->user_view = 1;
        self->dirty = 1;
        break;
    }
    default:
        break;
    }
    return 1;
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

static void
squares_new_view(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int square)
{
    if( ui->hooks.open_in_new_view )
        ui->hooks.open_in_new_view(
            ui->hooks_user, self->picker.squares[square].map_x, self->picker.squares[square].map_z);
    else
        Editor_UiStatus(ui, "New view refused: this window cannot open views");
}

/** A pane action on the focused square: the square, or -1 with the refusal
 *  said. */
static int
squares_focus_square(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    const char* action)
{
    int map_x;
    int map_z;

    if( self->picker.focus >= 0 )
        return self->picker.focus;
    if( Editor_PickerFocusCell(&self->picker, &map_x, &map_z) )
        Editor_UiStatus(ui, "%s refused: m%d_%d is empty (New square makes it)", action, map_x, map_z);
    else
        Editor_UiStatus(ui, "%s refused: no square is focused; click one or use the arrows", action);
    return -1;
}

/** The detail pane's actions and the Recent row. Returns 1 when `widget`
 *  was one. */
static int
squares_pane_activate(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int widget)
{
    struct Editor_Picker* picker = &self->picker;
    char key[EDITOR_DOC_KEY_MAX];
    int square;

    if( widget == self->button_open || widget == self->button_add )
    {
        squares_open_focus(ui, self, widget == self->button_add);
        return 1;
    }
    if( widget == self->button_new_view )
    {
        if( (square = squares_focus_square(ui, self, "New view")) >= 0 )
            squares_new_view(ui, self, square);
        return 1;
    }
    if( widget == self->button_copy )
    {
        if( (square = squares_focus_square(ui, self, "Copy to")) >= 0 )
        {
            Editor_PickerSquareKey(picker, square, key);
            Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_COPY_TO, key, -1, -1);
        }
        return 1;
    }
    if( widget == self->button_instance )
    {
        /* The instance helper (design section 13, edpanel_instance.c), its
         * source the selection (the shift-clicked squares, else the focus). */
        (void)picker;
        Editor_Instance_Open(ui);
        return 1;
    }
    if( widget == self->button_clear )
    {
        if( (square = squares_focus_square(ui, self, "Clear")) >= 0 )
            squares_confirm_delete(ui, self, square);
        return 1;
    }
    if( widget == self->button_delete )
    {
        if( (square = squares_focus_square(ui, self, "Delete")) >= 0 )
        {
            Editor_PickerSquareKey(picker, square, key);
            Editor_UiStatus(ui, "Delete refused: the server cannot remove %s's files yet; Clear empties it", key);
        }
        return 1;
    }
    if( widget == self->dropdown_recent )
    {
        int const row = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        ToriRSChrome_DropdownSetSelected(ui->chrome, widget, 0);
        if( row < 1 || row > self->recent.count )
            return 1;
        for( int i = 0; i < picker->square_count; i++ )
        {
            Editor_PickerSquareKey(picker, i, key);
            if( strcmp(key, self->recent.keys[row - 1]) == 0 )
            {
                struct Editor_PickerSearch result;
                memset(&result, 0, sizeof(result));
                result.kind = EDITOR_PICKER_SEARCH_SQUARE;
                result.map_x = picker->squares[i].map_x;
                result.map_z = picker->squares[i].map_z;
                result.square = i;
                result.tab = Editor_PickerSquareOnTab(picker, i, picker->active_tab) ? picker->active_tab
                                                                                    : Editor_PickerTabOfSquare(picker, i);
                Editor_PickerShow(picker, &result);
                squares_area_show(ui, self);
                self->user_view = 1;
                self->dirty = 1;
                return 1;
            }
        }
        Editor_UiStatus(ui, "Recent: %s is no longer in the atlas", self->recent.keys[row - 1]);
        return 1;
    }
    return 0;
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
            squares_new_view(ui, self, square);
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
        int const row = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        int const lane = squares_area_row_lane(self, row);
        if( row >= 0 && row < picker->tab_count )
        {
            int const was_lane = self->area_mode == SQUARES_AREA_LANE;
            squares_leave_area_mode(self);
            if( was_lane )
                ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_storage, picker->storage_filter + 1);
            Editor_PickerSetTab(picker, row);
            self->user_view = 0;
            self->dirty = 1;
        }
        else if( lane >= 0 )
            squares_show_lane(ui, self, lane);
        else if( row == self->area_row_count - 1 && Editor_PickerInstanceTab(picker) < 0 )
            squares_show_instance(ui, self);
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
        /* Another storage is no longer the lane's page. */
        if( self->area_mode == SQUARES_AREA_LANE && picker->storage_filter != self->area_lane )
        {
            self->area_mode = SQUARES_AREA_PAGE;
            squares_area_show(ui, self);
        }
        self->dirty = 1;
        return 1;
    }
    if( widget == self->input_search )
    {
        (void)squares_find(ui, self, ToriRSChrome_Text(ui->chrome, self->input_search));
        return 1;
    }
    if( squares_pane_activate(ui, self, widget) )
        return 1;
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
    Editor_WorldMapFree(&self->map);
    Editor_PickerFree(&self->picker);
    ToriMem_Free(ToriMem_Editor(), self->area_labels);
    ToriMem_Free(ToriMem_Editor(), (void*)self->area_rows);
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

int
Editor_SquarePicker_Key(
    struct EditorUi* ui,
    int action)
{
    struct SquaresPanel* self;

    assert(ui);
    assert(action >= EDITOR_HK_PICKER_NORTH);
    assert(action <= EDITOR_HK_PICKER_CLEAR_FIND);
    self = squares_self(ui);
    if( !self )
        return 0;
    if( !self->fetched )
    {
        Editor_UiStatus(ui, "%s refused: the picker has no atlas yet", Editor_HotkeyInfo(action)->name);
        return 1;
    }
    squares_key(ui, self, action);
    return 1;
}

int
Editor_SquarePicker_Sources(
    struct EditorUi* ui,
    enum Editor_SquarePickerSources which,
    char (*out)[EDITOR_DOC_KEY_MAX],
    int max)
{
    struct SquaresPanel* self;
    const struct Editor_Picker* picker;
    int n = 0;

    assert(ui);
    assert(out);
    assert(max > 0);
    self = squares_self(ui);
    if( !self || !self->fetched )
        return 0;
    picker = &self->picker;
    if( which == EDITOR_SQUARE_PICKER_OPEN_SET )
    {
        for( int i = 0; i < self->open_set.count && n < max; i++ )
            snprintf(out[n++], EDITOR_DOC_KEY_MAX, "%s", self->open_set.items[i].key);
        return n;
    }
    for( int i = 0; i < picker->square_count && n < max; i++ )
        if( picker->selected[i] )
            Editor_PickerSquareKey(picker, i, out[n++]);
    if( n == 0 && picker->focus >= 0 && picker->focus < picker->square_count )
        Editor_PickerSquareKey(picker, picker->focus, out[n++]);
    return n;
}

void
Editor_SquarePicker_Mark(
    struct EditorUi* ui,
    int map_x,
    int map_z,
    int w,
    int h)
{
    struct SquaresPanel* self;

    assert(ui);
    assert(w >= 0);
    assert(h >= 0);
    self = squares_self(ui);
    if( !self )
        return;
    self->picker.mark_x = map_x;
    self->picker.mark_z = map_z;
    self->picker.mark_w = w;
    self->picker.mark_h = h;
    if( w > 0 && h > 0 && self->fetched )
    {
        Editor_PickerCentre(&self->picker, map_x + w / 2, map_z + h / 2);
        self->user_view = 1;
    }
    self->dirty = 1;
}

void
Editor_SquarePicker_TakeAtlas(
    struct EditorUi* ui,
    const struct EditorHost_CatalogSquares* atlas)
{
    struct SquaresPanel* self;

    assert(ui);
    assert(atlas);
    self = squares_self(ui);
    assert(self);
    squares_take_atlas(ui, self, atlas);
}

int
Editor_SquarePicker_Find(
    struct EditorUi* ui,
    const char* text)
{
    struct SquaresPanel* self;

    assert(ui);
    assert(text);
    self = squares_self(ui);
    assert(self);
    ToriRSChrome_SetText(ui->chrome, self->input_search, text);
    return squares_find(ui, self, text);
}

int
Editor_SquarePicker_Area(
    struct EditorUi* ui,
    int index,
    const char** out_name,
    const char** out_display,
    int* out_squares)
{
    struct SquaresPanel* self;
    const struct Editor_Picker* picker;
    int seen = 0;

    assert(ui);
    assert(out_name);
    assert(out_display);
    assert(out_squares);
    self = squares_self(ui);
    assert(self);
    if( !self->fetched && index == 0 )
        squares_fetch(ui, self);
    picker = &self->picker;
    for( int t = 0; t < picker->tab_count; t++ )
    {
        int const a = picker->tabs[t].area;
        if( a < 0 || a >= picker->area_count )
            continue;
        if( seen++ != index )
            continue;
        *out_name = picker->areas[a].internal_name;
        *out_display = picker->areas[a].display_name;
        *out_squares = picker->tabs[t].count;
        return 1;
    }
    return 0;
}

int
Editor_SquarePicker_PaneRow(
    struct EditorUi* ui,
    int row,
    int* out_widget,
    const char** out_full,
    int* out_track)
{
    struct SquaresPanel* self;

    assert(ui);
    assert(out_widget);
    assert(out_full);
    assert(out_track);
    self = squares_self(ui);
    if( !self || row < 0 || row >= EDITOR_SQUARES_PANE_ROWS )
        return 0;
    *out_widget = self->pane_rows[row];
    *out_full = self->pane_full[row];
    *out_track = self->pane_track[row];
    return 1;
}

int
Editor_SquarePicker_Strip(
    struct EditorUi* ui,
    int* out_graph,
    int* out_count,
    int* out_shown)
{
    struct SquaresPanel* self;

    assert(ui);
    assert(out_graph);
    assert(out_count);
    assert(out_shown);
    self = squares_self(ui);
    if( !self )
        return 0;
    *out_graph = self->strip;
    *out_count = self->open_set.count;
    *out_shown = self->strip_layout.shown;
    return 1;
}

int
Editor_SquarePicker_Recent(
    struct EditorUi* ui,
    const char** out_keys,
    int max)
{
    struct SquaresPanel* self;
    int n = 0;

    assert(ui);
    assert(out_keys);
    self = squares_self(ui);
    if( !self )
        return 0;
    for( ; n < self->recent.count && n < max; n++ )
        out_keys[n] = self->recent.keys[n];
    return n;
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
    Editor_WorldMapInit(&self->map);
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
    ToriRSChrome_SetTooltip(chrome, self->input_search, "m50_50, 50 50, 3222 3218, a name, lane:x, instance:");
    self->dropdown_area = ToriRSChrome_Dropdown(chrome, self->panel, "Area", k_loading_titles, 1, 0);
    self->dropdown_storage = ToriRSChrome_Dropdown(chrome, self->panel, "Storage", NULL, 0, -1);
    self->dropdown_collection = ToriRSChrome_Dropdown(chrome, self->panel, "Collection", NULL, 0, -1);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_collection, "Only the squares of one named collection (Areas)");
    self->graph = ToriRSChrome_Graph(chrome, self->panel, "", edp_px(ui, SQUARES_WELL_H));
    ToriRSChrome_SetTooltip(chrome, self->graph, "click: open, wheel: zoom, drag: pan, arrows: focus, Enter: open");
    /* The open set: one chip per square this window holds (under the well,
     * so the well stays the panel's first GRAPH). */
    self->strip = ToriRSChrome_Graph(chrome, self->panel, "", edp_px(ui, SQUARES_STRIP_H));
    self->row_hover = ToriRSChrome_Label(chrome, self->panel, " ");
    self->row_status = ToriRSChrome_Label(chrome, self->panel, "");
    /* The detail pane: the focused (else hovered) square. */
    self->pane_rows[0] = ToriRSChrome_Heading(chrome, self->panel, "");
    for( int row = 1; row < EDITOR_SQUARES_PANE_ROWS; row++ )
        self->pane_rows[row] = ToriRSChrome_KeyValue(chrome, self->panel, k_pane_keys[row], "-");
    self->button_open = ToriRSChrome_Button(chrome, self->panel, "Open");
    ToriRSChrome_SetTooltip(chrome, self->button_open, "Open the focused square here (asks to save first)");
    self->button_add = ToriRSChrome_Button(chrome, self->panel, "Add to open set");
    ToriRSChrome_SetTooltip(chrome, self->button_add, "Add the focused square to what this window shows");
    self->button_new_view = ToriRSChrome_Button(chrome, self->panel, "Open in new view");
    ToriRSChrome_SetTooltip(chrome, self->button_new_view, "Open the focused square in a new view window");
    self->button_copy = ToriRSChrome_Button(chrome, self->panel, "Copy to...");
    ToriRSChrome_SetTooltip(chrome, self->button_copy, "Copy the focused square: coordinates and storage");
    self->button_instance = ToriRSChrome_Button(chrome, self->panel, "Instance...");
    ToriRSChrome_SetTooltip(chrome, self->button_instance, "Instance wizard: step 6.5; the New dialog for now");
    self->button_clear = ToriRSChrome_Button(chrome, self->panel, "Clear...");
    ToriRSChrome_SetTooltip(chrome, self->button_clear, "Clear every tile and loc of the focused square");
    self->button_delete = ToriRSChrome_Button(chrome, self->panel, "Delete...");
    ToriRSChrome_SetTooltip(chrome, self->button_delete, "Delete the focused square's files (not yet)");
    self->dropdown_recent = ToriRSChrome_Dropdown(chrome, self->panel, "Recent", NULL, 0, -1);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_recent, "The last squares opened: choose one to go to it");
    self->recent_dirty = 1;
    /* Folded until the panel's size is known: the default layout docks the
     * panel by its height with the pane folded. */
    squares_pane_fold(ui, self, 0);
    self->button_new = ToriRSChrome_Button(chrome, self->panel, "New square...");
    ToriRSChrome_SetTooltip(chrome, self->button_new,
        "Create a square: its storage (maps/ or a lane) and its template");
    self->button_fit = ToriRSChrome_Button(chrome, self->panel, "Fit");
    self->button_refresh = ToriRSChrome_Button(chrome, self->panel, "Refresh");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);
    self->strip_dirty = 1;
    self->hotkeys_serial = ui->hotkeys.serial;
    squares_chords(ui, self);
    /* The focused square's outline (design section 5: white). */
    self->colors.selected = EDITOR_SQUARES_FOCUS_RGB;

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
