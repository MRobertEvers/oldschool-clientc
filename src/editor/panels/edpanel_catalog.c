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
 *   Edit       "Edit record..." opens the selected loc / npc / obj in the
 *              RECORD panel (below, registered with this one): the record
 *              across every file that states it, every field editable.
 *   Copy       "Copy to new..." copies the picked loc / npc / obj
 *              definition to the typed name with a fresh id (SAVE_AS:
 *              Editor_CopyRecord), one undo step; the copy becomes the pick.
 *
 * The pick is shared tool state (tools.pick_kind / pick_id / pick_name): a
 * loc, npc or obj row with an id becomes it, the Place tool stamps it, the
 * spawns panel resolves an npc / obj pick to its pack symbol, and the PREVIEW
 * panel (edpanel_preview.c, registered with this one) shows its model. It is
 * also the Client's: every pick is published as STATE_CATALOG_PICK and heard
 * back (catalog_on_state), so a popped-out Preview or Catalog follows the
 * window that picked. The multiloc rungs stay on the client's provider,
 * reached through ui->app_services (skipped when they are NULL, as in
 * editor_panels_test): one record loaded on demand, never a sweep.
 */

#include "edpanel_catalog_model.h"
#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/editor_cfg.h"
#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "editor/editor_script.h"
#include "editor/map_editor_ghost.h"
#include "log/torirs_log.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CATALOG_LABEL_MAX 48
#define CATALOG_VARIANT_MAX 33
/** Result rows on screen, which is also the page size asked for. */
#define CATALOG_LIST_ROWS 12
#define CATALOG_RECORD_ROWS 8
/** Picks published whose echo has not come back yet. */
#define CATALOG_PICK_ECHO_MAX 8
/** TORIRS_EDITOR_CATALOG pick= entries. */
#define CATALOG_ENV_PICKS 8

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
    int button_edit_record;
    int button_open_script;
    int row_multi;
    int dropdown_variant;
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

    /** The pick relay (STATE_CATALOG_PICK): this window's publishes whose
     *  echo is still to come, oldest first; a heard pick's name, to be asked
     *  of the catalog on the next tick. */
    int32_t pick_echo[CATALOG_PICK_ECHO_MAX][2];
    int pick_echo_count;
    int pick_name_pending;
    /** Editor_Catalog_PickRecord: the row to select once the page holding it
     *  lands ("" none). */
    char select_name[EDITOR_CMD_NAME_MAX];
    uint32_t select_kind;

    /* TORIRS_EDITOR_CATALOG (catalog_env_tick) and the frame meter
     * (TORIRS_CATALOG_STATS). */
    int env_read;
    int env_frames;
    int env_open_frame;
    int env_every;
    uint32_t env_kind;
    char env_find[64];
    int env_typed;
    int env_pick_count;
    int env_pick_row[CATALOG_ENV_PICKS];
    int env_pick_frame[CATALOG_ENV_PICKS];
    int env_pick_next;
    int env_edit_frame;
    int env_next_frame;
    int env_last_frame;
    double meter_last_ms;
};

static void
catalog_pick_name(
    struct EditorUi* ui,
    struct CatalogPanel* self);

static struct EditorHost*
catalog_host(struct EditorUi* ui)
{
    if( !ui->editor || !ui->editor->host_open || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return NULL;
    return &ui->editor->host;
}

/* ---- the multiloc rungs and the model well (App-bound: ui->app_services) ------ */

/** Loc record `loc_id` through the App's services; NULL when it is not
 *  resident or the window has no App to ask. */
static struct ToriRS_Location*
catalog_loc_record(
    struct EditorUi* ui,
    int loc_id)
{
    const struct EditorUiAppServices* services = &ui->app_services;
    return services->loc_record ? services->loc_record(services->user_data, loc_id) : NULL;
}

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
    int const loc_id = self->var_choice > 0 ? catalog_variant_id(self) : ui->tools.pick_id;
    struct ToriRS_Location* cfg;

    if( loc_id < 0 )
    {
        catalog_rows_blank(ui, &self->var_rows, "(nothing at this value)");
        return;
    }
    cfg = catalog_loc_record(ui, loc_id);
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
    const struct EditorUiAppServices* services = &ui->app_services;
    struct ToriRS_Location* cfg = NULL;
    char line[TORIRS_CHROME_LABEL_MAX];

    if( self->var_shown_id != ui->tools.pick_id || self->var_shown_kind != ui->tools.pick_kind )
        self->var_choice = 0;
    self->var_shown_id = ui->tools.pick_id;
    self->var_shown_kind = ui->tools.pick_kind;
    self->var_count = 0;

    if( ui->tools.pick_kind == CACHEPROVIDER_CATALOG_LOC && ui->tools.pick_id >= 0 )
        cfg = catalog_loc_record(ui, ui->tools.pick_id);
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
        struct ToriRS_Location* rung_cfg = rung > 0 ? catalog_loc_record(ui, rung) : NULL;
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
        if( rung > 0 && !rung_cfg && services->loc_request )
            services->loc_request(services->user_data, rung);
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
    /* Editor_Catalog_PickRecord's row, once a page holding it lands. */
    if( self->select_name[0] )
        for( int r = 0; r < rows; r++ )
        {
            const struct EditorHost_CatalogRow* row = Editor_CatalogModelRow(&self->model, r);
            if( row->kind == self->select_kind && strcmp(row->name, self->select_name) == 0 )
            {
                self->select_name[0] = '\0';
                self->selected = r;
                ToriRSChrome_VListSetSelected(ui->chrome, self->list, r);
                break;
            }
        }
    catalog_record_show(ui, self);
}

