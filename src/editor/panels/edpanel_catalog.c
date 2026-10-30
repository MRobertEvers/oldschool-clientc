/*
 * The asset catalog panel (docs/MAPED_DESIGN.html #catalog): the SERVER's
 * index of the content tree, a page at a time.
 *
 *   Kind       loc, npc, obj, seq, spotanim, the vars, enum, struct, param,
 *              inv, dbrow/dbtable, interface, component, cs2, trigger,
 *              square, the pack tables, or every kind at once.
 *   Find       incremental: names, display names, ids and key=value filters
 *              (`op=Open`, `multivar=varb4153`), asked once the text has
 *              stood for a frame (edpanel_catalog_model.h).
 *   Results    one page of CATALOG_QUERY, as many rows as the list shows;
 *              Prev / Next page through the rest. The count row reads
 *              "<shown> of <total matching> <kind>" from the server's total.
 *   Record     the picked row's CATALOG_RECORD: its sources (file:line and
 *              provenance) and every field of its config block, a server
 *              overlay's restatement marked over the cache value it
 *              replaced.
 *   Copy       "Copy to new..." copies the picked loc / npc / obj
 *              definition to the typed name with a fresh id (SAVE_AS:
 *              Editor_CopyRecord), one undo step; the copy becomes the pick.
 *
 * The pick is shared tool state (tools.pick_kind / pick_id / pick_name): a
 * loc, npc or obj row with an id becomes it, the Place tool stamps it, the
 * spawns panel resolves an npc / obj pick to its pack symbol. The model well
 * and the multiloc rungs stay on the client's provider (App-bound, and
 * skipped when the panel runs without an App, as in editor_panels_test): a
 * preview is one record loaded on demand, never a sweep.
 */

#include "app.h"
#include "edpanel_catalog_model.h"
#include "edpanel_common.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <stdio.h>
#include <string.h>

#define CATALOG_LABEL_MAX 48
#define CATALOG_VARIANT_MAX 33
/** Result rows on screen, which is also the page size asked for. */
#define CATALOG_LIST_ROWS 12
#define CATALOG_RECORD_ROWS 8

struct CatalogRows
{
    int name;
    int desc;
    int cfg;
    int model;
    int render;
    int ops;
};

struct CatalogPanel
{
    int panel;
    int dropdown_kind;
    int input_search;
    int row_count;
    int list;
    int button_prev;
    int button_next;
    int row_page;
    int row_picked;
    int row_record;
    int record_list;
    int input_new_name;
    int button_copy;
    int row_multi;
    int dropdown_variant;
    int view;
    int item_reset;
    struct CatalogRows var_rows;

    struct Editor_CatalogModel model;
    /** The list shows the model's answer of this query serial. */
    int shown_queries;
    /** The selected result row (-1 none) and the record list built for it. */
    int selected;
    int record_built;

    char var_labels[CATALOG_VARIANT_MAX][CATALOG_LABEL_MAX];
    const char* var_options[CATALOG_VARIANT_MAX];
    int var_ids[CATALOG_VARIANT_MAX];
    int var_count;
    int var_choice;
    int var_shown_id;
    int var_shown_kind;
};

