/*
 * The asset catalog panel (docs/MAPED_DESIGN.html #catalog; the workbench's
 * loc finder, design section 6): the SERVER's index of the content tree.
 *
 *   Type       a strip of chips over the kinds the editor places or scripts
 *              against (all, loc, npc, obj, seq, spotanim, var, interface,
 *              cs2, trigger, square), the active one lit; "more" shows the
 *              Kind dropdown holding every other kind (enum, struct, param,
 *              inv, dbrow/dbtable, component, the pack tables...), and a
 *              kind chosen there stands as a lit chip of its own. The
 *              dropdown is also what TORIRS_EDITOR_CATALOG drives.
 *   Find       incremental: names, display names, ids, asked once the text
 *              has stood for a frame (edpanel_catalog_model.h). A typed
 *              `key=value` word becomes a FILTER CHIP under the box once a
 *              space ends it; a chip's x removes it; the "+ filter" dropdown
 *              adds the kind's summary-field chips (locs: shape class, size,
 *              category, op, multivar, dialogue, spawn; objs: the wear
 *              slots, stackable, tradeable, members, category, op; npcs:
 *              level, size, dialogue, spawn, category, op). The free words
 *              and the chips compose into the query's filter terms: `#4151`
 *              is the id, `wear=weapon` the wear position, `cat=`, `level=`
 *              and `stack=` the spoken forms of category, vislevel and
 *              stackable. A leading `obj:` / `npc:` / `loc:` (any kind name)
 *              sets the Type and leaves the box.
 *   Count      "1 to 100 of 212 loc": the window's first row to the last row
 *              held after it, of the server's total (the row under Find:
 *              the box has no slot at its right end).
 *   Shortcuts  chips over the index: favourites (editor/favourites.txt,
 *              editor_favourites.h), recent (the session's last 20 picks), in
 *              square (the locs the open squares place, from the mirror's
 *              documents), same category (the pick's, as a chip).
 *   Sort       best match (the server's order), name, id, category, size.
 *   Results    a CELLS list (ToriRSChrome_VListCells): a picture per row --
 *              a loc's 16 px top-down thumbnail the App rasters for the rows
 *              on screen only (app_services.loc_thumbnail, cached by id and
 *              shape), an obj's inventory icon as the game draws it
 *              (app_services.obj_icon: the cache's zoom2d and the three 2D
 *              angles), an npc's figure (app_services.record_thumb) -- the
 *              name with the display name dimmed, keycaps and the id
 *              right-aligned. A loc's keycaps are its placeable shapes (a
 *              click picks it at that shape); an obj's its wear slot and
 *              "stack" (a click adds that chip, narrowing the list); an npc's
 *              its level and footprint. CONTINUOUS: the list is the whole
 *              result, the window asks for the page it is on and the next is
 *              prefetched as it nears the end (no Next page button).
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
 *   Palette    "Add to palette" puts the pick into the Catalog palette's
 *              first free slot (edpanel_catalog_palette.c).
 *
 * The pick is shared tool state (tools.pick_kind / pick_id / pick_name): a
 * loc, npc or obj row with an id becomes it, the Place tool stamps it, the
 * spawns panel resolves an npc / obj pick to its pack symbol, and the PREVIEW
 * panel (edpanel_preview.c, registered with this one) shows its model and
 * carries the pick's actions (Place, the favourite star, Palette, Record,
 * Triggers, Find in world, Copy to new). It is also the Client's: every pick
 * is published as STATE_CATALOG_PICK and heard back (catalog_on_state), so a
 * popped-out Preview or Catalog follows the window that picked. The multiloc
 * rungs stay on the client's provider, reached through ui->app_services
 * (skipped when they are NULL, as in editor_panels_test): one record loaded
 * on demand, never a sweep.
 */

#include "edpanel_catalog_model.h"
#include "edpanel_chips.h"
#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "edpanel_ops_stamps.h"
#include "edpanels.h"
#include "editor/editor_cfg.h"
#include "editor/editor_favourites.h"
#include "editor/editor_palette.h"
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
/** Result rows on screen. */
#define CATALOG_LIST_ROWS 12
/** Rows a CATALOG_QUERY page holds: "1 to 100 of 212". */
#define CATALOG_PAGE_ROWS 100
#define CATALOG_RECORD_ROWS 8
/** Picks published whose echo has not come back yet. */
#define CATALOG_PICK_ECHO_MAX 8
/** TORIRS_EDITOR_CATALOG pick= entries. */
#define CATALOG_ENV_PICKS 8
/** Favourites and in-square locs whose records a rebuild asks at most:
 *  the rest list by id. */
#define CATALOG_RESOLVE_MAX 128
/** The open squares' locs are gathered at most this often while commands
 *  keep coming, through a set of this many slots (twice the list's cap). */
#define CATALOG_IN_SQUARE_MS 500
#define CATALOG_IN_SQUARE_SET 1024

_Static_assert(EDITOR_CATALOG_KEYCAPS_MAX == TORIRS_CHROME_CELL_KEYS_MAX,
    "the model's keycaps are the chrome's cells");

/** The shortcut chips, in their order. */
enum CatalogShortcut
{
    CATALOG_SHORTCUT_FAVOURITES = 0,
    CATALOG_SHORTCUT_RECENT,
    CATALOG_SHORTCUT_IN_SQUARE,
    CATALOG_SHORTCUT_CATEGORY,
    CATALOG_SHORTCUT_COUNT,
};

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
    /** The Type strip: the kinds as chips, "more" showing the dropdown. */
    struct Edp_ChipStrip type_strip;
    /** list | grid: the Gallery is the grid view of this search. */
    struct Edp_ChipStrip view_strip;
    int type_more;
    int type_kind_of[EDP_CHIPS_MAX];
    /** The kind the "+ filter" menu and the list header were built for. */
    uint32_t shown_filter_kind;
    int dropdown_kind;
    int input_search;
    int row_count;
    /** The filter chips under Find, and the "+ filter" dropdown. */
    struct Edp_ChipStrip chip_strip;
    int dropdown_filter;
    int dropdown_sort;
    /** favourites / recent / in square / same category. */
    struct Edp_ChipStrip shortcut_strip;
    int list;
    int row_picked;
    int row_record;
    int record_list;
    int input_new_name;
    int button_copy;
    int button_edit_record;
    /** The pick into the Catalog palette's first free slot. */
    int button_add_palette;
    /** ui->hotkeys.serial the Add button's keycap was labelled from. */
    uint32_t chord_serial;
    int button_open_script;
    int row_multi;
    int dropdown_variant;
    struct CatalogRows var_rows;

    struct Editor_CatalogModel model;
    /** The filter chips, and the Find text as last seen (a change takes
     *  its finished key=value words into chips). */
    struct Editor_CatalogChips chips;
    char find_seen[TORIRS_CHROME_INPUT_MAX];
    char composed[EDITOR_CATALOG_FILTER_MAX];
    /** The list shows the model's rows as of these serials. */
    uint32_t shown_answers;
    uint32_t shown_key;
    /** The selected result row (absolute, -1 none) and the record list built
     *  for it. */
    int selected;
    int record_built;
    /** The list's tooltip: the hovered row and cell it was written for. */
    int tip_row;
    int tip_cell;

    /* The shortcuts. */
    struct Editor_CatalogRecents recents;
    /** editor/favourites.txt as last parsed, and what it was parsed from. */
    struct Editor_FavouritesFile favourites;
    const char* fav_text;
    size_t fav_length;
    uint32_t fav_seq;
    /** A hash of the text parsed: a command that did not touch the file
     *  rebuilds nothing. */
    uint32_t fav_hash;
    int fav_opened;
    /** The open squares' loc ids (distinct), and the command seq / square
     *  count they were gathered at. */
    int* in_square_ids;
    int in_square_count;
    uint32_t in_square_seq;
    int in_square_squares;
    /** A hash of the id set: an edit that moved no id in or out rebuilds
     *  nothing. */
    uint32_t in_square_hash;
    /** The set the gather dedups through (CATALOG_IN_SQUARE_SET slots), and
     *  when it last ran (chrome ms). */
    int* in_square_set;
    int64_t in_square_ms;
    /** The rows a local list is built in (owned), and what the shown one
     *  was built from (source, serial). */
    struct Editor_CatalogLocalRow* local_build;
    int local_source;
    uint32_t local_serial;
    /** The pick's record name and category (the favourite star and "same
     *  category" read them), for (pick_kind, pick_id). */
    int sym_kind;
    int sym_id;
    char sym_name[EDITOR_CMD_NAME_MAX];
    char sym_category[EDITOR_CATALOG_CHIP_VALUE_MAX];
    uint32_t sym_mask;

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
    /** Editor_Catalog_PickRecord / PickFromWorld: the row to select, and
     *  scroll into view, once the page holding it lands ("" none). */
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
    if( host && self->selected >= 0 && self->selected < Editor_CatalogModelViewCount(&self->model) )
        record = Editor_CatalogModelRecordAt(&self->model, host, self->selected);
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

    if( !host || self->selected < 0 || self->selected >= Editor_CatalogModelViewCount(&self->model) )
        return;
    if( !ToriRSChrome_VListWindow(ui->chrome, self->record_list, &first, &shown) )
        return;
    /* Held: the same row answers from the model, no round trip. */
    record = Editor_CatalogModelRecordAt(&self->model, host, self->selected);
    for( int r = first; r < first + shown && r < catalog_record_lines(record); r++ )
    {
        char text[TORIRS_CHROME_INPUT_MAX];
        catalog_record_line(record, r, text, sizeof(text));
        ToriRSChrome_VListSetRow(ui->chrome, self->record_list, r, text);
    }
}

