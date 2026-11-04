/*
 * The Catalog palette (module "catalog_palette", View > Catalog palette): named
 * strips of up to ten catalog records -- locs, npcs, objs -- to switch between
 * while building, over the PALETTES document every window mirrors
 * (editor/editor_palette.h, `<content_dir>/editor/palettes.txt`).
 *
 *   Palette     the dropdown of palettes (in the file's order); Name + New
 *               palette / Rename / Delete. Each is one undo step on the
 *               authority, named in the History ("New palette Lumbridge").
 *   Strip       ten numbered slots, two rows of five, each a thumbnail of
 *               its record rendered by the App (app/app_catalog_palette.c,
 *               the Preview's camera; cached per record) -- the record's name
 *               while it renders, or where nothing renders (the web, a
 *               record with no model). Hovering a slot says what it holds.
 *               The slot holding the current pick is outlined.
 *   click       the slot's record becomes the catalog pick (published to
 *               the Client as a Catalog row click is: a popped-out palette
 *               picks for the main window) and the Place tool is armed --
 *               with a loc slot's remembered shape and rotation; an npc or
 *               obj pick under Place is the spawn ghost (app_map_editor.c).
 *   right-click Remove, Set pose from the Place tool (a loc: its shape and
 *               rotation now), Move to slot 1..10 (a held slot swaps).
 *   Add current pick  the catalog's pick into the chosen slot ("Add to":
 *               first free, or 1..10; a held slot is replaced), a loc with
 *               the Place tool's pose. The Catalog's "Add to palette" button
 *               and the Preview's menu row do the same into the first free
 *               slot.
 *
 * TORIRS_EDITOR_CATALOG_PALETTE=<palette>[,slot=<n>] (photographs): docks the
 * panel over the column under Terrain once the layout ran, shows <palette>,
 * and once the world is loaded clicks slot <n> as the pointer would.
 */

#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/editor.h"
#include "editor/editor_palette.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Palettes the dropdown lists. */
#define CATPAL_PALETTES_MAX 64
/** Record names resolved to ids (hits and remembered misses). */
#define CATPAL_IDS_MAX 128
#define CATPAL_COLS 5
#define CATPAL_ROWS (EDITOR_PALETTE_SLOTS / CATPAL_COLS)
#define CATPAL_PANEL_W 240
/** A cell's side at most, in logical px, and the gap between cells. */
#define CATPAL_CELL_MAX 46
#define CATPAL_GAP 2
/** Frames after the layout before the photograph's dock, and after the world
 *  settles before its click. */
#define CATPAL_ENV_DOCK_FRAMES 4
#define CATPAL_ENV_CLICK_FRAMES 30

enum
{
    CATPAL_MENU_REMOVE = 0,
    CATPAL_MENU_POSE,
    CATPAL_MENU_MOVE_FIRST,
    CATPAL_MENU_COUNT = CATPAL_MENU_MOVE_FIRST + EDITOR_PALETTE_SLOTS,
};

enum CatPalThumbState
{
    CATPAL_THUMB_FREE = 0,
    /** Asked of the App; ThumbWanted hands it out. */
    CATPAL_THUMB_WANTED,
    CATPAL_THUMB_READY,
    /** The App drew nothing (no model): the cell keeps the name. */
    CATPAL_THUMB_NONE,
};

struct CatPalThumb
{
    enum CatPalThumbState state;
    int kind;
    int id;
    int scene_id;
    uint32_t version;
    /** The strip draw that last showed it: the least recent is evicted. */
    uint32_t used;
};

struct CatPalId
{
    int kind;
    char record[EDITOR_PALETTE_RECORD_MAX];
    /** The catalog's id, or -1: the catalog has no such record. */
    int id;
};

struct CatPalPanel
{
    int panel;
    int dropdown;
    int row_status;
    int graph;
    int row_pick;
    int dropdown_target;
    int button_add;
    /** ui->hotkeys.serial the Add button's keycap was labelled from. */
    uint32_t chord_serial;
    int input_name;
    int button_new;
    int button_rename;
    int button_delete;
    int menu_panel;
    int menu_items[CATPAL_MENU_COUNT];

    /* The parse, current with the mirror's text. */
    struct Editor_TextDoc* file_doc;
    uint32_t file_revision;
    int file_ok;
    struct Editor_PalettesFile file;
    uint32_t serial;

    /** The shown palette ("" when there is none). */
    char current[EDITOR_PALETTE_NAME_MAX];

    /* The dropdown's options (borrowed by the chrome). */
    char names[CATPAL_PALETTES_MAX][EDITOR_PALETTE_NAME_MAX];
    const char* options[CATPAL_PALETTES_MAX];
    int option_count;
    uint32_t options_serial;
    char options_current[EDITOR_PALETTE_NAME_MAX];

    /* The strip. */
    int dirty;
    uint32_t drawn_serial;
    int drawn_w;
    int drawn_pick_kind;
    int drawn_pick_id;
    uint32_t draws;
    int hover_slot;
    int menu_slot;
    char status_text[TORIRS_CHROME_LABEL_MAX];
    int shown_pick_kind;
    int shown_pick_id;

    struct CatPalId ids[CATPAL_IDS_MAX];
    int id_count;
    uint32_t ids_serial;

    struct CatPalThumb thumbs[EDITOR_CATALOG_PALETTE_THUMBS];

    /* TORIRS_EDITOR_CATALOG_PALETTE */
    int env_read;
    int env_active;
    char env_palette[EDITOR_PALETTE_NAME_MAX];
    int env_slot;
    int env_dock_frames;
    int env_shown;
    int env_click_frames;
};

static const char* const k_target_rows[EDITOR_PALETTE_SLOTS + 1] = {
    "first free slot", "slot 1", "slot 2", "slot 3", "slot 4", "slot 5",
    "slot 6", "slot 7", "slot 8", "slot 9", "slot 10",
};

static struct CatPalPanel*
catpal_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "catalog_palette");
}

/* ---- the document ------------------------------------------------------------- */