static struct EditorHost*
catalog_host(struct EditorUi* ui)
{
    if( !ui->editor || !ui->editor->host_open || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return NULL;
    return &ui->editor->host;
}

/* ---- the multiloc rungs and the model well (App-bound) ------------------------ */

static void
catalog_rows_hidden(
    struct EditorUi* ui,
    struct CatalogPanel* self,
    int hidden)
{
    ToriRSChrome_SetHidden(ui->chrome, self->row_multi, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->dropdown_variant, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->var_rows.name, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->var_rows.desc, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->var_rows.cfg, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->var_rows.model, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->var_rows.render, hidden);
    ToriRSChrome_SetHidden(ui->chrome, self->var_rows.ops, hidden);
}

static void
catalog_rows_fill(
    struct EditorUi* ui,
    const struct CatalogRows* rows,
    const struct ToriRS_Location* cfg,
    int loc_id)
{
    char line[TORIRS_CHROME_LABEL_MAX];

    snprintf(line, sizeof(line), "%s  (loc %d)", cfg->name[0] ? cfg->name : "<unnamed>", loc_id);
    ToriRSChrome_SetText(ui->chrome, rows->name, line);
    ToriRSChrome_SetText(ui->chrome, rows->desc, cfg->desc[0] ? cfg->desc : "(no examine)");
    snprintf(line, sizeof(line), "size %dx%d  walk %s  proj %s  approach %d", cfg->size_x, cfg->size_z,
        cfg->blocks_walk ? "block" : "-", cfg->blocks_projectiles ? "block" : "-", cfg->force_approach);
    ToriRSChrome_SetText(ui->chrome, rows->cfg, line);
    snprintf(line, sizeof(line), "models %d  seq %d  wallw %d", cfg->shapes_and_model_count, cfg->seq_id,
        cfg->wall_width);
    ToriRSChrome_SetText(ui->chrome, rows->model, line);
    snprintf(line, sizeof(line), "amb %d con %d%s%s%s%s", cfg->ambient, cfg->contrast,
        cfg->sharelight ? "  sharelight" : "", cfg->occlude ? "  occlude" : "",
        cfg->shadowed ? "  shadowed" : "", cfg->contoured_ground ? "  contoured" : "");
    ToriRSChrome_SetText(ui->chrome, rows->render, line);
    line[0] = '\0';
    for( int i = 0; i < TORIRS_MENU_ACTION_SLOTS; i++ )
        if( cfg->actions[i][0] )
        {
            size_t const at = strlen(line);
            snprintf(line + at, sizeof(line) - at, "%s%s", at ? " / " : "", cfg->actions[i]);
        }
    ToriRSChrome_SetText(ui->chrome, rows->ops, line[0] ? line : "(no ops)");
}

static void
catalog_rows_blank(
    struct EditorUi* ui,
    const struct CatalogRows* rows,
    const char* why)
{
    ToriRSChrome_SetText(ui->chrome, rows->name, why);
    ToriRSChrome_SetText(ui->chrome, rows->desc, "");
    ToriRSChrome_SetText(ui->chrome, rows->cfg, "");
    ToriRSChrome_SetText(ui->chrome, rows->model, "");
    ToriRSChrome_SetText(ui->chrome, rows->render, "");
    ToriRSChrome_SetText(ui->chrome, rows->ops, "");
}

static int
catalog_variant_id(const struct CatalogPanel* self)
{
    if( self->var_choice <= 0 || self->var_choice >= self->var_count )
        return -1;
    return self->var_ids[self->var_choice];
}

static void
catalog_variant_readout(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    struct App* app = ui->app;
    int const loc_id = self->var_choice > 0 ? catalog_variant_id(self) : ui->tools.pick_id;
    struct ToriRS_Location* cfg;

    assert(app);
    if( loc_id < 0 )
    {
        catalog_rows_blank(ui, &self->var_rows, "(nothing at this value)");
        return;
    }
    cfg = app->provider ? CacheProvider_LocationGet(app->provider, loc_id) : NULL;
    if( cfg )
        catalog_rows_fill(ui, &self->var_rows, cfg, loc_id);
    else
        catalog_rows_blank(ui, &self->var_rows, "(config not resident)");
}

/** The picked loc's multiloc rungs, from the provider: once per pick. */
static void
catalog_variant_rebuild(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    struct App* app = ui->app;
    struct ToriRS_Location* cfg = NULL;
    char line[TORIRS_CHROME_LABEL_MAX];

    if( self->var_shown_id != ui->tools.pick_id || self->var_shown_kind != ui->tools.pick_kind )
        self->var_choice = 0;
    self->var_shown_id = ui->tools.pick_id;
    self->var_shown_kind = ui->tools.pick_kind;
    self->var_count = 0;

    if( app && app->provider && ui->tools.pick_kind == CACHEPROVIDER_CATALOG_LOC && ui->tools.pick_id >= 0 )
        cfg = CacheProvider_LocationGet(app->provider, ui->tools.pick_id);
    if( !cfg || cfg->transform_count <= 0 || !cfg->transforms )
    {
        self->var_choice = 0;
        ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_variant, NULL, 0, -1);
        catalog_rows_hidden(ui, self, 1);
        return;
    }

    snprintf(self->var_labels[0], CATALOG_LABEL_MAX, "shell: %s (%d)", cfg->name[0] ? cfg->name : "<unnamed>",
        ui->tools.pick_id);
    self->var_options[0] = self->var_labels[0];
    self->var_ids[0] = -1;
    self->var_count = 1;
    for( int i = 0; i < cfg->transform_count && self->var_count < CATALOG_VARIANT_MAX; i++ )
    {
        int const rung = cfg->transforms[i];
        int const row = self->var_count;
        int const fallback = i == cfg->transform_count - 1;
        struct ToriRS_Location* rung_cfg = rung > 0 ? CacheProvider_LocationGet(app->provider, rung) : NULL;
        char key[8];

        if( fallback )
            snprintf(key, sizeof(key), "else");
        else
            snprintf(key, sizeof(key), "%d", i);
        if( rung <= 0 )
            snprintf(self->var_labels[row], CATALOG_LABEL_MAX, "%s: none", key);
        else if( rung_cfg )
            snprintf(self->var_labels[row], CATALOG_LABEL_MAX, "%s: %s (%d)", key,
                rung_cfg->name[0] ? rung_cfg->name : "<unnamed>", rung);
        else
            snprintf(self->var_labels[row], CATALOG_LABEL_MAX, "%s: loc %d (loading)", key, rung);
        self->var_options[row] = self->var_labels[row];
        self->var_ids[row] = rung > 0 ? rung : -1;
        self->var_count++;
        if( rung > 0 && !rung_cfg )
        {
            struct ToriRS_Task* task = CreateTask_LocLoad(app->provider, rung);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
        }
    }
    if( self->var_choice >= self->var_count )
        self->var_choice = 0;
    if( cfg->transform_varbit != -1 )
        snprintf(line, sizeof(line), "multiloc: varbit %d, %d variants", cfg->transform_varbit,
            cfg->transform_count);
    else if( cfg->transform_varp != -1 )
        snprintf(line, sizeof(line), "multiloc: varp %d, %d variants", cfg->transform_varp, cfg->transform_count);
    else
        snprintf(line, sizeof(line), "multiloc: no selector, %d variants", cfg->transform_count);
    ToriRSChrome_SetText(ui->chrome, self->row_multi, line);
    ToriRSChrome_DropdownSetOptions(
        ui->chrome, self->dropdown_variant, self->var_options, self->var_count, self->var_choice);
    catalog_rows_hidden(ui, self, 0);
    catalog_variant_readout(ui, self);
}

