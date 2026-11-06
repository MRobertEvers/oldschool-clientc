/*
 * The Record panel (module "record"): one loc / npc / obj config record
 * across every file that states it, every field editable. Opened from the
 * Catalog's "Edit record...", the Preview's Record chip, the Spawns panel and
 * a Problems row (Editor_Record_Open). Moved out of edpanel_catalog.c whole;
 * the Catalog registers it beside itself (Editor_Catalog_Register).
 *
 * Workbench 6.6 (design section 9, "Records and triggers") made it the
 * record editor of the figure:
 *
 *   tabs        Cache block, one Server overlay tab per overlay file that
 *               states the record (named by its file), Triggers, Shell, Where
 *               used. A layer's tab lists the fields that layer states (the
 *               top layer's tab adds the register's fields nobody states,
 *               where Set puts them).
 *   rows        typed from fields/<type>.ini (Editor_CfgRecordType): the
 *               field, the value this layer states, its source badge (cache,
 *               overlay, generated; "shadowed" where another layer wins);
 *               "!" marks an issue (an unknown key, an unknown param, a value
 *               out of range), which is also a Problems row whose click comes
 *               back here; "*" a field changed since the record was opened
 *               or last clean.
 *   Find        the search inside the record (field, value, file, kind), and
 *               the "changed only" chip; "dirty, N fields" beside them, with
 *               "needs a client pack on RUN" when a changed field is one the
 *               client reads.
 *   the editor  for the picked row, by its kind: a number has step buttons
 *               and its range in the tooltip; a bool a checkbox; an enum a
 *               dropdown of its words; a name (and a param's name) a picker
 *               whose typing filters the catalog's records of the kind the
 *               field names; a param two columns, name and value; text a
 *               field. Set writes it (one undo step) where Write to says.
 *   Triggers    the subject's triggers (the Triggers panel's rows), Open in
 *               editor, and Check (the editor server's job).
 *   Shell       a multiloc / multinpc's table and summary, read from the
 *               cache block; Shell editor... opens the builder on it.
 *   Where used  the catalog's CATALOG_USES, a slice a frame: spawns,
 *               placements per square, triggers, scripts, interfaces, cs2,
 *               overlays, palettes and stamps that name it.
 *
 * Every edit is an Editor_Cmd through the authority (edp_apply); a refusal
 * says why in the status bar.
 */

#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/builder/builder_panels.h"
#include "editor/builder/builder_shell.h"
#include "editor/editor_cfg.h"
#include "editor/editor_host.h"
#include "editor/editor_script.h"
#include "editor/map_editor_ghost.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct EditorHost*
record_host(struct EditorUi* ui)
{
    if( !ui->editor || !ui->editor->host_open || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return NULL;
    return &ui->editor->host;
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
 *   select a row   its field and value fill the typed editor; the row says
 *                  which file holds it and how many beneath it are shadowed.
 *   Set field      writes the value into the file that holds the field (Write
 *                  to: "where it is stated"), or into the chosen layer -- an
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
#define RECORD_PICK_ROWS 5
#define RECORD_PICKS_MAX 20
#define RECORD_TABS_MAX (EDITOR_CFG_RECORD_LAYERS_MAX + 3)
#define RECORD_KNOWN_MAX 32
#define RECORD_USES_MAX 512
#define RECORD_TRIGGERS_MAX 64
#define RECORD_WORDS_MAX 16
/** Rows one tab lists at most (a record states a few dozen). */
#define RECORD_ROWS_SHOWN 1024

struct RecordUse
{
    struct EditorHost_CatalogUse view;
    char path[256];
    char text[192];
};

struct RecordPanel
{
    int panel;
    int row_title;
    int tabs;
    /* the field tabs */
    int input_find;
    int check_changed;
    int label_dirty;
    int list;
    int label_field;
    int input_field;
    int input_param;
    int input_value;
    int button_minus;
    int button_plus;
    int check_value;
    int dropdown_value;
    int list_pick;
    int dropdown_layer;
    int button_set;
    int button_remove;
    int row_hint;
    /* The record's own actions, on every tab. */
    int button_find_world;
    int input_copy_name;
    int button_copy;
    /* Triggers and Shell: one Find over whichever list shows. */
    int input_list_find;
    /* Triggers */
    int list_triggers;
    int button_trigger_open;
    int button_check;
    int button_triggers;
    /* Shell */
    int label_shell;
    int list_shell;
    int button_shell;
    /* Where used */
    int input_uses_find;
    int label_uses;
    int list_uses;
    int button_uses_open;

    int open;
    uint32_t kind;
    char type[16];
    char name[EDITOR_CMD_NAME_MAX];
    char overlays[EDITOR_CFG_RECORD_LAYERS_MAX][EDITOR_DOC_KEY_MAX];
    /** Each overlay's block header line in its file (the catalog's), 0 when
     *  not known. */
    int overlay_lines[EDITOR_CFG_RECORD_LAYERS_MAX];
    int overlay_count;
    struct Editor_CfgRecord record;
    /** The record when it was opened or last clean: "changed only". */
    struct Editor_CfgRecord base;
    int have_base;
    struct Editor_CfgRegisterField fields[EDITOR_CFG_REGISTER_MAX];
    int field_count;
    /** Params the catalog knows that the register does not list. */
    char known[RECORD_KNOWN_MAX][EDITOR_CMD_NAME_MAX];
    const char* known_ptrs[RECORD_KNOWN_MAX];
    int known_count;
    /** The fact seq the rows were resolved at. */
    uint32_t built_seq;
    int built;

    /* the tabs */
    char tab_titles[RECORD_TABS_MAX][TORIRS_CHROME_LABEL_MAX];
    const char* tab_ptrs[RECORD_TABS_MAX];
    int tab_roles[RECORD_TABS_MAX];
    /** A layer tab's layer, by its document key (layer indices move). */
    char tab_layers[RECORD_TABS_MAX][EDITOR_DOC_KEY_MAX];
    int tab_count;
    int shown_tab;

    /* the list the field tabs show */
    int* visible;
    int visible_count;
    uint32_t visible_seq;
    char visible_find[64];
    int visible_changed;
    int visible_tab;
    int visible_valid;
    /** The picked row (an index into record.rows), -1 for none. */
    int picked;
    char picked_name[EDITOR_CMD_NAME_MAX * 2];
    int changed_count;
    int needs_client;

    /* the typed editor */
    int editor_kind;
    char words[RECORD_WORDS_MAX][32];
    const char* word_ptrs[RECORD_WORDS_MAX + 1];
    int word_count;
    char picks[RECORD_PICKS_MAX][EDITOR_CMD_NAME_MAX];
    char pick_dims[RECORD_PICKS_MAX][64];
    int pick_count;
    char pick_query[EDITOR_CMD_NAME_MAX];
    uint32_t pick_kind;
    /** 1: the picker fills the param's name, 0: the value. */
    int pick_for_param;

    char layer_labels[EDITOR_CFG_RECORD_LAYERS_MAX + 1][TORIRS_CHROME_LABEL_MAX];
    const char* layer_options[EDITOR_CFG_RECORD_LAYERS_MAX + 1];

    /* Triggers */
    struct Editor_ScriptRef triggers[RECORD_TRIGGERS_MAX];
    int trigger_count;
    /** The triggers the Find leaves, as indices into `triggers`: row r of the
     *  list is triggers[trigger_shown[r]]. */
    int trigger_shown[RECORD_TRIGGERS_MAX];
    int trigger_shown_count;
    /* Shell */
    struct Builder_Shell* shell;
    int is_shell;
    /** The slots the Find leaves, as indices into shell->slots. */
    int shell_shown[BUILDER_SHELL_SLOTS_MAX];
    int shell_shown_count;
    /** The Find text the Triggers / Shell rows were last built from. */
    char list_find[TORIRS_CHROME_INPUT_MAX];
    /* Where used */
    struct RecordUse* uses;
    int use_count;
    int uses_dropped;
    uint32_t uses_cursor;
    int uses_searching;
    int uses_started;
    uint32_t uses_files_done;
    uint32_t uses_files_total;
    int* uses_visible;
    int uses_visible_count;
    char uses_find[64];
    int uses_built;

    uint32_t hint_serial;
};

static struct RecordPanel*
record_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "record");
}

static const char*
record_rank_name(int rank)
{
    return rank == 0 ? "cache" : rank == 1 ? "generated" : "overlay";
}

static const char*
record_basename(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/** The catalog kind a kind name names ("obj" -> TORIRSMAPED_CATALOG_OBJ);
 *  TORIRSMAPED_CATALOG_KIND_COUNT for none. */
static uint32_t
record_kind_of(const char* name)
{
    for( uint32_t k = 0; k < TORIRSMAPED_CATALOG_KIND_COUNT; k++ )
        if( strcmp(ToriRSMapEdCatalog_KindName(k), name) == 0 )
            return k;
    return TORIRSMAPED_CATALOG_KIND_COUNT;
}

/* ---- resolving ---------------------------------------------------------------------- */

/** Every layer's document into `into`, resolved and typed. */
static void
record_build(
    struct EditorUi* ui,
    struct RecordPanel* self,
    struct Editor_CfgRecord* into)
{
    Editor_CfgRecordFree(into);
    Editor_CfgRecordInit(into, self->type, self->name);
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
        (void)Editor_CfgRecordAddLayer(into, overlay, text, length);
    }
    Editor_CfgRecordResolve(into, self->fields, self->field_count);
    (void)Editor_CfgRecordType(into, self->fields, self->field_count, self->known_ptrs, self->known_count);
}

