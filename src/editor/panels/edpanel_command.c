/*
 * The command palette (module "command", View > Command palette, Ctrl+P;
 * Go to tile... Ctrl+Shift+G opens it prefilled): one box, one list, every
 * reachable thing in the editor as a row (the Unity / VS Code palette).
 *
 * What the box searches, and what Enter on a row does:
 *
 *   actions   every edge / repeat action of the hotkey table (its name, group
 *             and id; HELD flight keys and the palette's own toggle left out),
 *             the chord shown after a tab; runs Editor_HotkeyRun, and hands
 *             the App-bound ones (the camera, the eyedroppers, Paste at the
 *             hovered tile) to the App through Editor_Command_TakePendingAction;
 *   panels    "Open <panel>" for every View-menu panel: shows it;
 *   squares   "m50_50", "50 50", "50,50" through the picker's own parser
 *             (Editor_PickerSearch over the atlas it already holds): Open as
 *             the picker's Open does (the save guard first); a cell with no
 *             square offers New square there; a square NAME typed ("m50")
 *             lists the atlas's squares that start with it;
 *   tiles     an absolute tile "3222,3218" (or "3222 3218 1"): the camera
 *             over it when its square is in the scene, else the square is
 *             opened with that tile as the camera's landing (ui->open_goto_*);
 *   records   "loc:oak", "npc:hans", "obj:knife": the Catalog's own find --
 *             Editor_Catalog_Find sets its Kind and Find box and drives its
 *             model, which asks the server ONCE per settled text, never per
 *             keystroke -- and the held page's rows are the hits; Enter is
 *             Editor_Catalog_PickRecord (the pick, Place armed for a loc);
 *   recent    the squares opened most recently (ui->recent), first when the
 *             box is empty.
 *
 * Nothing here scans the content tree: the atlas is the picker's, the
 * records are a server index query the catalog already makes, the actions
 * and panels are tables in memory. Typing stays at frame rate.
 *
 * Keys: the box has the keyboard (the hotkey gates are shut while it does),
 * so Enter is the box's own activation; Up / Down and Escape come from the
 * App each frame through Editor_Command_Feed, exactly as the Hotkeys panel's
 * capture is fed. A row click runs that row. The panel is a chrome MODAL:
 * centred, a veil behind it, pointer and keyboard its own until it closes,
 * and it closes after every run.
 */

#include "edpanel_catalog_model.h"
#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "edpanel_picker.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/** Rows the list shows at once. */
#define CMD_ROWS 12
/** Hits gathered before the cut to CMD_ROWS. */
#define CMD_HITS_MAX 96
#define CMD_NAME_MAX 64
#define CMD_CHOSEN_RGB 0xFFFF00u
#define CMD_WIDTH 440

enum CmdHitKind
{
    CMD_HIT_ACTION = 0,
    CMD_HIT_PANEL,
    CMD_HIT_SQUARE,
    CMD_HIT_NEW_SQUARE,
    CMD_HIT_TILE,
    CMD_HIT_RECORD,
    CMD_HIT_RECENT,
};

struct CmdHit
{
    enum CmdHitKind kind;
    /** Higher sorts first: the match quality, then the kind's own weight. */
    int score;
    /** Table order, the stable tie-break. */
    int order;
    int action;
    int panel_index;
    int map_x;
    int map_z;
    int storage_index;
    char storage[EDITOR_DOC_STORAGE_MAX];
    int tile_x;
    int tile_z;
    int level;
    uint32_t record_kind;
    char name[CMD_NAME_MAX];
    char text[TORIRS_CHROME_LABEL_MAX];
};

struct CommandPanel
{
    int panel;
    int input;
    int label_status;
    int rows[CMD_ROWS];
    struct CmdHit hits[CMD_HITS_MAX];
    int hit_count;
    int chosen;
    /** What the rows were built from. */
    char shown_query[TORIRS_CHROME_INPUT_MAX];
    uint32_t shown_hk_serial;
    uint32_t shown_recent_serial;
    int shown_catalog_queries;
    int shown_chosen;
    int built;
    /** The palette is open (the panel visible, the box focused). */
    int open;
    /** An App-bound action a run asked for; -1 none. */
    int pending_action;
    /** Record mode this build: the kind asked of the catalog, and whether
     *  the catalog's page for the text has landed. */
    int record_mode;
    int record_waiting;
};

