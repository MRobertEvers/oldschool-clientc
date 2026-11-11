#ifndef SRC_EDITOR_PANELS_EDPANEL_CATALOG_MODEL_H
#define SRC_EDITOR_PANELS_EDPANEL_CATALOG_MODEL_H

/**
 * The Catalog panel's model (docs/MAPED_DESIGN.html #catalog): the server's
 * index, asked a page at a time.
 *
 * The panel used to list what the client's CacheProvider happened to have
 * decoded ("431 of 431 Locs" at boot). The content tree is indexed on the
 * server (torirsmaped/torirs_maped_catalog.h, 405k records), so this asks
 * CATALOG_QUERY for one page of (kind, find text) and CATALOG_RECORD for the
 * picked row, and remembers both:
 *
 *   - A query runs only when what it is keyed on changed — the kind, the
 *     find text, the page, the page size — or the model was invalidated.
 *     Every other frame it answers from the page it holds. An answer with no
 *     rows (a miss) is held exactly like one with rows, so a find text that
 *     matches nothing is asked once, not once a frame.
 *   - A changed find text waits until it has stood for one frame boundary
 *     (EDITOR_CATALOG_SETTLE_FRAMES), so typing a word asks for the word,
 *     not for each prefix. A kind or page change asks at once.
 *   - The record of the picked row is fetched once per pick and held.
 *
 * App-free: it reaches the server only through the EditorHost it is given,
 * so editor_panels_test drives it against an embedded ToriRSMapEd.
 */

#include "editor/editor_host.h"

#include <stdint.h>

/** Frames a changed find text must stand before it is asked. */
#define EDITOR_CATALOG_SETTLE_FRAMES 1
/** Bytes of find text kept (the chrome's input is 192). */
#define EDITOR_CATALOG_FILTER_MAX 192

/* ==== the loc finder (workbench 6.2, design section 6 "Finding locs") ===========
 *
 * Everything below is pure: rows go in (a page the server answered, or one a
 * test feeds with Editor_CatalogModelFeed, or the panel's local list), text
 * and choices come out. The panel (edpanel_catalog.c) turns them into
 * widgets; nothing here touches the chrome or a host.
 *
 *   Chips     a typed `key=value` becomes a chip; a chip's x removes it; the
 *             Find text's free words and the chips compose into the query's
 *             filter text (Editor_CatalogChipsCompose). A shape chip names a
 *             class (wall / decor / centre / roof / ground) or a number.
 *   Window    the list is continuous: the rows on screen ask for the page
 *             they are on, and the next page is prefetched when the window
 *             nears its end (Editor_CatalogModelWantWindow); at most
 *             EDITOR_CATALOG_CACHE_PAGES pages besides the anchor are held.
 *   Sources   the index (the server), or a local list the panel hands in:
 *             favourites, the session's recent picks, the locs the open
 *             squares place. Find's free words filter a local list here.
 *   Sort      name, id, category, size: a local list always; the index's
 *             rows once every match is held (the server's order otherwise,
 *             and the count says so).
 *   Preview   which loc the Preview shows (Editor_CatalogPreviewFollow).
 */

#define EDITOR_CATALOG_CHIPS_MAX 8
#define EDITOR_CATALOG_CHIP_KEY_MAX 24
#define EDITOR_CATALOG_CHIP_VALUE_MAX 64
/** Pages held besides the anchor: the prefetched next page and the ones a
 *  scroll back finds again. */
#define EDITOR_CATALOG_CACHE_PAGES 4

/** While the server's warm build runs, an answer comes from its partial
 *  index (TORIRSMAPED_CATALOG_PAGE_PARTIAL, workbench 6.2 server half); the
 *  question in view is asked again this often until a whole answer lands. */