static struct Editor_TextDoc*
catpal_doc(struct EditorUi* ui)
{
    struct Editor_DocHandle handle;

    if( !ui->editor )
        return NULL;
    if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_PALETTES, EDITOR_PALETTES_DOC_KEY, &handle) || !handle.text
        || !handle.text->loaded )
        return NULL;
    return handle.text;
}

/** The shown palette follows the file: still there, else the first one. */
static void
catpal_follow_current(struct CatPalPanel* self)
{
    const char* first;

    if( self->current[0] && self->file_ok && Editor_PalettesHas(&self->file, self->current) )
        return;
    first = self->file_ok ? Editor_PalettesName(&self->file, 0) : NULL;
    snprintf(self->current, sizeof(self->current), "%s", first ? first : "");
}

/** The parse, current with the mirror's text. NULL without the document (or
 *  when it does not parse -- said once). */
static const struct Editor_PalettesFile*
catpal_refresh(
    struct EditorUi* ui,
    struct CatPalPanel* self)
{
    struct Editor_TextDoc* doc = catpal_doc(ui);

    if( !doc )
    {
        if( self->file_doc )
        {
            Editor_PalettesFree(&self->file);
            self->file_doc = NULL;
            self->file_ok = 0;
            self->serial++;
            self->dirty = 1;
        }
        return NULL;
    }
    if( doc != self->file_doc || doc->revision != self->file_revision )
    {
        struct Editor_PalettesFile old;
        struct Editor_PalettesParseResult parsed;

        Editor_PalettesInit(&old);
        if( self->file_ok )
            Editor_PalettesCopy(&old, &self->file);
        parsed = Editor_PalettesParse(&self->file, doc->text, doc->length);
        if( !parsed.ok )
            Editor_UiLog(ui, "editor: %s line %d does not parse; palettes are read only until it is fixed",
                EDITOR_PALETTES_FILE_PATH, parsed.line);
        self->file_doc = doc;
        self->file_revision = doc->revision;
        self->file_ok = parsed.ok;
        self->serial++;
        self->dirty = 1;
        /* The shown palette went and exactly one new name came (a rename here
         * or in another window, or its undo): follow it. */
        if( self->file_ok && self->current[0] && !Editor_PalettesHas(&self->file, self->current) )
        {
            const char* fresh = NULL;
            int fresh_count = 0;
            for( int i = 0; Editor_PalettesName(&self->file, i); i++ )
                if( !Editor_PalettesHas(&old, Editor_PalettesName(&self->file, i)) )
                {
                    fresh = Editor_PalettesName(&self->file, i);
                    fresh_count++;
                }
            if( fresh_count == 1 )
                snprintf(self->current, sizeof(self->current), "%s", fresh);
        }
        Editor_PalettesFree(&old);
    }
    catpal_follow_current(self);
    return self->file_ok ? &self->file : NULL;
}

/** 0 with a console line when there is no document to edit. */
static const struct Editor_PalettesFile*
catpal_editable(
    struct EditorUi* ui,
    struct CatPalPanel* self)
{
    const struct Editor_PalettesFile* file;

    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: no editor session");
        return NULL;
    }
    file = catpal_refresh(ui, self);
    if( !file )
        Editor_UiLog(ui, "editor: this server has no palettes document (or %s does not parse)",
            EDITOR_PALETTES_FILE_PATH);
    return file;
}

/**
 * Submit one palette action. One command is its own undo step, named by the
 * codec; several (a rename, a delete, a swap) are ONE stroke named `label`.
 */
static int
catpal_submit(
    struct EditorUi* ui,
    const struct Editor_Cmd* commands,
    int count,
    const char* label)
{
    char row[EDITOR_HISTORY_LABEL_MAX];
    int applied = 0;

    assert(commands);
    assert(count > 0);
    assert(label);

    if( count == 1 )
    {
        Editor_PalettesCmdLabel(&commands[0], row, sizeof(row));
        if( !Editor_Apply(ui->editor, &commands[0]) )
        {
            Editor_UiLog(ui, "editor: the server refused '%s'", row);
            return 0;
        }
        Editor_UiLog(ui, "editor: %s (unsaved until Save)", row);
        return 1;
    }
    if( !ui->tools.stroke_open )
    {
        Editor_StrokeBegin(ui->editor);
        Editor_HistoryBreak(&ui->editor->history, label);
    }
    for( int i = 0; i < count; i++ )
        applied += Editor_Apply(ui->editor, &commands[i]) ? 1 : 0;
    if( !ui->tools.stroke_open )
        Editor_StrokeEnd(ui->editor);
    if( applied < count )
        Editor_UiLog(ui, "editor: %s: the server refused %d of %d edits", label, count - applied, count);
    else
        Editor_UiLog(ui, "editor: %s (unsaved until Save)", label);
    return applied == count;
}

/* ---- names to ids ---------------------------------------------------------------- */

/** The catalog's id of `record` of `kind`, -1 when it has none (remembered:
 *  a miss is asked again only after the document changed). */