/** 1 when a layer's document holds unsaved edits. */
static int
record_dirty(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    if( !ui->editor )
        return 0;
    for( int l = 0; l < self->record.layer_count; l++ )
    {
        struct Editor_DocHandle handle;
        if( Editor_DocFind(&ui->editor->doc, EDITOR_DOC_CFG, self->record.layers[l].doc_key, &handle) && handle.text
            && handle.text->dirty )
            return 1;
    }
    return 0;
}

/** The overlay header line of layer `layer` (0 for the cache half). */
static int
record_layer_header_line(
    const struct RecordPanel* self,
    int layer)
{
    const char* label = self->record.layers[layer].label;
    for( int i = 0; i < self->overlay_count; i++ )
        if( strcmp(self->overlays[i], label) == 0 )
            return self->overlay_lines[i];
    return 0;
}

/** Each row with an issue as a Problems row (source "record"). */
static void
record_report(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    (void)Editor_Problems_Clear(ui, "record");
    for( int r = 0; r < self->record.row_count; r++ )
    {
        const struct Editor_CfgRecordRow* row = &self->record.rows[r];
        char field[EDITOR_CMD_NAME_MAX * 2];
        char text[256];
        const char* file = "";
        int line = 0;

        if( row->issue == EDITOR_CFG_ISSUE_NONE )
            continue;
        Editor_CfgRecordRowName(row, field, sizeof(field));
        if( row->layer >= 0 && self->record.layers[row->layer].rank > 0 )
        {
            int const at = Editor_CfgRecordLayerStates(&self->record, row->layer, r, NULL, 0);
            int const header = record_layer_header_line(self, row->layer);
            file = self->record.layers[row->layer].label;
            line = header > 0 && at >= 0 ? header + at : 0;
        }
        if( row->issue == EDITOR_CFG_ISSUE_UNKNOWN_KEY || row->issue == EDITOR_CFG_ISSUE_UNKNOWN_PARAM )
            snprintf(text, sizeof(text), "%s %s: %s (not in fields/%s.ini)", self->name, field,
                Editor_CfgRowIssueText(row->issue), self->type);
        else
            snprintf(text, sizeof(text), "%s %s = %s: %s", self->name, field, row->value,
                Editor_CfgRowIssueText(row->issue));
        Editor_Problems_ReportRecord(ui, "record", self->type, self->name, field, file, line,
            row->issue == EDITOR_CFG_ISSUE_UNKNOWN_KEY || row->issue == EDITOR_CFG_ISSUE_UNKNOWN_PARAM
                ? EDITOR_PROBLEM_WARNING
                : EDITOR_PROBLEM_ERROR,
            text);
    }
}

/** The tab strip for this record's layers. */
static void
record_tabs(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    int n = 0;

    for( int l = 0; l < self->record.layer_count && n < RECORD_TABS_MAX - 3; l++ )
    {
        const struct Editor_CfgRecordLayer* layer = &self->record.layers[l];
        if( layer->rank == 0 )
            snprintf(self->tab_titles[n], sizeof(self->tab_titles[n]), "Cache block");
        else
            snprintf(self->tab_titles[n], sizeof(self->tab_titles[n]), "Server overlay %s",
                record_basename(layer->label));
        self->tab_roles[n] = layer->rank == 0 ? EDITOR_RECORD_TAB_CACHE : EDITOR_RECORD_TAB_OVERLAY;
        snprintf(self->tab_layers[n], sizeof(self->tab_layers[n]), "%s", layer->doc_key);
        n++;
    }
    snprintf(self->tab_titles[n], sizeof(self->tab_titles[n]), "Triggers");
    self->tab_roles[n] = EDITOR_RECORD_TAB_TRIGGERS;
    self->tab_layers[n++][0] = '\0';
    snprintf(self->tab_titles[n], sizeof(self->tab_titles[n]), "Shell");
    self->tab_roles[n] = EDITOR_RECORD_TAB_SHELL;
    self->tab_layers[n++][0] = '\0';
    snprintf(self->tab_titles[n], sizeof(self->tab_titles[n]), "Where used");
    self->tab_roles[n] = EDITOR_RECORD_TAB_USES;
    self->tab_layers[n++][0] = '\0';
    for( int i = 0; i < n; i++ )
        self->tab_ptrs[i] = self->tab_titles[i];
    self->tab_count = n;
    ToriRSChrome_TabsSetTitles(ui->chrome, self->tabs, self->tab_ptrs, n);
}

/** Re-read every layer's document and resolve the rows. */
static void
record_resolve(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    record_build(ui, self, &self->record);
    /* Clean again (a save, an undo back): what is now is the new "before". */
    if( !self->have_base || !record_dirty(ui, self) )
    {
        record_build(ui, self, &self->base);
        self->have_base = 1;
    }
    self->built_seq = ui->editor ? ui->editor->last_cmd_seq : 0;
    self->built = 1;
    self->visible_valid = 0;
    /* Row indices move when a field comes or goes: the pick is its name. */
    if( self->picked >= 0 )
    {
        self->picked = -1;
        for( int r = 0; r < self->record.row_count && self->picked < 0; r++ )
        {
            char name[EDITOR_CMD_NAME_MAX * 2];
            Editor_CfgRecordRowName(&self->record.rows[r], name, sizeof(name));
            if( strcmp(name, self->picked_name) == 0 )
                self->picked = r;
        }
    }

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
    {
        char line[TORIRS_CHROME_LABEL_MAX];
        int stated = 0;
        for( int r = 0; r < self->record.row_count; r++ )
            stated += self->record.rows[r].layer >= 0;
        snprintf(line, sizeof(line), "Record %s:%s: %d fields stated, %d more in fields/%s.ini; %d layer%s", self->type,
            self->name, stated, self->record.row_count - stated, self->type, self->record.layer_count,
            self->record.layer_count == 1 ? "" : "s");
        ToriRSChrome_SetText(ui->chrome, self->row_title, line);
    }
    self->changed_count = Editor_CfgRecordRemovedSince(&self->record, &self->base);
    self->needs_client = 0;
    for( int r = 0; r < self->record.row_count; r++ )
        if( Editor_CfgRecordRowChanged(&self->record, r, &self->base) )
        {
            self->changed_count++;
            self->needs_client |= self->record.rows[r].client_side;
        }
    {
        char line[TORIRS_CHROME_LABEL_MAX];
        if( self->changed_count == 0 )
            snprintf(line, sizeof(line), "clean");
        else
            snprintf(line, sizeof(line), "dirty, %d field%s%s", self->changed_count,
                self->changed_count == 1 ? "" : "s", self->needs_client ? "   needs a client pack on RUN" : "");
        ToriRSChrome_SetText(ui->chrome, self->label_dirty, line);
    }
    record_report(ui, self);
}

static void
record_fresh(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    if( !self->built || (ui->editor && self->built_seq != ui->editor->last_cmd_seq) )
        record_resolve(ui, self);
}

/* ---- the tab's rows ------------------------------------------------------------------ */

/** The tab showing, and (for a layer tab) its layer in the record now. */
static int
record_tab_role(
    struct EditorUi* ui,
    struct RecordPanel* self,
    int* out_layer)
{
    int tab = ToriRSChrome_PanelActiveTab(ui->chrome, self->panel);

    if( tab < 0 || tab >= self->tab_count )
        tab = 0;
    *out_layer = -1;
    if( self->tab_count == 0 )
        return EDITOR_RECORD_TAB_CACHE;
    if( self->tab_roles[tab] == EDITOR_RECORD_TAB_CACHE || self->tab_roles[tab] == EDITOR_RECORD_TAB_OVERLAY )
        *out_layer = Editor_CfgRecordFindLayer(&self->record, self->tab_layers[tab]);
    return self->tab_roles[tab];
}

static void
record_visible_build(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    int layer;
    int const role = record_tab_role(ui, self, &layer);
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_find);
    int const changed_only = ToriRSChrome_Checked(ui->chrome, self->check_changed);
    int const add_layer = Editor_CfgRecordAddLayerFor(&self->record);

    find = find ? find : "";
    self->visible_count = 0;
    if( (role == EDITOR_RECORD_TAB_CACHE || role == EDITOR_RECORD_TAB_OVERLAY) && layer >= 0 )
        for( int r = 0; r < self->record.row_count && self->visible_count < RECORD_ROWS_SHOWN; r++ )
        {
            const struct Editor_CfgRecordRow* row = &self->record.rows[r];
            int const stated = Editor_CfgRecordLayerStates(&self->record, layer, r, NULL, 0) >= 0;
            if( !stated && !(row->layer < 0 && layer == add_layer) )
                continue;
            if( !Editor_CfgRecordRowMatches(&self->record, r, find) )
                continue;
            if( changed_only && !Editor_CfgRecordRowChanged(&self->record, r, &self->base) )
                continue;
            self->visible[self->visible_count++] = r;
        }
    self->visible_seq = self->built_seq;
    snprintf(self->visible_find, sizeof(self->visible_find), "%s", find);
    self->visible_changed = changed_only;
    self->visible_tab = ToriRSChrome_PanelActiveTab(ui->chrome, self->panel);
    self->visible_valid = 1;
    ToriRSChrome_VListSetCount(ui->chrome, self->list, self->visible_count);
    {
        int first;
        int shown;
        if( ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
            for( int r = first; r < first + shown; r++ )
                ToriRSChrome_VListSetRow(ui->chrome, self->list, r, "");
    }
    /* The picked row keeps its pick when it is still listed. */
    self->picked = -1;
    if( self->picked_name[0] )
        for( int v = 0; v < self->visible_count; v++ )
        {
            char name[EDITOR_CMD_NAME_MAX * 2];
            Editor_CfgRecordRowName(&self->record.rows[self->visible[v]], name, sizeof(name));
            if( strcmp(name, self->picked_name) == 0 )
            {
                self->picked = self->visible[v];
                ToriRSChrome_VListSetSelected(ui->chrome, self->list, v);
            }
        }
}

