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
 *   View        "Show spawns in the world" (Shift+F6) stands every spawn of the
 *               open squares in the scene: npc bodies held still in their
 *               ready pose, ground items on their tiles, suppressed rows
 *               translucent, and the play account where it stands. The View
 *               tab filters them (npcs, objs, the player, generated, edited,
 *               suppressed) and turns the tile markers and the wander
 *               preview on and off. app_map_editor.c lands what
 *               Editor_Spawns_PreviewRows lists.
 *               A quest's or skill's own .spawn rows (the "spawns:" document)
 *               are listed and previewed too, marked ~; like generated rows
 *               they are not written, an edit suppresses them. The toggles
 *               persist in editor_prefs.ini.
 *               A spawn this window adds (a Place click, this panel, a
 *               paste, a stamp, a square copy: EditorUi::spawns_added_*)
 *               turns the switch on, and the filters a new edited row of its
 *               kind passes, and the status bar says so: placed with the
 *               preview off it was a console line and nothing in the world.
 *   Pick        with the preview up, a Select click on a spawn's model or
 *               marker selects it (Shift adds, Ctrl toggles, Alt takes out) and
 *               the minimenu's "Select spawn" does the same with any tool.
 *               The list follows the spawn's square.
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
#include "editor/builder/builder_panels.h"
#include "editor/editor_npc.h"
#include "editor/map_editor_ghost.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPAWNS_ROWS_MAX 1024
/** Rows the list shows at once: fewer than before, so the Edit form below
 *  it fits the column in the default layout (D11). */
#define SPAWNS_LIST_ROWS 8
/** Display names the rows show, remembered per spawn name. */
#define SPAWNS_DISPLAY_NAMES 128
/** Rows one selection (and so one Duplicate) holds. */
#define SPAWNS_SELECTION_MAX 64

enum
{
    SPAWNS_GROUP_KIND = 31,
    SPAWNS_GROUP_EDIT_KIND = 32,
};

/** Spawn names the preview has resolved to ids. */
#define SPAWNS_PREVIEW_NAMES MAP_EDITOR_SPAWN_PREVIEW_MAX

struct SpawnsPreviewName
{
    char name[EDITOR_CMD_NAME_MAX];
    int is_obj;
    int id;
};

/** One open square's generated file, parsed for the preview. */
struct SpawnsPreviewSquare
{
    int map_x;
    int map_z;
    int used;
    int tried;
    size_t length;
    struct Editor_SpawnFile generated;
    /** The "spawns:" document: quests' and skills' rows. */
    size_t scripted_length;
    struct Editor_SpawnFile scripted;
};

/** Npc records whose wander values the preview remembers. */
#define SPAWNS_WANDER_CACHE 32

struct SpawnsWanderEntry
{
    char name[EDITOR_CMD_NAME_MAX];
    /** What the merged record states (editor_npc.h); the preview draws
     *  its EFFECTIVE behaviour (a patroller's route, a wanderer's square,
     *  a nomove's pin), never the raw wanderrange (D10). */
    struct Editor_NpcMovement movement;
};

/** A spawn name's display name ("hans" -> "Hans"), from the catalog. */
struct SpawnsDisplayName
{
    char name[EDITOR_CMD_NAME_MAX];
    int is_obj;
    char display[48];
};

struct SpawnsListRow
{
    struct Editor_SpawnRow row;
    /** 1 the edited file, 0 the generated one or a quest's / skill's. */
    int edited;
    /** A quest's or skill's own .spawn file holds it. */
    int scripted;
    int suppressed;
};

struct SpawnsPanel
{
    int panel;
    int row_square;
    /** Find: the list shows the rows whose name, display name, tile or
     *  file word holds every term (D11). */
    int input_find;
    char find[64];
    int list;
    int button_delete;
    int button_restore;
    int button_duplicate;
    /** Group...: the selection into the encounter builder's set (6.7). */
    int button_group;
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

    /* The world preview: the master switch above the list, the View tab. */
    int check_preview;
    int check_view_npcs;
    int check_view_objs;
    int check_view_player;
    int check_view_generated;
    int check_view_edited;
    int check_view_scripted;
    int check_view_suppressed;
    int check_view_markers;
    int row_preview;
    /** The toggles as last read from or written to ui->prefs.spawn_view. */
    int view_prefs;
    int preview_landed;
    int preview_waiting;
    int preview_missing;
    int preview_counts_valid;
    /** What the rows were merged from; what they came to. */
    uint32_t preview_signature;
    uint32_t preview_hash;
    uint32_t preview_serial;
    int preview_clipped;
    struct MapEditorSpawnPreviewRow preview_rows[MAP_EDITOR_SPAWN_PREVIEW_MAX];
    int preview_count;
    struct SpawnsPreviewSquare preview_squares[EDITOR_DOC_MAX_SQUARES];
    struct SpawnsPreviewName preview_names[SPAWNS_PREVIEW_NAMES];
    int preview_name_count;
    /** A world pick waits for the list to hold its row, then shows it. */
    int reveal_pending;
    /** A tab was clicked: scroll its content, below the list, into view. */
    int tab_scroll_pending;
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
    /** The "spawns:" document, parsed; its text's length. */
    struct Editor_SpawnFile scripted;
    size_t scripted_length;
    int generated_x;
    int generated_z;
    int generated_tried;
    size_t generated_length;

    struct SpawnsListRow rows[SPAWNS_ROWS_MAX];
    int row_count;
    /** The list's row i is rows[shown[i]] (the Find box's filter). */
    int shown[SPAWNS_ROWS_MAX];
    int shown_count;
    struct SpawnsDisplayName displays[SPAWNS_DISPLAY_NAMES];
    int display_count;
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

    /* A square with no quest rows must not keep the last square's. */
    if( !self->generated_tried || self->generated_x != self->map_x || self->generated_z != self->map_z )
    {
        Editor_SpawnFree(&self->scripted);
        Editor_SpawnInit(&self->scripted);
        self->scripted_length = 0;
    }
    Editor_SpawnScriptedKey(key, self->map_x, self->map_z);
    (void)edp_ensure_doc(ui, EDITOR_DOC_TEXT, key);
    if( ui->editor && Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, key, &handle) && handle.text->text
        && handle.text->length != self->scripted_length )
    {
        (void)Editor_SpawnParse(&self->scripted, handle.text->text, handle.text->length);
        self->scripted_length = handle.text->length;
    }
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

/** The display name of a row's record, "" while unknown. Asked of the
 *  catalog once per name; a busy index is asked again. */