/* ---- TORIRS_EDITOR_CATALOG -------------------------------------------------------
 *
 * TORIRS_EDITOR_CATALOG=<kind>:<find>[@<frame>][,every=<frames>][,pick=<row>@<frame>...]
 *                       [,next=<frame>][,edit=<frame>]
 * (measurements and photographs; main window only): at editor frame <frame>
 * (default 60) open the Catalog on <kind>, then type <find> into Find one
 * character every <frames> frames (default 8, a quick typist at 60 Hz), and
 * at each pick frame select result row <row> as a click on it does; `next`
 * presses Next page, `edit` presses Edit record... on the selection. With
 * TORIRS_CATALOG_STATS=1 every frame from the open to 30 frames after the
 * last action prints its wall time (catalog_meter). */

static void
catalog_env_read(struct CatalogPanel* self)
{
    const char* value = getenv("TORIRS_EDITOR_CATALOG");
    char spec[256];
    char* next;
    char* entry;

    self->env_read = 1;
    self->env_open_frame = -1;
    if( !value || !value[0] )
        return;
    snprintf(spec, sizeof(spec), "%s", value);
    entry = strtok_r(spec, ",", &next);
    if( !entry )
        return;
    {
        char kind[16] = { 0 };
        char* at = strchr(entry, '@');
        char* colon;
        self->env_open_frame = at ? atoi(at + 1) : 60;
        if( at )
            *at = '\0';
        colon = strchr(entry, ':');
        if( colon )
        {
            *colon = '\0';
            snprintf(self->env_find, sizeof(self->env_find), "%s", colon + 1);
        }
        snprintf(kind, sizeof(kind), "%s", entry);
        self->env_kind = TORIRSMAPED_CATALOG_ANY;
        for( int i = 0; i < Editor_CatalogModelKindCount() - 1; i++ )
            if( strcmp(ToriRSMapEdCatalog_KindName(Editor_CatalogModelKindAt(i)), kind) == 0 )
                self->env_kind = Editor_CatalogModelKindAt(i);
    }
    self->env_every = 8;
    self->env_edit_frame = -1;
    self->env_next_frame = -1;
    while( (entry = strtok_r(NULL, ",", &next)) )
    {
        if( strncmp(entry, "edit=", 5) == 0 )
            self->env_edit_frame = atoi(entry + 5);
        else if( strncmp(entry, "next=", 5) == 0 )
            self->env_next_frame = atoi(entry + 5);
        else if( strncmp(entry, "every=", 6) == 0 )
            self->env_every = atoi(entry + 6) > 0 ? atoi(entry + 6) : 1;
        else if( strncmp(entry, "pick=", 5) == 0 && self->env_pick_count < CATALOG_ENV_PICKS )
        {
            char* at = strchr(entry, '@');
            self->env_pick_row[self->env_pick_count] = atoi(entry + 5);
            self->env_pick_frame[self->env_pick_count] = at ? atoi(at + 1) : 0;
            self->env_pick_count++;
        }
    }
    self->env_last_frame = self->env_open_frame + 1 + (int)strlen(self->env_find) * self->env_every;
    for( int i = 0; i < self->env_pick_count; i++ )
        if( self->env_pick_frame[i] > self->env_last_frame )
            self->env_last_frame = self->env_pick_frame[i];
    if( self->env_edit_frame > self->env_last_frame )
        self->env_last_frame = self->env_edit_frame;
    if( self->env_next_frame > self->env_last_frame )
        self->env_last_frame = self->env_next_frame;
}

static int
catalog_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget);

static void
catalog_env_tick(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    int f;

    if( !self->env_read )
        catalog_env_read(self);
    if( self->env_open_frame < 0 || ui->solo_panel >= 0 )
        return;
    f = ++self->env_frames;
    if( f == self->env_open_frame )
    {
        if( !ui->chrome->panels[self->panel].visible )
            (void)Editor_UiTogglePanel(ui, "catalog");
        ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_kind, Editor_CatalogModelKindIndex(self->env_kind));
        ToriRSChrome_SetText(ui->chrome, self->input_search, "");
        Editor_UiLog(ui, "editor: TORIRS_EDITOR_CATALOG opens %s", getenv("TORIRS_EDITOR_CATALOG"));
    }
    if( f > self->env_open_frame && (f - self->env_open_frame) % self->env_every == 0
        && self->env_typed < (int)strlen(self->env_find) )
    {
        char typed[64];
        self->env_typed++;
        snprintf(typed, sizeof(typed), "%.*s", self->env_typed, self->env_find);
        ToriRSChrome_SetText(ui->chrome, self->input_search, typed);
        if( Editor_CatalogModelStats() )
            TORIRS_REPORT("catalog: frame %d types '%s'\n", f, typed);
    }
    while( self->env_pick_next < self->env_pick_count && f >= self->env_pick_frame[self->env_pick_next] )
    {
        int const row = self->env_pick_row[self->env_pick_next++];
        if( row < 0 || row >= Editor_CatalogModelRowCount(&self->model) )
        {
            Editor_UiLog(ui, "editor: TORIRS_EDITOR_CATALOG pick=%d: the page has %d rows", row,
                Editor_CatalogModelRowCount(&self->model));
            continue;
        }
        if( Editor_CatalogModelStats() )
            TORIRS_REPORT("catalog: frame %d picks row %d\n", f, row);
        ToriRSChrome_VListSetSelected(ui->chrome, self->list, row);
        (void)catalog_activate(ui, self, self->list);
    }
    if( f == self->env_next_frame )
    {
        if( Editor_CatalogModelStats() )
            TORIRS_REPORT("catalog: frame %d presses Next page\n", f);
        (void)catalog_activate(ui, self, self->button_next);
    }
    if( f == self->env_edit_frame )
    {
        double const t0 = Editor_CatalogModelNowMs();
        (void)catalog_activate(ui, self, self->button_edit_record);
        if( Editor_CatalogModelStats() )
            TORIRS_REPORT("catalog: frame %d Edit record... %.2f ms\n", f, Editor_CatalogModelNowMs() - t0);
    }
}