static int
catpal_resolve(
    struct EditorUi* ui,
    struct CatPalPanel* self,
    int kind,
    const char* record)
{
    struct EditorHost_CatalogRecord answer;
    struct CatPalId* slot;
    int id = -1;

    if( self->ids_serial != self->serial )
    {
        /* Hits stay; misses may have become records. */
        int kept = 0;
        for( int i = 0; i < self->id_count; i++ )
            if( self->ids[i].id >= 0 )
                self->ids[kept++] = self->ids[i];
        self->id_count = kept;
        self->ids_serial = self->serial;
    }
    for( int i = 0; i < self->id_count; i++ )
        if( self->ids[i].kind == kind && strcmp(self->ids[i].record, record) == 0 )
            return self->ids[i].id;
    if( !ui->editor || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return -1;
    memset(&answer, 0, sizeof(answer));
    if( Editor_HostMapEdCatalogRecord(&ui->editor->host, (uint32_t)kind, -1, record, &answer) == EDITOR_HOST_OK )
        id = answer.id;
    Editor_HostCatalogRecordFree(&answer);
    if( self->id_count == CATPAL_IDS_MAX )
    {
        memmove(&self->ids[0], &self->ids[1], sizeof(self->ids) - sizeof(self->ids[0]));
        self->id_count--;
    }
    slot = &self->ids[self->id_count++];
    slot->kind = kind;
    snprintf(slot->record, sizeof(slot->record), "%s", record);
    slot->id = id;
    return id;
}

/* ---- thumbnails -------------------------------------------------------------------- */

/** The cache entry for (kind, id), made (evicting the least recently drawn)
 *  when there is none. */
static struct CatPalThumb*
catpal_thumb(
    struct CatPalPanel* self,
    int kind,
    int id)
{
    struct CatPalThumb* victim = NULL;

    for( int i = 0; i < EDITOR_CATALOG_PALETTE_THUMBS; i++ )
    {
        struct CatPalThumb* t = &self->thumbs[i];
        if( t->state != CATPAL_THUMB_FREE && t->kind == kind && t->id == id )
            return t;
    }
    for( int i = 0; i < EDITOR_CATALOG_PALETTE_THUMBS; i++ )
    {
        struct CatPalThumb* t = &self->thumbs[i];
        if( t->state == CATPAL_THUMB_FREE )
        {
            victim = t;
            break;
        }
        if( !victim || t->used < victim->used )
            victim = t;
    }
    assert(victim);
    victim->state = CATPAL_THUMB_WANTED;
    victim->kind = kind;
    victim->id = id;
    victim->scene_id = 0;
    victim->used = self->draws;
    return victim;
}

int
Editor_CatalogPalette_ThumbWanted(
    struct EditorUi* ui,
    int* out_kind,
    int* out_id,
    int* out_slot)
{
    struct CatPalPanel* self;

    assert(ui);
    assert(out_kind);
    assert(out_id);
    assert(out_slot);
    self = catpal_self(ui);
    if( !self || !ui->visible || !ui->chrome->panels[self->panel].visible )
        return 0;
    for( int i = 0; i < EDITOR_CATALOG_PALETTE_THUMBS; i++ )
        if( self->thumbs[i].state == CATPAL_THUMB_WANTED )
        {
            *out_kind = self->thumbs[i].kind;
            *out_id = self->thumbs[i].id;
            *out_slot = i;
            return 1;
        }
    return 0;
}

void
Editor_CatalogPalette_ThumbDone(
    struct EditorUi* ui,
    int slot,
    int scene_id)
{
    struct CatPalPanel* self;

    assert(ui);
    assert(slot >= 0);
    assert(slot < EDITOR_CATALOG_PALETTE_THUMBS);
    self = catpal_self(ui);
    assert(self);
    self->thumbs[slot].state = scene_id > 0 ? CATPAL_THUMB_READY : CATPAL_THUMB_NONE;
    self->thumbs[slot].scene_id = scene_id;
    self->thumbs[slot].version++;
    self->dirty = 1;
}

/* ---- the strip ----------------------------------------------------------------------- */

static int
catpal_cell(
    struct EditorUi* ui,
    struct CatPalPanel* self)
{
    int const scale = ToriRSChrome_Scale(ui->chrome);
    int const w = ui->chrome->widgets[self->graph].w > 0 ? ui->chrome->widgets[self->graph].w / (scale > 0 ? scale : 1)
                                                         : CATPAL_PANEL_W - 24;
    int cell = (w - CATPAL_GAP * (CATPAL_COLS - 1)) / CATPAL_COLS;

    if( cell > CATPAL_CELL_MAX )
        cell = CATPAL_CELL_MAX;
    return cell < 16 ? 16 : cell;
}

/** The slot's record id when it holds one the catalog knows, else -1. */
static int
catpal_slot_id(
    struct EditorUi* ui,
    struct CatPalPanel* self,
    const struct Editor_PaletteRow* row)
{
    return row ? catpal_resolve(ui, self, row->kind, row->record) : -1;
}

static void
catpal_draw(
    struct EditorUi* ui,
    struct CatPalPanel* self)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const cell = catpal_cell(ui, self);
    int const pitch = cell + CATPAL_GAP;

    self->draws++;
    ToriRSChrome_SetCustomHeight(chrome, self->graph, CATPAL_ROWS * pitch);
    ToriRSChrome_GraphBegin(chrome, self->graph);
    for( int slot = 1; slot <= EDITOR_PALETTE_SLOTS; slot++ )
    {
        const struct Editor_PaletteRow* row
            = self->file_ok && self->current[0] ? Editor_PalettesEntry(&self->file, self->current, slot) : NULL;
        int const x = ((slot - 1) % CATPAL_COLS) * pitch;
        int const y = ((slot - 1) / CATPAL_COLS) * pitch;
        int const id = catpal_slot_id(ui, self, row);
        int const picked = row && id >= 0 && row->kind == ui->tools.pick_kind && id == ui->tools.pick_id;
        struct CatPalThumb* thumb = NULL;
        char number[4];

        ToriRSChrome_GraphRect(chrome, self->graph, x, y, cell, cell, row ? 0x2c2820 : 0x18150f, 1, slot);
        if( row && id >= 0 )
        {
            thumb = catpal_thumb(self, row->kind, id);
            thumb->used = self->draws;
        }
        if( thumb && thumb->state == CATPAL_THUMB_READY )
            ToriRSChrome_GraphImage(chrome, self->graph, x + 1, y + 1, cell - 2, cell - 2, thumb->scene_id, 0, thumb->version);
        else if( row )
        {
            /* The name until (or where) a picture comes: its first letters. */
            char shortname[8];
            snprintf(shortname, sizeof(shortname), "%.6s", row->record);
            ToriRSChrome_GraphText(chrome, self->graph, x + 3, y + cell / 2 + 4, id >= 0 ? 0xd8d0c0 : 0xc06050, shortname);
        }
        /* Shift+1..0 reach slots 1..10: the number is the key. */
        snprintf(number, sizeof(number), "%d", slot % 10);
        ToriRSChrome_GraphText(chrome, self->graph, x + 2, y + 10, row ? 0xffe080 : 0x6a6458, number);
        if( row )
            ToriRSChrome_GraphText(chrome, self->graph, x + cell - 8, y + cell - 3, 0x9a9488,
                row->kind == EDITOR_PALETTE_LOC ? "L" : row->kind == EDITOR_PALETTE_NPC ? "N" : "O");
        if( picked )
            ToriRSChrome_GraphRect(chrome, self->graph, x, y, cell, cell, 0xffd040, 0, 0);
        else if( slot == self->hover_slot )
            ToriRSChrome_GraphRect(chrome, self->graph, x, y, cell, cell, 0x8a8478, 0, 0);
    }
    ToriRSChrome_GraphEnd(chrome, self->graph);
    self->drawn_serial = self->serial;
    self->drawn_w = cell;
    self->drawn_pick_kind = ui->tools.pick_kind;
    self->drawn_pick_id = ui->tools.pick_id;
    self->dirty = 0;
}