static const char*
spawns_display(
    struct EditorUi* ui,
    struct SpawnsPanel* self,
    const struct Editor_SpawnRow* row)
{
    struct EditorHost_CatalogRecord record;
    struct SpawnsDisplayName* entry;
    enum EditorHost_Status status;

    for( int i = 0; i < self->display_count; i++ )
        if( self->displays[i].is_obj == row->is_obj && strcmp(self->displays[i].name, row->name) == 0 )
            return self->displays[i].display;
    if( !ui->editor || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return "";
    memset(&record, 0, sizeof(record));
    status = Editor_HostMapEdCatalogRecord(&ui->editor->host, row->is_obj ? TORIRSMAPED_CATALOG_OBJ : TORIRSMAPED_CATALOG_NPC,
        row->name[0] == '#' ? (int32_t)atoi(row->name + 1) : -1, row->name[0] == '#' ? "" : row->name, &record);
    if( status == EDITOR_HOST_BUSY )
    {
        Editor_HostCatalogRecordFree(&record);
        return "";
    }
    if( self->display_count == SPAWNS_DISPLAY_NAMES )
        self->display_count = 0;
    entry = &self->displays[self->display_count++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->name, sizeof(entry->name), "%s", row->name);
    entry->is_obj = row->is_obj;
    if( status == EDITOR_HOST_OK && record.display && record.display[0] && strcmp(record.display, row->name) != 0 )
        snprintf(entry->display, sizeof(entry->display), "%s", record.display);
    Editor_HostCatalogRecordFree(&record);
    return entry->display;
}

/** One list row's text. */
static void
spawns_row_text(
    struct EditorUi* ui,
    struct SpawnsPanel* self,
    const struct SpawnsListRow* row,
    char* out,
    size_t out_size)
{
    const char* display = spawns_display(ui, self, &row->row);
    snprintf(out, out_size, "%s%c %s %s%s%s%s %d %d %d%s", spawns_selection_find(self, &row->row) >= 0 ? "* " : "",
        row->edited ? (row->row.suppress ? '-' : '+') : row->suppressed ? '-' : row->scripted ? '~' : ' ',
        row->row.is_obj ? "obj" : "npc", row->row.name, display[0] ? " (" : "", display, display[0] ? ")" : "",
        row->row.abs_x, row->row.abs_z, row->row.level,
        row->suppressed ? "  (suppressed)" : row->row.suppress ? "  (suppression row)" : "");
}

/** Case-insensitive: every whitespace-separated term of `find` in `text`. */
static int
spawns_row_matches(
    const char* text,
    const char* find)
{
    const char* at = find;

    while( *at )
    {
        char term[64];
        size_t n = 0;
        size_t hay_length;
        int found = 0;
        while( *at == ' ' )
            at++;
        while( *at && *at != ' ' && n + 1 < sizeof(term) )
            term[n++] = (char)tolower((unsigned char)*at++);
        term[n] = '\0';
        if( n == 0 )
            break;
        hay_length = strlen(text);
        for( size_t i = 0; i + n <= hay_length && !found; i++ )
        {
            size_t k = 0;
            while( k < n && tolower((unsigned char)text[i + k]) == (unsigned char)term[k] )
                k++;
            found = k == n;
        }
        if( !found )
            return 0;
    }
    return 1;
}

/** The list's rows from the panel's, through the Find box. */
static void
spawns_filter(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    int first;
    int shown;

    self->shown_count = 0;
    for( int r = 0; r < self->row_count; r++ )
    {
        if( self->find[0] )
        {
            char text[TORIRS_CHROME_INPUT_MAX];
            spawns_row_text(ui, self, &self->rows[r], text, sizeof(text));
            if( !spawns_row_matches(text, self->find) )
                continue;
        }
        self->shown[self->shown_count++] = r;
    }
    ToriRSChrome_VListSetCount(ui->chrome, self->list, self->shown_count);
    if( ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        for( int r = first; r < first + shown; r++ )
            ToriRSChrome_VListSetRow(ui->chrome, self->list, r, "");
}

/** The list row a panel row is shown at, -1 when the Find box hides it. */
static int
spawns_list_index(
    const struct SpawnsPanel* self,
    int row)
{
    for( int i = 0; i < self->shown_count; i++ )
        if( self->shown[i] == row )
            return i;
    return -1;
}

/** The panel row the list's selected row is, -1 for none. */
static int
spawns_list_picked(
    struct EditorUi* ui,
    const struct SpawnsPanel* self)
{
    int const at = ToriRSChrome_VListSelected(ui->chrome, self->list);
    return at >= 0 && at < self->shown_count ? self->shown[at] : -1;
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
                if( Editor_SpawnFind(&self->generated, &live) >= 0 || Editor_SpawnFind(&self->scripted, &live) >= 0 )
                    continue;
            }
            self->rows[self->row_count].row = line->row;
            self->rows[self->row_count].edited = 1;
            self->rows[self->row_count].scripted = 0;
            self->rows[self->row_count].suppressed = 0;
            self->row_count++;
        }
    for( int pass = 0; pass < 2; pass++ )
    {
        const struct Editor_SpawnFile* file = pass ? &self->scripted : &self->generated;
        for( int i = 0; i < file->line_count && self->row_count < SPAWNS_ROWS_MAX; i++ )
        {
            const struct Editor_SpawnLine* line = &file->lines[i];
            if( line->kind != EDITOR_SPAWN_LINE_ROW || line->row.suppress )
                continue;
            self->rows[self->row_count].row = line->row;
            self->rows[self->row_count].edited = 0;
            self->rows[self->row_count].scripted = pass;
            self->rows[self->row_count].suppressed = Editor_SpawnIsSuppressed(edited, &line->row);
            self->row_count++;
        }
    }
    spawns_selection_prune(self);
    /* Every visible row's text is stale now. */
    spawns_filter(ui, self);
}

static void
spawns_form_refresh(
    struct EditorUi* ui,
    struct SpawnsPanel* self);

static int
spawns_list_find(
    const struct SpawnsPanel* self,
    const struct Editor_SpawnRow* row);

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
            self->reveal_pending = 1;
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

/** Nobody has a record for the pick: not the catalog, and not this window (a
 *  record made here by "Copy to new" is one it knows the name of at once,
 *  Editor_Catalog_RecordName). What is left is an id whose record is gone. */
static void
spawns_pick_absent(
    struct EditorUi* ui,
    int pick_kind,
    int pick_id)
{
    Editor_UiStatus(ui, "Use pick refused: %s %d has no record in this lane (a Copy to new that was undone?)",
        pick_kind == 1 ? "npc" : "obj", pick_id);
}

/** Spawns this window just added (EditorUi::spawns_added_*, counted by
 *  edp_apply) are shown in the world: the master switch, the edited filter
 *  and the added kinds' filters come on where they were off, and the status
 *  bar says so with the chord that hides them again. Nothing is said when
 *  they were on already. */
static void
spawns_show_added(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    int const npcs = ui->spawns_added_npc;
    int const objs = ui->spawns_added_obj;
    int const boxes[4] = { self->check_preview, self->check_view_edited, npcs ? self->check_view_npcs : -1,
        objs ? self->check_view_objs : -1 };
    char chords[48];
    int turned = 0;

    if( !npcs && !objs )
        return;
    ui->spawns_added_npc = 0;
    ui->spawns_added_obj = 0;
    for( int i = 0; i < 4; i++ )
        if( boxes[i] >= 0 && !ToriRSChrome_Checked(ui->chrome, boxes[i]) )
        {
            ToriRSChrome_SetChecked(ui->chrome, boxes[i], 1);
            turned = 1;
        }
    if( !turned )
        return;
    Editor_HotkeyDisplay(&ui->hotkeys, EDITOR_HK_VIEW_SPAWN_PREVIEW, chords, (int)sizeof(chords));
    if( chords[0] )
        Editor_UiStatus(ui, "\"Show spawns in the world\" is on now, so the spawn just placed shows; %s hides them",
            chords);
    else
        Editor_UiStatus(ui, "\"Show spawns in the world\" is on now, so the spawn just placed shows (Spawns > View)");
}