/* ---- the list and the record ----------------------------------------------------- */

static void
catalog_picked_label(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];

    if( ui->tools.pick_id >= 0 && ui->tools.pick_kind >= 0 )
        snprintf(line, sizeof(line), "picked %s %s", ToriRSMapEdCatalog_KindName((uint32_t)ui->tools.pick_kind),
            ui->tools.pick_name);
    else
        snprintf(line, sizeof(line), "nothing picked");
    ToriRSChrome_SetText(ui->chrome, self->row_picked, line);
}

/** Lines the record list shows: every source, then every field. */
static int
catalog_record_lines(const struct EditorHost_CatalogRecord* record)
{
    return record ? record->source_count + record->field_count : 0;
}

static void
catalog_record_line(
    const struct EditorHost_CatalogRecord* record,
    int index,
    char* out,
    size_t out_size)
{
    if( index < record->source_count )
    {
        const struct EditorHost_CatalogSource* source = &record->sources[index];
        snprintf(out, out_size, "from %s:%u", source->path, source->line);
        return;
    }
    index -= record->source_count;
    {
        const struct EditorHost_CatalogRecordField* field = &record->fields[index];
        if( field->overridden_value )
            snprintf(out, out_size, "%s=%s   (overlay; cache: %s)", field->key, field->value,
                field->overridden_value);
        else
            snprintf(out, out_size, "%s=%s", field->key, field->value);
    }
}

