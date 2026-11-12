#ifndef SRC_EDITOR_PANELS_EDPANEL_PICKER_H
#define SRC_EDITOR_PANELS_EDPANEL_PICKER_H

/**
 * The square picker's model (docs/MAPED_DESIGN.html #squares): the atlas the
 * server's catalog serves (CATALOG_SQUARES), cut into one tab per world-map
 * area plus Unmapped, laid out as a grid of thumbnail cells with north up,
 * zoomed and panned, hit-tested, searched, and drawn into a GRAPH well.
 *
 * Holds no pointer into the atlas it was given (that answer is freed by its
 * caller) and none into the chrome beyond the call that draws. The tab titles
 * it lends a chrome TABSTRIP live as long as the model.
 *
 * Coordinates: `map` is a square's (map_x, map_z); `well` is the GRAPH's
 * local logical pixels, x right, y down. Map z grows NORTH, so it grows UP
 * the well.
 */

#include "editor/editor_cmd.h"
#include "editor/editor_host.h"
#include "editor/editor_ui.h"

#include <math.h>
#include <stdint.h>

struct ToriRSChrome;
struct Editor_Doc;
struct Editor_Picker;

/* The global pixel space of a zoom (game/rs_worldmap_overview.h), which the
 * grid and the world map's pictures share: square column (or north-based
 * row) m starts at floor(m * 64 * (zoom / 64)). */
static inline int
Editor_PickerEdge(
    double zoom,
    int squares)
{
    double const k = zoom / 64.0;
    return (int)floor((double)(squares * 64) * k);
}

/** The square column (or north-based row) holding global pixel `g`. */
static inline int
Editor_PickerSquareOfPixel(
    double zoom,
    int g)
{
    int m = (int)floor(((double)g + 0.5) / zoom);
    while( Editor_PickerEdge(zoom, m + 1) <= g )
        m++;
    while( Editor_PickerEdge(zoom, m) > g )
        m--;
    return m;
}

#define EDITOR_PICKER_TITLE_MAX 32
#define EDITOR_PICKER_NAME_MAX 64

/** The zoom, logical px per square, is continuous (struct Editor_Picker
 *  `zoom`): from the whole page fitting the well (Editor_PickerZoomMin) to
 *  one square filling it (Editor_PickerZoomMax). A wheel notch is this
 *  ratio. */
#define EDITOR_PICKER_ZOOM_STEP 1.25
/** Never fewer px a square than this: the Full map's ~200 rows still fit
 *  a small well. */
#define EDITOR_PICKER_ZOOM_FLOOR 0.25
/** A small page may still be backed out of to this many px a square. */
#define EDITOR_PICKER_ZOOM_OUT_TO 4.0
/** The closest the zoom always reaches, whatever the well: the CS2 world
 *  map's 8 px a tile is 512 device px a square, 256 logical at 2x. */
#define EDITOR_PICKER_ZOOM_CLOSEST 256.0
/** A search result is shown at least this close. */
#define EDITOR_PICKER_ZOOM_SHOW 24.0
/** The world's north edge in squares: square rows of the global pixel
 *  space count from it (game/rs_worldmap_overview.h). */
#define EDITOR_PICKER_WORLD_SQUARES 256
/** Axis labels: the least room one takes across (x) and down (z). */
#define EDITOR_PICKER_LABEL_W 30
#define EDITOR_PICKER_LABEL_H 16

/** Where the grid starts inside the well: room for the axis labels. */
#define EDITOR_PICKER_LEFT 22
#define EDITOR_PICKER_TOP 12

/** GRAPH items the picker may spend (the chrome's pool is shared by every
 *  well on the page). Past it the far cells are not drawn and the readout
 *  says so. */
#define EDITOR_PICKER_ITEMS_BUDGET 2600

/* ---- level of detail by zoom (the workbench design, section 5) ----------------------
 *
 *   under 12 px   the atlas row's average colour, the page's outline once;
 *                 no pictures, no labels
 *   12 to 40      the world-map picture; no icons, no labels
 *   40 to 96      the picture, open / dirty outlines, a dot per 10 spawns,
 *                 the coordinate label on hover only
 *   96 and up     the picture with the map's icons and names, the loc count,
 *                 the 8x8 zone grid faint
 *
 * Each band keeps what the bands below it draw (an outline at 40 is still
 * one at 200), except that the colour fill gives way to the picture at 12.
 * What the user did -- the selection, the hover, the empty cell New would
 * make, the view frames -- is drawn at every zoom. */
