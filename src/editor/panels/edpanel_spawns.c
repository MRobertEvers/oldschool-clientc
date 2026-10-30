/*
 * NPC and obj spawns of one square (design #placement), now documents: the
 * square's EDITED `.spawn` (server/scripts/areas/edited/configs/m<x>_<z>.spawn,
 * a SPAWN document, every edit an EDITOR_CMD_SPAWN* command with undo and a
 * relay to every window) over its GENERATED one (areas/world/configs, read as
 * a TEXT document and never written).
 *
 *   Place       a row in the edited file: the name typed (a pack symbol), or
 *               the catalog's npc / obj pick resolved to its symbol through
 *               the server's catalog, at the latched tile.
 *   Delete      an edited row is removed; a generated row gets a suppression
 *               row (`-name x z level`) instead, because writing into the
 *               generated file would be undone by its next regeneration.
 *   Restore     drops a suppression; the generated spawn is back.
 *   Duplicate   the selected rows copied one tile east — or, when a tile is
 *               latched that is not the first selected spawn's own, to that
 *               tile (the rest keep their offsets from the first) — as one
 *               undo step; the copies become the selection. A copy of a
 *               generated row is an edited row.
 *
 * The list selects one row per click and shift-click adds or takes a row out
 * of the selection (marked *), the way the loc multi-selection works.
 *
 * The list shows both files: edited rows marked +, suppressed generated rows
 * marked -, the rest as they come.
 */

#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPAWNS_ROWS_MAX 1024
#define SPAWNS_LIST_ROWS 10
/** Rows one selection (and so one Duplicate) holds. */
#define SPAWNS_SELECTION_MAX 64

enum
{
    SPAWNS_GROUP_KIND = 31,
};

struct SpawnsListRow
{
    struct Editor_SpawnRow row;
    /** 1 the edited file, 0 the generated one. */
    int edited;
    int suppressed;
};

struct SpawnsPanel
{
    int panel;
    int row_square;
    int list;
    int button_delete;
    int button_restore;
    int button_duplicate;
    int input_name;
    int radio_npc;
    int radio_obj;
    int slider_count;
    int button_pick;
    int button_place;

    int map_x;
    int map_z;
    int square_valid;

    /** The generated file, parsed; which square, and whether it was tried. */
    struct Editor_SpawnFile generated;
    int generated_x;
    int generated_z;
    int generated_tried;
    size_t generated_length;

    struct SpawnsListRow rows[SPAWNS_ROWS_MAX];
    int row_count;
    /** The multi-selection, by row CONTENT (a rebuild reorders the list,
     *  an edit elsewhere in the file moves line numbers). */
    struct Editor_SpawnRow selection[SPAWNS_SELECTION_MAX];
    int selection_count;
    /** What the list was built from. */
    uint32_t built_hash;
};

/** The square the panel lists: the latched tile's, else the first open. */
static int
spawns_square(
    struct EditorUi* ui,
    int* out_map_x,
    int* out_map_z)
{
    if( ui->tools.tile_valid )
    {
        *out_map_x = ui->tools.tile_x / EDITOR_SQUARE_X;
        *out_map_z = ui->tools.tile_z / EDITOR_SQUARE_Z;
        return 1;
    }
    if( ui->editor )
        for( int i = 0; i < ui->editor->doc.square_count; i++ )
            if( ui->editor->doc.squares[i].loaded )
            {
                *out_map_x = ui->editor->doc.squares[i].map_x;
                *out_map_z = ui->editor->doc.squares[i].map_z;
                return 1;
            }
    return 0;
}

static struct Editor_SpawnFile*
spawns_edited(
    struct EditorUi* ui,
    int map_x,
    int map_z)
{
    struct Editor_DocHandle handle;
    char key[EDITOR_DOC_KEY_MAX];

    if( !ui->editor )
        return NULL;
    Editor_SquareKey(key, map_x, map_z);
    if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_SPAWN, key, &handle) || !handle.spawn->loaded )
        return NULL;
    return &handle.spawn->file;
}

