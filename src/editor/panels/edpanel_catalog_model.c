/*
 * The Catalog panel's model: one held CATALOG_QUERY page and one held
 * CATALOG_RECORD, asked again only when what they are keyed on changes
 * (edpanel_catalog_model.h).
 */

#include "edpanel_catalog_model.h"

#include "edpanel_ops.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include "log/torirs_log.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

double
Editor_CatalogModelNowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int
Editor_CatalogModelStats(void)
{
    static int read = 0;
    static int on = 0;
    if( !read )
    {
        const char* value = getenv("TORIRS_CATALOG_STATS");
        on = value && value[0] && value[0] != '0';
        read = 1;
    }
    return on;
}

/* The dropdown: the kinds people place and edit first, then the rest of the
 * index, then every kind at once. */
static const uint32_t k_kinds[] = {
    TORIRSMAPED_CATALOG_LOC,        TORIRSMAPED_CATALOG_NPC,       TORIRSMAPED_CATALOG_OBJ,
    TORIRSMAPED_CATALOG_SEQ,        TORIRSMAPED_CATALOG_SPOTANIM,  TORIRSMAPED_CATALOG_VARP,
    TORIRSMAPED_CATALOG_VARBIT,     TORIRSMAPED_CATALOG_VARC,      TORIRSMAPED_CATALOG_VARN,
    TORIRSMAPED_CATALOG_VARS,       TORIRSMAPED_CATALOG_ENUM,      TORIRSMAPED_CATALOG_STRUCT,
    TORIRSMAPED_CATALOG_PARAM,      TORIRSMAPED_CATALOG_INV,       TORIRSMAPED_CATALOG_DBROW,
    TORIRSMAPED_CATALOG_DBTABLE,    TORIRSMAPED_CATALOG_INTERFACE, TORIRSMAPED_CATALOG_COMPONENT,
    TORIRSMAPED_CATALOG_CS2,        TORIRSMAPED_CATALOG_TRIGGER,   TORIRSMAPED_CATALOG_SQUARE,
    TORIRSMAPED_CATALOG_MODEL,      TORIRSMAPED_CATALOG_SPRITE,    TORIRSMAPED_CATALOG_TEXTURE,
    TORIRSMAPED_CATALOG_SYNTH,      TORIRSMAPED_CATALOG_SONG,      TORIRSMAPED_CATALOG_JINGLE,
    TORIRSMAPED_CATALOG_FRAMESET,   TORIRSMAPED_CATALOG_SKELETON,  TORIRSMAPED_CATALOG_FONT,
    TORIRSMAPED_CATALOG_CATEGORY,   TORIRSMAPED_CATALOG_STAT,      TORIRSMAPED_CATALOG_PACK,
    TORIRSMAPED_CATALOG_IDK,        TORIRSMAPED_CATALOG_UNDERLAY,  TORIRSMAPED_CATALOG_OVERLAY,
    TORIRSMAPED_CATALOG_HITSPLAT,   TORIRSMAPED_CATALOG_HEALTHBAR, TORIRSMAPED_CATALOG_MAPELEMENT,
    TORIRSMAPED_CATALOG_ANY,
};
#define CATALOG_MODEL_KINDS ((int)(sizeof(k_kinds) / sizeof(k_kinds[0])))

_Static_assert(CATALOG_MODEL_KINDS == TORIRSMAPED_CATALOG_KIND_COUNT + 1,
    "the dropdown offers every catalog kind, plus all of them");

static const char* g_kind_labels[CATALOG_MODEL_KINDS];

int
Editor_CatalogModelKindCount(void)
{
    return CATALOG_MODEL_KINDS;
}

uint32_t
Editor_CatalogModelKindAt(int index)
{
    assert(index >= 0);
    assert(index < CATALOG_MODEL_KINDS);
    return k_kinds[index];
}

int
Editor_CatalogModelKindIndex(uint32_t kind)
{
    for( int i = 0; i < CATALOG_MODEL_KINDS; i++ )
        if( k_kinds[i] == kind )
            return i;
    return 0;
}

const char* const*
Editor_CatalogModelKindLabels(void)
{
    if( !g_kind_labels[0] )
        for( int i = 0; i < CATALOG_MODEL_KINDS; i++ )
            g_kind_labels[i] = k_kinds[i] == TORIRSMAPED_CATALOG_ANY ? "all kinds"
                                                                     : ToriRSMapEdCatalog_KindName(k_kinds[i]);
    return g_kind_labels;
}

void
Editor_CatalogModelInit(
    struct Editor_CatalogModel* model,
    uint32_t kind,
    uint32_t page_size)
{
    assert(model);
    assert(page_size > 0);
    assert(page_size <= TORIRSMAPED_CATALOG_PAGE_MAX);

    memset(model, 0, sizeof(*model));
    model->kind = kind;
    model->page_size = page_size;
    model->record_row = -1;
}

static void
model_local_rebuild(struct Editor_CatalogModel* model);

static void
drop_record(struct Editor_CatalogModel* model)
{
    if( model->record_held )
        Editor_HostCatalogRecordFree(&model->record);
    memset(&model->record, 0, sizeof(model->record));
    model->record_held = 0;
    model->record_row = -1;
}

static void
drop_cache(struct Editor_CatalogModel* model)
{
    for( int i = 0; i < EDITOR_CATALOG_CACHE_PAGES; i++ )
    {
        if( model->cache[i].held )
            Editor_HostCatalogPageFree(&model->cache[i].result);
        memset(&model->cache[i], 0, sizeof(model->cache[i]));
    }
    model->order_count = 0;
}

void
Editor_CatalogModelFree(struct Editor_CatalogModel* model)
{
    if( !model )
        return;
    if( model->held )
        Editor_HostCatalogPageFree(&model->result);
    memset(&model->result, 0, sizeof(model->result));
    model->held = 0;
    drop_record(model);
    drop_cache(model);
    ToriMem_Free(ToriMem_Editor(), model->local);
    ToriMem_Free(ToriMem_Editor(), model->local_order);
    ToriMem_Free(ToriMem_Editor(), model->order);
    model->local = NULL;
    model->local_order = NULL;
    model->order = NULL;
    model->local_count = 0;
    model->local_shown = 0;
}

int
Editor_CatalogModelWant(
    struct Editor_CatalogModel* model,
    uint32_t kind,
    const char* filter)
{
    int changed = 0;

    assert(model);
    assert(filter);

    if( kind != model->kind )
    {
        model->kind = kind;
        model->page = 0;
        model->settle = 0;
        changed = 1;
    }
    if( strncmp(filter, model->filter, sizeof(model->filter) - 1) != 0 )
    {
        snprintf(model->filter, sizeof(model->filter), "%s", filter);
        model->page = 0;
        /* A kind change in the same call asks at once anyway. */
        if( !changed )
            model->settle = EDITOR_CATALOG_SETTLE_FRAMES;
        changed = 1;
        /* A local list is filtered here, at once: nothing to ask. */
        if( model->source != EDITOR_CATALOG_SOURCE_INDEX )
        {
            model_local_rebuild(model);
            model->key_serial++;
        }
    }
    return changed;
}

uint32_t
Editor_CatalogModelPageCount(const struct Editor_CatalogModel* model)
{
    assert(model);
    if( !model->held || model->result.total == 0 )
        return 1;
    return (model->result.total + model->page_size - 1) / model->page_size;
}

void
Editor_CatalogModelSetPage(
    struct Editor_CatalogModel* model,
    uint32_t page)
{
    uint32_t const last = Editor_CatalogModelPageCount(model) - 1;

    assert(model);
    model->page = page > last ? last : page;
}

void
Editor_CatalogModelInvalidate(struct Editor_CatalogModel* model)
{
    assert(model);
    if( model->held )
        Editor_HostCatalogPageFree(&model->result);
    memset(&model->result, 0, sizeof(model->result));
    model->held = 0;
    drop_record(model);
    drop_cache(model);
    model->answers++;
}