static void
record_visible_fresh(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_find);
    if( !self->visible_valid || self->visible_seq != self->built_seq || strcmp(self->visible_find, find ? find : "") != 0
        || self->visible_changed != ToriRSChrome_Checked(ui->chrome, self->check_changed)
        || self->visible_tab != ToriRSChrome_PanelActiveTab(ui->chrome, self->panel) )
        record_visible_build(ui, self);
}

static void
record_row_cells(
    struct EditorUi* ui,
    struct RecordPanel* self,
    int index,
    char* out,
    size_t out_size)
{
    const struct Editor_CfgRecordRow* row = &self->record.rows[index];
    char name[EDITOR_CMD_NAME_MAX * 2];
    char value[EDITOR_CMD_VALUE_MAX];
    char dim[EDITOR_CMD_VALUE_MAX + 96];
    const char* badge;
    int layer;
    int const changed = Editor_CfgRecordRowChanged(&self->record, index, &self->base);

    (void)record_tab_role(ui, self, &layer);
    Editor_CfgRecordRowName(row, name, sizeof(name));
    if( layer >= 0 && Editor_CfgRecordLayerStates(&self->record, layer, index, value, sizeof(value)) >= 0 )
    {
        if( row->layer == layer )
        {
            badge = record_rank_name(self->record.layers[layer].rank);
            snprintf(dim, sizeof(dim), "%s", value);
        }
        else
        {
            badge = "shadowed";
            snprintf(dim, sizeof(dim), "%s  (%s wins: %s)", value, record_basename(self->record.layers[row->layer].label),
                row->value);
        }
    }
    else
    {
        badge = "register";
        snprintf(dim, sizeof(dim), "(not stated)%s%s", row->scope[0] ? "  " : "", row->scope);
    }
    if( row->issue )
    {
        size_t const at = strlen(dim);
        snprintf(dim + at, sizeof(dim) - at, "   %s", Editor_CfgRowIssueText(row->issue));
    }
    /* sprite, name, dim, keys, right (the source), mark. */
    snprintf(out, out_size, "%s" TORIRS_CHROME_CELL_SEP_STR "%s" TORIRS_CHROME_CELL_SEP_STR "%s" TORIRS_CHROME_CELL_SEP_STR
                            TORIRS_CHROME_CELL_SEP_STR "%s" TORIRS_CHROME_CELL_SEP_STR "%s",
        TORIRS_CHROME_CELL_NO_SPRITE, name, dim, badge, row->issue ? "!" : changed ? "*" : "");
}

/* ---- the typed editor ---------------------------------------------------------------- */

/** Show only the widgets of the tab's role and the picked row's kind. */
static void
record_layout(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    int layer;
    int const role = record_tab_role(ui, self, &layer);
    int const fields = role == EDITOR_RECORD_TAB_CACHE || role == EDITOR_RECORD_TAB_OVERLAY;
    int const kind = self->editor_kind;
    int const value_kind = self->picked >= 0 ? self->record.rows[self->picked].value_kind : EDITOR_CFG_FIELD_TEXT;
    int const vk = kind == EDITOR_CFG_FIELD_PARAM ? value_kind : kind;
    struct ToriRSChrome* chrome = ui->chrome;

    ToriRSChrome_SetHidden(chrome, self->input_find, !fields);
    ToriRSChrome_SetHidden(chrome, self->check_changed, !fields);
    ToriRSChrome_SetHidden(chrome, self->label_dirty, !fields);
    ToriRSChrome_SetHidden(chrome, self->list, !fields);
    ToriRSChrome_SetHidden(chrome, self->label_field, !fields);
    ToriRSChrome_SetHidden(chrome, self->input_field, !fields);
    ToriRSChrome_SetHidden(chrome, self->input_param, !fields || kind != EDITOR_CFG_FIELD_PARAM);
    ToriRSChrome_SetHidden(chrome, self->input_value,
        !fields || vk == EDITOR_CFG_FIELD_BOOL || vk == EDITOR_CFG_FIELD_ENUM);
    ToriRSChrome_SetHidden(chrome, self->button_minus, !fields || vk != EDITOR_CFG_FIELD_NUMBER);
    ToriRSChrome_SetHidden(chrome, self->button_plus, !fields || vk != EDITOR_CFG_FIELD_NUMBER);
    ToriRSChrome_SetHidden(chrome, self->check_value, !fields || vk != EDITOR_CFG_FIELD_BOOL);
    ToriRSChrome_SetHidden(chrome, self->dropdown_value, !fields || vk != EDITOR_CFG_FIELD_ENUM);
    ToriRSChrome_SetHidden(chrome, self->list_pick,
        !fields || (vk != EDITOR_CFG_FIELD_NAME && kind != EDITOR_CFG_FIELD_PARAM));
    ToriRSChrome_SetHidden(chrome, self->dropdown_layer, !fields);
    ToriRSChrome_SetHidden(chrome, self->button_set, !fields);
    ToriRSChrome_SetHidden(chrome, self->button_remove, !fields);
    ToriRSChrome_SetHidden(chrome, self->row_hint, !fields);

    ToriRSChrome_SetHidden(chrome, self->input_list_find,
        role != EDITOR_RECORD_TAB_TRIGGERS && role != EDITOR_RECORD_TAB_SHELL);
    ToriRSChrome_SetHidden(chrome, self->list_triggers, role != EDITOR_RECORD_TAB_TRIGGERS);
    ToriRSChrome_SetHidden(chrome, self->button_trigger_open, role != EDITOR_RECORD_TAB_TRIGGERS);
    ToriRSChrome_SetHidden(chrome, self->button_check, role != EDITOR_RECORD_TAB_TRIGGERS);
    ToriRSChrome_SetHidden(chrome, self->button_triggers, role != EDITOR_RECORD_TAB_TRIGGERS);

    ToriRSChrome_SetHidden(chrome, self->label_shell, role != EDITOR_RECORD_TAB_SHELL);
    ToriRSChrome_SetHidden(chrome, self->list_shell, role != EDITOR_RECORD_TAB_SHELL);
    ToriRSChrome_SetHidden(chrome, self->button_shell, role != EDITOR_RECORD_TAB_SHELL);

    ToriRSChrome_SetHidden(chrome, self->input_uses_find, role != EDITOR_RECORD_TAB_USES);
    ToriRSChrome_SetHidden(chrome, self->label_uses, role != EDITOR_RECORD_TAB_USES);
    ToriRSChrome_SetHidden(chrome, self->list_uses, role != EDITOR_RECORD_TAB_USES);
    ToriRSChrome_SetHidden(chrome, self->button_uses_open, role != EDITOR_RECORD_TAB_USES);
}

/** The NAME picker's rows: the catalog's records of `kind` matching `text`. */
static void
record_pick_query(
    struct EditorUi* ui,
    struct RecordPanel* self,
    uint32_t kind,
    const char* text)
{
    struct EditorHost* host = record_host(ui);
    struct EditorHost_CatalogPage page;

    snprintf(self->pick_query, sizeof(self->pick_query), "%s", text);
    self->pick_kind = kind;
    self->pick_count = 0;
    if( host && kind < TORIRSMAPED_CATALOG_KIND_COUNT )
    {
        memset(&page, 0, sizeof(page));
        if( Editor_HostMapEdCatalogQuery(host, kind, text, 0, 0, RECORD_PICKS_MAX, &page) == EDITOR_HOST_OK )
            for( int i = 0; i < page.row_count && self->pick_count < RECORD_PICKS_MAX; i++ )
            {
                if( strlen(page.rows[i].name) >= sizeof(self->picks[0]) )
                    continue;
                snprintf(self->picks[self->pick_count], sizeof(self->picks[0]), "%s", page.rows[i].name);
                snprintf(self->pick_dims[self->pick_count], sizeof(self->pick_dims[0]), "%s",
                    page.rows[i].display ? page.rows[i].display : "");
                self->pick_count++;
            }
        Editor_HostCatalogPageFree(&page);
    }
    ToriRSChrome_VListSetCount(ui->chrome, self->list_pick, self->pick_count);
    {
        int first;
        int shown;
        if( ToriRSChrome_VListWindow(ui->chrome, self->list_pick, &first, &shown) )
            for( int r = first; r < first + shown; r++ )
                ToriRSChrome_VListSetRow(ui->chrome, self->list_pick, r, "");
    }
}

/** The range tooltip of a NUMBER row, its step buttons' too. */
static void
record_number_tips(
    struct EditorUi* ui,
    struct RecordPanel* self,
    const struct Editor_CfgRegisterField* field)
{
    char tip[TORIRS_CHROME_LABEL_MAX];
    long long lo;
    long long hi;

    if( field && Editor_CfgRegisterRange(field, &lo, &hi) )
        snprintf(tip, sizeof(tip), "range %lld to %lld (fields/%s.ini)", lo, hi, self->type);
    else
        snprintf(tip, sizeof(tip), "a whole number; fields/%s.ini gives no range", self->type);
    ToriRSChrome_SetTooltip(ui->chrome, self->input_value, tip);
    ToriRSChrome_SetTooltip(ui->chrome, self->button_minus, tip);
    ToriRSChrome_SetTooltip(ui->chrome, self->button_plus, tip);
}