/** The view toggles and their preference, each way: a preference that moved
 *  (the file landing at boot) sets the boxes, a box that moved sets the
 *  preference and has the host write it. */
static void
spawns_view_sync(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    const struct
    {
        int widget;
        int bit;
    } views[] = {
        { self->check_preview, EDITOR_PREFS_SPAWN_SHOW },
        { self->check_view_npcs, EDITOR_PREFS_SPAWN_NPCS },
        { self->check_view_objs, EDITOR_PREFS_SPAWN_OBJS },
        { self->check_view_player, EDITOR_PREFS_SPAWN_PLAYER },
        { self->check_view_generated, EDITOR_PREFS_SPAWN_GENERATED },
        { self->check_view_edited, EDITOR_PREFS_SPAWN_EDITED },
        { self->check_view_scripted, EDITOR_PREFS_SPAWN_SCRIPTED },
        { self->check_view_suppressed, EDITOR_PREFS_SPAWN_SUPPRESSED },
        { self->check_view_markers, EDITOR_PREFS_SPAWN_MARKERS },
        { self->check_wander, EDITOR_PREFS_SPAWN_WANDER },
    };
    int const count = (int)(sizeof(views) / sizeof(views[0]));
    int boxes = 0;

    if( ui->prefs.spawn_view != self->view_prefs )
    {
        self->view_prefs = ui->prefs.spawn_view;
        for( int i = 0; i < count; i++ )
            ToriRSChrome_SetChecked(ui->chrome, views[i].widget, (self->view_prefs & views[i].bit) != 0);
        return;
    }
    for( int i = 0; i < count; i++ )
        if( ToriRSChrome_Checked(ui->chrome, views[i].widget) )
            boxes |= views[i].bit;
    if( boxes != self->view_prefs )
    {
        self->view_prefs = boxes;
        ui->prefs.spawn_view = boxes;
        ui->prefs_serial++;
    }
}

/**
 * The list's rows brought up to the square's spawn documents: the tick's,
 * while the panel shows, and Delete's / Duplicate's, which run with the panel
 * behind another tab -- a spawn placed with the Place tool from the Catalog
 * is selected, but its row was never built, and Delete said "pick a spawn in
 * the list first" (2026-11-11). 0 when no square is latched.
 */
static int
spawns_rows_fresh(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    struct Editor_SpawnFile* edited;
    char line[TORIRS_CHROME_LABEL_MAX];
    int map_x;
    int map_z;
    uint32_t hash;

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
        return 0;
    }
    if( !self->square_valid || map_x != self->map_x || map_z != self->map_z )
    {
        char key[EDITOR_DOC_KEY_MAX];
        self->map_x = map_x;
        self->map_z = map_z;
        self->square_valid = 1;
        /* The rebuild this forces keeps of the selection what the new
         * square lists (spawns_selection_prune): a spawn just placed there
         * and selected stays selected. */
        self->built_hash = 0;
        Editor_SquareKey(key, map_x, map_z);
        (void)edp_ensure_doc(ui, EDITOR_DOC_SPAWN, key);
    }
    spawns_load_generated(ui, self);
    edited = spawns_edited(ui, map_x, map_z);
    hash = spawns_hash(&self->scripted, spawns_hash(&self->generated, spawns_hash(edited, 2166136261u)));
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
    return 1;
}

static void
spawns_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct SpawnsPanel* self = opaque;
    int first;
    int shown;

    if( self->hint_serial != ui->hotkeys.serial )
        spawns_hints(ui, self);
    /* Before the sync, so the boxes it turns on reach the preference. */
    spawns_show_added(ui, self);
    spawns_view_sync(ui, self);
    spawns_env_step(ui, self);

    if( !ui->chrome->panels[self->panel].visible )
        return;
    if( !spawns_rows_fresh(ui, self) )
        return;
    spawns_form_refresh(ui, self);
    /* The Find box filters as it is typed. */
    {
        const char* find = ToriRSChrome_Text(ui->chrome, self->input_find);
        if( strcmp(self->find, find ? find : "") != 0 )
        {
            snprintf(self->find, sizeof(self->find), "%s", find ? find : "");
            spawns_filter(ui, self);
        }
    }
    if( self->reveal_pending && self->selection_count > 0 )
    {
        int const r = spawns_list_find(self, &self->selection[0]);
        int const at = r >= 0 ? spawns_list_index(self, r) : -1;
        if( at >= 0 )
        {
            self->reveal_pending = 0;
            ToriRSChrome_VListSetSelected(ui->chrome, self->list, at);
            ToriRSChrome_VListSetScroll(ui->chrome, self->list, at > 2 ? at - 2 : 0);
        }
        else if( r >= 0 )
            self->reveal_pending = 0; /* hidden by the Find box */
    }
    if( self->tab_scroll_pending )
    {
        /* Laid out by the Build after the click: a hidden row is still at 0. */
        int const tab = ToriRSChrome_PanelActiveTab(ui->chrome, self->panel);
        int const row = tab == 2 ? self->row_preview : tab == 1 ? self->input_name : self->row_edit;
        if( ui->chrome->widgets[row].y > 0 )
        {
            self->tab_scroll_pending = 0;
            ToriRSChrome_PanelSetScroll(ui->chrome, self->panel, ui->chrome->widgets[row].y
                                                                     - ui->chrome->panels[self->panel].content_y
                                                                     + ui->chrome->panels[self->panel].scroll_y);
        }
    }
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->shown_count; r++ )
    {
        char text[TORIRS_CHROME_INPUT_MAX];
        spawns_row_text(ui, self, &self->rows[self->shown[r]], text, sizeof(text));
        ToriRSChrome_VListSetRow(ui->chrome, self->list, r, text);
    }
}

static int
spawns_selected(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    int const row = spawns_list_picked(ui, self);
    if( row < 0 )
    {
        Editor_UiLog(ui, "editor: pick a spawn in the list first");
        return -1;
    }
    return row;
}

/** Delete (or suppress, for a generated row) the selection as one undo step;
 *  with nothing selected, the list's highlighted row. */
