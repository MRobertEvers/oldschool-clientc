/*
 * The Record panel (module "record"): one loc / npc / obj config record
 * across every file that states it, every field editable. Opened from the
 * Catalog's "Edit record...", the Preview's Record chip and the Spawns panel
 * (Editor_Record_Open). Moved out of edpanel_catalog.c whole; the Catalog
 * registers it beside itself (Editor_Catalog_Register).
 */

#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/editor_cfg.h"
#include "editor/map_editor_ghost.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

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
    host = record_host(ui);
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