/** Row `view`'s thumbnail: the App's 16 x 16 picture of the loc at its
 *  first shape, asked for the rows on screen only; 0 while it is still to
 *  come, or for a row that is not a loc. */
static int
catalog_thumbnail(
    struct EditorUi* ui,
    const struct Editor_CatalogView* view)
{
    const struct EditorUiAppServices* services = &ui->app_services;
    int const shape = view->shape >= 0 ? view->shape : Editor_LocShapeFirst(view->shape_mask);
    int scene_id;

    if( view->id < 0 )
        return 0;
    if( view->kind == TORIRSMAPED_CATALOG_OBJ )
    {
        /* The game's own inventory icon (count 1): the bridge applies the
         * cache's zoom2d, xan2d, yan2d, zan2d and offsets. */
        if( !services->obj_icon )
            return 0;
        scene_id = services->obj_icon(services->user_data, view->id, 1, NULL, 0);
        return scene_id > 0 ? scene_id : 0;
    }
    if( view->kind == TORIRSMAPED_CATALOG_NPC )
    {
        if( !services->record_thumb )
            return 0;
        scene_id = services->record_thumb(services->user_data, view->kind, view->id, 16);
        return scene_id > 0 ? scene_id : 0;
    }
    if( !services->loc_thumbnail || view->kind != TORIRSMAPED_CATALOG_LOC || shape < 0 )
        return 0;
    return services->loc_thumbnail(services->user_data, view->id, shape);
}

/** The visible rows, from what the model holds (compare-then-set): a row
 *  whose page has not landed stays blank until it does. */
static void
catalog_list_rows(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    int first;
    int shown;
    int const any_kind = self->model.source == EDITOR_CATALOG_SOURCE_INDEX
                         && self->model.held_kind == TORIRSMAPED_CATALOG_ANY;

    if( !ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        return;
    for( int r = first; r < first + shown; r++ )
    {
        struct Editor_CatalogView view;
        char text[TORIRS_CHROME_INPUT_MAX];
        if( !Editor_CatalogModelViewAt(&self->model, r, &view) )
            continue;
        Editor_CatalogCellsText(&view, catalog_thumbnail(ui, &view), any_kind, text, sizeof(text));
        ToriRSChrome_VListSetRow(ui->chrome, self->list, r, text);
    }
}

/** The list follows the model: its count every frame, and on a NEW answer
 *  (kind, find, source) the top of it and no selection -- a page that lands
 *  as the window moves is the same list and keeps both. */
static void
catalog_list_show(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    ToriRSChrome_VListSetCount(ui->chrome, self->list, Editor_CatalogModelViewCount(&self->model));
    if( self->shown_key != self->model.key_serial )
    {
        self->shown_key = self->model.key_serial;
        self->selected = -1;
        ToriRSChrome_VListSetSelected(ui->chrome, self->list, -1);
        ToriRSChrome_VListSetScroll(ui->chrome, self->list, 0);
        catalog_record_show(ui, self);
    }
    if( self->shown_answers == self->model.answers )
        return;
    self->shown_answers = self->model.answers;
    /* Editor_Catalog_PickRecord / PickFromWorld's row, once a page holding
     * it lands: selected and scrolled into view. */
    if( self->select_name[0] )
    {
        int const row = Editor_CatalogModelFindRow(&self->model, self->select_kind, self->select_name);
        if( row >= 0 )
        {
            int first = 0;
            int shown = 0;
            self->select_name[0] = '\0';
            self->selected = row;
            ToriRSChrome_VListSetSelected(ui->chrome, self->list, row);
            (void)ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown);
            if( row < first || row >= first + (shown > 0 ? shown : CATALOG_LIST_ROWS) )
                ToriRSChrome_VListSetScroll(ui->chrome, self->list, row > 2 ? row - 2 : 0);
            catalog_record_show(ui, self);
        }
    }
}

/** The hovered row says itself whole (R2: the cells ellipsise) and a hovered
 *  keycap says what its click does (R5). */
static void
catalog_list_tip(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    struct Editor_CatalogView view;
    char tip[TORIRS_CHROME_LABEL_MAX];
    int cell = 0;
    int const row = ToriRSChrome_VListHoverRow(ui->chrome, self->list, &cell);

    if( row == self->tip_row && cell == self->tip_cell )
        return;
    self->tip_row = row;
    self->tip_cell = cell;
    if( row < 0 || !Editor_CatalogModelViewAt(&self->model, row, &view) )
    {
        ToriRSChrome_SetTooltip(ui->chrome, self->list, "click: pick; a shape key: pick at that shape");
        return;
    }
    if( cell > 0 && Editor_CatalogViewShapeAt(&view, cell) >= 0 )
    {
        int const shape = Editor_CatalogViewShapeAt(&view, cell);
        snprintf(tip, sizeof(tip), "pick %.24s at shape %d (%s)", view.name, shape, Editor_LocShapeName(shape));
    }
    else if( cell > 0 && view.kind == TORIRSMAPED_CATALOG_OBJ )
    {
        char key[EDITOR_CATALOG_CHIP_KEY_MAX];
        char value[EDITOR_CATALOG_CHIP_VALUE_MAX];
        if( Editor_CatalogViewKeyChip(&view, cell, key, value) )
            snprintf(tip, sizeof(tip), "narrow the list to %s=%s", key, value);
        else
            snprintf(tip, sizeof(tip), "%.52s %d", view.name, (int)view.id);
    }
    else if( view.display[0] && strcmp(view.display, view.name) != 0 )
        snprintf(tip, sizeof(tip), "%.36s \"%.20s\" %d", view.name, view.display, (int)view.id);
    else
        snprintf(tip, sizeof(tip), "%.52s %d", view.name, (int)view.id);
    /* A loc says its footprint and the layer its shapes go in (the server's
     * `size` and `layer`). */
    if( view.kind == TORIRSMAPED_CATALOG_LOC && view.layer[0] && !(cell > 0 && Editor_CatalogViewShapeAt(&view, cell) >= 0) )
    {
        size_t const at = strlen(tip);
        snprintf(tip + at, sizeof(tip) - at, ", %dx%d %.20s", view.size_w, view.size_l, view.layer);
    }
    ToriRSChrome_SetTooltip(ui->chrome, self->list, tip);
}

/* ---- the chips: filters and shortcuts ------------------------------------------- */

/** Find's finished `key=value` words become chips: a word is finished when
 *  a space follows it, so "op=Op" is not a chip while "en" is still coming.
 *  Writes the words left back into the box. */
static void
catalog_take_chips(
    struct EditorUi* ui,
    struct CatalogPanel* self,
    const char* text)
{
    char done[TORIRS_CHROME_INPUT_MAX];
    char rest[TORIRS_CHROME_INPUT_MAX];
    char box[TORIRS_CHROME_INPUT_MAX];
    const char* last_space = strrchr(text, ' ');
    const char* tail;

    if( !last_space )
        return;
    snprintf(done, sizeof(done), "%.*s", (int)(last_space - text), text);
    tail = last_space + 1;
    /* A word taken -- a new chip, or one already there -- leaves the box. */
    (void)Editor_CatalogChipsTake(&self->chips, done, rest, sizeof(rest));
    if( strcmp(rest, done) == 0 )
        return;
    snprintf(box, sizeof(box), "%s%s%s", rest, rest[0] ? " " : "", tail);
    ToriRSChrome_SetText(ui->chrome, self->input_search, box);
    snprintf(self->find_seen, sizeof(self->find_seen), "%s", box);
}

static void
catalog_chips_draw(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    edp_chips_begin(&self->chip_strip);
    for( int i = 0; i < self->chips.count; i++ )
    {
        char label[EDP_CHIP_LABEL_MAX];
        char tip[EDP_CHIP_TIP_MAX];
        Editor_CatalogChipLabel(&self->chips.chip[i], label, sizeof(label));
        Editor_CatalogChipTip(&self->chips.chip[i], tip, sizeof(tip));
        (void)edp_chips_push(&self->chip_strip, label, tip, 0, 1);
    }
    edp_chips_end(ui, &self->chip_strip);
    ToriRSChrome_SetHidden(ui->chrome, self->chip_strip.graph, self->chips.count == 0);
    edp_chips_hover(ui, &self->chip_strip);
}

static int
catalog_favourite_count(const struct CatalogPanel* self)
{
    return Editor_FavouritesCount(&self->favourites);
}

static void
catalog_shortcuts_draw(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    char label[EDP_CHIP_LABEL_MAX];
    char tip[EDP_CHIP_TIP_MAX];
    enum Editor_CatalogSource const source = self->model.source;

    edp_chips_begin(&self->shortcut_strip);
    snprintf(label, sizeof(label), "favourites (%d)", catalog_favourite_count(self));
    (void)edp_chips_push(&self->shortcut_strip, label,
        source == EDITOR_CATALOG_SOURCE_FAVOURITES ? "back to the index" : "your starred records, favourites.txt",
        source == EDITOR_CATALOG_SOURCE_FAVOURITES, 0);
    snprintf(label, sizeof(label), "recent (%d)", self->recents.count);
    (void)edp_chips_push(&self->shortcut_strip, label,
        source == EDITOR_CATALOG_SOURCE_RECENT ? "back to the index" : "the last 20 picks of this session",
        source == EDITOR_CATALOG_SOURCE_RECENT, 0);
    snprintf(label, sizeof(label), "in square (%d)", self->in_square_count);
    (void)edp_chips_push(&self->shortcut_strip, label,
        source == EDITOR_CATALOG_SOURCE_IN_SQUARE ? "back to the index" : "the locs the open squares place",
        source == EDITOR_CATALOG_SOURCE_IN_SQUARE, 0);
    if( self->sym_category[0] && ui->tools.pick_kind >= 0 && self->sym_kind == ui->tools.pick_kind
        && self->sym_id == ui->tools.pick_id )
    {
        snprintf(label, sizeof(label), "same category (%.24s)", self->sym_category);
        snprintf(tip, sizeof(tip), "adds the chip category: %.40s", self->sym_category);
    }
    else
    {
        snprintf(label, sizeof(label), "same category");
        snprintf(tip, sizeof(tip), "pick a record with a category first");
    }
    (void)edp_chips_push(&self->shortcut_strip, label, tip, 0, 0);
    edp_chips_end(ui, &self->shortcut_strip);
    edp_chips_hover(ui, &self->shortcut_strip);
}