static struct CommandPanel*
command_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "command");
}

/* ---- matching -------------------------------------------------------------------- */

static int
cmd_lower(int c)
{
    return tolower((unsigned char)c);
}

/**
 * How well `needle` (lower-cased by the caller, non-empty) matches `hay`:
 * 0 not at all, 1 as a subsequence (fuzzy: "ssq" in "Save square"), 2 as a
 * substring, 3 at the start of a word, 4 at the start of the text.
 */
static int
cmd_match(
    const char* hay,
    const char* needle)
{
    size_t const n = strlen(needle);
    int best = 0;
    const char* h;
    const char* k;

    for( h = hay; *h; h++ )
    {
        size_t i = 0;
        while( i < n && h[i] && cmd_lower(h[i]) == (unsigned char)needle[i] )
            i++;
        if( i != n )
            continue;
        if( h == hay )
            return 4;
        if( h[-1] == ' ' || h[-1] == '.' || h[-1] == '_' || h[-1] == '/' || h[-1] == '-' )
            best = best < 3 ? 3 : best;
        else
            best = best < 2 ? 2 : best;
    }
    if( best )
        return best;
    k = needle;
    for( h = hay; *h && *k; h++ )
        if( cmd_lower(*h) == (unsigned char)*k )
            k++;
    return *k ? 0 : 1;
}

static void
cmd_lowercase(
    const char* text,
    char* out,
    int out_size)
{
    int i = 0;
    while( text[i] && i < out_size - 1 )
    {
        out[i] = (char)cmd_lower(text[i]);
        i++;
    }
    out[i] = '\0';
}

static char*
cmd_trim(char* text)
{
    char* end;
    while( *text == ' ' || *text == '\t' )
        text++;
    end = text + strlen(text);
    while( end > text && (end[-1] == ' ' || end[-1] == '\t') )
        *--end = '\0';
    return text;
}

static struct CmdHit*
cmd_hit_add(
    struct CommandPanel* self,
    enum CmdHitKind kind,
    int score)
{
    struct CmdHit* hit;
    if( self->hit_count >= CMD_HITS_MAX )
        return NULL;
    hit = &self->hits[self->hit_count];
    memset(hit, 0, sizeof(*hit));
    hit->kind = kind;
    hit->score = score;
    hit->order = self->hit_count;
    hit->action = -1;
    hit->panel_index = -1;
    hit->storage_index = -1;
    hit->level = 0;
    self->hit_count++;
    return hit;
}

static int
cmd_hit_compare(
    const void* a,
    const void* b)
{
    const struct CmdHit* x = a;
    const struct CmdHit* y = b;
    if( x->score != y->score )
        return y->score - x->score;
    return x->order - y->order;
}

/* ---- the sources ------------------------------------------------------------------ */

/** A square's storage as the picker names it ("" for maps/). */
static void
cmd_square_storage(
    const struct Editor_Picker* picker,
    int square,
    struct CmdHit* hit)
{
    const char* storage = picker->storages[picker->squares[square].storage];
    hit->storage_index = picker->squares[square].storage;
    snprintf(hit->storage, sizeof(hit->storage), "%s", strcmp(storage, "maps") == 0 ? "" : storage);
}