/** The record list for the selected row: built once per selection. */
static void
catalog_record_show(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    struct EditorHost* host = catalog_host(ui);
    const struct EditorHost_CatalogRecord* record = NULL;
    char line[TORIRS_CHROME_LABEL_MAX];

    self->record_built = self->selected;
    if( host && self->selected >= 0 && self->selected < Editor_CatalogModelRowCount(&self->model) )
        record = Editor_CatalogModelRecord(&self->model, host, self->selected);
    if( record )
        snprintf(line, sizeof(line), "%s %s  id %d%s%s", ToriRSMapEdCatalog_KindName(record->kind), record->name,
            (int)record->id, record->display[0] ? "  " : "", record->display);
    else if( self->selected >= 0 )
        snprintf(line, sizeof(line), "(no record)");
    else
        snprintf(line, sizeof(line), "pick a row for its record");
    ToriRSChrome_SetText(ui->chrome, self->row_record, line);
    ToriRSChrome_VListSetCount(ui->chrome, self->record_list, catalog_record_lines(record));
    ToriRSChrome_VListSetScroll(ui->chrome, self->record_list, 0);
}

static void
catalog_record_rows(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    const struct EditorHost_CatalogRecord* record;
    struct EditorHost* host = catalog_host(ui);
    int first;
    int shown;

    if( !host || self->selected < 0 || self->selected >= Editor_CatalogModelRowCount(&self->model) )
        return;
    if( !ToriRSChrome_VListWindow(ui->chrome, self->record_list, &first, &shown) )
        return;
    /* Held: the same row answers from the model, no round trip. */
    record = Editor_CatalogModelRecord(&self->model, host, self->selected);
    for( int r = first; r < first + shown && r < catalog_record_lines(record); r++ )
    {
        char text[TORIRS_CHROME_INPUT_MAX];
        catalog_record_line(record, r, text, sizeof(text));
        ToriRSChrome_VListSetRow(ui->chrome, self->record_list, r, text);
    }
}

/** The visible result rows' text, from the held page (compare-then-set). */
static void
catalog_list_rows(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    int first;
    int shown;
    int const rows = Editor_CatalogModelRowCount(&self->model);

    if( !ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < rows; r++ )
    {
        char text[TORIRS_CHROME_INPUT_MAX];
        Editor_CatalogModelRowText(&self->model, r, text, sizeof(text));
        ToriRSChrome_VListSetRow(ui->chrome, self->list, r, text);
    }
}

/** The page the model holds, into the list. Once per answer. */
static void
catalog_list_show(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    int const rows = Editor_CatalogModelRowCount(&self->model);

    self->shown_queries = self->model.queries;
    self->selected = -1;
    ToriRSChrome_VListSetCount(ui->chrome, self->list, rows);
    ToriRSChrome_VListSetSelected(ui->chrome, self->list, -1);
    ToriRSChrome_VListSetScroll(ui->chrome, self->list, 0);
    catalog_list_rows(ui, self);
    snprintf(line, sizeof(line), "page %u of %u", self->model.page + 1, Editor_CatalogModelPageCount(&self->model));
    ToriRSChrome_SetText(ui->chrome, self->row_page, line);
    catalog_record_show(ui, self);
}