static int
spawns_delete(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    struct Editor_Cmd* commands;
    struct Editor_SpawnFile* edited;
    int rows[SPAWNS_SELECTION_MAX];
    int capacity;
    int picked = 0;
    int count = 0;
    int skipped = 0;
    int applied;

    /* First: it may latch another square, and open its edited document. */
    (void)spawns_rows_fresh(ui, self);
    edited = spawns_edited(ui, self->map_x, self->map_z);
    for( int i = 0; i < self->selection_count; i++ )
    {
        int const r = spawns_list_find(self, &self->selection[i]);
        if( r >= 0 )
            rows[picked++] = r;
    }
    if( picked == 0 )
    {
        int const r = spawns_selected(ui, self);
        if( r < 0 )
            return 0;
        rows[picked++] = r;
    }
    commands = edp_scratch(&capacity);
    for( int i = 0; i < picked && count < capacity; i++ )
    {
        const struct SpawnsListRow* row = &self->rows[rows[i]];
        if( row->suppressed )
        {
            skipped++;
            continue;
        }
        if( !row->edited && !edited )
        {
            Editor_UiLog(ui, "editor: the edited spawn file is not open; suppression refused");
            return 0;
        }
        if( Editor_SpawnDeleteCommand(row->edited ? edited : NULL, &row->row, &commands[count]) )
            count++;
    }
    if( count == 0 )
    {
        if( skipped )
            Editor_UiLog(ui, "editor: already suppressed; Restore brings it back");
        return 0;
    }
    applied = edp_apply(ui, commands, count, commands[0].kind == EDITOR_CMD_SPAWN_SUPPRESS ? "suppress" : "delete spawn");
    if( applied == 0 )
        return 0;
    /* In the bar, as a deleted loc is: Delete after a placement left
     * "Placed npc ..." there (2026-11-11). */
    if( picked == 1 )
        Editor_UiStatus(ui, "%s %s at %d,%d; Ctrl+Z undoes it",
            commands[0].kind == EDITOR_CMD_SPAWN_SUPPRESS ? "Suppressed" : "Deleted", self->rows[rows[0]].row.name,
            self->rows[rows[0]].row.abs_x, self->rows[rows[0]].row.abs_z);
    else
        Editor_UiStatus(ui, "Deleted or suppressed %d spawns in one step; Ctrl+Z undoes it", applied);
    /* A deleted row leaves the selection now, not when the panel next shows. */
    (void)spawns_rows_fresh(ui, self);
    return applied;
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
    char name[EDITOR_CMD_NAME_MAX];
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
    if( !Editor_Catalog_RecordName(ui, kind, ui->tools.pick_id, name, sizeof(name)) )
    {
        spawns_pick_absent(ui, ui->tools.pick_kind, ui->tools.pick_id);
        return;
    }
    ToriRSChrome_SetText(ui->chrome, self->input_name, name);
    if( kind == TORIRSMAPED_CATALOG_OBJ )
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_obj);
    else
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_npc);
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
    int const row = spawns_list_picked(ui, self);
    int at;

    if( row < 0 )
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

    (void)spawns_rows_fresh(ui, self);
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
            self->rows[r].edited ? "edited file"
            : self->rows[r].scripted ? "a quest's or skill's file: an edit suppresses it and adds an edited row"
                                     : "generated: an edit suppresses it and adds an edited row");
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
    char name[EDITOR_CMD_NAME_MAX];
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
    if( Editor_Catalog_RecordName(ui, kind, ui->tools.pick_id, name, sizeof(name)) )
    {
        ToriRSChrome_SetText(ui->chrome, self->input_edit_name, name);
        ToriRSChrome_RadioSelect(ui->chrome, kind == TORIRSMAPED_CATALOG_OBJ ? self->radio_edit_obj : self->radio_edit_npc);
    }
    else
        spawns_pick_absent(ui, ui->tools.pick_kind, ui->tools.pick_id);
}

/* ---- the wander preview ------------------------------------------------------- */

/** The npc's movement statements: the Record panel's (unsaved edits
 *  included) when it holds that npc, else the server's catalog (the merged
 *  record), else nothing stated (the engine defaults stand). Remembered per
 *  name until a fact. */
static void
spawns_npc_movement(
    struct EditorUi* ui,
    struct SpawnsPanel* self,
    const char* name,
    struct Editor_NpcMovement* out)
{
    struct SpawnsWanderEntry* entry;