static void
cmd_hits_actions(
    struct EditorUi* ui,
    struct CommandPanel* self,
    const char* needle)
{
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(a);
        char chords[48];
        struct CmdHit* hit;
        int score;
        if( info->kind == EDITOR_HK_HELD || a == EDITOR_HK_VIEW_COMMAND )
            continue;
        score = cmd_match(info->name, needle);
        if( score < cmd_match(info->group, needle) )
            score = cmd_match(info->group, needle);
        if( score < cmd_match(info->id, needle) )
            score = cmd_match(info->id, needle);
        if( !score )
            continue;
        hit = cmd_hit_add(self, CMD_HIT_ACTION, score * 10 + 4);
        if( !hit )
            return;
        hit->action = a;
        Editor_HotkeyDisplay(&ui->hotkeys, a, chords, (int)sizeof(chords));
        if( chords[0] )
            snprintf(hit->text, sizeof(hit->text), "%s: %s\t%s", info->group, info->name, chords);
        else
            snprintf(hit->text, sizeof(hit->text), "%s: %s", info->group, info->name);
    }
}

static void
cmd_hits_panels(
    struct EditorUi* ui,
    struct CommandPanel* self,
    const char* needle)
{
    for( int i = 0; i < ui->panel_count; i++ )
    {
        const struct EditorUiPanel* panel = &ui->panels[i];
        char label[TORIRS_CHROME_LABEL_MAX];
        struct CmdHit* hit;
        int score;
        if( !panel->title || panel->chrome_panel < 0 )
            continue;
        snprintf(label, sizeof(label), "Open %s", panel->title);
        score = cmd_match(panel->title, needle);
        if( score < cmd_match(label, needle) )
            score = cmd_match(label, needle);
        if( !score )
            continue;
        hit = cmd_hit_add(self, CMD_HIT_PANEL, score * 10 + 3);
        if( !hit )
            return;
        hit->panel_index = i;
        if( ui->chrome->panels[panel->chrome_panel].visible )
            snprintf(hit->text, sizeof(hit->text), "%s\topen", label);
        else
            snprintf(hit->text, sizeof(hit->text), "%s", label);
    }
}

/** The recent squares, every one when `needle` is empty. */
static void
cmd_hits_recent(
    struct EditorUi* ui,
    struct CommandPanel* self,
    const char* needle)
{
    const struct Editor_Picker* picker = needle[0] || ui->recent.count ? edp_squares_picker(ui) : NULL;

    for( int i = 0; i < ui->recent.count; i++ )
    {
        struct CmdHit* hit;
        int score = needle[0] ? cmd_match(ui->recent.keys[i], needle) : 2;
        int mx;
        int mz;
        char storage[EDITOR_DOC_STORAGE_MAX];
        if( !score || !Editor_RecentEntry(&ui->recent, i, &mx, &mz, storage, (int)sizeof(storage)) )
            continue;
        hit = cmd_hit_add(self, CMD_HIT_RECENT, score * 10 + 6 - (i > 5 ? 1 : 0));
        if( !hit )
            return;
        hit->map_x = mx;
        hit->map_z = mz;
        snprintf(hit->storage, sizeof(hit->storage), "%s", storage);
        hit->storage_index = picker ? Editor_PickerStorageIndex(picker, storage[0] ? storage : "maps") : -1;
        snprintf(hit->text, sizeof(hit->text), "Open %s\trecent", ui->recent.keys[i]);
    }
}

/**
 * Coordinates with no atlas to check them against (the server's square
 * index is still building in the first seconds of a boot): "m50_50",
 * "50 50", "50,50" are a square, "3222,3218" / "3222 3218 1" a tile (both
 * numbers under 256 read as a square, as the picker reads them). The open
 * says so in the console when the square is not there.
 */