/** The model holds an answer to the question it now wants (any page). */
static int
model_question_held(const struct Editor_CatalogModel* model)
{
    return model->held && model->held_kind == model->kind && model->held_page_size == model->page_size
           && strcmp(model->held_filter, model->filter) == 0;
}

/** Page `page` of the held question is held (anchor or cache, any status). */
static int
model_page_held(
    const struct Editor_CatalogModel* model,
    uint32_t page)
{
    if( !model->held )
        return 0;
    if( model->held_page == page )
        return 1;
    for( int i = 0; i < EDITOR_CATALOG_CACHE_PAGES; i++ )
        if( model->cache[i].held && model->cache[i].page == page )
            return 1;
    return 0;
}

/** The answered page `page`, NULL when it is not held or was refused. */
static const struct EditorHost_CatalogPage*
model_page(
    const struct Editor_CatalogModel* model,
    uint32_t page)
{
    if( !model->held )
        return NULL;
    if( model->held_page == page )
        return model->status == EDITOR_HOST_OK ? &model->result : NULL;
    for( int i = 0; i < EDITOR_CATALOG_CACHE_PAGES; i++ )
        if( model->cache[i].held && model->cache[i].page == page )
            return model->cache[i].status == EDITOR_HOST_OK ? &model->cache[i].result : NULL;
    return NULL;
}

/** Into the cache: the slot already holding that page, a free one, or the
 *  one farthest from the anchor. Takes ownership of `page`. */
static void
model_cache_put(
    struct Editor_CatalogModel* model,
    uint32_t page_index,
    struct EditorHost_CatalogPage* page,
    enum EditorHost_Status status)
{
    int slot = -1;
    uint32_t far = 0;

    for( int i = 0; i < EDITOR_CATALOG_CACHE_PAGES && slot < 0; i++ )
        if( model->cache[i].held && model->cache[i].page == page_index )
            slot = i;
    for( int i = 0; i < EDITOR_CATALOG_CACHE_PAGES && slot < 0; i++ )
        if( !model->cache[i].held )
            slot = i;
    if( slot < 0 )
        for( int i = 0; i < EDITOR_CATALOG_CACHE_PAGES; i++ )
        {
            uint32_t const p = model->cache[i].page;
            uint32_t const d = p > model->page ? p - model->page : model->page - p;
            if( slot < 0 || d > far )
            {
                slot = i;
                far = d;
            }
        }
    if( model->cache[slot].held )
        Editor_HostCatalogPageFree(&model->cache[slot].result);
    model->cache[slot].held = 1;
    model->cache[slot].page = page_index;
    model->cache[slot].status = status;
    model->cache[slot].result = *page;
    memset(page, 0, sizeof(*page));
}

static void
model_order_rebuild(struct Editor_CatalogModel* model);

/**
 * An answer: to a new question it replaces everything held (and bumps
 * `key_serial`); to the held one it is the anchor when it is the page the
 * window starts on (the old anchor goes to the cache), else a cached page.
 * Takes ownership of `page`. Held whatever came back -- rows, none, or a
 * refusal -- so the same question is not asked again next frame.
 */
static void
model_take(
    struct Editor_CatalogModel* model,
    uint32_t page_index,
    struct EditorHost_CatalogPage* page,
    enum EditorHost_Status status)
{
    if( !model_question_held(model) || model->reasking )
    {
        /* A new question, or the same one asked again over a partial index
         * (the rows can be others now): every held page goes. Only a new
         * question resets the list's scroll and selection. */
        int const fresh = !model->reasking;
        if( model->held )
            Editor_HostCatalogPageFree(&model->result);
        drop_record(model);
        drop_cache(model);
        model->result = *page;
        model->status = status;
        model->held = 1;
        model->held_kind = model->kind;
        model->held_page = page_index;
        model->held_page_size = model->page_size;
        snprintf(model->held_filter, sizeof(model->held_filter), "%s", model->filter);
        model->reasking = 0;
        if( fresh )
            model->key_serial++;
    }
    else if( page_index == model->page )
    {
        if( model->held_page != page_index )
        {
            struct EditorHost_CatalogPage old = model->result;
            model_cache_put(model, model->held_page, &old, model->status);
        }
        else
            Editor_HostCatalogPageFree(&model->result);
        model->result = *page;
        model->status = status;
        model->held_page = page_index;
    }
    else
        model_cache_put(model, page_index, page, status);
    memset(page, 0, sizeof(*page));
    if( model->result.page_flags & TORIRSMAPED_CATALOG_PAGE_PARTIAL )
        model->partial_asked_ms = Editor_CatalogModelNowMs();
    model->answers++;
    model_order_rebuild(model);
}

/** The anchor from the cache, when the window moved onto a page held there:
 *  no round trip. */
static int
model_swap_in(
    struct Editor_CatalogModel* model,
    uint32_t page_index)
{
    for( int i = 0; i < EDITOR_CATALOG_CACHE_PAGES; i++ )
    {
        if( !model->cache[i].held || model->cache[i].page != page_index )
            continue;
        {
            struct EditorHost_CatalogPage const anchor = model->result;
            enum EditorHost_Status const anchor_status = model->status;
            uint32_t const anchor_page = model->held_page;
            model->result = model->cache[i].result;
            model->status = model->cache[i].status;
            model->held_page = page_index;
            model->cache[i].result = anchor;
            model->cache[i].status = anchor_status;
            model->cache[i].page = anchor_page;
        }
        model->answers++;
        return 1;
    }
    return 0;
}

/** Pages a sort can gather to order the whole result. */
#define CATALOG_SORT_PAGES (EDITOR_CATALOG_CACHE_PAGES + 1)

/**
 * The page to ask next besides the anchor, -1 none: every page while a sort
 * other than best match wants the whole result and it spans few enough of
 * them; else the page the window's end reaches, a quarter page early (the
 * prefetch, so a scroll to the end finds its rows waiting).
 */
static int
model_missing_page(const struct Editor_CatalogModel* model)
{
    uint32_t const pages = Editor_CatalogModelPageCount(model);

    if( !model->held || model->status != EDITOR_HOST_OK || model->result.total == 0 )
        return -1;
    if( model->sort != EDITOR_CATALOG_SORT_BEST && pages <= CATALOG_SORT_PAGES )
        for( uint32_t p = 0; p < pages; p++ )
            if( !model_page_held(model, p) )
                return (int)p;
    if( model->window_rows > 0 )
    {
        uint32_t last = (uint32_t)model->window_first + (uint32_t)model->window_rows - 1 + model->page_size / 4;
        if( last >= model->result.total )
            last = model->result.total - 1;
        for( uint32_t p = (uint32_t)model->window_first / model->page_size; p <= last / model->page_size; p++ )
            if( !model_page_held(model, p) )
                return (int)p;
    }
    return -1;
}

/** One CATALOG_QUERY round trip for page `page_index` of the wanted question. */
static int
model_ask(
    struct Editor_CatalogModel* model,
    struct EditorHost* host,
    uint32_t page_index)
{
    struct EditorHost_CatalogPage page;
    enum EditorHost_Status status;

    memset(&page, 0, sizeof(page));
    {
        double const t0 = Editor_CatalogModelNowMs();
        /* PROGRESS: while the server's warm build runs the answer comes
         * from its partial index instead of waiting for the build (the host
         * drops the flag for a server that does not read it). */
        status = Editor_HostMapEdCatalogQuery(
            host, model->kind, model->filter, TORIRSMAPED_CATALOG_PROGRESS, page_index, model->page_size, &page);
        model->last_query_ms = Editor_CatalogModelNowMs() - t0;
    }
    model->queries++;
    if( page_index != model->page )
        model->prefetches++;
    if( Editor_CatalogModelStats() )
        TORIRS_REPORT("catalog: query kind=%u find='%s' page=%u total=%u rows=%d %.2f ms%s\n", model->kind,
            model->filter, page_index, page.total, page.row_count, model->last_query_ms,
            page_index != model->page ? " (prefetch)" : "");
    model_take(model, page_index, &page, status);
    return 1;
}