    if( Editor_Record_NpcMovement(ui, "npc", name, out) )
        return;
    if( ui->editor && ui->editor->last_cmd_seq != self->wander_seq )
    {
        self->wander_seq = ui->editor->last_cmd_seq;
        self->wander_count = 0;
    }
    for( int i = 0; i < self->wander_count; i++ )
        if( strcmp(self->wander[i].name, name) == 0 )
        {
            *out = self->wander[i].movement;
            return;
        }
    if( self->wander_count == SPAWNS_WANDER_CACHE )
        self->wander_count = 0;
    entry = &self->wander[self->wander_count++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    Editor_NpcMovementInit(&entry->movement);
    if( ui->editor && (ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
    {
        struct EditorHost_CatalogRecord record;
        int const by_id = name[0] == '#';
        memset(&record, 0, sizeof(record));
        if( Editor_HostMapEdCatalogRecord(&ui->editor->host, TORIRSMAPED_CATALOG_NPC,
                by_id ? (int32_t)atoi(name + 1) : -1, by_id ? "" : name, &record)
            == EDITOR_HOST_OK )
        {
            /* The merged record lists every statement, the later source
             * over the earlier: feed them in order, the winner lands last. */
            for( int i = 0; i < record.field_count; i++ )
                if( record.fields[i].key && record.fields[i].value )
                    (void)Editor_NpcMovementRead(&entry->movement, record.fields[i].key, record.fields[i].value);
        }
        Editor_HostCatalogRecordFree(&record);
    }
    *out = entry->movement;
}

/** The preview's rectangles for `name` standing at (x, z, level), from its
 *  effective behaviour (Editor_NpcPreviewRects). */
static int
spawns_preview_rects(
    struct EditorUi* ui,
    struct SpawnsPanel* self,
    const char* name,
    int x,
    int z,
    int level,
    struct MapEditorWanderRect* out,
    int max)
{
    struct Editor_NpcMovement movement;

    spawns_npc_movement(ui, self, name, &movement);
    return Editor_NpcPreviewRects(&movement, x, z, level, out, max);
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
        || widget == self->input_name || widget == self->input_edit_name
        || widget == self->input_find
        || widget == self->radio_edit_npc || widget == self->radio_edit_obj || widget == self->input_x
        || widget == self->input_z || widget == self->input_level || widget == self->input_count
        || widget == self->check_suppressed || widget == self->check_wander || widget == self->check_preview
        || widget == self->check_view_npcs || widget == self->check_view_objs || widget == self->check_view_player
        || widget == self->check_view_generated
        || widget == self->check_view_edited || widget == self->check_view_scripted
        || widget == self->check_view_suppressed
        || widget == self->check_view_markers )
        return 1;
    if( widget == self->tabs )
    {
        self->tab_scroll_pending = 1;
        return 1;
    }
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
            (void)Editor_Spawns_EditNpc(ui, &self->selection[0]);
        return 1;
    }
    if( widget == self->button_duplicate )
        (void)spawns_duplicate(ui, self);
    else if( widget == self->button_group )
        (void)Editor_Encounter_GroupSelection(ui);
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
    Editor_SpawnFree(&self->scripted);
    for( int i = 0; i < EDITOR_DOC_MAX_SQUARES; i++ )
    {
        Editor_SpawnFree(&self->preview_squares[i].generated);
        Editor_SpawnFree(&self->preview_squares[i].scripted);
    }
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
    Editor_SpawnInit(&self->scripted);
    self->view_prefs = ui->prefs.spawn_view;
    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8), edp_px(ui, 120),
        edp_px(ui, 260), "Spawns");
    assert(self->panel >= 0);
    self->check_preview = ToriRSChrome_Checkbox(chrome, self->panel, "Show spawns in the world", 0);
    ToriRSChrome_SetTooltip(chrome, self->check_preview, "every spawn of the open squares; click one to select it");
    self->row_square = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find, "a name, display name, tile or file word; every term must match");
    self->list = ToriRSChrome_VList(chrome, self->panel, "", SPAWNS_LIST_ROWS, 0);
    self->button_delete = ToriRSChrome_Button(chrome, self->panel, "Delete / suppress");
    ToriRSChrome_SetTooltip(chrome, self->button_delete,
        "Edited rows go; generated ones are suppressed (-name x z level)");
    self->button_restore = ToriRSChrome_Button(chrome, self->panel, "Restore");
    self->button_duplicate = ToriRSChrome_Button(chrome, self->panel, "Duplicate");
    self->button_group = ToriRSChrome_Button(chrome, self->panel, "Group...");
    ToriRSChrome_SetTooltip(chrome, self->button_group, "the selection into an encounter set");
    ToriRSChrome_Separator(chrome, self->panel);
    {
        static const char* const tabs[] = { "Edit", "Place", "View" };
        self->tabs = ToriRSChrome_Tabs(chrome, self->panel, tabs, 3, 0);
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
    self->button_record = ToriRSChrome_Button(chrome, self->panel, "Edit npc...");
    ToriRSChrome_SetTooltip(chrome, self->button_record, "the NPC editor on this npc; an obj opens its Record");
    ToriRSChrome_PanelBeginTab(chrome, self->panel, 1);
    self->input_name = ToriRSChrome_TextInput(chrome, self->panel, "Name", "");
    self->radio_npc = ToriRSChrome_Radio(chrome, self->panel, "npc", SPAWNS_GROUP_KIND, 1);
    self->radio_obj = ToriRSChrome_Radio(chrome, self->panel, "obj", SPAWNS_GROUP_KIND, 0);
    self->slider_count = ToriRSChrome_Slider(chrome, self->panel, "Count", 1, 1000, 1, 1);
    self->button_pick = ToriRSChrome_Button(chrome, self->panel, "Use catalog pick");
    self->button_place = ToriRSChrome_Button(chrome, self->panel, "Place at latched tile");
    ToriRSChrome_PanelBeginTab(chrome, self->panel, 2);
    self->row_preview = ToriRSChrome_Label(chrome, self->panel, "tick Show spawns in the world");
    self->check_view_npcs = ToriRSChrome_Checkbox(chrome, self->panel, "Npcs", 1);
    self->check_view_objs = ToriRSChrome_Checkbox(chrome, self->panel, "Ground items", 1);
    self->check_view_player = ToriRSChrome_Checkbox(chrome, self->panel, "Active player", 1);
    ToriRSChrome_SetTooltip(chrome, self->check_view_player, "the play account where it stands, in its own gear");
    self->check_view_generated = ToriRSChrome_Checkbox(chrome, self->panel, "Generated rows", 1);
    self->check_view_edited = ToriRSChrome_Checkbox(chrome, self->panel, "Edited rows", 1);
    self->check_view_scripted = ToriRSChrome_Checkbox(chrome, self->panel, "Quest and skill rows", 1);
    ToriRSChrome_SetTooltip(chrome, self->check_view_scripted, "rows from the .spawn files quests and skills keep");
    self->check_view_suppressed = ToriRSChrome_Checkbox(chrome, self->panel, "Suppressed rows", 0);
    ToriRSChrome_SetTooltip(chrome, self->check_view_suppressed, "drawn translucent, with a red marker");
    self->check_view_markers = ToriRSChrome_Checkbox(chrome, self->panel, "Tile markers", 1);
    ToriRSChrome_SetTooltip(chrome, self->check_view_markers,
        "spawn tiles: green edited, yellow generated, red suppressed");
    self->check_wander = ToriRSChrome_Checkbox(chrome, self->panel, "Show wander range", 1);
    self->row_wander = ToriRSChrome_Label(chrome, self->panel, "");
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

/** Shown, or open behind another tab of its group's strip: a Spawns tab
 *  behind another keeps its selection, so Del, Ctrl+D, the S context, the
 *  hint strip and the wander rects still act on it (the menubar's View rows
 *  read "open" the same way). */
static int
spawns_open(
    struct EditorUi* ui,
    const struct SpawnsPanel* self)
{
    return ui->chrome->panels[self->panel].visible || Editor_UiPanelInStrip(ui, "spawns");
}

int
Editor_Spawns_SelectionCount(struct EditorUi* ui)
{
    struct SpawnsPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "spawns");
    if( !self || !spawns_open(ui, self) )
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

static struct SpawnsPreviewSquare*
spawns_preview_square(
    struct EditorUi* ui,
    struct SpawnsPanel* self,
    int map_x,
    int map_z);

static void
spawns_preview_load_square(
    struct EditorUi* ui,
    struct SpawnsPreviewSquare* slot,
    int* open_budget);

int
Editor_Spawns_RowsOf(
    struct EditorUi* ui,
    const char* name,
    int id,
    struct MapEditorSpawnPreviewRow* out,
    int max)
{
    struct SpawnsPanel* self;
    struct Editor_Doc* doc;
    char by_id[16];
    int count = 0;
    int open_budget = EDITOR_DOC_MAX_SQUARES;

    assert(ui);
    assert(name);
    assert(out || max == 0);
    self = Editor_UiPanelSelf(ui, "spawns");
    if( !self || !ui->editor )
        return 0;
    snprintf(by_id, sizeof(by_id), "#%d", id);
    doc = &ui->editor->doc;
    for( int s2 = 0; s2 < doc->square_count && count < max; s2++ )
    {
        struct SpawnsPreviewSquare* slot;
        const struct Editor_SpawnFile* edited;
        if( !doc->squares[s2].loaded )
            continue;
        slot = spawns_preview_square(ui, self, doc->squares[s2].map_x, doc->squares[s2].map_z);
        spawns_preview_load_square(ui, slot, &open_budget);
        edited = spawns_edited(ui, slot->map_x, slot->map_z);
        for( int pass = 0; pass < 3 && count < max; pass++ )
        {
            const struct Editor_SpawnFile* file = pass == 0 ? edited : pass == 1 ? &slot->generated : &slot->scripted;
            if( !file )
                continue;
            for( int i = 0; i < file->line_count && count < max; i++ )
            {
                const struct Editor_SpawnLine* line = &file->lines[i];
                if( line->kind != EDITOR_SPAWN_LINE_ROW || line->row.suppress || line->row.is_obj )
                    continue;
                if( strcmp(line->row.name, name) != 0 && !(id >= 0 && strcmp(line->row.name, by_id) == 0) )
                    continue;
                memset(&out[count], 0, sizeof(out[count]));
                out[count].row = line->row;
                out[count].edited = pass == 0;
                out[count].scripted = pass == 2;
                out[count].suppressed = pass > 0 && Editor_SpawnIsSuppressed(edited, &line->row);
                count++;
            }
        }
    }
    return count;
}

int
Editor_Spawns_DeleteRow(
    struct EditorUi* ui,
    const struct MapEditorSpawnPreviewRow* row)
{
    struct Editor_Cmd command;
    struct Editor_SpawnFile* edited;
    char key[EDITOR_DOC_KEY_MAX];

    assert(ui);
    assert(row);
    if( row->suppressed )
    {
        Editor_UiLog(ui, "editor: %s at %d,%d is already suppressed", row->row.name, row->row.abs_x, row->row.abs_z);
        return 0;
    }
    Editor_SpawnDocKeyAt(key, row->row.abs_x, row->row.abs_z);
    if( !edp_ensure_doc(ui, EDITOR_DOC_SPAWN, key) )
    {
        Editor_UiLog(ui, "editor: could not open the spawn document %s", key);
        return 0;
    }
    edited = spawns_edited(ui, row->row.abs_x / EDITOR_SQUARE_X, row->row.abs_z / EDITOR_SQUARE_Z);
    if( !row->edited && !edited )
    {
        Editor_UiLog(ui, "editor: the edited spawn file is not open; suppression refused");
        return 0;
    }
    if( !Editor_SpawnDeleteCommand(row->edited ? edited : NULL, &row->row, &command) )
        return 0;
    return edp_apply(ui, &command, 1, command.kind == EDITOR_CMD_SPAWN_SUPPRESS ? "suppress" : "delete spawn");
}

int
Editor_Spawns_PlaceRow(
    struct EditorUi* ui,
    const struct Editor_SpawnRow* row)
{
    struct Editor_Cmd command;
    char key[EDITOR_DOC_KEY_MAX];

    assert(ui);
    assert(row);
    if( !Editor_SpawnRowValid(row) )
    {
        Editor_UiLog(ui, "editor: %s is not a spawn row", row->name);
        return 0;
    }
    Editor_SpawnDocKeyAt(key, row->abs_x, row->abs_z);
    if( !edp_ensure_doc(ui, EDITOR_DOC_SPAWN, key) )
    {
        Editor_UiLog(ui, "editor: could not open the spawn document %s", key);
        return 0;
    }
    Editor_SpawnCommand(&command, row, 1);
    return edp_apply(ui, &command, 1, "place spawn");
}

int
Editor_Spawns_EditNpc(
    struct EditorUi* ui,
    const struct Editor_SpawnRow* row)
{
    char name[EDITOR_CMD_NAME_MAX];

    assert(ui);
    assert(row);
    if( row->is_obj || !Editor_UiFindPanel(ui, BUILDER_PANEL_NPC) )
        return Editor_Record_Open(ui, row->is_obj ? TORIRSMAPED_CATALOG_OBJ : TORIRSMAPED_CATALOG_NPC, row->name);
    /* A `#<id>` row: the record's name, from the catalog. */
    snprintf(name, sizeof(name), "%s", row->name);
    if( name[0] == '#' )
    {
        struct EditorHost_CatalogRecord record;
        memset(&record, 0, sizeof(record));
        if( !ui->editor
            || Editor_HostMapEdCatalogRecord(&ui->editor->host, TORIRSMAPED_CATALOG_NPC, (int32_t)atoi(name + 1), "",
                   &record)
                   != EDITOR_HOST_OK
            || !record.name || strlen(record.name) >= sizeof(name) )
        {
            Editor_HostCatalogRecordFree(&record);
            Editor_UiStatus(ui, "Edit npc refused: npc %s has no record name the catalog knows", name);
            return 0;
        }
        snprintf(name, sizeof(name), "%s", record.name);
        Editor_HostCatalogRecordFree(&record);
    }
    if( !Builder_PanelOpenSubject(ui, BUILDER_PANEL_NPC, name) )
    {
        Editor_UiStatus(ui, "Edit npc: the NPC editor could not open %s (see its status); opening its Record", name);
        return Editor_Record_Open(ui, TORIRSMAPED_CATALOG_NPC, name);
    }
    if( Editor_UiPanelInStrip(ui, BUILDER_PANEL_NPC) )
        Editor_UiGroupShow(ui, BUILDER_PANEL_NPC);
    else if( Editor_UiTogglePanel(ui, BUILDER_PANEL_NPC) == 0 )
        (void)Editor_UiTogglePanel(ui, BUILDER_PANEL_NPC);
    return 1;
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
    /* The list follows the selection (D11: it stayed at the top). */
    self->reveal_pending = count > 0;
    return count;
}

int
Editor_Spawns_ClearSelection(struct EditorUi* ui)
{
    struct SpawnsPanel* self;
    int count;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "spawns");
    if( !self )
        return 0;
    count = self->selection_count;
    self->selection_count = 0;
    self->move_armed = 0;
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
        count += spawns_preview_rects(ui, self, name, hover_x, hover_z, hover_level, out + count, max - count);
    }
    if( !spawns_open(ui, self) )
        return count;
    for( int i = 0; i < self->selection_count; i++ )
    {
        const struct Editor_SpawnRow* row = &self->selection[i];
        int const r = spawns_list_find(self, row);
        if( row->is_obj || r < 0 || spawns_row_suppressed(&self->rows[r]) )
            continue;
        count += spawns_preview_rects(ui, self, row->name, row->abs_x, row->abs_z, row->level, out + count,
            max - count);
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
            count += spawns_preview_rects(ui, self, row->name, x, z, hover_level, out + count, max - count);
        }
        if( i == 0 )
        {
            /* The status line says what the server will do, not the raw
             * fields (D10: Hans's wanderrange 5 is dead data). */
            char line[TORIRS_CHROME_LABEL_MAX];
            char sentence[TORIRS_CHROME_LABEL_MAX];
            struct Editor_NpcMovement movement;
            struct Editor_NpcEffective effective;
            spawns_npc_movement(ui, self, row->name, &movement);
            Editor_NpcEffectiveMode(&movement, &effective);
            Editor_NpcEffectiveSentence(&movement, &effective, sentence, sizeof(sentence));
            snprintf(line, sizeof(line), "%s: %s", row->name, sentence);
            ToriRSChrome_SetText(ui->chrome, self->row_wander, line);
        }
    }
    return count;
}

