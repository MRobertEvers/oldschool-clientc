/*
 * The square picker (docs/MAPED_DESIGN.html #squares): a map of every square
 * the content tree holds, one page per world-map area plus Unmapped, chosen
 * with the Area dropdown (a tab per area made a strip of ~50 overlapping
 * labels nobody could read).
 *
 * The map is a window of its own, "Square map" (module "squaremap"): docked
 * in the column or tabbed into a strip it was a few grid rows high. It opens
 * at most of the world's box beside the column (squares_size_map) from the
 * Squares panel's Map, a Find, the tool rail's Squares or View > Square map;
 * the Squares panel keeps Find, the dropdowns, the open-set strip and the
 * detail pane, and one module drives both windows.
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
 *   empty cell     a click focuses it (the pane names it empty); New square
 *                  makes a scratch square, placed by Save (below)
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
 *                  closes it through the save guard (as the pane's Close
 *                  square, the right-click row and File > Close square do),
 *                  a drag reorders; chips past the width fold into "+N" (the
 *                  rest in its tooltip). Open REPLACES what the window
 *                  shows, Add to open set adds; a square that leaves with
 *                  nothing unsaved leaves the mirror too, and a scene is at
 *                  most EDITOR_DOC_WORLD_SIDE_MAX squares a side
 *                  (app_map_editor_world_begin)
 *                  A scratch square is a chip too ("scratch 1"): it has no
 *                  cell on the map, and the strip used to read "nothing
 *                  open" over the square being edited
 *   detail pane    under the atlas: the focused (else hovered) square's
 *                  heading, its actions, then its key, area, tiles,
 *                  contents, state, collections, and the last
 *                  EDITOR_PICKER_RECENT_MAX squares opened. No row is ever
 *                  hidden: a short panel scrolls;
 *                  with no cell focused or hovered, the scratch square this
 *                  window shows ("unsaved, Save places it")
 *   outlines       open squares white, unsaved ones in the accent, the
 *                  selection yellow, created-not-saved green, and every
 *                  view's window framed (STATE_VIEW_SQUARE)
 *   collection     the Collection dropdown shows only one named collection's
 *                  squares (the AREAS document, edpanel_areas.c), whatever
 *                  their area page; "all" is every square again
 *
 * The map also picks (docs/MAPED_STATUS.md "Scratch squares"): the line over
 * the well is the instruction and the verdict (while browsing, a hint; it is
 * always there, so the well never moves), and a Stored in choice, the action
 * and Cancel come up under the well. A pick is made on the Full map page
 * (every square where it is), and the cell under the pointer is asked of the
 * chrome every tick (squares_pointer_follow):
 *
 *   Open squares   File > Open squares (Ctrl+O): a drag sweeps up to 3x3
 *                  cells (Editor_PickerSweepRect) and Open opens the squares
 *                  among them together as the working set, through the save
 *                  guard; empty cells are skipped. Scroll pans, wheel zooms.
 *   Place          Save with a scratch square holding work, Save as new on
 *                  one, or Copy to on any square: the hovered cell is checked
 *                  as a destination (Editor_NewSquareValidate) and outlined
 *                  green or in the accent with the reason in the bar; a click
 *                  copies the source there (DOC_CREATE) -- with its spawns
 *                  (edp_square_create), in the same undo step: always for a
 *                  scratch square, whose npcs would otherwise stay at its
 *                  parking coordinate, and by the bar's "with its spawns"
 *                  box (on) for Copy to. A scratch square's Place then
 *                  saves, clears the scratch slot and opens the placed
 *                  square as the working set, the camera on the same local
 *                  tile, at the same yaw, pitch and distance, it had over
 *                  the scratch square; Copy to adds it.
 *
 * The atlas is the catalog's (CATALOG_SQUARES), fetched the first time the
 * panel is shown and replaced whenever the server broadcasts a change (a
 * DOC_CREATE, a save that wrote squares). A server with no catalog binding
 * still gets a picker: the square list, every square unmapped.
 */

#include "edpanel_common.h"
#include "edpanel_newform.h"
#include "edpanel_picker.h"
#include "edpanel_picker_find.h"
#include "edpanel_picker_nav.h"
#include "edpanels.h"
#include "editor/editor_areas.h"
#include "editor/editor_view.h"
#include "editor/editor_worldmap.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "ui/torirs_chrome_metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The well's height when the Square map window is content-sized, and the
 * least it shrinks to when the window is resized short (1x pixels). 88 is
 * eleven grid rows. */
#define SQUARES_WELL_H 300
#define SQUARES_WELL_MIN_H 88
/* The Square map window opens at this fraction of the world's box each way
 * (squares_size_map): the map has a window of its own because a column slot
 * or a strip left it a few rows high. */
#define SQUARES_MAP_NUM 3
#define SQUARES_MAP_DEN 4
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

enum
{
    MENU_OPEN = 0,
    MENU_ADD,
    MENU_CLOSE,
    MENU_NEW_VIEW,
    MENU_COPY_TO,
    MENU_DELETE,
    MENU_COUNT
};
static const char* const k_menu_rows[MENU_COUNT] = {
    "Open", "Add to open set", "Close square", "Open in new view", "Copy to...", "Delete (clear)...",
};
/* The pick bar's line while nothing is being picked. It is always there:
 * a line that came and went moved the well, and every cell with it, under
 * the pointer (F8, 2026-11-11). */
#define SQUARES_BROWSE_HINT "click: open   shift+click: add   right-click: more"

static const char* const k_loading_titles[1] = { "Squares" };

/* What a press on the Square map does: open (browsing), sweep cells for
 * Open squares, or place the pick's source. */
enum
{
    SQUARES_PICK_BROWSE = 0,
    SQUARES_PICK_SWEEP,
    SQUARES_PICK_PLACE,
};
/* Ticks a placed scratch square's camera waits for its world before it is
 * given up (the load landed without the square: the console says which). */
#define SQUARES_VIEW_TICKS 1200
/* An Open squares sweep: at most this many squares a side. */
#define SQUARES_SWEEP_SIDE 3
_Static_assert(SQUARES_SWEEP_SIDE * SQUARES_SWEEP_SIDE <= EDITOR_UI_OPEN_SET_MAX, "a sweep fits a working set");

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
    /** The Square map window: the well and the hover readout under it. */
    int map_panel;
    /** The box squares_size_map last gave it. */
    int map_set_w;
    int map_set_h;
    int input_search;
    /** The Find text squares_find last acted on: Open and Add act on what
     *  the box says, so a text typed without Enter is found first. */
    char find_applied[TORIRS_CHROME_LABEL_MAX];
    int dropdown_area;
    int dropdown_storage;
    int graph;
    int row_hover;
    int row_status;
    int button_open;
    int button_add;
    /* The Square map was opened by a Find, not asked for: it is put away
     * again when the found square is opened or added from the pane, so it
     * does not stay over the World (every World tool then read its tiles as
     * covered, 2026-11-11). */
    int map_from_find;
    int button_new;
    int button_fit;
    int button_refresh;
    int button_map;

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
    /** Open (1) or Add (2) pressed before the square list arrived: done when
     *  it does (squares_take_atlas). 0 none. */
    int open_pending;
    /** A Find made before the square list arrived: run again when it does. */
    int find_pending;
    int want_refresh;
    /** The server answered BUSY (its index still building) and the log said
     *  so once; the next tick asks again. */
    int busy_logged;
    int dirty;
    /** The user zoomed, panned or searched: the view is theirs, and a well
     *  that changes size keeps it instead of fitting the page again. */
    int user_view;
    /** A pick's view (squares_pick_view): the cell to centre once the well
     *  has its size -- on the frame the map window first opens it has none,
     *  and a centre worked out then put the page off the well. */
    int centre_pending;
    int centre_x;
    int centre_z;
    /** The well size and the view the centre was last applied at: a view
     *  that is not that one any more was moved by someone else. */
    int centre_w;
    int centre_h;
    double centre_origin_x;
    double centre_origin_z;
    double centre_zoom;
    /** The well size the map was last drawn at (logical px). */
    int drawn_w;
    int drawn_h;

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
    int button_close;
    int button_new_view;
    int button_copy;
    int button_instance;
    int button_clear;
    int button_delete;
    struct Editor_PickerRecent recent;
    int dropdown_recent;
    char recent_labels[EDITOR_PICKER_RECENT_MAX + 1][TORIRS_CHROME_LABEL_MAX];
    const char* recent_rows[EDITOR_PICKER_RECENT_MAX + 1];
    int recent_dirty;
    /** The table the Open / Add keycaps were read from. */
    uint32_t hotkeys_serial;

    /* The pick (Open squares, Place): the bar over the well, the mode, the
     * sweep's first cell, and Place's source. */
    int pick_mode;
    int row_pick;
    int dropdown_place;
    int input_place_lane;
    /** Copy to's "with its spawns" (a scratch square's Place always brings
     *  them: it is a move). */
    int check_place_spawns;
    int button_pick_go;
    int button_pick_cancel;
    int sweeping;
    int sweep_anchor_x;
    int sweep_anchor_z;
    char place_source[EDITOR_DOC_KEY_MAX];
    /** Place's source is a scratch square (Save's Place), not Copy to's. */
    int place_scratch;
    /** A Place asked for while the atlas was still coming (the catalog index
     *  building): it starts on the tick the atlas lands. */
    int place_waiting;
    /** What the pointer follow last told Place's bar (squares_pointer_follow):
     *  the cell, or none. */
    int place_said_valid;
    int place_said_x;
    int place_said_z;
    /** The Stored in rows: label, and the picker storage index (-1 = a new
     *  lane named in input_place_lane). */
    char place_labels[SQUARES_STORAGE_ROWS + 1][TORIRS_CHROME_LABEL_MAX];
    const char* place_rows[SQUARES_STORAGE_ROWS + 1];
    int place_index[SQUARES_STORAGE_ROWS + 1];
    int place_count;

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
    /** What the count line last said with the map window shut (0: the map's
     *  own draw says it). */
    uint32_t status_said;

    /* The scratch square in this window's world (the pane names it when no
     * cell is focused or hovered), and what the strip's scratch chips were
     * made from. */
    int scratch_here;
    int scratch_x;
    int scratch_z;
    int scratch_dirty;
    uint32_t scratch_hash;

    /* A placed scratch square's camera: where it looked in the scratch
     * square, moved to the placed one, put back once its world has landed. */
    int view_pending;
    int view_ticks;
    struct EditorViewCamera view_pose;
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
static void
squares_pick_leave(
    struct EditorUi* ui,
    struct SquaresPanel* self);