int
Editor_CatalogModelNextPage(const struct Editor_CatalogModel* model)
{
    assert(model);
    if( model->source != EDITOR_CATALOG_SOURCE_INDEX )
        return -1;
    if( !model_question_held(model) )
        return (int)model->page;
    if( model->held_page != model->page )
        return model_page_held(model, model->page) ? -1 : (int)model->page;
    return model_missing_page(model);
}

int
Editor_CatalogModelRefresh(
    struct Editor_CatalogModel* model,
    struct EditorHost* host)
{
    int next;

    assert(model);
    assert(host);

    /* A local list (favourites, recent, in square) asks nothing. */
    if( model->source != EDITOR_CATALOG_SOURCE_INDEX )
        return 0;
    if( model_question_held(model) && model->held_page != model->page && model_swap_in(model, model->page) )
        /* The window moved onto a page the cache holds: no round trip. */
        return 1;
    if( !model_question_held(model) && model->settle > 0 )
    {
        model->settle--;
        return 0;
    }
    /* The page in view came from the warm build's partial index: ask it
     * again until the build is done (Editor_CatalogModelPartial). */
    if( model_question_held(model) && model->held_page == model->page && Editor_CatalogModelPartial(model)
        && Editor_CatalogModelNowMs() - model->partial_asked_ms >= EDITOR_CATALOG_PARTIAL_REASK_MS )
    {
        model->reasking = 1;
        return model_ask(model, host, model->page);
    }
    next = Editor_CatalogModelNextPage(model);
    if( next < 0 )
        return 0;
    return model_ask(model, host, (uint32_t)next);
}

int
Editor_CatalogModelPartial(const struct Editor_CatalogModel* model)
{
    assert(model);
    return model->source == EDITOR_CATALOG_SOURCE_INDEX && model->held && model->status == EDITOR_HOST_OK
           && (model->result.page_flags & TORIRSMAPED_CATALOG_PAGE_PARTIAL) != 0;
}

int
Editor_CatalogModelRowCount(const struct Editor_CatalogModel* model)
{
    assert(model);
    if( !model->held || model->status != EDITOR_HOST_OK )
        return 0;
    return model->result.row_count;
}

const struct EditorHost_CatalogRow*
Editor_CatalogModelRow(
    const struct Editor_CatalogModel* model,
    int row)
{
    assert(model);
    assert(row >= 0);
    assert(row < Editor_CatalogModelRowCount(model));
    return &model->result.rows[row];
}

static const char*
kind_label(uint32_t kind)
{
    return kind == TORIRSMAPED_CATALOG_ANY ? "records" : ToriRSMapEdCatalog_KindName(kind);
}

void
Editor_CatalogModelCountText(
    const struct Editor_CatalogModel* model,
    char* out,
    int out_size)
{
    assert(model);
    assert(out);
    assert(out_size > 0);

    if( !model->held )
        snprintf(out, (size_t)out_size, "asking the server...");
    else if( model->status == EDITOR_HOST_UNSUPPORTED )
        snprintf(out, (size_t)out_size, "the server has no catalog");
    else if( model->status != EDITOR_HOST_OK )
        snprintf(out, (size_t)out_size, "the catalog did not answer (%d)", (int)model->status);
    else
        snprintf(out, (size_t)out_size, "%d of %u %s", model->result.row_count, model->result.total,
            kind_label(model->held_kind));
}

void
Editor_CatalogModelRowText(
    const struct Editor_CatalogModel* model,
    int row,
    char* out,
    int out_size)
{
    const struct EditorHost_CatalogRow* r = Editor_CatalogModelRow(model, row);
    char id[16];

    assert(out);
    assert(out_size > 0);
    if( r->id >= 0 )
        snprintf(id, sizeof(id), "%d", (int)r->id);
    else
        snprintf(id, sizeof(id), "-");
    if( model->held_kind == TORIRSMAPED_CATALOG_ANY )
        snprintf(out, (size_t)out_size, "%s %s  (%s)%s%s", ToriRSMapEdCatalog_KindName(r->kind), r->name, id,
            r->display[0] ? "  " : "", r->display);
    else
        snprintf(out, (size_t)out_size, "%s  (%s)%s%s", r->name, id, r->display[0] ? "  " : "", r->display);
}

const struct EditorHost_CatalogRecord*
Editor_CatalogModelRecord(
    struct Editor_CatalogModel* model,
    struct EditorHost* host,
    int row)
{
    const struct EditorHost_CatalogRow* r;

    assert(model);
    assert(host);

    if( row != model->record_row )
    {
        drop_record(model);
        r = Editor_CatalogModelRow(model, row);
        {
            double const t0 = Editor_CatalogModelNowMs();
            model->record_status = Editor_HostMapEdCatalogRecord(host, r->kind, r->id, r->name, &model->record);
            model->last_record_ms = Editor_CatalogModelNowMs() - t0;
        }
        model->record_fetches++;
        if( Editor_CatalogModelStats() )
            TORIRS_REPORT("catalog: record %s %s %.2f ms\n", ToriRSMapEdCatalog_KindName(r->kind), r->name,
                model->last_record_ms);
        model->record_row = row;
        model->record_held = model->record_status == EDITOR_HOST_OK;
    }
    return model->record_held ? &model->record : NULL;
}

/* ==== the loc finder (workbench 6.2) ============================================ */

/* ---- shape classes ---- */

struct catalog_shape_class
{
    const char* name;
    uint32_t mask;
    const char* numbers;
};

static const struct catalog_shape_class k_shape_classes[] = {
    { "wall", (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 9), "0 1 2 3 9" },
    { "decor", (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8), "4 5 6 7 8" },
    { "centre", (1u << 10) | (1u << 11), "10 11" },
    { "roof", 0x3FF000u, "12 to 21" },
    { "ground", 1u << 22, "22" },
};
#define CATALOG_SHAPE_CLASSES ((int)(sizeof(k_shape_classes) / sizeof(k_shape_classes[0])))

_Static_assert(EDITOR_LOC_SHAPE_COUNT == 23, "the classes cover shapes 0..22");

/** "0".."22" -> the number, else -1. */
static int
catalog_shape_number(const char* text)
{
    int value = 0;

    if( !text[0] || strlen(text) > 2 )
        return -1;
    for( const char* c = text; *c; c++ )
    {
        if( *c < '0' || *c > '9' )
            return -1;
        value = value * 10 + (*c - '0');
    }
    return value < EDITOR_LOC_SHAPE_COUNT ? value : -1;
}

int
Editor_CatalogShapeClass(
    const char* name,
    uint32_t* out_mask)
{
    int number;

    assert(name);
    assert(out_mask);
    *out_mask = 0;
    number = catalog_shape_number(name);
    if( number >= 0 )
    {
        *out_mask = 1u << number;
        return 1;
    }
    for( int i = 0; i < CATALOG_SHAPE_CLASSES; i++ )
        if( strcmp(name, k_shape_classes[i].name) == 0 )
        {
            *out_mask = k_shape_classes[i].mask;
            return 1;
        }
    return 0;
}

const char*
Editor_CatalogShapeClassOf(int shape)
{
    assert(shape >= 0);
    assert(shape < EDITOR_LOC_SHAPE_COUNT);
    for( int i = 0; i < CATALOG_SHAPE_CLASSES; i++ )
        if( k_shape_classes[i].mask & (1u << shape) )
            return k_shape_classes[i].name;
    return "?";
}

/* ---- chips ---- */