/* ---- the Type strip ------------------------------------------------------------- */

/** The kinds the strip always shows, in its order. */
static const uint32_t k_type_strip[] = {
    TORIRSMAPED_CATALOG_ANY,       TORIRSMAPED_CATALOG_LOC,     TORIRSMAPED_CATALOG_NPC,       TORIRSMAPED_CATALOG_OBJ,
    TORIRSMAPED_CATALOG_SEQ,       TORIRSMAPED_CATALOG_SPOTANIM, TORIRSMAPED_CATALOG_VARS,     TORIRSMAPED_CATALOG_INTERFACE,
    TORIRSMAPED_CATALOG_CS2,       TORIRSMAPED_CATALOG_TRIGGER, TORIRSMAPED_CATALOG_SQUARE,
};
#define CATALOG_TYPE_STRIP ((int)(sizeof(k_type_strip) / sizeof(k_type_strip[0])))

/** The kind the dropdown holds. */
static uint32_t
catalog_kind(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    int const index = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_kind);
    return Editor_CatalogModelKindAt(index >= 0 && index < Editor_CatalogModelKindCount() ? index : 0);
}

/** Choosing a kind, from the strip, a prefix or the dropdown: asks again
 *  (the tree may have changed) and leaves any local list. */
static void
catalog_set_kind(
    struct EditorUi* ui,
    struct CatalogPanel* self,
    uint32_t kind)
{
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_kind, Editor_CatalogModelKindIndex(kind));
    Editor_CatalogModelInvalidate(&self->model);
    Editor_CatalogModelSetSource(&self->model, EDITOR_CATALOG_SOURCE_INDEX);
}

static void
catalog_type_strip_draw(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    uint32_t const current = catalog_kind(ui, self);
    int shown = 0;
    int index;

    edp_chips_begin(&self->type_strip);
    for( int i = 0; i < CATALOG_TYPE_STRIP; i++ )
    {
        uint32_t const kind = k_type_strip[i];
        const char* name = kind == TORIRSMAPED_CATALOG_ANY ? "all" : ToriRSMapEdCatalog_KindName(kind);
        char tip[EDP_CHIP_TIP_MAX];
        snprintf(tip, sizeof(tip), kind == TORIRSMAPED_CATALOG_ANY ? "every kind at once" : "only %s records", name);
        index = edp_chips_push(&self->type_strip, name, tip, kind == current, 0);
        if( index >= 0 )
            self->type_kind_of[index] = (int)kind;
        shown |= kind == current;
    }
    if( !shown )
    {
        index = edp_chips_push(&self->type_strip, ToriRSMapEdCatalog_KindName(current), "the kind chosen under more", 1, 0);
        if( index >= 0 )
            self->type_kind_of[index] = (int)current;
    }
    self->type_more = edp_chips_push(&self->type_strip, "more",
        ui->chrome->widgets[self->dropdown_kind].hidden ? "show the Kind dropdown: every other kind"
                                                              : "hide the Kind dropdown",
        !ui->chrome->widgets[self->dropdown_kind].hidden, 0);
    if( self->type_more >= 0 )
        self->type_kind_of[self->type_more] = -1;
    edp_chips_end(ui, &self->type_strip);
    edp_chips_hover(ui, &self->type_strip);

    edp_chips_begin(&self->view_strip);
    (void)edp_chips_push(&self->view_strip, "list", "this list", 1, 0);
    (void)edp_chips_push(&self->view_strip, "grid", "the Gallery: the same search as pictures (Shift+F8)",
        Editor_Gallery_Visible(ui), 0);
    edp_chips_end(ui, &self->view_strip);
    edp_chips_hover(ui, &self->view_strip);
}

/** The "+ filter" menu and the list header follow the kind. */
static void
catalog_kind_follow(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    uint32_t const kind = catalog_kind(ui, self);

    if( kind == self->shown_filter_kind )
        return;
    self->shown_filter_kind = kind;
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_filter, Editor_CatalogFilterMenuLabelsFor(kind),
        Editor_CatalogFilterMenuCountFor(kind), 0);
    ToriRSChrome_SetLabel(ui->chrome, self->list, Editor_CatalogCellsHeaderFor(kind));
}

/* ---- the local lists: favourites, recent, in square ---------------------------- */

/** editor/favourites.txt, re-parsed when its text moved. Opened on the
 *  first tick with a session. */
static void
catalog_favourites_follow(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    struct Editor_DocHandle handle;

    if( !ui->editor )
        return;
    if( !self->fav_opened )
    {
        self->fav_opened = 1;
        if( !edp_ensure_doc(ui, EDITOR_DOC_TEXT, EDITOR_FAVOURITES_DOC_KEY) )
            Editor_UiLog(ui, "editor: could not open %s; favourites are off", EDITOR_FAVOURITES_FILE_PATH);
    }
    if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, EDITOR_FAVOURITES_DOC_KEY, &handle) || !handle.text )
        return;
    if( handle.text->text == self->fav_text && handle.text->length == self->fav_length
        && self->fav_seq == ui->editor->last_cmd_seq )
        return;
    self->fav_text = handle.text->text;
    self->fav_length = handle.text->length;
    self->fav_seq = ui->editor->last_cmd_seq;
    {
        uint32_t hash = 2166136261u;
        for( size_t i = 0; i < handle.text->length; i++ )
            hash = (hash ^ (uint8_t)handle.text->text[i]) * 16777619u;
        hash ^= (uint32_t)handle.text->length;
        if( hash == self->fav_hash && self->favourites.text )
            return;
        self->fav_hash = hash;
    }
    {
        struct Editor_FavouritesParseResult const result =
            Editor_FavouritesParse(&self->favourites, handle.text->text, handle.text->length);
        if( !result.ok )
            Editor_UiLog(ui, "editor: %s line %d does not parse; it is kept as it is", EDITOR_FAVOURITES_FILE_PATH,
                result.line);
    }
}

/**
 * The open squares' distinct loc ids, gathered again when a command or an
 * open moved them -- at most every CATALOG_IN_SQUARE_MS while commands keep
 * coming (a brush stroke is a command a frame), with the panel shown. One
 * pass over the squares' locs through an open-addressed set, no per-loc
 * search of the list.
 */
static void
catalog_in_square_follow(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    const struct Editor_Doc* doc;

    if( !ui->editor )
        return;
    doc = &ui->editor->doc;
    if( self->in_square_ids && self->in_square_seq == ui->editor->last_cmd_seq
        && self->in_square_squares == doc->square_count )
        return;
    if( self->in_square_ids && ui->chrome->now_ms - self->in_square_ms < CATALOG_IN_SQUARE_MS
        && ui->chrome->now_ms >= self->in_square_ms )
        return;
    if( !self->in_square_ids )
    {
        self->in_square_ids = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * EDITOR_CATALOG_LOCAL_MAX);
        assert(self->in_square_ids);
        self->in_square_set = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * CATALOG_IN_SQUARE_SET);
        assert(self->in_square_set);
    }
    self->in_square_ms = ui->chrome->now_ms;
    self->in_square_seq = ui->editor->last_cmd_seq;
    self->in_square_squares = doc->square_count;
    self->in_square_count = 0;
    for( int i = 0; i < CATALOG_IN_SQUARE_SET; i++ )
        self->in_square_set[i] = -1;
    for( int s = 0; s < doc->square_count; s++ )
    {
        const struct Editor_Square* square = &doc->squares[s];
        if( !square->loaded )
            continue;
        for( int i = 0; i < square->loc_count && self->in_square_count < EDITOR_CATALOG_LOCAL_MAX; i++ )
        {
            int const id = square->locs[i].loc_id;
            uint32_t slot = ((uint32_t)id * 2654435761u) & (CATALOG_IN_SQUARE_SET - 1);
            while( self->in_square_set[slot] >= 0 && self->in_square_set[slot] != id )
                slot = (slot + 1) & (CATALOG_IN_SQUARE_SET - 1);
            if( self->in_square_set[slot] == id )
                continue;
            self->in_square_set[slot] = id;
            self->in_square_ids[self->in_square_count++] = id;
        }
    }
    {
        uint32_t hash = 2166136261u;
        for( int i = 0; i < self->in_square_count; i++ )
            hash = (hash ^ (uint32_t)self->in_square_ids[i]) * 16777619u;
        self->in_square_hash = hash ^ (uint32_t)self->in_square_count;
    }
}

/** One local row from the catalog's record of (kind, id or name): its
 *  name, display name, category, shapes and size. 0 when the catalog has no
 *  such record (the row keeps what it had). */
static int
catalog_resolve_local(
    struct EditorHost* host,
    struct Editor_CatalogLocalRow* row)
{
    struct EditorHost_CatalogRecord record;
    struct EditorHost_CatalogField fields[64];
    struct EditorHost_CatalogRow as_row;
    int count = 0;
    int ok;