static void
squares_pick_layout(
    struct EditorUi* ui,
    struct SquaresPanel* self);
static void
squares_pick_page(
    struct EditorUi* ui,
    struct SquaresPanel* self);
static void
squares_pick_view(struct SquaresPanel* self);
static int
squares_world_tab(const struct Editor_Picker* picker);

static void
squares_view_step(
    struct EditorUi* ui,
    struct SquaresPanel* self);
static int
squares_in_instance_space(
    int map_x,
    int map_z);

static void
squares_scratch_sync(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    const struct EditorUiViewFrame* own);
static void
squares_pointer_follow(
    struct EditorUi* ui,
    struct SquaresPanel* self);
static void
squares_place_hover(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int x,
    int y);

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
squares_open_focus(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int add);

static int
squares_find(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    const char* text);

/** What was asked of the panel before the square list arrived (a Find, an
 *  Open, an Add), done now that it is here. */
static void
squares_run_pending(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    if( self->open_pending )
    {
        int const add = self->open_pending == 2;
        self->open_pending = 0;
        self->find_pending = 0;
        squares_open_focus(ui, self, add);
    }
    else if( self->find_pending )
    {
        char text[TORIRS_CHROME_LABEL_MAX];
        self->find_pending = 0;
        snprintf(text, sizeof(text), "%s", ToriRSChrome_Text(ui->chrome, self->input_search));
        if( text[0] != '\0' )
            (void)squares_find(ui, self, text);
    }
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
    /* The cells the user left the pointer and the focus on are answered from
     * the new atlas: after a Place the pointer still stands on the cell it
     * clicked, and the readout and the pane went on saying "m10_50 empty"
     * of the square just put there (N4, 2026-11-09). */
    {
        struct Editor_Picker* picker = &self->picker;
        if( picker->hover_valid && picker->well_w > 0 )
        {
            int x;
            int y;
            int w;
            int h;
            Editor_PickerCellBox(picker, picker->hover_map_x, picker->hover_map_z, &x, &y, &w, &h);
            picker->hover = Editor_PickerSquareAt(picker, x + w / 2, y + h / 2);
        }
        if( picker->focus < 0 && picker->empty_valid
            && Editor_PickerFind(picker, picker->empty_map_x, picker->empty_map_z, -1) >= 0 )
            Editor_PickerFocusSet(picker, picker->empty_map_x, picker->empty_map_z);
    }
    squares_point_widgets(ui, self);
    squares_storage_rows(ui, self);
    self->strip_dirty = 1;
    self->pane_hash = 0;
    self->fetched = 1;
    self->dirty = 1;
    /* A cell Place judged may be taken now (or free again): judged anew. */
    self->place_said_valid = -1;
    squares_run_pending(ui, self);
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
            squares_run_pending(ui, self);
        }
        return;
    }
    memset(&atlas, 0, sizeof(atlas));
    {
        enum EditorHost_Status const status =
            Editor_HostMapEdCatalogSquaresFlags(&ui->editor->host, TORIRSMAPED_CATALOG_SQUARES_NOWAIT, &atlas);
        if( status == EDITOR_HOST_BUSY )
        {
            /* The index is still building: `fetched` stays 0 and the next
             * tick asks again (one round trip, microseconds). */
            if( !self->busy_logged )
                Editor_UiLog(ui, "editor: the catalog index is still building; the square picker waits for it");
            self->busy_logged = 1;
            return;
        }
        if( status != EDITOR_HOST_OK )
        {
            Editor_UiLog(ui, "editor: the server did not answer CATALOG_SQUARES");
            return;
        }
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

/** Whether the layout has given the well its own size yet (squares_well_w
 *  and _h answer a default until it has). */
static int
squares_well_laid(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    int const scale = ToriRSChrome_Scale(ui->chrome);
    struct ToriRSChromeWidget const* well = &ui->chrome->widgets[self->graph];

    return well->w / (scale > 0 ? scale : 1) > 32 && well->h / (scale > 0 ? scale : 1) > 32;
}

/**
 * The well fills the Square map window: content-sized (no layout height) it
 * is SQUARES_WELL_H at the chrome's scale; given a height by a resize, it
 * takes what the rows around it leave, down to SQUARES_WELL_MIN_H.
 *
 * The Squares window has no well. Its pane never hides a row: a panel given
 * less height than its rows take scrolls (the chrome's own scroll bar), so
 * Copy to..., Instance... and the rest are a wheel turn away at any size.
 * They used to fold away below ~650 px with nothing to say so, which in the
 * default layout was always (F4, 2026-11-11).
 */
static void
squares_size_well(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct ToriRSChromePanel const* map = &ui->chrome->panels[self->map_panel];
    struct ToriRSChromeWidget const* well = &ui->chrome->widgets[self->graph];
    int want = edp_px(ui, SQUARES_WELL_H);

    if( map->fixed_h > 0 && map->view_h > 0 && map->content_h > 0 )
    {
        want = map->view_h - (map->content_h - well->view_h);
        if( want < edp_px(ui, SQUARES_WELL_MIN_H) )
            want = edp_px(ui, SQUARES_WELL_MIN_H);
    }
    if( want != well->view_h )
        ToriRSChrome_SetCustomHeight(ui->chrome, self->graph, want);
}

/**
 * The Square map window, until the user moves or resizes it: most of the
 * world's box (SQUARES_MAP_NUM / SQUARES_MAP_DEN of it each way), flown out
 * beside the column by the layout (ui_layout_follow) like any window that is
 * not in it. Set again only when the world's box changes.
 */
static void
squares_size_map(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct ToriRSChromePanel const* map = &ui->chrome->panels[self->map_panel];
    int w = ui->world.w * SQUARES_MAP_NUM / SQUARES_MAP_DEN;
    int h = ui->world.h * SQUARES_MAP_NUM / SQUARES_MAP_DEN;

    if( !map->visible || map->user_placed || map->maximised || ui->world.w <= 0 || ui->world.h <= 0 )
        return;
    if( w < edp_px(ui, EDP_PICKER_W) )
        w = edp_px(ui, EDP_PICKER_W);
    if( h < edp_px(ui, SQUARES_WELL_H) )
        h = edp_px(ui, SQUARES_WELL_H);
    if( w == self->map_set_w && h == self->map_set_h )
        return;
    self->map_set_w = w;
    self->map_set_h = h;
    ToriRSChrome_PanelSetFixedWidth(ui->chrome, self->map_panel, w);
    ToriRSChrome_PanelSetFixedHeight(ui->chrome, self->map_panel, h);
}

/** Show or hide the Square map window. */
static void
squares_show_map(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int show)
{
    if( ui->chrome->panels[self->map_panel].visible == show )
        return;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->map_panel, show);
    self->dirty = 1;
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

    int const shown = ui->chrome->panels[self->panel].visible || ui->chrome->panels[self->map_panel].visible;

    /* Before the visibility test: the camera is owed whether or not the
     * Squares windows are still up. */
    squares_view_step(ui, self);
    /* So is the atlas. The picker is not this panel's alone: the instance
     * helper's Find free, the Areas filter and the New square dialog read
     * which squares exist off it (edp_squares_picker). While this test came
     * first, a session with Squares closed never fetched the atlas and never
     * heard of a square made since -- and Squares is closed by default since
     * 2026-11-09, when Find free offered the square the last Create had just
     * made. */
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
    /* The Place that waited for the atlas (Save on a scratch square while
     * the catalog index was still building). */
    if( self->place_waiting )
    {
        self->place_waiting = 0;
        Editor_UiLog(ui, "editor: the catalog index is ready");
        Editor_SquarePicker_Place(ui, self->place_source, self->place_scratch);
    }
    if( !shown )
    {
        /* Closed: what the document holds and the scratch square still land
         * on the picker; nothing is sized, baked or drawn. */
        if( ui->editor )
        {
            const struct EditorUiViewFrame* own = NULL;
            for( int f = 0; f < EDITOR_UI_VIEW_FRAMES_MAX; f++ )
                if( ui->frames[f].used && ui->frames[f].slot == ui->view_slot )
                    own = &ui->frames[f];
            Editor_PickerNoteDocument(picker, &ui->editor->doc, own);
            squares_scratch_sync(ui, self, own);
        }
        return;
    }
    squares_collection_rows(ui, self);

    squares_size_map(ui, self);
    squares_size_well(ui, self);
    /* Fitted until the user moves the view: the first frame the well has a
     * size, and again whenever the layout changes that size. */
    if( !self->user_view && ui->chrome->widgets[self->graph].w > 0
        && (picker->well_w != squares_well_w(ui, self) || picker->well_h != squares_well_h(ui, self)) )
    {
        Editor_PickerFit(picker, squares_well_w(ui, self), squares_well_h(ui, self));
        self->dirty = 1;
    }
    /* A pick's view, once the map window has laid its well out: centred
     * again each tick until the well keeps one size for two (the pick's
     * rows under it take their room a frame after it opens). */
    if( self->centre_pending && self->centre_w >= 0
        && (picker->origin_x != self->centre_origin_x || picker->origin_z != self->centre_origin_z
            || picker->zoom != self->centre_zoom) )
    {
        /* Something moved the view since (a Find, a pan): it is theirs. */
        self->centre_pending = 0;
    }
    if( self->centre_pending && ui->chrome->panels[self->map_panel].visible && squares_well_laid(ui, self) )
    {
        int const w = squares_well_w(ui, self);
        int const h = squares_well_h(ui, self);

        picker->well_w = w;
        picker->well_h = h;
        if( picker->zoom < EDITOR_PICKER_ZOOM_SHOW )
            Editor_PickerZoomTo(picker, EDITOR_PICKER_ZOOM_SHOW, (EDITOR_PICKER_LEFT + w) / 2,
                (EDITOR_PICKER_TOP + h) / 2);
        Editor_PickerCentre(picker, self->centre_x, self->centre_z);
        if( w == self->centre_w && h == self->centre_h )
            self->centre_pending = 0;
        self->centre_w = w;
        self->centre_h = h;
        self->centre_origin_x = picker->origin_x;
        self->centre_origin_z = picker->origin_z;
        self->centre_zoom = picker->zoom;
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
        squares_scratch_sync(ui, self, own);
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
    squares_pointer_follow(ui, self);
    squares_pane_update(ui, self);
    /* The map's close box ends a pick. */
    if( self->pick_mode != SQUARES_PICK_BROWSE && !ui->chrome->panels[self->map_panel].visible )
        squares_pick_leave(ui, self);
    /* A closed map keeps its dirt for the frame it opens on. The Squares
     * panel's own count line ("Full map: 2975 squares") is said all the
     * same when what it counts moved: it stayed blank for as long as the
     * map window was shut. */
    if( !ui->chrome->panels[self->map_panel].visible )
    {
        uint32_t const said = (picker->atlas_serial * 31u + (uint32_t)picker->active_tab) * 31u
                              + (uint32_t)picker->filter_shown + (picker->filter_on ? 7u : 0u) + 1u;
        if( said != self->status_said )
        {
            self->status_said = said;
            squares_readout(ui, self);
        }
        return;
    }
    self->status_said = 0;
    /* The well's size is part of what was drawn: on the frame the map
     * window opens the well has no size yet and the draw takes the default
     * one, and nothing else marked the map dirty when the layout then gave
     * it its real size -- the page sat in a corner of a large empty frame
     * until the pointer moved (F9, 2026-11-11). */
    if( squares_well_w(ui, self) != self->drawn_w || squares_well_h(ui, self) != self->drawn_h )
        self->dirty = 1;
    if( !self->dirty )
        return;
    self->drawn_w = squares_well_w(ui, self);
    self->drawn_h = squares_well_h(ui, self);
    Editor_PickerDraw(
        picker, ui->chrome, self->graph, self->drawn_w, self->drawn_h, &self->colors, ui->frames,
        EDITOR_UI_VIEW_FRAMES_MAX, ui->view_slot);
    squares_readout(ui, self);
    self->dirty = 0;
}

/* ---- actions ------------------------------------------------------------------------ */

/**
 * The scratch squares this window holds, into the open-set strip and the
 * pane: one is held while it is in this window's world (`own`, its view
 * frame) or has unplaced work in the mirror. The atlas lists none (a scratch
 * square has no cell), so the document is asked. A blank test runs only for
 * a scratch square that is not shown here, which is rare and never per
 * square of the map.
 */
static void
squares_scratch_sync(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    const struct EditorUiViewFrame* own)
{
    struct Editor_PickerOpenItem held[EDITOR_DOC_SCRATCH_SLOTS];
    struct Editor_Doc const* doc = &ui->editor->doc;
    uint32_t hash = 2166136261u;
    int count = 0;
    int here_any = 0;
    int here_x = 0;
    int here_z = 0;
    int here_dirty = 0;

    for( int i = 0; i < doc->square_count && count < EDITOR_DOC_SCRATCH_SLOTS; i++ )
    {
        struct Editor_Square const* square = &doc->squares[i];
        int here;
        int dirty;

        if( !square->loaded || !Editor_DocStorageIsScratch(doc->square_storage[i]) )
            continue;
        here = own && own->used && square->map_x >= own->map_x && square->map_x < own->map_x + own->w
               && square->map_z >= own->map_z && square->map_z < own->map_z + own->h;
        dirty = square->dirty_map || square->dirty_loc;
        if( !here && (!dirty || Editor_SquareIsBlank(square)) )
            continue;
        memset(&held[count], 0, sizeof(held[count]));
        snprintf(held[count].key, sizeof(held[count].key), "%s/m%d_%d", EDITOR_DOC_SCRATCH_STORAGE, square->map_x,
            square->map_z);
        held[count].map_x = square->map_x;
        held[count].map_z = square->map_z;
        held[count].here = here;
        held[count].dirty = dirty;
        hash = (hash ^ (uint32_t)(square->map_x * 1031 + square->map_z * 31 + here * 2 + dirty)) * 16777619u;
        if( here && !here_any )
        {
            here_any = 1;
            here_x = square->map_x;
            here_z = square->map_z;
            here_dirty = dirty;
        }
        count++;
    }
    if( hash != self->scratch_hash )
    {
        self->scratch_hash = hash;
        if( Editor_PickerOpenSetSyncScratch(&self->open_set, held, count) )
            self->strip_dirty = 1;
    }
    self->scratch_here = here_any;
    self->scratch_x = here_x;
    self->scratch_z = here_z;
    self->scratch_dirty = here_dirty;
}

/**
 * The cell under the pointer, asked of the chrome once a tick while the
 * pointer is over the well.
 *
 * A GRAPH_HOVER only comes when the keyed item under the pointer changes.
 * From 12 px a square the cells are not items (the world map is the fill),
 * and an empty cell never is: crossing from one empty cell to the next sent
 * nothing, so the readout and Place's "Click: creates m72_56" went on naming
 * the cell the pointer had left (F2). Nor does anything come when the well
 * moves under a still pointer (F8). Only while the chrome itself last saw
 * the pointer on this well: a hover a test or the env posts as an event is
 * left alone.
 */
static void
squares_pointer_follow(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct Editor_Picker* picker = &self->picker;
    int x = -1;
    int y = -1;
    int map_x = -1;
    int map_z = -1;
    int on_cell;

    if( !ui->chrome->panels[self->map_panel].visible || ui->chrome->graph_hover != self->graph )
        return;
    if( !ToriRSChrome_GraphPointer(ui->chrome, self->graph, &x, &y) )
    {
        /* The well moved from under it: no cell is hovered. */
        x = -1;
        y = -1;
    }
    if( Editor_PickerHover(picker, x, y) )
        self->dirty = 1;
    if( self->pick_mode != SQUARES_PICK_PLACE )
        return;
    on_cell = Editor_PickerCellAt(picker, x, y, &map_x, &map_z);
    if( on_cell == self->place_said_valid
        && (!on_cell || (map_x == self->place_said_x && map_z == self->place_said_z)) )
        return;
    squares_place_hover(ui, self, x, y);
}

/**
 * The camera a placed scratch square is owed (squares_place_go): put where
 * it looked before the Place, in the placed square, once the world that
 * shows it has landed and the open's own "camera to the centre" has run
 * (world_busy covers both). Given up after SQUARES_VIEW_TICKS.
 */
static void
squares_view_step(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    if( !self->view_pending )
        return;
    if( !ui->app_services.world_camera_set || !ui->app_services.world_busy )
    {
        self->view_pending = 0;
        return;
    }
    if( ++self->view_ticks > SQUARES_VIEW_TICKS )
    {
        self->view_pending = 0;
        Editor_UiLog(ui, "editor: the placed square did not come into the scene; the camera stays where it is");
        return;
    }
    if( ui->app_services.world_busy(ui->app_services.user_data) )
        return;
    if( !ui->app_services.world_camera_set(ui->app_services.user_data, &self->view_pose) )
        return; /* not in the scene yet: asked again next tick */
    self->view_pending = 0;
    if( ui->app_services.request_redraw )
        ui->app_services.request_redraw(ui->app_services.user_data);
}

/**
 * Where the World's camera looks, if that is inside scratch square
 * (scratch_x, scratch_z), as the pose that looks at the same local tile of
 * (to_x, to_z): same level, yaw, pitch and distance. 0 when the window has
 * no camera or it looks elsewhere (the open then centres as it always did).
 */
static int
squares_view_carry(
    struct EditorUi* ui,
    int scratch_x,
    int scratch_z,
    int to_x,
    int to_z,
    struct EditorViewCamera* out)
{
    int const level = ui->tools.edit_level < 0 ? 0 : ui->tools.edit_level;
    int const unit = 128;

    if( !ui->app_services.world_camera
        || !ui->app_services.world_camera(ui->app_services.user_data, level, out) )
        return 0;
    if( out->x < scratch_x * EDITOR_SQUARE_X * unit || out->x >= (scratch_x + 1) * EDITOR_SQUARE_X * unit
        || out->z < scratch_z * EDITOR_SQUARE_Z * unit || out->z >= (scratch_z + 1) * EDITOR_SQUARE_Z * unit )
        return 0;
    out->x += (to_x - scratch_x) * EDITOR_SQUARE_X * unit;
    out->z += (to_z - scratch_z) * EDITOR_SQUARE_Z * unit;
    return 1;
}

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
        /* Adding keeps what is open: nothing unsaved is at stake. Add never
         * takes a square OUT of the scene, so a square too far from what
         * this window shows to share one scene with it is refused here (the
         * world load would build around the new square and drop the rest,
         * app_map_editor_world_begin): Open is the way to go there. */
        int candidates[(EDITOR_DOC_MAX_SQUARES + 1) * 2];
        int kept[EDITOR_DOC_WORLD_SQUARES_MAX * 2];
        int dropped[(EDITOR_DOC_MAX_SQUARES + 1) * 2];
        int dropped_count = 0;
        int count = 0;

        for( int i = 0; i < picker->square_count && count < EDITOR_DOC_MAX_SQUARES; i++ )
        {
            if( !(picker->squares[i].flags & EDITOR_PICKER_OPEN_HERE) || i == square )
                continue;
            candidates[count * 2] = picker->squares[i].map_x;
            candidates[count * 2 + 1] = picker->squares[i].map_z;
            count++;
        }
        candidates[count * 2] = guard.map_x;
        candidates[count * 2 + 1] = guard.map_z;
        count++;
        (void)Editor_DocWorldFit(candidates, count, count - 1, kept, dropped, &dropped_count);
        if( dropped_count > 0 )
        {
            Editor_UiStatus(ui, "Add refused: m%d_%d is too far from the %d square%s shown for one scene (%d x %d at "
                                "most); Open shows it instead",
                guard.map_x, guard.map_z, count - 1, count == 2 ? "" : "s", EDITOR_DOC_WORLD_SIDE_MAX,
                EDITOR_DOC_WORLD_SIDE_MAX);
            return;
        }
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
        Editor_UiStatus(ui, "m%d_%d is empty: New square... (Ctrl+N) makes one here, its Place at filled in", map_x,
            map_z);
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

    snprintf(self->find_applied, sizeof(self->find_applied), "%s", text);
    /* Typed in the moments after the panel opened: the list of squares is not
     * here yet, and "m72_57 does not exist yet" was said of a square that does
     * (2026-11-12). Found when the list arrives. */
    if( !self->fetched )
    {
        self->find_pending = 1;
        Editor_UiStatus(ui, "Find: the square list is still loading; it finds '%s' when it arrives", text);
        return 0;
    }
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
        /* A square no world-map area claims (one made here, east of the
         * surface) is found on the Full map, among its neighbours: the
         * Unmapped page drew it alone in an empty frame. Instance space
         * keeps its own page. */
        if( match.square >= 0 && picker->squares[match.square].primary_area < 0
            && !squares_in_instance_space(match.map_x, match.map_z)
            && picker->tabs[squares_world_tab(picker)].area == EDITOR_PICKER_TAB_FULL
            && !Editor_PickerSquareOnTab(picker, match.square, picker->active_tab) )
            result.tab = squares_world_tab(picker);
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
        /* What was found is on the map: open it, and the arrows go on
         * from there. */
        if( !ui->chrome->panels[self->map_panel].visible )
            self->map_from_find = 1;
        squares_show_map(ui, self, 1);
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
    self->find_applied[0] = '\0';
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
        if( item->scratch )
            snprintf(tip, sizeof(tip), "%s: unsaved, Save places it on the Square map; click goes to it", text);
        else
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
    if( item->scratch )
    {
        /* No cell to focus: the map's focus goes, so the pane names the
         * scratch square; one not shown here opens as the working set. */
        Editor_PickerSelect(picker, -1, 0);
        picker->empty_valid = 0;
        self->dirty = 1;
        if( !item->here )
        {
            struct EditorUiGuard guard;
            memset(&guard, 0, sizeof(guard));
            guard.action = EDITOR_UI_GUARD_OPEN_SQUARE;
            guard.map_x = item->map_x;
            guard.map_z = item->map_z;
            snprintf(guard.storage, sizeof(guard.storage), "%s", EDITOR_DOC_SCRATCH_STORAGE);
            guard.storage_index = Editor_PickerStorageIndex(picker, EDITOR_DOC_SCRATCH_STORAGE);
            Editor_UiStatus(ui, "Opening the scratch square: Save places it on the Square map");
            (void)Editor_UiGuarded(ui, &guard);
            return;
        }
    }
    else
    {
        Editor_PickerFocusSet(picker, item->map_x, item->map_z);
        Editor_PickerKeepInView(picker, item->map_x, item->map_z);
        self->user_view = 1;
    }
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

/**
 * Close a square: through the save guard only when THAT square owes a save
 * (its tiles, locs or spawns). A close loses nothing either way -- a square
 * with unsaved edits stays an open document after it leaves the world -- so
 * asking about a scratch square elsewhere before closing a clean one was a
 * modal to dismiss on every close (2026-11-11).
 */
static void
squares_close_guarded(
    struct EditorUi* ui,
    const struct EditorUiGuard* guard)
{
    struct Editor_Square const* square;
    struct Editor_DocHandle handle;
    char key[EDITOR_DOC_KEY_MAX];
    int owes;

    /* No session (a panel test): the guard runs it, as it does any action. */
    if( !ui->editor )
    {
        (void)Editor_UiGuarded(ui, guard);
        return;
    }
    square = Editor_DocFindSquare(&ui->editor->doc, guard->map_x, guard->map_z);
    owes = square && square->loaded && (square->dirty_map || square->dirty_loc);
    Editor_SquareKey(key, guard->map_x, guard->map_z);
    if( Editor_DocFind(&ui->editor->doc, EDITOR_DOC_SPAWN, key, &handle) && handle.spawn->dirty )
        owes = 1;
    if( owes )
        (void)Editor_UiGuarded(ui, guard);
    else
        Editor_UiRunGuarded(ui, guard);
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
    if( item->scratch )
    {
        /* Closing it would leave unplaced work with no chip to come back by
         * but New's "Open unplaced scratch". */
        Editor_UiStatus(ui, "Close refused: a scratch square is placed, not closed; Save places it on the Square map");
        return;
    }
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
    squares_close_guarded(ui, &guard);
}

/**
 * Close square: the pane's button, the map's right-click row and File >
 * Close square (file.close_square). Which square: (map_x, map_z) when given
 * (>= 0, the right-clicked one), else the focused one when this window shows
 * it, else the one the World's camera looks at. Through the same guard and
 * refusals as the strip's Shift+click (squares_close_chip).
 */
static void
squares_close_square(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int map_x,
    int map_z)
{
    struct Editor_Picker const* picker = &self->picker;
    int const level = ui->tools.edit_level < 0 ? 0 : ui->tools.edit_level;
    struct EditorViewCamera pose;

    if( map_x < 0 && picker->focus >= 0 && (picker->squares[picker->focus].flags & EDITOR_PICKER_OPEN_HERE) )
    {
        map_x = picker->squares[picker->focus].map_x;
        map_z = picker->squares[picker->focus].map_z;
    }
    if( map_x < 0 && ui->app_services.world_camera
        && ui->app_services.world_camera(ui->app_services.user_data, level, &pose) )
    {
        map_x = pose.x / (EDITOR_SQUARE_X * 128);
        map_z = pose.z / (EDITOR_SQUARE_Z * 128);
    }
    if( map_x < 0 )
    {
        Editor_UiStatus(ui, "Close square refused: focus a square this window shows (click its chip in Squares)");
        return;
    }
    if( self->scratch_here && self->scratch_x == map_x && self->scratch_z == map_z )
    {
        Editor_UiStatus(ui, "Close refused: a scratch square is placed, not closed; Save places it on the Square map");
        return;
    }
    /* From the picker's flags, not the strip: they follow the document while
     * the Squares windows are closed too, and File > Close square works then. */
    {
        struct EditorUiGuard guard;
        int target = -1;
        int here = 0;

        for( int i = 0; i < picker->square_count; i++ )
        {
            if( !(picker->squares[i].flags & EDITOR_PICKER_OPEN_HERE) )
                continue;
            here++;
            if( picker->squares[i].map_x == map_x && picker->squares[i].map_z == map_z )
                target = i;
        }
        if( target < 0 )
        {
            Editor_UiStatus(ui, "Close square refused: m%d_%d is not in this window's world", map_x, map_z);
            return;
        }
        if( here <= 1 )
        {
            Editor_UiStatus(ui, "Close refused: m%d_%d is the only square this window shows; open another first",
                map_x, map_z);
            return;
        }
        memset(&guard, 0, sizeof(guard));
        guard.action = EDITOR_UI_GUARD_CLOSE_SQUARE;
        guard.map_x = map_x;
        guard.map_z = map_z;
        guard.storage_index = picker->squares[target].storage;
        squares_storage_of(picker, target, guard.storage);
        squares_close_guarded(ui, &guard);
    }
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
        self->scratch_here,
        self->scratch_x,
        self->scratch_z,
        self->scratch_dirty,
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
    if( !squares_pane_cell(self, &index, &map_x, &map_z) && self->scratch_here )
    {
        /* Nothing on the map is asked about, and this window is editing a
         * scratch square: the pane says so (N3, 2026-11-09). */
        char chip[EDITOR_PICKER_NAME_MAX];
        snprintf(key, sizeof(key), "%s/m%d_%d", EDITOR_DOC_SCRATCH_STORAGE, self->scratch_x, self->scratch_z);
        Editor_PickerChipText(key, chip);
        if( !squares_contents_text(ui, self->scratch_x, self->scratch_z, 0, contents, (int)sizeof(contents)) )
            snprintf(contents, sizeof(contents), "-");
        snprintf(self->pane_full[0], SQUARES_PANE_TEXT_MAX, "%s: unsaved, Save places it", chip);
        snprintf(self->pane_full[1], SQUARES_PANE_TEXT_MAX, "%s, open in this window", chip);
        snprintf(self->pane_full[2], SQUARES_PANE_TEXT_MAX, "no cell yet: Save asks for one on the Square map");
        snprintf(self->pane_full[3], SQUARES_PANE_TEXT_MAX, "%d x %d, placed by Save", EDITOR_SQUARE_X,
            EDITOR_SQUARE_Z);
        snprintf(self->pane_full[4], SQUARES_PANE_TEXT_MAX, "%s", contents);
        snprintf(self->pane_full[5], SQUARES_PANE_TEXT_MAX, "open in this window, unsaved: Save places it");
        snprintf(self->pane_full[6], SQUARES_PANE_TEXT_MAX, "-");
    }
    else if( !squares_pane_cell(self, &index, &map_x, &map_z) )
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
    const char* typed = ToriRSChrome_Text(ui->chrome, self->input_search);
    int map_x;
    int map_z;

    /* Pressed in the moments after the panel opened, before the square list
     * arrived: nothing can be found yet, and "no square is focused" was the
     * wrong answer (2026-11-12). Done when the list lands. */
    if( !self->fetched )
    {
        self->open_pending = add ? 2 : 1;
        Editor_UiStatus(ui, "%s: the square list is still loading; it %s when it arrives", add ? "Add" : "Open",
            add ? "adds" : "opens");
        return;
    }
    /* "m72_57" typed into Find and Add pressed without Enter: the square
     * typed, not the one focused before (or a refusal that none is). */
    /* Nor when the Find ran before the panel had a size (opened the frame
     * the text was typed): it found the square and could focus nothing. */
    if( typed[0] != '\0'
        && (strcmp(typed, self->find_applied) != 0
            || (picker->focus < 0 && !Editor_PickerFocusCell(picker, &map_x, &map_z))) )
    {
        char text[TORIRS_CHROME_LABEL_MAX];
        snprintf(text, sizeof(text), "%s", typed);
        if( !squares_find(ui, self, text) )
            return;
    }
    if( picker->focus >= 0 )
    {
        squares_open(ui, self, picker->focus, add);
        if( self->map_from_find && self->pick_mode == SQUARES_PICK_BROWSE )
            squares_show_map(ui, self, 0);
        self->map_from_find = 0;
        return;
    }
    if( !Editor_PickerFocusCell(picker, &map_x, &map_z) )
    {
        Editor_UiStatus(ui, "%s refused: no square is focused; click one or use the arrows", add ? "Add" : "Open");
        return;
    }
    if( add )
    {
        Editor_UiStatus(ui, "Add refused: m%d_%d is empty; Enter starts a New square", map_x, map_z);
        return;
    }
    Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_NEW, NULL);
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

/* ---- the pick: Open squares and Place ------------------------------------------- */

/** Place's Stored in rows: every storage but the scratch one, maps/ chosen,
 *  then New lane. */
static void
squares_pick_storages(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct Editor_Picker const* picker = &self->picker;
    int const maps = Editor_PickerStorageIndex(picker, "maps");
    int selected = 0;
    int n = 0;

    for( int i = 0; i < picker->storage_count && n < SQUARES_STORAGE_ROWS; i++ )
    {
        const char* storage = picker->storages[i];
        if( Editor_DocStorageIsScratch(storage) )
            continue;
        if( storage[0] == '\0' || strcmp(storage, "maps") == 0 )
            snprintf(self->place_labels[n], TORIRS_CHROME_LABEL_MAX, "maps/ (content tree)");
        else
            snprintf(self->place_labels[n], TORIRS_CHROME_LABEL_MAX, "%s/", storage);
        self->place_rows[n] = self->place_labels[n];
        self->place_index[n] = i;
        if( i == maps )
            selected = n;
        n++;
    }
    snprintf(self->place_labels[n], TORIRS_CHROME_LABEL_MAX, "New lane...");
    self->place_rows[n] = self->place_labels[n];
    self->place_index[n] = -1;
    n++;
    self->place_count = n;
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_place, self->place_rows, n, selected);
}