/** TORIRS_CATALOG_STATS=1: the wall time between this tick and the last, i.e.
 *  the frame that just ran, for every frame the env drives and any frame
 *  over 33 ms (the warm build's slices included). */
static void
catalog_meter(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    double const now = Editor_CatalogModelNowMs();
    double const dt = self->meter_last_ms > 0.0 ? now - self->meter_last_ms : 0.0;
    int const driven = self->env_open_frame >= 0 && self->env_frames >= self->env_open_frame
                       && self->env_frames <= self->env_last_frame + 30;

    self->meter_last_ms = now;
    if( !Editor_CatalogModelStats() || dt <= 0.0 )
        return;
    (void)ui;
    if( driven || dt > 33.0 )
        TORIRS_REPORT("catalog: frame %d %.2f ms\n", self->env_frames, dt);
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
    int kind_index;

    catalog_meter(ui, self);
    catalog_env_tick(ui, self);
    kind_index = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_kind);
    /* A pick heard from another window: named whether or not this panel is
     * open (a popped-out Preview shows the name). */
    if( self->pick_name_pending )
        catalog_pick_name(ui, self);
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
    if( ui->app_services.loc_record && (self->var_shown_id != ui->tools.pick_id || self->var_shown_kind != ui->tools.pick_kind) )
        catalog_variant_rebuild(ui, self);
}

/** Publish the pick to the Client, noting the echo to come (catalog_on_state
 *  consumes it). A window with no catalog panel publishes untracked: its
 *  echo is the same pick and lands as a no-op. */
static void
catalog_publish_pick(struct EditorUi* ui)
{
    struct CatalogPanel* self = Editor_UiPanelSelf(ui, "catalog");
    int32_t const values[2] = { (int32_t)ui->tools.pick_kind, ui->tools.pick_id };

    if( !Editor_UiPublish(ui, TORIRSMAPED_STATE_CATALOG_PICK, values, 2) || !self )
        return;
    if( self->pick_echo_count == CATALOG_PICK_ECHO_MAX )
    {
        /* A full queue of publishes with no echo: the oldest is not coming back. */
        memmove(self->pick_echo[0], self->pick_echo[1], sizeof(self->pick_echo) - sizeof(self->pick_echo[0]));
        self->pick_echo_count--;
    }
    self->pick_echo[self->pick_echo_count][0] = values[0];
    self->pick_echo[self->pick_echo_count][1] = values[1];
    self->pick_echo_count++;
}

/**
 * A STATE_CATALOG_PICK fact. The server relays every publish to the whole
 * Client in one order, the publisher included, so while this window waits for
 * its own echo anything it hears was ordered before that publish and is
 * superseded by it (the shared tools' rule, editor_ui.c ui_tools_heard). A
 * pick heard with no echo pending becomes this window's; its name is asked of
 * the catalog on the next tick (not from inside the fact pump).
 */
static void
catalog_on_state(
    struct EditorUi* ui,
    void* opaque,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct CatalogPanel* self = opaque;

    if( key != TORIRSMAPED_STATE_CATALOG_PICK || count < 2 )
        return;
    if( self->pick_echo_count > 0 )
    {
        if( values[0] == self->pick_echo[0][0] && values[1] == self->pick_echo[0][1] )
        {
            self->pick_echo_count--;
            memmove(self->pick_echo[0], self->pick_echo[1], sizeof(self->pick_echo[0]) * (size_t)self->pick_echo_count);
        }
        return;
    }
    /* Wire data, not a caller: a kind this window cannot preview is ignored. */
    if( values[0] < (int32_t)TORIRSMAPED_CATALOG_LOC || values[0] > (int32_t)TORIRSMAPED_CATALOG_OBJ || values[1] < 0 )
        return;
    if( ui->tools.pick_kind == values[0] && ui->tools.pick_id == values[1] )
        return;
    ui->tools.pick_kind = values[0];
    ui->tools.pick_id = values[1];
    snprintf(ui->tools.pick_name, sizeof(ui->tools.pick_name), "#%d", (int)values[1]);
    self->pick_name_pending = 1;
    catalog_picked_label(ui, self);
}