static void
catalog_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct CatalogPanel* self = opaque;
    struct EditorHost* host;
    const char* search;
    char line[TORIRS_CHROME_LABEL_MAX];
    int const kind_index = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_kind);

    if( !ui->chrome->panels[self->panel].visible )
        return;
    host = catalog_host(ui);
    search = ToriRSChrome_Text(ui->chrome, self->input_search);
    (void)Editor_CatalogModelWant(&self->model,
        Editor_CatalogModelKindAt(kind_index >= 0 && kind_index < Editor_CatalogModelKindCount() ? kind_index : 0),
        search ? search : "");
    if( host )
    {
        /* Asks only when the kind, the settled text or the page changed. */
        (void)Editor_CatalogModelRefresh(&self->model, host);
        if( self->model.queries != self->shown_queries )
            catalog_list_show(ui, self);
        Editor_CatalogModelCountText(&self->model, line, sizeof(line));
    }
    else
        snprintf(line, sizeof(line), "no editor server with a catalog");
    ToriRSChrome_SetText(ui->chrome, self->row_count, line);
    catalog_list_rows(ui, self);
    catalog_record_rows(ui, self);
    if( ui->app && (self->var_shown_id != ui->tools.pick_id || self->var_shown_kind != ui->tools.pick_kind) )
        catalog_variant_rebuild(ui, self);
}

/** The pick follows a selected loc / npc / obj row with an id. */
static void
catalog_pick_row(
    struct EditorUi* ui,
    struct CatalogPanel* self,
    int row)
{
    const struct EditorHost_CatalogRow* r = Editor_CatalogModelRow(&self->model, row);

    if( r->kind > TORIRSMAPED_CATALOG_OBJ )
        return; /* not a placeable kind: the record shows, the pick stays */
    if( r->id < 0 )
    {
        Editor_UiLog(ui, "editor: %s %s has no id yet (a server-only record); nothing to place",
            ToriRSMapEdCatalog_KindName(r->kind), r->name);
        return;
    }
    ui->tools.pick_kind = (int)r->kind;
    ui->tools.pick_id = r->id;
    snprintf(ui->tools.pick_name, sizeof(ui->tools.pick_name), "%s (%d)", r->name, (int)r->id);
    /* A loc pick arms Place: picking one is asking to put it down. */
    if( r->kind == TORIRSMAPED_CATALOG_LOC && ui->tools.tool == EDITOR_TOOL_SELECT )
        Editor_UiSetTool(ui, EDITOR_TOOL_LOC_PLACE);
    {
        int32_t values[2] = { (int32_t)r->kind, r->id };
        (void)Editor_UiPublish(ui, TORIRSMAPED_STATE_CATALOG_PICK, values, 2);
    }
}

/** 1 when `name` can be a record name: [a-z0-9_], and it fits. */
static int
catalog_name_ok(const char* name)
{
    size_t length;

    assert(name);
    length = strlen(name);

    if( length == 0 || length >= EDITOR_CMD_NAME_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
    }
    return 1;
}

int
Editor_Catalog_CopyRecord(
    struct EditorUi* ui,
    uint32_t kind,
    const char* name,
    const char* new_name)
{
    enum EditorHost_Status status;
    const char* type;
    int id = -1;

    assert(ui);
    assert(name);
    assert(new_name);

    if( kind > TORIRSMAPED_CATALOG_OBJ )
    {
        Editor_UiLog(ui, "editor: only a loc, npc or obj definition can be copied");
        return 0;
    }
    if( !catalog_name_ok(new_name) )
    {
        Editor_UiLog(ui, "editor: the new name must be [a-z0-9_], under %d bytes", EDITOR_CMD_NAME_MAX);
        return 0;
    }
    if( !ui->editor || !ui->editor->host_open )
    {
        Editor_UiLog(ui, "editor: no editor server to copy on");
        return 0;
    }
    type = ToriRSMapEdCatalog_KindName(kind);
    status = Editor_CopyRecord(ui->editor, type, name, new_name, &id);
    if( status != EDITOR_HOST_OK )
    {
        Editor_UiLog(ui, "editor: copying %s:%s to %s was refused (%s)", type, name, new_name,
            status == EDITOR_HOST_REFUSED  ? "the name is taken or not a name"
            : status == EDITOR_HOST_ABSENT ? "no such record"
                                           : "the server could not");
        return 0;
    }
    ui->tools.pick_kind = (int)kind;
    ui->tools.pick_id = id;
    snprintf(ui->tools.pick_name, sizeof(ui->tools.pick_name), "%s (%d)", new_name, id);
    {
        int32_t values[2] = { (int32_t)kind, id };
        (void)Editor_UiPublish(ui, TORIRSMAPED_STATE_CATALOG_PICK, values, 2);
    }
    Editor_UiLog(ui, "editor: copied %s:%s to %s:%s, id %d (unsaved; Save writes it, RUN builds it)", type, name,
        type, new_name, id);
    return 1;
}