/** The generated file for the square, read once per square. */
static void
spawns_load_generated(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    struct Editor_DocHandle handle;
    char key[EDITOR_DOC_KEY_MAX];

    if( self->generated_tried && self->generated_x == self->map_x && self->generated_z == self->map_z )
    {
        /* Re-parse only if a resync replaced the text. */
        Editor_SpawnGeneratedKey(key, self->map_x, self->map_z);
        if( ui->editor && Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, key, &handle)
            && handle.text->length != self->generated_length )
        {
            (void)Editor_SpawnParse(&self->generated, handle.text->text, handle.text->length);
            self->generated_length = handle.text->length;
        }
        return;
    }
    Editor_SpawnFree(&self->generated);
    Editor_SpawnInit(&self->generated);
    self->generated_x = self->map_x;
    self->generated_z = self->map_z;
    self->generated_tried = 1;
    self->generated_length = 0;
    Editor_SpawnGeneratedKey(key, self->map_x, self->map_z);
    if( !edp_ensure_doc(ui, EDITOR_DOC_TEXT, key) )
        return; /* no generated file for this square: nothing to list */
    if( Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, key, &handle) && handle.text->text )
    {
        struct Editor_SpawnParseResult result =
            Editor_SpawnParse(&self->generated, handle.text->text, handle.text->length);
        if( !result.ok )
            Editor_UiLog(ui, "editor: %s line %d does not parse", key, result.line);
        self->generated_length = handle.text->length;
    }
}

static uint32_t
spawns_hash(
    const struct Editor_SpawnFile* file,
    uint32_t hash)
{
    if( !file )
        return hash * 16777619u;
    for( int i = 0; i < file->line_count; i++ )
    {
        const struct Editor_SpawnLine* line = &file->lines[i];
        if( line->kind != EDITOR_SPAWN_LINE_ROW )
            continue;
        hash = (hash ^ (uint32_t)(line->row.abs_x * 4099 + line->row.abs_z * 31 + line->row.level
                                  + line->row.suppress * 7 + line->row.count))
               * 16777619u;
        for( const char* c = line->row.name; *c; c++ )
            hash = (hash ^ (uint32_t)(unsigned char)*c) * 16777619u;
    }
    return hash;
}

static int
spawns_row_same(
    const struct Editor_SpawnRow* a,
    const struct Editor_SpawnRow* b)
{
    return a->is_obj == b->is_obj && a->abs_x == b->abs_x && a->abs_z == b->abs_z && a->level == b->level
           && (!a->is_obj || a->count == b->count) && strcmp(a->name, b->name) == 0;
}

/** Index of `row` in the selection, or -1. */
static int
spawns_selection_find(
    const struct SpawnsPanel* self,
    const struct Editor_SpawnRow* row)
{
    for( int i = 0; i < self->selection_count; i++ )
        if( spawns_row_same(&self->selection[i], row) )
            return i;
    return -1;
}

/** Drop selected rows the list no longer has (deleted, undone). */
static void
spawns_selection_prune(struct SpawnsPanel* self)
{
    int kept = 0;

    for( int i = 0; i < self->selection_count; i++ )
    {
        int present = 0;
        for( int r = 0; r < self->row_count && !present; r++ )
            present = spawns_row_same(&self->rows[r].row, &self->selection[i]);
        if( present )
            self->selection[kept++] = self->selection[i];
    }
    self->selection_count = kept;
}