/** A heard pick's name, from the catalog's record of it: once per pick. */
static void
catalog_pick_name(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    struct EditorHost* host = catalog_host(ui);
    struct EditorHost_CatalogRecord record;

    self->pick_name_pending = 0;
    if( !host || ui->tools.pick_kind < 0 || ui->tools.pick_id < 0 )
        return;
    memset(&record, 0, sizeof(record));
    if( Editor_HostMapEdCatalogRecord(host, (uint32_t)ui->tools.pick_kind, ui->tools.pick_id, "", &record)
            == EDITOR_HOST_OK
        && record.name && record.name[0] )
    {
        snprintf(ui->tools.pick_name, sizeof(ui->tools.pick_name), "%s (%d)", record.name, ui->tools.pick_id);
        catalog_picked_label(ui, self);
    }
    Editor_HostCatalogRecordFree(&record);
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
    catalog_publish_pick(ui);
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
    catalog_publish_pick(ui);
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
    if( widget == self->button_edit_record )
    {
        const struct EditorHost_CatalogRow* r;
        if( self->selected < 0 || self->selected >= Editor_CatalogModelRowCount(&self->model) )
        {
            Editor_UiLog(ui, "editor: pick the loc, npc or obj to edit first");
            return 1;
        }
        r = Editor_CatalogModelRow(&self->model, self->selected);
        (void)Editor_Record_Open(ui, r->kind, r->name);
        return 1;
    }
    if( widget == self->button_open_script )
    {
        const struct EditorHost_CatalogRow* r = self->selected >= 0
                                                        && self->selected < Editor_CatalogModelRowCount(&self->model)
                                                    ? Editor_CatalogModelRow(&self->model, self->selected)
                                                    : NULL;
        const char* file = NULL;
        const char* line = NULL;
        char rel[EDITOR_SCRIPT_PATH_MAX];
        if( !r || r->kind != TORIRSMAPED_CATALOG_TRIGGER )
        {
            Editor_UiLog(ui, "editor: Open in editor: pick a trigger row (Kind: trigger)");
            return 1;
        }
        for( int f = 0; f < r->field_count; f++ )
        {
            if( strcmp(r->fields[f].key, "file") == 0 )
                file = r->fields[f].value;
            else if( strcmp(r->fields[f].key, "line") == 0 )
                line = r->fields[f].value;
        }
        if( !file || !Editor_ScriptRelFromContentPath(file, rel, sizeof(rel)) || !Editor_UiPanelSelf(ui, "triggers") )
        {
            Editor_UiLog(ui, "editor: Open in editor: %s is not a script this window can open", file ? file : "?");
            return 1;
        }
        (void)Editor_Triggers_OpenInEditor(ui, rel, line ? atoi(line) : 0);
        return 1;
    }
    if( widget == self->dropdown_variant )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        self->var_choice = choice >= 0 && choice < self->var_count ? choice : 0;
        if( ui->app_services.loc_record )
            catalog_variant_readout(ui, self);
        return 1;
    }
    return widget == self->input_search || widget == self->record_list || widget == self->input_new_name;
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
    /* The rung only while the rungs are this pick's (they are rebuilt when
     * the panel next ticks open; a closed panel keeps an older pick's). */
    if( self->var_count > 0 && self->var_choice > 0 && self->var_shown_id == ui->tools.pick_id
        && self->var_shown_kind == ui->tools.pick_kind )
        return catalog_variant_id(self);
    return ui->tools.pick_id;
}

int
Editor_Catalog_PickRecord(
    struct EditorUi* ui,
    uint32_t kind,
    const char* name)
{
    struct CatalogPanel* self;
    struct EditorHost* host;
    struct EditorHost_CatalogRecord record;
    int id = -1;

    assert(ui);
    assert(name);
    assert(kind <= TORIRSMAPED_CATALOG_OBJ);
    self = Editor_UiPanelSelf(ui, "catalog");
    host = catalog_host(ui);
    if( !self || !host )
    {
        Editor_UiLog(ui, "editor: no catalog to pick %s %s from", ToriRSMapEdCatalog_KindName(kind), name);
        return 0;
    }
    memset(&record, 0, sizeof(record));
    if( Editor_HostMapEdCatalogRecord(host, kind, -1, name, &record) == EDITOR_HOST_OK )
        id = record.id;
    Editor_HostCatalogRecordFree(&record);
    if( id < 0 )
    {
        Editor_UiLog(ui, "editor: the catalog has no %s %s with an id", ToriRSMapEdCatalog_KindName(kind), name);
        return 0;
    }
    ui->tools.pick_kind = (int)kind;
    ui->tools.pick_id = id;
    snprintf(ui->tools.pick_name, sizeof(ui->tools.pick_name), "%s (%d)", name, id);
    if( kind == TORIRSMAPED_CATALOG_LOC && ui->tools.tool == EDITOR_TOOL_SELECT )
        Editor_UiSetTool(ui, EDITOR_TOOL_LOC_PLACE);
    catalog_publish_pick(ui);
    catalog_picked_label(ui, self);
    /* Shown in the list: that kind, found by name, its row selected when the
     * page lands (catalog_list_show). */
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_kind, Editor_CatalogModelKindIndex(kind));
    ToriRSChrome_SetText(ui->chrome, self->input_search, name);
    snprintf(self->select_name, sizeof(self->select_name), "%s", name);
    self->select_kind = kind;
    return 1;
}

void
Editor_Catalog_Adopt(
    struct EditorUi* ui,
    int loc_id)
{
    struct CatalogPanel* self;
    struct ToriRS_Location* cfg;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self || loc_id < 0 )
        return;
    ui->tools.pick_kind = CACHEPROVIDER_CATALOG_LOC;
    ui->tools.pick_id = loc_id;
    cfg = catalog_loc_record(ui, loc_id);
    snprintf(ui->tools.pick_name, sizeof(ui->tools.pick_name), "%s (%d)",
        cfg && cfg->name[0] ? cfg->name : "<unnamed>", loc_id);
    catalog_publish_pick(ui);
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
    self->button_edit_record = ToriRSChrome_Button(chrome, self->panel, "Edit record...");
    ToriRSChrome_SetTooltip(chrome, self->button_edit_record,
        "every field of the picked loc/npc/obj");
    self->button_open_script = ToriRSChrome_Button(chrome, self->panel, "Open in editor");
    ToriRSChrome_SetTooltip(chrome, self->button_open_script, "the picked trigger's file at its line, in your editor");
    self->input_new_name = ToriRSChrome_TextInput(chrome, self->panel, "New name", "");
    self->button_copy = ToriRSChrome_Button(chrome, self->panel, "Copy to new...");
    ToriRSChrome_SetTooltip(chrome, self->button_copy,
        "Copy the picked loc/npc/obj to New name, fresh id");
    self->row_multi = ToriRSChrome_Label(chrome, self->panel, "");
    self->dropdown_variant = ToriRSChrome_Dropdown(chrome, self->panel, "", NULL, 0, -1);
    self->var_rows.name = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.desc = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.cfg = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.model = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.render = ToriRSChrome_Label(chrome, self->panel, "");
    self->var_rows.ops = ToriRSChrome_Label(chrome, self->panel, "");
    catalog_rows_hidden(ui, self, 1);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "catalog";
    desc.title = "Catalog";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = catalog_tick;
    desc.activate = catalog_activate;
    desc.on_state = catalog_on_state;
    desc.destroy = catalog_destroy;
    Editor_UiAddPanel(ui, &desc);

    /* The Preview shows the catalog's pick, and sits next to it in the View
     * menu and the dock order. */
    if( !Editor_UiPanelSelf(ui, "preview") )
        Editor_Preview_Register(ui);
    /* The record editor travels with the catalog it opens records from. */
    if( !Editor_UiPanelSelf(ui, "record") )
        Editor_Record_Register(ui);
}