/** Row `r` (into record.rows) into the typed editor, "where it is stated",
 *  and the hint. */
static void
record_select_row(
    struct EditorUi* ui,
    struct RecordPanel* self,
    int r)
{
    const struct Editor_CfgRecordRow* row;
    const struct Editor_CfgRegisterField* field;
    char name[EDITOR_CMD_NAME_MAX * 2];
    char hint[TORIRS_CHROME_LABEL_MAX];
    char head[TORIRS_CHROME_LABEL_MAX];
    int layer;

    if( r < 0 || r >= self->record.row_count )
        return;
    row = &self->record.rows[r];
    field = row->reg >= 0 ? &self->fields[row->reg] : NULL;
    self->picked = r;
    Editor_CfgRecordRowName(row, name, sizeof(name));
    snprintf(self->picked_name, sizeof(self->picked_name), "%s", name);
    self->editor_kind = row->kind;
    ToriRSChrome_SetText(ui->chrome, self->input_field, name);
    ToriRSChrome_SetText(ui->chrome, self->input_value, row->value);
    ToriRSChrome_SetText(ui->chrome, self->input_param, row->sub);
    ToriRSChrome_SetTooltip(ui->chrome, self->input_value, "the value Set writes");
    {
        int const vk = row->kind == EDITOR_CFG_FIELD_PARAM ? row->value_kind : row->kind;
        if( vk == EDITOR_CFG_FIELD_NUMBER )
            record_number_tips(ui, self, field);
        else if( vk == EDITOR_CFG_FIELD_BOOL )
            ToriRSChrome_SetChecked(ui->chrome, self->check_value,
                strcmp(row->value, "yes") == 0 || strcmp(row->value, "true") == 0 || strcmp(row->value, "1") == 0);
        else if( vk == EDITOR_CFG_FIELD_ENUM && field )
        {
            int selected = -1;
            self->word_count = Editor_CfgRegisterWords(field, self->words, RECORD_WORDS_MAX);
            for( int i = 0; i < self->word_count; i++ )
            {
                self->word_ptrs[i] = self->words[i];
                if( strcmp(self->words[i], row->value) == 0 )
                    selected = i;
            }
            ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_value, self->word_ptrs, self->word_count,
                selected);
        }
        if( vk == EDITOR_CFG_FIELD_NAME || row->kind == EDITOR_CFG_FIELD_PARAM )
        {
            /* A param's picker fills its name (the catalog's params); a name
             * field's, its value (the kind the register names). */
            self->pick_for_param = row->kind == EDITOR_CFG_FIELD_PARAM && vk != EDITOR_CFG_FIELD_NAME;
            if( self->pick_for_param )
                record_pick_query(ui, self, TORIRSMAPED_CATALOG_PARAM, row->sub);
            else
                record_pick_query(ui, self, record_kind_of(field ? Editor_CfgRegisterRefType(field) : ""), row->value);
        }
    }
    (void)record_tab_role(ui, self, &layer);
    /* Write to: the tab's layer when it states the field, else where it is. */
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_layer,
        layer >= 0 && Editor_CfgRecordLayerStates(&self->record, layer, r, NULL, 0) >= 0 ? layer + 1 : 0);
    snprintf(head, sizeof(head), "%s   %s%s%s", name,
        Editor_CfgFieldKindName(row->kind == EDITOR_CFG_FIELD_PARAM ? row->value_kind : row->kind),
        row->kind == EDITOR_CFG_FIELD_PARAM ? " param" : "", field ? "" : "   (not in the register)");
    ToriRSChrome_SetText(ui->chrome, self->label_field, head);
    if( row->layer >= 0 )
        snprintf(hint, sizeof(hint), "%s is stated in %s%s%s%s", name, self->record.layers[row->layer].label,
            row->shadowed ? " (over a lower layer)" : "", row->issue ? "; " : "", Editor_CfgRowIssueText(row->issue));
    else
        snprintf(hint, sizeof(hint), "%s is stated nowhere; Set adds it to %s", name,
            self->record.layers[Editor_CfgRecordAddLayerFor(&self->record)].label);
    if( row->client_side )
    {
        size_t const at = strlen(hint);
        snprintf(hint + at, sizeof(hint) - at, "; the client reads it: a change needs a client pack on RUN");
    }
    ToriRSChrome_SetText(ui->chrome, self->row_hint, hint);
    record_layout(ui, self);
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
    record_fresh(ui, self);
    count = Editor_CfgRecordFieldCommands(&self->record, key, sub, occurrence, value, layer, commands, 2);
    if( count < 0 )
    {
        Editor_UiStatus(ui, "Set refused: %s%s%s cannot be written that way (a key or value the config grammar refuses)",
            key, sub[0] ? ":" : "", sub);
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

/** The value the typed editor holds now. */
static void
record_editor_value(
    struct EditorUi* ui,
    struct RecordPanel* self,
    char* out,
    size_t out_size)
{
    int const vk = self->editor_kind == EDITOR_CFG_FIELD_PARAM && self->picked >= 0
                       ? self->record.rows[self->picked].value_kind
                       : self->editor_kind;
    const char* text = ToriRSChrome_Text(ui->chrome, self->input_value);

    if( vk == EDITOR_CFG_FIELD_BOOL )
    {
        /* The spelling the record already uses, else yes / no. */
        const char* old = self->picked >= 0 ? self->record.rows[self->picked].value : "";
        int const on = ToriRSChrome_Checked(ui->chrome, self->check_value);
        int const truthy = strcmp(old, "true") == 0 || strcmp(old, "false") == 0;
        snprintf(out, out_size, "%s", truthy ? (on ? "true" : "false") : (on ? "yes" : "no"));
        return;
    }
    if( vk == EDITOR_CFG_FIELD_ENUM && self->word_count > 0 )
    {
        int const at = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_value);
        if( at >= 0 && at < self->word_count )
        {
            snprintf(out, out_size, "%s", self->words[at]);
            return;
        }
    }
    snprintf(out, out_size, "%s", text ? text : "");
}

/** Set field (`remove` 0) or Remove field (1), from the editor. */
static int
record_set_from_editor(
    struct EditorUi* ui,
    struct RecordPanel* self,
    int remove)
{
    char key[EDITOR_CMD_NAME_MAX];
    char sub[EDITOR_CMD_NAME_MAX];
    char value[EDITOR_CMD_VALUE_MAX];
    int occurrence;
    int const choice = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_layer);
    const struct Editor_CfgRegisterField* field = NULL;
    int vk = EDITOR_CFG_FIELD_TEXT;

    if( !record_parse_field(ToriRSChrome_Text(ui->chrome, self->input_field), key, sub, &occurrence) )
    {
        Editor_UiStatus(ui, "Set refused: Field is `key`, `key#N` or `param:<name>`");
        return 0;
    }
    if( self->editor_kind == EDITOR_CFG_FIELD_PARAM && strcmp(key, "param") == 0 )
    {
        /* The two-column row: the name column names the param. */
        const char* param = ToriRSChrome_Text(ui->chrome, self->input_param);
        if( !param || !param[0] || strlen(param) >= EDITOR_CMD_NAME_MAX || strchr(param, ',') )
        {
            Editor_UiStatus(ui, "Set refused: a param row needs a param name (no comma)");
            return 0;
        }
        snprintf(sub, sizeof(sub), "%s", param);
    }
    value[0] = '\0';
    if( !remove )
    {
        record_editor_value(ui, self, value, sizeof(value));
        if( !value[0] )
        {
            Editor_UiStatus(ui, "Set refused: type a value (Remove field takes a field out)");
            return 0;
        }
        if( self->picked >= 0 )
        {
            const struct Editor_CfgRecordRow* row = &self->record.rows[self->picked];
            field = row->reg >= 0 ? &self->fields[row->reg] : NULL;
            vk = row->kind == EDITOR_CFG_FIELD_PARAM ? row->value_kind : row->kind;
        }
        if( field )
        {
            int const issue = Editor_CfgValueIssue(field, vk, value);
            if( issue != EDITOR_CFG_ISSUE_NONE )
            {
                long long lo;
                long long hi;
                if( issue == EDITOR_CFG_ISSUE_OUT_OF_RANGE && Editor_CfgRegisterRange(field, &lo, &hi) )
                    Editor_UiStatus(ui, "Set refused: %s = %s is out of range (%lld to %lld, fields/%s.ini)", key, value,
                        lo, hi, self->type);
                else
                    Editor_UiStatus(ui, "Set refused: %s = %s is %s (fields/%s.ini)", sub[0] ? sub : key, value,
                        Editor_CfgRowIssueText(issue), self->type);
                return 0;
            }
        }
    }
    return record_set(ui, self, key, sub, occurrence, value, choice > 0 ? choice - 1 : -1) >= 0;
}