/* ---- the world preview ------------------------------------------------------- */

/** The square's cache slot: its own, else a free one, else one whose square
 *  is no longer open. */
static struct SpawnsPreviewSquare*
spawns_preview_square(
    struct EditorUi* ui,
    struct SpawnsPanel* self,
    int map_x,
    int map_z)
{
    struct SpawnsPreviewSquare* spare = NULL;

    for( int i = 0; i < EDITOR_DOC_MAX_SQUARES; i++ )
    {
        struct SpawnsPreviewSquare* slot = &self->preview_squares[i];
        if( !slot->used )
        {
            if( !spare )
                spare = slot;
            continue;
        }
        if( slot->map_x == map_x && slot->map_z == map_z )
            return slot;
    }
    if( !spare )
        for( int i = 0; i < EDITOR_DOC_MAX_SQUARES && !spare; i++ )
        {
            int open = 0;
            for( int s = 0; s < ui->editor->doc.square_count && !open; s++ )
                open = ui->editor->doc.squares[s].loaded
                       && ui->editor->doc.squares[s].map_x == self->preview_squares[i].map_x
                       && ui->editor->doc.squares[s].map_z == self->preview_squares[i].map_z;
            if( !open )
                spare = &self->preview_squares[i];
        }
    /* At most EDITOR_DOC_MAX_SQUARES are open, and this one is among them. */
    assert(spare);
    Editor_SpawnFree(&spare->generated);
    Editor_SpawnFree(&spare->scripted);
    memset(spare, 0, sizeof(*spare));
    spare->map_x = map_x;
    spare->map_z = map_z;
    spare->used = 1;
    Editor_SpawnInit(&spare->generated);
    Editor_SpawnInit(&spare->scripted);
    return spare;
}

/** Bring a square's two files in (the first time) and its parse up to date.
 *  `*open_budget` is how many first opens this frame may still make: each one
 *  is a request to the server. */