/* ---- the Record panel: one config record, every field -----------------------------
 *
 * A loc / npc / obj record is not one block. Its fields are spread over the
 * cache half (configs/all.<type>) and every server overlay that states the
 * record -- the generated layer (_explicit/configs/server_fields.generated.<type>)
 * and authored files (areas/<area>/configs/<file>.<type>) -- and the pack the
 * server reads merges them in a fixed order (editor_cfg.h, "a record across
 * its layers"). This panel opens one CFG document per layer (the overlay ones
 * as "<type>:<name>@<overlay>", block-scoped: the 10 MB generated file is
 * never loaded whole), resolves each field to the layer that wins it, and
 * lists every field of every layer plus every key of the field register
 * (fields/<type>.ini, as the TEXT document "fields:<type>.ini") the record
 * states nowhere.
 *
 *   select a row   its field and winning value fill Field / Value; the row
 *                  says which file holds it and how many beneath it are
 *                  shadowed.
 *   Set field      writes Value into the file that holds the field (Write to:
 *                  "where it is stated"), or into the chosen layer -- an
 *                  "override in <overlay>" for a field a lower layer states,
 *                  or the place a field nobody states is added (the record's
 *                  top layer by default). One undo step ("edit record").
 *   Remove field   takes the statement out of that file; a lower layer's
 *                  value (or the engine default) shows through.
 *
 * Field names: `key`, `key#N` for the N-th statement of a key a layer
 * repeats, and `param:<name>` for a param (`param=<name>,<value>`), which the
 * merge treats per param name. The layers come from the server's catalog
 * (the record's sources); without one only configs/all.<type> is edited.
 */

#define RECORD_LIST_ROWS 14

struct RecordPanel
{
    int panel;
    int row_title;
    int list;
    int input_field;
    int input_value;
    int dropdown_layer;
    int button_set;
    int button_remove;
    int row_hint;
    /* The Triggers panel on this record. */
    int button_triggers;

    int open;
    uint32_t kind;
    char type[16];
    char name[EDITOR_CMD_NAME_MAX];
    char overlays[EDITOR_CFG_RECORD_LAYERS_MAX][EDITOR_DOC_KEY_MAX];
    int overlay_count;
    struct Editor_CfgRecord record;
    struct Editor_CfgRegisterField fields[EDITOR_CFG_REGISTER_MAX];
    int field_count;
    /** The fact seq the rows were resolved at. */
    uint32_t built_seq;
    int built;

    char layer_labels[EDITOR_CFG_RECORD_LAYERS_MAX + 1][TORIRS_CHROME_LABEL_MAX];
    const char* layer_options[EDITOR_CFG_RECORD_LAYERS_MAX + 1];
};

static const char*
record_rank_name(int rank)
{
    return rank == 0 ? "cache" : rank == 1 ? "generated" : "authored";
}