#define EDITOR_CATALOG_PARTIAL_REASK_MS 250.0
/** The recent picks a session keeps. */
#define EDITOR_CATALOG_RECENTS_MAX 20
/** Shape keycaps a row shows at most (TORIRS_CHROME_CELL_KEYS_MAX). */
#define EDITOR_CATALOG_KEYCAPS_MAX 8
/** Rows a local list (favourites, in square) holds at most. */
#define EDITOR_CATALOG_LOCAL_MAX 512
/** Bytes of a local row's name / display / category, terminator included. */
#define EDITOR_CATALOG_LOCAL_NAME_MAX 64

struct Editor_CatalogChip
{
    char key[EDITOR_CATALOG_CHIP_KEY_MAX];
    char value[EDITOR_CATALOG_CHIP_VALUE_MAX];
};

struct Editor_CatalogChips
{
    struct Editor_CatalogChip chip[EDITOR_CATALOG_CHIPS_MAX];
    int count;
};

enum Editor_CatalogSource
{
    /** The server's index (CATALOG_QUERY). */
    EDITOR_CATALOG_SOURCE_INDEX = 0,
    /** editor/favourites.txt (editor_favourites.h). */
    EDITOR_CATALOG_SOURCE_FAVOURITES,
    /** The last EDITOR_CATALOG_RECENTS_MAX picks of this session. */
    EDITOR_CATALOG_SOURCE_RECENT,
    /** The locs the open squares place (the mirror's square documents). */
    EDITOR_CATALOG_SOURCE_IN_SQUARE,
    EDITOR_CATALOG_SOURCE_COUNT,
};

enum Editor_CatalogSort
{
    /** The server's order: the whole text first, then a name that starts
     *  with the first word, then the rest, by kind and id. */
    EDITOR_CATALOG_SORT_BEST = 0,
    EDITOR_CATALOG_SORT_NAME,
    EDITOR_CATALOG_SORT_ID,
    EDITOR_CATALOG_SORT_CATEGORY,
    EDITOR_CATALOG_SORT_SIZE,
    EDITOR_CATALOG_SORT_COUNT,
};

/** One row of a local list: what the panel knows of it. */
struct Editor_CatalogLocalRow
{
    uint32_t kind;
    /** -1 while only the name is known (a favourite not yet asked). */
    int32_t id;
    char name[EDITOR_CATALOG_LOCAL_NAME_MAX];
    char display[EDITOR_CATALOG_LOCAL_NAME_MAX];
    char category[EDITOR_CATALOG_LOCAL_NAME_MAX];
    /** Placeable shapes (bit s), 0 unknown. */
    uint32_t shape_mask;
    /** Footprint in tiles, 0 unknown. */
    int size_w;
    int size_l;
    /** A remembered pose (a favourite's `shape S rot R`, a recent pick's),
     *  shape -1 for none. */
    int shape;
    int rotation;
};

/** One row of the list, whichever source it came from. Pointers live until
 *  the model next changes (a Refresh that asked, a Want, a SetLocal). */
struct Editor_CatalogView
{
    uint32_t kind;
    int32_t id;
    const char* name;
    /** "" when there is none. */
    const char* display;
    const char* category;
    /** A loc's `layer` field from the server ("wall main"); "" otherwise. */
    const char* layer;
    uint32_t shape_mask;
    int size_w;
    int size_l;
    int shape;
    int rotation;
    /** An obj's primary wear position (`wearpos`), -1 none; 1 when it
     *  stacks (`stackable=yes`). */
    int wearpos;
    int stackable;
    /** An npc's combat level (`vislevel`, -1 none) and footprint (`size`,
     *  1 when absent). */
    int level;
    int npc_size;
};


/** One page held besides the anchor (Editor_CatalogModelWantWindow). */
struct Editor_CatalogCachedPage
{
    int held;
    uint32_t page;
    enum EditorHost_Status status;
    struct EditorHost_CatalogPage result;
};

struct Editor_CatalogModel
{
    /* What is wanted. */
    uint32_t kind;
    char filter[EDITOR_CATALOG_FILTER_MAX];
    uint32_t page;
    uint32_t page_size;
    /** Frames left before a changed filter is asked. */
    int settle;

