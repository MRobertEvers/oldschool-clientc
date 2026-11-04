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
 *   Edit        the form under the list shows the selected row (the FIRST
 *               selected one, the anchor): kind, name (a pack symbol, the
 *               `#<id>` escape, or a display name the server's catalog
 *               resolves), absolute X / Z, level, count (obj), Suppressed.
 *               Apply is ONE undo step ("edit spawn"): an edited row is
 *               rewritten in place (EDITOR_CMD_SPAWN_EDIT), or deleted and
 *               re-added when the kind or the square changes; a generated
 *               row is suppressed and an edited row with the new fields
 *               added (Editor_SpawnEditPlan). With several rows selected,
 *               each field that differs from the anchor's is applied to every
 *               row -- X / Z / Level as the same offset, the rest as the
 *               same value.
 *   Move        "Move: click a tile" arms a move: the next world click
 *               (editor_panel.c) re-tiles the selection there, the anchor on
 *               the clicked tile and the rest at their offsets, as one undo
 *               step ("move spawn"); the pending destination is drawn (the
 *               wander preview's footprint) under the pointer meanwhile.
 *   Record      "Edit npc record..." opens the anchor's npc (obj) record in
 *               the Record panel: every field of every layer.
 *   Wander      "Show wander range" (on by default) draws each selected
 *               npc's wanderrange square and maxrange leash on the ground
 *               (map_editor_ghost.h MapEditorWander_Rects), also around the
 *               spawn ghost and at a pending move's destination.
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
#include "editor/map_editor_ghost.h"
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
    SPAWNS_GROUP_EDIT_KIND = 32,
};

/** Npc records whose wander values the preview remembers. */
#define SPAWNS_WANDER_CACHE 32

struct SpawnsWanderEntry
{
    char name[EDITOR_CMD_NAME_MAX];
    int wanderrange;
    int maxrange;
    /** 1 when no record stated a value and the engine defaults stand. */
    int defaulted;
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
    /** ui->hotkeys.serial the tips were labelled from. */
    uint32_t hint_serial;
    int input_name;
    int radio_npc;
    int radio_obj;
    int slider_count;
    int button_pick;
    int button_place;

    /* The edit form (the "Edit" tab). */
    int tabs;
    int row_edit;
    int input_edit_name;
    int button_edit_pick;
    int radio_edit_npc;
    int radio_edit_obj;
    int input_x;
    int input_z;
    int input_level;
    int input_count;
    int check_suppressed;
    int button_apply;
    int button_move;
    int button_record;
    int check_wander;
    int row_wander;
    /** The form shows this row (the anchor) and selection size; refilled
     *  only when they change, so typing is never overwritten. */
    struct Editor_SpawnRow form_row;
    int form_count;
    int form_valid;
    /** "Move: click a tile" is armed. */
    int move_armed;