/** The dropdown follows the file's palettes and the shown one. */
static void
catpal_options(
    struct EditorUi* ui,
    struct CatPalPanel* self)
{
    int selected = -1;

    if( self->options_serial == self->serial && strcmp(self->options_current, self->current) == 0 )
        return;
    self->option_count = 0;
    for( int i = 0; self->file_ok && i < CATPAL_PALETTES_MAX; i++ )
    {
        const char* name = Editor_PalettesName(&self->file, i);
        if( !name )
            break;
        snprintf(self->names[i], sizeof(self->names[i]), "%s", name);
        self->options[i] = self->names[i];
        if( strcmp(name, self->current) == 0 )
            selected = i;
        self->option_count++;
    }
    if( self->option_count == 0 )
    {
        snprintf(self->names[0], sizeof(self->names[0]), "(no palettes: New palette)");
        self->options[0] = self->names[0];
        ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown, self->options, 1, 0);
    }
    else
        ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown, self->options, self->option_count, selected);
    self->options_serial = self->serial;
    snprintf(self->options_current, sizeof(self->options_current), "%s", self->current);
}

static void
catpal_status(
    struct EditorUi* ui,
    struct CatPalPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];

    if( !catpal_doc(ui) )
        snprintf(line, sizeof(line), "this server has no palettes document");
    else if( !self->file_ok )
        snprintf(line, sizeof(line), "%s does not parse: read only", EDITOR_PALETTES_FILE_PATH);
    else if( !self->current[0] )
        snprintf(line, sizeof(line), "no palettes yet: name one and press New palette");
    else
    {
        int const header = Editor_PalettesFindHeader(&self->file, self->current);
        const char* note = header >= 0 ? self->file.lines[header].row->note : "";
        snprintf(line, sizeof(line), "%d of %d slots%s%s", Editor_PalettesSlotCount(&self->file, self->current),
            EDITOR_PALETTE_SLOTS, note[0] ? " - " : "", note);
    }
    if( strcmp(line, self->status_text) != 0 )
    {
        snprintf(self->status_text, sizeof(self->status_text), "%s", line);
        ToriRSChrome_SetText(ui->chrome, self->row_status, line);
    }
}

static void
catpal_pick_row(
    struct EditorUi* ui,
    struct CatPalPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];

    if( ui->tools.pick_kind == self->shown_pick_kind && ui->tools.pick_id == self->shown_pick_id )
        return;
    self->shown_pick_kind = ui->tools.pick_kind;
    self->shown_pick_id = ui->tools.pick_id;
    if( ui->tools.pick_kind >= EDITOR_PALETTE_LOC && ui->tools.pick_kind <= EDITOR_PALETTE_OBJ && ui->tools.pick_id >= 0 )
        snprintf(line, sizeof(line), "pick: %s %s", Editor_PaletteKindName(ui->tools.pick_kind), ui->tools.pick_name);
    else
        snprintf(line, sizeof(line), "pick: nothing (pick a loc, npc or obj in the Catalog)");
    ToriRSChrome_SetText(ui->chrome, self->row_pick, line);
}

/* ---- actions ---------------------------------------------------------------------------- */

int
Editor_CatalogPalette_UseSlot(
    struct EditorUi* ui,
    int slot)
{
    struct CatPalPanel* self;
    const struct Editor_PaletteRow* row;
    int kind;
    char record[EDITOR_PALETTE_RECORD_MAX];
    int shape;
    int rotation;

    assert(ui);
    assert(slot >= 1);
    assert(slot <= EDITOR_PALETTE_SLOTS);
    self = catpal_self(ui);
    if( !self )
    {
        Editor_UiLog(ui, "editor: this window has no Catalog palette");
        return 0;
    }
    catpal_refresh(ui, self);
    row = self->file_ok && self->current[0] ? Editor_PalettesEntry(&self->file, self->current, slot) : NULL;
    if( !row )
    {
        Editor_UiLog(ui, "editor: slot %d of palette %s is empty: Add current pick fills it", slot,
            self->current[0] ? self->current : "(none)");
        return 0;
    }
    /* Copied out: the pick publishes, and a fact pumped meanwhile may
     * re-parse the file under `row`. */
    kind = row->kind;
    snprintf(record, sizeof(record), "%s", row->record);
    shape = row->shape;
    rotation = row->rotation;
    if( !Editor_Catalog_PickRecord(ui, (uint32_t)kind, record) )
        return 0;
    if( kind == EDITOR_PALETTE_LOC && shape >= 0 )
    {
        ui->tools.place_shape = shape;
        ui->tools.place_rotation = rotation;
    }
    /* Picking from a palette is asking to put it down: Place stamps a loc and
     * drops an npc / obj pick's spawn ghost. */
    if( ui->tools.tool != EDITOR_TOOL_LOC_PLACE )
        Editor_UiSetTool(ui, EDITOR_TOOL_LOC_PLACE);
    if( kind == EDITOR_PALETTE_LOC && shape >= 0 )
        Editor_UiLog(ui, "editor: palette %s slot %d: %s %s, shape %d rot %d", self->current, slot,
            Editor_PaletteKindName(kind), record, shape, rotation);
    else
        Editor_UiLog(ui, "editor: palette %s slot %d: %s %s", self->current, slot, Editor_PaletteKindName(kind), record);
    self->dirty = 1;
    return 1;
}

/** The pick's pack symbol, through the server's catalog. 0 with a console
 *  line when there is no pick or no record. */