static const char*
record_basename(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/** Re-read every layer's document and resolve the rows. */
static void
record_resolve(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    Editor_CfgRecordFree(&self->record);
    Editor_CfgRecordInit(&self->record, self->type, self->name);
    for( int i = -1; i < self->overlay_count; i++ )
    {
        struct Editor_DocHandle handle;
        char key[EDITOR_DOC_KEY_MAX];
        const char* overlay = i < 0 ? NULL : self->overlays[i];
        const char* text = "";
        size_t length = 0;

        if( !Editor_CfgOverlayKey(key, self->type, self->name, overlay) )
            continue;
        if( ui->editor && Editor_DocFind(&ui->editor->doc, EDITOR_DOC_CFG, key, &handle) && handle.text
            && handle.text->text )
        {
            text = handle.text->text;
            length = handle.text->length;
        }
        (void)Editor_CfgRecordAddLayer(&self->record, overlay, text, length);
    }
    Editor_CfgRecordResolve(&self->record, self->fields, self->field_count);
    self->built_seq = ui->editor ? ui->editor->last_cmd_seq : 0;
    self->built = 1;

    snprintf(self->layer_labels[0], sizeof(self->layer_labels[0]), "where it is stated");
    self->layer_options[0] = self->layer_labels[0];
    for( int l = 0; l < self->record.layer_count; l++ )
    {
        const struct Editor_CfgRecordLayer* layer = &self->record.layers[l];
        snprintf(self->layer_labels[l + 1], sizeof(self->layer_labels[l + 1]), "%s %s (%s)",
            layer->present ? "in" : "override in", record_basename(layer->label), record_rank_name(layer->rank));
        self->layer_options[l + 1] = self->layer_labels[l + 1];
    }
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_layer, self->layer_options,
        self->record.layer_count + 1, 0);
    ToriRSChrome_VListSetCount(ui->chrome, self->list, self->record.row_count);
    {
        int first;
        int shown;
        if( ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
            for( int r = first; r < first + shown; r++ )
                ToriRSChrome_VListSetRow(ui->chrome, self->list, r, "");
    }
    {
        char line[TORIRS_CHROME_LABEL_MAX];
        int stated = 0;
        for( int r = 0; r < self->record.row_count; r++ )
            stated += self->record.rows[r].layer >= 0;
        snprintf(line, sizeof(line), "%s %s: %d fields stated, %d more in fields/%s.ini; %d layer%s", self->type,
            self->name, stated, self->record.row_count - stated, self->type, self->record.layer_count,
            self->record.layer_count == 1 ? "" : "s");
        ToriRSChrome_SetText(ui->chrome, self->row_title, line);
    }
}

static void
record_row_text(
    const struct RecordPanel* self,
    int index,
    char* out,
    size_t out_size)
{
    const struct Editor_CfgRecordRow* row = &self->record.rows[index];
    char name[EDITOR_CMD_NAME_MAX * 2];

    if( row->sub[0] )
        snprintf(name, sizeof(name), "param:%s", row->sub);
    else if( row->occurrence > 0 )
        snprintf(name, sizeof(name), "%s#%d", row->key, row->occurrence);
    else
        snprintf(name, sizeof(name), "%s", row->key);
    if( row->layer < 0 )
        snprintf(out, out_size, "  %s = (not stated)%s%s", name, row->scope[0] ? "  " : "", row->scope);
    else
    {
        const struct Editor_CfgRecordLayer* layer = &self->record.layers[row->layer];
        char shadow[24] = "";
        if( row->shadowed > 0 )
            snprintf(shadow, sizeof(shadow), " +%d below", row->shadowed);
        snprintf(out, out_size, "%c %s = %s   [%s%s]", layer->rank == 0 ? ' ' : '*', name, row->value,
            record_basename(layer->label), shadow);
    }
}

/** "param:<name>" | "key#N" | "key" -> (key, sub, occurrence). */
static int
record_parse_field(
    const char* text,
    char key[EDITOR_CMD_NAME_MAX],
    char sub[EDITOR_CMD_NAME_MAX],
    int* occurrence)
{
    const char* hash;

    *occurrence = 0;
    sub[0] = '\0';
    while( text && *text == ' ' )
        text++;
    if( !text || !text[0] || strlen(text) >= EDITOR_CMD_NAME_MAX || strchr(text, '=') || strchr(text, ' ') )
        return 0;
    if( strncmp(text, "param:", 6) == 0 )
    {
        if( !text[6] )
            return 0;
        snprintf(key, EDITOR_CMD_NAME_MAX, "param");
        snprintf(sub, EDITOR_CMD_NAME_MAX, "%s", text + 6);
        return 1;
    }
    hash = strchr(text, '#');
    if( hash )
    {
        char* end = NULL;
        long n = strtol(hash + 1, &end, 10);
        if( hash == text || !end || *end || n < 0 )
            return 0;
        *occurrence = (int)n;
        snprintf(key, EDITOR_CMD_NAME_MAX, "%.*s", (int)(hash - text), text);
        return 1;
    }
    snprintf(key, EDITOR_CMD_NAME_MAX, "%s", text);
    return 1;
}

static int
record_set(
    struct EditorUi* ui,
    struct RecordPanel* self,
    const char* key,
    const char* sub,
    int occurrence,
    const char* value,
    int layer)
{
    struct Editor_Cmd commands[2];
    int count;

    if( !self->open )
        return -1;
    if( !self->built || (ui->editor && self->built_seq != ui->editor->last_cmd_seq) )
        record_resolve(ui, self);
    count = Editor_CfgRecordFieldCommands(&self->record, key, sub, occurrence, value, layer, commands, 2);
    if( count < 0 )
    {
        Editor_UiLog(ui, "editor: %s%s%s cannot be written that way (a key or value the config grammar refuses)", key,
            sub[0] ? ":" : "", sub);
        return -1;
    }
    if( count == 0 )
    {
        Editor_UiLog(ui, "editor: %s%s%s: nothing to change", key, sub[0] ? ":" : "", sub);
        return 0;
    }
    count = edp_apply(ui, commands, count, "edit record");
    if( count > 0 )
    {
        record_resolve(ui, self);
        Editor_UiLog(ui, "editor: %s %s: %s%s%s %s in %s (unsaved; Save writes it, RUN builds it)", self->type,
            self->name, key, sub[0] ? ":" : "", sub, value[0] ? "set" : "removed", commands[count - 1].doc_key);
    }
    return count;
}

static void
record_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct RecordPanel* self = opaque;
    int first;
    int shown;

    if( !ui->chrome->panels[self->panel].visible )
        return;
    if( !self->open )
    {
        /* Nothing to show until a record is opened (a layout or a saved
         * layout may have shown the empty window). */
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
        return;
    }
    if( !self->built || (ui->editor && self->built_seq != ui->editor->last_cmd_seq) )
        record_resolve(ui, self);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->record.row_count; r++ )
    {
        char text[TORIRS_CHROME_INPUT_MAX];
        record_row_text(self, r, text, sizeof(text));
        ToriRSChrome_VListSetRow(ui->chrome, self->list, r, text);
    }
}

/** Row `r` into Field / Value, "where it is stated", and the hint. */
static void
record_select_row(
    struct EditorUi* ui,
    struct RecordPanel* self,
    int r)
{
    const struct Editor_CfgRecordRow* row;
    char field[EDITOR_CMD_NAME_MAX * 2];
    char hint[TORIRS_CHROME_LABEL_MAX];

    if( r < 0 || r >= self->record.row_count )
        return;
    row = &self->record.rows[r];
    if( row->sub[0] )
        snprintf(field, sizeof(field), "param:%s", row->sub);
    else if( row->occurrence > 0 )
        snprintf(field, sizeof(field), "%s#%d", row->key, row->occurrence);
    else
        snprintf(field, sizeof(field), "%s", row->key);
    ToriRSChrome_SetText(ui->chrome, self->input_field, field);
    ToriRSChrome_SetText(ui->chrome, self->input_value, row->value);
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_layer, 0);
    if( row->layer >= 0 )
        snprintf(hint, sizeof(hint), "%s is stated in %s%s", field, self->record.layers[row->layer].label,
            row->shadowed ? " (over a lower layer)" : "");
    else
        snprintf(hint, sizeof(hint), "%s is stated nowhere; Set adds it to %s", field,
            self->record.layers[Editor_CfgRecordAddLayerFor(&self->record)].label);
    ToriRSChrome_SetText(ui->chrome, self->row_hint, hint);
}