/** A step button: the value +/- 1, kept in the register's range. */
static void
record_step(
    struct EditorUi* ui,
    struct RecordPanel* self,
    int delta)
{
    const char* text = ToriRSChrome_Text(ui->chrome, self->input_value);
    char* end = NULL;
    long long value = strtoll(text ? text : "", &end, 10);
    long long lo;
    long long hi;
    char out[32];
    const struct Editor_CfgRegisterField* field =
        self->picked >= 0 && self->record.rows[self->picked].reg >= 0 ? &self->fields[self->record.rows[self->picked].reg]
                                                                      : NULL;

    if( !text || !text[0] || !end || *end )
    {
        Editor_UiStatus(ui, "Step refused: the value is not a whole number");
        return;
    }
    value += delta;
    if( field && Editor_CfgRegisterRange(field, &lo, &hi) && (value < lo || value > hi) )
    {
        Editor_UiStatus(ui, "Step refused: %lld is past the range (%lld to %lld)", value, lo, hi);
        return;
    }
    snprintf(out, sizeof(out), "%lld", value);
    ToriRSChrome_SetText(ui->chrome, self->input_value, out);
}

/* ---- the record's actions --------------------------------------------------------------- */

/** Find in world for the open record: the Catalog's (every placement of the
 *  loc in the open squares, selected, the camera on the first), with the
 *  record made the pick first. The pick would arm the Place tool; Find
 *  selects, so the tool is left as it was. */
static void
record_find_in_world(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    enum Editor_Tool const tool = ui->tools.tool;

    if( !self->open )
    {
        Editor_UiStatus(ui, "Find in world refused: open a record first");
        return;
    }
    if( self->kind != TORIRSMAPED_CATALOG_LOC )
    {
        Editor_UiStatus(ui, "Find in world refused: %s:%s is not a loc (spawns are in the Spawns panel)", self->type,
            self->name);
        return;
    }
    if( !Editor_Catalog_PickRecord(ui, self->kind, self->name) )
        return;
    if( ui->tools.tool != tool )
        Editor_UiSetTool(ui, tool);
    (void)Editor_Catalog_FindInWorld(ui);
}

/* ---- Triggers, Shell, Where used ------------------------------------------------------ */

/** 1 when `find` is empty or a part of `text`, ignoring ASCII case. */
static int
record_list_matches(
    const char* text,
    const char* find)
{
    size_t const length = strlen(find);

    if( length == 0 )
        return 1;
    for( const char* at = text; *at; at++ )
    {
        size_t i = 0;
        while( i < length && at[i] && tolower((unsigned char)at[i]) == tolower((unsigned char)find[i]) )
            i++;
        if( i == length )
            return 1;
    }
    return 0;
}

static void
record_trigger_row(
    const struct Editor_ScriptRef* ref,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "[%s,%s]  %s:%d%s", ref->kind, ref->subject, record_basename(ref->rel), ref->line,
        ref->unsaved ? " (unsaved)" : "");
}

/** The Triggers list: the triggers the Find leaves. */
static void
record_triggers_show(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_list_find);

    find = find ? find : "";
    self->trigger_shown_count = 0;
    for( int r = 0; r < self->trigger_count; r++ )
    {
        char row[TORIRS_CHROME_INPUT_MAX];
        record_trigger_row(&self->triggers[r], row, sizeof(row));
        if( record_list_matches(row, find) )
            self->trigger_shown[self->trigger_shown_count++] = r;
    }
    ToriRSChrome_VListSetCount(ui->chrome, self->list_triggers, self->trigger_shown_count);
    for( int v = 0; v < self->trigger_shown_count; v++ )
    {
        char row[TORIRS_CHROME_INPUT_MAX];
        record_trigger_row(&self->triggers[self->trigger_shown[v]], row, sizeof(row));
        ToriRSChrome_VListSetRow(ui->chrome, self->list_triggers, v, row);
    }
    snprintf(self->list_find, sizeof(self->list_find), "%s", find);
}

static void
record_triggers_fill(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    self->trigger_count =
        Editor_UiPanelSelf(ui, "triggers") ? Editor_Triggers_ForSubject(ui, self->name, self->triggers, RECORD_TRIGGERS_MAX)
                                           : 0;
    record_triggers_show(ui, self);
}

static void
record_shell_row(
    const struct Builder_Shell* shell,
    int slot,
    char* out,
    size_t out_size)
{
    char value[32];

    Builder_ShellSlotValueLabel(shell, slot, value, sizeof(value));
    snprintf(out, out_size, "%-12s %s", value,
        Builder_ShellSlotHidden(&shell->slots[slot]) ? "(hidden)" : shell->slots[slot].child);
}

/** The Shell list: the slots the Find leaves (a shell can have 520 slots;
 *  the Find matches a slot's value or its child's name). */
static void
record_shell_show(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_list_find);

    find = find ? find : "";
    self->shell_shown_count = 0;
    if( self->is_shell )
        for( int s = 0; s < self->shell->slot_count; s++ )
        {
            char row[TORIRS_CHROME_INPUT_MAX];
            record_shell_row(self->shell, s, row, sizeof(row));
            if( record_list_matches(row, find) )
                self->shell_shown[self->shell_shown_count++] = s;
        }
    ToriRSChrome_VListSetCount(ui->chrome, self->list_shell, self->shell_shown_count);
    for( int v = 0; v < self->shell_shown_count; v++ )
    {
        char row[TORIRS_CHROME_INPUT_MAX];
        record_shell_row(self->shell, self->shell_shown[v], row, sizeof(row));
        ToriRSChrome_VListSetRow(ui->chrome, self->list_shell, v, row);
    }
    snprintf(self->list_find, sizeof(self->list_find), "%s", find);
}

static void
record_shell_fill(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    /* Layers run in merge order: the cache half, when there is one, first. */
    int const cache = self->record.layer_count > 0 && self->record.layers[0].rank == 0 ? 0 : -1;

    self->is_shell = 0;
    if( (self->kind == TORIRSMAPED_CATALOG_LOC || self->kind == TORIRSMAPED_CATALOG_NPC) && cache >= 0 )
        self->is_shell = Builder_ShellParse(self->shell,
            self->kind == TORIRSMAPED_CATALOG_LOC ? BUILDER_SUBJECT_LOC : BUILDER_SUBJECT_NPC,
            self->record.layers[cache].block.text, self->record.layers[cache].block.length);
    if( !self->is_shell )
    {
        snprintf(line, sizeof(line), "%s:%s is not a shell (no multivar / multiloc); Shell editor... makes it one",
            self->type, self->name);
        ToriRSChrome_SetText(ui->chrome, self->label_shell, line);
        ToriRSChrome_VListSetCount(ui->chrome, self->list_shell, 0);
        return;
    }
    {
        char summary[TORIRS_CHROME_LABEL_MAX];
        Builder_ShellSummary(self->shell, summary, sizeof(summary));
        snprintf(line, sizeof(line), "%s %s, %d slot%s: %s", self->shell->var_kind == BUILDER_SHELL_VAR_VARBIT ? "varbit"
                                                                                                         : "varp",
            self->shell->var, self->shell->slot_count, self->shell->slot_count == 1 ? "" : "s", summary);
        ToriRSChrome_SetText(ui->chrome, self->label_shell, line);
    }
    record_shell_show(ui, self);
}

static void
record_uses_reset(struct RecordPanel* self)
{
    self->use_count = 0;
    self->uses_dropped = 0;
    self->uses_cursor = 0;
    self->uses_searching = 0;
    self->uses_started = 0;
    self->uses_files_done = 0;
    self->uses_files_total = 0;
    self->uses_built = 0;
}

static int
record_use_matches(
    const struct RecordUse* use,
    const char* find)
{
    const char* p = find;

    for( ;; )
    {
        const char* b;
        char term[64];
        size_t n;

        while( *p == ' ' )
            p++;
        if( !*p )
            return 1;
        b = p;
        while( *p && *p != ' ' )
            p++;
        n = (size_t)(p - b) < sizeof(term) - 1 ? (size_t)(p - b) : sizeof(term) - 1;
        memcpy(term, b, n);
        term[n] = '\0';
        if( !strstr(use->path, term) && !strstr(use->text, term)
            && !strstr(ToriRSMapEdCatalog_UseName(use->view.use), term) )
            return 0;
    }
}

static void
record_uses_build(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_uses_find);
    char line[TORIRS_CHROME_LABEL_MAX];

    find = find ? find : "";
    self->uses_visible_count = 0;
    for( int i = 0; i < self->use_count; i++ )
        if( record_use_matches(&self->uses[i], find) )
            self->uses_visible[self->uses_visible_count++] = i;
    snprintf(self->uses_find, sizeof(self->uses_find), "%s", find);
    self->uses_built = 1;
    ToriRSChrome_VListSetCount(ui->chrome, self->list_uses, self->uses_visible_count);
    {
        int first;
        int shown;
        if( ToriRSChrome_VListWindow(ui->chrome, self->list_uses, &first, &shown) )
            for( int r = first; r < first + shown; r++ )
                ToriRSChrome_VListSetRow(ui->chrome, self->list_uses, r, "");
    }
    if( !record_host(ui) || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG_USES) )
        snprintf(line, sizeof(line), "Where used needs the editor server's catalog (CATALOG_USES)");
    else if( self->uses_searching )
        snprintf(line, sizeof(line), "searching: %u of %u files, %d use%s so far", self->uses_files_done,
            self->uses_files_total, self->use_count, self->use_count == 1 ? "" : "s");
    else if( self->uses_visible_count == self->use_count )
        snprintf(line, sizeof(line), "%d use%s of %s:%s (%u files read)%s", self->use_count,
            self->use_count == 1 ? "" : "s", self->type, self->name, self->uses_files_total,
            self->uses_dropped ? "; more than the list holds" : "");
    else
        snprintf(line, sizeof(line), "%d of %d uses", self->uses_visible_count, self->use_count);
    ToriRSChrome_SetText(ui->chrome, self->label_uses, line);
}