static void
cmd_hits_coords_blind(
    struct CommandPanel* self,
    const char* query)
{
    int a;
    int b;
    int level = 0;
    int n = 0;
    struct CmdHit* hit;

    if( sscanf(query, "m%d_%d%n", &a, &b, &n) == 2 && query[n] == '\0' )
    {
        hit = cmd_hit_add(self, CMD_HIT_SQUARE, 89);
        if( hit )
        {
            hit->map_x = a;
            hit->map_z = b;
            snprintf(hit->text, sizeof(hit->text), "Open m%d_%d\tsquare (atlas still building)", a, b);
        }
        return;
    }
    if( sscanf(query, "%d%*[ ,]%d%n", &a, &b, &n) < 2 || a < 0 || b < 0 )
        return;
    if( query[n] != '\0' )
    {
        int m = 0;
        if( sscanf(query + n, "%*[ ,]%d%n", &level, &m) < 1 || query[n + m] != '\0' || level < 0 || level > 3 )
            return;
    }
    if( a < 256 && b < 256 )
    {
        hit = cmd_hit_add(self, CMD_HIT_SQUARE, 89);
        if( hit )
        {
            hit->map_x = a;
            hit->map_z = b;
            snprintf(hit->text, sizeof(hit->text), "Open m%d_%d\tsquare (atlas still building)", a, b);
        }
        return;
    }
    hit = cmd_hit_add(self, CMD_HIT_TILE, 90);
    if( hit )
    {
        hit->tile_x = a;
        hit->tile_z = b;
        hit->level = level;
        hit->map_x = a / 64;
        hit->map_z = b / 64;
        snprintf(hit->text, sizeof(hit->text), "Go to tile %d,%d L%d\tm%d_%d", a, b, level, a / 64, b / 64);
    }
}

/** Coordinates and square names, through the picker's parser and atlas. */
static void
cmd_hits_squares(
    struct EditorUi* ui,
    struct CommandPanel* self,
    const char* query,
    const char* needle)
{
    const struct Editor_Picker* picker = edp_squares_picker(ui);
    struct Editor_PickerSearch found;
    int listed = 0;

    if( !picker )
    {
        cmd_hits_coords_blind(self, query);
        return;
    }
    memset(&found, 0, sizeof(found));
    if( Editor_PickerSearch(picker, query, &found) )
    {
        struct CmdHit* hit = NULL;
        if( found.kind == EDITOR_PICKER_SEARCH_TILE )
        {
            hit = cmd_hit_add(self, CMD_HIT_TILE, 90);
            if( hit )
            {
                hit->tile_x = found.tile_x;
                hit->tile_z = found.tile_z;
                hit->level = found.level;
                hit->map_x = found.map_x;
                hit->map_z = found.map_z;
                if( found.square >= 0 )
                    cmd_square_storage(picker, found.square, hit);
                snprintf(hit->text, sizeof(hit->text), "Go to tile %d,%d L%d\tm%d_%d%s", found.tile_x, found.tile_z,
                    found.level, found.map_x, found.map_z, found.square >= 0 ? "" : " (no square)");
            }
        }
        else if( found.kind == EDITOR_PICKER_SEARCH_SQUARE && found.square >= 0 )
        {
            hit = cmd_hit_add(self, CMD_HIT_SQUARE, 89);
            if( hit )
            {
                hit->map_x = found.map_x;
                hit->map_z = found.map_z;
                cmd_square_storage(picker, found.square, hit);
                snprintf(hit->text, sizeof(hit->text), "Open m%d_%d%s%s\tsquare", found.map_x, found.map_z,
                    hit->storage[0] ? " in " : "", hit->storage);
            }
        }
        else if( found.kind == EDITOR_PICKER_SEARCH_SQUARE )
        {
            hit = cmd_hit_add(self, CMD_HIT_NEW_SQUARE, 88);
            if( hit )
            {
                hit->map_x = found.map_x;
                hit->map_z = found.map_z;
                snprintf(hit->text, sizeof(hit->text), "New square at m%d_%d...\tno square there", found.map_x,
                    found.map_z);
            }
        }
    }
    /* A square name typed in part ("m50", "m50_5"): the atlas's squares that
     * start with it. Only a name shape, or "s" alone would list the world. */
    if( needle[0] == 'm' && isdigit((unsigned char)needle[1]) )
    {
        for( int i = 0; i < picker->square_count && listed < CMD_ROWS; i++ )
        {
            char name[32];
            struct CmdHit* hit;
            snprintf(name, sizeof(name), "m%d_%d", picker->squares[i].map_x, picker->squares[i].map_z);
            if( strncmp(name, needle, strlen(needle)) != 0 || strcmp(name, needle) == 0 )
                continue;
            hit = cmd_hit_add(self, CMD_HIT_SQUARE, 30);
            if( !hit )
                return;
            hit->map_x = picker->squares[i].map_x;
            hit->map_z = picker->squares[i].map_z;
            cmd_square_storage(picker, i, hit);
            snprintf(hit->text, sizeof(hit->text), "Open %s%s%s\tsquare", name, hit->storage[0] ? " in " : "",
                hit->storage);
            listed++;
        }
    }
}