    memset(&record, 0, sizeof(record));
    ok = Editor_HostMapEdCatalogRecord(host, row->kind, row->id, row->id >= 0 ? "" : row->name, &record)
         == EDITOR_HOST_OK;
    if( ok )
    {
        const char* category;
        row->id = record.id;
        if( record.name )
            snprintf(row->name, sizeof(row->name), "%s", record.name);
        if( record.display )
            snprintf(row->display, sizeof(row->display), "%s", record.display);
        for( int i = 0; i < record.field_count && count < 64; i++ )
        {
            fields[count].key = record.fields[i].key;
            fields[count].value = record.fields[i].value;
            count++;
        }
        memset(&as_row, 0, sizeof(as_row));
        as_row.kind = row->kind;
        as_row.id = row->id;
        as_row.name = row->name;
        as_row.display = row->display;
        as_row.field_count = count;
        as_row.fields = fields;
        row->shape_mask = Editor_CatalogRowShapeMask(&as_row);
        if( row->kind == TORIRSMAPED_CATALOG_LOC )
            Editor_CatalogRowSize(&as_row, &row->size_w, &row->size_l);
        category = Editor_CatalogRowField(&as_row, "category");
        if( category )
            snprintf(row->category, sizeof(row->category), "%s", category);
    }
    Editor_HostCatalogRecordFree(&record);
    return ok;
}

/** The shown local list, built again when what it lists moved: the
 *  favourites file, the recent picks, the open squares. */
static void
catalog_local_follow(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    struct EditorHost* host = catalog_host(ui);
    struct Editor_CatalogLocalRow* rows;
    int count = 0;
    int capacity = 0;
    int resolved = 0;
    uint32_t serial;

    switch( self->model.source )
    {
    case EDITOR_CATALOG_SOURCE_FAVOURITES:
        serial = self->fav_hash;
        break;
    case EDITOR_CATALOG_SOURCE_RECENT:
        serial = self->recents.serial;
        break;
    case EDITOR_CATALOG_SOURCE_IN_SQUARE:
        serial = self->in_square_hash;
        break;
    default:
        return;
    }
    if( self->local_source == (int)self->model.source && self->local_serial == serial )
        return;
    self->local_source = (int)self->model.source;
    self->local_serial = serial;
    if( !self->local_build )
    {
        self->local_build = ToriMem_Alloc(ToriMem_Editor(), sizeof(*self->local_build) * EDITOR_CATALOG_LOCAL_MAX);
        assert(self->local_build);
    }
    rows = self->local_build;
    capacity = EDITOR_CATALOG_LOCAL_MAX;

    if( self->model.source == EDITOR_CATALOG_SOURCE_RECENT )
    {
        for( int i = 0; i < self->recents.count && count < capacity; i++ )
            rows[count++] = self->recents.rows[i];
    }
    else if( self->model.source == EDITOR_CATALOG_SOURCE_FAVOURITES )
    {
        int const n = Editor_FavouritesCount(&self->favourites);
        for( int i = 0; i < n && count < capacity; i++ )
        {
            const struct Editor_FavouriteRow* fav = Editor_FavouritesAt(&self->favourites, i);
            struct Editor_CatalogLocalRow* row = &rows[count++];
            memset(row, 0, sizeof(*row));
            row->kind = (uint32_t)fav->kind;
            row->id = -1;
            snprintf(row->name, sizeof(row->name), "%s", fav->record);
            row->shape = fav->shape;
            row->rotation = fav->rotation;
            /* A favourite is a name: the catalog says its id and fields. */
            if( host && resolved < CATALOG_RESOLVE_MAX )
            {
                resolved++;
                (void)catalog_resolve_local(host, row);
            }
        }
    }
    else
    {
        for( int i = 0; i < self->in_square_count && count < capacity; i++ )
        {
            struct Editor_CatalogLocalRow* row = &rows[count++];
            memset(row, 0, sizeof(*row));
            row->kind = TORIRSMAPED_CATALOG_LOC;
            row->id = self->in_square_ids[i];
            row->shape = -1;
            snprintf(row->name, sizeof(row->name), "loc_%d", self->in_square_ids[i]);
            if( host && resolved < CATALOG_RESOLVE_MAX )
            {
                resolved++;
                (void)catalog_resolve_local(host, row);
            }
        }
    }
    Editor_CatalogModelSetLocal(&self->model, rows, count);
}

/** Show `source`, or the index when it is shown already (a lit shortcut's
 *  second click goes back). */
static void
catalog_show_source(
    struct EditorUi* ui,
    struct CatalogPanel* self,
    enum Editor_CatalogSource source)
{
    if( self->model.source == source )
        source = EDITOR_CATALOG_SOURCE_INDEX;
    self->local_source = -1;
    Editor_CatalogModelSetSource(&self->model, source);
    catalog_local_follow(ui, self);
}

/* ---- TORIRS_EDITOR_CATALOG -------------------------------------------------------
 *
 * TORIRS_EDITOR_CATALOG=<kind>:<find>[@<frame>][,every=<frames>][,pick=<row>@<frame>...]
 *                       [,next=<frame>][,edit=<frame>]
 * (measurements and photographs; main window only): at editor frame <frame>
 * (default 60) open the Catalog on <kind>, then type <find> into Find one
 * character every <frames> frames (default 8, a quick typist at 60 Hz), and
 * at each pick frame select result row <row> as a click on it does; `next`
 * scrolls the list a window on, `edit` presses Edit record... on the selection. With
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
        if( row < 0 || row >= Editor_CatalogModelViewCount(&self->model) )
        {
            Editor_UiLog(ui, "editor: TORIRS_EDITOR_CATALOG pick=%d: the list has %d rows", row,
                Editor_CatalogModelViewCount(&self->model));
            continue;
        }
        if( Editor_CatalogModelStats() )
            TORIRS_REPORT("catalog: frame %d picks row %d\n", f, row);
        ToriRSChrome_VListSetSelected(ui->chrome, self->list, row);
        (void)catalog_activate(ui, self, self->list);
    }
    if( f == self->env_next_frame )
    {
        /* The list is continuous: "next" scrolls a window on (the page
         * after it is prefetched). */
        if( Editor_CatalogModelStats() )
            TORIRS_REPORT("catalog: frame %d scrolls a window on\n", f);
        ToriRSChrome_VListSetScroll(
            ui->chrome, self->list, ui->chrome->widgets[self->list].imin + CATALOG_LIST_ROWS);
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

/** The pick's record name, category and shapes, for the star, "same
 *  category" and the Preview's shape row: asked once per pick of a record
 *  this window did not pick from a row (a heard pick, a selection). */
static void
catalog_symbol_follow(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    struct EditorHost* host;
    struct Editor_CatalogLocalRow row;

    if( self->sym_kind == ui->tools.pick_kind && self->sym_id == ui->tools.pick_id )
        return;
    self->sym_kind = ui->tools.pick_kind;
    self->sym_id = ui->tools.pick_id;
    self->sym_name[0] = '\0';
    self->sym_category[0] = '\0';
    self->sym_mask = 0;
    host = catalog_host(ui);
    if( !host || ui->tools.pick_kind < 0 || ui->tools.pick_kind > (int)TORIRSMAPED_CATALOG_OBJ
        || ui->tools.pick_id < 0 )
        return;
    memset(&row, 0, sizeof(row));
    row.kind = (uint32_t)ui->tools.pick_kind;
    row.id = ui->tools.pick_id;
    if( !catalog_resolve_local(host, &row) )
        return;
    snprintf(self->sym_name, sizeof(self->sym_name), "%s", row.name);
    snprintf(self->sym_category, sizeof(self->sym_category), "%s", row.category);
    self->sym_mask = row.shape_mask;
}

/** The pick's record name, "" when the catalog has none for it. */
static const char*
catalog_pick_symbol(
    struct EditorUi* ui,
    struct CatalogPanel* self)
{
    catalog_symbol_follow(ui, self);
    return self->sym_name;
}

/** Note the pick as made from `view` (its name, category and shapes are
 *  known: nothing to ask), and remember it among the recent picks. */
static void
catalog_note_pick(
    struct EditorUi* ui,
    struct CatalogPanel* self,
    const struct Editor_CatalogView* view)
{
    struct Editor_CatalogLocalRow recent;

    /* The view's strings may be these very fields (Pick from world): no
     * copy onto itself. */
    self->sym_kind = (int)view->kind;
    self->sym_id = view->id;
    if( view->name != self->sym_name )
        snprintf(self->sym_name, sizeof(self->sym_name), "%s", view->name);
    if( view->category != self->sym_category )
        snprintf(self->sym_category, sizeof(self->sym_category), "%s", view->category);
    self->sym_mask = view->shape_mask;
    memset(&recent, 0, sizeof(recent));
    recent.kind = view->kind;
    recent.id = view->id;
    snprintf(recent.name, sizeof(recent.name), "%s", view->name);
    snprintf(recent.display, sizeof(recent.display), "%s", view->display);
    snprintf(recent.category, sizeof(recent.category), "%s", view->category);
    recent.shape_mask = view->shape_mask;
    recent.size_w = view->size_w;
    recent.size_l = view->size_l;
    recent.shape = view->kind == TORIRSMAPED_CATALOG_LOC ? ui->tools.place_shape : -1;
    recent.rotation = view->kind == TORIRSMAPED_CATALOG_LOC ? ui->tools.place_rotation & 3 : 0;
    Editor_CatalogRecentsPush(&self->recents, &recent);
}