static void
catalog_copy(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    const struct EditorHost_CatalogRow* r;
    const char* new_name = ToriRSChrome_Text(ui->chrome, self->input_new_name);

    if( self->selected < 0 || self->selected >= Editor_CatalogModelRowCount(&self->model) )
    {
        Editor_UiLog(ui, "editor: pick the loc, npc or obj to copy first");
        return;
    }
    r = Editor_CatalogModelRow(&self->model, self->selected);
    if( !Editor_Catalog_CopyRecord(ui, r->kind, r->name, new_name ? new_name : "") )
        return;
    /* The copy is unsaved, so the index (which reads the tree) does not hold
     * it yet; a save makes it findable. Ask again then. */
    Editor_CatalogModelInvalidate(&self->model);
    catalog_picked_label(ui, self);
    {
        char line[TORIRS_CHROME_LABEL_MAX];
        snprintf(line, sizeof(line), "copy: %s", ui->tools.pick_name);
        ToriRSChrome_SetText(ui->chrome, self->row_record, line);
    }
}

static int
catalog_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct CatalogPanel* self = opaque;

    if( widget == self->dropdown_kind )
    {
        /* Choosing a kind again asks again: the tree may have changed. */
        Editor_CatalogModelInvalidate(&self->model);
        return 1;
    }
    if( widget == self->list )
    {
        int const choice = ToriRSChrome_VListSelected(ui->chrome, widget);
        self->selected = choice >= 0 && choice < Editor_CatalogModelRowCount(&self->model) ? choice : -1;
        if( self->selected >= 0 )
            catalog_pick_row(ui, self, self->selected);
        catalog_picked_label(ui, self);
        catalog_record_show(ui, self);
        return 1;
    }
    if( widget == self->button_prev || widget == self->button_next )
    {
        uint32_t page = self->model.page;
        if( widget == self->button_next )
            page++;
        else if( page > 0 )
            page--;
        Editor_CatalogModelSetPage(&self->model, page);
        return 1;
    }
    if( widget == self->button_copy )
    {
        catalog_copy(ui, self);
        return 1;
    }
    if( widget == self->dropdown_variant )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        self->var_choice = choice >= 0 && choice < self->var_count ? choice : 0;
        if( ui->app )
            catalog_variant_readout(ui, self);
        return 1;
    }
    if( widget == self->item_reset )
    {
        struct App* app = ui->app;
        if( app )
            EditorPreviewCamera_Invalidate(&app->preview_camera, false);
        return 1;
    }
    return widget == self->input_search || widget == self->view || widget == self->record_list
           || widget == self->input_new_name;
}

static void
catalog_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct CatalogPanel* self = opaque;
    (void)ui;
    Editor_CatalogModelFree(&self->model);
    ToriMem_Free(ToriMem_Editor(), self);
}

int
Editor_Catalog_PreviewId(struct EditorUi* ui)
{
    struct CatalogPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self || ui->tools.pick_kind < 0 )
        return -1;
    if( ui->tools.pick_kind != CACHEPROVIDER_CATALOG_LOC )
        return ui->tools.pick_id;
    if( self->var_count > 0 && self->var_choice > 0 )
        return catalog_variant_id(self);
    return ui->tools.pick_id;
}

int
Editor_Catalog_ModelView(struct EditorUi* ui)
{
    struct CatalogPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    return self ? self->view : -1;
}