    /* What `result` answers; `held` 0 until the first answer (or after an
     * invalidation). */
    int held;
    uint32_t held_kind;
    char held_filter[EDITOR_CATALOG_FILTER_MAX];
    uint32_t held_page;
    uint32_t held_page_size;
    enum EditorHost_Status status;
    struct EditorHost_CatalogPage result;
    /** The anchor answer was the warm build's partial index
     *  (result.page_flags & TORIRSMAPED_CATALOG_PAGE_PARTIAL): when it was
     *  asked, and a re-ask in flight replacing every held page without
     *  counting as a new question. */
    double partial_asked_ms;
    int reasking;

    /* The picked row's record: which row it is, and the answer. */
    int record_row;
    int record_held;
    enum EditorHost_Status record_status;
    struct EditorHost_CatalogRecord record;

    /* The continuous list (6.2): pages of the held question besides the
     * anchor `result`, the page prefetched after the window, and the serials
     * the panel redraws by. */
    struct Editor_CatalogCachedPage cache[EDITOR_CATALOG_CACHE_PAGES];
    /** The window the list shows (Editor_CatalogModelWantWindow). */
    int window_first;
    int window_rows;
    /** Bumped whenever the rows the list can show changed (an answer, a
     *  page swapped in from the cache, a local list, a sort). */
    uint32_t answers;
    /** Bumped when an answer to a NEW question lands (kind, filter, source):
     *  the panel scrolls to the top and drops the selection then, and only
     *  then -- a page that lands as the window moves is the same list. */
    uint32_t key_serial;

    /* Where the rows come from, and their order. */
    enum Editor_CatalogSource source;
    enum Editor_CatalogSort sort;
    /** The local list (Editor_CatalogModelSetLocal), owned, and the rows of
     *  it Find and the shape chips let through, in sort order. */
    struct Editor_CatalogLocalRow* local;
    int local_count;
    int* local_order;
    int local_shown;
    /** The filter keys a local row cannot answer (`op`, `multivar`, ...;
     *  space-separated, "" when every chip applies): they filter nothing in
     *  a local list, and the count row says so. */
    char local_unchecked[64];
    /** The index's held rows in sort order, once every match is held:
     *  absolute row -> held row; `order_count` 0 when the sort does not
     *  apply. Rebuilt when `answers` moves past `order_answers`. */
    int* order;
    int order_count;
    uint32_t order_answers;

    /** CATALOG_QUERY / CATALOG_RECORD round trips made; the test reads
     *  them to prove an unchanged frame asks nothing. `prefetches` are
     *  the queries for pages other than the anchor. */
    int queries;
    int prefetches;
    int record_fetches;
    /** Wall time of the last CATALOG_QUERY / CATALOG_RECORD round trip
     *  (TORIRS_CATALOG_STATS=1 prints each one). */
    double last_query_ms;
    double last_record_ms;
};

/** Wall clock in ms (the stats lines and the panel's frame meter). */
double
Editor_CatalogModelNowMs(void);

/** 1 when TORIRS_CATALOG_STATS is set: one line per round trip, one per
 *  frame the catalog drives. */
int
Editor_CatalogModelStats(void);

void
Editor_CatalogModelInit(
    struct Editor_CatalogModel* model,
    uint32_t kind,
    uint32_t page_size);

/** Frees the held answers. NULL-tolerant. */
void
Editor_CatalogModelFree(struct Editor_CatalogModel* model);

/** Want `kind` and `filter`. A kind change goes back to page 0 and asks at
 *  once; a filter change goes back to page 0 and settles first. Returns 1
 *  when anything changed. */
int
Editor_CatalogModelWant(
    struct Editor_CatalogModel* model,
    uint32_t kind,
    const char* filter);