static void
catalog_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct CatalogPanel* self = opaque;
    struct EditorHost* host;
    const char* search;
    char line[TORIRS_CHROME_INPUT_MAX];
    int kind_index;
    int first;

    catalog_meter(ui, self);
    catalog_env_tick(ui, self);
    /* Add to palette wears palette.add_pick's chord (design section 15
     * surface 3), relabelled on a rebind whether or not the panel shows. */
    if( self->chord_serial != ui->hotkeys.serial )
    {
        edp_button_chord(ui, self->button_add_palette, EDITOR_HK_PALETTE_ADD_PICK);
        self->chord_serial = ui->hotkeys.serial;
    }
    kind_index = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_kind);
    /* A pick heard from another window: named whether or not this panel is
     * open (a popped-out Preview shows the name). */
    if( self->pick_name_pending )
        catalog_pick_name(ui, self);
    /* The star, the shortcut chips and the Preview's shape row read these
     * whether or not the list shows; each is a compare until what it
     * follows moves. */
    catalog_favourites_follow(ui, self);
    catalog_symbol_follow(ui, self);
    /* The Gallery is a second view of this query: it is driven for it too. */
    if( !ui->chrome->panels[self->panel].visible && !Editor_Gallery_Visible(ui) )
        return;
    /* Only the shown panel counts the open squares' locs. */
    catalog_in_square_follow(ui, self);
    host = catalog_host(ui);

    /* Find: its finished key=value words become chips; the rest and the
     * chips compose the question. */
    search = ToriRSChrome_Text(ui->chrome, self->input_search);
    if( !search )
        search = "";
    if( strcmp(search, self->find_seen) != 0 )
    {
        uint32_t prefix_kind;
        char rest[TORIRS_CHROME_INPUT_MAX];
        /* `obj:whip`: the kind leaves the box and becomes the Type. */
        if( Editor_CatalogTakeKindPrefix(search, &prefix_kind, rest, sizeof(rest)) )
        {
            catalog_set_kind(ui, self, prefix_kind);
            ToriRSChrome_SetText(ui->chrome, self->input_search, rest);
            search = ToriRSChrome_Text(ui->chrome, self->input_search);
            kind_index = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_kind);
        }
        snprintf(self->find_seen, sizeof(self->find_seen), "%s", search);
        catalog_take_chips(ui, self, search);
        search = ToriRSChrome_Text(ui->chrome, self->input_search);
    }
    catalog_kind_follow(ui, self);
    Editor_CatalogChipsCompose(&self->chips, search ? search : "", self->composed, sizeof(self->composed));
    (void)Editor_CatalogModelWant(&self->model,
        Editor_CatalogModelKindAt(kind_index >= 0 && kind_index < Editor_CatalogModelKindCount() ? kind_index : 0),
        self->composed);
    first = ui->chrome->widgets[self->list].imin;
    Editor_CatalogModelWantWindow(&self->model, first, CATALOG_LIST_ROWS);
    catalog_local_follow(ui, self);
    if( host )
        /* Asks only when the kind, the settled text, the window's page or
         * a prefetch is due: at most one round trip. */
        (void)Editor_CatalogModelRefresh(&self->model, host);
    if( host || self->model.source != EDITOR_CATALOG_SOURCE_INDEX )
        Editor_CatalogModelRangeText(&self->model, first, line, sizeof(line));
    else
        snprintf(line, sizeof(line), "no editor server with a catalog");
    ToriRSChrome_SetText(ui->chrome, self->row_count, line);
    catalog_list_show(ui, self);
    catalog_list_rows(ui, self);
    catalog_list_tip(ui, self);
    catalog_record_rows(ui, self);
    catalog_chips_draw(ui, self);
    catalog_shortcuts_draw(ui, self);
    catalog_type_strip_draw(ui, self);
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

/**
 * The pick follows a chosen loc / npc / obj row with an id. `cell` is the
 * keycap the click landed on (ToriRSChrome_VListPickedCell): a shape key
 * picks the loc AT that shape; the body keeps a shape the loc has (or takes
 * its first), or a favourite's / recent pick's remembered pose.
 */