void
Editor_Catalog_Adopt(
    struct EditorUi* ui,
    int loc_id)
{
    struct CatalogPanel* self;
    struct App* app;
    struct ToriRS_Location* cfg;

    assert(ui);
    app = ui->app;
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self || loc_id < 0 )
        return;
    ui->tools.pick_kind = CACHEPROVIDER_CATALOG_LOC;
    ui->tools.pick_id = loc_id;
    cfg = app && app->provider ? CacheProvider_LocationGet(app->provider, loc_id) : NULL;
    snprintf(ui->tools.pick_name, sizeof(ui->tools.pick_name), "%s (%d)",
        cfg && cfg->name[0] ? cfg->name : "<unnamed>", loc_id);
    catalog_picked_label(ui, self);
    /* Show it: kind loc, found by id (one query, when the panel next ticks). */
    ToriRSChrome_DropdownSetSelected(
        ui->chrome, self->dropdown_kind, Editor_CatalogModelKindIndex(TORIRSMAPED_CATALOG_LOC));
    {
        char id_text[16];
        snprintf(id_text, sizeof(id_text), "%d", loc_id);
        ToriRSChrome_SetText(ui->chrome, self->input_search, id_text);
    }
}

void
Editor_Catalog_Register(struct EditorUi* ui)
{
    struct CatalogPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    Editor_CatalogModelInit(&self->model, TORIRSMAPED_CATALOG_LOC, CATALOG_LIST_ROWS);
    self->shown_queries = -1;
    self->selected = -1;
    self->record_built = -1;
    self->var_shown_id = -1;
    self->var_shown_kind = -1;

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8), edp_px(ui, 420),
        edp_px(ui, 240), "Catalog");
    assert(self->panel >= 0);
    self->dropdown_kind = ToriRSChrome_Dropdown(chrome, self->panel, "Kind", Editor_CatalogModelKindLabels(),
        Editor_CatalogModelKindCount(), 0);
    self->input_search = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(chrome, self->input_search, "name, display name, id, or key=value (op=Open)");
    self->row_count = ToriRSChrome_Label(chrome, self->panel, "");
    self->list = ToriRSChrome_VList(chrome, self->panel, "", CATALOG_LIST_ROWS, 0);
    self->button_prev = ToriRSChrome_Button(chrome, self->panel, "Prev page");
    self->button_next = ToriRSChrome_Button(chrome, self->panel, "Next page");
    self->row_page = ToriRSChrome_Label(chrome, self->panel, "");
    ToriRSChrome_Separator(chrome, self->panel);
    self->row_picked = ToriRSChrome_Label(chrome, self->panel, "nothing picked");
    self->row_record = ToriRSChrome_Label(chrome, self->panel, "pick a row for its record");
    self->record_list = ToriRSChrome_VList(chrome, self->panel, "", CATALOG_RECORD_ROWS, 0);
    self->input_new_name = ToriRSChrome_TextInput(chrome, self->panel, "New name", "");
    self->button_copy = ToriRSChrome_Button(chrome, self->panel, "Copy to new...");
    ToriRSChrome_SetTooltip(chrome, self->button_copy,
        "Copy the picked loc/npc/obj to New name, fresh id");
    self->row_multi = ToriRSChrome_Label(chrome, self->panel, "");
    self->dropdown_variant = ToriRSChrome_Dropdown(chrome, self->panel, "", NULL, 0, -1);
    self->view = ToriRSChrome_ModelView(chrome, self->panel, 120, 96);
    self->item_reset = ToriRSChrome_MenuItem(chrome, self->panel, "Reset view");
    self->var_rows.name = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.desc = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.cfg = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.model = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.render = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.ops = ToriRSChrome_Label(chrome, self->panel, "");
    catalog_rows_hidden(ui, self, 1);
    if( !ui->app )
    {
        /* No provider to preview with (a headless test). */
        ToriRSChrome_SetHidden(chrome, self->view, 1);
        ToriRSChrome_SetHidden(chrome, self->item_reset, 1);
    }
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "catalog";
    desc.title = "Catalog";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = catalog_tick;
    desc.activate = catalog_activate;
    desc.destroy = catalog_destroy;
    Editor_UiAddPanel(ui, &desc);
}

/* ---- for the tests ------------------------------------------------------------- */

const struct Editor_CatalogModel*
Editor_Catalog_Model(struct EditorUi* ui)
{
    struct CatalogPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    return self ? &self->model : NULL;
}