#define EDITOR_PICKER_LOD_PICTURE_FROM 12.0
#define EDITOR_PICKER_LOD_DETAIL_FROM 40.0
#define EDITOR_PICKER_LOD_CLOSE_FROM 96.0

enum Editor_PickerLodBand
{
    EDITOR_PICKER_LOD_COLOUR = 0,
    EDITOR_PICKER_LOD_PICTURE,
    EDITOR_PICKER_LOD_DETAIL,
    EDITOR_PICKER_LOD_CLOSE,
};

/** What a zoom draws (Editor_PickerLodAt). */
struct Editor_PickerLod
{
    enum Editor_PickerLodBand band;
    /** Cells filled with the atlas row's colour (no picture under them). */
    int colour_cells;
    /** The page's outline, one rect around its squares. */
    int page_outline;
    /** The world-map pictures under the grid. */
    int pictures;
    /** A lane's squares: the blue inset. */
    int lane_inset;
    /** Open here / unsaved / created outlines. */
    int state_outlines;
    /** A dot per EDITOR_PICKER_SPAWNS_PER_DOT spawns. */
    int spawn_dots;
    /** "m50_50" in the hovered cell. */
    int hover_label;
    /** The map's element icons. */
    int icons;
    /** The map's area names. */
    int names;
    /** "61 locs" in each cell. */
    int loc_count;
    /** The 8x8 zone lines, faint. */
    int zone_grid;
};

/** Spawns a dot stands for, and the most dots a cell carries. */
#define EDITOR_PICKER_SPAWNS_PER_DOT 10
#define EDITOR_PICKER_DOTS_MAX 12

/** What zoom `zoom` (logical px a square, > 0) draws. Pure. */
void
Editor_PickerLodAt(
    double zoom,
    struct Editor_PickerLod* out);

/** What the item budget dropped on the last draw, by kind: never silent (the
 *  readout says "not all drawn", the console gets one line per change). */
enum Editor_PickerDropKind
{
    EDITOR_PICKER_DROP_CELLS = 0,
    EDITOR_PICKER_DROP_OUTLINES,
    EDITOR_PICKER_DROP_PICTURES,
    EDITOR_PICKER_DROP_ICONS,
    EDITOR_PICKER_DROP_NAMES,
    EDITOR_PICKER_DROP_DOTS,
    EDITOR_PICKER_DROP_LOC_COUNTS,
    EDITOR_PICKER_DROP_ZONE_LINES,
    EDITOR_PICKER_DROP_HATCH,
    EDITOR_PICKER_DROP_LABELS,
    EDITOR_PICKER_DROP_KINDS,
};

/** What a drop kind is called in the log line ("spawn dots"). */
const char*
Editor_PickerDropName(enum Editor_PickerDropKind kind);

/* ---- instance space ------------------------------------------------------------------
 *
 * The unmapped high-coordinate space the instance helper fills (the design's
 * section 13: "unmapped space 0..19 x 100..255"): TORIRSMAPED_INSTANCE_* in
 * torirsmaped/torirs_maped_catalog.h, the atlas rows in it flagged
 * TORIRSMAPED_SQUARE_INSTANCE. The picker draws it hatched where no square
 * exists, and its own page (EDITOR_PICKER_TAB_INSTANCE, "Instance space",
 * the Area dropdown's entry) frames all of it. */

/** What a band of the map is for: the Square map outlines each and names it,
 *  and the hover readout says it. */
enum Editor_PickerUse
{
    EDITOR_PICKER_USE_SURFACE = 0,
    /** Runtime copies of the templates (TORIRSMAPED_POOL_*): never built in. */
    EDITOR_PICKER_USE_POOL,
    /** Instance templates, instance space (TORIRSMAPED_INSTANCE_*). */
    EDITOR_PICKER_USE_TEMPLATE,
    /** Dungeons and caves, 100 squares north of the surface. */
    EDITOR_PICKER_USE_UNDERGROUND,
    EDITOR_PICKER_USE_COUNT
};