/** Want page `page` (clamped to the last page of the held total). */
void
Editor_CatalogModelSetPage(
    struct Editor_CatalogModel* model,
    uint32_t page);

/** Pages the held total spans (at least 1). */
uint32_t
Editor_CatalogModelPageCount(const struct Editor_CatalogModel* model);

/** Drop the held page and record: the next Refresh asks again (the tree
 *  changed under it — a copy, a save). */
void
Editor_CatalogModelInvalidate(struct Editor_CatalogModel* model);

/**
 * Once per frame: ask the server when what is wanted is not what is held
 * and the filter has settled -- the window's page, else one prefetch
 * (Editor_CatalogModelNextPage), at most one round trip. Returns 1 when the
 * held rows changed (a query ran, or the window moved onto a cached page,
 * which needs none), 0 when what is held still answers. A local list asks
 * nothing.
 */
int
Editor_CatalogModelRefresh(
    struct Editor_CatalogModel* model,
    struct EditorHost* host);

/** 1 while the held answer came from the server's partial index (its warm
 *  build is still running): the rows are what is merged so far, the count
 *  text says how far the build is, and Refresh asks again every
 *  EDITOR_CATALOG_PARTIAL_REASK_MS until a whole answer lands. */
int
Editor_CatalogModelPartial(const struct Editor_CatalogModel* model);

/** The held page's rows (0 when nothing is held). */
int
Editor_CatalogModelRowCount(const struct Editor_CatalogModel* model);

const struct EditorHost_CatalogRow*
Editor_CatalogModelRow(
    const struct Editor_CatalogModel* model,
    int row);

/** "<shown> of <total matching> <kind>", from the server's totals. */
void
Editor_CatalogModelCountText(
    const struct Editor_CatalogModel* model,
    char* out,
    int out_size);

/** One result row as the list shows it: "name  (id)  display". */
void
Editor_CatalogModelRowText(
    const struct Editor_CatalogModel* model,
    int row,
    char* out,
    int out_size);

/**
 * The record of held row `row`, fetched once and held until another row is
 * asked for or the model is invalidated. NULL when the server has no such
 * record (that answer is held too).
 */
const struct EditorHost_CatalogRecord*
Editor_CatalogModelRecord(
    struct Editor_CatalogModel* model,
    struct EditorHost* host,
    int row);

/** As Editor_CatalogModelRecord, for absolute row `row` of the list
 *  (Editor_CatalogModelViewAt) rather than of the anchor page: what the
 *  continuous list asks. NULL when the row is not held or the server has no
 *  such record. */
const struct EditorHost_CatalogRecord*
Editor_CatalogModelRecordAt(
    struct Editor_CatalogModel* model,
    struct EditorHost* host,
    int row);

/** The kinds the panel's dropdown offers, in its order; the last is
 *  TORIRSMAPED_CATALOG_ANY ("all kinds"). */
int
Editor_CatalogModelKindCount(void);

uint32_t
Editor_CatalogModelKindAt(int index);

/** The dropdown index of `kind`, or 0. */
int
Editor_CatalogModelKindIndex(uint32_t kind);

/** The dropdown labels, `Editor_CatalogModelKindCount()` of them. */
const char* const*
Editor_CatalogModelKindLabels(void);

/* ---- chips ---------------------------------------------------------------- */

void
Editor_CatalogChipsInit(struct Editor_CatalogChips* chips);

/**
 * Every `key=value` word of `typed` becomes a chip (a key of [a-z0-9_], a
 * value of no space; one already there is not added twice; past
 * EDITOR_CATALOG_CHIPS_MAX the word stays text). The other words go to
 * `out_rest`, single-spaced. Returns the chips added.
 */
int
Editor_CatalogChipsTake(
    struct Editor_CatalogChips* chips,
    const char* typed,
    char* out_rest,
    int out_size);

/** Add one chip: 1 added, 0 when it is there already, full, or not a key
 *  and value the grammar takes. */