/** The pick's widgets: the line over the well always (the browsing hint, or
 *  the pick's instruction and verdict); under the well Stored in (and Lane,
 *  for a new one) while placing, Open while sweeping, Cancel for both. They
 *  are under it so that showing them shortens the well from its foot: its
 *  top, and with it every cell, stays where the pointer left it. */
static void
squares_pick_layout(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    int const browse = self->pick_mode == SQUARES_PICK_BROWSE;
    int const place = self->pick_mode == SQUARES_PICK_PLACE;
    int const row = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_place);
    int const new_lane = place && row >= 0 && row < self->place_count && self->place_index[row] < 0;

    if( browse )
    {
        ToriRSChrome_SetText(ui->chrome, self->row_pick, SQUARES_BROWSE_HINT);
        ToriRSChrome_SetColor(ui->chrome, self->row_pick, ui->chrome->theme.text_dim);
    }
    ToriRSChrome_SetHidden(ui->chrome, self->dropdown_place, !place);
    ToriRSChrome_SetHidden(ui->chrome, self->input_place_lane, !new_lane);
    ToriRSChrome_SetHidden(ui->chrome, self->check_place_spawns, !place || self->place_scratch);
    ToriRSChrome_SetHidden(ui->chrome, self->button_pick_go, self->pick_mode != SQUARES_PICK_SWEEP);
    ToriRSChrome_SetHidden(ui->chrome, self->button_pick_cancel, browse);
}