static void
spawns_rebuild(
    struct EditorUi* ui,
    struct SpawnsPanel* self,
    const struct Editor_SpawnFile* edited)
{
    self->row_count = 0;
    if( edited )
        for( int i = 0; i < edited->line_count && self->row_count < SPAWNS_ROWS_MAX; i++ )
        {
            const struct Editor_SpawnLine* line = &edited->lines[i];
            if( line->kind != EDITOR_SPAWN_LINE_ROW || line->row.suppress )
                continue;
            self->rows[self->row_count].row = line->row;
            self->rows[self->row_count].edited = 1;
            self->rows[self->row_count].suppressed = 0;
            self->row_count++;
        }
    for( int i = 0; i < self->generated.line_count && self->row_count < SPAWNS_ROWS_MAX; i++ )
    {
        const struct Editor_SpawnLine* line = &self->generated.lines[i];
        if( line->kind != EDITOR_SPAWN_LINE_ROW || line->row.suppress )
            continue;
        self->rows[self->row_count].row = line->row;
        self->rows[self->row_count].edited = 0;
        self->rows[self->row_count].suppressed = Editor_SpawnIsSuppressed(edited, &line->row);
        self->row_count++;
    }
    ToriRSChrome_VListSetCount(ui->chrome, self->list, self->row_count);
    spawns_selection_prune(self);
    /* Every visible row's text is stale now. */
    {
        int first;
        int shown;
        if( ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
            for( int r = first; r < first + shown; r++ )
                ToriRSChrome_VListSetRow(ui->chrome, self->list, r, "");
    }
}

static void
spawns_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct SpawnsPanel* self = opaque;
    struct Editor_SpawnFile* edited;
    char line[TORIRS_CHROME_LABEL_MAX];
    int map_x;
    int map_z;
    int first;
    int shown;
    uint32_t hash;

    if( !ui->chrome->panels[self->panel].visible )
        return;
    if( !spawns_square(ui, &map_x, &map_z) )
    {
        if( self->square_valid )
        {
            self->square_valid = 0;
            self->selection_count = 0;
            ToriRSChrome_SetText(ui->chrome, self->row_square, "open a square, latch a tile");
            self->row_count = 0;
            ToriRSChrome_VListSetCount(ui->chrome, self->list, 0);
        }
        return;
    }
    if( !self->square_valid || map_x != self->map_x || map_z != self->map_z )
    {
        char key[EDITOR_DOC_KEY_MAX];
        self->map_x = map_x;
        self->map_z = map_z;
        self->square_valid = 1;
        self->built_hash = 0;
        self->selection_count = 0;
        Editor_SquareKey(key, map_x, map_z);
        (void)edp_ensure_doc(ui, EDITOR_DOC_SPAWN, key);
    }
    spawns_load_generated(ui, self);
    edited = spawns_edited(ui, map_x, map_z);
    hash = spawns_hash(&self->generated, spawns_hash(edited, 2166136261u));
    if( hash != self->built_hash )
    {
        int edited_rows = 0;
        int suppressed = 0;
        spawns_rebuild(ui, self, edited);
        self->built_hash = hash;
        for( int i = 0; i < self->row_count; i++ )
        {
            edited_rows += self->rows[i].edited;
            suppressed += self->rows[i].suppressed;
        }
        snprintf(line, sizeof(line), "m%d_%d: %d spawns, %d edited, %d suppressed", map_x, map_z,
            self->row_count, edited_rows, suppressed);
        ToriRSChrome_SetText(ui->chrome, self->row_square, line);
    }
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->row_count; r++ )
    {
        const struct SpawnsListRow* row = &self->rows[r];
        char text[TORIRS_CHROME_INPUT_MAX];
        snprintf(text, sizeof(text), "%s%c %s %s %d %d %d%s", spawns_selection_find(self, &row->row) >= 0 ? "* " : "",
            row->edited ? '+' : row->suppressed ? '-' : ' ', row->row.is_obj ? "obj" : "npc", row->row.name,
            row->row.abs_x, row->row.abs_z, row->row.level, row->suppressed ? "  (suppressed)" : "");
        ToriRSChrome_VListSetRow(ui->chrome, self->list, r, text);
    }
}

static int
spawns_selected(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    int const row = ToriRSChrome_VListSelected(ui->chrome, self->list);
    if( row < 0 || row >= self->row_count )
    {
        Editor_UiLog(ui, "editor: pick a spawn in the list first");
        return -1;
    }
    return row;
}