/** One slice of CATALOG_USES: the server reads a few milliseconds of files. */
static void
record_uses_step(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    struct EditorHost* host = record_host(ui);
    struct EditorHost_CatalogUses answer;
    enum EditorHost_Status status;

    if( !host || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG_USES) )
    {
        self->uses_searching = 0;
        self->uses_started = 1;
        return;
    }
    memset(&answer, 0, sizeof(answer));
    status = Editor_HostMapEdCatalogUses(host, self->kind, self->name, self->uses_cursor, &answer);
    if( status != EDITOR_HOST_OK && status != EDITOR_HOST_ABSENT )
    {
        Editor_UiStatus(ui, "Where used stopped: the catalog answered %d", (int)status);
        self->uses_searching = 0;
        Editor_HostCatalogUsesFree(&answer);
        return;
    }
    for( int i = 0; i < answer.row_count; i++ )
    {
        struct RecordUse* use;
        if( self->use_count >= RECORD_USES_MAX )
        {
            self->uses_dropped++;
            continue;
        }
        use = &self->uses[self->use_count++];
        snprintf(use->path, sizeof(use->path), "%s", answer.rows[i].path);
        snprintf(use->text, sizeof(use->text), "%s", answer.rows[i].text);
        use->view = answer.rows[i];
        use->view.path = use->path;
        use->view.text = use->text;
    }
    self->uses_cursor = answer.next_cursor;
    self->uses_files_done = answer.files_done;
    self->uses_files_total = answer.files_total;
    self->uses_searching = answer.next_cursor != 0;
    self->uses_built = 0;
    Editor_HostCatalogUsesFree(&answer);
}

static void
record_use_cells(
    const struct RecordUse* use,
    char* out,
    size_t out_size)
{
    char where[300];
    char right[16];

    if( use->view.line > 0 )
        snprintf(where, sizeof(where), "%s:%u", record_basename(use->path), use->view.line);
    else
        snprintf(where, sizeof(where), "%s", record_basename(use->path));
    snprintf(right, sizeof(right), "%u", use->view.count);
    snprintf(out, out_size, "%s" TORIRS_CHROME_CELL_SEP_STR "%s: %s" TORIRS_CHROME_CELL_SEP_STR "%s"
                            TORIRS_CHROME_CELL_SEP_STR TORIRS_CHROME_CELL_SEP_STR "%s",
        TORIRS_CHROME_CELL_NO_SPRITE, ToriRSMapEdCatalog_UseName(use->view.use),
        use->view.use == TORIRSMAPED_USE_MORE ? "more lines" : use->text, where, right);
}

/** A use row's file in the user's editor (a script under server/scripts). */
static void
record_use_open(
    struct EditorUi* ui,
    struct RecordPanel* self,
    int index)
{
    const struct RecordUse* use = &self->uses[index];
    static const char k_scripts[] = "server/scripts/";

    if( strncmp(use->path, k_scripts, sizeof(k_scripts) - 1) == 0 && Editor_UiPanelSelf(ui, "triggers")
        && Editor_ScriptPathValid(use->path + sizeof(k_scripts) - 1) )
    {
        (void)Editor_Triggers_OpenInEditor(ui, use->path + sizeof(k_scripts) - 1, (int)use->view.line);
        return;
    }
    Editor_UiStatus(ui, "Open refused: %s is not a script this window opens (%s:%u)", use->path, use->path,
        use->view.line);
}

/* ---- the frame ------------------------------------------------------------------------ */

static void
record_hints(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    edp_button_chord(ui, self->button_check, EDITOR_HK_FILE_CHECK_SCRIPTS);
    edp_button_chord(ui, self->button_triggers, EDITOR_HK_VIEW_TRIGGERS);
    edp_button_chord(ui, self->button_shell, EDITOR_HK_VIEW_SHELL);
    self->hint_serial = ui->hotkeys.serial;
}

static void
record_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct RecordPanel* self = opaque;
    int first;
    int shown;
    int layer;
    int role;

    if( self->hint_serial != ui->hotkeys.serial )
        record_hints(ui, self);
    if( !ui->chrome->panels[self->panel].visible )
        return;
    if( !self->open )
    {
        /* Nothing to show until a record is opened (a layout or a saved
         * layout may have shown the empty window). */
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
        return;
    }
    record_fresh(ui, self);
    role = record_tab_role(ui, self, &layer);
    if( ToriRSChrome_PanelActiveTab(ui->chrome, self->panel) != self->shown_tab )
    {
        self->shown_tab = ToriRSChrome_PanelActiveTab(ui->chrome, self->panel);
        if( role == EDITOR_RECORD_TAB_TRIGGERS )
            record_triggers_fill(ui, self);
        else if( role == EDITOR_RECORD_TAB_SHELL )
            record_shell_fill(ui, self);
        else if( role == EDITOR_RECORD_TAB_USES && !self->uses_started )
        {
            self->uses_started = 1;
            self->uses_searching = 1;
        }
        record_layout(ui, self);
    }
    if( role == EDITOR_RECORD_TAB_TRIGGERS || role == EDITOR_RECORD_TAB_SHELL )
    {
        const char* find = ToriRSChrome_Text(ui->chrome, self->input_list_find);
        if( strcmp(self->list_find, find ? find : "") != 0 )
        {
            if( role == EDITOR_RECORD_TAB_TRIGGERS )
                record_triggers_show(ui, self);
            else
                record_shell_show(ui, self);
        }
    }
    if( role == EDITOR_RECORD_TAB_USES )
    {
        const char* find = ToriRSChrome_Text(ui->chrome, self->input_uses_find);
        /* One slice a frame: no frame waits on the whole tree. */
        if( self->uses_searching )
            record_uses_step(ui, self);
        if( !self->uses_built || strcmp(self->uses_find, find ? find : "") != 0 )
            record_uses_build(ui, self);
        if( ToriRSChrome_VListWindow(ui->chrome, self->list_uses, &first, &shown) )
            for( int r = first; r < first + shown && r < self->uses_visible_count; r++ )
            {
                char text[TORIRS_CHROME_INPUT_MAX];
                record_use_cells(&self->uses[self->uses_visible[r]], text, sizeof(text));
                ToriRSChrome_VListSetRow(ui->chrome, self->list_uses, r, text);
            }
        return;
    }
    if( role != EDITOR_RECORD_TAB_CACHE && role != EDITOR_RECORD_TAB_OVERLAY )
        return;
    record_visible_fresh(ui, self);
    /* The picker follows what is typed into the box it fills. */
    if( self->picked >= 0
        && (self->editor_kind == EDITOR_CFG_FIELD_NAME || self->editor_kind == EDITOR_CFG_FIELD_PARAM
            || self->record.rows[self->picked].value_kind == EDITOR_CFG_FIELD_NAME) )
    {
        const char* typed = ToriRSChrome_Text(ui->chrome, self->pick_for_param ? self->input_param : self->input_value);
        if( typed && strcmp(typed, self->pick_query) != 0 )
            record_pick_query(ui, self, self->pick_kind, typed);
    }
    if( ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        for( int r = first; r < first + shown && r < self->visible_count; r++ )
        {
            char text[TORIRS_CHROME_INPUT_MAX];
            record_row_cells(ui, self, self->visible[r], text, sizeof(text));
            ToriRSChrome_VListSetRow(ui->chrome, self->list, r, text);
        }
    if( ToriRSChrome_VListWindow(ui->chrome, self->list_pick, &first, &shown) )
        for( int r = first; r < first + shown && r < self->pick_count; r++ )
        {
            char text[TORIRS_CHROME_INPUT_MAX];
            snprintf(text, sizeof(text), "-" TORIRS_CHROME_CELL_SEP_STR "%s" TORIRS_CHROME_CELL_SEP_STR "%s",
                self->picks[r], self->pick_dims[r]);
            ToriRSChrome_VListSetRow(ui->chrome, self->list_pick, r, text);
        }
}