static void
squares_pick_say(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    const char* text,
    int ok)
{
    ToriRSChrome_SetText(ui->chrome, self->row_pick, text);
    ToriRSChrome_SetColor(ui->chrome, self->row_pick, ok ? 0x9fdf8f : 0xff7f5f);
}

/** Back to browsing: the bar hides, the sweep goes. */
static void
squares_pick_leave(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    if( self->pick_mode == SQUARES_PICK_BROWSE )
        return;
    self->pick_mode = SQUARES_PICK_BROWSE;
    self->sweeping = 0;
    self->picker.sweep_w = 0;
    self->picker.sweep_h = 0;
    squares_pick_layout(ui, self);
    self->dirty = 1;
}

static void
squares_pick_enter(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int mode)
{
    squares_menu_hide(ui, self);
    self->pick_mode = mode;
    self->sweeping = 0;
    self->picker.sweep_w = 0;
    self->picker.sweep_h = 0;
    self->picker.sweep_ok = 1;
    self->place_said_valid = -1;
    if( mode == SQUARES_PICK_PLACE )
    {
        squares_pick_storages(ui, self);
        ToriRSChrome_SetText(ui->chrome, self->input_place_lane, "");
    }
    squares_pick_layout(ui, self);
    self->map_from_find = 0;
    squares_show_map(ui, self, 1);
    squares_pick_page(ui, self);
    squares_pick_view(self);
}