    struct SpawnsWanderEntry wander[SPAWNS_WANDER_CACHE];
    int wander_count;
    /** The fact seq the cache was filled at: an edit anywhere empties it. */
    uint32_t wander_seq;

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
            if( line->kind != EDITOR_SPAWN_LINE_ROW )
                continue;
            /* A suppression row is listed through the generated row it
             * hides; one that hides nothing here is listed itself, so it can
             * be edited or deleted like any edited row. */
            if( line->row.suppress )
            {
                struct Editor_SpawnRow live = line->row;
                live.suppress = 0;
                if( Editor_SpawnFind(&self->generated, &live) >= 0 )
                    continue;
            }
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
spawns_form_refresh(
    struct EditorUi* ui,
    struct SpawnsPanel* self);

/*
 * TORIRS_EDITOR_SPAWNS=<x>,<z>,<level>[,record|-[,<scroll px>]] -- a headless photograph has
 * no pointer, so this opens the Spawns panel once, at the first frame with an
 * App, latches tile (x, z, level) (the panel lists that square) and, once the
 * list holds a row standing there, selects it (the edit form fills, the
 * wander preview draws); `record` also opens that row's npc in the Record
 * panel; <scroll px> scrolls the Spawns panel down to the edit form. Read
 * once per process.
 */
static struct
{
    int resolved;
    int on;
    int x;
    int z;
    int level;
    int record;
    int scroll;
    int done;
} g_spawns_env;

static void
spawns_env_step(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    if( !g_spawns_env.resolved )
    {
        const char* value = getenv("TORIRS_EDITOR_SPAWNS");
        char tail[16] = "";
        g_spawns_env.resolved = 1;
        if( value
            && sscanf(value, "%d,%d,%d,%15[^,],%d", &g_spawns_env.x, &g_spawns_env.z, &g_spawns_env.level, tail,
                   &g_spawns_env.scroll)
                   >= 3 )
        {
            g_spawns_env.on = 1;
            g_spawns_env.record = strcmp(tail, "record") == 0;
        }
    }
    if( !g_spawns_env.on || g_spawns_env.done || !ui->app || !ui->editor )
        return;
    if( !ui->chrome->panels[self->panel].visible )
    {
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
        ui->tools.tile_valid = 1;
        ui->tools.tile_x = g_spawns_env.x;
        ui->tools.tile_z = g_spawns_env.z;
        ui->tools.tile_level = g_spawns_env.level;
        return;
    }
    for( int r = 0; r < self->row_count; r++ )
        if( self->rows[r].row.abs_x == g_spawns_env.x && self->rows[r].row.abs_z == g_spawns_env.z
            && self->rows[r].row.level == g_spawns_env.level && !self->rows[r].suppressed )
        {
            self->selection[0] = self->rows[r].row;
            self->selection_count = 1;
            self->form_valid = 0;
            ToriRSChrome_VListSetSelected(ui->chrome, self->list, r);
            g_spawns_env.done = 1;
            /* For the photograph: over the right column, so the world's
             * middle (where the selected spawn and its ring are) stays clear. */
            if( ui->dock_x > 0 && ui->chrome->surface_w > ui->dock_x )
            {
                ToriRSChrome_PanelSetFixedWidth(ui->chrome, self->panel, ui->chrome->surface_w - ui->dock_x);
                ToriRSChrome_PanelSetFixedHeight(ui->chrome, self->panel, ui->dock_bottom - ui->dock_top);
                ToriRSChrome_PanelMove(ui->chrome, self->panel, ui->dock_x, ui->dock_top);
                ui->chrome->panels[self->panel].user_placed = 1;
            }
            if( g_spawns_env.scroll > 0 )
                ToriRSChrome_PanelSetScroll(ui->chrome, self->panel, g_spawns_env.scroll);
            Editor_UiLog(ui, "editor: TORIRS_EDITOR_SPAWNS selected %s at %d,%d", self->rows[r].row.name,
                g_spawns_env.x, g_spawns_env.z);
            if( g_spawns_env.record && Editor_UiPanelSelf(ui, "record")
                && Editor_Record_Open(ui, self->rows[r].row.is_obj ? TORIRSMAPED_CATALOG_OBJ : TORIRSMAPED_CATALOG_NPC,
                    self->rows[r].row.name)
                && ui->dock_x > 0 )
            {
                /* The record over the left half of the world, the ring and
                 * the Spawns form still in view. */
                int const w = (ui->dock_x - ui->world.x) / 2;
                Editor_Record_Place(ui, ui->world.x + w, ui->dock_top, w, ui->dock_bottom - ui->dock_top);
                (void)Editor_Record_SelectField(ui, "wanderrange", "");
            }
            return;
        }
}

/** The buttons with a hotkey wear its chord as a keycap (the tooltip ends
 *  with every chord), from the hotkey table; again whenever its serial moves.
 *  Move and Place arm the next world click and have no action of their own
 *  in the table, so they wear none. */
static void
spawns_hints(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    ToriRSChrome_SetTooltip(ui->chrome, self->button_duplicate, "Copy the selection one tile east");
    edp_button_chord(ui, self->button_duplicate, EDITOR_HK_EDIT_DUPLICATE);
    ToriRSChrome_SetTooltip(ui->chrome, self->button_delete, "Delete, or suppress a generated row");
    edp_button_chord(ui, self->button_delete, EDITOR_HK_EDIT_DELETE);
    self->hint_serial = ui->hotkeys.serial;
}

static void
spawns_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct SpawnsPanel* self = opaque;