static void
catalog_pick_row(
    struct EditorUi* ui,
    struct CatalogPanel* self,
    int row,
    int cell)
{
    struct Editor_CatalogView view;
    int shape = -1;

    if( !Editor_CatalogModelViewAt(&self->model, row, &view) )
        return;
    if( view.kind > TORIRSMAPED_CATALOG_OBJ )
        return; /* not a placeable kind: the record shows, the pick stays */
    if( view.id < 0 )
    {
        Editor_UiStatus(ui, "Pick refused: %s %s has no id yet (a server-only record); nothing to place",
            ToriRSMapEdCatalog_KindName(view.kind), view.name);
        return;
    }
    ui->tools.pick_kind = (int)view.kind;
    ui->tools.pick_id = view.id;
    snprintf(ui->tools.pick_name, sizeof(ui->tools.pick_name), "%s (%d)", view.name, (int)view.id);
    if( view.kind == TORIRSMAPED_CATALOG_OBJ && cell > 0 )
    {
        /* A wear or stack keycap narrows the list to that chip. */
        char key[EDITOR_CATALOG_CHIP_KEY_MAX];
        char value[EDITOR_CATALOG_CHIP_VALUE_MAX];
        if( Editor_CatalogViewKeyChip(&view, cell, key, value) )
        {
            if( Editor_CatalogChipsAdd(&self->chips, key, value) )
                Editor_UiStatus(ui, "narrowed to %s=%s", key, value);
            else
                Editor_UiStatus(ui, "%s=%s is a chip already", key, value);
        }
    }
    if( view.kind == TORIRSMAPED_CATALOG_LOC )
    {
        if( cell > 0 )
            shape = Editor_CatalogViewShapeAt(&view, cell);
        if( shape >= 0 )
            ui->tools.place_shape = shape;
        else if( view.shape >= 0 )
        {
            ui->tools.place_shape = view.shape;
            ui->tools.place_rotation = view.rotation;
        }
        else if( view.shape_mask
                 && !(ui->tools.place_shape >= 0 && ui->tools.place_shape < EDITOR_LOC_SHAPE_COUNT
                      && (view.shape_mask & (1u << ui->tools.place_shape))) )
            ui->tools.place_shape = Editor_LocShapeFirst(view.shape_mask);
        /* A loc pick arms Place: picking one is asking to put it down; a
         * shape key, whatever tool was armed. */
        if( ui->tools.tool == EDITOR_TOOL_SELECT || shape >= 0 )
            Editor_UiSetTool(ui, EDITOR_TOOL_LOC_PLACE);
        if( shape >= 0 )
            Editor_UiStatus(ui, "Place %s at shape %d (%s), rotation %d", view.name, shape, Editor_LocShapeName(shape),
                ui->tools.place_rotation & 3);
    }
    catalog_note_pick(ui, self, &view);
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
        Editor_UiStatus(ui, "Copy refused: only a loc, npc or obj definition can be copied");
        return 0;
    }
    if( !catalog_name_ok(new_name) )
    {
        Editor_UiStatus(ui, "Copy refused: the new name must be [a-z0-9_], under %d bytes", EDITOR_CMD_NAME_MAX);
        return 0;
    }
    if( !ui->editor || !ui->editor->host_open )
    {
        Editor_UiStatus(ui, "Copy refused: no editor server to copy on");
        return 0;
    }
    type = ToriRSMapEdCatalog_KindName(kind);
    status = Editor_CopyRecord(ui->editor, type, name, new_name, &id);
    if( status != EDITOR_HOST_OK )
    {
        Editor_UiStatus(ui, "Copy refused: copying %s:%s to %s (%s)", type, name, new_name,
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
    struct Editor_CatalogView view;
    const char* new_name = ToriRSChrome_Text(ui->chrome, self->input_new_name);

    if( self->selected < 0 || !Editor_CatalogModelViewAt(&self->model, self->selected, &view) )
    {
        Editor_UiStatus(ui, "Copy refused: pick the loc, npc or obj to copy first");
        return;
    }
    if( !Editor_Catalog_CopyRecord(ui, view.kind, view.name, new_name ? new_name : "") )
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

/** A "+ filter" row: a whole chip, or "key=" put in Find for the value. */
static void
catalog_filter_menu(
    struct EditorUi* ui,
    struct CatalogPanel* self,
    int index)
{
    char key[EDITOR_CATALOG_CHIP_KEY_MAX];
    char value[EDITOR_CATALOG_CHIP_VALUE_MAX];
    int kind;

    if( index <= 0 || index >= Editor_CatalogFilterMenuCountFor(catalog_kind(ui, self)) )
        return;
    kind = Editor_CatalogFilterMenuItemFor(catalog_kind(ui, self), index, key, value);
    if( kind == 1 )
    {
        if( !Editor_CatalogChipsAdd(&self->chips, key, value) )
            Editor_UiStatus(ui, "Filter refused: %s=%s is a chip already, or %d chips are the most",
                key, value, EDITOR_CATALOG_CHIPS_MAX);
    }
    else if( kind == 2 )
    {
        const char* text = ToriRSChrome_Text(ui->chrome, self->input_search);
        char box[TORIRS_CHROME_INPUT_MAX];
        snprintf(box, sizeof(box), "%s%s%s=", text && text[0] ? text : "", text && text[0] ? " " : "", key);
        ToriRSChrome_SetText(ui->chrome, self->input_search, box);
        (void)ToriRSChrome_FocusWidget(ui->chrome, self->input_search, (int)strlen(box));
        Editor_UiStatus(ui, "type the %s to filter by after %s= (a space makes it a chip)", key, key);
    }
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_filter, 0);
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
        /* Choosing a kind again asks again: the tree may have changed. A
         * kind is a question for the index. */
        Editor_CatalogModelInvalidate(&self->model);
        Editor_CatalogModelSetSource(&self->model, EDITOR_CATALOG_SOURCE_INDEX);
        return 1;
    }
    if( widget == self->list )
    {
        int const choice = ToriRSChrome_VListSelected(ui->chrome, widget);
        self->selected = choice >= 0 && choice < Editor_CatalogModelViewCount(&self->model) ? choice : -1;
        if( self->selected >= 0 )
            catalog_pick_row(ui, self, self->selected, ToriRSChrome_VListPickedCell(ui->chrome, widget));
        catalog_picked_label(ui, self);
        catalog_record_show(ui, self);
        return 1;
    }
    if( widget == self->dropdown_filter )
    {
        catalog_filter_menu(ui, self, ToriRSChrome_DropdownSelected(ui->chrome, widget));
        return 1;
    }
    if( widget == self->dropdown_sort )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        if( choice >= 0 && choice < EDITOR_CATALOG_SORT_COUNT )
            Editor_CatalogModelSetSort(&self->model, (enum Editor_CatalogSort)choice);
        return 1;
    }
    if( widget == self->button_copy )
    {
        catalog_copy(ui, self);
        return 1;
    }
    if( widget == self->button_add_palette )
    {
        (void)Editor_CatalogPalette_AddPick(ui, 0);
        return 1;
    }
    if( widget == self->button_edit_record )
    {
        struct Editor_CatalogView view;
        if( self->selected < 0 || !Editor_CatalogModelViewAt(&self->model, self->selected, &view) )
        {
            Editor_UiStatus(ui, "Edit refused: pick the loc, npc or obj to edit first");
            return 1;
        }
        (void)Editor_Record_Open(ui, view.kind, view.name);
        return 1;
    }
    if( widget == self->button_open_script )
    {
        struct EditorHost* host = catalog_host(ui);
        const struct EditorHost_CatalogRecord* record = NULL;
        struct Editor_CatalogView view;
        const char* file = NULL;
        const char* line = NULL;
        char rel[EDITOR_SCRIPT_PATH_MAX];
        if( self->selected < 0 || !Editor_CatalogModelViewAt(&self->model, self->selected, &view)
            || view.kind != TORIRSMAPED_CATALOG_TRIGGER )
        {
            Editor_UiStatus(ui, "Open in editor: pick a trigger row (Kind: trigger)");
            return 1;
        }
        if( host )
            record = Editor_CatalogModelRecordAt(&self->model, host, self->selected);
        for( int f = 0; record && f < record->field_count; f++ )
        {
            if( strcmp(record->fields[f].key, "file") == 0 )
                file = record->fields[f].value;
            else if( strcmp(record->fields[f].key, "line") == 0 )
                line = record->fields[f].value;
        }
        if( !file || !Editor_ScriptRelFromContentPath(file, rel, sizeof(rel)) || !Editor_UiPanelSelf(ui, "triggers") )
        {
            Editor_UiStatus(ui, "Open in editor: %s is not a script this window can open", file ? file : "?");
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
    return widget == self->input_search || widget == self->record_list || widget == self->input_new_name
           || widget == self->chip_strip.graph || widget == self->shortcut_strip.graph || widget == self->type_strip.graph
           || widget == self->view_strip.graph;
}

/** A press on a chip: a filter chip's x removes it; a shortcut shows its
 *  list (again: the index), "same category" adds the pick's category. */
static int
catalog_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct CatalogPanel* self = opaque;
    int on_x = 0;
    int index;

    if( event->widget == self->chip_strip.graph )
    {
        if( event->kind != TORIRS_CHROME_EVENT_GRAPH_PRESS )
            return 1;
        index = edp_chips_key_of(&self->chip_strip, event->key, &on_x);
        if( index >= 0 && index < self->chips.count && on_x )
        {
            char label[EDP_CHIP_LABEL_MAX];
            Editor_CatalogChipLabel(&self->chips.chip[index], label, sizeof(label));
            Editor_CatalogChipsRemove(&self->chips, index);
            Editor_UiStatus(ui, "filter %s removed", label);
        }
        return 1;
    }
    if( event->widget == self->view_strip.graph )
    {
        if( event->kind != TORIRS_CHROME_EVENT_GRAPH_PRESS )
            return 1;
        index = edp_chips_key_of(&self->view_strip, event->key, &on_x);
        if( index == 1 && !Editor_Gallery_Visible(ui) )
            Editor_UiPanelOpenBeside(ui, "gallery", "catalog");
        return 1;
    }
    if( event->widget == self->type_strip.graph )
    {
        if( event->kind != TORIRS_CHROME_EVENT_GRAPH_PRESS )
            return 1;
        index = edp_chips_key_of(&self->type_strip, event->key, &on_x);
        if( index < 0 || index >= EDP_CHIPS_MAX )
            return 1;
        if( index == self->type_more )
            ToriRSChrome_SetHidden(ui->chrome, self->dropdown_kind, !ui->chrome->widgets[self->dropdown_kind].hidden);
        else if( self->type_kind_of[index] >= 0 && (uint32_t)self->type_kind_of[index] != catalog_kind(ui, self) )
            catalog_set_kind(ui, self, (uint32_t)self->type_kind_of[index]);
        return 1;
    }
    if( event->widget != self->shortcut_strip.graph )
        return 0;
    if( event->kind != TORIRS_CHROME_EVENT_GRAPH_PRESS )
        return 1;
    index = edp_chips_key_of(&self->shortcut_strip, event->key, &on_x);
    if( index == CATALOG_SHORTCUT_FAVOURITES )
        catalog_show_source(ui, self, EDITOR_CATALOG_SOURCE_FAVOURITES);
    else if( index == CATALOG_SHORTCUT_RECENT )
        catalog_show_source(ui, self, EDITOR_CATALOG_SOURCE_RECENT);
    else if( index == CATALOG_SHORTCUT_IN_SQUARE )
        catalog_show_source(ui, self, EDITOR_CATALOG_SOURCE_IN_SQUARE);
    else if( index == CATALOG_SHORTCUT_CATEGORY )
    {
        catalog_symbol_follow(ui, self);
        if( !self->sym_category[0] )
            Editor_UiStatus(ui, "Same category refused: %s", ui->tools.pick_kind < 0 ? "nothing is picked"
                                                                                       : "the pick states no category");
        else
        {
            (void)Editor_CatalogChipsAdd(&self->chips, "category", self->sym_category);
            Editor_CatalogModelSetSource(&self->model, EDITOR_CATALOG_SOURCE_INDEX);
        }
    }
    return 1;
}

static void
catalog_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct CatalogPanel* self = opaque;
    (void)ui;
    Editor_CatalogModelFree(&self->model);
    Editor_FavouritesFree(&self->favourites);
    ToriMem_Free(ToriMem_Editor(), self->in_square_ids);
    ToriMem_Free(ToriMem_Editor(), self->in_square_set);
    ToriMem_Free(ToriMem_Editor(), self->local_build);
    ToriMem_Free(ToriMem_Editor(), self);
}

int
Editor_Catalog_PreviewId(struct EditorUi* ui)
{
    struct CatalogPanel* self;
    int rung;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self || ui->tools.pick_kind < 0 )
        return -1;
    rung = Editor_Catalog_PreviewRung(ui);
    if( rung == -1 )
        return ui->tools.pick_id;
    return rung >= 0 ? rung : -1;
}

int
Editor_Catalog_PreviewRung(struct EditorUi* ui)
{
    struct CatalogPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self || ui->tools.pick_kind != CACHEPROVIDER_CATALOG_LOC )
        return -1;
    /* The rung only while the rungs are this pick's (they are rebuilt when
     * the panel next ticks open; a closed panel keeps an older pick's). */
    if( self->var_count > 0 && self->var_choice > 0 && self->var_shown_id == ui->tools.pick_id
        && self->var_shown_kind == ui->tools.pick_kind )
        return catalog_variant_id(self) >= 0 ? catalog_variant_id(self) : -2;
    return -1;
}

int
Editor_Catalog_PickRecord(
    struct EditorUi* ui,
    uint32_t kind,
    const char* name)
{
    struct CatalogPanel* self;
    struct EditorHost* host;
    struct Editor_CatalogLocalRow row;
    struct Editor_CatalogView view;

    assert(ui);
    assert(name);
    assert(kind <= TORIRSMAPED_CATALOG_OBJ);
    self = Editor_UiPanelSelf(ui, "catalog");
    host = catalog_host(ui);
    if( !self || !host )
    {
        Editor_UiStatus(ui, "Pick refused: no catalog to pick %s %s from", ToriRSMapEdCatalog_KindName(kind), name);
        return 0;
    }
    memset(&row, 0, sizeof(row));
    row.kind = kind;
    row.id = -1;
    row.shape = -1;
    snprintf(row.name, sizeof(row.name), "%s", name);
    if( !catalog_resolve_local(host, &row) || row.id < 0 )
    {
        Editor_UiStatus(ui, "Pick refused: the catalog has no %s %s with an id", ToriRSMapEdCatalog_KindName(kind),
            name);
        return 0;
    }
    ui->tools.pick_kind = (int)kind;
    ui->tools.pick_id = row.id;
    snprintf(ui->tools.pick_name, sizeof(ui->tools.pick_name), "%s (%d)", name, (int)row.id);
    if( kind == TORIRSMAPED_CATALOG_LOC && ui->tools.tool == EDITOR_TOOL_SELECT )
        Editor_UiSetTool(ui, EDITOR_TOOL_LOC_PLACE);
    memset(&view, 0, sizeof(view));
    view.kind = row.kind;
    view.id = row.id;
    view.name = row.name;
    view.display = row.display;
    view.category = row.category;
    view.shape_mask = row.shape_mask;
    view.size_w = row.size_w;
    view.size_l = row.size_l;
    view.shape = -1;
    catalog_note_pick(ui, self, &view);
    catalog_publish_pick(ui);
    catalog_picked_label(ui, self);
    /* Shown in the list: that kind, found by name, its row selected and
     * scrolled into view when the page lands (catalog_list_show). */
    Editor_CatalogModelSetSource(&self->model, EDITOR_CATALOG_SOURCE_INDEX);
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_kind, Editor_CatalogModelKindIndex(kind));
    ToriRSChrome_SetText(ui->chrome, self->input_search, name);
    snprintf(self->select_name, sizeof(self->select_name), "%s", name);
    self->select_kind = kind;
    return 1;
}