/** The band square (map_x, map_z) is in. Pure. */
enum Editor_PickerUse
Editor_PickerUseOf(
    int map_x,
    int map_z);

/** The band's name for a person: "instance pool (runtime copies)",
 *  "instance templates", "underground"; "" for the surface. */
const char*
Editor_PickerUseName(enum Editor_PickerUse use);

/** Whether (map_x, map_z) is in instance space. Pure. */
int
Editor_PickerInInstanceSpace(
    int map_x,
    int map_z);

/** The Instance space page's tab: there always is one once an atlas came
 *  (-1 before). */
int
Editor_PickerInstanceTab(const struct Editor_Picker* picker);

/** GRAPH keys: a square cell is 1 + its index; 0 is the background. */
#define EDITOR_PICKER_KEY_SQUARE(index) ((index) + 1)

/** TORIRSMAPED_SQUARE_* plus what only this window knows. */
enum
{
    /** In THIS window's world now (its view frame), loaded in its mirror. */
    EDITOR_PICKER_OPEN_HERE = 1 << 16,
    /** Unsaved in this window's mirror. */
    EDITOR_PICKER_DIRTY_HERE = 1 << 17,
};

/** What the world map puts in the well (editor/editor_worldmap.h). */
#define EDITOR_PICKER_MAP_IMAGES_MAX 192
#define EDITOR_PICKER_MAP_ICONS_MAX 900
#define EDITOR_PICKER_MAP_LABELS_MAX 96

/** One picture or icon: a scene sprite over a box of the well. */
struct Editor_PickerImage
{
    int scene_id;
    int x;
    int y;
    int w;
    int h;
    /** 0 opaque .. 255 invisible (a fade). */
    int trans;
    /** Pictures put under `scene_id` so far: a repaint of the same id is a
     *  new item to the GRAPH. */
    uint32_t version;
};

struct Editor_PickerLabel
{
    /** Left end and baseline, well px. */
    int x;
    int y;
    uint32_t color;
    char text[32];
};

struct Editor_PickerSquare
{
    int map_x;
    int map_z;
    /** Index into the model's storages. */
    int storage;
    uint32_t flags;
    int spawn_count;
    /** Locs the square's .jl2 places (the atlas row's loc_count). */
    int loc_count;
    uint32_t rgb;
    /** Index into the model's areas, -1 unmapped. */
    int primary_area;
    /** Into `area_refs`. */
    int area_first;
    int area_count;
};

struct Editor_PickerArea
{
    char internal_name[EDITOR_PICKER_NAME_MAX];
    char display_name[EDITOR_PICKER_NAME_MAX];
    int is_main;
};

/** Editor_PickerTab.area of the Full map page: every square of the grid
 *  where it really is, every world-map area's imagery composed onto it. */
#define EDITOR_PICKER_TAB_FULL (-2)
/** ...and of the Unmapped page. */
#define EDITOR_PICKER_TAB_UNMAPPED (-1)
/** ...and of the Instance space page: the whole instance rectangle, its
 *  squares where they are and hatching where none is. No world-map area. */
#define EDITOR_PICKER_TAB_INSTANCE (-3)

struct Editor_PickerTab
{
    /** Index into areas; EDITOR_PICKER_TAB_UNMAPPED, or EDITOR_PICKER_TAB_FULL. */
    int area;
    char title[EDITOR_PICKER_TITLE_MAX];
    int count;
    int min_x;
    int max_x;
    int min_z;
    int max_z;
};

/** The world map under the grid (editor/editor_worldmap.h). */
struct Editor_PickerUnderlay
{
    /** A map is shown: the cells are not filled (the map fills them) and
     *  the well under the grid is black where it has nothing. */
    int active;
    /** Its pictures, drawn first (stand-ins from another zoom, then this
     *  zoom's over them). */
    int count;
    struct Editor_PickerImage items[EDITOR_PICKER_MAP_IMAGES_MAX];
    /** Map element icons and area labels over them. */
    int icon_count;
    struct Editor_PickerImage icons[EDITOR_PICKER_MAP_ICONS_MAX];
    int label_count;
    struct Editor_PickerLabel labels[EDITOR_PICKER_MAP_LABELS_MAX];
};

