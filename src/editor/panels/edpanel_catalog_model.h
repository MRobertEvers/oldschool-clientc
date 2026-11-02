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

    /* The picked row's record: which row it is, and the answer. */
    int record_row;
    int record_held;
    enum EditorHost_Status record_status;
    struct EditorHost_CatalogRecord record;

    /** CATALOG_QUERY / CATALOG_RECORD round trips made; the test reads
     *  them to prove an unchanged frame asks nothing. */
    int queries;
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
 * and the filter has settled. Returns 1 when a query ran (the panel then
 * redraws its list), 0 when the held page still answers.
 */
int
Editor_CatalogModelRefresh(
    struct Editor_CatalogModel* model,
    struct EditorHost* host);

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

#endif