/**
 * What a pick is made on: every square where it is. An area's page draws
 * only the squares the world map gives that area, so a square made in this
 * session (unmapped, east of the surface) was not on the page a Place or an
 * Open squares sweep opened on -- the cell read as empty ground and the next
 * square was placed blind (F9). A collection's or a lane's page is the
 * user's own choice and stays. The view is theirs from here: the bar's rows
 * resize the well, and a fitted page would rescale under the pointer.
 */
static void
squares_pick_page(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct Editor_Picker* picker = &self->picker;

    if( picker->tab_count > 0 && !picker->filter_on && self->area_mode == SQUARES_AREA_PAGE
        && picker->tabs[picker->active_tab].area != EDITOR_PICKER_TAB_FULL )
    {
        int const full = squares_world_tab(picker);
        if( picker->tabs[full].area == EDITOR_PICKER_TAB_FULL )
        {
            /* The page changes, the view does not: the same squares stay
             * under the same pixels, with their neighbours drawn in. */
            picker->active_tab = full;
            picker->hover = -1;
            squares_area_show(ui, self);
        }
    }
    self->user_view = 1;
}

/**
 * Where a pick opens the map: on the work, close enough to click a cell
 * (EDITOR_PICKER_ZOOM_SHOW a square at least). A Copy to's Place, on the
 * square being copied; a scratch square's Place, on the first free cell
 * beside the squares made here (Editor_NewSquareFindFree); a sweep, on the
 * squares this window shows. A pick used to keep whatever view the map had
 * -- on a map not opened yet this session, the page's corner at the
 * start-up zoom: an empty frame with no cell in it (F9, 2026-11-11). Applied
 * by the tick once the well has its size (centre_pending).
 */