int
Editor_Catalog_PickFromWorld(
    struct EditorUi* ui,
    int loc_id,
    int shape,
    int rotation)
{
    struct CatalogPanel* self;
    const char* name;

    assert(ui);
    assert(loc_id >= 0);
    assert(shape >= 0);
    assert(shape < EDITOR_LOC_SHAPE_COUNT);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self )
    {
        Editor_UiStatus(ui, "Pick refused: this window has no catalog to pick into");
        return 0;
    }
    ui->tools.pick_kind = CACHEPROVIDER_CATALOG_LOC;
    ui->tools.pick_id = loc_id;
    /* Its exact pose: what the hovered placement is, Place stamps. */
    ui->tools.place_shape = shape;
    ui->tools.place_rotation = rotation & 3;
    Editor_UiSetTool(ui, EDITOR_TOOL_LOC_PLACE);
    name = catalog_pick_symbol(ui, self);
    if( name[0] )
    {
        struct Editor_CatalogView view;
        snprintf(ui->tools.pick_name, sizeof(ui->tools.pick_name), "%s (%d)", name, loc_id);
        memset(&view, 0, sizeof(view));
        view.kind = TORIRSMAPED_CATALOG_LOC;
        view.id = loc_id;
        view.name = self->sym_name;
        view.display = "";
        view.category = self->sym_category;
        view.shape_mask = self->sym_mask;
        view.shape = -1;
        catalog_note_pick(ui, self, &view);
    }
    else
    {
        struct ToriRS_Location* cfg = catalog_loc_record(ui, loc_id);
        snprintf(ui->tools.pick_name, sizeof(ui->tools.pick_name), "%s (%d)",
            cfg && cfg->name[0] ? cfg->name : "<unnamed>", loc_id);
    }
    catalog_publish_pick(ui);
    catalog_picked_label(ui, self);
    /* Find = its name (its id without a catalog), the chips off so nothing
     * hides it, and its row selected and scrolled into view once it lands. */
    Editor_CatalogChipsInit(&self->chips);
    Editor_CatalogModelSetSource(&self->model, EDITOR_CATALOG_SOURCE_INDEX);
    ToriRSChrome_DropdownSetSelected(
        ui->chrome, self->dropdown_kind, Editor_CatalogModelKindIndex(TORIRSMAPED_CATALOG_LOC));
    if( name[0] )
    {
        ToriRSChrome_SetText(ui->chrome, self->input_search, name);
        snprintf(self->select_name, sizeof(self->select_name), "%s", name);
        self->select_kind = TORIRSMAPED_CATALOG_LOC;
    }
    else
    {
        char id_text[16];
        snprintf(id_text, sizeof(id_text), "%d", loc_id);
        ToriRSChrome_SetText(ui->chrome, self->input_search, id_text);
    }
    Editor_UiStatus(ui, "picked %s at shape %d (%s), rotation %d", ui->tools.pick_name, shape,
        Editor_LocShapeName(shape), rotation & 3);
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
    Editor_CatalogModelSetSource(&self->model, EDITOR_CATALOG_SOURCE_INDEX);
    ToriRSChrome_DropdownSetSelected(
        ui->chrome, self->dropdown_kind, Editor_CatalogModelKindIndex(TORIRSMAPED_CATALOG_LOC));
    {
        char id_text[16];
        snprintf(id_text, sizeof(id_text), "%d", loc_id);
        ToriRSChrome_SetText(ui->chrome, self->input_search, id_text);
    }
}

/* ---- the pick's actions (the Preview pane's row) ------------------------------- */

/** The favourite row for the pick, with Place's pose for a loc. 0 (said)
 *  when there is no pick or no record name for it. */
static int
catalog_favourite_row(
    struct EditorUi* ui,
    struct CatalogPanel* self,
    struct Editor_FavouriteRow* out)
{
    const char* name;

    if( ui->tools.pick_kind < 0 || ui->tools.pick_kind > (int)TORIRSMAPED_CATALOG_OBJ || ui->tools.pick_id < 0 )
    {
        Editor_UiStatus(ui, "Favourite refused: pick a loc, npc or obj first");
        return 0;
    }
    name = catalog_pick_symbol(ui, self);
    if( !name[0] || strlen(name) >= sizeof(out->record) )
    {
        Editor_UiStatus(ui, "Favourite refused: the catalog has no record name for %s", ui->tools.pick_name);
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->kind = ui->tools.pick_kind;
    snprintf(out->record, sizeof(out->record), "%s", name);
    out->shape = -1;
    if( out->kind == EDITOR_PALETTE_LOC && ui->tools.place_shape >= 0
        && ui->tools.place_shape < EDITOR_LOC_SHAPE_COUNT )
    {
        out->shape = ui->tools.place_shape;
        out->rotation = ui->tools.place_rotation & 3;
    }
    return 1;
}

int
Editor_Catalog_IsFavourite(struct EditorUi* ui)
{
    struct CatalogPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self || ui->tools.pick_kind < 0 || ui->tools.pick_id < 0 )
        return 0;
    catalog_favourites_follow(ui, self);
    return catalog_pick_symbol(ui, self)[0]
           && Editor_FavouritesFind(&self->favourites, ui->tools.pick_kind, self->sym_name) >= 0;
}

int
Editor_Catalog_ToggleFavourite(struct EditorUi* ui)
{
    struct CatalogPanel* self;
    struct Editor_FavouriteRow row;
    struct Editor_Cmd command;
    struct Editor_DocHandle handle;
    int was;

    assert(ui);
    was = Editor_Catalog_IsFavourite(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self || !ui->editor )
    {
        Editor_UiStatus(ui, "Favourite refused: no catalog or no session in this window");
        return -1;
    }
    if( !catalog_favourite_row(ui, self, &row) )
        return -1;
    if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, EDITOR_FAVOURITES_DOC_KEY, &handle) || !handle.text )
    {
        Editor_UiStatus(ui, "Favourite refused: %s is not open", EDITOR_FAVOURITES_FILE_PATH);
        return -1;
    }
    if( was ? !Editor_FavouritesRemoveCommand(&command, &self->favourites, row.kind, row.record)
            : !Editor_FavouritesAddCommand(&command, &self->favourites, &row) )
    {
        Editor_UiStatus(ui, "Favourite refused: %s:%s cannot be written to %s", Editor_PaletteKindName(row.kind),
            row.record, EDITOR_FAVOURITES_FILE_PATH);
        return -1;
    }
    if( edp_apply(ui, &command, 1, was ? "unfavourite" : "favourite") <= 0 )
    {
        Editor_UiStatus(ui, "Favourite refused: the server did not take the edit to %s", EDITOR_FAVOURITES_FILE_PATH);
        return -1;
    }
    catalog_favourites_follow(ui, self);
    if( was )
        Editor_UiStatus(ui, "%s is no longer a favourite", row.record);
    else if( row.shape >= 0 )
        Editor_UiStatus(ui, "%s is a favourite, at shape %d rot %d (unsaved; Save writes it)", row.record, row.shape,
            row.rotation);
    else
        Editor_UiStatus(ui, "%s is a favourite (unsaved; Save writes it)", row.record);
    return !was;
}

int
Editor_Catalog_FindInWorld(struct EditorUi* ui)
{
    const struct Editor_Doc* doc;
    int found = 0;
    int first_x = -1;
    int first_z = -1;
    int first_level = 0;

    assert(ui);
    if( ui->tools.pick_kind != CACHEPROVIDER_CATALOG_LOC || ui->tools.pick_id < 0 )
    {
        Editor_UiStatus(ui, "Find in world refused: pick a loc first");
        return 0;
    }
    if( !ui->editor )
    {
        Editor_UiStatus(ui, "Find in world refused: no session in this window");
        return 0;
    }
    doc = &ui->editor->doc;
    for( int s = 0; s < doc->square_count; s++ )
    {
        const struct Editor_Square* square = &doc->squares[s];
        if( !square->loaded )
            continue;
        for( int i = 0; i < square->loc_count; i++ )
        {
            const struct Editor_Loc* loc = &square->locs[i];
            struct Editor_LocRef ref;
            int const wx = square->map_x * 64 + loc->x;
            int const wz = square->map_z * 64 + loc->z;
            if( loc->loc_id != ui->tools.pick_id )
                continue;
            if( found < EDITOR_LOC_SELECTION_MAX )
            {
                Editor_LocRefAt(&ref, wx, wz, loc->level, loc->shape, loc->loc_id, loc->rotation);
                if( found == 0 )
                {
                    Editor_UiSelectLoc(ui, &ref);
                    first_x = wx;
                    first_z = wz;
                    first_level = loc->level;
                }
                else
                    (void)Editor_UiToggleLoc(ui, &ref);
            }
            found++;
        }
    }
    if( found == 0 )
    {
        Editor_UiStatus(ui, "Find in world: %s is not placed in the open squares", ui->tools.pick_name);
        return 0;
    }
    if( ui->app_services.camera_to_tile )
        (void)ui->app_services.camera_to_tile(ui->app_services.user_data, first_x, first_z, first_level);
    Editor_UiStatus(ui, "Find in world: %d placement%s of %s in the open squares%s", found, found == 1 ? "" : "s",
        ui->tools.pick_name, found > EDITOR_LOC_SELECTION_MAX ? " (the first ones selected)" : ", selected");
    return found;
}