int
Editor_CatalogChipsAdd(
    struct Editor_CatalogChips* chips,
    const char* key,
    const char* value);

void
Editor_CatalogChipsRemove(
    struct Editor_CatalogChips* chips,
    int index);

/**
 * The query's filter text: `free_text`'s words, then each chip as the
 * server's filter grammar spells it. A shape NUMBER chip is `shape=N,*` (the
 * cache's `shapeK=N,model` lines, a prefix); a shape CLASS chip
 * (`shape=wall`), `size`, `spawn` pass through as typed, for the server's
 * summary fields to answer; every other chip is `key=value`.
 */
void
Editor_CatalogChipsCompose(
    const struct Editor_CatalogChips* chips,
    const char* free_text,
    char* out,
    int out_size);

/**
 * A `<kind>:` prefix on the Find text (`obj:whip`, `npc:hans`, `all:`): 1
 * with the kind and the text after the colon (leading spaces dropped), 0
 * when the text starts with no kind name the catalog knows.
 */
int
Editor_CatalogTakeKindPrefix(
    const char* text,
    uint32_t* out_kind,
    char* out_rest,
    int out_size);

/** The worn slot's name as wornitems.if lays it out ("weapon" for 3),
 *  "" for a slot no obj is worn in; and the slot a name (or a number)
 *  names, -1 for none. */
const char*
Editor_CatalogWearName(int wearpos);

int
Editor_CatalogWearSlot(const char* name);

/** What a chip shows: "shape: wall", "op: Open". */
void
Editor_CatalogChipLabel(
    const struct Editor_CatalogChip* chip,
    char* out,
    int out_size);

/** Its tooltip: a shape class lists its numbers ("shape wall: 0 1 2 3 9"). */
void
Editor_CatalogChipTip(
    const struct Editor_CatalogChip* chip,
    char* out,
    int out_size);

/** The shapes of class `name` (wall, decor, centre, roof, ground) or of one
 *  number "0".."22": 1 and the mask, else 0. */
int
Editor_CatalogShapeClass(
    const char* name,
    uint32_t* out_mask);

/** The class a shape is in: "wall", "decor", "centre", "roof", "ground". */
const char*
Editor_CatalogShapeClassOf(int shape);

/** The "+ filter" dropdown: its first row is the title ("+ filter"), the
 *  rest the summary fields. The loc menu (the Catalog's default). */
int
Editor_CatalogFilterMenuCount(void);

const char* const*
Editor_CatalogFilterMenuLabels(void);

/**
 * Row `index` of that menu: 1 and a whole chip (`shape=wall`,
 * `dialogue=yes`, `multivar=*`), 2 and a key the user completes
 * (`category`, `op`: the panel puts "key=" in Find), 0 for the title row.
 */
int
Editor_CatalogFilterMenuItem(
    int index,
    char out_key[EDITOR_CATALOG_CHIP_KEY_MAX],
    char out_value[EDITOR_CATALOG_CHIP_VALUE_MAX]);

/** The same menu for `kind`: locs get the shape, size and multivar rows;
 *  objs the wear slots, stackable, tradeable, members; npcs level, size,
 *  dialogue, spawn; every other kind category alone. */
int
Editor_CatalogFilterMenuCountFor(uint32_t kind);

const char* const*
Editor_CatalogFilterMenuLabelsFor(uint32_t kind);

int
Editor_CatalogFilterMenuItemFor(
    uint32_t kind,
    int index,
    char out_key[EDITOR_CATALOG_CHIP_KEY_MAX],
    char out_value[EDITOR_CATALOG_CHIP_VALUE_MAX]);

/* ---- a row's summary fields --------------------------------------------------- */

/** The value of summary field `key` (an exact key), NULL when the row states
 *  none. */
const char*
Editor_CatalogRowField(
    const struct EditorHost_CatalogRow* row,
    const char* key);