static void
squares_pick_view(struct SquaresPanel* self)
{
    struct Editor_Picker* picker = &self->picker;
    int x0 = 1 << 30;
    int x1 = -1;
    int z0 = 1 << 30;
    int z1 = -1;
    int map_x = (EDITOR_NEWFORM_FREE_X0 + EDITOR_NEWFORM_FREE_X1) / 2;
    int map_z = (EDITOR_NEWFORM_FREE_Z0 + EDITOR_NEWFORM_FREE_Z1) / 2;

    for( int i = 0; i < picker->square_count; i++ )
    {
        const struct Editor_PickerSquare* square = &picker->squares[i];
        if( !(square->flags & EDITOR_PICKER_OPEN_HERE) )
            continue;
        x0 = square->map_x < x0 ? square->map_x : x0;
        x1 = square->map_x > x1 ? square->map_x : x1;
        z0 = square->map_z < z0 ? square->map_z : z0;
        z1 = square->map_z > z1 ? square->map_z : z1;
    }
    if( self->pick_mode == SQUARES_PICK_PLACE )
    {
        const char* key = strrchr(self->place_source, '/');
        int x;
        int z;

        key = key ? key + 1 : self->place_source;
        if( !self->place_scratch && Editor_SquareKeyParse(key, &x, &z) )
        {
            map_x = x;
            map_z = z;
        }
        else if( Editor_NewSquareFindFree(picker, 1, 1, &x, &z) )
        {
            map_x = x;
            map_z = z;
        }
    }
    else if( x1 >= 0 )
    {
        map_x = (x0 + x1) / 2;
        map_z = (z0 + z1) / 2;
    }
    else
        (void)Editor_NewSquareFindFree(picker, 1, 1, &map_x, &map_z);
    self->centre_pending = 1;
    self->centre_x = map_x;
    self->centre_z = map_z;
    self->centre_w = -1;
    self->centre_h = -1;
    self->user_view = 1;
    self->dirty = 1;
}

/** The squares the sweep holds -- each cell's on the page's storage, else
 *  any, empty cells counted in `out_empty` -- the one nearest the middle
 *  first (where the camera goes). Returns how many. */
static int
squares_sweep_refs(
    struct SquaresPanel* self,
    struct EditorUiSquareRef* out,
    int* out_empty)
{
    struct Editor_Picker const* picker = &self->picker;
    int const mid_x2 = 2 * picker->sweep_x + picker->sweep_w - 1;
    int const mid_z2 = 2 * picker->sweep_z + picker->sweep_h - 1;
    int best = -1;
    int best_d = 0;
    int count = 0;

    *out_empty = 0;
    for( int z = picker->sweep_z; z < picker->sweep_z + picker->sweep_h; z++ )
    {
        for( int x = picker->sweep_x; x < picker->sweep_x + picker->sweep_w; x++ )
        {
            int const square = Editor_PickerFind(picker, x, z, picker->storage_filter);
            int d;
            if( square < 0 )
            {
                (*out_empty)++;
                continue;
            }
            assert(count < EDITOR_UI_OPEN_SET_MAX);
            out[count].map_x = x;
            out[count].map_z = z;
            squares_storage_of(picker, square, out[count].storage);
            d = abs(2 * x - mid_x2) + abs(2 * z - mid_z2);
            if( best < 0 || d < best_d )
            {
                best = count;
                best_d = d;
            }
            count++;
        }
    }
    if( best > 0 )
    {
        struct EditorUiSquareRef const first = out[0];
        out[0] = out[best];
        out[best] = first;
    }
    return count;
}

static void
squares_sweep_say(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct Editor_Picker const* picker = &self->picker;
    struct EditorUiSquareRef refs[EDITOR_UI_OPEN_SET_MAX];
    char line[TORIRS_CHROME_LABEL_MAX];
    int empty;
    int count;

    if( picker->sweep_w <= 0 )
    {
        squares_pick_say(ui, self, "Drag over up to 3x3 squares, then Open", 1);
        ToriRSChrome_SetDisabled(ui->chrome, self->button_pick_go, 1);
        return;
    }
    count = squares_sweep_refs(self, refs, &empty);
    if( picker->sweep_w == 1 && picker->sweep_h == 1 )
        snprintf(line, sizeof(line), "m%d_%d: %s", picker->sweep_x, picker->sweep_z, count ? "1 square" : "empty");
    else
        snprintf(line, sizeof(line), "m%d_%d .. m%d_%d: %d square%s%s", picker->sweep_x, picker->sweep_z,
            picker->sweep_x + picker->sweep_w - 1, picker->sweep_z + picker->sweep_h - 1, count,
            count == 1 ? "" : "s", empty ? " (empty cells skipped)" : "");
    squares_pick_say(ui, self, line, count > 0);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_pick_go, count == 0);
}

/** Open: the swept squares as the working set, through the save guard. */
static void
squares_sweep_open(
    struct EditorUi* ui,
    struct SquaresPanel* self)
{
    struct EditorUiGuard guard;
    int empty;

    memset(&guard, 0, sizeof(guard));
    guard.action = EDITOR_UI_GUARD_OPEN_SET;
    guard.set_count = self->picker.sweep_w > 0 ? squares_sweep_refs(self, guard.set, &empty) : 0;
    if( guard.set_count == 0 )
    {
        squares_pick_say(ui, self, "No squares there to open: drag over some", 0);
        return;
    }
    guard.map_x = guard.set[0].map_x;
    guard.map_z = guard.set[0].map_z;
    for( int i = 0; i < guard.set_count; i++ )
    {
        char key[EDITOR_DOC_KEY_MAX];
        if( guard.set[i].storage[0] )
            snprintf(key, sizeof(key), "%s/m%d_%d", guard.set[i].storage, guard.set[i].map_x, guard.set[i].map_z);
        else
            snprintf(key, sizeof(key), "m%d_%d", guard.set[i].map_x, guard.set[i].map_z);
        Editor_PickerRecentPush(&self->recent, key);
    }
    self->recent_dirty = 1;
    squares_pick_leave(ui, self);
    (void)Editor_UiGuarded(ui, &guard);
}

/** Place's form for cell (map_x, map_z): a copy of the source there, in the
 *  storage the bar names. */
static void
squares_place_form(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int map_x,
    int map_z,
    struct Editor_NewSquareForm* form)
{
    int const row = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_place);

    memset(form, 0, sizeof(*form));
    form->mode = EDITOR_NEW_SQUARE_COPY_TO;
    form->map_x = map_x;
    form->map_z = map_z;
    form->storage = row >= 0 && row < self->place_count ? self->place_index[row] : 1 << 20;
    snprintf(form->new_lane, sizeof(form->new_lane), "%s", ToriRSChrome_Text(ui->chrome, self->input_place_lane));
    form->template_kind = EDITOR_DOC_TEMPLATE_COPY;
    snprintf(form->source_key, sizeof(form->source_key), "%s", self->place_source);
}