/** "loc:oak" and friends: the catalog's held page for the text. Returns 1
 *  when the query was a record query (whatever it found). */
static int
cmd_hits_records(
    struct EditorUi* ui,
    struct CommandPanel* self,
    const char* query)
{
    static const char* const k_prefix[3] = { "loc:", "npc:", "obj:" };
    const struct Editor_CatalogModel* model;
    uint32_t kind = TORIRSMAPED_CATALOG_ANY;
    char text[EDITOR_CATALOG_FILTER_MAX];
    char* body;

    for( int k = 0; k < 3; k++ )
        if( strncasecmp(query, k_prefix[k], 4) == 0 )
            kind = (uint32_t)k;
    if( kind == TORIRSMAPED_CATALOG_ANY )
        return 0;
    self->record_mode = (int)kind + 1;
    snprintf(text, sizeof(text), "%s", query + 4);
    body = cmd_trim(text);
    model = Editor_Catalog_Find(ui, kind, body);
    if( !model )
    {
        struct CmdHit* hit = cmd_hit_add(self, CMD_HIT_RECORD, 1);
        if( hit )
            snprintf(hit->text, sizeof(hit->text), "(this window has no catalog)");
        return 1;
    }
    self->shown_catalog_queries = model->queries;
    if( !model->held || model->held_kind != kind || strcmp(model->held_filter, body) != 0 )
    {
        self->record_waiting = 1;
        return 1;
    }
    for( int r = 0; r < Editor_CatalogModelRowCount(model) && r < CMD_ROWS; r++ )
    {
        const struct EditorHost_CatalogRow* row = Editor_CatalogModelRow(model, r);
        struct CmdHit* hit;
        char line[TORIRS_CHROME_LABEL_MAX];
        if( row->id < 0 )
            continue;
        hit = cmd_hit_add(self, CMD_HIT_RECORD, 50 - r);
        if( !hit )
            return 1;
        hit->record_kind = kind;
        snprintf(hit->name, sizeof(hit->name), "%s", row->name);
        Editor_CatalogModelRowText(model, r, line, (int)sizeof(line));
        snprintf(hit->text, sizeof(hit->text), "Pick %s %s", ToriRSMapEdCatalog_KindName(kind), line);
    }
    return 1;
}

/* ---- the rows ----------------------------------------------------------------- */

static void
cmd_rebuild(
    struct EditorUi* ui,
    struct CommandPanel* self,
    const char* query_text)
{
    char query[TORIRS_CHROME_INPUT_MAX];
    char needle[TORIRS_CHROME_INPUT_MAX];
    char* body;

    self->hit_count = 0;
    self->record_mode = 0;
    self->record_waiting = 0;
    snprintf(query, sizeof(query), "%s", query_text);
    body = cmd_trim(query);
    cmd_lowercase(body, needle, (int)sizeof(needle));
    if( !cmd_hits_records(ui, self, body) )
    {
        cmd_hits_recent(ui, self, needle);
        if( needle[0] )
        {
            cmd_hits_squares(ui, self, body, needle);
            cmd_hits_actions(ui, self, needle);
            cmd_hits_panels(ui, self, needle);
        }
    }
    qsort(self->hits, (size_t)self->hit_count, sizeof(self->hits[0]), cmd_hit_compare);
    if( self->hit_count > CMD_ROWS )
        self->hit_count = CMD_ROWS;
    if( self->chosen >= self->hit_count )
        self->chosen = self->hit_count - 1;
    if( self->chosen < 0 )
        self->chosen = 0;
}