static int
record_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct RecordPanel* self = opaque;

    if( !self->open )
        return widget == self->list || widget == self->button_set || widget == self->button_remove;
    if( widget == self->list )
    {
        int const v = ToriRSChrome_VListSelected(ui->chrome, widget);
        record_fresh(ui, self);
        record_visible_fresh(ui, self);
        if( v >= 0 && v < self->visible_count )
            record_select_row(ui, self, self->visible[v]);
        return 1;
    }
    if( widget == self->list_pick )
    {
        int const v = ToriRSChrome_VListSelected(ui->chrome, widget);
        if( v >= 0 && v < self->pick_count )
        {
            ToriRSChrome_SetText(ui->chrome, self->pick_for_param ? self->input_param : self->input_value,
                self->picks[v]);
            snprintf(self->pick_query, sizeof(self->pick_query), "%s", self->picks[v]);
        }
        return 1;
    }
    if( widget == self->button_set || widget == self->button_remove )
    {
        (void)record_set_from_editor(ui, self, widget == self->button_remove);
        return 1;
    }
    if( widget == self->button_minus || widget == self->button_plus )
    {
        record_step(ui, self, widget == self->button_plus ? 1 : -1);
        return 1;
    }
    if( widget == self->button_triggers )
    {
        if( Editor_UiPanelSelf(ui, "triggers") )
            Editor_Triggers_Follow(ui);
        else
            Editor_UiStatus(ui, "Triggers refused: this window has no Triggers panel");
        return 1;
    }
    if( widget == self->button_check )
    {
        (void)Editor_Triggers_Check(ui);
        return 1;
    }
    if( widget == self->button_find_world )
    {
        record_find_in_world(ui, self);
        return 1;
    }
    if( widget == self->button_copy )
    {
        const char* new_name = ToriRSChrome_Text(ui->chrome, self->input_copy_name);
        if( !self->open )
            Editor_UiStatus(ui, "Copy refused: open a record first");
        else if( self->kind > TORIRSMAPED_CATALOG_OBJ )
            Editor_UiStatus(ui, "Copy refused: only a loc, npc or obj copies (this is a %s)", self->type);
        else
            (void)Editor_Catalog_CopyRecord(ui, self->kind, self->name, new_name ? new_name : "");
        return 1;
    }
    if( widget == self->list_triggers || widget == self->button_trigger_open )
    {
        int const v = ToriRSChrome_VListSelected(ui->chrome, self->list_triggers);
        int const r = v >= 0 && v < self->trigger_shown_count ? self->trigger_shown[v] : -1;
        if( widget == self->button_trigger_open && r < 0 )
            Editor_UiStatus(ui, "Open refused: pick a trigger first");
        else if( widget == self->button_trigger_open )
            (void)Editor_Triggers_OpenInEditor(ui, self->triggers[r].rel, self->triggers[r].line);
        return 1;
    }
    if( widget == self->button_shell )
    {
        if( !Editor_UiFindPanel(ui, BUILDER_PANEL_SHELL) )
            Editor_UiStatus(ui, "Shell editor refused: this window has no shell editor");
        else if( !Builder_PanelOpenSubject(ui, BUILDER_PANEL_SHELL, self->name) )
            Editor_UiStatus(ui, "Shell editor refused: %s:%s did not open there (see the console)", self->type,
                self->name);
        else
            ToriRSChrome_PanelSetVisible(ui->chrome, Editor_UiFindPanel(ui, BUILDER_PANEL_SHELL)->chrome_panel, 1);
        return 1;
    }
    if( widget == self->list_uses || widget == self->button_uses_open )
    {
        int const v = ToriRSChrome_VListSelected(ui->chrome, self->list_uses);
        if( v < 0 || v >= self->uses_visible_count )
        {
            if( widget == self->button_uses_open )
                Editor_UiStatus(ui, "Open refused: pick a use first");
        }
        else if( widget == self->button_uses_open )
            record_use_open(ui, self, self->uses_visible[v]);
        return 1;
    }
    return widget == self->input_field || widget == self->input_value || widget == self->dropdown_layer
           || widget == self->input_param || widget == self->input_find || widget == self->check_changed
           || widget == self->check_value || widget == self->dropdown_value || widget == self->tabs
           || widget == self->input_uses_find || widget == self->list_shell;
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
    Editor_CfgRecordFree(&self->base);
    ToriMem_Free(ToriMem_Editor(), self->visible);
    ToriMem_Free(ToriMem_Editor(), self->shell);
    ToriMem_Free(ToriMem_Editor(), self->uses);
    ToriMem_Free(ToriMem_Editor(), self->uses_visible);
    ToriMem_Free(ToriMem_Editor(), self);
}

void
Editor_Record_Register(struct EditorUi* ui)
{
    static const char* const k_first_tabs[] = { "Cache block", "Triggers", "Shell", "Where used" };
    struct RecordPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->visible = ToriMem_Calloc(ToriMem_Editor(), RECORD_ROWS_SHOWN, sizeof(*self->visible));
    assert(self->visible);
    self->shell = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self->shell));
    assert(self->shell);
    self->uses = ToriMem_Calloc(ToriMem_Editor(), RECORD_USES_MAX, sizeof(*self->uses));
    assert(self->uses);
    self->uses_visible = ToriMem_Calloc(ToriMem_Editor(), RECORD_USES_MAX, sizeof(*self->uses_visible));
    assert(self->uses_visible);
    self->picked = -1;
    self->shown_tab = -1;
    self->hint_serial = UINT32_MAX;
    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 16), edp_px(ui, 140),
        edp_px(ui, 400), "Record");
    assert(self->panel >= 0);
    ToriRSChrome_PanelBeginTab(chrome, self->panel, -1);
    self->row_title = ToriRSChrome_Label(chrome, self->panel, "open a record: Catalog > Edit record, or Spawns");
    self->button_find_world = ToriRSChrome_Button(chrome, self->panel, "Find in world");
    ToriRSChrome_SetTooltip(chrome, self->button_find_world, "Select this loc's placements in the open squares");
    self->input_copy_name = ToriRSChrome_TextInput(chrome, self->panel, "New name", "");
    self->button_copy = ToriRSChrome_Button(chrome, self->panel, "Copy to new...");
    ToriRSChrome_SetTooltip(chrome, self->button_copy, "This record under New name with a fresh id: one undo step");
    self->tabs = ToriRSChrome_Tabs(chrome, self->panel, k_first_tabs, 4, 0);
    assert(self->tabs >= 0);
    /* The field tabs. */
    self->input_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find, "Search inside the record: a field, a value, a file, a kind");
    self->check_changed = ToriRSChrome_Checkbox(chrome, self->panel, "changed only", 0);
    ToriRSChrome_SetTooltip(chrome, self->check_changed, "Only the fields changed since the record opened or was saved");
    self->label_dirty = ToriRSChrome_Label(chrome, self->panel, "");
    self->list = ToriRSChrome_VListCells(chrome, self->panel,
        "-" TORIRS_CHROME_CELL_SEP_STR "field" TORIRS_CHROME_CELL_SEP_STR "value" TORIRS_CHROME_CELL_SEP_STR
        TORIRS_CHROME_CELL_SEP_STR "source",
        RECORD_LIST_ROWS, 0);
    assert(self->list >= 0);
    ToriRSChrome_SetTooltip(chrome, self->list, "Pick a field to edit it; ! an issue (in Problems), * changed");
    self->label_field = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_field = ToriRSChrome_TextInput(chrome, self->panel, "Field", "");
    ToriRSChrome_SetTooltip(chrome, self->input_field, "key, key#N, or param:<name>; a new key adds a field");
    self->input_param = ToriRSChrome_TextInput(chrome, self->panel, "Param", "");
    ToriRSChrome_SetTooltip(chrome, self->input_param, "The param's name: typing filters the catalog's params");
    self->input_value = ToriRSChrome_TextInput(chrome, self->panel, "Value", "");
    self->button_minus = ToriRSChrome_Button(chrome, self->panel, "-1");
    self->button_plus = ToriRSChrome_Button(chrome, self->panel, "+1");
    self->check_value = ToriRSChrome_Checkbox(chrome, self->panel, "yes", 0);
    ToriRSChrome_SetTooltip(chrome, self->check_value, "On writes yes (or true where the record spells it so)");
    self->dropdown_value = ToriRSChrome_Dropdown(chrome, self->panel, "Value", NULL, 0, -1);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_value, "The words fields/<type>.ini lists for this field");
    self->list_pick = ToriRSChrome_VListCells(chrome, self->panel, "", RECORD_PICK_ROWS, 0);
    assert(self->list_pick >= 0);
    ToriRSChrome_SetTooltip(chrome, self->list_pick, "The catalog's records of the kind the field names: pick one");
    self->dropdown_layer = ToriRSChrome_Dropdown(chrome, self->panel, "Write to", NULL, 0, -1);
    self->button_set = ToriRSChrome_Button(chrome, self->panel, "Set field");
    ToriRSChrome_SetTooltip(chrome, self->button_set, "Write the value into Write to's file: one undo step");
    self->button_remove = ToriRSChrome_Button(chrome, self->panel, "Remove field");
    ToriRSChrome_SetTooltip(chrome, self->button_remove, "Take the statement out of that file: a lower one shows through");
    self->row_hint = ToriRSChrome_Label(chrome, self->panel, "");
    /* Triggers and Shell. */
    self->input_list_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(chrome, self->input_list_find, "Filter the rows: a trigger, a file, a slot value or child");
    /* Triggers. */
    self->list_triggers = ToriRSChrome_VList(chrome, self->panel, "", 8, 0);
    self->button_trigger_open = ToriRSChrome_Button(chrome, self->panel, "Open in editor");
    ToriRSChrome_SetTooltip(chrome, self->button_trigger_open, "The picked trigger's file at its line, in your editor");
    self->button_check = ToriRSChrome_Button(chrome, self->panel, "Check");
    ToriRSChrome_SetTooltip(chrome, self->button_check, "sscompile on the editor server (the window never waits)");
    self->button_triggers = ToriRSChrome_Button(chrome, self->panel, "Triggers...");
    ToriRSChrome_SetTooltip(chrome, self->button_triggers, "this record's script triggers; add one");
    /* Shell. */
    self->label_shell = ToriRSChrome_Label(chrome, self->panel, "");
    self->list_shell = ToriRSChrome_VList(chrome, self->panel, "", 8, 0);
    self->button_shell = ToriRSChrome_Button(chrome, self->panel, "Shell editor...");
    ToriRSChrome_SetTooltip(chrome, self->button_shell, "The shell editor on this record: slots and the preview");
    /* Where used. */
    self->input_uses_find = ToriRSChrome_TextInput(chrome, self->panel, "Find use", "");
    ToriRSChrome_SetTooltip(chrome, self->input_uses_find, "Filter the uses: a file, a word of the line, a kind of use");
    self->label_uses = ToriRSChrome_Label(chrome, self->panel, "");
    self->list_uses = ToriRSChrome_VListCells(chrome, self->panel,
        "-" TORIRS_CHROME_CELL_SEP_STR "use" TORIRS_CHROME_CELL_SEP_STR "where" TORIRS_CHROME_CELL_SEP_STR
        TORIRS_CHROME_CELL_SEP_STR "count",
        RECORD_LIST_ROWS, 0);
    assert(self->list_uses >= 0);
    self->button_uses_open = ToriRSChrome_Button(chrome, self->panel, "Open");
    ToriRSChrome_SetTooltip(chrome, self->button_uses_open, "The picked use's script at its line, in your editor");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);
    record_layout(ui, self);

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