static void
spawns_delete(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    struct Editor_Cmd command;
    int const row = spawns_selected(ui, self);
    struct Editor_SpawnFile* edited;

    if( row < 0 )
        return;
    edited = spawns_edited(ui, self->map_x, self->map_z);
    if( self->rows[row].suppressed )
    {
        Editor_UiLog(ui, "editor: already suppressed; Restore brings it back");
        return;
    }
    if( !Editor_SpawnDeleteCommand(self->rows[row].edited ? edited : NULL, &self->rows[row].row, &command) )
        return;
    if( !self->rows[row].edited && !edited )
    {
        Editor_UiLog(ui, "editor: the edited spawn file is not open; suppression refused");
        return;
    }
    if( edp_apply(ui, &command, 1, command.kind == EDITOR_CMD_SPAWN_SUPPRESS ? "suppress" : "delete spawn") )
        Editor_UiLog(ui, "editor: %s %s at %d,%d", command.kind == EDITOR_CMD_SPAWN_SUPPRESS ? "suppressed" : "deleted",
            self->rows[row].row.name, self->rows[row].row.abs_x, self->rows[row].row.abs_z);
}

static void
spawns_restore(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    struct Editor_Cmd command;
    struct Editor_SpawnRow target;
    int const row = spawns_selected(ui, self);

    if( row < 0 )
        return;
    if( !self->rows[row].suppressed )
    {
        Editor_UiLog(ui, "editor: only a suppressed generated spawn can be restored");
        return;
    }
    target = self->rows[row].row;
    target.suppress = 1;
    Editor_SpawnCommand(&command, &target, 0);
    if( edp_apply(ui, &command, 1, "restore spawn") )
        Editor_UiLog(ui, "editor: restored %s at %d,%d", target.name, target.abs_x, target.abs_z);
}

/** The catalog's npc / obj pick as a pack symbol, through the server's
 *  catalog (the provider knows display names, the spawn grammar symbols). */
static void
spawns_use_pick(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    struct EditorHost_CatalogRecord record;
    uint32_t kind;

    if( ui->tools.pick_kind != 1 && ui->tools.pick_kind != 2 )
    {
        Editor_UiLog(ui, "editor: pick an npc or obj in the catalog first");
        return;
    }
    if( !ui->editor || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
    {
        Editor_UiLog(ui, "editor: the server has no catalog; type the spawn's name");
        return;
    }
    kind = ui->tools.pick_kind == 1 ? TORIRSMAPED_CATALOG_NPC : TORIRSMAPED_CATALOG_OBJ;
    memset(&record, 0, sizeof(record));
    if( Editor_HostMapEdCatalogRecord(&ui->editor->host, kind, ui->tools.pick_id, "", &record)
        != EDITOR_HOST_OK )
    {
        Editor_UiLog(ui, "editor: no catalog record for %s %d", kind == TORIRSMAPED_CATALOG_NPC ? "npc" : "obj",
            ui->tools.pick_id);
        return;
    }
    ToriRSChrome_SetText(ui->chrome, self->input_name, record.name ? record.name : "");
    if( kind == TORIRSMAPED_CATALOG_OBJ )
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_obj);
    else
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_npc);
    Editor_HostCatalogRecordFree(&record);
}

static void
spawns_place(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    struct Editor_SpawnRow row;
    struct Editor_Cmd command;
    char key[EDITOR_DOC_KEY_MAX];
    const char* name = ToriRSChrome_Text(ui->chrome, self->input_name);

    if( !ui->tools.tile_valid )
    {
        Editor_UiLog(ui, "editor: latch a tile with the select tool first");
        return;
    }
    if( !name || !name[0] || strchr(name, ' ') || strlen(name) >= EDITOR_CMD_NAME_MAX )
    {
        Editor_UiLog(ui, "editor: type the spawn's content name (a pack symbol, no spaces)");
        return;
    }
    memset(&row, 0, sizeof(row));
    row.is_obj = ToriRSChrome_Checked(ui->chrome, self->radio_obj) ? 1 : 0;
    snprintf(row.name, sizeof(row.name), "%s", name);
    row.abs_x = ui->tools.tile_x;
    row.abs_z = ui->tools.tile_z;
    row.level = ui->tools.tile_level;
    row.count = row.is_obj ? ToriRSChrome_SliderValue(ui->chrome, self->slider_count) : 1;
    Editor_SpawnDocKeyAt(key, row.abs_x, row.abs_z);
    if( !edp_ensure_doc(ui, EDITOR_DOC_SPAWN, key) )
    {
        Editor_UiLog(ui, "editor: could not open the spawn document %s", key);
        return;
    }
    Editor_SpawnCommand(&command, &row, 1);
    if( edp_apply(ui, &command, 1, "place spawn") )
        Editor_UiLog(ui, "editor: placed %s %s at %d,%d level %d", row.is_obj ? "obj" : "npc", row.name,
            row.abs_x, row.abs_z, row.level);
}