static int
catpal_pick_symbol(
    struct EditorUi* ui,
    char* out,
    size_t out_size)
{
    struct EditorHost_CatalogRecord record;
    int ok = 0;

    if( ui->tools.pick_kind < EDITOR_PALETTE_LOC || ui->tools.pick_kind > EDITOR_PALETTE_OBJ || ui->tools.pick_id < 0 )
    {
        Editor_UiLog(ui, "editor: pick a loc, npc or obj in the Catalog first");
        return 0;
    }
    if( !ui->editor || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
    {
        Editor_UiLog(ui, "editor: the server has no catalog to name the pick");
        return 0;
    }
    memset(&record, 0, sizeof(record));
    if( Editor_HostMapEdCatalogRecord(&ui->editor->host, (uint32_t)ui->tools.pick_kind, ui->tools.pick_id, "", &record)
            == EDITOR_HOST_OK
        && record.name && Editor_PaletteRecordValid(record.name) )
    {
        snprintf(out, out_size, "%s", record.name);
        ok = 1;
    }
    else
        Editor_UiLog(ui, "editor: no catalog record (with a name) for %s %d",
            Editor_PaletteKindName(ui->tools.pick_kind), ui->tools.pick_id);
    Editor_HostCatalogRecordFree(&record);
    return ok;
}

/** A palette name nobody uses: "Palette 1", "Palette 2", ... */
static void
catpal_fresh_name(
    const struct Editor_PalettesFile* file,
    char* out,
    size_t out_size)
{
    for( int n = 1;; n++ )
    {
        snprintf(out, out_size, "Palette %d", n);
        if( !Editor_PalettesHas(file, out) )
            return;
    }
}

int
Editor_CatalogPalette_AddPick(
    struct EditorUi* ui,
    int slot)
{
    struct CatPalPanel* self;
    const struct Editor_PalettesFile* file;
    struct Editor_PaletteRow row;
    struct Editor_Cmd* cmds;
    char record[EDITOR_PALETTE_RECORD_MAX];
    char name[EDITOR_PALETTE_NAME_MAX];
    int capacity;
    int count;

    assert(ui);
    assert(slot >= 0);
    assert(slot <= EDITOR_PALETTE_SLOTS);
    self = catpal_self(ui);
    if( !self )
    {
        Editor_UiLog(ui, "editor: this window has no Catalog palette");
        return 0;
    }
    file = catpal_editable(ui, self);
    if( !file || !catpal_pick_symbol(ui, record, sizeof(record)) )
        return 0;
    if( self->current[0] )
        snprintf(name, sizeof(name), "%s", self->current);
    else
        catpal_fresh_name(file, name, sizeof(name));
    if( slot == 0 )
    {
        slot = Editor_PalettesFirstFreeSlot(file, name);
        if( slot == 0 )
        {
            Editor_UiLog(ui, "editor: palette %s is full: choose a slot in Add to (it is replaced)", name);
            return 0;
        }
    }
    Editor_PaletteEntryRow(&row, name, slot, ui->tools.pick_kind, record);
    /* A loc remembers the pose it is placed with now. */
    if( row.kind == EDITOR_PALETTE_LOC && ui->tools.place_shape >= 0 && ui->tools.place_shape <= EDITOR_PALETTE_SHAPE_MAX )
    {
        row.shape = ui->tools.place_shape;
        row.rotation = ui->tools.place_rotation & 3;
    }
    cmds = edp_scratch(&capacity);
    count = Editor_PalettePlanAdd(cmds, capacity, file, &row);
    if( count == 0 )
    {
        Editor_UiLog(ui, "editor: %s is already in slot %d of palette %s", record, slot, name);
        return 0;
    }
    if( !catpal_submit(ui, cmds, count, "Add to palette") )
        return 0;
    snprintf(self->current, sizeof(self->current), "%s", name);
    self->dirty = 1;
    return slot;
}

static int
catpal_new(
    struct EditorUi* ui,
    struct CatPalPanel* self)
{
    const struct Editor_PalettesFile* file = catpal_editable(ui, self);
    const char* typed = ToriRSChrome_Text(ui->chrome, self->input_name);
    struct Editor_PaletteRow row;
    struct Editor_Cmd* cmds;
    char name[EDITOR_PALETTE_NAME_MAX];
    int capacity;

    if( !file )
        return 0;
    if( typed && typed[0] )
        snprintf(name, sizeof(name), "%s", typed);
    else
        catpal_fresh_name(file, name, sizeof(name));
    if( !Editor_PaletteNameValid(name) )
    {
        Editor_UiLog(ui, "editor: '%s' cannot name a palette (no '|', no leading/trailing space)", name);
        return 0;
    }
    if( Editor_PalettesHas(file, name) )
    {
        Editor_UiLog(ui, "editor: there is a palette %s already", name);
        return 0;
    }
    Editor_PaletteHeaderRow(&row, name, NULL);
    cmds = edp_scratch(&capacity);
    if( !Editor_PaletteSetCommand(&cmds[0], file, -1, &row) || !catpal_submit(ui, cmds, 1, "New palette") )
        return 0;
    snprintf(self->current, sizeof(self->current), "%s", name);
    ToriRSChrome_SetText(ui->chrome, self->input_name, "");
    self->dirty = 1;
    return 1;
}

static int
catpal_rename(
    struct EditorUi* ui,
    struct CatPalPanel* self)
{
    const struct Editor_PalettesFile* file = catpal_editable(ui, self);
    const char* typed = ToriRSChrome_Text(ui->chrome, self->input_name);
    struct Editor_Cmd* cmds;
    char label[EDITOR_HISTORY_LABEL_MAX];
    char name[EDITOR_PALETTE_NAME_MAX];
    int capacity;
    int count;

    if( !file )
        return 0;
    if( !self->current[0] )
    {
        Editor_UiLog(ui, "editor: no palette to rename");
        return 0;
    }
    snprintf(name, sizeof(name), "%s", typed ? typed : "");
    if( !Editor_PaletteNameValid(name) || Editor_PalettesHas(file, name) )
    {
        Editor_UiLog(ui, "editor: type a new, unused palette name in Name first");
        return 0;
    }
    cmds = edp_scratch(&capacity);
    count = Editor_PalettePlanRename(cmds, capacity, file, self->current, name);
    snprintf(label, sizeof(label), "Rename palette %s to %s", self->current, name);
    if( count == 0 || !catpal_submit(ui, cmds, count, label) )
        return 0;
    snprintf(self->current, sizeof(self->current), "%s", name);
    ToriRSChrome_SetText(ui->chrome, self->input_name, "");
    self->dirty = 1;
    return 1;
}

static int
catpal_delete(
    struct EditorUi* ui,
    struct CatPalPanel* self)
{
    const struct Editor_PalettesFile* file = catpal_editable(ui, self);
    struct Editor_Cmd* cmds;
    char label[EDITOR_HISTORY_LABEL_MAX];
    int capacity;
    int count;

    if( !file )
        return 0;
    if( !self->current[0] )
    {
        Editor_UiLog(ui, "editor: no palette to delete");
        return 0;
    }
    cmds = edp_scratch(&capacity);
    count = Editor_PalettePlanDelete(cmds, capacity, file, self->current);
    snprintf(label, sizeof(label), "Delete palette %s", self->current);
    if( count == 0 || !catpal_submit(ui, cmds, count, label) )
        return 0;
    self->current[0] = '\0';
    catpal_refresh(ui, self);
    self->dirty = 1;
    return 1;
}

/** The right-click menu's actions on `slot`. */
static void
catpal_menu_act(
    struct EditorUi* ui,
    struct CatPalPanel* self,
    int item,
    int slot)
{
    const struct Editor_PalettesFile* file = catpal_editable(ui, self);
    struct Editor_Cmd* cmds;
    int capacity;
    int count = 0;
    int line;
    char label[EDITOR_HISTORY_LABEL_MAX];

    if( !file || !self->current[0] || slot < 1 || slot > EDITOR_PALETTE_SLOTS )
        return;
    line = Editor_PalettesFindEntry(file, self->current, slot);
    if( line < 0 )
    {
        Editor_UiLog(ui, "editor: slot %d is empty", slot);
        return;
    }
    cmds = edp_scratch(&capacity);
    if( item == CATPAL_MENU_REMOVE )
    {
        if( Editor_PaletteRemoveCommand(&cmds[0], file, line) )
            catpal_submit(ui, cmds, 1, "Remove from palette");
        return;
    }
    if( item == CATPAL_MENU_POSE )
    {
        struct Editor_PaletteRow row = *file->lines[line].row;
        if( row.kind != EDITOR_PALETTE_LOC )
        {
            Editor_UiLog(ui, "editor: a pose is a loc's; slot %d holds an %s", slot, Editor_PaletteKindName(row.kind));
            return;
        }
        if( ui->tools.place_shape < 0 || ui->tools.place_shape > EDITOR_PALETTE_SHAPE_MAX )
        {
            Editor_UiLog(ui, "editor: the Place tool has no shape to remember");
            return;
        }
        row.shape = ui->tools.place_shape;
        row.rotation = ui->tools.place_rotation & 3;
        if( Editor_PaletteSetCommand(&cmds[0], file, line, &row) )
            catpal_submit(ui, cmds, 1, "Set pose");
        else
            Editor_UiLog(ui, "editor: slot %d already has that pose", slot);
        return;
    }
    if( item >= CATPAL_MENU_MOVE_FIRST && item < CATPAL_MENU_COUNT )
    {
        int const to = item - CATPAL_MENU_MOVE_FIRST + 1;
        count = Editor_PalettePlanMove(cmds, capacity, file, self->current, slot, to);
        if( Editor_PalettesEntry(file, self->current, to) )
            snprintf(label, sizeof(label), "Swap slots %d and %d of palette %s", slot, to, self->current);
        else
            snprintf(label, sizeof(label), "Move slot %d to %d of palette %s", slot, to, self->current);
        if( count > 0 )
            catpal_submit(ui, cmds, count, label);
    }
}

static void
catpal_menu_show(
    struct EditorUi* ui,
    struct CatPalPanel* self,
    int slot,
    int well_x,
    int well_y)
{
    int const scale = ToriRSChrome_Scale(ui->chrome);
    struct ToriRSChromeWidget const* well = &ui->chrome->widgets[self->graph];
    const struct Editor_PaletteRow* row = Editor_PalettesEntry(&self->file, self->current, slot);
    char title[TORIRS_CHROME_LABEL_MAX];

    self->menu_slot = slot;
    snprintf(title, sizeof(title), "Slot %d: %s", slot, row ? row->record : "");
    ToriRSChrome_PanelSetTitle(ui->chrome, self->menu_panel, title);
    ToriRSChrome_SetHidden(ui->chrome, self->menu_items[CATPAL_MENU_POSE], !row || row->kind != EDITOR_PALETTE_LOC);
    for( int s = 1; s <= EDITOR_PALETTE_SLOTS; s++ )
        ToriRSChrome_SetHidden(ui->chrome, self->menu_items[CATPAL_MENU_MOVE_FIRST + s - 1], s == slot);
    ToriRSChrome_PanelMove(ui->chrome, self->menu_panel, well->x + well_x * scale, well->y + well_y * scale);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->menu_panel, 1);
}