static int
record_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct RecordPanel* self = opaque;

    if( widget == self->list )
    {
        record_select_row(ui, self, ToriRSChrome_VListSelected(ui->chrome, widget));
        return 1;
    }
    if( widget == self->button_set || widget == self->button_remove )
    {
        char key[EDITOR_CMD_NAME_MAX];
        char sub[EDITOR_CMD_NAME_MAX];
        int occurrence;
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_layer);
        const char* value = widget == self->button_set ? ToriRSChrome_Text(ui->chrome, self->input_value) : "";

        if( !record_parse_field(ToriRSChrome_Text(ui->chrome, self->input_field), key, sub, &occurrence) )
        {
            Editor_UiLog(ui, "editor: Field is `key`, `key#N` or `param:<name>`");
            return 1;
        }
        if( widget == self->button_set && (!value || !value[0]) )
        {
            Editor_UiLog(ui, "editor: type a value (Remove field takes a field out)");
            return 1;
        }
        (void)record_set(ui, self, key, sub, occurrence, value ? value : "", choice > 0 ? choice - 1 : -1);
        return 1;
    }
    if( widget == self->button_triggers )
    {
        if( Editor_UiPanelSelf(ui, "triggers") )
            Editor_Triggers_Follow(ui);
        else
            Editor_UiLog(ui, "editor: this window has no Triggers panel");
        return 1;
    }
    return widget == self->input_field || widget == self->input_value || widget == self->dropdown_layer;
}

/** Shown with the editor only once a record is open (Editor_Record_Open). */
static void
record_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct RecordPanel* self = opaque;
    if( visible && !self->open )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
}

static void
record_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct RecordPanel* self = opaque;
    (void)ui;
    Editor_CfgRecordFree(&self->record);
    ToriMem_Free(ToriMem_Editor(), self);
}

void
Editor_Record_Register(struct EditorUi* ui)
{
    struct RecordPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 16), edp_px(ui, 140),
        edp_px(ui, 360), "Record");
    assert(self->panel >= 0);
    self->row_title = ToriRSChrome_Label(chrome, self->panel, "open a record: Catalog > Edit record, or Spawns");
    self->list = ToriRSChrome_VList(chrome, self->panel, "", RECORD_LIST_ROWS, 0);
    self->input_field = ToriRSChrome_TextInput(chrome, self->panel, "Field", "");
    ToriRSChrome_SetTooltip(chrome, self->input_field, "key, key#N, or param:<name>");
    self->input_value = ToriRSChrome_TextInput(chrome, self->panel, "Value", "");
    self->dropdown_layer = ToriRSChrome_Dropdown(chrome, self->panel, "Write to", NULL, 0, -1);
    self->button_set = ToriRSChrome_Button(chrome, self->panel, "Set field");
    self->button_remove = ToriRSChrome_Button(chrome, self->panel, "Remove field");
    self->row_hint = ToriRSChrome_Label(chrome, self->panel, "");
    self->button_triggers = ToriRSChrome_Button(chrome, self->panel, "Triggers...");
    ToriRSChrome_SetTooltip(chrome, self->button_triggers, "this record's script triggers; add one (Ctrl+E)");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "record";
    desc.title = "Record";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = record_tick;
    desc.activate = record_activate;
    desc.set_visible = record_set_visible;
    desc.destroy = record_destroy;
    Editor_UiAddPanel(ui, &desc);
}