struct Editor_Picker
{
    struct Editor_PickerSquare* squares;
    int square_count;
    int* area_refs;
    int area_ref_count;
    struct Editor_PickerArea* areas;
    int area_count;
    char (*storages)[EDITOR_DOC_STORAGE_MAX];
    int storage_count;

    struct Editor_PickerTab* tabs;
    /** Points into `tabs[].title`: what a chrome TABSTRIP borrows. */
    const char** tab_titles;
    int tab_count;
    int active_tab;

    /** Logical px per square: continuous, Editor_PickerZoomMin..Max. */
    double zoom;
    /** The map coordinates at the grid's top-left corner, in squares: x of
     *  the west edge, z of the NORTH edge. Fractional, so a drag pans by the
     *  pixel at any zoom. */
    double origin_x;
    double origin_z;
    /** The well's logical size, as last drawn. */
    int well_w;
    int well_h;

    /** The square under the pointer (-1), and the map cell (even empty). */
    int hover;
    int hover_valid;
    int hover_map_x;
    int hover_map_z;

    /** One byte per square. */
    unsigned char* selected;
    /** The last square clicked or searched (-1). */
    int focus;
    /** An empty cell clicked: where New would create (valid flag). */
    int empty_valid;
    int empty_map_x;
    int empty_map_z;
    /** A rectangle of squares marked for the instance helper (its Find free
     *  answer, workbench 6.5): [mark_x, mark_x + mark_w) x [mark_z,
     *  mark_z + mark_h); mark_w 0 = none. Drawn at every zoom. */
    int mark_x;
    int mark_z;
    int mark_w;
    int mark_h;
    /** The Square map's pick (edpanel_squares.c): the cells an Open squares
     *  drag sweeps, or the cell Place would put a scratch square in.
     *  [sweep_x, sweep_x + sweep_w) x [sweep_z, sweep_z + sweep_h); sweep_w
     *  0 = none. Drawn at every zoom, twice outlined: in the created green
     *  when `sweep_ok`, else in the unsaved accent (a cell Place refuses). */
    int sweep_x;
    int sweep_z;
    int sweep_w;
    int sweep_h;
    int sweep_ok;

    /** Storage filter: -1 every storage, else one index. */
    int storage_filter;

    /** What the last draw spent, and whether it ran out of budget. */
    int items_drawn;
    int truncated;
    /** What the budget dropped on the last draw, by Editor_PickerDropKind,
     *  and the line that said so ("" when nothing was). Logged once per
     *  change, not once per frame. */
    int dropped[EDITOR_PICKER_DROP_KINDS];
    char drop_text[192];
    /** The collection filter (Editor_PickerSetFilter): when on, only the
     *  squares whose keys are listed are shown -- on every Area page, the
     *  collection being the page -- and Fit frames them. `filter_mask` is
     *  one byte per square, rebuilt from the keys when the atlas changes. */
    int filter_on;
    char (*filter_keys)[EDITOR_DOC_KEY_MAX];
    int filter_key_count;
    unsigned char* filter_mask;
    /** Squares the filter shows. */
    int filter_shown;
    /** Bumped whenever which squares are shown may have changed (a new
     *  atlas, a filter): what a picture keyed on them compares. */
    uint32_t atlas_serial;
    /** Drawn under the grid when set; the cells then are not filled (the
     *  picture is what fills them) and only the outlines go on top. */
    struct Editor_PickerUnderlay underlay;
};

void
Editor_PickerInit(struct Editor_Picker* picker);

void
Editor_PickerFree(struct Editor_Picker* picker);

/**
 * Take the atlas: squares, areas, storages copied; tabs rebuilt -- the
 * surface first, then every area that claims a square by name, then
 * Unmapped. The active tab, the selection and the view survive when the
 * area set is the same (a refresh after a save or a DOC_CREATE).
 * Returns 1 when the TAB SET changed (a borrower must re-point at
 * `tab_titles`).
 */
int
Editor_PickerSetAtlas(
    struct Editor_Picker* picker,
    const struct EditorHost_CatalogSquares* atlas);

/** Index of square (map_x, map_z): in `storage` (-1 any, preferring maps/),
 *  or -1. */
int
Editor_PickerFind(
    const struct Editor_Picker* picker,
    int map_x,
    int map_z,
    int storage);