/**
 * A loc row's placeable shapes, from the summary fields CATALOG_QUERY
 * already returns: every `shapeK=N,<model>` line states shape N; a loc with
 * `models=` and no shape line is a centrepiece (shape 10, as the client's
 * loader reads it). Widened as Editor_LocShapeMask widens a config (wall
 * decor 4 places as 4..8, a centrepiece as 10 and 11). 0 for any other kind,
 * or a loc that states no model at all.
 */
uint32_t
Editor_CatalogRowShapeMask(const struct EditorHost_CatalogRow* row);

/** Footprint from `width=` / `length=` (1 each when absent). */
void
Editor_CatalogRowSize(
    const struct EditorHost_CatalogRow* row,
    int* out_w,
    int* out_l);

/* ---- the list: window, sources, sort ------------------------------------------------ */

/**
 * The rows the list shows are [first, first + rows): want the page `first`
 * is on (the anchor), and prefetch the page after the window when the
 * window comes within a quarter page of the anchor's end. Refresh asks the
 * anchor first, then one prefetch, at most one round trip a call.
 */
void
Editor_CatalogModelWantWindow(
    struct Editor_CatalogModel* model,
    int first,
    int rows);

/** The page Refresh asks next: the window's when it is not held, else the
 *  next one a sort or the prefetch wants; -1 when nothing is due (or the
 *  source is a local list). Pure: what a test checks before feeding it. */
int
Editor_CatalogModelNextPage(const struct Editor_CatalogModel* model);

/** Rows the list has: the server's total for the index, the filtered count
 *  for a local list. */
int
Editor_CatalogModelViewCount(const struct Editor_CatalogModel* model);

/** Row `row` of the list (absolute): 1 and `out`, or 0 when its page is not
 *  held yet (the list draws it blank and the window asks for it). */
int
Editor_CatalogModelViewAt(
    const struct Editor_CatalogModel* model,
    int row,
    struct Editor_CatalogView* out);

/** The absolute row of (kind, name) among the rows held, -1 when none
 *  is. */
int
Editor_CatalogModelFindRow(
    const struct Editor_CatalogModel* model,
    uint32_t kind,
    const char* name);

/** Show the index, or a local list (`rows`, copied; filtered by Find's free
 *  words and the shape chips, and sorted, here). */
void
Editor_CatalogModelSetSource(
    struct Editor_CatalogModel* model,
    enum Editor_CatalogSource source);

void
Editor_CatalogModelSetLocal(
    struct Editor_CatalogModel* model,
    const struct Editor_CatalogLocalRow* rows,
    int count);

void
Editor_CatalogModelSetSort(
    struct Editor_CatalogModel* model,
    enum Editor_CatalogSort sort);

/** 1 when the list shows the chosen sort: always for best match and for a
 *  local list; for the index, once every match is held (Refresh fetches
 *  the missing pages while there are few enough, EDITOR_CATALOG_CACHE_PAGES
 *  + 1). */
int
Editor_CatalogModelSortApplies(const struct Editor_CatalogModel* model);

/** The dropdown labels: "best match", "name", "id", "category", "size". */
const char* const*
Editor_CatalogSortLabels(void);

/**
 * The count the search box shows: "1 to 100 of 212" -- the first row of the
 * window to the last held row after it, of the total -- plus the kind, and
 * what the sort is waiting for. Before the first answer it says so; over
 * the server's partial index it says "so far" and how much is read, and a
 * scoped total (TORIRSMAPED_CATALOG_PAGE_SCOPED) reads "212+".
 */
void
Editor_CatalogModelRangeText(
    const struct Editor_CatalogModel* model,
    int first,
    char* out,
    int out_size);

/**
 * The count a window of `shown` rows from `first` shows: "37 to 72 of
 * 63,334 loc" -- what is on screen, of everything the list scrolls
 * through. The Gallery's line: it read "100 of 63334 loc", the page the
 * model happened to hold, which said a hundred was all there was. The
 * states with nothing to count read as Editor_CatalogModelRangeText's.
 */