static void
cmd_show(
    struct EditorUi* ui,
    struct CommandPanel* self,
    const char* query)
{
    char status[TORIRS_CHROME_LABEL_MAX];

    for( int r = 0; r < CMD_ROWS; r++ )
    {
        int const shown = r < self->hit_count;
        ToriRSChrome_SetHidden(ui->chrome, self->rows[r], !shown);
        if( !shown )
            continue;
        ToriRSChrome_SetLabel(ui->chrome, self->rows[r], self->hits[r].text);
        ToriRSChrome_SetColor(ui->chrome, self->rows[r], r == self->chosen ? CMD_CHOSEN_RGB : 0);
    }
    if( self->record_waiting )
        snprintf(status, sizeof(status), "asking the catalog...");
    else if( !query[0] )
        snprintf(status, sizeof(status), "an action, Open <panel>, m50_50, 3222,3218, loc:name  (Esc closes)");
    else if( self->hit_count == 0 )
        snprintf(status, sizeof(status), "nothing matches '%s'", query);
    else
        snprintf(status, sizeof(status), "%d hit%s: Enter runs the yellow one, Up / Down choose, Esc closes",
            self->hit_count, self->hit_count == 1 ? "" : "s");
    ToriRSChrome_SetText(ui->chrome, self->label_status, status);
}

static void
cmd_refresh(
    struct EditorUi* ui,
    struct CommandPanel* self,
    int force)
{
    const char* query = ToriRSChrome_Text(ui->chrome, self->input);
    const struct Editor_CatalogModel* model;
    int catalog_moved = 0;

    if( !query )
        query = "";
    if( self->record_mode )
    {
        model = Editor_Catalog_Model(ui);
        catalog_moved = model && model->queries != self->shown_catalog_queries;
    }
    if( self->built && !force && strcmp(self->shown_query, query) == 0 && self->shown_hk_serial == ui->hotkeys.serial
        && self->shown_recent_serial == ui->recent.serial && !catalog_moved && !self->record_waiting
        && self->shown_chosen == self->chosen )
        return;
    if( strcmp(self->shown_query, query) != 0 )
        self->chosen = 0;
    cmd_rebuild(ui, self, query);
    cmd_show(ui, self, query);
    snprintf(self->shown_query, sizeof(self->shown_query), "%s", query);
    self->shown_hk_serial = ui->hotkeys.serial;
    self->shown_recent_serial = ui->recent.serial;
    self->shown_chosen = self->chosen;
    self->built = 1;
}

/* ---- running a hit ------------------------------------------------------------- */

static void
cmd_open_square(
    struct EditorUi* ui,
    const struct CmdHit* hit)
{
    struct EditorUiGuard guard;

    memset(&guard, 0, sizeof(guard));
    guard.action = EDITOR_UI_GUARD_OPEN_SQUARE;
    guard.map_x = hit->map_x;
    guard.map_z = hit->map_z;
    guard.storage_index = hit->storage_index;
    snprintf(guard.storage, sizeof(guard.storage), "%s", hit->storage);
    (void)Editor_UiGuarded(ui, &guard);
}