static void
catpal_menu_hide(
    struct EditorUi* ui,
    struct CatPalPanel* self)
{
    if( ui->chrome->panels[self->menu_panel].visible )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->menu_panel, 0);
}

int
Editor_CatalogPalette_Show(
    struct EditorUi* ui,
    const char* name)
{
    struct CatPalPanel* self;

    assert(ui);
    assert(name);
    self = catpal_self(ui);
    if( !self || !catpal_refresh(ui, self) || !Editor_PalettesHas(&self->file, name) )
        return 0;
    snprintf(self->current, sizeof(self->current), "%s", name);
    self->dirty = 1;
    return 1;
}

int
Editor_CatalogPalette_Step(
    struct EditorUi* ui,
    int step)
{
    struct CatPalPanel* self;
    int count;
    int at = 0;

    assert(ui);
    assert(step == 1 || step == -1);
    self = catpal_self(ui);
    if( !self || !catpal_refresh(ui, self) )
        return 0;
    count = Editor_PalettesCount(&self->file);
    if( count < 2 )
        return 0;
    for( int i = 0; i < count; i++ )
        if( strcmp(Editor_PalettesName(&self->file, i), self->current) == 0 )
            at = i;
    at = (at + step + count) % count;
    snprintf(self->current, sizeof(self->current), "%s", Editor_PalettesName(&self->file, at));
    Editor_UiLog(ui, "editor: palette %s", self->current);
    self->dirty = 1;
    return 1;
}