/** A click on the list: select that row, or with shift add / remove it. */
static void
spawns_select_click(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    int const row = ToriRSChrome_VListSelected(ui->chrome, self->list);
    int at;

    if( row < 0 || row >= self->row_count )
        return;
    at = spawns_selection_find(self, &self->rows[row].row);
    if( !ui->shift_held )
    {
        self->selection[0] = self->rows[row].row;
        self->selection_count = 1;
    }
    else if( at >= 0 )
    {
        self->selection[at] = self->selection[self->selection_count - 1];
        self->selection_count--;
    }
    else if( self->selection_count < SPAWNS_SELECTION_MAX )
        self->selection[self->selection_count++] = self->rows[row].row;
    else
        Editor_UiLog(ui, "editor: a spawn selection holds %d rows", SPAWNS_SELECTION_MAX);
    /* The * markers changed. */
    {
        int first;
        int shown;
        if( ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
            for( int r = first; r < first + shown; r++ )
                ToriRSChrome_VListSetRow(ui->chrome, self->list, r, "");
    }
}

static int
spawns_duplicate(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    struct Editor_SpawnRow rows[SPAWNS_SELECTION_MAX];
    struct Editor_SpawnRow copies[SPAWNS_SELECTION_MAX];
    struct Editor_Cmd* commands;
    int capacity;
    int n = 0;
    int count;
    int applied;
    int dx = 1;
    int dz = 0;
    int dlevel = 0;

    for( int i = 0; i < self->selection_count; i++ )
    {
        struct Editor_SpawnRow probe = self->selection[i];
        int suppressed = 0;
        for( int r = 0; r < self->row_count; r++ )
            if( spawns_row_same(&self->rows[r].row, &probe) )
                suppressed = self->rows[r].suppressed;
        if( !suppressed )
            rows[n++] = probe;
    }
    if( n == 0 )
    {
        Editor_UiLog(ui, "editor: select the spawns to duplicate (a suppressed one is not live)");
        return 0;
    }
    /* At the latched tile, when one is latched that is not where the first
     * selected spawn already stands; else one tile east, beside it. */
    if( ui->tools.tile_valid
        && (ui->tools.tile_x != rows[0].abs_x || ui->tools.tile_z != rows[0].abs_z
            || ui->tools.tile_level != rows[0].level) )
    {
        dx = ui->tools.tile_x - rows[0].abs_x;
        dz = ui->tools.tile_z - rows[0].abs_z;
        dlevel = ui->tools.tile_level - rows[0].level;
    }
    commands = edp_scratch(&capacity);
    count = Editor_SpawnOpsDuplicate(rows, n, dx, dz, dlevel, commands, capacity, copies);
    for( int i = 0; i < count; i++ )
        if( !edp_ensure_doc(ui, EDITOR_DOC_SPAWN, commands[i].doc_key) )
        {
            Editor_UiLog(ui, "editor: could not open the spawn document %s", commands[i].doc_key);
            return 0;
        }
    applied = edp_apply(ui, commands, count, "duplicate spawn");
    if( applied == count && count > 0 )
    {
        memcpy(self->selection, copies, sizeof(copies[0]) * (size_t)count);
        self->selection_count = count;
        Editor_UiLog(ui, "editor: duplicated %d spawn(s) by %d,%d; the copies are selected", count, dx, dz);
    }
    return applied;
}

static int
spawns_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct SpawnsPanel* self = opaque;

    if( widget == self->list )
    {
        spawns_select_click(ui, self);
        return 1;
    }
    if( widget == self->radio_npc || widget == self->radio_obj || widget == self->slider_count
        || widget == self->input_name )
        return 1;
    if( widget == self->button_duplicate )
        (void)spawns_duplicate(ui, self);
    else if( widget == self->button_delete )
        spawns_delete(ui, self);
    else if( widget == self->button_restore )
        spawns_restore(ui, self);
    else if( widget == self->button_pick )
        spawns_use_pick(ui, self);
    else if( widget == self->button_place )
        spawns_place(ui, self);
    else
        return 0;
    return 1;
}