/** The hovered cell as Place's destination: outlined green or in the
 *  accent, the bar saying what a click does or why it cannot. */
static void
squares_place_hover(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int x,
    int y)
{
    struct Editor_Picker* picker = &self->picker;
    struct Editor_NewSquareForm form;
    struct Editor_NewSquareCheck check;
    char line[TORIRS_CHROME_LABEL_MAX];
    int map_x;
    int map_z;

    if( !Editor_PickerCellAt(picker, x, y, &map_x, &map_z) )
    {
        if( picker->sweep_w > 0 )
            self->dirty = 1;
        picker->sweep_w = 0;
        self->place_said_valid = 0;
        snprintf(line, sizeof(line), "Click the cell %s goes in", self->place_source);
        squares_pick_say(ui, self, line, 1);
        return;
    }
    self->place_said_valid = 1;
    self->place_said_x = map_x;
    self->place_said_z = map_z;
    squares_place_form(ui, self, map_x, map_z, &form);
    picker->sweep_ok = Editor_NewSquareValidate(picker, &form, &check, NULL);
    picker->sweep_x = map_x;
    picker->sweep_z = map_z;
    picker->sweep_w = 1;
    picker->sweep_h = 1;
    if( picker->sweep_ok )
        snprintf(line, sizeof(line), "Click: %s", check.message);
    else
        snprintf(line, sizeof(line), "%s", check.message);
    squares_pick_say(ui, self, line, picker->sweep_ok);
    self->dirty = 1;
}

/**
 * Place the source on cell (map_x, map_z): DOC_CREATE a copy there, with
 * the source's spawns in the same undo step (edp_square_create: always for
 * a scratch square, by the bar's box for Copy to). A scratch square's Place
 * then clears the scratch slot and its spawn rows (one undo step of its
 * own), saves -- Save is what asked -- and opens the placed square as the
 * working set, the camera owed the tile it was looking at (squares_view_step);
 * the next unplaced scratch square, if any, is asked about next. Copy to
 * adds the copy to the open set.
 */
static void
squares_place_go(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int map_x,
    int map_z)
{
    struct Editor_Picker* picker = &self->picker;
    struct Editor_NewSquareForm form;
    struct Editor_NewSquareCheck check;
    struct EditorHost_DocCreate request;
    struct EditorUiSquareRef placed;
    enum EditorHost_Status status;
    char next[EDITOR_DOC_KEY_MAX];
    int const with_spawns = self->place_scratch || ToriRSChrome_Checked(ui->chrome, self->check_place_spawns);
    int spawns = 0;

    squares_place_form(ui, self, map_x, map_z, &form);
    if( !Editor_NewSquareValidate(picker, &form, &check, &request) )
    {
        squares_pick_say(ui, self, check.message, 0);
        return;
    }
    if( !ui->editor )
    {
        squares_pick_say(ui, self, "no editor session", 0);
        return;
    }
    status = edp_square_create(ui, &request, with_spawns, &spawns);
    if( status != EDITOR_HOST_OK )
    {
        squares_pick_say(ui, self,
            status == EDITOR_HOST_REFUSED ? "refused: it exists there, or is open from another storage"
                                          : "the server did not place it",
            0);
        Editor_UiLog(ui, "editor: placing %s at %s refused (status %d)", self->place_source, request.key,
            (int)status);
        return;
    }
    memset(&placed, 0, sizeof(placed));
    placed.map_x = map_x;
    placed.map_z = map_z;
    snprintf(placed.storage, sizeof(placed.storage), "%s", request.storage);
    squares_pick_leave(ui, self);
    self->want_refresh = 1;
    if( !self->place_scratch )
    {
        if( with_spawns )
            Editor_UiStatus(ui, "Copied %s to %s with its %d spawn%s; unsaved until Save", self->place_source,
                request.key, spawns, spawns == 1 ? "" : "s");
        else
            Editor_UiStatus(ui, "Copied %s to %s; its spawns stay behind (\"with its spawns\" is off)",
                self->place_source, request.key);
        Editor_UiLog(ui, "editor: %s (unsaved until Save)", check.message);
        Editor_UiOpenSquare(ui, map_x, map_z, request.storage, Editor_PickerStorageIndex(picker, request.storage), 1);
        return;
    }

    {
        char storage[EDITOR_DOC_STORAGE_MAX];
        char bare[EDITOR_DOC_KEY_MAX];
        int scratch_x;
        int scratch_z;
        int saved;
        self->view_pending = 0;
        if( Editor_DocSplitSquareKey(self->place_source, storage, bare)
            && Editor_SquareKeyParse(bare, &scratch_x, &scratch_z) )
        {
            /* Read before the slot is cleared and the world moves on. */
            if( squares_view_carry(ui, scratch_x, scratch_z, map_x, map_z, &self->view_pose) )
            {
                self->view_pending = 1;
                self->view_ticks = 0;
            }
            if( edp_ensure_doc(ui, EDITOR_DOC_SQUARE, self->place_source) )
                (void)edp_scratch_clear(ui, scratch_x, scratch_z);
        }
        saved = Editor_SaveAll(ui->editor);
        if( saved < 0 )
            Editor_UiStatus(ui, "Placed the scratch square as %s%s%s with %d spawn%s; NOT saved: the session is read-only",
                request.storage[0] ? request.storage : "", request.storage[0] ? "/" : "", request.key, spawns,
                spawns == 1 ? "" : "s");
        else
            Editor_UiStatus(ui, "Placed the scratch square as %s%s%s with %d spawn%s; saved %d document%s; opening it",
                request.storage[0] ? request.storage : "", request.storage[0] ? "/" : "", request.key, spawns,
                spawns == 1 ? "" : "s", saved, saved == 1 ? "" : "s");
    }
    /* The placed square is what the window shows and what the map and the
     * pane name: its cell is focused now (an empty one until the atlas that
     * lists it lands, squares_take_atlas). */
    Editor_PickerFocusSet(picker, map_x, map_z);
    self->pane_hash = 0;
    Editor_UiOpenSquareSet(ui, &placed, 1);
    if( Editor_NewSquare_UnplacedScratch(ui, next) )
        Editor_SquarePicker_Place(ui, next, 1);
}

/** A well event while picking; 0 for the ones browsing's handling keeps
 *  (wheel, pinch, scroll, the keys but Escape). */
static int
squares_pick_event(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    const struct ToriRSChromeEvent* event)
{
    struct Editor_Picker* picker = &self->picker;
    int map_x;
    int map_z;

    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_PRESS )
    {
        squares_menu_hide(ui, self);
        if( !Editor_PickerCellAt(picker, event->x, event->y, &map_x, &map_z) )
            return 1;
        if( self->pick_mode == SQUARES_PICK_PLACE )
        {
            squares_place_go(ui, self, map_x, map_z);
            return 1;
        }
        self->sweeping = 1;
        self->sweep_anchor_x = map_x;
        self->sweep_anchor_z = map_z;
    }
    else if( event->kind == TORIRS_CHROME_EVENT_GRAPH_DRAG )
    {
        if( self->pick_mode != SQUARES_PICK_SWEEP || !self->sweeping
            || !Editor_PickerCellAt(picker, event->x, event->y, &map_x, &map_z) )
            return 1;
    }
    else if( event->kind == TORIRS_CHROME_EVENT_GRAPH_RELEASE )
    {
        self->sweeping = 0;
        return 1;
    }
    else if( event->kind == TORIRS_CHROME_EVENT_GRAPH_HOVER )
    {
        if( Editor_PickerHover(picker, event->x, event->y) )
            self->dirty = 1;
        if( self->pick_mode == SQUARES_PICK_PLACE )
            squares_place_hover(ui, self, event->x, event->y);
        return 1;
    }
    else if( event->kind == TORIRS_CHROME_EVENT_GRAPH_MENU )
        return 1;
    else if( event->kind == TORIRS_CHROME_EVENT_GRAPH_KEY && event->value == -TORIRS_CHROME_KEY_ESCAPE )
    {
        squares_pick_leave(ui, self);
        return 1;
    }
    else
        return 0;

    /* A press or a drag of the sweep: the rectangle from its first cell. */
    Editor_PickerSweepRect(self->sweep_anchor_x, self->sweep_anchor_z, map_x, map_z, SQUARES_SWEEP_SIDE,
        &picker->sweep_x, &picker->sweep_z, &picker->sweep_w, &picker->sweep_h);
    picker->sweep_ok = 1;
    squares_sweep_say(ui, self);
    self->dirty = 1;
    return 1;
}