const char*
Editor_CatalogPalette_Current(struct EditorUi* ui)
{
    struct CatPalPanel* self;

    assert(ui);
    self = catpal_self(ui);
    return self ? self->current : "";
}

/* ---- TORIRS_EDITOR_CATALOG_PALETTE ------------------------------------------------------- */

static void
catpal_env_read(struct CatPalPanel* self)
{
    const char* env = getenv("TORIRS_EDITOR_CATALOG_PALETTE");
    char opts[160];
    char* comma;

    self->env_read = 1;
    if( !env || !env[0] )
        return;
    snprintf(opts, sizeof(opts), "%s", env);
    comma = strchr(opts, ',');
    if( comma )
    {
        *comma = '\0';
        if( strncmp(comma + 1, "slot=", 5) == 0 )
            self->env_slot = atoi(comma + 6);
    }
    snprintf(self->env_palette, sizeof(self->env_palette), "%s", opts);
    self->env_active = 1;
}

/** The panel docked for a photograph: over the column under the Terrain
 *  panel when it is docked there, else at the world's top-left. */
static void
catpal_env_dock(
    struct EditorUi* ui,
    struct CatPalPanel* self)
{
    struct EditorUiPanel const* terrain = Editor_UiFindPanel(ui, "terrain");
    struct ToriRSChromePanel const* t = terrain ? &ui->chrome->panels[terrain->chrome_panel] : NULL;
    int x = ui->world.x + edp_px(ui, 8);
    int y = ui->world.y + edp_px(ui, 8);
    int const w = ui->dock_w > 0 ? ui->dock_w : edp_px(ui, CATPAL_PANEL_W);

    if( t && t->visible && ui->dock_w > 0 && t->fixed_h > 0 )
    {
        x = ui->dock_x;
        y = t->y + t->fixed_h;
    }
    ToriRSChrome_PanelSetFixedWidth(ui->chrome, self->panel, w);
    ToriRSChrome_PanelMove(ui->chrome, self->panel, x, y);
    ui->chrome->panels[self->panel].user_placed = 1;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    self->dirty = 1;
}

static void
catpal_env_tick(
    struct EditorUi* ui,
    struct CatPalPanel* self)
{
    if( !self->env_read )
        catpal_env_read(self);
    if( !self->env_active || ui->solo_panel >= 0 )
        return;
    if( self->env_dock_frames >= 0 && ui->laid_out && ++self->env_dock_frames > CATPAL_ENV_DOCK_FRAMES )
    {
        catpal_env_dock(ui, self);
        self->env_dock_frames = -1;
    }
    if( !self->env_shown && self->file_doc )
    {
        self->env_shown = 1;
        if( !Editor_CatalogPalette_Show(ui, self->env_palette) )
            Editor_UiLog(ui, "editor: TORIRS_EDITOR_CATALOG_PALETTE: no palette named '%s'", self->env_palette);
        else
            Editor_UiLog(ui, "editor: TORIRS_EDITOR_CATALOG_PALETTE shows %s", self->env_palette);
    }
    /* The click once the world has settled, so the Place ghost lands on it. */
    if( self->env_shown && self->env_slot >= 1 && self->env_slot <= EDITOR_PALETTE_SLOTS
        && !(ui->app_services.world_busy && ui->app_services.world_busy(ui->app_services.user_data))
        && ++self->env_click_frames > CATPAL_ENV_CLICK_FRAMES )
    {
        int const slot = self->env_slot;
        self->env_slot = 0;
        (void)Editor_CatalogPalette_UseSlot(ui, slot);
    }
}

/* ---- the module ----------------------------------------------------------------------------- */

static void
catpal_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct CatPalPanel* self = opaque;

    catpal_refresh(ui, self);
    catpal_env_tick(ui, self);
    /* Add wears palette.add_pick's chord (design section 15 surface 3). */
    if( self->chord_serial != ui->hotkeys.serial )
    {
        edp_button_chord(ui, self->button_add, EDITOR_HK_PALETTE_ADD_PICK);
        self->chord_serial = ui->hotkeys.serial;
    }
    if( !ui->chrome->panels[self->panel].visible )
    {
        catpal_menu_hide(ui, self);
        return;
    }
    catpal_options(ui, self);
    catpal_status(ui, self);
    catpal_pick_row(ui, self);
    if( self->dirty || self->drawn_serial != self->serial || self->drawn_w != catpal_cell(ui, self)
        || self->drawn_pick_kind != ui->tools.pick_kind || self->drawn_pick_id != ui->tools.pick_id )
        catpal_draw(ui, self);
}

static void
catpal_tooltip(
    struct EditorUi* ui,
    struct CatPalPanel* self,
    int slot)
{
    const struct Editor_PaletteRow* row
        = slot >= 1 && self->file_ok && self->current[0] ? Editor_PalettesEntry(&self->file, self->current, slot) : NULL;
    char text[TORIRS_CHROME_LABEL_MAX];

    if( slot < 1 )
        snprintf(text, sizeof(text), "click a slot to pick it; right-click for more");
    else if( !row )
        snprintf(text, sizeof(text), "%d: empty - Add current pick fills it", slot);
    else if( row->shape >= 0 )
        snprintf(text, sizeof(text), "%d: %s %s (shape %d rot %d)", slot, Editor_PaletteKindName(row->kind), row->record,
            row->shape, row->rotation);
    else
        snprintf(text, sizeof(text), "%d: %s %s", slot, Editor_PaletteKindName(row->kind), row->record);
    ToriRSChrome_SetTooltip(ui->chrome, self->graph, text);
}