static void
spawns_preview_load_square(
    struct EditorUi* ui,
    struct SpawnsPreviewSquare* slot,
    int* open_budget)
{
    struct Editor_DocHandle handle;
    char key[EDITOR_DOC_KEY_MAX];

    if( !slot->tried )
    {
        if( *open_budget <= 0 )
            return;
        (*open_budget)--;
        slot->tried = 1;
        Editor_SquareKey(key, slot->map_x, slot->map_z);
        (void)edp_ensure_doc(ui, EDITOR_DOC_SPAWN, key);
        Editor_SpawnGeneratedKey(key, slot->map_x, slot->map_z);
        (void)edp_ensure_doc(ui, EDITOR_DOC_TEXT, key);
        Editor_SpawnScriptedKey(key, slot->map_x, slot->map_z);
        (void)edp_ensure_doc(ui, EDITOR_DOC_TEXT, key);
    }
    Editor_SpawnGeneratedKey(key, slot->map_x, slot->map_z);
    if( Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, key, &handle) && handle.text->text
        && handle.text->length != slot->length )
    {
        struct Editor_SpawnParseResult const result =
            Editor_SpawnParse(&slot->generated, handle.text->text, handle.text->length);
        if( !result.ok )
            Editor_UiLog(ui, "editor: %s line %d does not parse", key, result.line);
        slot->length = handle.text->length;
    }
    Editor_SpawnScriptedKey(key, slot->map_x, slot->map_z);
    if( Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, key, &handle) && handle.text->text
        && handle.text->length != slot->scripted_length )
    {
        (void)Editor_SpawnParse(&slot->scripted, handle.text->text, handle.text->length);
        slot->scripted_length = handle.text->length;
    }
}

/** One row into the merged list, unless a filter or the table says no. */
static void
spawns_preview_add(
    struct EditorUi* ui,
    struct SpawnsPanel* self,
    const struct Editor_SpawnRow* row,
    int edited,
    int scripted,
    int suppressed)
{
    struct MapEditorSpawnPreviewRow* out;

    if( !ToriRSChrome_Checked(ui->chrome, row->is_obj ? self->check_view_objs : self->check_view_npcs) )
        return;
    if( !ToriRSChrome_Checked(ui->chrome, suppressed ? self->check_view_suppressed
                                          : edited   ? self->check_view_edited
                                          : scripted ? self->check_view_scripted
                                                     : self->check_view_generated) )
        return;
    /* One stack per obj per tile is all the world holds, so a second row of
     * the same obj there has nothing of its own to stand as. */
    if( row->is_obj )
        for( int i = 0; i < self->preview_count; i++ )
        {
            const struct Editor_SpawnRow* other = &self->preview_rows[i].row;
            if( other->is_obj && other->abs_x == row->abs_x && other->abs_z == row->abs_z && other->level == row->level
                && strcmp(other->name, row->name) == 0 )
                return;
        }
    if( self->preview_count == MAP_EDITOR_SPAWN_PREVIEW_MAX )
    {
        self->preview_clipped = 1;
        return;
    }
    out = &self->preview_rows[self->preview_count++];
    memset(out, 0, sizeof(*out));
    out->row = *row;
    out->edited = edited;
    out->scripted = scripted;
    out->suppressed = suppressed;
}

/** The open squares' rows through the filters, as the panel's list reads the
 *  two files: a suppression row shows only through the generated row it hides. */
static void
spawns_preview_merge(
    struct EditorUi* ui,
    struct SpawnsPanel* self)
{
    struct Editor_Doc* doc = &ui->editor->doc;
    uint32_t hash = 2166136261u;
    int const was_clipped = self->preview_clipped;

    self->preview_count = 0;
    self->preview_clipped = 0;
    for( int s = 0; s < doc->square_count; s++ )
    {
        struct SpawnsPreviewSquare* slot;
        const struct Editor_SpawnFile* edited;

        if( !doc->squares[s].loaded )
            continue;
        slot = spawns_preview_square(ui, self, doc->squares[s].map_x, doc->squares[s].map_z);
        edited = spawns_edited(ui, slot->map_x, slot->map_z);
        if( edited )
            for( int i = 0; i < edited->line_count; i++ )
                if( edited->lines[i].kind == EDITOR_SPAWN_LINE_ROW && !edited->lines[i].row.suppress )
                    spawns_preview_add(ui, self, &edited->lines[i].row, 1, 0, 0);
        for( int pass = 0; pass < 2; pass++ )
        {
            const struct Editor_SpawnFile* file = pass ? &slot->scripted : &slot->generated;
            for( int i = 0; i < file->line_count; i++ )
            {
                const struct Editor_SpawnLine* line = &file->lines[i];
                if( line->kind == EDITOR_SPAWN_LINE_ROW && !line->row.suppress )
                    spawns_preview_add(ui, self, &line->row, 0, pass, Editor_SpawnIsSuppressed(edited, &line->row));
            }
        }
    }
    if( self->preview_clipped && !was_clipped )
        Editor_UiLog(ui, "editor: the spawn preview shows the first %d spawns", MAP_EDITOR_SPAWN_PREVIEW_MAX);
    for( int i = 0; i < self->preview_count; i++ )
        hash = (hash ^ MapEditorSpawnPreview_RowHash(&self->preview_rows[i])) * 16777619u;
    if( hash != self->preview_hash )
    {
        self->preview_hash = hash;
        self->preview_serial++;
    }
}

const struct MapEditorSpawnPreviewRow*
Editor_Spawns_PreviewRows(
    struct EditorUi* ui,
    int* out_count,
    uint32_t* out_serial)
{
    struct SpawnsPanel* self;
    uint32_t signature = 2166136261u;
    int open_budget = 1;

    assert(ui);
    assert(out_count);
    assert(out_serial);
    *out_count = 0;
    *out_serial = 0;
    self = Editor_UiPanelSelf(ui, "spawns");
    if( !self || !ui->editor )
        return NULL;
    /* TORIRS_EDITOR_SPAWN_PREVIEW=1: a headless photograph has no pointer to
     * tick the box with. Read once per process. */
    {
        static int resolved;
        if( !resolved )
        {
            resolved = 1;
            if( getenv("TORIRS_EDITOR_SPAWN_PREVIEW") )
                ToriRSChrome_SetChecked(ui->chrome, self->check_preview, 1);
        }
    }
    if( !ToriRSChrome_Checked(ui->chrome, self->check_preview) )
    {
        if( self->preview_count )
        {
            self->preview_count = 0;
            self->preview_hash = 0;
            self->preview_signature = 0;
            self->preview_serial++;
        }
        *out_serial = self->preview_serial;
        return self->preview_rows;
    }

    {
        int const filters[6] = {
            self->check_view_npcs,   self->check_view_objs,     self->check_view_generated,
            self->check_view_edited, self->check_view_scripted, self->check_view_suppressed,
        };
        for( int i = 0; i < 6; i++ )
            signature = (signature ^ (uint32_t)ToriRSChrome_Checked(ui->chrome, filters[i])) * 16777619u;
    }
    signature = (signature ^ ui->editor->last_cmd_seq) * 16777619u;
    for( int s = 0; s < ui->editor->doc.square_count; s++ )
    {
        struct Editor_Square const* square = &ui->editor->doc.squares[s];
        struct SpawnsPreviewSquare* slot;
        const struct Editor_SpawnFile* edited;

        if( !square->loaded )
            continue;
        slot = spawns_preview_square(ui, self, square->map_x, square->map_z);
        spawns_preview_load_square(ui, slot, &open_budget);
        edited = spawns_edited(ui, slot->map_x, slot->map_z);
        signature = (signature ^ (uint32_t)(slot->map_x * 4099 + slot->map_z)) * 16777619u;
        signature = (signature ^ (uint32_t)slot->length) * 16777619u;
        signature = (signature ^ (uint32_t)slot->scripted_length) * 16777619u;
        signature = (signature ^ (uint32_t)(edited ? edited->line_count + 1 : 0)) * 16777619u;
    }
    if( signature != self->preview_signature )
    {
        self->preview_signature = signature;
        spawns_preview_merge(ui, self);
    }
    *out_count = self->preview_count;
    *out_serial = self->preview_serial;
    return self->preview_rows;
}