void
Editor_CatalogChipsInit(struct Editor_CatalogChips* chips)
{
    assert(chips);
    memset(chips, 0, sizeof(*chips));
}

static int
chip_key_ok(const char* key)
{
    size_t const length = strlen(key);
    if( length == 0 || length >= EDITOR_CATALOG_CHIP_KEY_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
        if( !((key[i] >= 'a' && key[i] <= 'z') || (key[i] >= '0' && key[i] <= '9') || key[i] == '_') )
            return 0;
    return 1;
}

static int
chip_value_ok(const char* value)
{
    size_t const length = strlen(value);
    if( length == 0 || length >= EDITOR_CATALOG_CHIP_VALUE_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
        if( value[i] == ' ' || value[i] == '=' || (unsigned char)value[i] < 0x20 )
            return 0;
    return 1;
}

int
Editor_CatalogChipsAdd(
    struct Editor_CatalogChips* chips,
    const char* key,
    const char* value)
{
    assert(chips);
    assert(key);
    assert(value);
    if( !chip_key_ok(key) || !chip_value_ok(value) )
        return 0;
    for( int i = 0; i < chips->count; i++ )
        if( strcmp(chips->chip[i].key, key) == 0 && strcmp(chips->chip[i].value, value) == 0 )
            return 0;
    if( chips->count >= EDITOR_CATALOG_CHIPS_MAX )
        return 0;
    snprintf(chips->chip[chips->count].key, sizeof(chips->chip[0].key), "%s", key);
    snprintf(chips->chip[chips->count].value, sizeof(chips->chip[0].value), "%s", value);
    chips->count++;
    return 1;
}

void
Editor_CatalogChipsRemove(
    struct Editor_CatalogChips* chips,
    int index)
{
    assert(chips);
    assert(index >= 0);
    assert(index < chips->count);
    memmove(&chips->chip[index], &chips->chip[index + 1], sizeof(chips->chip[0]) * (size_t)(chips->count - index - 1));
    chips->count--;
}

/** Append `word` to `out` (length `*at`), a space first unless it is the
 *  first. Stops quietly where `out` ends: the input box is the bound. */
static void
catalog_append_word(
    char* out,
    int out_size,
    int* at,
    const char* word,
    int length)
{
    int const need = (*at > 0 ? 1 : 0) + length;
    if( *at + need >= out_size )
        return;
    if( *at > 0 )
        out[(*at)++] = ' ';
    memcpy(out + *at, word, (size_t)length);
    *at += length;
    out[*at] = '\0';
}

int
Editor_CatalogChipsTake(
    struct Editor_CatalogChips* chips,
    const char* typed,
    char* out_rest,
    int out_size)
{
    const char* p;
    int at = 0;
    int added = 0;

    assert(chips);
    assert(typed);
    assert(out_rest);
    assert(out_size > 0);
    out_rest[0] = '\0';
    for( p = typed; *p; )
    {
        const char* end;
        const char* eq;
        while( *p == ' ' )
            p++;
        if( !*p )
            break;
        end = p;
        while( *end && *end != ' ' )
            end++;
        eq = memchr(p, '=', (size_t)(end - p));
        if( eq && eq > p && eq + 1 < end && eq - p < EDITOR_CATALOG_CHIP_KEY_MAX
            && end - eq - 1 < EDITOR_CATALOG_CHIP_VALUE_MAX )
        {
            char key[EDITOR_CATALOG_CHIP_KEY_MAX];
            char value[EDITOR_CATALOG_CHIP_VALUE_MAX];
            int const taken_before = chips->count;
            snprintf(key, sizeof(key), "%.*s", (int)(eq - p), p);
            snprintf(value, sizeof(value), "%.*s", (int)(end - eq - 1), eq + 1);
            if( Editor_CatalogChipsAdd(chips, key, value) )
                added++;
            else if( chips->count == taken_before && chips->count >= EDITOR_CATALOG_CHIPS_MAX )
                catalog_append_word(out_rest, out_size, &at, p, (int)(end - p));
            else if( !chip_key_ok(key) || !chip_value_ok(value) )
                catalog_append_word(out_rest, out_size, &at, p, (int)(end - p));
        }
        else
            catalog_append_word(out_rest, out_size, &at, p, (int)(end - p));
        p = end;
    }
    return added;
}

void
Editor_CatalogChipsCompose(
    const struct Editor_CatalogChips* chips,
    const char* free_text,
    char* out,
    int out_size)
{
    int at = 0;

    assert(chips);
    assert(free_text);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    for( const char* p = free_text; *p; )
    {
        const char* end;
        while( *p == ' ' )
            p++;
        if( !*p )
            break;
        end = p;
        while( *end && *end != ' ' )
            end++;
        catalog_append_word(out, out_size, &at, p, (int)(end - p));
        p = end;
    }
    for( int i = 0; i < chips->count; i++ )
    {
        const struct Editor_CatalogChip* chip = &chips->chip[i];
        char word[EDITOR_CATALOG_CHIP_KEY_MAX + EDITOR_CATALOG_CHIP_VALUE_MAX + 4];
        int length;
        /* `shape=` (a class or a number) resolves server-side against the
         * loc's placeable shapes (torirs_maped_catalog.h), so every chip
         * goes as it reads. */
        length = snprintf(word, sizeof(word), "%s=%s", chip->key, chip->value);
        catalog_append_word(out, out_size, &at, word, length);
    }
}

void
Editor_CatalogChipLabel(
    const struct Editor_CatalogChip* chip,
    char* out,
    int out_size)
{
    assert(chip);
    assert(out);
    assert(out_size > 0);
    if( strcmp(chip->key, "dialogue") == 0 && strcmp(chip->value, "yes") == 0 )
        snprintf(out, (size_t)out_size, "has dialogue");
    else if( strcmp(chip->key, "spawn") == 0 && strcmp(chip->value, "yes") == 0 )
        snprintf(out, (size_t)out_size, "has spawn");
    else if( strcmp(chip->value, "*") == 0 )
        snprintf(out, (size_t)out_size, "%s: any", chip->key);
    else
        snprintf(out, (size_t)out_size, "%s: %s", chip->key, chip->value);
}

void
Editor_CatalogChipTip(
    const struct Editor_CatalogChip* chip,
    char* out,
    int out_size)
{
    assert(chip);
    assert(out);
    assert(out_size > 0);
    if( strcmp(chip->key, "shape") == 0 )
    {
        for( int i = 0; i < CATALOG_SHAPE_CLASSES; i++ )
            if( strcmp(chip->value, k_shape_classes[i].name) == 0 )
            {
                snprintf(out, (size_t)out_size, "shape %s: %s; x removes it", chip->value, k_shape_classes[i].numbers);
                return;
            }
        if( catalog_shape_number(chip->value) >= 0 )
        {
            snprintf(out, (size_t)out_size, "shape %s, %s; x removes it", chip->value,
                Editor_LocShapeName(catalog_shape_number(chip->value)));
            return;
        }
    }
    snprintf(out, (size_t)out_size, "%s=%s; x removes it", chip->key, chip->value);
}

/* ---- the "+ filter" menu ---- */

struct catalog_filter_item
{
    const char* label;
    const char* key;
    /** NULL: a key the user completes in Find. */
    const char* value;
};

static const struct catalog_filter_item k_filter_items[] = {
    { "+ filter", NULL, NULL },
    { "shape: wall (0 1 2 3 9)", "shape", "wall" },
    { "shape: decor (4 to 8)", "shape", "decor" },
    { "shape: centre (10 11)", "shape", "centre" },
    { "shape: roof (12 to 21)", "shape", "roof" },
    { "shape: ground (22)", "shape", "ground" },
    { "size: 1x1", "size", "1x1" },
    { "size: 2x2", "size", "2x2" },
    { "size: 3x3", "size", "3x3" },
    { "category=... (type it)", "category", NULL },
    { "op=... (type it)", "op", NULL },
    { "multivar: any", "multivar", "*" },
    { "has dialogue", "dialogue", "yes" },
    { "has spawn", "spawn", "yes" },
};
#define CATALOG_FILTER_ITEMS ((int)(sizeof(k_filter_items) / sizeof(k_filter_items[0])))

static const char* g_filter_labels[CATALOG_FILTER_ITEMS];

int
Editor_CatalogFilterMenuCount(void)
{
    return CATALOG_FILTER_ITEMS;
}

const char* const*
Editor_CatalogFilterMenuLabels(void)
{
    if( !g_filter_labels[0] )
        for( int i = 0; i < CATALOG_FILTER_ITEMS; i++ )
            g_filter_labels[i] = k_filter_items[i].label;
    return g_filter_labels;
}

int
Editor_CatalogFilterMenuItem(
    int index,
    char out_key[EDITOR_CATALOG_CHIP_KEY_MAX],
    char out_value[EDITOR_CATALOG_CHIP_VALUE_MAX])
{
    assert(index >= 0);
    assert(index < CATALOG_FILTER_ITEMS);
    assert(out_key);
    assert(out_value);
    out_key[0] = '\0';
    out_value[0] = '\0';
    if( !k_filter_items[index].key )
        return 0;
    snprintf(out_key, EDITOR_CATALOG_CHIP_KEY_MAX, "%s", k_filter_items[index].key);
    if( !k_filter_items[index].value )
        return 2;
    snprintf(out_value, EDITOR_CATALOG_CHIP_VALUE_MAX, "%s", k_filter_items[index].value);
    return 1;
}

/* ---- a row's summary fields ---- */

const char*
Editor_CatalogRowField(
    const struct EditorHost_CatalogRow* row,
    const char* key)
{
    assert(row);
    assert(key);
    for( int i = 0; i < row->field_count; i++ )
        if( strcmp(row->fields[i].key, key) == 0 )
            return row->fields[i].value;
    return NULL;
}

/** `key` is `family` or `family` followed by digits (`shape3`, `model2`). */
static int
catalog_key_in_family(
    const char* key,
    const char* family)
{
    size_t const length = strlen(family);
    if( strncmp(key, family, length) != 0 )
        return 0;
    for( const char* c = key + length; *c; c++ )
        if( *c < '0' || *c > '9' )
            return 0;
    return 1;
}

uint32_t
Editor_CatalogRowShapeMask(const struct EditorHost_CatalogRow* row)
{
    uint32_t have = 0;
    uint32_t mask = 0;
    int models = 0;

    assert(row);
    if( row->kind != TORIRSMAPED_CATALOG_LOC )
        return 0;
    /* The server's derived `shapes` (workbench 6.2, torirs_maped_catalog.h):
     * the placeable shapes by the Locs panel's rule, already worked out. A
     * row without it (an older server, a local row) is read from its
     * `shapeK=` / `models=` rows below. */
    {
        const char* shapes = Editor_CatalogRowField(row, "shapes");
        if( shapes )
        {
            for( const char* c = shapes; *c; )
            {
                char* end;
                long const n = strtol(c, &end, 10);
                if( end == c )
                {
                    c++;
                    continue;
                }
                if( n >= 0 && n < EDITOR_LOC_SHAPE_COUNT )
                    mask |= 1u << n;
                c = end;
            }
            return mask;
        }
    }
    for( int i = 0; i < row->field_count; i++ )
    {
        const char* key = row->fields[i].key;
        const char* value = row->fields[i].value;
        if( catalog_key_in_family(key, "shape") )
        {
            /* `shapeK=N,<model>[,<model>...]`: the shape is the head. */
            int n = 0;
            int digits = 0;
            while( value[digits] >= '0' && value[digits] <= '9' && digits < 3 )
            {
                n = n * 10 + (value[digits] - '0');
                digits++;
            }
            if( digits > 0 && n < EDITOR_LOC_SHAPE_COUNT )
                have |= 1u << n;
        }
        else if( catalog_key_in_family(key, "model") || catalog_key_in_family(key, "models") )
            models = 1;
    }
    /* No shape line: the models are a centrepiece's (the loader's reading). */
    if( !have && models )
        have = 1u << 10;
    for( int s = 0; s < EDITOR_LOC_SHAPE_COUNT; s++ )
        if( have & (1u << Editor_LocShapeModelShape(s)) )
            mask |= 1u << s;
    return mask;
}

void
Editor_CatalogRowSize(
    const struct EditorHost_CatalogRow* row,
    int* out_w,
    int* out_l)
{
    const char* width;
    const char* length;

    assert(row);
    assert(out_w);
    assert(out_l);
    /* The server's derived `size` ("WxL", 1 where unstated), else the
     * config's own width= / length=. */
    {
        const char* size = Editor_CatalogRowField(row, "size");
        int w;
        int l;
        if( size && sscanf(size, "%dx%d", &w, &l) == 2 && w > 0 && l > 0 )
        {
            *out_w = w;
            *out_l = l;
            return;
        }
    }
    width = Editor_CatalogRowField(row, "width");
    length = Editor_CatalogRowField(row, "length");
    *out_w = width && atoi(width) > 0 ? atoi(width) : 1;
    *out_l = length && atoi(length) > 0 ? atoi(length) : 1;
}

/* ---- views ---- */

static void
catalog_view_of_row(
    const struct EditorHost_CatalogRow* row,
    struct Editor_CatalogView* out)
{
    const char* category = Editor_CatalogRowField(row, "category");
    memset(out, 0, sizeof(*out));
    out->kind = row->kind;
    out->id = row->id;
    out->name = row->name;
    out->display = row->display ? row->display : "";
    out->category = category ? category : "";
    out->shape_mask = Editor_CatalogRowShapeMask(row);
    if( row->kind == TORIRSMAPED_CATALOG_LOC )
    {
        const char* layer = Editor_CatalogRowField(row, "layer");
        Editor_CatalogRowSize(row, &out->size_w, &out->size_l);
        out->layer = layer ? layer : "";
    }
    else
        out->layer = "";
    out->shape = -1;
}

static void
catalog_view_of_local(
    const struct Editor_CatalogLocalRow* row,
    struct Editor_CatalogView* out)
{
    memset(out, 0, sizeof(*out));
    out->kind = row->kind;
    out->id = row->id;
    out->name = row->name;
    out->display = row->display;
    out->category = row->category;
    out->layer = "";
    out->shape_mask = row->shape_mask;
    out->size_w = row->size_w;
    out->size_l = row->size_l;
    out->shape = row->shape;
    out->rotation = row->rotation;
}

/** Server row `row` (absolute, the server's order) of the held question. */
static const struct EditorHost_CatalogRow*
model_server_row(
    const struct Editor_CatalogModel* model,
    int row)
{
    const struct EditorHost_CatalogPage* page;
    uint32_t const index = (uint32_t)row / model->page_size;
    int at;

    page = model_page(model, index);
    if( !page )
        return NULL;
    at = row - (int)(index * model->page_size);
    return at >= 0 && at < page->row_count ? &page->rows[at] : NULL;
}

static int
catalog_view_cmp(
    const struct Editor_CatalogView* a,
    const struct Editor_CatalogView* b,
    enum Editor_CatalogSort sort)
{
    int c = 0;
    switch( sort )
    {
    case EDITOR_CATALOG_SORT_ID:
        /* No id (-1) sorts last. */
        if( a->id != b->id )
            return (uint32_t)a->id < (uint32_t)b->id ? -1 : 1;
        break;
    case EDITOR_CATALOG_SORT_CATEGORY:
        if( !a->category[0] != !b->category[0] )
            return a->category[0] ? -1 : 1;
        c = strcmp(a->category, b->category);
        break;
    case EDITOR_CATALOG_SORT_SIZE:
        c = a->size_w * a->size_l - b->size_w * b->size_l;
        break;
    default:
        break;
    }
    if( c != 0 )
        return c;
    c = strcmp(a->name, b->name);
    if( c != 0 )
        return c;
    return a->id == b->id ? 0 : (uint32_t)a->id < (uint32_t)b->id ? -1 : 1;
}

/** Insertion sort of `order` by the views it names: at most a few hundred
 *  rows, rebuilt only when an answer or the sort changed. */
static void
model_sort_order(
    const struct Editor_CatalogModel* model,
    int* order,
    int count,
    int local)
{
    for( int i = 1; i < count; i++ )
    {
        int const item = order[i];
        struct Editor_CatalogView vi;
        int j = i - 1;
        if( local )
            catalog_view_of_local(&model->local[item], &vi);
        else
            catalog_view_of_row(model_server_row(model, item), &vi);
        while( j >= 0 )
        {
            struct Editor_CatalogView vj;
            if( local )
                catalog_view_of_local(&model->local[order[j]], &vj);
            else
                catalog_view_of_row(model_server_row(model, order[j]), &vj);
            if( catalog_view_cmp(&vj, &vi, model->sort) <= 0 )
                break;
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = item;
    }
}

static void
model_order_rebuild(struct Editor_CatalogModel* model)
{
    uint32_t pages;
    int total;

    model->order_count = 0;
    model->order_answers = model->answers;
    if( model->source != EDITOR_CATALOG_SOURCE_INDEX || model->sort == EDITOR_CATALOG_SORT_BEST || !model->held
        || model->status != EDITOR_HOST_OK || model->result.total == 0 )
        return;
    pages = Editor_CatalogModelPageCount(model);
    if( pages > CATALOG_SORT_PAGES )
        return;
    for( uint32_t p = 0; p < pages; p++ )
        if( !model_page(model, p) )
            return;
    total = (int)model->result.total;
    for( int r = 0; r < total; r++ )
        if( !model_server_row(model, r) )
            return;
    if( !model->order )
    {
        model->order = ToriMem_Alloc(
            ToriMem_Editor(), sizeof(int) * (size_t)CATALOG_SORT_PAGES * TORIRSMAPED_CATALOG_PAGE_MAX);
        assert(model->order);
    }
    for( int r = 0; r < total; r++ )
        model->order[r] = r;
    model_sort_order(model, model->order, total, 0);
    model->order_count = total;
}

/** A local row passes Find's free words (each in its name or display name,
 *  any case) and the shape chips (one of its shapes in the class). */
static int
catalog_local_match(
    const struct Editor_CatalogLocalRow* row,
    const char* filter)
{
    for( const char* p = filter; *p; )
    {
        const char* end;
        const char* eq;
        char word[EDITOR_CATALOG_FILTER_MAX];
        int length;
        while( *p == ' ' )
            p++;
        if( !*p )
            break;
        end = p;
        while( *end && *end != ' ' )
            end++;
        length = (int)(end - p);
        snprintf(word, sizeof(word), "%.*s", length, p);
        p = end;
        eq = strchr(word, '=');
        if( eq )
        {
            uint32_t mask;
            char value[EDITOR_CATALOG_CHIP_VALUE_MAX];
            if( strncmp(word, "shape=", 6) != 0 )
                continue; /* the index's fields: a local row does not hold them */
            snprintf(value, sizeof(value), "%s", eq + 1);
            if( strchr(value, ',') )
                *strchr(value, ',') = '\0';
            if( Editor_CatalogShapeClass(value, &mask) && row->shape_mask && !(row->shape_mask & mask) )
                return 0;
            continue;
        }
        for( char* c = word; *c; c++ )
            if( *c >= 'A' && *c <= 'Z' )
                *c = (char)(*c - 'A' + 'a');
        {
            char hay[2 * EDITOR_CATALOG_LOCAL_NAME_MAX + 2];
            snprintf(hay, sizeof(hay), "%s %s", row->name, row->display);
            for( char* c = hay; *c; c++ )
                if( *c >= 'A' && *c <= 'Z' )
                    *c = (char)(*c - 'A' + 'a');
            if( !strstr(hay, word) )
                return 0;
        }
    }
    return 1;
}

static void
model_local_rebuild(struct Editor_CatalogModel* model)
{
    model->local_shown = 0;
    for( int i = 0; i < model->local_count; i++ )
        if( catalog_local_match(&model->local[i], model->filter) )
            model->local_order[model->local_shown++] = i;
    if( model->sort != EDITOR_CATALOG_SORT_BEST )
        model_sort_order(model, model->local_order, model->local_shown, 1);
    model->answers++;
}

/* ---- the list ---- */

void
Editor_CatalogModelWantWindow(
    struct Editor_CatalogModel* model,
    int first,
    int rows)
{
    uint32_t const last_page = Editor_CatalogModelPageCount(model) - 1;
    uint32_t page;

    assert(model);
    assert(rows >= 0);
    if( first < 0 )
        first = 0;
    model->window_first = first;
    model->window_rows = rows;
    if( model->source != EDITOR_CATALOG_SOURCE_INDEX )
        return;
    /* While a sort orders the whole result the window's rows are the
     * order's, every page held: the anchor stays. */
    if( model->order_count > 0 )
        return;
    page = (uint32_t)first / model->page_size;
    model->page = page > last_page ? last_page : page;
}

int
Editor_CatalogModelViewCount(const struct Editor_CatalogModel* model)
{
    assert(model);
    if( model->source != EDITOR_CATALOG_SOURCE_INDEX )
        return model->local_shown;
    if( !model->held || model->status != EDITOR_HOST_OK )
        return 0;
    return (int)model->result.total;
}

int
Editor_CatalogModelViewAt(
    const struct Editor_CatalogModel* model,
    int row,
    struct Editor_CatalogView* out)
{
    const struct EditorHost_CatalogRow* r;

    assert(model);
    assert(out);
    assert(row >= 0);
    if( model->source != EDITOR_CATALOG_SOURCE_INDEX )
    {
        if( row >= model->local_shown )
            return 0;
        catalog_view_of_local(&model->local[model->local_order[row]], out);
        return 1;
    }
    if( row >= Editor_CatalogModelViewCount(model) )
        return 0;
    if( model->order_count > 0 && model->order_answers == model->answers )
        r = row < model->order_count ? model_server_row(model, model->order[row]) : NULL;
    else
        r = model_server_row(model, row);
    if( !r )
        return 0;
    catalog_view_of_row(r, out);
    return 1;
}

int
Editor_CatalogModelFindRow(
    const struct Editor_CatalogModel* model,
    uint32_t kind,
    const char* name)
{
    int const count = Editor_CatalogModelViewCount(model);

    assert(model);
    assert(name);
    /* Only rows held can be found: the window's pages, the cache, the
     * local list. A few hundred at most. */
    for( int r = 0; r < count; r++ )
    {
        struct Editor_CatalogView view;
        if( model->source == EDITOR_CATALOG_SOURCE_INDEX && model->order_count == 0
            && !model_page(model, (uint32_t)r / model->page_size) )
        {
            /* Skip the rest of an unheld page in one step. */
            r = (int)(((uint32_t)r / model->page_size + 1) * model->page_size) - 1;
            continue;
        }
        if( Editor_CatalogModelViewAt(model, r, &view) && view.kind == kind && strcmp(view.name, name) == 0 )
            return r;
    }
    return -1;
}

void
Editor_CatalogModelSetSource(
    struct Editor_CatalogModel* model,
    enum Editor_CatalogSource source)
{
    assert(model);
    assert(source >= EDITOR_CATALOG_SOURCE_INDEX);
    assert(source < EDITOR_CATALOG_SOURCE_COUNT);
    if( source == model->source )
        return;
    model->source = source;
    model->key_serial++;
    model->answers++;
    drop_record(model);
    if( source != EDITOR_CATALOG_SOURCE_INDEX )
        model_local_rebuild(model);
    else
        model_order_rebuild(model);
}

void
Editor_CatalogModelSetLocal(
    struct Editor_CatalogModel* model,
    const struct Editor_CatalogLocalRow* rows,
    int count)
{
    assert(model);
    assert(count >= 0);
    if( count > EDITOR_CATALOG_LOCAL_MAX )
        count = EDITOR_CATALOG_LOCAL_MAX;
    if( !model->local )
    {
        model->local = ToriMem_Alloc(ToriMem_Editor(), sizeof(*model->local) * EDITOR_CATALOG_LOCAL_MAX);
        assert(model->local);
        model->local_order = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * EDITOR_CATALOG_LOCAL_MAX);
        assert(model->local_order);
    }
    if( count > 0 )
    {
        assert(rows);
        memcpy(model->local, rows, sizeof(*rows) * (size_t)count);
    }
    model->local_count = count;
    if( model->source != EDITOR_CATALOG_SOURCE_INDEX )
        model_local_rebuild(model);
}

void
Editor_CatalogModelSetSort(
    struct Editor_CatalogModel* model,
    enum Editor_CatalogSort sort)
{
    assert(model);
    assert(sort >= EDITOR_CATALOG_SORT_BEST);
    assert(sort < EDITOR_CATALOG_SORT_COUNT);
    if( sort == model->sort )
        return;
    model->sort = sort;
    /* Rows move: a record held for an absolute row is another row's now. */
    drop_record(model);
    model->answers++;
    if( model->source != EDITOR_CATALOG_SOURCE_INDEX )
        model_local_rebuild(model);
    else
        model_order_rebuild(model);
}

int
Editor_CatalogModelSortApplies(const struct Editor_CatalogModel* model)
{
    assert(model);
    if( model->sort == EDITOR_CATALOG_SORT_BEST || model->source != EDITOR_CATALOG_SOURCE_INDEX )
        return 1;
    if( !model->held || model->status != EDITOR_HOST_OK || model->result.total <= 1 )
        return 1;
    return model->order_count > 0 && model->order_answers == model->answers;
}

static const char* const k_sort_labels[EDITOR_CATALOG_SORT_COUNT] = {
    "best match", "name", "id", "category", "size",
};

const char* const*
Editor_CatalogSortLabels(void)
{
    return k_sort_labels;
}

/** `value` with thousands commas: 63307 -> "63,307". */
static void
catalog_thousands(
    uint32_t value,
    char* out,
    size_t out_size)
{
    char digits[16];
    int const length = snprintf(digits, sizeof(digits), "%u", value);
    size_t at = 0;

    for( int i = 0; i < length && at + 2 < out_size; i++ )
    {
        if( i > 0 && (length - i) % 3 == 0 )
            out[at++] = ',';
        out[at++] = digits[i];
    }
    out[at] = '\0';
}

static const char*
catalog_source_label(enum Editor_CatalogSource source)
{
    switch( source )
    {
    case EDITOR_CATALOG_SOURCE_FAVOURITES:
        return "favourites";
    case EDITOR_CATALOG_SOURCE_RECENT:
        return "recent picks";
    case EDITOR_CATALOG_SOURCE_IN_SQUARE:
        return "in the open squares";
    default:
        return "";
    }
}

void
Editor_CatalogModelRangeText(
    const struct Editor_CatalogModel* model,
    int first,
    char* out,
    int out_size)
{
    int const count = Editor_CatalogModelViewCount(model);
    char a[16];
    char b[16];
    char t[16];
    int last;

    assert(model);
    assert(out);
    assert(out_size > 0);
    if( model->source == EDITOR_CATALOG_SOURCE_INDEX )
    {
        if( !model->held )
        {
            snprintf(out, (size_t)out_size, "asking the server...");
            return;
        }
        if( model->status == EDITOR_HOST_UNSUPPORTED )
        {
            snprintf(out, (size_t)out_size, "the server has no catalog");
            return;
        }
        if( model->status != EDITOR_HOST_OK )
        {
            snprintf(out, (size_t)out_size, "the catalog did not answer (%d)", (int)model->status);
            return;
        }
    }
    if( count == 0 && Editor_CatalogModelPartial(model) )
    {
        uint32_t const of = model->result.progress_total;
        snprintf(out, (size_t)out_size, "0 %s so far, index %u%% read", kind_label(model->held_kind),
            of > 0 ? (unsigned)((uint64_t)model->result.progress_done * 100u / of) : 0u);
        return;
    }
    if( count == 0 )
    {
        snprintf(out, (size_t)out_size, "0 of 0 %s",
            model->source == EDITOR_CATALOG_SOURCE_INDEX ? kind_label(model->held_kind)
                                                         : catalog_source_label(model->source));
        return;
    }
    if( first < 0 )
        first = 0;
    if( first >= count )
        first = count - 1;
    /* The last held row of the run that starts at the window. */
    last = first;
    if( model->source != EDITOR_CATALOG_SOURCE_INDEX || model->order_count > 0 )
        last = count - 1;
    else
        while( last + 1 < count && model_server_row(model, last + 1) )
            last++;
    catalog_thousands((uint32_t)first + 1, a, sizeof(a));
    catalog_thousands((uint32_t)last + 1, b, sizeof(b));
    catalog_thousands((uint32_t)count, t, sizeof(t));
    /* An "all" query the server stopped early: the total is what it found. */
    if( model->source == EDITOR_CATALOG_SOURCE_INDEX && (model->result.page_flags & TORIRSMAPED_CATALOG_PAGE_SCOPED)
        && strlen(t) + 2 < sizeof(t) )
        strcat(t, "+");
    if( model->source == EDITOR_CATALOG_SOURCE_INDEX && Editor_CatalogModelPartial(model) )
    {
        uint32_t const done = model->result.progress_done;
        uint32_t const of = model->result.progress_total;
        snprintf(out, (size_t)out_size, "%s to %s of %s %s so far, index %u%% read", a, b, t,
            kind_label(model->held_kind), of > 0 ? (unsigned)((uint64_t)done * 100u / of) : 0u);
    }
    else if( model->source != EDITOR_CATALOG_SOURCE_INDEX )
        snprintf(out, (size_t)out_size, "%s to %s of %s %s", a, b, t, catalog_source_label(model->source));
    else if( !Editor_CatalogModelSortApplies(model) && Editor_CatalogModelPageCount(model) > CATALOG_SORT_PAGES )
        snprintf(out, (size_t)out_size, "%s to %s of %s %s, best match: %s sorts %d or fewer", a, b, t,
            kind_label(model->held_kind), k_sort_labels[model->sort],
            CATALOG_SORT_PAGES * (int)model->page_size);
    else if( !Editor_CatalogModelSortApplies(model) )
        snprintf(out, (size_t)out_size, "%s to %s of %s %s, sorting...", a, b, t, kind_label(model->held_kind));
    else
        snprintf(out, (size_t)out_size, "%s to %s of %s %s", a, b, t, kind_label(model->held_kind));
}

void
Editor_CatalogModelFeed(
    struct Editor_CatalogModel* model,
    uint32_t page,
    uint32_t total,
    const struct EditorHost_CatalogRow* rows,
    int row_count)
{
    struct EditorHost_CatalogPage copy;
    size_t bytes;
    int fields = 0;
    char* text;
    struct EditorHost_CatalogField* field_out;

    assert(model);
    assert(row_count >= 0);
    assert(row_count <= (int)model->page_size);
    /* A new question's first answer is the page the window starts on. */
    assert(model_question_held(model) || page == model->page);
    if( row_count > 0 )
        assert(rows);

    /* One block, as the host's decoder lays a page out: the rows, their
     * fields, then every string. */
    bytes = sizeof(struct EditorHost_CatalogRow) * (size_t)row_count;
    for( int r = 0; r < row_count; r++ )
    {
        bytes += strlen(rows[r].name) + 1 + strlen(rows[r].display ? rows[r].display : "") + 1;
        fields += rows[r].field_count;
        for( int f = 0; f < rows[r].field_count; f++ )
            bytes += strlen(rows[r].fields[f].key) + 1 + strlen(rows[r].fields[f].value) + 1;
    }
    bytes += sizeof(struct EditorHost_CatalogField) * (size_t)fields;
    memset(&copy, 0, sizeof(copy));
    copy.total = total;
    copy.page = page;
    copy.page_size = model->page_size;
    copy.row_count = row_count;
    copy.storage = ToriMem_Alloc(ToriMem_Editor(), bytes > 0 ? bytes : 1);
    assert(copy.storage);
    copy.rows = copy.storage;
    field_out = (struct EditorHost_CatalogField*)(copy.rows + row_count);
    text = (char*)(field_out + fields);
#define FEED_STR(dst, src)                                                                         \
    do                                                                                             \
    {                                                                                              \
        size_t const n_ = strlen(src) + 1;                                                         \
        memcpy(text, (src), n_);                                                                   \
        (dst) = text;                                                                              \
        text += n_;                                                                                \
    } while( 0 )
    for( int r = 0; r < row_count; r++ )
    {
        struct EditorHost_CatalogRow* out = &copy.rows[r];
        *out = rows[r];
        FEED_STR(out->name, rows[r].name);
        FEED_STR(out->display, rows[r].display ? rows[r].display : "");
        out->fields = field_out;
        for( int f = 0; f < rows[r].field_count; f++ )
        {
            FEED_STR(field_out->key, rows[r].fields[f].key);
            FEED_STR(field_out->value, rows[r].fields[f].value);
            field_out++;
        }
    }
#undef FEED_STR
    model->settle = 0;
    model_take(model, page, &copy, EDITOR_HOST_OK);
}

/* ---- cells ---- */

#define CATALOG_CELL_SEP '\x1f'

void
Editor_CatalogCellsText(
    const struct Editor_CatalogView* view,
    int sprite_id,
    int any_kind,
    char* out,
    int out_size)
{
    int shapes[EDITOR_LOC_SHAPE_COUNT];
    int const shape_count = Editor_LocShapeList(view->shape_mask, shapes);
    char keys[EDITOR_LOC_SHAPE_COUNT * 3 + 1];
    int at = 0;
    char sprite[16] = "";
    char id[16];
    char name[EDITOR_CATALOG_LOCAL_NAME_MAX + 16];

    assert(view);
    assert(out);
    assert(out_size > 0);
    keys[0] = '\0';
    /* At most EDITOR_CATALOG_KEYCAPS_MAX caps; past that the last says how
     * many more ("+3"), which is no shape (Editor_CatalogViewShapeAt). */
    for( int i = 0; i < shape_count && i < EDITOR_CATALOG_KEYCAPS_MAX; i++ )
    {
        if( i == EDITOR_CATALOG_KEYCAPS_MAX - 1 && shape_count > EDITOR_CATALOG_KEYCAPS_MAX )
            at += snprintf(keys + at, sizeof(keys) - (size_t)at, " +%d", shape_count - i);
        else
            at += snprintf(keys + at, sizeof(keys) - (size_t)at, "%s%d", at ? " " : "", shapes[i]);
    }
    if( sprite_id > 0 )
        snprintf(sprite, sizeof(sprite), "%d", sprite_id);
    if( view->id >= 0 )
        snprintf(id, sizeof(id), "%d", (int)view->id);
    else
        snprintf(id, sizeof(id), "-");
    if( any_kind )
        snprintf(name, sizeof(name), "%s %s", ToriRSMapEdCatalog_KindName(view->kind), view->name);
    else
        snprintf(name, sizeof(name), "%s", view->name);
    snprintf(out, (size_t)out_size, "%s%c%s%c%s%c%s%c%s", sprite, CATALOG_CELL_SEP, name, CATALOG_CELL_SEP,
        strcmp(view->display, view->name) == 0 ? "" : view->display, CATALOG_CELL_SEP, keys, CATALOG_CELL_SEP, id);
}

const char*
Editor_CatalogCellsHeader(void)
{
    return "\x1f" "name" "\x1f" "\x1f" "shapes" "\x1f" "id";
}

int
Editor_CatalogViewShapeAt(
    const struct Editor_CatalogView* view,
    int cell)
{
    int shapes[EDITOR_LOC_SHAPE_COUNT];
    int count;

    assert(view);
    if( cell <= 0 || cell > EDITOR_CATALOG_KEYCAPS_MAX )
        return -1;
    count = Editor_LocShapeList(view->shape_mask, shapes);
    if( count > EDITOR_CATALOG_KEYCAPS_MAX && cell == EDITOR_CATALOG_KEYCAPS_MAX )
        return -1; /* the "+n" cap */
    return cell <= count ? shapes[cell - 1] : -1;
}

/* ---- recent picks ---- */

void
Editor_CatalogRecentsPush(
    struct Editor_CatalogRecents* recents,
    const struct Editor_CatalogLocalRow* row)
{
    int at = -1;

    assert(recents);
    assert(row);
    for( int i = 0; i < recents->count && at < 0; i++ )
    {
        const struct Editor_CatalogLocalRow* r = &recents->rows[i];
        if( r->kind == row->kind && (row->id >= 0 ? r->id == row->id : strcmp(r->name, row->name) == 0) )
            at = i;
    }
    if( at < 0 )
        at = recents->count < EDITOR_CATALOG_RECENTS_MAX ? recents->count++ : EDITOR_CATALOG_RECENTS_MAX - 1;
    memmove(&recents->rows[1], &recents->rows[0], sizeof(recents->rows[0]) * (size_t)at);
    recents->rows[0] = *row;
    recents->serial++;
}

/* ---- the Preview's subject ---- */

int
Editor_CatalogPreviewFollow(
    const struct Editor_CatalogPreviewInput* input,
    int* out_kind,
    int* out_id)
{
    int picked;
    int selected;

    assert(input);
    assert(out_kind);
    assert(out_id);
    picked = input->pick_kind >= 0 && input->pick_id >= 0;
    selected = input->selection_count > 0 && input->selection_loc_id >= 0;
    *out_kind = -1;
    *out_id = -1;
    if( selected && !(input->placing && picked) )
    {
        *out_kind = 0; /* a selection is of locs */
        *out_id = input->selection_loc_id;
        return 1;
    }
    if( !picked )
        return 0;
    *out_kind = input->pick_kind;
    *out_id = input->rung_id != -1 ? input->rung_id : input->pick_id;
    return *out_id >= 0;
}

const struct EditorHost_CatalogRecord*
Editor_CatalogModelRecordAt(
    struct Editor_CatalogModel* model,
    struct EditorHost* host,
    int row)
{
    struct Editor_CatalogView view;

    assert(model);
    assert(host);
    assert(row >= 0);
    if( row != model->record_row )
    {
        drop_record(model);
        if( !Editor_CatalogModelViewAt(model, row, &view) )
            return NULL;
        {
            double const t0 = Editor_CatalogModelNowMs();
            model->record_status = Editor_HostMapEdCatalogRecord(host, view.kind, view.id, view.name, &model->record);
            model->last_record_ms = Editor_CatalogModelNowMs() - t0;
        }
        model->record_fetches++;
        if( Editor_CatalogModelStats() )
            TORIRS_REPORT("catalog: record %s %s %.2f ms\n", ToriRSMapEdCatalog_KindName(view.kind), view.name,
                model->last_record_ms);
        model->record_row = row;
        model->record_held = model->record_status == EDITOR_HOST_OK;
    }
    return model->record_held ? &model->record : NULL;
}