/** The params an overlay names that the register lacks: the catalog's, if
 *  it knows them (one exact lookup each, at open). */
static void
record_learn_params(
    struct EditorUi* ui,
    struct RecordPanel* self)
{
    struct EditorHost* host = record_host(ui);

    self->known_count = 0;
    if( !host )
        return;
    for( int r = 0; r < self->record.row_count && self->known_count < RECORD_KNOWN_MAX; r++ )
    {
        const struct Editor_CfgRecordRow* row = &self->record.rows[r];
        struct EditorHost_CatalogPage page;
        if( row->issue != EDITOR_CFG_ISSUE_UNKNOWN_PARAM )
            continue;
        memset(&page, 0, sizeof(page));
        if( Editor_HostMapEdCatalogQuery(host, TORIRSMAPED_CATALOG_PARAM, row->sub, TORIRSMAPED_CATALOG_EXACT, 0, 1, &page)
                == EDITOR_HOST_OK
            && page.row_count > 0 )
        {
            snprintf(self->known[self->known_count], sizeof(self->known[0]), "%s", row->sub);
            self->known_ptrs[self->known_count] = self->known[self->known_count];
            self->known_count++;
        }
        Editor_HostCatalogPageFree(&page);
    }
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
    self = record_self(ui);
    assert(self);
    if( kind > TORIRSMAPED_CATALOG_OBJ )
    {
        Editor_UiStatus(ui, "Open record refused: the Record panel edits loc, npc and obj records");
        return 0;
    }
    if( !ui->editor )
    {
        Editor_UiStatus(ui, "Open record refused: no editor session");
        return 0;
    }
    type = ToriRSMapEdCatalog_KindName(kind);
    snprintf(self->type, sizeof(self->type), "%s", type);
    self->kind = kind;
    self->overlay_count = 0;
    self->open = 0;
    self->built = 0;
    self->have_base = 0;
    self->known_count = 0;
    self->picked = -1;
    self->picked_name[0] = '\0';
    self->editor_kind = EDITOR_CFG_FIELD_TEXT;
    self->shown_tab = -1;
    record_uses_reset(self);

    /* A `#<id>` spawn name: the record's own name, from the catalog. */
    host = record_host(ui);
    if( name[0] == '#' )
    {
        struct EditorHost_CatalogRecord record;
        memset(&record, 0, sizeof(record));
        if( !host || Editor_HostMapEdCatalogRecord(host, kind, (int32_t)atoi(name + 1), "", &record) != EDITOR_HOST_OK
            || !record.name || strlen(record.name) >= sizeof(self->name) )
        {
            Editor_UiStatus(ui, "Open record refused: %s %s has no record name the catalog knows", type, name);
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
                self->overlay_lines[self->overlay_count] = (int)record.sources[i].line;
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
    record_build(ui, self, &self->record);
    record_learn_params(ui, self);
    record_resolve(ui, self);
    record_tabs(ui, self);
    ToriRSChrome_PanelSetActiveTab(ui->chrome, self->panel, 0);
    ToriRSChrome_SetText(ui->chrome, self->input_find, "");
    ToriRSChrome_SetChecked(ui->chrome, self->check_changed, 0);
    ToriRSChrome_SetText(ui->chrome, self->input_uses_find, "");
    record_layout(ui, self);
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
    self = record_self(ui);
    if( !self || !self->open || strcmp(self->type, type) != 0 || strcmp(self->name, name) != 0 )
        return 0;
    record_fresh(ui, self);
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
    self = record_self(ui);
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
    self = record_self(ui);
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
    self = record_self(ui);
    assert(self);
    if( !self->open )
        return -1;
    record_fresh(ui, self);
    row = Editor_CfgRecordFindRow(&self->record, key, sub, 0);
    if( row < 0 )
        return -1;
    /* The tab of the layer that wins it (the top one for a field nobody
     * states), with no search or chip hiding it. */
    {
        int const layer = self->record.rows[row].layer >= 0 ? self->record.rows[row].layer
                                                             : Editor_CfgRecordAddLayerFor(&self->record);
        for( int t = 0; t < self->tab_count; t++ )
            if( strcmp(self->tab_layers[t], self->record.layers[layer].doc_key) == 0 )
                ToriRSChrome_PanelSetActiveTab(ui->chrome, self->panel, t);
    }
    ToriRSChrome_SetText(ui->chrome, self->input_find, "");
    ToriRSChrome_SetChecked(ui->chrome, self->check_changed, 0);
    record_visible_build(ui, self);
    for( int v = 0; v < self->visible_count; v++ )
        if( self->visible[v] == row )
        {
            ToriRSChrome_VListSetSelected(ui->chrome, self->list, v);
            ToriRSChrome_VListSetScroll(ui->chrome, self->list, v > 2 ? v - 2 : 0);
        }
    record_select_row(ui, self, row);
    return row;
}

const struct Editor_CfgRecord*
Editor_Record_Current(struct EditorUi* ui)
{
    struct RecordPanel* self;

    assert(ui);
    self = record_self(ui);
    if( !self || !self->open )
        return NULL;
    record_fresh(ui, self);
    return &self->record;
}

/* ---- workbench 6.6: what the tests and the other panels read ---------------------------- */

int
Editor_Record_TabCount(struct EditorUi* ui)
{
    struct RecordPanel* self;

    assert(ui);
    self = record_self(ui);
    return self && self->open ? self->tab_count : 0;
}

const char*
Editor_Record_TabTitle(
    struct EditorUi* ui,
    int tab,
    int* out_role)
{
    struct RecordPanel* self;

    assert(ui);
    assert(out_role);
    self = record_self(ui);
    assert(self);
    assert(tab >= 0);
    assert(tab < self->tab_count);
    *out_role = self->tab_roles[tab];
    return self->tab_titles[tab];
}

int
Editor_Record_ShowTab(
    struct EditorUi* ui,
    int tab)
{
    struct RecordPanel* self;

    assert(ui);
    self = record_self(ui);
    assert(self);
    assert(tab >= 0);
    assert(tab < self->tab_count);
    ToriRSChrome_PanelSetActiveTab(ui->chrome, self->panel, tab);
    return 1;
}

int
Editor_Record_VisibleRows(
    struct EditorUi* ui,
    int* out,
    int max)
{
    struct RecordPanel* self;
    int n = 0;

    assert(ui);
    assert(out || max == 0);
    self = record_self(ui);
    assert(self);
    if( !self->open )
        return 0;
    record_fresh(ui, self);
    record_visible_fresh(ui, self);
    for( int v = 0; v < self->visible_count && n < max; v++ )
        out[n++] = self->visible[v];
    return n;
}

void
Editor_Record_SetFind(
    struct EditorUi* ui,
    const char* text,
    int changed_only)
{
    struct RecordPanel* self;

    assert(ui);
    assert(text);
    self = record_self(ui);
    assert(self);
    ToriRSChrome_SetText(ui->chrome, self->input_find, text);
    ToriRSChrome_SetChecked(ui->chrome, self->check_changed, changed_only);
}

int
Editor_Record_ChangedCount(struct EditorUi* ui)
{
    struct RecordPanel* self;

    assert(ui);
    self = record_self(ui);
    assert(self);
    if( !self->open )
        return 0;
    record_fresh(ui, self);
    return self->changed_count;
}

int
Editor_Record_NeedsClientPack(struct EditorUi* ui)
{
    struct RecordPanel* self;

    assert(ui);
    self = record_self(ui);
    assert(self);
    if( !self->open )
        return 0;
    record_fresh(ui, self);
    return self->needs_client;
}

int
Editor_Record_EditorKind(struct EditorUi* ui)
{
    struct RecordPanel* self;

    assert(ui);
    self = record_self(ui);
    assert(self);
    return self->open && self->picked >= 0 ? self->editor_kind : -1;
}

int
Editor_Record_PickCount(struct EditorUi* ui)
{
    struct RecordPanel* self;

    assert(ui);
    self = record_self(ui);
    assert(self);
    return self->pick_count;
}

const char*
Editor_Record_PickRow(
    struct EditorUi* ui,
    int index)
{
    struct RecordPanel* self;

    assert(ui);
    self = record_self(ui);
    assert(self);
    assert(index >= 0);
    assert(index < self->pick_count);
    return self->picks[index];
}

int
Editor_Record_UsesCount(
    struct EditorUi* ui,
    int* out_searching)
{
    struct RecordPanel* self;

    assert(ui);
    assert(out_searching);
    self = record_self(ui);
    assert(self);
    *out_searching = self->uses_searching;
    return self->use_count;
}

const struct EditorHost_CatalogUse*
Editor_Record_UsesRow(
    struct EditorUi* ui,
    int index)
{
    struct RecordPanel* self;

    assert(ui);
    self = record_self(ui);
    assert(self);
    assert(index >= 0);
    assert(index < self->use_count);
    return &self->uses[index].view;
}