/** Index of storage `name` ("" and "maps" are the content tree's), or -1. */
int
Editor_PickerStorageIndex(
    const struct Editor_Picker* picker,
    const char* name);

/** Whether square `index` belongs on tab `tab`. */
int
Editor_PickerSquareOnTab(
    const struct Editor_Picker* picker,
    int index,
    int tab);

/** The tab a square is best shown on: its primary area's, else Unmapped. */
int
Editor_PickerTabOfSquare(
    const struct Editor_Picker* picker,
    int index);

/** Switch tab and fit its squares into the well. */
void
Editor_PickerSetTab(
    struct Editor_Picker* picker,
    int tab);

/** Fit the active tab into a well of (w, h). */
void
Editor_PickerFit(
    struct Editor_Picker* picker,
    int well_w,
    int well_h);

/** Centre the view on (map_x, map_z), keeping the zoom. */
void
Editor_PickerCentre(
    struct Editor_Picker* picker,
    int map_x,
    int map_z);

/** Zoom by wheel `delta` notches (negative in) about well point (x, y):
 *  EDITOR_PICKER_ZOOM_STEP a notch, clamped. */
void
Editor_PickerZoom(
    struct Editor_Picker* picker,
    int delta,
    int x,
    int y);

/** Zoom to `zoom` (clamped) keeping the map point under (x, y). */
void
Editor_PickerZoomTo(
    struct Editor_Picker* picker,
    double zoom,
    int x,
    int y);

/** The zoom range: the whole page in the well (or EDITOR_PICKER_ZOOM_OUT_TO
 *  for a small page) .. one square filling it (at least
 *  EDITOR_PICKER_ZOOM_CLOSEST). */
double
Editor_PickerZoomMin(const struct Editor_Picker* picker);
double
Editor_PickerZoomMax(const struct Editor_Picker* picker);

/** The global pixel (game/rs_worldmap_overview.h, at `zoom`) under well
 *  point (0, 0): a picture at global (gx, gy) is drawn at (gx - x, gy - y). */
void
Editor_PickerGlobalOrigin(
    const struct Editor_Picker* picker,
    int* out_x,
    int* out_y);

/** Labels every this many squares so that each has `min_px` of room. */
int
Editor_PickerLabelEvery(
    double zoom,
    int min_px);

/** Pan by (dx, dy) well pixels (a drag). */
void
Editor_PickerPan(
    struct Editor_Picker* picker,
    int dx,
    int dy);

/** Pan by (dx, dy) well pixels, fractions kept (a trackpad scroll). The
 *  content moves by the deltas, as under Editor_PickerPan. */
void
Editor_PickerPanBy(
    struct Editor_Picker* picker,
    double dx,
    double dy);

/** The map cell under well point (x, y). 0 when the point is off the grid. */
int
Editor_PickerCellAt(
    const struct Editor_Picker* picker,
    int x,
    int y,
    int* out_map_x,
    int* out_map_z);

/** Whether square `index` is shown on the active page (its tab, the
 *  storage filter, the collection filter). */
int
Editor_PickerSquareVisible(
    const struct Editor_Picker* picker,
    int index);