/** The bar's widgets. */
static int
squares_pick_activate(
    struct EditorUi* ui,
    struct SquaresPanel* self,
    int widget)
{
    if( widget == self->button_pick_cancel )
    {
        squares_pick_leave(ui, self);
        return 1;
    }
    if( widget == self->button_pick_go )
    {
        squares_sweep_open(ui, self);
        return 1;
    }
    if( widget == self->dropdown_place || widget == self->input_place_lane )
    {
        squares_pick_layout(ui, self);
        self->place_said_valid = -1;
        if( self->picker.sweep_w > 0 && self->picker.hover_valid )
        {
            int x;
            int y;
            int w;
            int h;
            Editor_PickerCellBox(&self->picker, self->picker.sweep_x, self->picker.sweep_z, &x, &y, &w, &h);
            squares_place_hover(ui, self, x + w / 2, y + h / 2);
        }
        return 1;
    }
    return 0;
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
    if( self->pick_mode != SQUARES_PICK_BROWSE && squares_pick_event(ui, self, event) )
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
    if( widget == self->button_close )
    {
        squares_close_square(ui, self, -1, -1);
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
            Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_COPY_TO, key);
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
        else if( i == MENU_CLOSE )
            squares_close_square(ui, self, picker->squares[square].map_x, picker->squares[square].map_z);
        else if( i == MENU_NEW_VIEW )
            squares_new_view(ui, self, square);
        else if( i == MENU_COPY_TO )
            Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_COPY_TO, key);
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
        if( picker->focus >= 0 )
            Editor_PickerSquareKey(picker, picker->focus, key);
        Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_NEW, picker->focus >= 0 ? key : NULL);
        return 1;
    }
    if( squares_pick_activate(ui, self, widget) )
        return 1;
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
    if( widget == self->button_map )
    {
        self->map_from_find = 0;
        squares_show_map(ui, self, !ui->chrome->panels[self->map_panel].visible);
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

/** The Square map module's: not part of the default set, it opens from the
 *  Squares panel's Map, a Find, the tool rail or View > Square map. */
static void
squaremap_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct SquaresPanel* self = opaque;
    (void)visible;
    squares_menu_hide(ui, self);
    squares_pick_leave(ui, self);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->map_panel, 0);
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

void
Editor_SquarePicker_Place(
    struct EditorUi* ui,
    const char* source_key,
    int scratch)
{
    struct SquaresPanel* self;
    char line[TORIRS_CHROME_LABEL_MAX];

    assert(ui);
    assert(source_key);
    self = squares_self(ui);
    if( !self )
    {
        Editor_UiLog(ui, "editor: no Square map in this window to place %s on", source_key);
        return;
    }
    if( !self->fetched )
        squares_fetch(ui, self);
    snprintf(self->place_source, sizeof(self->place_source), "%s", source_key);
    self->place_scratch = scratch;
    if( !self->fetched )
    {
        /* The atlas is the catalog index's, and a fresh lane builds that for
         * a minute or two: the Place is owed, not refused (it used to say
         * "is the server reachable?" of a server that had just answered). */
        self->place_waiting = 1;
        Editor_UiStatus(ui, "The catalog index is still building: the Square map opens to place %s when it is ready",
            source_key);
        return;
    }
    self->place_waiting = 0;
    squares_pick_enter(ui, self, SQUARES_PICK_PLACE);
    snprintf(line, sizeof(line), "Click the cell %s goes in", source_key);
    squares_pick_say(ui, self, line, 1);
    Editor_UiStatus(ui, "Place %s: click its cell on the Square map", source_key);
}

void
Editor_SquarePicker_OpenSquares(struct EditorUi* ui)
{
    struct SquaresPanel* self;

    assert(ui);
    self = squares_self(ui);
    if( !self )
    {
        Editor_UiLog(ui, "editor: no Square map in this window");
        return;
    }
    if( !self->fetched )
        squares_fetch(ui, self);
    if( !self->fetched )
    {
        Editor_UiStatus(ui, "Open squares refused: the catalog index is still building; the map has no squares yet");
        return;
    }
    squares_pick_enter(ui, self, SQUARES_PICK_SWEEP);
    squares_sweep_say(ui, self);
    Editor_UiStatus(ui, "Open squares: drag over up to 3x3 squares on the Square map, then Open");
}

void
Editor_SquarePicker_CloseSquare(struct EditorUi* ui)
{
    struct SquaresPanel* self;

    assert(ui);
    self = squares_self(ui);
    if( !self || !self->fetched )
    {
        Editor_UiStatus(ui, "Close square refused: the square list is not in yet");
        return;
    }
    squares_close_square(ui, self, -1, -1);
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
    /* The open set: one chip per square this window holds. */
    self->strip = ToriRSChrome_Graph(chrome, self->panel, "", edp_px(ui, SQUARES_STRIP_H));
    self->row_status = ToriRSChrome_Label(chrome, self->panel, "");
    /* The detail pane: the focused (else hovered) square. Its heading, then
     * what can be done with it, then the details: the actions are what a
     * short panel must reach first. */
    self->pane_rows[0] = ToriRSChrome_Heading(chrome, self->panel, "");
    self->button_open = ToriRSChrome_Button(chrome, self->panel, "Open");
    ToriRSChrome_SetTooltip(chrome, self->button_open, "Open it INSTEAD of what is open (asks to save first)");
    self->button_add = ToriRSChrome_Button(chrome, self->panel, "Add to open set");
    ToriRSChrome_SetTooltip(chrome, self->button_add, "Add it to what this window shows (a scene is 4x4 at most)");
    self->button_close = ToriRSChrome_Button(chrome, self->panel, "Close square");
    ToriRSChrome_SetTooltip(chrome, self->button_close, "Drop it from what this window shows (asks to save first)");
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
    for( int row = 1; row < EDITOR_SQUARES_PANE_ROWS; row++ )
        self->pane_rows[row] = ToriRSChrome_KeyValue(chrome, self->panel, k_pane_keys[row], "-");
    self->dropdown_recent = ToriRSChrome_Dropdown(chrome, self->panel, "Recent", NULL, 0, -1);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_recent, "The last squares opened: choose one to go to it");
    self->recent_dirty = 1;
    self->button_new = ToriRSChrome_Button(chrome, self->panel, "New square...");
    ToriRSChrome_SetTooltip(chrome, self->button_new,
        "One square or a block: placed by key, or shaped first");
    self->button_map = ToriRSChrome_Button(chrome, self->panel, "Map");
    ToriRSChrome_SetTooltip(chrome, self->button_map, "Open or close the Square map window");
    self->button_fit = ToriRSChrome_Button(chrome, self->panel, "Fit");
    self->button_refresh = ToriRSChrome_Button(chrome, self->panel, "Refresh");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    /* The map in a window of its own: the well, and the readout naming the
     * hovered square under it. Placed and sized by squares_size_map. */
    self->map_panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, x, edp_px(ui, EDP_TOP_Y), edp_px(ui, EDP_PICKER_W), "Square map");
    assert(self->map_panel >= 0);
    /* The pick's line (the browsing hint, a Place's or a sweep's verdict)
     * over the well, always; the pick's own rows under it (squares_pick_
     * layout says why there). */
    self->row_pick = ToriRSChrome_Label(chrome, self->map_panel, SQUARES_BROWSE_HINT);
    self->graph = ToriRSChrome_Graph(chrome, self->map_panel, "", edp_px(ui, SQUARES_WELL_H));
    /* The map zooms with the wheel (squares_event), wherever its panel is. */
    ToriRSChrome_GraphSetWheel(chrome, self->graph, 1);
    ToriRSChrome_SetTooltip(chrome, self->graph, "click: open, wheel: zoom, drag: pan, arrows: focus, Enter: open");
    self->row_hover = ToriRSChrome_Label(chrome, self->map_panel, " ");
    self->dropdown_place = ToriRSChrome_Dropdown(chrome, self->map_panel, "Stored in", NULL, 0, -1);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_place,
        "maps/ is what the cache pack reads; a lane ships with its lane");
    self->input_place_lane = ToriRSChrome_TextInput(chrome, self->map_panel, "Lane", "");
    ToriRSChrome_SetTooltip(chrome, self->input_place_lane, "The new lane's directory: ported/<lane>/maps");
    self->check_place_spawns = ToriRSChrome_Checkbox(chrome, self->map_panel, "with its spawns", 1);
    ToriRSChrome_SetTooltip(chrome, self->check_place_spawns,
        "The copy brings the source square's npcs and ground items");
    self->button_pick_go = ToriRSChrome_Button(chrome, self->map_panel, "Open");
    self->button_pick_cancel = ToriRSChrome_Button(chrome, self->map_panel, "Cancel");
    ToriRSChrome_PanelSetResizable(chrome, self->map_panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->map_panel, 0);
    squares_pick_layout(ui, self);
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
    Editor_UiAddPanel(ui, &desc);

    /* The Square map's own registry entry, for the View menu, the layout
     * file and a pop-out; the Squares module above ticks it, takes its
     * events and frees it. */
    memset(&desc, 0, sizeof(desc));
    desc.name = "squaremap";
    desc.title = "Square map";
    desc.self = self;
    desc.chrome_panel = self->map_panel;
    desc.set_visible = squaremap_set_visible;
    Editor_UiAddPanel(ui, &desc);
}