    if( self->hint_serial != ui->hotkeys.serial )
        spawns_hints(ui, self);
    spawns_env_step(ui, self);
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
    spawns_form_refresh(ui, self);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->row_count; r++ )
    {
        const struct SpawnsListRow* row = &self->rows[r];
        char text[TORIRS_CHROME_INPUT_MAX];
        snprintf(text, sizeof(text), "%s%c %s %s %d %d %d%s", spawns_selection_find(self, &row->row) >= 0 ? "* " : "",
            row->edited ? (row->row.suppress ? '-' : '+') : row->suppressed ? '-' : ' ', row->row.is_obj ? "obj" : "npc",
            row->row.name, row->row.abs_x, row->row.abs_z, row->row.level,
            row->suppressed ? "  (suppressed)" : row->row.suppress ? "  (suppression row)" : "");
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

static int
spawns_delete(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    struct Editor_Cmd command;
    int const row = spawns_selected(ui, self);
    struct Editor_SpawnFile* edited;

    if( row < 0 )
        return 0;
    edited = spawns_edited(ui, self->map_x, self->map_z);
    if( self->rows[row].suppressed )
    {
        Editor_UiLog(ui, "editor: already suppressed; Restore brings it back");
        return 0;
    }
    if( !Editor_SpawnDeleteCommand(self->rows[row].edited ? edited : NULL, &self->rows[row].row, &command) )
        return 0;
    if( !self->rows[row].edited && !edited )
    {
        Editor_UiLog(ui, "editor: the edited spawn file is not open; suppression refused");
        return 0;
    }
    if( !edp_apply(ui, &command, 1, command.kind == EDITOR_CMD_SPAWN_SUPPRESS ? "suppress" : "delete spawn") )
        return 0;
    Editor_UiLog(ui, "editor: %s %s at %d,%d", command.kind == EDITOR_CMD_SPAWN_SUPPRESS ? "suppressed" : "deleted",
        self->rows[row].row.name, self->rows[row].row.abs_x, self->rows[row].row.abs_z);
    return 1;
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


/* ---- the edit form -------------------------------------------------------------- */

/** The listed row a selection entry is, or -1. */
static int
spawns_list_find(
    const struct SpawnsPanel* self,
    const struct Editor_SpawnRow* row)
{
    for( int r = 0; r < self->row_count; r++ )
        if( spawns_row_same(&self->rows[r].row, row) && self->rows[r].row.suppress == row->suppress )
            return r;
    for( int r = 0; r < self->row_count; r++ )
        if( spawns_row_same(&self->rows[r].row, row) )
            return r;
    return -1;
}

/** Whether a listed row is suppressed as the form shows it: a generated
 *  row the edited file hides, or an edited suppression row. */
static int
spawns_row_suppressed(const struct SpawnsListRow* row)
{
    return row->edited ? row->row.suppress : row->suppressed;
}

static void
spawns_set_int(
    struct EditorUi* ui,
    int widget,
    int value)
{
    char text[16];
    snprintf(text, sizeof(text), "%d", value);
    ToriRSChrome_SetText(ui->chrome, widget, text);
}

/** A whole decimal int from a text input. */
static int
spawns_get_int(
    struct EditorUi* ui,
    int widget,
    int* out)
{
    const char* text = ToriRSChrome_Text(ui->chrome, widget);
    char* end = NULL;
    long value;

    if( !text || !text[0] )
        return 0;
    value = strtol(text, &end, 10);
    if( !end || *end != '\0' || value < -1000000 || value > 1000000 )
        return 0;
    *out = (int)value;
    return 1;
}

/** The form shows the anchor (the first selected row): refilled only when
 *  the anchor or the selection size changed, never over typing. */
static void
spawns_form_refresh(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    int r;

    if( self->selection_count == 0 )
    {
        if( self->form_valid || self->form_count != 0 )
        {
            self->form_valid = 0;
            self->form_count = 0;
            self->move_armed = 0;
            ToriRSChrome_SetText(ui->chrome, self->row_edit, "select a spawn in the list to edit it");
        }
        return;
    }
    if( self->form_valid && self->form_count == self->selection_count
        && spawns_row_same(&self->form_row, &self->selection[0])
        && self->form_row.suppress == self->selection[0].suppress )
        return;
    r = spawns_list_find(self, &self->selection[0]);
    if( r < 0 )
        return;
    self->form_row = self->selection[0];
    self->form_count = self->selection_count;
    self->form_valid = 1;
    ToriRSChrome_SetText(ui->chrome, self->input_edit_name, self->form_row.name);
    ToriRSChrome_RadioSelect(ui->chrome, self->form_row.is_obj ? self->radio_edit_obj : self->radio_edit_npc);
    spawns_set_int(ui, self->input_x, self->form_row.abs_x);
    spawns_set_int(ui, self->input_z, self->form_row.abs_z);
    spawns_set_int(ui, self->input_level, self->form_row.level);
    spawns_set_int(ui, self->input_count, self->form_row.is_obj ? self->form_row.count : 1);
    ToriRSChrome_SetChecked(ui->chrome, self->check_suppressed, spawns_row_suppressed(&self->rows[r]));
    if( self->selection_count == 1 )
        snprintf(line, sizeof(line), "editing %s %s (%s)", self->form_row.is_obj ? "obj" : "npc", self->form_row.name,
            self->rows[r].edited ? "edited file" : "generated: an edit suppresses it and adds an edited row");
    else
        snprintf(line, sizeof(line), "%d selected: changed fields apply to each (X/Z/Level as an offset)",
            self->selection_count);
    ToriRSChrome_SetText(ui->chrome, self->row_edit, line);
}

/**
 * The typed name as a spawn row name: a pack symbol or `#<id>` the catalog
 * knows (when the server has one), or a display name it resolves to exactly
 * one record's symbol. Without a catalog only the grammar is checked.
 */
static int
spawns_resolve_name(
    struct EditorUi* ui,
    int is_obj,
    const char* typed,
    char out[EDITOR_CMD_NAME_MAX])
{
    struct EditorHost_CatalogRecord record;
    struct EditorHost_CatalogPage page;
    uint32_t const kind = is_obj ? TORIRSMAPED_CATALOG_OBJ : TORIRSMAPED_CATALOG_NPC;
    const char* what = is_obj ? "obj" : "npc";
    int ok = 0;

    assert(typed);
    while( *typed == ' ' )
        typed++;
    if( !typed[0] || strlen(typed) >= EDITOR_CMD_NAME_MAX )
    {
        Editor_UiLog(ui, "editor: type the spawn's name: a pack symbol, #<id> or a display name");
        return 0;
    }
    if( !ui->editor || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
    {
        struct Editor_SpawnRow probe;
        memset(&probe, 0, sizeof(probe));
        snprintf(probe.name, sizeof(probe.name), "%s", typed);
        probe.count = 1;
        if( !Editor_SpawnRowValid(&probe) )
        {
            Editor_UiLog(ui, "editor: '%s' is not a spawn name (no catalog here to resolve a display name)", typed);
            return 0;
        }
        snprintf(out, EDITOR_CMD_NAME_MAX, "%s", typed);
        return 1;
    }

    memset(&record, 0, sizeof(record));
    if( typed[0] == '#' )
    {
        char* end = NULL;
        long id = strtol(typed + 1, &end, 10);
        if( !end || *end != '\0' || end == typed + 1 || id < 0 )
        {
            Editor_UiLog(ui, "editor: '%s' is not a #<id>", typed);
            return 0;
        }
        if( Editor_HostMapEdCatalogRecord(&ui->editor->host, kind, (int32_t)id, "", &record) == EDITOR_HOST_OK )
        {
            snprintf(out, EDITOR_CMD_NAME_MAX, "%s", typed);
            ok = 1;
        }
        else
            Editor_UiLog(ui, "editor: there is no %s with id %ld", what, id);
        Editor_HostCatalogRecordFree(&record);
        return ok;
    }
    if( !strchr(typed, ' ')
        && Editor_HostMapEdCatalogRecord(&ui->editor->host, kind, -1, typed, &record) == EDITOR_HOST_OK
        && record.name && strcmp(record.name, typed) == 0 )
    {
        Editor_HostCatalogRecordFree(&record);
        snprintf(out, EDITOR_CMD_NAME_MAX, "%s", typed);
        return 1;
    }
    Editor_HostCatalogRecordFree(&record);

    /* A display name: the symbol when exactly one record carries it. */
    memset(&page, 0, sizeof(page));
    if( Editor_HostMapEdCatalogQuery(&ui->editor->host, kind, typed, TORIRSMAPED_CATALOG_EXACT, 0, 8, &page)
        != EDITOR_HOST_OK )
    {
        Editor_UiLog(ui, "editor: the catalog could not look '%s' up", typed);
        return 0;
    }
    if( page.total == 1 && page.row_count == 1 && page.rows[0].name && strlen(page.rows[0].name) < EDITOR_CMD_NAME_MAX )
    {
        snprintf(out, EDITOR_CMD_NAME_MAX, "%s", page.rows[0].name);
        Editor_UiLog(ui, "editor: '%s' is %s %s", typed, what, out);
        ok = 1;
    }
    else if( page.total == 0 )
        Editor_UiLog(ui, "editor: no %s is named '%s'", what, typed);
    else
        Editor_UiLog(ui, "editor: %u %ss are named '%s' (%s, %s...): type the symbol or #<id>", page.total, what,
            typed, page.row_count > 0 ? page.rows[0].name : "?", page.row_count > 1 ? page.rows[1].name : "?");
    Editor_HostCatalogPageFree(&page);
    return ok;
}

/**
 * Edit every selected row the way `anchor_want` edits the anchor: each field
 * the anchor changes is applied to each row (the tile and level as the same
 * offset), and `want_suppressed` likewise when it differs from the anchor's.
 * One undo step named `verb`. Returns the commands the authority took.
 */
static int
spawns_edit_selection(
    struct EditorUi* ui,
    struct SpawnsPanel* self,
    const struct Editor_SpawnRow* anchor_want,
    int want_suppressed,
    const char* verb)
{
    struct Editor_SpawnRow next[SPAWNS_SELECTION_MAX];
    struct Editor_SpawnRow anchor;
    struct Editor_Cmd* commands;
    int capacity;
    int count = 0;
    int anchor_suppressed;
    int applied;
    int r;

    if( self->selection_count == 0 )
    {
        Editor_UiLog(ui, "editor: select the spawn(s) to %s first", verb);
        return 0;
    }
    anchor = self->selection[0];
    r = spawns_list_find(self, &anchor);
    if( r < 0 )
        return 0;
    anchor_suppressed = spawns_row_suppressed(&self->rows[r]);
    commands = edp_scratch(&capacity);

    for( int i = 0; i < self->selection_count; i++ )
    {
        struct Editor_SpawnEditSource source;
        struct Editor_SpawnRow want;
        struct Editor_SpawnFile* edited;
        int row_suppressed;
        int n;
        int lr = spawns_list_find(self, &self->selection[i]);

        if( lr < 0 )
            continue;
        memset(&source, 0, sizeof(source));
        source.row = self->rows[lr].row;
        source.edited = self->rows[lr].edited;
        source.suppressed = self->rows[lr].suppressed;
        row_suppressed = spawns_row_suppressed(&self->rows[lr]);

        want = source.row;
        if( strcmp(anchor_want->name, anchor.name) != 0 )
            snprintf(want.name, sizeof(want.name), "%s", anchor_want->name);
        if( anchor_want->is_obj != anchor.is_obj )
            want.is_obj = anchor_want->is_obj;
        if( anchor_want->is_obj && (anchor_want->count != anchor.count || !anchor.is_obj) )
            want.count = anchor_want->count;
        want.abs_x += anchor_want->abs_x - anchor.abs_x;
        want.abs_z += anchor_want->abs_z - anchor.abs_z;
        want.level += anchor_want->level - anchor.level;
        want.suppress = want_suppressed != anchor_suppressed ? want_suppressed : row_suppressed;
        if( !want.is_obj )
            want.count = 1;

        edited = NULL;
        if( source.edited )
        {
            struct Editor_DocHandle handle;
            char key[EDITOR_DOC_KEY_MAX];
            Editor_SpawnRowDocKey(&source.row, key);
            if( ui->editor && Editor_DocFind(&ui->editor->doc, EDITOR_DOC_SPAWN, key, &handle) && handle.spawn->loaded )
                edited = &handle.spawn->file;
        }
        n = Editor_SpawnEditPlan(edited, &source, &want, commands + count, capacity - count);
        if( n < 0 )
        {
            Editor_UiLog(ui, "editor: %s %d %d %d is not a spawn the server takes (name, tile >= 0, level 0..3, "
                             "count >= 1)", want.name, want.abs_x, want.abs_z, want.level);
            return 0;
        }
        count += n;
        /* What the row is after: a changed generated row becomes the edited
         * row added for it. */
        next[i] = want;
        if( !source.edited )
        {
            struct Editor_SpawnRow fields = want;
            fields.suppress = 0;
            if( Editor_SpawnRowsEqual(&fields, &source.row) )
                next[i] = source.row; /* only its suppression changed */
            else
                next[i].suppress = 0; /* the edited row added for it */
        }
    }
    if( count == 0 )
    {
        Editor_UiLog(ui, "editor: nothing to change");
        return 0;
    }
    for( int i = 0; i < count; i++ )
        if( !edp_ensure_doc(ui, EDITOR_DOC_SPAWN, commands[i].doc_key) )
        {
            Editor_UiLog(ui, "editor: could not open the spawn document %s", commands[i].doc_key);
            return 0;
        }
    applied = edp_apply(ui, commands, count, verb);
    if( applied == count )
    {
        memcpy(self->selection, next, sizeof(next[0]) * (size_t)self->selection_count);
        self->form_valid = 0;
        Editor_UiLog(ui, "editor: %s: %d row(s), %d command(s), one undo step", verb, self->selection_count, count);
    }
    return applied;
}

/** Apply: the form, read and checked, as one undo step. */
static int
spawns_apply_form(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    struct Editor_SpawnRow want;
    const char* typed = ToriRSChrome_Text(ui->chrome, self->input_edit_name);

    if( self->selection_count == 0 )
    {
        Editor_UiLog(ui, "editor: select a spawn in the list first");
        return 0;
    }
    memset(&want, 0, sizeof(want));
    want.is_obj = ToriRSChrome_Checked(ui->chrome, self->radio_edit_obj) ? 1 : 0;
    if( !spawns_get_int(ui, self->input_x, &want.abs_x) || !spawns_get_int(ui, self->input_z, &want.abs_z)
        || !spawns_get_int(ui, self->input_level, &want.level) )
    {
        Editor_UiLog(ui, "editor: X, Z and Level must be whole numbers");
        return 0;
    }
    want.count = 1;
    if( want.is_obj && (!spawns_get_int(ui, self->input_count, &want.count) || want.count < 1) )
    {
        Editor_UiLog(ui, "editor: an obj spawn's count is a whole number >= 1");
        return 0;
    }
    if( want.level < 0 || want.level > 3 || want.abs_x < 0 || want.abs_z < 0 )
    {
        Editor_UiLog(ui, "editor: the tile must be >= 0 and the level 0..3");
        return 0;
    }
    if( strcmp(typed ? typed : "", self->selection[0].name) == 0 && want.is_obj == self->selection[0].is_obj )
        snprintf(want.name, sizeof(want.name), "%s", self->selection[0].name);
    else if( !spawns_resolve_name(ui, want.is_obj, typed ? typed : "", want.name) )
        return 0;
    {
        struct Editor_SpawnRow const* a = &self->selection[0];
        int const moved_only = strcmp(want.name, a->name) == 0 && want.is_obj == a->is_obj
                               && (!want.is_obj || want.count == a->count);
        int const suppressed = ToriRSChrome_Checked(ui->chrome, self->check_suppressed) ? 1 : 0;
        int const r = spawns_list_find(self, a);
        int const toggled = r >= 0 && suppressed != spawns_row_suppressed(&self->rows[r]);
        return spawns_edit_selection(ui, self, &want, suppressed,
            moved_only && !toggled && (want.abs_x != a->abs_x || want.abs_z != a->abs_z || want.level != a->level)
                ? "move spawn"
                : "edit spawn");
    }
}

/** The catalog's npc / obj pick into the form's name (its pack symbol). */
static void
spawns_edit_use_pick(
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
    if( Editor_HostMapEdCatalogRecord(&ui->editor->host, kind, ui->tools.pick_id, "", &record) == EDITOR_HOST_OK
        && record.name )
    {
        ToriRSChrome_SetText(ui->chrome, self->input_edit_name, record.name);
        ToriRSChrome_RadioSelect(ui->chrome, kind == TORIRSMAPED_CATALOG_OBJ ? self->radio_edit_obj : self->radio_edit_npc);
    }
    else
        Editor_UiLog(ui, "editor: no catalog record for that pick");
    Editor_HostCatalogRecordFree(&record);
}

/* ---- the wander preview ------------------------------------------------------- */

/** An int field of a catalog record; -1 when absent or not a number. */
static int
spawns_record_int(
    const struct EditorHost_CatalogRecord* record,
    const char* key)
{
    for( int i = 0; i < record->field_count; i++ )
        if( strcmp(record->fields[i].key, key) == 0 )
        {
            char* end = NULL;
            long value = strtol(record->fields[i].value, &end, 10);
            if( end && end != record->fields[i].value && *end == '\0' && value >= 0 )
                return (int)value;
            return -1;
        }
    return -1;
}

/** The npc's wanderrange / maxrange: the Record panel's (unsaved edits
 *  included) when it holds that npc, else the server's catalog (the merged
 *  record), else the engine defaults. Remembered per name until a fact. */
static void
spawns_wander_values(
    struct EditorUi* ui,
    struct SpawnsPanel* self,
    const char* name,
    int* out_wander,
    int* out_leash)
{
    struct SpawnsWanderEntry* entry;

    if( Editor_Record_WanderValues(ui, "npc", name, out_wander, out_leash) )
        return;
    if( ui->editor && ui->editor->last_cmd_seq != self->wander_seq )
    {
        self->wander_seq = ui->editor->last_cmd_seq;
        self->wander_count = 0;
    }
    for( int i = 0; i < self->wander_count; i++ )
        if( strcmp(self->wander[i].name, name) == 0 )
        {
            *out_wander = self->wander[i].wanderrange;
            *out_leash = self->wander[i].maxrange;
            return;
        }
    if( self->wander_count == SPAWNS_WANDER_CACHE )
        self->wander_count = 0;
    entry = &self->wander[self->wander_count++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    entry->wanderrange = -1;
    entry->maxrange = -1;
    if( ui->editor && (ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
    {
        struct EditorHost_CatalogRecord record;
        int const by_id = name[0] == '#';
        memset(&record, 0, sizeof(record));
        if( Editor_HostMapEdCatalogRecord(&ui->editor->host, TORIRSMAPED_CATALOG_NPC,
                by_id ? (int32_t)atoi(name + 1) : -1, by_id ? "" : name, &record)
            == EDITOR_HOST_OK )
        {
            entry->wanderrange = spawns_record_int(&record, "wanderrange");
            entry->maxrange = spawns_record_int(&record, "maxrange");
        }
        Editor_HostCatalogRecordFree(&record);
    }
    entry->defaulted = entry->wanderrange < 0 || entry->maxrange < 0;
    if( entry->wanderrange < 0 )
        entry->wanderrange = MAP_EDITOR_WANDER_DEFAULT;
    if( entry->maxrange < 0 )
        entry->maxrange = MAP_EDITOR_LEASH_DEFAULT;
    *out_wander = entry->wanderrange;
    *out_leash = entry->maxrange;
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
        || widget == self->input_name || widget == self->tabs || widget == self->input_edit_name
        || widget == self->radio_edit_npc || widget == self->radio_edit_obj || widget == self->input_x
        || widget == self->input_z || widget == self->input_level || widget == self->input_count
        || widget == self->check_suppressed || widget == self->check_wander )
        return 1;
    if( widget == self->button_apply )
    {
        (void)spawns_apply_form(ui, self);
        return 1;
    }
    if( widget == self->button_edit_pick )
    {
        spawns_edit_use_pick(ui, self);
        return 1;
    }
    if( widget == self->button_move )
    {
        if( self->selection_count == 0 )
            Editor_UiLog(ui, "editor: select the spawn(s) to move first");
        else
        {
            self->move_armed = !self->move_armed;
            /* The click lands through the select tool (editor_panel.c). */
            if( self->move_armed && ui->tools.tool != EDITOR_TOOL_SELECT )
                Editor_UiSetTool(ui, EDITOR_TOOL_SELECT);
            Editor_UiLog(ui, self->move_armed ? "editor: click the tile to move %d spawn(s) to (the first lands on it)"
                                              : "editor: move cancelled (%d selected)",
                self->selection_count);
        }
        return 1;
    }
    if( widget == self->button_record )
    {
        if( self->selection_count == 0 )
            Editor_UiLog(ui, "editor: select a spawn first");
        else
            (void)Editor_Record_Open(ui, self->selection[0].is_obj ? TORIRSMAPED_CATALOG_OBJ : TORIRSMAPED_CATALOG_NPC,
                self->selection[0].name);
        return 1;
    }
    if( widget == self->button_duplicate )
        (void)spawns_duplicate(ui, self);
    else if( widget == self->button_delete )
        (void)spawns_delete(ui, self);
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
    self->button_duplicate = ToriRSChrome_Button(chrome, self->panel, "Duplicate");
    ToriRSChrome_Separator(chrome, self->panel);
    {
        static const char* const tabs[] = { "Edit", "Place" };
        self->tabs = ToriRSChrome_Tabs(chrome, self->panel, tabs, 2, 0);
    }
    ToriRSChrome_PanelBeginTab(chrome, self->panel, 0);
    self->row_edit = ToriRSChrome_Label(chrome, self->panel, "select a spawn in the list to edit it");
    self->input_edit_name = ToriRSChrome_TextInput(chrome, self->panel, "Spawn name", "");
    ToriRSChrome_SetTooltip(chrome, self->input_edit_name, "pack symbol, #<id>, or a display name");
    self->button_edit_pick = ToriRSChrome_Button(chrome, self->panel, "Fill from pick");
    self->radio_edit_npc = ToriRSChrome_Radio(chrome, self->panel, "npc spawn", SPAWNS_GROUP_EDIT_KIND, 1);
    self->radio_edit_obj = ToriRSChrome_Radio(chrome, self->panel, "obj spawn", SPAWNS_GROUP_EDIT_KIND, 0);
    self->input_x = ToriRSChrome_TextInput(chrome, self->panel, "X", "");
    self->input_z = ToriRSChrome_TextInput(chrome, self->panel, "Z", "");
    self->input_level = ToriRSChrome_TextInput(chrome, self->panel, "Level", "");
    self->input_count = ToriRSChrome_TextInput(chrome, self->panel, "Count (obj)", "");
    self->check_suppressed = ToriRSChrome_Checkbox(chrome, self->panel, "Suppressed", 0);
    ToriRSChrome_SetTooltip(chrome, self->check_suppressed,
        "generated: hidden by a -name row; edited: written as one");
    self->button_apply = ToriRSChrome_Button(chrome, self->panel, "Apply edit");
    self->button_move = ToriRSChrome_Button(chrome, self->panel, "Move: click a tile");
    ToriRSChrome_SetTooltip(chrome, self->button_move, "the next world click moves the selection");
    self->button_record = ToriRSChrome_Button(chrome, self->panel, "Edit npc record...");
    ToriRSChrome_SetTooltip(chrome, self->button_record, "every field of the npc / obj record");
    self->check_wander = ToriRSChrome_Checkbox(chrome, self->panel, "Show wander range", 1);
    self->row_wander = ToriRSChrome_Label(chrome, self->panel, "");
    ToriRSChrome_PanelBeginTab(chrome, self->panel, 1);
    self->input_name = ToriRSChrome_TextInput(chrome, self->panel, "Name", "");
    self->radio_npc = ToriRSChrome_Radio(chrome, self->panel, "npc", SPAWNS_GROUP_KIND, 1);
    self->radio_obj = ToriRSChrome_Radio(chrome, self->panel, "obj", SPAWNS_GROUP_KIND, 0);
    self->slider_count = ToriRSChrome_Slider(chrome, self->panel, "Count", 1, 1000, 1, 1);
    self->button_pick = ToriRSChrome_Button(chrome, self->panel, "Use catalog pick");
    self->button_place = ToriRSChrome_Button(chrome, self->panel, "Place at latched tile");
    ToriRSChrome_PanelBeginTab(chrome, self->panel, -1);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
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
Editor_Spawns_DeleteSelected(struct EditorUi* ui)
{
    struct SpawnsPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "spawns");
    if( !self )
        return 0;
    return spawns_delete(ui, self);
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

int
Editor_Spawns_MoveArmed(struct EditorUi* ui)
{
    struct SpawnsPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "spawns");
    return self && self->move_armed && self->selection_count > 0;
}

int
Editor_Spawns_MoveTo(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level)
{
    struct SpawnsPanel* self;
    struct Editor_SpawnRow want;
    int r;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "spawns");
    assert(self);
    self->move_armed = 0;
    if( self->selection_count == 0 )
        return 0;
    r = spawns_list_find(self, &self->selection[0]);
    if( r < 0 )
        return 0;
    want = self->selection[0];
    want.abs_x = world_x;
    want.abs_z = world_z;
    want.level = level < 0 ? 0 : level > 3 ? 3 : level;
    return spawns_edit_selection(ui, self, &want, spawns_row_suppressed(&self->rows[r]), "move spawn");
}

int
Editor_Spawns_SetEditForm(
    struct EditorUi* ui,
    const char* name,
    int is_obj,
    int x,
    int z,
    int level,
    int count,
    int suppressed)
{
    struct SpawnsPanel* self;

    assert(ui);
    assert(name);
    self = Editor_UiPanelSelf(ui, "spawns");
    assert(self);
    ToriRSChrome_SetText(ui->chrome, self->input_edit_name, name);
    ToriRSChrome_RadioSelect(ui->chrome, is_obj ? self->radio_edit_obj : self->radio_edit_npc);
    spawns_set_int(ui, self->input_x, x);
    spawns_set_int(ui, self->input_z, z);
    spawns_set_int(ui, self->input_level, level);
    spawns_set_int(ui, self->input_count, count);
    ToriRSChrome_SetChecked(ui->chrome, self->check_suppressed, suppressed);
    return spawns_apply_form(ui, self);
}

int
Editor_Spawns_Select(
    struct EditorUi* ui,
    const struct Editor_SpawnRow* rows,
    int count)
{
    struct SpawnsPanel* self;

    assert(ui);
    assert(rows || count == 0);
    self = Editor_UiPanelSelf(ui, "spawns");
    assert(self);
    if( count > SPAWNS_SELECTION_MAX )
        count = SPAWNS_SELECTION_MAX;
    memcpy(self->selection, rows, sizeof(*rows) * (size_t)count);
    self->selection_count = count;
    self->form_valid = 0;
    return count;
}

int
Editor_Spawns_WanderRects(
    struct EditorUi* ui,
    int hover_valid,
    int hover_x,
    int hover_z,
    int hover_level,
    int ghost_npc_id,
    struct MapEditorWanderRect* out,
    int max)
{
    struct SpawnsPanel* self;
    int count = 0;
    int wander;
    int leash;

    assert(ui);
    assert(out || max == 0);
    self = Editor_UiPanelSelf(ui, "spawns");
    if( !self || !ToriRSChrome_Checked(ui->chrome, self->check_wander) )
        return 0;

    /* The spawn ghost under the pointer (Place with an npc pick). */
    if( ghost_npc_id >= 0 && hover_valid )
    {
        char name[16];
        snprintf(name, sizeof(name), "#%d", ghost_npc_id);
        spawns_wander_values(ui, self, name, &wander, &leash);
        count += MapEditorWander_Rects(hover_x, hover_z, hover_level, 1, wander, leash, out + count, max - count);
    }
    if( !ui->chrome->panels[self->panel].visible )
        return count;
    for( int i = 0; i < self->selection_count; i++ )
    {
        const struct Editor_SpawnRow* row = &self->selection[i];
        int const r = spawns_list_find(self, row);
        if( row->is_obj || r < 0 || spawns_row_suppressed(&self->rows[r]) )
            continue;
        spawns_wander_values(ui, self, row->name, &wander, &leash);
        count += MapEditorWander_Rects(row->abs_x, row->abs_z, row->level, 1, wander, leash, out + count, max - count);
        /* A pending move: where it would stand, and roam, from the pointer. */
        if( self->move_armed && hover_valid )
        {
            int const x = hover_x + row->abs_x - self->selection[0].abs_x;
            int const z = hover_z + row->abs_z - self->selection[0].abs_z;
            if( count < max )
            {
                out[count].kind = MAP_EDITOR_WANDER_FOOTPRINT;
                out[count].x0 = x;
                out[count].z0 = z;
                out[count].x1 = x;
                out[count].z1 = z;
                out[count].level = hover_level;
                count++;
            }
            count += MapEditorWander_Rects(x, z, hover_level, 1, wander, leash, out + count, max - count);
        }
        if( i == 0 )
        {
            char line[TORIRS_CHROME_LABEL_MAX];
            snprintf(line, sizeof(line), "%s: wander %d, leash %d", row->name, wander, leash);
            ToriRSChrome_SetText(ui->chrome, self->row_wander, line);
        }
    }
    return count;
}