static int
cmd_run_hit(
    struct EditorUi* ui,
    struct CommandPanel* self,
    const struct CmdHit* hit)
{
    switch( hit->kind )
    {
    case CMD_HIT_ACTION:
        Editor_UiLog(ui, "editor: palette: %s", Editor_HotkeyInfo(hit->action)->name);
        if( !Editor_HotkeyRun(ui, (enum Editor_HotkeyAction)hit->action) )
            self->pending_action = hit->action;
        return 1;
    case CMD_HIT_PANEL:
    {
        const struct EditorUiPanel* panel = &ui->panels[hit->panel_index];
        if( ui->chrome->panels[panel->chrome_panel].visible )
            Editor_UiLog(ui, "editor: palette: %s is already open", panel->title);
        else
            (void)Editor_UiTogglePanel(ui, panel->name);
        return 1;
    }
    case CMD_HIT_SQUARE:
    case CMD_HIT_RECENT:
        cmd_open_square(ui, hit);
        return 1;
    case CMD_HIT_NEW_SQUARE:
        Editor_NewSquare_Open(ui, EDITOR_NEW_SQUARE_NEW, NULL, hit->map_x, hit->map_z);
        return 1;
    case CMD_HIT_TILE:
        /* In the scene already: the camera goes now. Else the square is
         * opened with the tile as where the camera lands. */
        if( ui->app_services.camera_to_tile
            && ui->app_services.camera_to_tile(ui->app_services.user_data, hit->tile_x, hit->tile_z, hit->level) )
        {
            Editor_UiLog(ui, "editor: camera on %d,%d L%d", hit->tile_x, hit->tile_z, hit->level);
            if( ui->app_services.select_terrain )
                ui->app_services.select_terrain(ui->app_services.user_data, hit->tile_x, hit->tile_z, hit->level);
            return 1;
        }
        if( hit->storage_index < 0 && !hit->storage[0] )
        {
            const struct Editor_Picker* picker = edp_squares_picker(ui);
            if( picker && Editor_PickerFind(picker, hit->map_x, hit->map_z, -1) < 0 )
            {
                Editor_UiLog(ui, "editor: no square at m%d_%d holds tile %d,%d", hit->map_x, hit->map_z,
                    hit->tile_x, hit->tile_z);
                return 0;
            }
        }
        ui->open_goto_valid = 1;
        ui->open_goto_x = hit->tile_x;
        ui->open_goto_z = hit->tile_z;
        ui->open_goto_level = hit->level;
        cmd_open_square(ui, hit);
        return 1;
    case CMD_HIT_RECORD:
        if( !hit->name[0] )
            return 0;
        return Editor_Catalog_PickRecord(ui, hit->record_kind, hit->name);
    }
    return 0;
}

/* ---- the entry points ------------------------------------------------------------ */

void
Editor_Command_Open(
    struct EditorUi* ui,
    const char* prefill)
{
    struct CommandPanel* self;

    assert(ui);
    self = command_self(ui);
    if( !self )
    {
        Editor_UiLog(ui, "editor: this window has no command palette");
        return;
    }
    ToriRSChrome_SetText(ui->chrome, self->input, prefill ? prefill : "");
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    (void)ToriRSChrome_FocusWidget(ui->chrome, self->input, prefill ? (int)strlen(prefill) : 0);
    self->open = 1;
    self->chosen = 0;
    cmd_refresh(ui, self, 1);
}

void
Editor_Command_Close(struct EditorUi* ui)
{
    struct CommandPanel* self;

    assert(ui);
    self = command_self(ui);
    if( !self )
        return;
    if( ui->chrome->focus == self->input )
        (void)ToriRSChrome_FocusWidget(ui->chrome, -1, 0);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
    self->open = 0;
}

int
Editor_Command_IsOpen(struct EditorUi* ui)
{
    struct CommandPanel* self;

    assert(ui);
    self = command_self(ui);
    return self && self->open && ui->chrome->panels[self->panel].visible;
}

int
Editor_Command_Feed(
    struct EditorUi* ui,
    const struct Editor_HotkeyInput* input)
{
    struct CommandPanel* self;

    assert(ui);
    assert(input);
    self = command_self(ui);
    if( !self || !Editor_Command_IsOpen(ui) )
        return 0;
    if( input->down[TORIRSK_ESCAPE] )
    {
        Editor_Command_Close(ui);
        return 1;
    }
    if( input->down[TORIRSK_DOWN] && self->chosen + 1 < self->hit_count )
    {
        self->chosen++;
        cmd_refresh(ui, self, 0);
        return 1;
    }
    if( input->down[TORIRSK_UP] && self->chosen > 0 )
    {
        self->chosen--;
        cmd_refresh(ui, self, 0);
        return 1;
    }
    return input->down[TORIRSK_DOWN] || input->down[TORIRSK_UP];
}