static int
catpal_event(
    struct EditorUi* ui,
    void* opaque,
    const struct ToriRSChromeEvent* event)
{
    struct CatPalPanel* self = opaque;
    int const slot = event->key >= 1 && event->key <= EDITOR_PALETTE_SLOTS ? event->key : 0;

    if( event->widget != self->graph )
        return 0;
    catpal_refresh(ui, self);
    if( event->kind == TORIRS_CHROME_EVENT_GRAPH_PRESS )
    {
        catpal_menu_hide(ui, self);
        if( slot )
            (void)Editor_CatalogPalette_UseSlot(ui, slot);
    }
    else if( event->kind == TORIRS_CHROME_EVENT_GRAPH_MENU )
    {
        if( slot && self->current[0] && Editor_PalettesEntry(&self->file, self->current, slot) )
            catpal_menu_show(ui, self, slot, event->x, event->y);
        else
            catpal_menu_hide(ui, self);
    }
    else if( event->kind == TORIRS_CHROME_EVENT_GRAPH_HOVER )
    {
        if( slot != self->hover_slot )
        {
            self->hover_slot = slot;
            catpal_tooltip(ui, self, slot);
            self->dirty = 1;
        }
    }
    return 1;
}

static int
catpal_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct CatPalPanel* self = opaque;

    for( int i = 0; i < CATPAL_MENU_COUNT; i++ )
        if( widget == self->menu_items[i] )
        {
            int const slot = self->menu_slot;
            catpal_menu_hide(ui, self);
            catpal_menu_act(ui, self, i, slot);
            self->dirty = 1;
            return 1;
        }
    if( widget == self->dropdown )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        catpal_refresh(ui, self);
        if( choice >= 0 && choice < self->option_count )
        {
            snprintf(self->current, sizeof(self->current), "%s", self->names[choice]);
            self->dirty = 1;
        }
        return 1;
    }
    if( widget == self->button_add )
    {
        int const target = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_target);
        (void)Editor_CatalogPalette_AddPick(ui, target > 0 && target <= EDITOR_PALETTE_SLOTS ? target : 0);
        return 1;
    }
    if( widget == self->dropdown_target )
        return 1;
    if( widget == self->button_new )
        return catpal_new(ui, self), 1;
    if( widget == self->button_rename )
        return catpal_rename(ui, self), 1;
    if( widget == self->button_delete )
        return catpal_delete(ui, self), 1;
    return widget == self->graph || widget == self->input_name;
}

/** The editor opening shows every titled panel; this one opens from the View
 *  menu (or the photograph's env), so it starts closed -- it is not part of
 *  the default column. */
static void
catpal_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct CatPalPanel* self = opaque;

    if( visible && !self->env_active )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
    catpal_menu_hide(ui, self);
    self->dirty = 1;
}

static void
catpal_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct CatPalPanel* self = opaque;

    (void)ui;
    Editor_PalettesFree(&self->file);
    ToriMem_Free(ToriMem_Editor(), self);
}

void
Editor_CatalogPalette_Register(struct EditorUi* ui)
{
    struct CatPalPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->menu_slot = -1;
    self->shown_pick_kind = -2;
    self->shown_pick_id = -2;
    self->drawn_pick_kind = -2;
    self->options_serial = (uint32_t)-1;
    self->dirty = 1;

    self->panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8),
        edp_px(ui, 180), edp_px(ui, CATPAL_PANEL_W), "Catalog palette");
    assert(self->panel >= 0);
    self->names[0][0] = '\0';
    self->options[0] = self->names[0];
    self->dropdown = ToriRSChrome_Dropdown(chrome, self->panel, "Palette", self->options, 0, -1);
    self->row_status = ToriRSChrome_Label(chrome, self->panel, "");
    self->graph = ToriRSChrome_Graph(chrome, self->panel, "", CATPAL_ROWS * (CATPAL_CELL_MAX + CATPAL_GAP));
    assert(self->graph >= 0);
    ToriRSChrome_SetTooltip(chrome, self->graph, "click a slot to pick it; right-click for more");
    self->row_pick = ToriRSChrome_Label(chrome, self->panel, "");
    self->dropdown_target = ToriRSChrome_Dropdown(chrome, self->panel, "Add to", k_target_rows, EDITOR_PALETTE_SLOTS + 1, 0);
    self->button_add = ToriRSChrome_Button(chrome, self->panel, "Add current pick");
    ToriRSChrome_SetTooltip(chrome, self->button_add, "the Catalog's pick into the slot (a loc with the Place pose)");
    edp_button_chord(ui, self->button_add, EDITOR_HK_PALETTE_ADD_PICK);
    self->chord_serial = ui->hotkeys.serial;
    ToriRSChrome_Separator(chrome, self->panel);
    self->input_name = ToriRSChrome_TextInput(chrome, self->panel, "Name", "");
    self->button_new = ToriRSChrome_Button(chrome, self->panel, "New palette");
    self->button_rename = ToriRSChrome_Button(chrome, self->panel, "Rename");
    self->button_delete = ToriRSChrome_Button(chrome, self->panel, "Delete");
    ToriRSChrome_SetTooltip(chrome, self->button_delete, "the shown palette, every slot; one undo step");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    self->menu_panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_MENU, 0, 0, 0, "Slot");
    assert(self->menu_panel >= 0);
    self->menu_items[CATPAL_MENU_REMOVE] = ToriRSChrome_MenuItem(chrome, self->menu_panel, "Remove");
    self->menu_items[CATPAL_MENU_POSE] = ToriRSChrome_MenuItem(chrome, self->menu_panel, "Set pose from Place tool");
    for( int s = 1; s <= EDITOR_PALETTE_SLOTS; s++ )
    {
        char row[24];
        snprintf(row, sizeof(row), "Move to slot %d", s);
        self->menu_items[CATPAL_MENU_MOVE_FIRST + s - 1] = ToriRSChrome_MenuItem(chrome, self->menu_panel, row);
    }
    ToriRSChrome_PanelSetVisible(chrome, self->menu_panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "catalog_palette";
    desc.title = "Catalog palette";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = catpal_tick;
    desc.activate = catpal_activate;
    desc.event = catpal_event;
    desc.set_visible = catpal_set_visible;
    desc.destroy = catpal_destroy;
    Editor_UiAddPanel(ui, &desc);
}