/** Square (map_x, map_z)'s cell as drawn, well px. */
void
Editor_PickerCellBox(
    const struct Editor_Picker* picker,
    int map_x,
    int map_z,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

/** The well point of square (map_x, map_z)'s top-left corner, as drawn. */
void
Editor_PickerCellOrigin(
    const struct Editor_Picker* picker,
    int map_x,
    int map_z,
    int* out_x,
    int* out_y);

/** The squares any part of which is in the well: [x0, x0+w) x [z0, z0+h),
 *  clamped at 0. 0 when none is (a well not sized yet). */
int
Editor_PickerVisibleSquares(
    const struct Editor_Picker* picker,
    int* out_x0,
    int* out_z0,
    int* out_w,
    int* out_h);

/** The square shown at well point (x, y) on the active tab, or -1. */
int
Editor_PickerSquareAt(
    const struct Editor_Picker* picker,
    int x,
    int y);

/** The square a GRAPH key names, or -1. */
int
Editor_PickerSquareOfKey(
    const struct Editor_Picker* picker,
    int key);

/** Track the pointer; returns 1 when the hover changed. */
int
Editor_PickerHover(
    struct Editor_Picker* picker,
    int x,
    int y);

/** Replace the selection with `index` (-1 clears), or toggle it in (`add`). */
void
Editor_PickerSelect(
    struct Editor_Picker* picker,
    int index,
    int add);

int
Editor_PickerSelectedCount(const struct Editor_Picker* picker);

/** "m50_50" or "ported/<lane>/maps/m50_50": square `index`'s full key. */
void
Editor_PickerSquareKey(
    const struct Editor_Picker* picker,
    int index,
    char out[EDITOR_DOC_KEY_MAX]);

/** Show only the squares whose full keys ("m50_50",
 *  "ported/<lane>/maps/m16_101") are in `keys` (`count` of them); a count
 *  below 0 turns the filter off. A key the atlas does not hold is kept and
 *  shown once a refresh brings it. Returns the squares shown. */
int
Editor_PickerSetFilter(
    struct Editor_Picker* picker,
    const char* const* keys,
    int count);

/** Fold this window's mirror into the flags: DIRTY_HERE for a loaded square
 *  with unsaved edits, OPEN_HERE for one inside `self_frame` (this window's
 *  world; NULL: none). */
void
Editor_PickerNoteDocument(
    struct Editor_Picker* picker,
    struct Editor_Doc* doc,
    const struct EditorUiViewFrame* self_frame);

enum Editor_PickerSearchKind
{
    EDITOR_PICKER_SEARCH_NONE = 0,
    /** "m50_50", "50 50", "50,50". */
    EDITOR_PICKER_SEARCH_SQUARE,
    /** An absolute tile: "3222 3218" (and an optional level). */
    EDITOR_PICKER_SEARCH_TILE,
    /** An area or region name. */
    EDITOR_PICKER_SEARCH_AREA,
};

struct Editor_PickerSearch
{
    enum Editor_PickerSearchKind kind;
    int map_x;
    int map_z;
    int tile_x;
    int tile_z;
    int level;
    /** The square found (-1 when the coordinates name an empty cell). */
    int square;
    /** The tab the result shows on. */
    int tab;
};

/** Parse a search. Pure: changes nothing. */
int
Editor_PickerSearch(
    const struct Editor_Picker* picker,
    const char* text,
    struct Editor_PickerSearch* out);

/** Show a search result: switch to its tab, centre on it, focus it (an
 *  empty cell becomes the New dialog's prefill). */
void
Editor_PickerShow(
    struct Editor_Picker* picker,
    const struct Editor_PickerSearch* result);

/** The colours the picker draws with. */
struct Editor_PickerColors
{
    uint32_t background;
    uint32_t grid;
    uint32_t empty;
    uint32_t text;
    uint32_t open;
    uint32_t dirty;
    uint32_t selected;
    uint32_t created;
    uint32_t frame;
    uint32_t self_frame;
    /** A lane's square: the inset. */
    uint32_t lane;
    /** Instance space where no square is: the hatching. */
    uint32_t hatch;
    /** The zone lines at close zooms, and the spawn dots. */
    uint32_t zone;
    uint32_t spawn;
    /** The outlines of what a band of the map is for (Editor_PickerUse):
     *  the runtime instance pool, the instance templates, underground. */
    uint32_t use_pool;
    uint32_t use_template;
    uint32_t use_underground;
};

void
Editor_PickerDefaultColors(struct Editor_PickerColors* colors);

/**
 * Restate the GRAPH well's picture at the zoom's level of detail
 * (Editor_PickerLodAt): the active tab's cells (thumbnail colour below 12 px
 * a square, else the map under them), a lane's inset, open squares outlined,
 * dirty ones in the accent, the selection, axis labels, instance space
 * hatched, and every view window in `frames` (`self_slot` drawn as this
 * window's). Never more than EDITOR_PICKER_ITEMS_BUDGET items: what the user
 * did and the cells are drawn first in the budget's account, decoration
 * takes what is left, and whatever was dropped is counted in `dropped` and
 * logged. Returns the items drawn.
 */
int
Editor_PickerDraw(
    struct Editor_Picker* picker,
    struct ToriRSChrome* chrome,
    int graph,
    int well_w,
    int well_h,
    const struct Editor_PickerColors* colors,
    const struct EditorUiViewFrame* frames,
    int frame_count,
    int self_slot);

#endif