int
Editor_Command_TakePendingAction(struct EditorUi* ui)
{
    struct CommandPanel* self;
    int action;

    assert(ui);
    self = command_self(ui);
    if( !self )
        return -1;
    action = self->pending_action;
    self->pending_action = -1;
    return action;
}

const char*
Editor_Command_Row(
    struct EditorUi* ui,
    int index)
{
    struct CommandPanel* self;

    assert(ui);
    assert(index >= 0);
    self = command_self(ui);
    if( !self || index >= self->hit_count )
        return NULL;
    return self->hits[index].text;
}

int
Editor_Command_Chosen(struct EditorUi* ui)
{
    struct CommandPanel* self;

    assert(ui);
    self = command_self(ui);
    return self ? self->chosen : -1;
}

int
Editor_Command_Run(struct EditorUi* ui)
{
    struct CommandPanel* self;
    struct CmdHit hit;

    assert(ui);
    self = command_self(ui);
    if( !self || self->hit_count == 0 || self->chosen < 0 || self->chosen >= self->hit_count )
        return 0;
    /* Copied out: the run may rebuild the hits (a Go to tile reopens the
     * palette with new text) and the palette closes first, so an action
     * that opens something is not shut by the close. */
    hit = self->hits[self->chosen];
    Editor_Command_Close(ui);
    return cmd_run_hit(ui, self, &hit);
}

/* ---- the panel ------------------------------------------------------------------ */

static void
command_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct CommandPanel* self = opaque;

    if( !ui->chrome->panels[self->panel].visible )
    {
        /* Closed by the modal's Escape or its Close button. */
        if( self->open )
            Editor_Command_Close(ui);
        return;
    }
    /* Shown by the View menu's row (the chrome toggle, not Open): adopt it,
     * the box gets the keyboard. */
    if( !self->open )
    {
        Editor_Command_Open(ui, NULL);
        return;
    }
    cmd_refresh(ui, self, 0);
}

static int
command_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct CommandPanel* self = opaque;

    if( widget == self->input )
    {
        /* Enter in the box: the chosen row. With nothing matched yet (a
         * record query still being asked) the box keeps the keyboard. */
        cmd_refresh(ui, self, 0);
        if( self->hit_count > 0 )
            (void)Editor_Command_Run(ui);
        else
            (void)ToriRSChrome_FocusWidget(ui->chrome, self->input, (int)strlen(self->shown_query));
        return 1;
    }
    for( int r = 0; r < CMD_ROWS; r++ )
        if( widget == self->rows[r] )
        {
            if( r < self->hit_count )
            {
                self->chosen = r;
                (void)Editor_Command_Run(ui);
            }
            return 1;
        }
    return 0;
}

static void
command_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct CommandPanel* self = opaque;
    (void)visible;
    /* On demand only: Ctrl+P opens it. */
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
    self->open = 0;
}

static void
command_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_Command_Register(struct EditorUi* ui)
{
    struct CommandPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->pending_action = -1;
    self->shown_chosen = -1;

    self->panel = ToriRSChrome_ModalAdd(chrome, "Command palette", edp_px(ui, CMD_WIDTH));
    assert(self->panel >= 0);
    self->input = ToriRSChrome_TextInput(chrome, self->panel, ">", "");
    ToriRSChrome_SetTooltip(chrome, self->input,
        "An action, Open <panel>, m50_50, 50 50, 3222,3218, loc:name, npc:name, obj:name");
    self->label_status = ToriRSChrome_Label(chrome, self->panel, "");
    for( int r = 0; r < CMD_ROWS; r++ )
    {
        self->rows[r] = ToriRSChrome_TreeRow(chrome, self->panel, "", 0, TORIRS_CHROME_TREE_LEAF);
        assert(self->rows[r] >= 0);
        ToriRSChrome_SetHidden(chrome, self->rows[r], 1);
    }
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "command";
    desc.title = "Command palette";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = command_tick;
    desc.activate = command_activate;
    desc.set_visible = command_set_visible;
    desc.destroy = command_destroy;
    Editor_UiAddPanel(ui, &desc);
}