void
Editor_CatalogModelWindowText(
    const struct Editor_CatalogModel* model,
    int first,
    int shown,
    char* out,
    int out_size);

/**
 * Feed an answer the way a CATALOG_QUERY round trip would (the model is
 * pure: the test gives it rows): page `page` of the question the model now
 * wants, `total` matches, `rows` copied. Held exactly as an asked answer is.
 */
void
Editor_CatalogModelFeed(
    struct Editor_CatalogModel* model,
    uint32_t page,
    uint32_t total,
    const struct EditorHost_CatalogRow* rows,
    int row_count);

/**
 * A row as the cells list shows it (ToriRSChrome_VListCells): the picture's
 * scene sprite id (0 none), the name (kind first when `any_kind`), the
 * display name, the placeable shapes as keycaps, the id.
 */
void
Editor_CatalogCellsText(
    const struct Editor_CatalogView* view,
    int sprite_id,
    int any_kind,
    char* out,
    int out_size);

/** The header row in the same format: "name", "shapes", "id". */
const char*
Editor_CatalogCellsHeader(void);

/** The header for `kind`'s rows: the keycap column reads `shapes` for locs,
 *  `wear` for objs, `level` for npcs, nothing for the rest. */
const char*
Editor_CatalogCellsHeaderFor(uint32_t kind);

/**
 * The filter an obj row's keycap `cell` (1-based) narrows to: 1 with the
 * chip (`wear=weapon`, `stackable=yes`), 0 for the row body, a loc's shape
 * cap or a cell past the caps.
 */
int
Editor_CatalogViewKeyChip(
    const struct Editor_CatalogView* view,
    int cell,
    char out_key[EDITOR_CATALOG_CHIP_KEY_MAX],
    char out_value[EDITOR_CATALOG_CHIP_VALUE_MAX]);

/** The shape keycap `cell` (1-based, as ToriRSChrome_VListPickedCell says)
 *  of a row stands for, -1 when the cell is the row body or past them. */
int
Editor_CatalogViewShapeAt(
    const struct Editor_CatalogView* view,
    int cell);

/* ---- recent picks ---------------------------------------------------------------- */

struct Editor_CatalogRecents
{
    struct Editor_CatalogLocalRow rows[EDITOR_CATALOG_RECENTS_MAX];
    int count;
    /** Bumped on every push (the panel rebuilds a shown recent list). */
    uint32_t serial;
};

/** `row` becomes the most recent; a pick already there moves to the front
 *  (its pose updated), the oldest past EDITOR_CATALOG_RECENTS_MAX goes. */
void
Editor_CatalogRecentsPush(
    struct Editor_CatalogRecents* recents,
    const struct Editor_CatalogLocalRow* row);

/* ---- which loc the Preview shows ------------------------------------------------------ */

struct Editor_CatalogPreviewInput
{
    /** The armed tool places (Place, a stamp): the pick is the subject. */
    int placing;
    /** The catalog's pick (kind 0 loc, 1 npc, 2 obj, -1 none) and the
     *  multiloc rung the Shell editor (the Catalog's variant dropdown)
     *  chose for it: -1 none chosen, -2 a value with nothing at it. */
    int pick_kind;
    int pick_id;
    int rung_id;
    /** The selection: how many locs, and the primary's loc id. */
    int selection_count;
    int selection_loc_id;
};

/**
 * The Preview's subject: the pick while placing; the primary selected loc
 * while selecting (whatever was picked before -- the old fallback looked at
 * the selection only while nothing had ever been picked, so after the first
 * pick it never followed a selection again); with no selection the pick.
 * A pick's chosen rung stands in for it. Returns 1 and the kind / id, or 0.
 */
int
Editor_CatalogPreviewFollow(
    const struct Editor_CatalogPreviewInput* input,
    int* out_kind,
    int* out_id);

#endif