static void
spawns_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct SpawnsPanel* self = opaque;
    /* Opened from the View menu; not part of the default layout. */
    if( visible )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
}

static void
spawns_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct SpawnsPanel* self = opaque;
    (void)ui;
    Editor_SpawnFree(&self->generated);
    ToriMem_Free(ToriMem_Editor(), self);
}

void
Editor_Spawns_Register(struct EditorUi* ui)
{
    struct SpawnsPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    Editor_SpawnInit(&self->generated);
    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8), edp_px(ui, 120),
        edp_px(ui, 260), "Spawns");
    assert(self->panel >= 0);
    self->row_square = ToriRSChrome_Label(chrome, self->panel, "");
    self->list = ToriRSChrome_VList(chrome, self->panel, "", SPAWNS_LIST_ROWS, 0);
    self->button_delete = ToriRSChrome_Button(chrome, self->panel, "Delete / suppress");
    ToriRSChrome_SetTooltip(chrome, self->button_delete,
        "Edited rows go; generated ones are suppressed (-name x z level)");
    self->button_restore = ToriRSChrome_Button(chrome, self->panel, "Restore");
    self->button_duplicate = ToriRSChrome_Button(chrome, self->panel, "Duplicate  [D]");
    ToriRSChrome_SetTooltip(chrome, self->button_duplicate, "Copy the selection one tile east, or to the latched tile");
    ToriRSChrome_Separator(chrome, self->panel);
    self->input_name = ToriRSChrome_TextInput(chrome, self->panel, "Name", "");
    self->radio_npc = ToriRSChrome_Radio(chrome, self->panel, "npc", SPAWNS_GROUP_KIND, 1);
    self->radio_obj = ToriRSChrome_Radio(chrome, self->panel, "obj", SPAWNS_GROUP_KIND, 0);
    self->slider_count = ToriRSChrome_Slider(chrome, self->panel, "Count", 1, 1000, 1, 1);
    self->button_pick = ToriRSChrome_Button(chrome, self->panel, "Use catalog pick");
    self->button_place = ToriRSChrome_Button(chrome, self->panel, "Place at latched tile");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "spawns";
    desc.title = "Spawns";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = spawns_tick;
    desc.activate = spawns_activate;
    desc.set_visible = spawns_set_visible;
    desc.destroy = spawns_destroy;
    Editor_UiAddPanel(ui, &desc);
}

int
Editor_Spawns_SelectionCount(struct EditorUi* ui)
{
    struct SpawnsPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "spawns");
    if( !self || !ui->chrome->panels[self->panel].visible )
        return 0;
    return self->selection_count;
}

int
Editor_Spawns_Duplicate(struct EditorUi* ui)
{
    struct SpawnsPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "spawns");
    if( !self )
        return 0;
    return spawns_duplicate(ui, self);
}

int
Editor_Spawns_Selection(
    struct EditorUi* ui,
    struct Editor_SpawnRow* out,
    int max)
{
    struct SpawnsPanel* self;
    int count;

    assert(ui);
    assert(out || max == 0);
    self = Editor_UiPanelSelf(ui, "spawns");
    if( !self )
        return 0;
    count = self->selection_count < max ? self->selection_count : max;
    memcpy(out, self->selection, sizeof(*out) * (size_t)count);
    return self->selection_count;
}