int
Editor_Spawns_PreviewResolve(
    struct EditorUi* ui,
    const struct Editor_SpawnRow* row)
{
    struct EditorHost_CatalogRecord record;
    struct SpawnsPanel* self;
    uint32_t const kind = row->is_obj ? TORIRSMAPED_CATALOG_OBJ : TORIRSMAPED_CATALOG_NPC;
    int id = MAP_EDITOR_SPAWN_PREVIEW_ID_MISSING;

    assert(ui);
    assert(row);
    if( row->name[0] == '#' )
        return atoi(row->name + 1);
    self = Editor_UiPanelSelf(ui, "spawns");
    assert(self);
    for( int i = 0; i < self->preview_name_count; i++ )
        if( self->preview_names[i].is_obj == row->is_obj && strcmp(self->preview_names[i].name, row->name) == 0 )
            return self->preview_names[i].id;
    if( !ui->editor || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return id;
    /* A miss is not remembered here: the caller's item holds the answer for
     * as long as its row stands, and a record added later may yet exist. */
    memset(&record, 0, sizeof(record));
    {
        enum EditorHost_Status const status =
            Editor_HostMapEdCatalogRecord(&ui->editor->host, kind, -1, row->name, &record);
        if( status == EDITOR_HOST_OK && record.id >= 0 )
        {
            id = record.id;
            if( self->preview_name_count < SPAWNS_PREVIEW_NAMES )
            {
                struct SpawnsPreviewName* name = &self->preview_names[self->preview_name_count++];
                snprintf(name->name, sizeof(name->name), "%s", row->name);
                name->is_obj = row->is_obj;
                name->id = id;
            }
        }
        else if( status == EDITOR_HOST_BUSY )
        {
            /* The index is still building (the first seconds after boot,
             * when a saved "Show spawns in the world" asks at once): a miss
             * on a partial index is no answer. The caller asks again, instead
             * of holding every spawn of the square as "not found" until its
             * rows change. */
            id = MAP_EDITOR_SPAWN_PREVIEW_ID_UNKNOWN;
        }
    }
    Editor_HostCatalogRecordFree(&record);
    return id;
}

void
Editor_Spawns_PreviewCounts(
    struct EditorUi* ui,
    int landed,
    int waiting,
    int missing)
{
    struct SpawnsPanel* self;
    char line[TORIRS_CHROME_LABEL_MAX];

    assert(ui);
    self = Editor_UiPanelSelf(ui, "spawns");
    assert(self);
    if( self->preview_counts_valid && self->preview_landed == landed && self->preview_waiting == waiting
        && self->preview_missing == missing )
        return;
    self->preview_counts_valid = 1;
    self->preview_landed = landed;
    self->preview_waiting = waiting;
    self->preview_missing = missing;
    snprintf(line, sizeof(line), "%d shown, %d loading, %d not found", landed, waiting, missing);
    ToriRSChrome_SetText(ui->chrome, self->row_preview, line);
}

int
Editor_Spawns_PreviewMarkers(struct EditorUi* ui)
{
    struct SpawnsPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "spawns");
    return self && ToriRSChrome_Checked(ui->chrome, self->check_preview)
           && ToriRSChrome_Checked(ui->chrome, self->check_view_markers);
}

int
Editor_Spawns_PreviewPlayer(struct EditorUi* ui)
{
    struct SpawnsPanel* self;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "spawns");
    return self && ToriRSChrome_Checked(ui->chrome, self->check_preview)
           && ToriRSChrome_Checked(ui->chrome, self->check_view_player);
}

int
Editor_Spawns_TogglePreview(struct EditorUi* ui)
{
    struct SpawnsPanel* self;
    int on;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "spawns");
    if( !self )
        return 0;
    on = !ToriRSChrome_Checked(ui->chrome, self->check_preview);
    ToriRSChrome_SetChecked(ui->chrome, self->check_preview, on);
    return on;
}

int
Editor_Spawns_SelectFromWorld(
    struct EditorUi* ui,
    const struct Editor_SpawnRow* row,
    enum Editor_LocSelectMode mode)
{
    struct SpawnsPanel* self;
    int const map_x = row->abs_x / EDITOR_SQUARE_X;
    int const map_z = row->abs_z / EDITOR_SQUARE_Z;
    int same_square;
    int at;
    int replaced;

    assert(ui);
    assert(row);
    self = Editor_UiPanelSelf(ui, "spawns");
    assert(self);

    same_square = self->square_valid && self->map_x == map_x && self->map_z == map_z;
    /* The list is one square's, so a selection is too. */
    replaced = mode == EDITOR_LOC_SELECT_REPLACE || !same_square;
    self->move_armed = 0;
    if( !same_square )
    {
        char key[EDITOR_DOC_KEY_MAX];
        if( mode != EDITOR_LOC_SELECT_REPLACE && self->selection_count > 0 )
            Editor_UiLog(ui, "editor: a spawn selection is one square's; m%d_%d replaces it", map_x, map_z);
        self->map_x = map_x;
        self->map_z = map_z;
        self->square_valid = 1;
        self->built_hash = 0;
        Editor_SquareKey(key, map_x, map_z);
        (void)edp_ensure_doc(ui, EDITOR_DOC_SPAWN, key);
    }
    if( replaced )
    {
        self->selection[0] = *row;
        self->selection_count = 1;
    }
    else
    {
        at = spawns_selection_find(self, row);
        if( at >= 0 && mode != EDITOR_LOC_SELECT_ADD )
        {
            self->selection[at] = self->selection[self->selection_count - 1];
            self->selection_count--;
        }
        else if( at < 0 && mode != EDITOR_LOC_SELECT_SUBTRACT )
        {
            if( self->selection_count < SPAWNS_SELECTION_MAX )
                self->selection[self->selection_count++] = *row;
            else
                Editor_UiLog(ui, "editor: a spawn selection holds %d rows", SPAWNS_SELECTION_MAX);
        }
    }
    self->form_valid = 0;
    self->reveal_pending = 1;

    /* The panel is where a selection is read and edited: bring it up. */
    if( Editor_UiPanelInStrip(ui, "spawns") )
        Editor_UiGroupShow(ui, "spawns");
    else if( !ui->chrome->panels[self->panel].visible )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    return replaced;
}