int
Editor_Record_Open(
    struct EditorUi* ui,
    uint32_t kind,
    const char* name)
{
    struct RecordPanel* self;
    const char* type;
    char key[EDITOR_DOC_KEY_MAX];
    char register_key[EDITOR_DOC_KEY_MAX];
    struct EditorHost* host;

    assert(ui);
    assert(name);
    self = Editor_UiPanelSelf(ui, "record");
    assert(self);
    if( kind > TORIRSMAPED_CATALOG_OBJ )
    {
        Editor_UiLog(ui, "editor: the Record panel edits loc, npc and obj records");
        return 0;
    }
    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: no editor session");
        return 0;
    }
    type = ToriRSMapEdCatalog_KindName(kind);
    snprintf(self->type, sizeof(self->type), "%s", type);
    self->kind = kind;
    self->overlay_count = 0;
    self->open = 0;
    self->built = 0;

    /* A `#<id>` spawn name: the record's own name, from the catalog. */
    host = catalog_host(ui);
    if( name[0] == '#' )
    {
        struct EditorHost_CatalogRecord record;
        memset(&record, 0, sizeof(record));
        if( !host || Editor_HostMapEdCatalogRecord(host, kind, (int32_t)atoi(name + 1), "", &record) != EDITOR_HOST_OK
            || !record.name || strlen(record.name) >= sizeof(self->name) )
        {
            Editor_UiLog(ui, "editor: %s %s has no record name the catalog knows", type, name);
            Editor_HostCatalogRecordFree(&record);
            return 0;
        }
        snprintf(self->name, sizeof(self->name), "%s", record.name);
        Editor_HostCatalogRecordFree(&record);
    }
    else
        snprintf(self->name, sizeof(self->name), "%s", name);

    /* The layers: every server overlay the catalog says states the record. */
    if( host )
    {
        struct EditorHost_CatalogRecord record;
        size_t const type_length = strlen(type);
        memset(&record, 0, sizeof(record));
        if( Editor_HostMapEdCatalogRecord(host, kind, -1, self->name, &record) == EDITOR_HOST_OK )
            for( int i = 0; i < record.source_count && self->overlay_count < EDITOR_CFG_RECORD_LAYERS_MAX - 1; i++ )
            {
                const char* path = record.sources[i].path;
                size_t const length = strlen(path);
                if( strncmp(path, "server/scripts/", 15) != 0 || length < 15 + type_length + 2
                    || path[length - type_length - 1] != '.' || strcmp(path + length - type_length, type) != 0 )
                    continue;
                if( strlen(path + 15) >= sizeof(self->overlays[0]) )
                    continue;
                snprintf(self->overlays[self->overlay_count++], sizeof(self->overlays[0]), "%s", path + 15);
            }
        Editor_HostCatalogRecordFree(&record);
    }
    else
        Editor_UiLog(ui, "editor: no catalog: only configs/all.%s is edited (server overlays are not found)", type);

    /* Open each layer's document (an absent block opens empty). */
    for( int i = -1; i < self->overlay_count; i++ )
    {
        if( !Editor_CfgOverlayKey(key, type, self->name, i < 0 ? NULL : self->overlays[i]) )
            continue;
        if( !edp_ensure_doc(ui, EDITOR_DOC_CFG, key) )
            Editor_UiLog(ui, "editor: could not open %s", key);
    }
    /* The field register: what a record of this type may state. */
    snprintf(register_key, sizeof(register_key), "fields:%s.ini", type);
    self->field_count = 0;
    if( edp_ensure_doc(ui, EDITOR_DOC_TEXT, register_key) )
    {
        struct Editor_DocHandle handle;
        if( Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, register_key, &handle) && handle.text && handle.text->text )
            self->field_count = Editor_CfgRegisterParse(type, handle.text->text, handle.text->length, self->fields,
                EDITOR_CFG_REGISTER_MAX);
    }
    self->open = 1;
    record_resolve(ui, self);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    Editor_UiLog(ui, "editor: record %s:%s over %d layer(s), %d register field(s)", type, self->name,
        self->record.layer_count, self->field_count);
    return 1;
}

int
Editor_Record_WanderValues(
    struct EditorUi* ui,
    const char* type,
    const char* name,
    int* out_wander,
    int* out_leash)
{
    struct RecordPanel* self;
    int row;

    assert(ui);
    assert(type);
    assert(name);
    assert(out_wander);
    assert(out_leash);
    self = Editor_UiPanelSelf(ui, "record");
    if( !self || !self->open || strcmp(self->type, type) != 0 || strcmp(self->name, name) != 0 )
        return 0;
    if( !self->built || (ui->editor && self->built_seq != ui->editor->last_cmd_seq) )
        record_resolve(ui, self);
    *out_wander = MAP_EDITOR_WANDER_DEFAULT;
    *out_leash = MAP_EDITOR_LEASH_DEFAULT;
    row = Editor_CfgRecordFindRow(&self->record, "wanderrange", "", 0);
    if( row >= 0 && self->record.rows[row].layer >= 0 && self->record.rows[row].value[0] >= '0'
        && self->record.rows[row].value[0] <= '9' )
        *out_wander = atoi(self->record.rows[row].value);
    row = Editor_CfgRecordFindRow(&self->record, "maxrange", "", 0);
    if( row >= 0 && self->record.rows[row].layer >= 0 && self->record.rows[row].value[0] >= '0'
        && self->record.rows[row].value[0] <= '9' )
        *out_leash = atoi(self->record.rows[row].value);
    return 1;
}

int
Editor_Record_SetField(
    struct EditorUi* ui,
    const char* key,
    const char* sub,
    const char* value,
    int layer)
{
    struct RecordPanel* self;

    assert(ui);
    assert(key);
    assert(sub);
    assert(value);
    self = Editor_UiPanelSelf(ui, "record");
    assert(self);
    return record_set(ui, self, key, sub, 0, value, layer);
}

void
Editor_Record_Place(
    struct EditorUi* ui,
    int x,
    int y,
    int w,
    int h)
{
    struct RecordPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "record");
    assert(self);
    ToriRSChrome_PanelSetFixedWidth(ui->chrome, self->panel, w);
    ToriRSChrome_PanelSetFixedHeight(ui->chrome, self->panel, h);
    ToriRSChrome_PanelMove(ui->chrome, self->panel, x, y);
    ui->chrome->panels[self->panel].user_placed = 1;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
}

int
Editor_Record_SelectField(
    struct EditorUi* ui,
    const char* key,
    const char* sub)
{
    struct RecordPanel* self;
    int row;

    assert(ui);
    assert(key);
    assert(sub);
    self = Editor_UiPanelSelf(ui, "record");
    assert(self);
    if( !self->open )
        return -1;
    if( !self->built || (ui->editor && self->built_seq != ui->editor->last_cmd_seq) )
        record_resolve(ui, self);
    row = Editor_CfgRecordFindRow(&self->record, key, sub, 0);
    if( row < 0 )
        return -1;
    ToriRSChrome_VListSetSelected(ui->chrome, self->list, row);
    ToriRSChrome_VListSetScroll(ui->chrome, self->list, row > 2 ? row - 2 : 0);
    record_select_row(ui, self, row);
    return row;
}

const struct Editor_CfgRecord*
Editor_Record_Current(struct EditorUi* ui)
{
    struct RecordPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "record");
    if( !self || !self->open )
        return NULL;
    if( !self->built || (ui->editor && self->built_seq != ui->editor->last_cmd_seq) )
        record_resolve(ui, self);
    return &self->record;
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
