/*
 * The Catalog panel's model: one held CATALOG_QUERY page and one held
 * CATALOG_RECORD, asked again only when what they are keyed on changes
 * (edpanel_catalog_model.h).
 */

#include "edpanel_catalog_model.h"

#include "torirsmaped/torirs_maped_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

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
drop_record(struct Editor_CatalogModel* model)
{
    if( model->record_held )
        Editor_HostCatalogRecordFree(&model->record);
    memset(&model->record, 0, sizeof(model->record));
    model->record_held = 0;
    model->record_row = -1;
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
}

int
Editor_CatalogModelRefresh(
    struct Editor_CatalogModel* model,
    struct EditorHost* host)
{
    struct EditorHost_CatalogPage page;
    enum EditorHost_Status status;

    assert(model);
    assert(host);

    if( model->held && model->held_kind == model->kind && model->held_page == model->page
        && model->held_page_size == model->page_size && strcmp(model->held_filter, model->filter) == 0 )
        return 0;
    if( model->settle > 0 )
    {
        model->settle--;
        return 0;
    }

    memset(&page, 0, sizeof(page));
    status = Editor_HostMapEdCatalogQuery(host, model->kind, model->filter, 0, model->page, model->page_size, &page);
    model->queries++;
    if( model->held )
        Editor_HostCatalogPageFree(&model->result);
    drop_record(model);
    /* Held whatever came back — rows, none, or a refusal — so the same
     * question is not asked again next frame. */
    model->result = page;
    model->status = status;
    model->held = 1;
    model->held_kind = model->kind;
    model->held_page = model->page;
    model->held_page_size = model->page_size;
    snprintf(model->held_filter, sizeof(model->held_filter), "%s", model->filter);
    return 1;
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
        model->record_status = Editor_HostMapEdCatalogRecord(host, r->kind, r->id, r->name, &model->record);
        model->record_fetches++;
        model->record_row = row;
        model->record_held = model->record_status == EDITOR_HOST_OK;
    }
    return model->record_held ? &model->record : NULL;
}