int
Editor_Catalog_EditPick(struct EditorUi* ui)
{
    struct CatalogPanel* self;
    const char* name;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self || ui->tools.pick_kind < 0 || ui->tools.pick_kind > (int)TORIRSMAPED_CATALOG_OBJ )
    {
        Editor_UiStatus(ui, "Record refused: pick a loc, npc or obj first");
        return 0;
    }
    name = catalog_pick_symbol(ui, self);
    if( !name[0] )
    {
        Editor_UiStatus(ui, "Record refused: the catalog has no record name for %s", ui->tools.pick_name);
        return 0;
    }
    return Editor_Record_Open(ui, (uint32_t)ui->tools.pick_kind, name);
}

int
Editor_Catalog_CopyPick(struct EditorUi* ui)
{
    struct CatalogPanel* self;
    const char* name;
    const char* new_name;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self || ui->tools.pick_kind < 0 || ui->tools.pick_kind > (int)TORIRSMAPED_CATALOG_OBJ )
    {
        Editor_UiStatus(ui, "Copy refused: pick a loc, npc or obj first");
        return 0;
    }
    name = catalog_pick_symbol(ui, self);
    new_name = ToriRSChrome_Text(ui->chrome, self->input_new_name);
    if( !name[0] )
    {
        Editor_UiStatus(ui, "Copy refused: the catalog has no record name for %s", ui->tools.pick_name);
        return 0;
    }
    if( !new_name || !new_name[0] )
    {
        Editor_UiStatus(ui, "Copy refused: type the new name in the Catalog's New name first");
        return 0;
    }
    if( !Editor_Catalog_CopyRecord(ui, (uint32_t)ui->tools.pick_kind, name, new_name) )
        return 0;
    Editor_CatalogModelInvalidate(&self->model);
    catalog_picked_label(ui, self);
    return 1;
}

uint32_t
Editor_Catalog_PickShapes(struct EditorUi* ui)
{
    struct CatalogPanel* self;
    uint32_t mask = 0;

    assert(ui);
    if( ui->tools.pick_kind != CACHEPROVIDER_CATALOG_LOC || ui->tools.pick_id < 0 )
        return 0;
    /* The config's shapes when it is resident (what the world draws), else
     * the catalog's summary fields. */
    if( edp_loc_shapes(ui, ui->tools.pick_id, &mask) && mask )
        return mask;
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self )
        return 0;
    catalog_symbol_follow(ui, self);
    return self->sym_mask;
}

const struct Editor_CatalogRecents*
Editor_Catalog_Recents(struct EditorUi* ui)
{
    struct CatalogPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    return self ? &self->recents : NULL;
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
    Editor_CatalogModelInit(&self->model, TORIRSMAPED_CATALOG_LOC, CATALOG_PAGE_ROWS);
    Editor_CatalogChipsInit(&self->chips);
    Editor_FavouritesInit(&self->favourites);
    self->shown_key = (uint32_t)-1;
    self->shown_answers = (uint32_t)-1;
    self->selected = -1;
    self->record_built = -1;
    self->var_shown_id = -1;
    self->var_shown_kind = -1;
    self->local_source = -1;
    self->sym_kind = -2;
    self->sym_id = -2;
    self->tip_row = -2;

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8), edp_px(ui, 420),
        edp_px(ui, 260), "Catalog");
    assert(self->panel >= 0);
    /* Twelve chips: two lines at the panel's width. */
    edp_chips_init(ui, &self->type_strip, self->panel, 2);
    self->type_more = -1;
    self->shown_filter_kind = 0xFFFFFFFFu;
    self->dropdown_kind = ToriRSChrome_Dropdown(chrome, self->panel, "Kind", Editor_CatalogModelKindLabels(),
        Editor_CatalogModelKindCount(), 0);
    /* Every other kind lives here, shown by the strip's "more". */
    ToriRSChrome_SetHidden(chrome, self->dropdown_kind, 1);
    /* The Stamps kind (workbench 6.3): a chip beside Kind that opens the
     * Stamps panel, which owns it (edpanel_stamps.c). */
    Editor_Stamps_CatalogChip(ui, self->panel);
    self->input_search = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(chrome, self->input_search, "name, #id, obj:/npc:/loc:; key=value + space = a chip");
    edp_chips_init(ui, &self->view_strip, self->panel, 1);
    self->row_count = ToriRSChrome_Label(chrome, self->panel, "");
    edp_chips_init(ui, &self->chip_strip, self->panel, 2);
    ToriRSChrome_SetHidden(chrome, self->chip_strip.graph, 1);
    self->dropdown_filter = ToriRSChrome_Dropdown(chrome, self->panel, "Filter", Editor_CatalogFilterMenuLabels(),
        Editor_CatalogFilterMenuCount(), 0);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_filter, "add a filter chip on a summary field");
    self->dropdown_sort = ToriRSChrome_Dropdown(chrome, self->panel, "Sort", Editor_CatalogSortLabels(),
        EDITOR_CATALOG_SORT_COUNT, EDITOR_CATALOG_SORT_BEST);
    edp_chips_init(ui, &self->shortcut_strip, self->panel, 1);
    self->list = ToriRSChrome_VListCells(chrome, self->panel, Editor_CatalogCellsHeader(), CATALOG_LIST_ROWS, 0);
    assert(self->list >= 0);
    ToriRSChrome_Separator(chrome, self->panel);
    self->row_picked = ToriRSChrome_Label(chrome, self->panel, "nothing picked");
    self->row_record = ToriRSChrome_Label(chrome, self->panel, "pick a row for its record");
    self->record_list = ToriRSChrome_VList(chrome, self->panel, "", CATALOG_RECORD_ROWS, 0);
    self->button_edit_record = ToriRSChrome_Button(chrome, self->panel, "Edit record...");
    ToriRSChrome_SetTooltip(chrome, self->button_edit_record,
        "every field of the picked loc/npc/obj");
    self->button_add_palette = ToriRSChrome_Button(chrome, self->panel, "Add to palette");
    ToriRSChrome_SetTooltip(chrome, self->button_add_palette,
        "the pick into the Catalog palette's first free slot");
    edp_button_chord(ui, self->button_add_palette, EDITOR_HK_PALETTE_ADD_PICK);
    self->chord_serial = ui->hotkeys.serial;
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
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "catalog";
    desc.title = "Catalog";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = catalog_tick;
    desc.activate = catalog_activate;
    desc.event = catalog_event;
    desc.on_state = catalog_on_state;
    desc.destroy = catalog_destroy;
    Editor_UiAddPanel(ui, &desc);

    /* The Preview shows the catalog's pick, and sits next to it in the View
     * menu and the dock order. */
    if( !Editor_UiPanelSelf(ui, "preview") )
        Editor_Preview_Register(ui);
    if( !Editor_UiPanelSelf(ui, "gallery") )
        Editor_Gallery_Register(ui);
    /* The record editor travels with the catalog it opens records from. */
    if( !Editor_UiPanelSelf(ui, "record") )
        Editor_Record_Register(ui);
    if( !Editor_UiPanelSelf(ui, "stamps") )
        Editor_Stamps_Register(ui);
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

uint32_t
Editor_Catalog_Kind(struct EditorUi* ui)
{
    struct CatalogPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    return self ? catalog_kind(ui, self) : TORIRSMAPED_CATALOG_LOC;
}

void
Editor_Catalog_SetKind(
    struct EditorUi* ui,
    uint32_t kind)
{
    struct CatalogPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( self && kind != catalog_kind(ui, self) )
        catalog_set_kind(ui, self, kind);
}

const char*
Editor_Catalog_FindText(struct EditorUi* ui)
{
    struct CatalogPanel* self;
    const char* text;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self )
        return "";
    text = ToriRSChrome_Text(ui->chrome, self->input_search);
    return text ? text : "";
}

void
Editor_Catalog_SetFindText(
    struct EditorUi* ui,
    const char* text)
{
    struct CatalogPanel* self;

    assert(ui);
    assert(text);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( self )
        ToriRSChrome_SetText(ui->chrome, self->input_search, text);
}

void
Editor_Catalog_WantWindow(
    struct EditorUi* ui,
    int first,
    int rows)
{
    struct CatalogPanel* self;

    assert(ui);
    assert(first >= 0);
    assert(rows > 0);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( self )
        Editor_CatalogModelWantWindow(&self->model, first, rows);
}

int
Editor_Catalog_PickRow(
    struct EditorUi* ui,
    int row)
{
    struct CatalogPanel* self;
    struct Editor_CatalogView view;

    assert(ui);
    assert(row >= 0);
    self = Editor_UiPanelSelf(ui, "catalog");
    if( !self || !Editor_CatalogModelViewAt(&self->model, row, &view) )
        return 0;
    self->selected = row;
    ToriRSChrome_VListSetSelected(ui->chrome, self->list, row);
    catalog_pick_row(ui, self, row, 0);
    catalog_picked_label(ui, self);
    catalog_record_show(ui, self);
    return view.kind <= TORIRSMAPED_CATALOG_OBJ && view.id >= 0;
}
