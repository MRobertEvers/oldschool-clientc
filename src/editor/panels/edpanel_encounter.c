/*
 * The encounter builder (workbench 6.7, design #encounter; module
 * "encounter", View > Encounters, the Spawns panel's Group... button): one
 * named set of spawns (editor/editor_encounters.h, the ENCOUNTERS document)
 * and the npc records, stats ledgers, combat script and drops behind it.
 *
 *   header      Set (the document's sets), Name + New set / Rename / Delete
 *               set, Save set (writes editor/encounters.txt and the ledgers
 *               this panel edited); the title says the set, its spawns and
 *               its records
 *   Set         the set's spawns as rows (Find over them); Add spawn: a name
 *               placed at the latched tile and listed, one undo step; From
 *               Spawns selection; Remove from set; Colour. The selected row's
 *               npc: respawnrate, wanderrange, maxrange, huntmode, and the
 *               params huntrange, attackrate, damagetype, from its record's
 *               layers (editor_cfg.h, the Record panel's path), Apply fields
 *               (one undo step), Record... (the Record panel on it); Show
 *               rings draws each set's hunt and wander rings in its colour
 *               (app_overlay.c asks Editor_Encounter_Rings)
 *   Stats       hitpoints, attack, strength, defence, magic, ranged and the
 *               bonus params, from the record; the ledger
 *               (npc_stats/<shard>/<npc>.stats) named with its source; Apply
 *               stats writes the ledger (`source = authored`) and the record
 *               in ONE undo step. Read-only beside them: the max hit per
 *               style and the combat level, each tooltip naming its formula
 *               (editor_npcstats.h)
 *   Combat      an ai_* template (melee, caster, ranger, healer) put into the
 *               npc's step builder as its `[ai_opplayer2]` trigger
 *               (server/scripts/edited/<npc>/scripts/<npc>.rs2), the NPC
 *               builder opened on it
 *   Drops       the npc's death drops as its `[ai_queue3,<npc>]` script states
 *               them (Editor_DropsRead), READ-ONLY: the tree has no drop-table
 *               document to write them back to, and the row says so
 *   Test        RUN (the Live panel's), Place player at the set's centre (the
 *               Place player tool's action, Editor_Accounts_PlacePlayer),
 *               Follow (the World's camera tracks the play account), and the
 *               fight log: the live link's lines that name a hit or a hunt,
 *               else -- said so -- its lines that name the set's npcs
 *
 * Generated layers are never written: a field the generated overlay (rank 1)
 * wins is written into the record's authored overlay when it has one, and
 * refused with a sentence when it has none (the ledger still takes a stat,
 * gen_npc_stats.py carries it into its config). Every refusal says why
 * through Editor_UiStatus. Nothing here runs longer than a frame: RUN is the
 * Live panel's BUILD job, the documents are the mirror's.
 */

#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/accounts/accounts_panel.h"
#include "editor/builder/builder_panels.h"
#include "editor/editor.h"
#include "editor/editor_cfg.h"
#include "editor/editor_encounters.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_npcstats.h"
#include "editor/editor_stamps.h"
#include "editor/live/live_panel.h"
#include "editor/map_editor_ghost.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENC_LIST_ROWS 8
#define ENC_DROP_ROWS 8
#define ENC_LOG_ROWS 10
#define ENC_SETS_MAX 64
/** npc records the panel holds at once: the distinct npcs of a set. */
#define ENC_RECORDS_MAX 12
#define ENC_ROW_TEXT 160

enum
{
    ENC_TAB_SET = 0,
    ENC_TAB_STATS,
    ENC_TAB_COMBAT,
    ENC_TAB_DROPS,
    ENC_TAB_TEST,
    ENC_TAB_COUNT
};

/** The Set tab's server combat fields: (key, param) as the record states
 *  them (torirs_server_content.h; huntrange, attackrate and damagetype are
 *  `param=` lines, as gen_npc_stats.py writes them). */
static const struct
{
    const char* key;
    const char* sub;
} k_set_fields[] = {
    { "respawnrate", "" },
    { "wanderrange", "" },
    { "maxrange", "" },
    { "param", "huntrange" },
    { "param", "attackrate" },
    { "param", "damagetype" },
    /* The combat level a player sees, and the one the server's aggression
     * test reads (a player above twice it is left alone): not the stats. */
    { "vislevel", "" },
};
#define ENC_SET_FIELDS ((int)(sizeof(k_set_fields) / sizeof(k_set_fields[0])))
_Static_assert(sizeof(((struct Editor_EncounterWidgets*)0)->input_fields) == sizeof(int) * ENC_SET_FIELDS,
    "the exported Set inputs are k_set_fields'");

static const char* const k_tabs[ENC_TAB_COUNT] = { "Set", "Stats", "Combat", "Drops", "Test" };
static const char* const k_huntmodes[3] = { "(not stated)", "aggressive", "none" };
static const char* const k_colours[EDITOR_ENCOUNTER_COLOURS + 1] = {
    "by its place", "0 red", "1 orange", "2 yellow", "3 green", "4 cyan", "5 blue", "6 violet", "7 white",
};
/** The ring palette, by colour index (the areas wash's hues). */
static const uint32_t k_ring_rgb[EDITOR_ENCOUNTER_COLOURS] = {
    0xFF5050u, 0xFF9A30u, 0xFFE040u, 0x50E070u, 0x40D8E8u, 0x5080FFu, 0xC070FFu, 0xF0F0F0u,
};

/** One npc's record, resolved over its layers (the Record panel's way). */
struct EncRecord
{
    int used;
    char name[EDITOR_CMD_NAME_MAX];
    char overlays[EDITOR_CFG_RECORD_LAYERS_MAX][EDITOR_DOC_KEY_MAX];
    int overlay_count;
    struct Editor_CfgRecord record;
    uint32_t built_seq;
    int built;
    /** The catalog knew it. */
    int found;
};

struct EncounterPanel
{
    int panel;
    int row_title;
    int dropdown_set;
    int input_name;
    int button_new;
    int button_rename;
    int button_delete;
    int button_save;
    int tabs;

    /* Set */
    int input_spawn;
    int button_add_name;
    int button_add_selection;
    int dropdown_colour;
    int input_find;
    int list_spawns;
    int button_remove;
    int row_selected;
    int button_record;
    int input_fields[ENC_SET_FIELDS];
    int dropdown_huntmode;
    int button_apply_fields;
    int check_rings;
    int row_rings;
    /** Whom it hunts: huntmode, huntrange and vislevel in one sentence. */
    int row_hunts;

    /* Stats */
    int row_ledger;
    int input_levels[EDITOR_NPCSTATS_LEVELS];
    int input_bonuses[EDITOR_NPCSTATS_BONUSES];
    int button_apply_stats;
    int row_melee;
    int row_ranged;
    int row_magic;
    int row_level;

    /* Combat */
    int dropdown_template;
    int row_combat;
    /** The file the Combat tab's sentence is about, on its own row (a label
     *  holds 63 bytes: a sentence and a path do not share them). */
    int row_combat_file;
    int button_open_builder;

    /* Drops */
    int row_drops;
    int input_drop_find;
    int list_drops;
    int button_drops_read;

    /* Test */
    int button_run;
    int button_place;
    int check_follow;
    int row_log;
    int input_log_find;
    int list_log;

    int row_status;

    /* The document, parsed when its text revision moves. */
    struct Editor_EncountersFile file;
    const struct Editor_TextDoc* file_doc;
    uint32_t file_revision;
    int file_ok;
    int file_line;
    uint32_t serial;

    char set_name[EDITOR_ENCOUNTER_NAME_MAX];
    char set_names[ENC_SETS_MAX][EDITOR_ENCOUNTER_NAME_MAX];
    const char* set_options[ENC_SETS_MAX];
    int set_count;
    uint32_t built_serial;
    int built;

    /* The Set tab's rows: indexes of the shown set's spawns that match Find. */
    int rows[EDITOR_ENCOUNTER_SPAWNS_MAX];
    int row_count;
    char shown_find[TORIRS_CHROME_INPUT_MAX];

    /* The selected npc and the fields as last filled. */
    char npc[EDITOR_CMD_NAME_MAX];
    uint32_t filled_seq;
    int filled;
    char filled_npc[EDITOR_CMD_NAME_MAX];

    struct EncRecord records[ENC_RECORDS_MAX];

    /* The npc's ledger: its key, what this panel last read. */
    char ledger_key[EDITOR_DOC_KEY_MAX];
    /** Ledgers this panel edited, saved with the set. */
    char edited_ledgers[16][EDITOR_DOC_KEY_MAX];
    int edited_ledger_count;

    /* Combat: the npc the row was last worked out for, and when (a trigger
     * index query; the file can be written outside the editor, so it is
     * asked again once a second, never every frame). */
    char combat_npc[EDITOR_CMD_NAME_MAX];
    int64_t combat_ms;

    /* Drops */
    struct Editor_DropTable drops;
    char drops_npc[EDITOR_CMD_NAME_MAX];
    char drops_where[EDITOR_DOC_KEY_MAX];
    int drop_rows[EDITOR_DROPS_MAX];
    int drop_row_count;
    char shown_drop_find[TORIRS_CHROME_INPUT_MAX];

    /* The fight log: indexes into the live model's log, oldest first. */
    int log_rows[EDITOR_LIVE_LOG_LINES];
    int log_row_count;
    int log_fight;
    int log_serial;
    int log_built;
    char shown_log_find[TORIRS_CHROME_INPUT_MAX];
    char shown_log_set[EDITOR_ENCOUNTER_NAME_MAX];

    /** The panel was open on the last frame: it opens with the editor. */
    int wanted;

    /* Follow: the play account's tile the camera was last put on. */
    int follow_x;
    int follow_z;
};

static struct EncounterPanel*
enc_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "encounter");
}

static struct EditorHost*
enc_host(struct EditorUi* ui)
{
    if( !ui->editor || !ui->editor->host_open || !(ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return NULL;
    return &ui->editor->host;
}

/* ---- the document ------------------------------------------------------------- */

static struct Editor_TextDoc*
enc_text(
    struct EditorUi* ui,
    const char* key)
{
    struct Editor_DocHandle handle;

    if( !edp_ensure_doc(ui, EDITOR_DOC_TEXT, key) )
        return NULL;
    if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, key, &handle) || !handle.text || !handle.text->loaded )
        return NULL;
    return handle.text;
}

/** Re-parse the document when its text moved. 1 when it parses. */
static int
enc_refresh(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    const struct Editor_TextDoc* doc;

    if( !ui->editor )
        return 0;
    doc = enc_text(ui, EDITOR_ENCOUNTERS_DOC_KEY);
    if( !doc )
    {
        if( self->file_doc )
        {
            Editor_EncountersFree(&self->file);
            self->file_doc = NULL;
            self->file_ok = 0;
            self->serial++;
        }
        return 0;
    }
    if( doc != self->file_doc || doc->revision != self->file_revision )
    {
        struct Editor_EncountersParseResult const r = Editor_EncountersParse(&self->file, doc->text, doc->length);
        self->file_doc = doc;
        self->file_revision = doc->revision;
        self->file_ok = r.ok;
        self->file_line = r.line;
        self->serial++;
    }
    return self->file_ok;
}

static const struct Editor_Encounter*
enc_current(const struct EncounterPanel* self)
{
    int const line = self->set_name[0] ? Editor_EncountersFind(&self->file, self->set_name) : -1;
    return line >= 0 ? self->file.lines[line].encounter : NULL;
}

/**
 * Splice the ENCOUNTERS document to `text`, inside the open stroke. Returns
 * the edits applied (0 nothing changes), -1 when refused.
 */
static int
enc_write_text(
    struct EditorUi* ui,
    const char* key,
    const char* text,
    size_t length,
    const char* what)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    struct Editor_TextDoc* doc = enc_text(ui, key);
    int count;
    int applied;

    if( !doc )
        return -1;
    count = Editor_StampSplices(key, doc->text ? doc->text : "", doc->length, text, length, commands, capacity);
    if( count < 0 )
        return -1;
    if( count == 0 )
        return 0;
    applied = edp_apply(ui, commands, count, what);
    return applied == count ? applied : -1;
}

/** Open ONE undo step named `what` (unless one is open); returns whether it
 *  was open already, for enc_stroke_end. */
static int
enc_stroke_begin(
    struct EditorUi* ui,
    const char* what)
{
    int const was_open = ui->tools.stroke_open;
    if( !was_open )
    {
        Editor_StrokeBegin(ui->editor);
        Editor_HistoryName(ui->editor, what);
    }
    ui->tools.stroke_open = 1;
    return was_open;
}

static void
enc_stroke_end(
    struct EditorUi* ui,
    int was_open)
{
    ui->tools.stroke_open = was_open;
    if( !was_open )
        Editor_StrokeEnd(ui->editor);
}

/** A refusal partway through a step this panel opened (`was_open` 0): the
 *  step goes back whole, as the instance helper's does, since half a
 *  record edit (or a record without its ledger) is not what was applied.
 *  Inside a stroke someone else opened the edits stay in that step. 1 when
 *  nothing of it is left applied. */
static int
enc_stroke_abandon(
    struct EditorUi* ui,
    int was_open,
    int applied_before)
{
    if( was_open )
        return 0;
    if( Editor_HistoryAppliedCommands(Editor_GetHistory(ui->editor)) > applied_before )
        (void)Editor_Undo(ui->editor);
    return 1;
}

/** Write `set` in place of `old_name` (NULL: a new set). 1 when it landed. */
static int
enc_write_set(
    struct EditorUi* ui,
    struct EncounterPanel* self,
    const char* old_name,
    const struct Editor_Encounter* set,
    const char* verb)
{
    size_t length = 0;
    char* text;
    int ok;

    if( !Editor_EncountersTextWith(&self->file, old_name, set, NULL, 0, &length) )
    {
        Editor_UiStatus(ui, "%s refused: set %s is not one the file can hold, or another set has that name", verb,
            set->name);
        return 0;
    }
    text = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(text);
    Editor_EncountersTextWith(&self->file, old_name, set, text, length + 1, &length);
    ok = enc_write_text(ui, EDITOR_ENCOUNTERS_DOC_KEY, text, length, verb) >= 0;
    ToriMem_Free(ToriMem_Editor(), text);
    if( !ok )
        Editor_UiStatus(ui, "%s refused: the authority did not take the edit (a read-only session?)", verb);
    (void)enc_refresh(ui, self);
    return ok;
}

/** A copy of the shown set to edit (heap), or NULL with a sentence. */
static struct Editor_Encounter*
enc_edit_copy(
    struct EditorUi* ui,
    struct EncounterPanel* self,
    const char* verb)
{
    const struct Editor_Encounter* current;
    struct Editor_Encounter* copy;

    if( !enc_refresh(ui, self) )
    {
        if( self->file_doc )
            Editor_UiStatus(ui, "%s refused: %s line %d does not parse; fix it by hand first", verb,
                EDITOR_ENCOUNTERS_FILE_PATH, self->file_line);
        else
            Editor_UiStatus(ui, "%s refused: this session cannot open %s", verb, EDITOR_ENCOUNTERS_FILE_PATH);
        return NULL;
    }
    current = enc_current(self);
    if( !current )
    {
        Editor_UiStatus(ui, "%s refused: no set is shown; name one and press New set", verb);
        return NULL;
    }
    copy = ToriMem_Alloc(ToriMem_Editor(), sizeof(*copy));
    assert(copy);
    *copy = *current;
    return copy;
}

/* ---- npc records ---------------------------------------------------------------- */

static void
enc_record_resolve(
    struct EditorUi* ui,
    struct EncRecord* r)
{
    Editor_CfgRecordFree(&r->record);
    Editor_CfgRecordInit(&r->record, "npc", r->name);
    for( int i = -1; i < r->overlay_count; i++ )
    {
        struct Editor_DocHandle handle;
        char key[EDITOR_DOC_KEY_MAX];
        const char* overlay = i < 0 ? NULL : r->overlays[i];
        const char* text = "";
        size_t length = 0;

        if( !Editor_CfgOverlayKey(key, "npc", r->name, overlay) )
            continue;
        if( Editor_DocFind(&ui->editor->doc, EDITOR_DOC_CFG, key, &handle) && handle.text && handle.text->text )
        {
            text = handle.text->text;
            length = handle.text->length;
        }
        (void)Editor_CfgRecordAddLayer(&r->record, overlay, text, length);
    }
    Editor_CfgRecordResolve(&r->record, NULL, 0);
    r->built_seq = ui->editor->last_cmd_seq;
    r->built = 1;
}

/** The record of npc `name`, its layers opened (the catalog's sources) the
 *  first time it is asked for; resolved again when an edit landed. */
static struct EncRecord*
enc_record(
    struct EditorUi* ui,
    struct EncounterPanel* self,
    const char* name)
{
    struct EncRecord* r = NULL;
    struct EditorHost* host;

    if( !ui->editor || !name[0] )
        return NULL;
    for( int i = 0; i < ENC_RECORDS_MAX && !r; i++ )
        if( self->records[i].used && strcmp(self->records[i].name, name) == 0 )
            r = &self->records[i];
    if( r )
    {
        if( !r->built || r->built_seq != ui->editor->last_cmd_seq )
            enc_record_resolve(ui, r);
        return r;
    }
    for( int i = 0; i < ENC_RECORDS_MAX && !r; i++ )
        if( !self->records[i].used )
            r = &self->records[i];
    if( !r )
    {
        /* Full: the oldest goes (the set changed under it). */
        r = &self->records[0];
        Editor_CfgRecordFree(&r->record);
        memmove(&self->records[0], &self->records[1], sizeof(self->records[0]) * (ENC_RECORDS_MAX - 1));
        r = &self->records[ENC_RECORDS_MAX - 1];
    }
    memset(r, 0, sizeof(*r));
    r->used = 1;
    snprintf(r->name, sizeof(r->name), "%s", name);
    host = enc_host(ui);
    if( host )
    {
        struct EditorHost_CatalogRecord record;
        memset(&record, 0, sizeof(record));
        if( Editor_HostMapEdCatalogRecord(host, TORIRSMAPED_CATALOG_NPC, -1, name, &record) == EDITOR_HOST_OK )
        {
            r->found = 1;
            for( int i = 0; i < record.source_count && r->overlay_count < EDITOR_CFG_RECORD_LAYERS_MAX - 1; i++ )
            {
                const char* path = record.sources[i].path;
                size_t const length = strlen(path);
                if( strncmp(path, "server/scripts/", 15) != 0 || length < 19 || strcmp(path + length - 4, ".npc") != 0
                    || strlen(path + 15) >= sizeof(r->overlays[0]) )
                    continue;
                snprintf(r->overlays[r->overlay_count++], sizeof(r->overlays[0]), "%s", path + 15);
            }
        }
        Editor_HostCatalogRecordFree(&record);
    }
    /* No authored overlay (a Copy to new has none; most base npcs state
     * their server fields in a generated file only): the editor's own, the
     * file the NPC editor's Movement tab writes the same record into
     * (edpanel_npcdoc.c). Without it Apply fields and Apply stats put a
     * clone's hitpoints and huntmode in configs/all.npc, a file of 1.9
     * million lines, and said nothing of where. */
    {
        int authored = 0;
        for( int i = 0; i < r->overlay_count; i++ )
            authored += Editor_CfgLayerRank(r->overlays[i]) == 2;
        if( !authored && r->overlay_count < EDITOR_CFG_RECORD_LAYERS_MAX - 1 )
        {
            int const n = snprintf(r->overlays[r->overlay_count], sizeof(r->overlays[0]), "edited/%s/configs/%s.npc",
                name, name);
            if( n > 0 && (size_t)n < sizeof(r->overlays[0]) )
                r->overlay_count++;
        }
    }
    for( int i = -1; i < r->overlay_count; i++ )
    {
        char key[EDITOR_DOC_KEY_MAX];
        if( Editor_CfgOverlayKey(key, "npc", name, i < 0 ? NULL : r->overlays[i]) )
            (void)edp_ensure_doc(ui, EDITOR_DOC_CFG, key);
    }
    enc_record_resolve(ui, r);
    /* A Copy to new the catalog index does not list until Save is still an
     * npc: its block is in the session's documents. */
    for( int l = 0; l < r->record.layer_count && !r->found; l++ )
        r->found = r->record.layers[l].present;
    return r;
}

/** The value the record's winning layer states ("" when none). */
static const char*
enc_value(
    const struct EncRecord* r,
    const char* key,
    const char* sub)
{
    int row;
    if( !r )
        return "";
    row = Editor_CfgRecordFindRow(&r->record, key, sub, 0);
    /* `=default` is a cache block's spelling of "not stated" (Copy to new
     * writes one per field): an empty field, not the word in it. */
    return row >= 0 && r->record.rows[row].layer >= 0 && strcmp(r->record.rows[row].value, "default") != 0
               ? r->record.rows[row].value
               : "";
}

static int
enc_value_int(
    const struct EncRecord* r,
    const char* key,
    const char* sub,
    int fallback)
{
    const char* value = enc_value(r, key, sub);
    char* end = NULL;
    long n;
    if( !value[0] )
        return fallback;
    n = strtol(value, &end, 10);
    return end && *end == '\0' ? (int)n : fallback;
}

/**
 * The layer a write of (key, sub) goes to: the authored overlay that states
 * it, else the record's authored overlay (the editor's own
 * edited/<npc>/configs/<npc>.npc when it has no other: enc_record) -- the
 * rule the NPC editor writes by (Editor_CfgRecordWriteLayerFor), so one
 * npc's movement and combat fields land in one file. Only a record with no
 * overlay at all falls back to where the field is stated, never a GENERATED
 * layer. -1 when there is nowhere (the caller says why).
 */
static int
enc_write_layer(
    const struct EncRecord* r,
    const char* key,
    const char* sub)
{
    int const row = Editor_CfgRecordFindRow(&r->record, key, sub, 0);
    int layer = Editor_CfgRecordWriteLayerFor(&r->record, row);

    if( layer >= 0 )
        return layer;
    layer = row >= 0 && r->record.rows[row].layer >= 0 ? r->record.rows[row].layer
                                                       : Editor_CfgRecordAddLayerFor(&r->record);
    if( r->record.layers[layer].rank != 1 )
        return layer;
    for( int l = r->record.layer_count - 1; l >= 0; l-- )
        if( r->record.layers[l].rank == 2 && r->record.layers[l].present )
            return l;
    return -1;
}

/**
 * Append the commands that set (key, sub) to `value` on the record to
 * `out`. Returns the commands added (0 nothing changes), -1 when the write
 * has nowhere to go but a generated file, -2 when the grammar refuses it.
 */
static int
enc_field_commands(
    const struct EncRecord* r,
    const char* key,
    const char* sub,
    const char* value,
    struct Editor_Cmd* out,
    int max)
{
    int layer;
    int count;

    if( strcmp(enc_value(r, key, sub), value) == 0 )
        return 0;
    layer = enc_write_layer(r, key, sub);
    if( layer < 0 )
        return -1;
    count = Editor_CfgRecordFieldCommands(&r->record, key, sub, 0, value, layer, out, max);
    return count < 0 ? -2 : count;
}

/* ---- the rows ------------------------------------------------------------------ */

static void
enc_spawn_text(
    const struct Editor_Encounter* set,
    int index,
    char* out,
    size_t out_size)
{
    const struct Editor_EncounterSpawn* s = &set->spawns[index];
    int same = 0;

    for( int i = 0; i < set->spawn_count; i++ )
        same += set->spawns[i].is_obj == s->is_obj && strcmp(set->spawns[i].name, s->name) == 0;
    if( s->is_obj )
        snprintf(out, out_size, "obj %s x%d  %d,%d L%d", s->name, s->count, s->x, s->z, s->level);
    else if( same > 1 )
        snprintf(out, out_size, "%s  %d,%d L%d  (1 of %d)", s->name, s->x, s->z, s->level, same);
    else
        snprintf(out, out_size, "%s  %d,%d L%d", s->name, s->x, s->z, s->level);
}

static int
enc_matches(
    const char* text,
    const char* find)
{
    size_t const n = strlen(find);
    if( n == 0 )
        return 1;
    for( const char* at = text; *at; at++ )
    {
        size_t i = 0;
        while( i < n && at[i] && tolower((unsigned char)at[i]) == tolower((unsigned char)find[i]) )
            i++;
        if( i == n )
            return 1;
    }
    return 0;
}

/** The set dropdown, the title, the spawn rows: rebuilt when the document
 *  or Find moved. */
static void
enc_build(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    const struct Editor_Encounter* set;
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_find);
    char line[ENC_ROW_TEXT];
    int selected = -1;

    if( !find )
        find = "";
    if( self->built && self->built_serial == self->serial && strcmp(find, self->shown_find) == 0 )
        return;
    self->built = 1;
    self->built_serial = self->serial;
    snprintf(self->shown_find, sizeof(self->shown_find), "%s", find);

    /* The dropdown. */
    self->set_count = 0;
    for( int i = 0; i < Editor_EncountersCount(&self->file) && self->set_count < ENC_SETS_MAX; i++ )
    {
        const struct Editor_Encounter* e = Editor_EncountersNth(&self->file, i);
        snprintf(self->set_names[self->set_count], sizeof(self->set_names[0]), "%s", e->name);
        if( strcmp(e->name, self->set_name) == 0 )
            selected = self->set_count;
        self->set_options[self->set_count] = self->set_names[self->set_count];
        self->set_count++;
    }
    if( selected < 0 && self->set_count > 0 && !self->set_name[0] )
    {
        selected = 0;
        snprintf(self->set_name, sizeof(self->set_name), "%s", self->set_names[0]);
    }
    if( selected < 0 )
        self->set_name[0] = '\0';
    if( self->set_count == 0 )
    {
        snprintf(self->set_names[0], sizeof(self->set_names[0]), "(no sets: name one, New set)");
        self->set_options[0] = self->set_names[0];
        ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_set, self->set_options, 1, 0);
    }
    else
        ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_set, self->set_options, self->set_count,
            selected < 0 ? 0 : selected);

    /* The title and the rows. */
    set = enc_current(self);
    self->row_count = 0;
    if( set )
    {
        char names[EDITOR_ENCOUNTER_SPAWNS_MAX][EDITOR_CMD_NAME_MAX];
        int const npcs = Editor_EncounterNpcNames(set, names, EDITOR_ENCOUNTER_SPAWNS_MAX);
        snprintf(line, sizeof(line), "Encounter %s | %d spawn%s | %d record%s", set->name, set->spawn_count,
            set->spawn_count == 1 ? "" : "s", npcs, npcs == 1 ? "" : "s");
        for( int i = 0; i < set->spawn_count; i++ )
        {
            char text[ENC_ROW_TEXT];
            enc_spawn_text(set, i, text, sizeof(text));
            if( enc_matches(text, find) )
                self->rows[self->row_count++] = i;
        }
        ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_colour, set->colour + 1);
    }
    else if( !self->file_doc )
        snprintf(line, sizeof(line), "no %s in this session", EDITOR_ENCOUNTERS_FILE_PATH);
    else if( !self->file_ok )
        snprintf(line, sizeof(line), "%s line %d does not parse: read only", EDITOR_ENCOUNTERS_FILE_PATH,
            self->file_line);
    else
        snprintf(line, sizeof(line), "no set shown: name one and press New set");
    ToriRSChrome_SetText(ui->chrome, self->row_title, line);
    ToriRSChrome_VListSetCount(ui->chrome, self->list_spawns, self->row_count);
    {
        int first;
        int shown;
        if( ToriRSChrome_VListWindow(ui->chrome, self->list_spawns, &first, &shown) )
            for( int r = first; r < first + shown; r++ )
                ToriRSChrome_VListSetRow(ui->chrome, self->list_spawns, r, "");
    }
}

/* ---- the selected npc: Set and Stats ----------------------------------------------- */

static void
enc_set_input(
    struct EditorUi* ui,
    int widget,
    const char* text)
{
    ToriRSChrome_SetText(ui->chrome, widget, text);
}

/** Fill the Set and Stats inputs from the selected npc's record and ledger,
 *  when the npc changed or an edit landed. */
static void
enc_fill(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    struct EncRecord* r;
    const struct Editor_Encounter* set = enc_current(self);
    struct Editor_NpcStatsFile ledger;
    struct Editor_TextDoc* ledger_doc = NULL;
    char line[ENC_ROW_TEXT];
    char source[32] = "";
    int same = 0;

    if( !ui->editor )
        return;
    if( self->filled && self->filled_seq == ui->editor->last_cmd_seq && strcmp(self->filled_npc, self->npc) == 0 )
        return;
    self->filled = 1;
    self->filled_seq = ui->editor->last_cmd_seq;
    snprintf(self->filled_npc, sizeof(self->filled_npc), "%s", self->npc);

    r = self->npc[0] ? enc_record(ui, self, self->npc) : NULL;
    for( int i = 0; set && i < set->spawn_count; i++ )
        same += !set->spawns[i].is_obj && strcmp(set->spawns[i].name, self->npc) == 0;
    if( !self->npc[0] )
        snprintf(line, sizeof(line), "select a spawn row: its npc's fields show here");
    else if( r && !r->found )
        snprintf(line, sizeof(line), "Selected %s | %d spawn%s | no npc has that name", self->npc, same,
            same == 1 ? "" : "s");
    else
        snprintf(line, sizeof(line), "Selected %s | %d spawn%s | %d layer%s", self->npc, same, same == 1 ? "" : "s",
            r ? r->record.layer_count : 0, r && r->record.layer_count == 1 ? "" : "s");
    ToriRSChrome_SetText(ui->chrome, self->row_selected, line);

    for( int f = 0; f < ENC_SET_FIELDS; f++ )
        enc_set_input(ui, self->input_fields[f], enc_value(r, k_set_fields[f].key, k_set_fields[f].sub));
    {
        const char* mode = enc_value(r, "huntmode", "");
        ToriRSChrome_DropdownSetSelected(
            ui->chrome, self->dropdown_huntmode, strcmp(mode, "aggressive") == 0 ? 1 : strcmp(mode, "none") == 0 ? 2 : 0);
    }
    {
        int const wander = enc_value_int(r, "wanderrange", "", MAP_EDITOR_WANDER_DEFAULT);
        int const leash = enc_value_int(r, "maxrange", "", MAP_EDITOR_LEASH_DEFAULT);
        int const hunt = enc_value_int(r, "param", "huntrange", -1);
        if( hunt >= 0 )
            snprintf(line, sizeof(line), "rings: hunt %d | wander %d | leash %d", hunt, wander, leash);
        else
            snprintf(line, sizeof(line), "rings: wander %d | leash %d (no huntrange stated)", wander, leash);
        ToriRSChrome_SetText(ui->chrome, self->row_rings, self->npc[0] ? line : "");
        /* The server's gate (torirs_server_combat.c maybe_aggress): a player
         * above twice the npc's vislevel is never hunted. A giant rat copied
         * as a boss keeps vislevel 3 and leaves every real player alone. */
        if( strcmp(enc_value(r, "huntmode", ""), "aggressive") != 0 )
            snprintf(line, sizeof(line), "it fights only when attacked (huntmode is not aggressive)");
        else if( hunt <= 0 )
            snprintf(line, sizeof(line), "aggressive, but with no huntrange it never sees a player");
        else
        {
            int const level = enc_value_int(r, "vislevel", "", 0);
            if( level > 0 )
                snprintf(line, sizeof(line), "hunts players of combat level %d or lower (2 x vislevel %d)", level * 2,
                    level);
            else
                snprintf(line, sizeof(line), "hunts players of any combat level (it has no vislevel)");
        }
        ToriRSChrome_SetText(ui->chrome, self->row_hunts, self->npc[0] ? line : "");
    }

    /* Stats: the record's values (what RUN builds), the ledger beside. */
    Editor_NpcStatsInit(&ledger);
    self->ledger_key[0] = '\0';
    if( self->npc[0] && Editor_NpcStatsNameValid(self->npc) )
    {
        Editor_NpcStatsDocKey(self->ledger_key, self->npc);
        ledger_doc = enc_text(ui, self->ledger_key);
        if( ledger_doc )
            Editor_NpcStatsParse(&ledger, ledger_doc->text, ledger_doc->length);
    }
    for( int i = 0; i < EDITOR_NPCSTATS_LEVELS; i++ )
        enc_set_input(ui, self->input_levels[i], enc_value(r, Editor_NpcStatsLevelKey(i), ""));
    for( int i = 0; i < EDITOR_NPCSTATS_BONUSES; i++ )
        enc_set_input(ui, self->input_bonuses[i], enc_value(r, "param", Editor_NpcStatsBonusKey(i)));
    if( !self->npc[0] )
        snprintf(line, sizeof(line), "no npc selected (the Set tab's rows)");
    else if( !ledger_doc )
        snprintf(line, sizeof(line), "%s: no ledger document in this session", self->npc);
    else if( ledger_doc->length == 0 )
        snprintf(line, sizeof(line), "%s: no ledger yet; Apply stats writes npc_stats/%c/%s.stats", self->npc,
            Editor_NpcStatsShard(self->npc), self->npc);
    else
    {
        (void)Editor_NpcStatsGet(&ledger, "source", source, sizeof(source));
        snprintf(line, sizeof(line), "ledger npc_stats/%c/%s.stats (source %s)", Editor_NpcStatsShard(self->npc),
            self->npc, source[0] ? source : "not stated");
    }
    ToriRSChrome_SetText(ui->chrome, self->row_ledger, line);
    Editor_NpcStatsFree(&ledger);
}

/** The combat level the Stats inputs give now (an empty input counts 1). */
static int
enc_typed_combat_level(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    int levels[EDITOR_NPCSTATS_LEVELS];

    for( int i = 0; i < EDITOR_NPCSTATS_LEVELS; i++ )
    {
        const char* text = ToriRSChrome_Text(ui->chrome, self->input_levels[i]);
        levels[i] = text && text[0] ? atoi(text) : 1;
    }
    return Editor_NpcCombatLevel(levels[1], levels[2], levels[3], levels[0], levels[4], levels[5]);
}

/** The derived cells, from what the inputs say now (they follow typing). */
static void
enc_derived(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    int levels[EDITOR_NPCSTATS_LEVELS];
    char line[ENC_ROW_TEXT];
    const char* magic_maxhit;

    for( int i = 0; i < EDITOR_NPCSTATS_LEVELS; i++ )
    {
        const char* text = ToriRSChrome_Text(ui->chrome, self->input_levels[i]);
        levels[i] = text && text[0] ? atoi(text) : 1;
    }
    {
        const char* bonus = ToriRSChrome_Text(ui->chrome, self->input_bonuses[6]);
        snprintf(line, sizeof(line), "Max hit melee %d (strength %d, bonus %d)",
            Editor_NpcMaxHit(levels[2], bonus && bonus[0] ? atoi(bonus) : 0), levels[2],
            bonus && bonus[0] ? atoi(bonus) : 0);
        ToriRSChrome_SetText(ui->chrome, self->row_melee, line);
    }
    {
        const char* bonus = ToriRSChrome_Text(ui->chrome, self->input_bonuses[8]);
        snprintf(line, sizeof(line), "Max hit ranged %d (ranged %d, bonus %d)",
            Editor_NpcMaxHit(levels[5], bonus && bonus[0] ? atoi(bonus) : 0), levels[5],
            bonus && bonus[0] ? atoi(bonus) : 0);
        ToriRSChrome_SetText(ui->chrome, self->row_ranged, line);
    }
    magic_maxhit = ToriRSChrome_Text(ui->chrome, self->input_bonuses[12]);
    if( magic_maxhit && magic_maxhit[0] )
        snprintf(line, sizeof(line), "Max hit magic %d (the param magic_maxhit)", atoi(magic_maxhit));
    else
        snprintf(line, sizeof(line), "Max hit magic: none stated (a spell's own)");
    ToriRSChrome_SetText(ui->chrome, self->row_magic, line);
    {
        struct EncRecord* r = self->npc[0] ? enc_record(ui, self, self->npc) : NULL;
        const char* vislevel = enc_value(r, "vislevel", "");
        int const level = enc_typed_combat_level(ui, self);
        if( vislevel[0] )
            snprintf(line, sizeof(line), "Combat level %d (the record's vislevel %s)", level, vislevel);
        else
            snprintf(line, sizeof(line), "Combat level %d", level);
        ToriRSChrome_SetText(ui->chrome, self->row_level, line);
    }
}

/* ---- the verbs ------------------------------------------------------------------- */

static void
enc_select_set(
    struct EncounterPanel* self,
    const char* name)
{
    snprintf(self->set_name, sizeof(self->set_name), "%s", name);
    self->built = 0;
    self->npc[0] = '\0';
    self->filled = 0;
}

static void
enc_new_set(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    const char* name = ToriRSChrome_Text(ui->chrome, self->input_name);
    struct Editor_Encounter* set;

    if( !name || !Editor_EncounterNameValid(name) )
    {
        Editor_UiStatus(ui, "New set refused: \"%.40s\" is not a set name (letters, digits, _ and -, under %d)",
            name ? name : "", EDITOR_ENCOUNTER_NAME_MAX);
        return;
    }
    if( !enc_refresh(ui, self) && self->file_doc )
    {
        Editor_UiStatus(ui, "New set refused: %s line %d does not parse; fix it by hand first",
            EDITOR_ENCOUNTERS_FILE_PATH, self->file_line);
        return;
    }
    if( Editor_EncountersFind(&self->file, name) >= 0 )
    {
        Editor_UiStatus(ui, "New set refused: a set named %s exists; choose another name", name);
        return;
    }
    set = ToriMem_Alloc(ToriMem_Editor(), sizeof(*set));
    assert(set);
    Editor_EncounterInit(set, name);
    if( enc_write_set(ui, self, NULL, set, "new encounter set") )
    {
        enc_select_set(self, name);
        Editor_UiStatus(ui, "New set %s (unsaved; Save set writes %s)", name, EDITOR_ENCOUNTERS_FILE_PATH);
    }
    ToriMem_Free(ToriMem_Editor(), set);
}

static void
enc_rename_set(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    const char* name = ToriRSChrome_Text(ui->chrome, self->input_name);
    struct Editor_Encounter* set = enc_edit_copy(ui, self, "Rename");
    char old[EDITOR_ENCOUNTER_NAME_MAX];

    if( !set )
        return;
    if( !name || !Editor_EncounterNameValid(name) )
        Editor_UiStatus(ui, "Rename refused: \"%.40s\" is not a set name", name ? name : "");
    else
    {
        snprintf(old, sizeof(old), "%s", set->name);
        snprintf(set->name, sizeof(set->name), "%s", name);
        if( enc_write_set(ui, self, old, set, "rename encounter set") )
        {
            enc_select_set(self, name);
            Editor_UiStatus(ui, "Renamed set %s to %s", old, name);
        }
    }
    ToriMem_Free(ToriMem_Editor(), set);
}

static void
enc_delete_set(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    struct Editor_Encounter* set = enc_edit_copy(ui, self, "Delete set");
    size_t length = 0;
    char* text;

    if( !set )
        return;
    if( Editor_EncountersTextWithout(&self->file, set->name, NULL, 0, &length) )
    {
        text = ToriMem_Alloc(ToriMem_Editor(), length + 1);
        assert(text);
        Editor_EncountersTextWithout(&self->file, set->name, text, length + 1, &length);
        if( enc_write_text(ui, EDITOR_ENCOUNTERS_DOC_KEY, text, length, "delete encounter set") >= 0 )
        {
            Editor_UiStatus(ui, "Deleted set %s (its spawns stay; Ctrl+Z brings the set back)", set->name);
            enc_select_set(self, "");
        }
        else
            Editor_UiStatus(ui, "Delete set refused: the authority did not take the edit");
        ToriMem_Free(ToriMem_Editor(), text);
        (void)enc_refresh(ui, self);
    }
    ToriMem_Free(ToriMem_Editor(), set);
}

static void
enc_save(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    int saved = 0;
    int failed = 0;
    int r;

    if( !ui->editor )
        return;
    r = Editor_SaveDoc(ui->editor, EDITOR_DOC_TEXT, EDITOR_ENCOUNTERS_DOC_KEY);
    saved += r > 0;
    failed += r < 0;
    for( int i = 0; i < self->edited_ledger_count; i++ )
    {
        r = Editor_SaveDoc(ui->editor, EDITOR_DOC_TEXT, self->edited_ledgers[i]);
        saved += r > 0;
        failed += r < 0;
    }
    if( failed )
        Editor_UiStatus(ui, "Save set refused for %d document(s): the server is read-only or the write failed", failed);
    else
        Editor_UiStatus(ui, "Saved %d document(s): %s and the ledgers this panel edited (records save with File > Save)",
            saved, EDITOR_ENCOUNTERS_FILE_PATH);
}

/** Spawns join the shown set (made first, named `fallback`, when none is
 *  shown). One undo step with `extra` (n SPAWN commands, may be 0). */
static int
enc_add_spawns(
    struct EditorUi* ui,
    struct EncounterPanel* self,
    const struct Editor_EncounterSpawn* spawns,
    int n,
    const struct Editor_Cmd* extra,
    int extra_count,
    const char* verb)
{
    const struct Editor_Encounter* current;
    struct Editor_Encounter* set;
    int added = 0;
    int was_open;
    int applied_before;
    int ok = 1;
    int is_new = 0;

    if( !enc_refresh(ui, self) && self->file_doc )
    {
        Editor_UiStatus(ui, "%s refused: %s line %d does not parse; fix it by hand first", verb,
            EDITOR_ENCOUNTERS_FILE_PATH, self->file_line);
        return 0;
    }
    if( !self->file_doc )
    {
        Editor_UiStatus(ui, "%s refused: this session cannot open %s", verb, EDITOR_ENCOUNTERS_FILE_PATH);
        return 0;
    }
    set = ToriMem_Alloc(ToriMem_Editor(), sizeof(*set));
    assert(set);
    current = enc_current(self);
    if( current )
        *set = *current;
    else
    {
        /* No set shown: a new one, the first free group_<n>. */
        char name[EDITOR_ENCOUNTER_NAME_MAX];
        int k = 1;
        do
            snprintf(name, sizeof(name), "group_%d", k++);
        while( Editor_EncountersFind(&self->file, name) >= 0 );
        Editor_EncounterInit(set, name);
        is_new = 1;
    }
    for( int i = 0; i < n; i++ )
        added += Editor_EncounterAddSpawn(set, &spawns[i]);
    if( added == 0 && extra_count == 0 )
    {
        Editor_UiStatus(ui, "%s refused: %s already in set %s, or the set is full (%d spawns)", verb,
            n == 1 ? "that spawn is" : "those spawns are", set->name, EDITOR_ENCOUNTER_SPAWNS_MAX);
        ToriMem_Free(ToriMem_Editor(), set);
        return 0;
    }
    applied_before = Editor_HistoryAppliedCommands(Editor_GetHistory(ui->editor));
    was_open = enc_stroke_begin(ui, verb);
    if( extra_count > 0 && edp_apply(ui, extra, extra_count, verb) != extra_count )
        ok = 0;
    if( ok && !enc_write_set(ui, self, is_new ? NULL : set->name, set, verb) )
        ok = 0;
    enc_stroke_end(ui, was_open);
    if( !ok )
        (void)enc_stroke_abandon(ui, was_open, applied_before);
    if( ok )
    {
        enc_select_set(self, set->name);
        /* Where, for one spawn: the tile is the one latched, which may be a
         * click from minutes ago -- "joined" with no place hid that. */
        if( n == 1 && added == 1 )
            Editor_UiStatus(ui, "%s: %s joined set %s at %d,%d level %d (unsaved)", verb, spawns[0].name, set->name,
                spawns[0].x, spawns[0].z, spawns[0].level);
        else
            Editor_UiStatus(ui, "%s: %d spawn%s joined set %s (unsaved)", verb, added, added == 1 ? "" : "s",
                set->name);
    }
    ToriMem_Free(ToriMem_Editor(), set);
    return ok ? added : 0;
}

static void
enc_add_by_name(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    const char* name = ToriRSChrome_Text(ui->chrome, self->input_spawn);
    struct Editor_EncounterSpawn spawn;
    struct Editor_SpawnRow row;
    struct Editor_Cmd command;
    char key[EDITOR_DOC_KEY_MAX];

    if( !name || !Editor_EncounterSpawnNameValid(name) )
    {
        Editor_UiStatus(ui, "Add spawn refused: type the npc's content name (a pack symbol, no spaces)");
        return;
    }
    if( !ui->tools.tile_valid )
    {
        Editor_UiStatus(ui, "Add spawn refused: latch a tile with the Select tool first; the spawn goes there");
        return;
    }
    /* The set holds this npc on this tile already (Enter in the field,
     * then the button, is one request made twice): nothing is written. The
     * second press used to add a second spawn row -- two bosses on one tile
     * -- under "0 spawns joined". */
    {
        const struct Editor_Encounter* current = enc_current(self);
        for( int i = 0; current && i < current->spawn_count; i++ )
            if( !current->spawns[i].is_obj && strcmp(current->spawns[i].name, name) == 0
                && current->spawns[i].x == ui->tools.tile_x && current->spawns[i].z == ui->tools.tile_z
                && current->spawns[i].level == ui->tools.tile_level )
            {
                Editor_UiStatus(ui, "Add spawn: %s is in set %s at %d,%d already; latch another tile for a second one",
                    name, current->name, ui->tools.tile_x, ui->tools.tile_z);
                return;
            }
    }
    memset(&row, 0, sizeof(row));
    snprintf(row.name, sizeof(row.name), "%s", name);
    row.abs_x = ui->tools.tile_x;
    row.abs_z = ui->tools.tile_z;
    row.level = ui->tools.tile_level;
    row.count = 1;
    Editor_SpawnDocKeyAt(key, row.abs_x, row.abs_z);
    if( !edp_ensure_doc(ui, EDITOR_DOC_SPAWN, key) )
    {
        Editor_UiStatus(ui, "Add spawn refused: the session cannot open the spawn document %s", key);
        return;
    }
    Editor_SpawnCommand(&command, &row, 1);
    memset(&spawn, 0, sizeof(spawn));
    snprintf(spawn.name, sizeof(spawn.name), "%s", name);
    spawn.x = row.abs_x;
    spawn.z = row.abs_z;
    spawn.level = row.level;
    spawn.count = 1;
    (void)enc_add_spawns(ui, self, &spawn, 1, &command, 1, "add encounter spawn");
}

static int
enc_add_selection(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    struct Editor_SpawnRow rows[EDITOR_ENCOUNTER_SPAWNS_MAX];
    struct Editor_EncounterSpawn spawns[EDITOR_ENCOUNTER_SPAWNS_MAX];
    int n = Editor_Spawns_Selection(ui, rows, EDITOR_ENCOUNTER_SPAWNS_MAX);
    int count = 0;

    if( n <= 0 )
    {
        Editor_UiStatus(ui, "Group refused: select spawns in the Spawns panel first");
        return 0;
    }
    if( n > EDITOR_ENCOUNTER_SPAWNS_MAX )
        n = EDITOR_ENCOUNTER_SPAWNS_MAX;
    for( int i = 0; i < n; i++ )
    {
        if( rows[i].suppress || !Editor_EncounterSpawnNameValid(rows[i].name) )
            continue;
        memset(&spawns[count], 0, sizeof(spawns[count]));
        spawns[count].is_obj = rows[i].is_obj;
        snprintf(spawns[count].name, sizeof(spawns[count].name), "%s", rows[i].name);
        spawns[count].x = rows[i].abs_x;
        spawns[count].z = rows[i].abs_z;
        spawns[count].level = rows[i].level;
        spawns[count].count = rows[i].count > 0 ? rows[i].count : 1;
        count++;
    }
    if( count == 0 )
    {
        Editor_UiStatus(ui, "Group refused: the selection is suppression rows or names a set cannot hold");
        return 0;
    }
    return enc_add_spawns(ui, self, spawns, count, NULL, 0, "group spawns");
}

static void
enc_remove_spawn(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    int const r = ToriRSChrome_VListSelected(ui->chrome, self->list_spawns);
    struct Editor_Encounter* set = enc_edit_copy(ui, self, "Remove from set");

    if( !set )
        return;
    if( r < 0 || r >= self->row_count || self->rows[r] >= set->spawn_count )
        Editor_UiStatus(ui, "Remove from set refused: select a spawn row first");
    else
    {
        char what[ENC_ROW_TEXT];
        struct Editor_SpawnRow row;
        const struct Editor_EncounterSpawn* gone = &set->spawns[self->rows[r]];
        memset(&row, 0, sizeof(row));
        row.is_obj = gone->is_obj;
        snprintf(row.name, sizeof(row.name), "%s", gone->name);
        row.abs_x = gone->x;
        row.abs_z = gone->z;
        row.level = gone->level;
        row.count = gone->count;
        enc_spawn_text(set, self->rows[r], what, sizeof(what));
        Editor_EncounterRemoveSpawn(set, self->rows[r]);
        if( enc_write_set(ui, self, set->name, set, "remove encounter spawn") )
        {
            /* The set only names the spawn: it still stands in the world (an
             * aggressive boss left on a tile it was added to by mistake).
             * Selected, it is one Delete away. */
            if( Editor_UiFindPanel(ui, "spawns") && Editor_Spawns_Select(ui, &row, 1) == 1 )
                Editor_UiStatus(ui, "Removed %s from set %s; its spawn still stands and is selected: Delete removes it",
                    what, set->name);
            else
                Editor_UiStatus(ui, "Removed %s from set %s (the spawn itself stays)", what, set->name);
        }
    }
    ToriMem_Free(ToriMem_Editor(), set);
}

static void
enc_set_colour(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    int const choice = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_colour);
    struct Editor_Encounter* set = enc_edit_copy(ui, self, "Colour");

    if( !set )
        return;
    if( set->colour != choice - 1 )
    {
        set->colour = choice - 1;
        (void)enc_write_set(ui, self, set->name, set, "encounter colour");
    }
    ToriMem_Free(ToriMem_Editor(), set);
}

/** Say why a record write found no file to take it. */
static void
enc_say_generated(
    struct EditorUi* ui,
    const char* verb,
    const char* npc,
    const char* field)
{
    Editor_UiStatus(ui, "%s refused: %s's %s is stated only in a generated file, and %s has no authored overlay to "
                        "write it into (open its Record to add one)",
        verb, npc, field, npc);
}

/** One field (key, sub) of the selected npc's record to `value`, inside the
 *  open stroke, planned against the record as the last edit left it.
 *  Returns the commands applied, 0 nothing changes, -1 refused. */
static int
enc_apply_field(
    struct EditorUi* ui,
    struct EncounterPanel* self,
    const char* key,
    const char* sub,
    const char* value,
    const char* what)
{
    struct EncRecord* r = enc_record(ui, self, self->npc);
    struct Editor_Cmd commands[4];
    int count;

    if( !r )
        return -1;
    count = enc_field_commands(r, key, sub, value, commands, 4);
    if( count <= 0 )
        return count == 0 ? 0 : -1;
    if( edp_apply(ui, commands, count, what) != count )
        return -1;
    enc_record_resolve(ui, r);
    return count;
}

/** The value Set's input `f` (ENC_SET_FIELDS is the huntmode dropdown). */
static const char*
enc_set_field_value(
    struct EditorUi* ui,
    struct EncounterPanel* self,
    int f)
{
    const char* value;
    if( f < ENC_SET_FIELDS )
        value = ToriRSChrome_Text(ui->chrome, self->input_fields[f]);
    else
    {
        int const mode = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_huntmode);
        value = mode == 1 ? "aggressive" : mode == 2 ? "none" : "";
    }
    return value ? value : "";
}

static void
enc_apply_fields(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    struct EncRecord* r;
    struct Editor_Cmd probe[4];
    int changed = 0;
    int applied = 0;
    int was_open;
    int applied_before;
    int ok = 1;

    if( !self->npc[0] )
    {
        Editor_UiStatus(ui, "Apply fields refused: select an npc spawn row first");
        return;
    }
    r = enc_record(ui, self, self->npc);
    if( !r )
        return;
    /* Every refusal before any edit: all of it lands, or none. */
    for( int f = 0; f <= ENC_SET_FIELDS; f++ )
    {
        const char* key = f < ENC_SET_FIELDS ? k_set_fields[f].key : "huntmode";
        const char* sub = f < ENC_SET_FIELDS ? k_set_fields[f].sub : "";
        const char* value = enc_set_field_value(ui, self, f);
        int const planned = enc_field_commands(r, key, sub, value, probe, 4);
        if( planned == -1 )
        {
            enc_say_generated(ui, "Apply fields", self->npc, sub[0] ? sub : key);
            return;
        }
        if( planned == -2 )
        {
            Editor_UiStatus(ui, "Apply fields refused: %s=%s is not a line a config can hold", sub[0] ? sub : key, value);
            return;
        }
        changed += planned > 0;
    }
    if( changed == 0 )
    {
        Editor_UiStatus(ui, "Apply fields: nothing changes for %s", self->npc);
        return;
    }
    /* ONE undo step, each field planned against the edit before it (two
     * inserts into one block, a removal's line). */
    applied_before = Editor_HistoryAppliedCommands(Editor_GetHistory(ui->editor));
    was_open = enc_stroke_begin(ui, "encounter fields");
    for( int f = 0; f <= ENC_SET_FIELDS && ok; f++ )
    {
        const char* key = f < ENC_SET_FIELDS ? k_set_fields[f].key : "huntmode";
        const char* sub = f < ENC_SET_FIELDS ? k_set_fields[f].sub : "";
        int const n = enc_apply_field(ui, self, key, sub, enc_set_field_value(ui, self, f), "encounter fields");
        if( n < 0 )
            ok = 0;
        else
            applied += n > 0;
    }
    enc_stroke_end(ui, was_open);
    if( ok )
    {
        /* Where they went: the file a person opens to see them. */
        int const layer = enc_write_layer(r, "huntmode", "");
        Editor_UiStatus(ui, "%s: %d field%s set in server/scripts/%s (unsaved; Save writes it, RUN builds it)", self->npc,
            applied, applied == 1 ? "" : "s", layer >= 0 ? r->record.layers[layer].label : "its record");
    }
    else if( enc_stroke_abandon(ui, was_open, applied_before) )
        Editor_UiStatus(ui, "Apply fields refused: the authority did not take every edit; nothing was applied");
    else
        Editor_UiStatus(ui, "Apply fields refused: the authority did not take every edit (the ones before it stay in "
                            "the open step)");
    self->filled = 0;
}

/** Stat `i` of the Stats tab: its ledger key and its record (key, sub). */
static void
enc_stat_key(
    int i,
    const char** out_ledger_key,
    const char** out_key,
    const char** out_sub)
{
    if( i < EDITOR_NPCSTATS_LEVELS )
    {
        *out_ledger_key = Editor_NpcStatsLevelKey(i);
        *out_key = *out_ledger_key;
        *out_sub = "";
    }
    else
    {
        *out_ledger_key = Editor_NpcStatsBonusKey(i - EDITOR_NPCSTATS_LEVELS);
        *out_key = "param";
        *out_sub = *out_ledger_key;
    }
}

static void
enc_apply_stats(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    struct EncRecord* r;
    struct Editor_TextDoc* ledger;
    struct Editor_Cmd probe[4];
    char* text;
    size_t length;
    int changed = 0;
    int levels_changed = 0;
    int record_edits = 0;
    int generated_only = 0;
    int was_open;
    int applied_before;
    int ok = 1;
    /* vislevel follows a level change: it, not the stats, is the combat
     * level a player sees and the one the aggression test reads. */
    char vislevel_before[16] = "";
    char vislevel_after[16] = "";
    int vislevel_written = 0;

    if( !self->npc[0] || !Editor_NpcStatsNameValid(self->npc) )
    {
        Editor_UiStatus(ui, "Apply stats refused: select an npc spawn row first");
        return;
    }
    r = enc_record(ui, self, self->npc);
    ledger = self->ledger_key[0] ? enc_text(ui, self->ledger_key) : NULL;
    if( !r || !ledger )
    {
        Editor_UiStatus(ui, "Apply stats refused: the session cannot open %s's record or its ledger", self->npc);
        return;
    }
    /* The ledger's new text with every changed stat; the refusals first. */
    length = ledger->length;
    text = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(text);
    if( length > 0 )
        memcpy(text, ledger->text, length);
    text[length] = '\0';
    for( int i = 0; i < EDITOR_NPCSTATS_LEVELS + EDITOR_NPCSTATS_BONUSES && ok; i++ )
    {
        int const widget =
            i < EDITOR_NPCSTATS_LEVELS ? self->input_levels[i] : self->input_bonuses[i - EDITOR_NPCSTATS_LEVELS];
        const char* value = ToriRSChrome_Text(ui->chrome, widget);
        const char* ledger_key;
        const char* key;
        const char* sub;
        size_t next_length = 0;
        char* next;
        int planned;

        enc_stat_key(i, &ledger_key, &key, &sub);
        if( !value )
            value = "";
        if( strcmp(enc_value(r, key, sub), value) == 0 )
            continue;
        changed++;
        levels_changed += i < EDITOR_NPCSTATS_LEVELS;
        if( !Editor_NpcStatsTextSet(text, length, self->npc, ledger_key, value, NULL, 0, &next_length) )
        {
            Editor_UiStatus(ui, "Apply stats refused: %s = %s is not a line the ledger can hold", ledger_key, value);
            ok = 0;
            break;
        }
        next = ToriMem_Alloc(ToriMem_Editor(), next_length + 1);
        assert(next);
        Editor_NpcStatsTextSet(text, length, self->npc, ledger_key, value, next, next_length + 1, &next_length);
        ToriMem_Free(ToriMem_Editor(), text);
        text = next;
        length = next_length;
        planned = enc_field_commands(r, key, sub, value, probe, 4);
        if( planned == -2 )
        {
            Editor_UiStatus(ui, "Apply stats refused: %s=%s is not a line a config can hold", ledger_key, value);
            ok = 0;
        }
        /* -1: only a generated file states it; the ledger takes it and
         * gen_npc_stats.py carries it into its config. */
        generated_only += planned == -1;
    }
    if( ok && levels_changed )
    {
        snprintf(vislevel_before, sizeof(vislevel_before), "%s", enc_value(r, "vislevel", ""));
        snprintf(vislevel_after, sizeof(vislevel_after), "%d", enc_typed_combat_level(ui, self));
        /* Only where the npc's own overlay can take it (a generated-only
         * record keeps its level, as its stats wait for gen_npc_stats.py). */
        if( strcmp(vislevel_before, vislevel_after) == 0
            || enc_field_commands(r, "vislevel", "", vislevel_after, probe, 4) <= 0 )
            vislevel_after[0] = '\0';
    }
    if( ok && changed == 0 )
    {
        Editor_UiStatus(ui, "Apply stats: nothing changes for %s", self->npc);
        ok = 0;
    }
    if( ok )
    {
        /* The record and the ledger in ONE undo step. */
        applied_before = Editor_HistoryAppliedCommands(Editor_GetHistory(ui->editor));
        was_open = enc_stroke_begin(ui, "encounter stats");
        for( int i = 0; i < EDITOR_NPCSTATS_LEVELS + EDITOR_NPCSTATS_BONUSES && ok; i++ )
        {
            int const widget =
                i < EDITOR_NPCSTATS_LEVELS ? self->input_levels[i] : self->input_bonuses[i - EDITOR_NPCSTATS_LEVELS];
            const char* value = ToriRSChrome_Text(ui->chrome, widget);
            const char* ledger_key;
            const char* key;
            const char* sub;
            struct EncRecord* now = enc_record(ui, self, self->npc);
            int n;

            enc_stat_key(i, &ledger_key, &key, &sub);
            if( !now || enc_write_layer(now, key, sub) < 0 )
                continue;
            n = enc_apply_field(ui, self, key, sub, value ? value : "", "encounter stats");
            if( n < 0 )
                ok = 0;
            else
                record_edits += n > 0;
        }
        if( ok && vislevel_after[0] )
        {
            struct EncRecord* now = enc_record(ui, self, self->npc);
            if( now && enc_write_layer(now, "vislevel", "") >= 0 )
            {
                int const n = enc_apply_field(ui, self, "vislevel", "", vislevel_after, "encounter stats");
                if( n < 0 )
                    ok = 0;
                else
                {
                    record_edits += n > 0;
                    vislevel_written = n > 0;
                }
            }
        }
        if( ok && enc_write_text(ui, self->ledger_key, text, length, "encounter stats") < 0 )
            ok = 0;
        enc_stroke_end(ui, was_open);
        if( ok )
        {
            int listed = 0;
            for( int i = 0; i < self->edited_ledger_count; i++ )
                listed |= strcmp(self->edited_ledgers[i], self->ledger_key) == 0;
            if( !listed && self->edited_ledger_count < 16 )
                snprintf(self->edited_ledgers[self->edited_ledger_count++], sizeof(self->edited_ledgers[0]), "%s",
                    self->ledger_key);
            {
                /* The record's half, by file: the npc's own overlay. */
                struct EncRecord* written = enc_record(ui, self, self->npc);
                int const layer = written ? enc_write_layer(written, "hitpoints", "") : -1;
                char level_note[64] = "";
                if( vislevel_written )
                    snprintf(level_note, sizeof(level_note), "; combat level (vislevel) %s -> %s",
                        vislevel_before[0] ? vislevel_before : "unstated", vislevel_after);
                Editor_UiStatus(ui, "%s: %d stat%s in the ledger (source = authored), %d in the record (%s)%s%s",
                    self->npc, changed, changed == 1 ? "" : "s", record_edits,
                    layer >= 0 ? written->record.layers[layer].label : "no authored file",
                    generated_only ? "; generated-only ones wait for gen_npc_stats.py" : "", level_note);
            }
        }
        else if( enc_stroke_abandon(ui, was_open, applied_before) )
            Editor_UiStatus(ui, "Apply stats refused: the authority did not take every edit; nothing was applied");
        else
            Editor_UiStatus(ui, "Apply stats refused: the authority did not take every edit (the ones before it stay in "
                                "the open step)");
    }
    ToriMem_Free(ToriMem_Editor(), text);
    self->filled = 0;
}

/* ---- Combat ------------------------------------------------------------------------ */

static void
enc_trigger_file(
    struct EditorUi* ui,
    const char* trigger,
    char out[EDITOR_DOC_KEY_MAX]);

/** The file that holds the npc's own [ai_opplayer2,<npc>] when it is NOT the
 *  step builder's (edited/<npc>/scripts/<npc>.rs2): a boss whose combat is
 *  written by hand. Its path under server/scripts into `out`; 0 when the
 *  tree has no such trigger outside the builder's file. */
static int
enc_combat_scripted(
    struct EditorUi* ui,
    const char* npc,
    char* out,
    size_t out_size)
{
    char trigger[EDITOR_CMD_NAME_MAX * 2];
    char key[EDITOR_DOC_KEY_MAX];
    char own[EDITOR_DOC_KEY_MAX];

    snprintf(trigger, sizeof(trigger), "ai_opplayer2,%s", npc);
    enc_trigger_file(ui, trigger, key);
    if( !key[0] )
        return 0;
    snprintf(own, sizeof(own), "rs2:edited/%s/scripts/%s.rs2", npc, npc);
    if( strcmp(key, own) == 0 )
        return 0;
    snprintf(out, out_size, "%s", key + 4);
    return 1;
}

static void
enc_combat_row(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    char line[ENC_ROW_TEXT];
    char file[EDITOR_DOC_KEY_MAX];
    int const scripted = self->npc[0] && enc_combat_scripted(ui, self->npc, file, sizeof(file));

    if( scripted )
        snprintf(line, sizeof(line), "this npc's combat is scripted by hand, in:");
    else if( self->npc[0] )
    {
        snprintf(line, sizeof(line), "a template writes its [ai_opplayer2] trigger into:");
        snprintf(file, sizeof(file), "edited/%s/scripts/%s.rs2", self->npc, self->npc);
    }
    else
    {
        snprintf(line, sizeof(line), "select an npc spawn row (the Set tab) first");
        file[0] = '\0';
    }
    ToriRSChrome_SetText(ui->chrome, self->row_combat, line);
    {
        /* The path's tail when it is longer than a label. */
        char shown[TORIRS_CHROME_LABEL_MAX];
        size_t const length = strlen(file);
        if( length < sizeof(shown) )
            snprintf(shown, sizeof(shown), "%s", file);
        else
            snprintf(shown, sizeof(shown), "...%s", file + length - (sizeof(shown) - 4));
        ToriRSChrome_SetText(ui->chrome, self->row_combat_file, shown);
    }
    /* A template would be a second [ai_opplayer2] for the npc. */
    ToriRSChrome_SetDisabled(ui->chrome, self->button_open_builder, scripted);
    ToriRSChrome_SetDisabled(ui->chrome, self->dropdown_template, scripted);
}

static void
enc_open_builder(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    enum Editor_CombatTemplate const kind =
        (enum Editor_CombatTemplate)ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_template);
    struct Builder_PanelView view;
    struct Builder_Model* model;
    struct Builder_Trigger* trigger;
    char trigger_name[32];
    char body[BUILDER_TEXT_MAX * 4];
    const char* source = NULL;
    size_t source_length = 0;
    int length;

    if( !self->npc[0] || !Builder_NameValid(self->npc) )
    {
        Editor_UiStatus(ui, "Open in step builder refused: select an npc spawn row whose name the builder takes");
        return;
    }
    if( kind < 0 || kind >= EDITOR_COMBAT_TEMPLATE_COUNT )
        return;
    /* A hand-written fight stays the only one: the template is not written
     * beside it (two [ai_opplayer2,<npc>] do not compile) or over it. */
    {
        char file[EDITOR_DOC_KEY_MAX];
        if( enc_combat_scripted(ui, self->npc, file, sizeof(file)) )
        {
            Editor_UiStatus(ui, "Open in step builder refused: %s's combat is scripted in server/scripts/%s; edit it "
                                "there (the Triggers panel opens it)",
                self->npc, file);
            return;
        }
    }
    if( kind == EDITOR_COMBAT_CASTER )
    {
        struct Editor_TextDoc* doc = enc_text(ui, "rs2:" EDITOR_COMBAT_CASTER_SOURCE);
        if( doc && doc->text )
        {
            source = doc->text;
            source_length = doc->length;
        }
    }
    length = Editor_CombatTemplateBody(kind, source, source_length, trigger_name, body, sizeof(body));
    if( length < 0 )
    {
        Editor_UiStatus(ui, "Caster refused: server/scripts/%s has no [ai_opplayer2] swing to copy",
            EDITOR_COMBAT_CASTER_SOURCE);
        return;
    }
    if( (size_t)length >= sizeof(body) )
    {
        Editor_UiStatus(ui, "Caster refused: the dark wizard's swing is longer than the step builder holds");
        return;
    }
    if( !Editor_UiPanelSelf(ui, BUILDER_PANEL_NPC) || !Builder_PanelOpenSubject(ui, BUILDER_PANEL_NPC, self->npc)
        || !Builder_PanelInspect(ui, BUILDER_PANEL_NPC, &view) || !view.model )
    {
        Editor_UiStatus(ui, "Open in step builder refused: the NPC editor cannot open %s (see its status)", self->npc);
        return;
    }
    if( view.read_only )
    {
        Editor_UiStatus(ui, "Open in step builder refused: %s's builder is read-only (a trigger it would write "
                            "exists elsewhere)",
            self->npc);
        Editor_UiPanelOpenBeside(ui, BUILDER_PANEL_NPC, "encounter");
        return;
    }
    model = ToriMem_Alloc(ToriMem_Editor(), sizeof(*model));
    assert(model);
    *model = *view.model;
    trigger = Builder_ModelAiTrigger(model, trigger_name);
    if( trigger && trigger->step_count > 0 )
    {
        Editor_UiStatus(ui, "Open in step builder: %s already has a [%s] trigger; opened it as it stands", self->npc,
            trigger_name);
        Editor_UiPanelOpenBeside(ui, BUILDER_PANEL_NPC, "encounter");
        ToriMem_Free(ToriMem_Editor(), model);
        return;
    }
    trigger = Builder_ModelAiTriggerAdd(model, trigger_name);
    if( !trigger )
    {
        Editor_UiStatus(ui, "Open in step builder refused: %s's builder holds %d triggers already", self->npc,
            BUILDER_TRIGGERS_MAX);
        ToriMem_Free(ToriMem_Editor(), model);
        return;
    }
    /* The body as RAW steps, one per chunk of whole lines a step holds. */
    {
        const char* at = body;
        while( *at && trigger->step_count < BUILDER_STEPS_MAX )
        {
            struct Builder_Step step;
            size_t used = 0;
            Builder_StepInit(&step, BUILDER_STEP_RAW);
            while( *at )
            {
                const char* end = strchr(at, '\n');
                size_t const line = end ? (size_t)(end - at) + 1 : strlen(at);
                if( used + line >= sizeof(step.text) && used > 0 )
                    break;
                if( line >= sizeof(step.text) )
                {
                    Editor_UiStatus(ui, "Open in step builder refused: a line of the %s template is longer than a step",
                        Editor_CombatTemplateName(kind));
                    ToriMem_Free(ToriMem_Editor(), model);
                    return;
                }
                memcpy(step.text + used, at, line);
                used += line;
                at += line;
            }
            while( used > 0 && step.text[used - 1] == '\n' )
                used--;
            step.text[used] = '\0';
            (void)Builder_TriggerStepAdd(trigger, &step);
        }
    }
    if( !Builder_PanelSetModel(ui, BUILDER_PANEL_NPC, model) )
        Editor_UiStatus(ui, "Open in step builder refused: the NPC editor did not take the %s template",
            Editor_CombatTemplateName(kind));
    else
        Editor_UiStatus(ui, "%s: the %s template is %s's [%s] (unsaved; the builder's Save writes it)", self->npc,
            Editor_CombatTemplateName(kind), self->npc, trigger_name);
    Editor_UiPanelOpenBeside(ui, BUILDER_PANEL_NPC, "encounter");
    ToriMem_Free(ToriMem_Editor(), model);
}

/* ---- Drops --------------------------------------------------------------------------- */

/** The rs2 TEXT key of a catalog TRIGGER record's file ("" when the catalog
 *  has none). */
static void
enc_trigger_file(
    struct EditorUi* ui,
    const char* trigger,
    char out[EDITOR_DOC_KEY_MAX])
{
    struct EditorHost* host = enc_host(ui);
    struct EditorHost_CatalogRecord record;

    out[0] = '\0';
    if( !host )
        return;
    memset(&record, 0, sizeof(record));
    if( Editor_HostMapEdCatalogRecord(host, TORIRSMAPED_CATALOG_TRIGGER, -1, trigger, &record) == EDITOR_HOST_OK
        && record.source_count > 0 && strncmp(record.sources[0].path, "server/scripts/", 15) == 0
        && strlen(record.sources[0].path + 15) + 4 < EDITOR_DOC_KEY_MAX )
        snprintf(out, EDITOR_DOC_KEY_MAX, "rs2:%s", record.sources[0].path + 15);
    Editor_HostCatalogRecordFree(&record);
}

static void
enc_read_drops(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    char header[EDITOR_CMD_NAME_MAX * 2];
    char key[EDITOR_DOC_KEY_MAX];
    struct Editor_TextDoc* doc;
    char line[ENC_ROW_TEXT];

    memset(&self->drops, 0, sizeof(self->drops));
    self->drops_where[0] = '\0';
    snprintf(self->drops_npc, sizeof(self->drops_npc), "%s", self->npc);
    self->shown_drop_find[0] = '\x01';
    if( !self->npc[0] )
    {
        ToriRSChrome_SetText(ui->chrome, self->row_drops, "select an npc spawn row (the Set tab) first");
        return;
    }
    snprintf(header, sizeof(header), "ai_queue3,%s", self->npc);
    enc_trigger_file(ui, header, key);
    doc = key[0] ? enc_text(ui, key) : NULL;
    if( !doc || !Editor_DropsRead(doc->text, doc->length, header, &self->drops) )
    {
        snprintf(line, sizeof(line), "%s has no [ai_queue3,%s] in the tree: a category's table or the default (bones)",
            self->npc, self->npc);
        ToriRSChrome_SetText(ui->chrome, self->row_drops, line);
        return;
    }
    snprintf(self->drops_where, sizeof(self->drops_where), "%s", key + 4);
    /* The rows behind a call: in the same file, else wherever the catalog
     * says the label or proc is. */
    if( self->drops.row_count == 0 && self->drops.call[0] )
    {
        char label[EDITOR_CMD_NAME_MAX * 2];
        int found = 0;
        for( int k = 0; k < 2 && !found; k++ )
        {
            snprintf(label, sizeof(label), "%s,%s", k == 0 ? "label" : "proc", self->drops.call);
            found = Editor_DropsRead(doc->text, doc->length, label, &self->drops);
            if( !found )
            {
                struct Editor_TextDoc* other;
                char other_key[EDITOR_DOC_KEY_MAX];
                enc_trigger_file(ui, label, other_key);
                other = other_key[0] ? enc_text(ui, other_key) : NULL;
                if( other && Editor_DropsRead(other->text, other->length, label, &self->drops) )
                {
                    found = 1;
                    snprintf(self->drops_where, sizeof(self->drops_where), "%s", other_key + 4);
                }
            }
        }
    }
    snprintf(line, sizeof(line), "read-only, from %s (no drop-table document exists)%s", self->drops_where,
        self->drops.unread ? "; some lines unread" : "");
    ToriRSChrome_SetText(ui->chrome, self->row_drops, line);
}

static void
enc_drop_text(
    const struct Editor_DropRow* row,
    char* out,
    size_t out_size)
{
    char chance[48];
    Editor_DropChance(row, chance, sizeof(chance));
    snprintf(out, out_size, "%s x %s  %s", row->obj, row->count, chance);
}

/* ---- Test ------------------------------------------------------------------------------- */

static void
enc_place_player(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    const struct Editor_Encounter* set;
    int x;
    int z;
    int level;

    (void)enc_refresh(ui, self);
    set = enc_current(self);
    if( !set || !Editor_EncounterCentre(set, &x, &z, &level) )
    {
        Editor_UiStatus(ui, "Place player refused: the shown set has no spawns to stand among");
        return;
    }
    if( Editor_Accounts_PlacePlayer(ui, x, z, level) )
        Editor_UiStatus(ui, "Placing the play account at set %s's centre, %d,%d level %d", set->name, x, z, level);
    else
        Editor_UiStatus(ui, "Place player refused: no Accounts panel, no lane, or the play account is not known yet");
}

int
Editor_Encounter_FightLog(
    const struct Editor_LiveModel* model,
    const char (*names)[EDITOR_CMD_NAME_MAX],
    int name_count,
    const char* find,
    int* out_rows,
    int max,
    int* out_fight)
{
    int count = 0;

    assert(model);
    assert(names || name_count == 0);
    assert(find);
    assert(out_rows || max == 0);
    assert(out_fight);
    *out_fight = 0;
    /* A fight's lines: the server's HIT line, anything that says hunt. */
    for( int i = model->log_count - 1; i >= 0; i-- )
    {
        const char* text = Editor_LiveLogLine(model, i);
        if( !text || !(strstr(text, "HIT ") || enc_matches(text, "hunt")) )
            continue;
        *out_fight = 1;
        if( enc_matches(text, find) && count < max )
            out_rows[count++] = i;
    }
    if( *out_fight )
        return count;
    /* None: the lines that name one of the set's npcs. */
    for( int i = model->log_count - 1; i >= 0; i-- )
    {
        const char* text = Editor_LiveLogLine(model, i);
        int take = 0;
        if( !text )
            continue;
        for( int n = 0; n < name_count && !take; n++ )
            take = strstr(text, names[n]) != NULL;
        if( take && enc_matches(text, find) && count < max )
            out_rows[count++] = i;
    }
    return count;
}

/** The Test tab's log rows and the sentence above them. */
static void
enc_build_log(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    const struct Editor_LiveModel* model = Editor_LivePanelModel(ui);
    const struct Editor_Encounter* set = enc_current(self);
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_log_find);
    char names[16][EDITOR_CMD_NAME_MAX];
    int const name_count = set ? Editor_EncounterNpcNames(set, names, 16) : 0;
    char line[ENC_ROW_TEXT];

    if( !find )
        find = "";
    if( self->log_built && (model ? model->log_serial : -1) == self->log_serial
        && strcmp(find, self->shown_log_find) == 0 && strcmp(self->set_name, self->shown_log_set) == 0 )
        return;
    self->log_built = 1;
    self->log_serial = model ? model->log_serial : -1;
    snprintf(self->shown_log_find, sizeof(self->shown_log_find), "%s", find);
    snprintf(self->shown_log_set, sizeof(self->shown_log_set), "%s", self->set_name);
    self->log_row_count = 0;
    self->log_fight = 0;
    if( model )
        self->log_row_count = Editor_Encounter_FightLog(model, (const char(*)[EDITOR_CMD_NAME_MAX])names, name_count,
            find, self->log_rows, EDITOR_LIVE_LOG_LINES, &self->log_fight);
    if( !model )
        snprintf(line, sizeof(line), "no Live panel in this window: no live link to read");
    else if( self->log_fight )
        snprintf(line, sizeof(line), "fight log: %d line%s naming a hit or a hunt", self->log_row_count,
            self->log_row_count == 1 ? "" : "s");
    else
        snprintf(line, sizeof(line), "the link carries no hit or hunt lines; %d naming the set's npcs",
            self->log_row_count);
    ToriRSChrome_SetText(ui->chrome, self->row_log, line);
    ToriRSChrome_VListSetCount(ui->chrome, self->list_log, self->log_row_count);
    {
        int first;
        int shown;
        if( ToriRSChrome_VListWindow(ui->chrome, self->list_log, &first, &shown) )
            for( int r = first; r < first + shown; r++ )
                ToriRSChrome_VListSetRow(ui->chrome, self->list_log, r, "");
    }
}

static void
enc_follow(
    struct EditorUi* ui,
    struct EncounterPanel* self)
{
    const struct Editor_LiveModel* model;

    if( !ToriRSChrome_Checked(ui->chrome, self->check_follow) )
        return;
    model = Editor_LivePanelModel(ui);
    if( !model || model->play_x < 0 || !ui->app_services.camera_to_tile )
        return;
    if( model->play_x == self->follow_x && model->play_z == self->follow_z )
        return;
    if( ui->app_services.camera_to_tile(ui->app_services.user_data, model->play_x, model->play_z, model->play_level) )
    {
        self->follow_x = model->play_x;
        self->follow_z = model->play_z;
    }
}

/* ---- the module ------------------------------------------------------------------------- */

static void
enc_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct EncounterPanel* self = opaque;
    const struct Editor_Encounter* set;
    int first;
    int shown;
    int const tab = ToriRSChrome_PanelActiveTab(ui->chrome, self->panel);

    enc_follow(ui, self);
    /* Open when the editor opens again only if it was open (View >
     * Encounters, Group..., Editor_Encounter_Show). */
    self->wanted = ui->chrome->panels[self->panel].visible;
    if( !self->wanted )
        return;
    (void)enc_refresh(ui, self);
    enc_build(ui, self);
    set = enc_current(self);

    /* The selected row names the npc. */
    {
        int const r = ToriRSChrome_VListSelected(ui->chrome, self->list_spawns);
        const char* npc = "";
        if( set && r >= 0 && r < self->row_count && self->rows[r] < set->spawn_count && !set->spawns[self->rows[r]].is_obj )
            npc = set->spawns[self->rows[r]].name;
        if( strcmp(npc, self->npc) != 0 )
            snprintf(self->npc, sizeof(self->npc), "%s", npc);
    }
    enc_fill(ui, self);
    if( tab == ENC_TAB_STATS )
        enc_derived(ui, self);
    if( tab == ENC_TAB_COMBAT
        && (strcmp(self->combat_npc, self->npc) != 0 || ui->chrome->now_ms - self->combat_ms >= 1000) )
    {
        snprintf(self->combat_npc, sizeof(self->combat_npc), "%s", self->npc);
        self->combat_ms = ui->chrome->now_ms;
        enc_combat_row(ui, self);
    }
    if( tab == ENC_TAB_DROPS && strcmp(self->drops_npc, self->npc) != 0 )
        enc_read_drops(ui, self);
    if( tab == ENC_TAB_TEST )
        enc_build_log(ui, self);

    if( set && ToriRSChrome_VListWindow(ui->chrome, self->list_spawns, &first, &shown) )
        for( int r = first; r < first + shown && r < self->row_count; r++ )
        {
            char text[ENC_ROW_TEXT];
            enc_spawn_text(set, self->rows[r], text, sizeof(text));
            ToriRSChrome_VListSetRow(ui->chrome, self->list_spawns, r, text);
        }
    if( tab == ENC_TAB_DROPS )
    {
        const char* find = ToriRSChrome_Text(ui->chrome, self->input_drop_find);
        if( !find )
            find = "";
        if( strcmp(find, self->shown_drop_find) != 0 )
        {
            snprintf(self->shown_drop_find, sizeof(self->shown_drop_find), "%s", find);
            self->drop_row_count = 0;
            for( int i = 0; i < self->drops.row_count; i++ )
            {
                char text[ENC_ROW_TEXT];
                enc_drop_text(&self->drops.rows[i], text, sizeof(text));
                if( enc_matches(text, find) )
                    self->drop_rows[self->drop_row_count++] = i;
            }
            ToriRSChrome_VListSetCount(ui->chrome, self->list_drops, self->drop_row_count);
            if( ToriRSChrome_VListWindow(ui->chrome, self->list_drops, &first, &shown) )
                for( int r = first; r < first + shown; r++ )
                    ToriRSChrome_VListSetRow(ui->chrome, self->list_drops, r, "");
        }
        if( ToriRSChrome_VListWindow(ui->chrome, self->list_drops, &first, &shown) )
            for( int r = first; r < first + shown && r < self->drop_row_count; r++ )
            {
                char text[ENC_ROW_TEXT];
                enc_drop_text(&self->drops.rows[self->drop_rows[r]], text, sizeof(text));
                ToriRSChrome_VListSetRow(ui->chrome, self->list_drops, r, text);
            }
    }
    if( tab == ENC_TAB_TEST && ToriRSChrome_VListWindow(ui->chrome, self->list_log, &first, &shown) )
    {
        const struct Editor_LiveModel* model = Editor_LivePanelModel(ui);
        for( int r = first; model && r < first + shown && r < self->log_row_count; r++ )
        {
            const char* text = Editor_LiveLogLine(model, self->log_rows[r]);
            ToriRSChrome_VListSetRow(ui->chrome, self->list_log, r, text ? text : "");
        }
    }
}

static int
enc_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct EncounterPanel* self = opaque;

    if( widget == self->dropdown_set )
    {
        int const choice = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        if( choice >= 0 && choice < self->set_count )
            enc_select_set(self, self->set_names[choice]);
        return 1;
    }
    if( widget == self->button_new )
        enc_new_set(ui, self);
    else if( widget == self->button_rename )
        enc_rename_set(ui, self);
    else if( widget == self->button_delete )
        enc_delete_set(ui, self);
    else if( widget == self->button_save )
        enc_save(ui, self);
    else if( widget == self->button_add_name || widget == self->input_spawn )
        enc_add_by_name(ui, self);
    else if( widget == self->button_add_selection )
        (void)enc_add_selection(ui, self);
    else if( widget == self->button_remove )
        enc_remove_spawn(ui, self);
    else if( widget == self->dropdown_colour )
        enc_set_colour(ui, self);
    else if( widget == self->button_apply_fields )
        enc_apply_fields(ui, self);
    else if( widget == self->button_apply_stats )
        enc_apply_stats(ui, self);
    else if( widget == self->button_record )
    {
        if( !self->npc[0] )
            Editor_UiStatus(ui, "Record refused: select an npc spawn row first");
        else if( !Editor_UiPanelSelf(ui, "record") )
            Editor_UiStatus(ui, "Record refused: this window has no Record panel (no catalog)");
        else
            (void)Editor_Record_Open(ui, TORIRSMAPED_CATALOG_NPC, self->npc);
    }
    else if( widget == self->button_open_builder )
        enc_open_builder(ui, self);
    else if( widget == self->button_drops_read )
    {
        self->drops_npc[0] = '\x01';
        enc_read_drops(ui, self);
    }
    else if( widget == self->button_run )
    {
        int const sent = Editor_Live_Run(ui);
        if( sent < 0 )
            Editor_UiStatus(ui, "RUN refused: this window has no Live panel (no editor server)");
        else if( sent == 0 )
            Editor_UiStatus(ui, "RUN refused: a RUN or build is in flight, or the connection is gone (see the console)");
    }
    else if( widget == self->button_place )
        enc_place_player(ui, self);
    else if( widget == self->list_spawns )
        self->filled = 0;
    else if( widget == self->tabs || widget == self->input_find || widget == self->input_name
             || widget == self->check_rings || widget == self->check_follow || widget == self->dropdown_huntmode
             || widget == self->dropdown_template || widget == self->input_drop_find || widget == self->input_log_find
             || widget == self->list_drops || widget == self->list_log )
        return 1;
    else
    {
        for( int f = 0; f < ENC_SET_FIELDS; f++ )
            if( widget == self->input_fields[f] )
                return 1;
        for( int i = 0; i < EDITOR_NPCSTATS_LEVELS; i++ )
            if( widget == self->input_levels[i] )
                return 1;
        for( int i = 0; i < EDITOR_NPCSTATS_BONUSES; i++ )
            if( widget == self->input_bonuses[i] )
                return 1;
        return 0;
    }
    return 1;
}

static void
enc_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct EncounterPanel* self = opaque;
    if( visible && !self->wanted )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
    self->built = 0;
}

static void
enc_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct EncounterPanel* self = opaque;
    (void)ui;
    Editor_EncountersFree(&self->file);
    for( int i = 0; i < ENC_RECORDS_MAX; i++ )
        Editor_CfgRecordFree(&self->records[i].record);
    ToriMem_Free(ToriMem_Editor(), self);
}

static void
enc_tip(
    struct EditorUi* ui,
    int widget,
    const char* tip)
{
    ToriRSChrome_SetTooltip(ui->chrome, widget, tip);
}

void
Editor_Encounter_Register(struct EditorUi* ui)
{
    struct EncounterPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    /* No npc is a row too ("select an npc spawn row"): worked out at once. */
    self->combat_npc[0] = '\x01';
    Editor_EncountersInit(&self->file);
    self->follow_x = -1;
    self->follow_z = -1;
    self->panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 24),
        edp_px(ui, 150), edp_px(ui, 340), "Encounters");
    assert(self->panel >= 0);

    self->row_title = ToriRSChrome_Label(chrome, self->panel, "");
    /* "Open set", not "Set": the first tab is "Set", and one label naming
     * two controls cannot be clicked by it. */
    self->dropdown_set = ToriRSChrome_Dropdown(chrome, self->panel, "Open set", NULL, 0, -1);
    self->input_name = ToriRSChrome_TextInput(chrome, self->panel, "Name", "");
    enc_tip(ui, self->input_name, "a set name: letters, digits, _ and -");
    self->button_new = ToriRSChrome_Button(chrome, self->panel, "New set");
    self->button_rename = ToriRSChrome_Button(chrome, self->panel, "Rename");
    self->button_delete = ToriRSChrome_Button(chrome, self->panel, "Delete set");
    enc_tip(ui, self->button_delete, "the set goes; its spawns stay");
    self->button_save = ToriRSChrome_Button(chrome, self->panel, "Save set");
    enc_tip(ui, self->button_save, "write editor/encounters.txt and edited ledgers");
    self->tabs = ToriRSChrome_Tabs(chrome, self->panel, k_tabs, ENC_TAB_COUNT, 0);

    /* Set */
    ToriRSChrome_PanelBeginTab(chrome, self->panel, ENC_TAB_SET);
    self->input_spawn = ToriRSChrome_TextInput(chrome, self->panel, "Add spawn", "");
    enc_tip(ui, self->input_spawn, "an npc's name; it is placed at the latched tile");
    self->button_add_name = ToriRSChrome_Button(chrome, self->panel, "Add at latched tile");
    self->button_add_selection = ToriRSChrome_Button(chrome, self->panel, "From Spawns selection");
    /* A short button cuts these captions ("Add at latche..."): full width. */
    ToriRSChrome_ButtonSetFill(chrome, self->button_add_name, 1);
    ToriRSChrome_ButtonSetFill(chrome, self->button_add_selection, 1);
    self->dropdown_colour =
        ToriRSChrome_Dropdown(chrome, self->panel, "Colour", k_colours, EDITOR_ENCOUNTER_COLOURS + 1, 0);
    self->input_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    self->list_spawns = ToriRSChrome_VList(chrome, self->panel, "", ENC_LIST_ROWS, 0);
    assert(self->list_spawns >= 0);
    self->button_remove = ToriRSChrome_Button(chrome, self->panel, "Remove from set");
    ToriRSChrome_ButtonSetFill(chrome, self->button_remove, 1);
    enc_tip(ui, self->button_remove, "the row leaves the set; the spawn stays");
    self->row_selected = ToriRSChrome_Label(chrome, self->panel, "");
    self->button_record = ToriRSChrome_Button(chrome, self->panel, "Record...");
    enc_tip(ui, self->button_record, "every field of the selected npc's record");
    for( int f = 0; f < ENC_SET_FIELDS; f++ )
        self->input_fields[f] = ToriRSChrome_TextInput(
            chrome, self->panel, k_set_fields[f].sub[0] ? k_set_fields[f].sub : k_set_fields[f].key, "");
    enc_tip(ui, self->input_fields[3], "param=huntrange: tiles an aggressive npc sees");
    enc_tip(ui, self->input_fields[4], "param=attackrate: ticks between swings");
    enc_tip(ui, self->input_fields[5], "param=damagetype: stab, slash or crush");
    enc_tip(ui, self->input_fields[6], "combat level: it hunts players up to twice it");
    self->dropdown_huntmode = ToriRSChrome_Dropdown(chrome, self->panel, "huntmode", k_huntmodes, 3, 0);
    self->button_apply_fields = ToriRSChrome_Button(chrome, self->panel, "Apply fields");
    enc_tip(ui, self->button_apply_fields, "write the changed fields: one undo step");
    self->check_rings = ToriRSChrome_Checkbox(chrome, self->panel, "Show rings", 1);
    enc_tip(ui, self->check_rings, "each set's hunt and wander rings in its colour");
    self->row_rings = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_hunts = ToriRSChrome_Label(chrome, self->panel, "");

    /* Stats */
    ToriRSChrome_PanelBeginTab(chrome, self->panel, ENC_TAB_STATS);
    self->row_ledger = ToriRSChrome_Label(chrome, self->panel, "");
    for( int i = 0; i < EDITOR_NPCSTATS_LEVELS; i++ )
        self->input_levels[i] = ToriRSChrome_TextInput(chrome, self->panel, Editor_NpcStatsLevelKey(i), "");
    for( int i = 0; i < EDITOR_NPCSTATS_BONUSES; i++ )
        self->input_bonuses[i] = ToriRSChrome_TextInput(chrome, self->panel, Editor_NpcStatsBonusKey(i), "");
    self->button_apply_stats = ToriRSChrome_Button(chrome, self->panel, "Apply stats");
    enc_tip(ui, self->button_apply_stats, "the ledger (source = authored) and the record");
    self->row_melee = ToriRSChrome_Label(chrome, self->panel, "");
    enc_tip(ui, self->row_melee, EDITOR_NPC_MELEE_MAXHIT_TIP);
    self->row_ranged = ToriRSChrome_Label(chrome, self->panel, "");
    enc_tip(ui, self->row_ranged, EDITOR_NPC_RANGED_MAXHIT_TIP);
    self->row_magic = ToriRSChrome_Label(chrome, self->panel, "");
    enc_tip(ui, self->row_magic, EDITOR_NPC_MAGIC_MAXHIT_TIP);
    self->row_level = ToriRSChrome_Label(chrome, self->panel, "");
    enc_tip(ui, self->row_level, EDITOR_NPC_COMBAT_TIP);

    /* Combat */
    ToriRSChrome_PanelBeginTab(chrome, self->panel, ENC_TAB_COMBAT);
    {
        static const char* templates[EDITOR_COMBAT_TEMPLATE_COUNT];
        for( int t = 0; t < EDITOR_COMBAT_TEMPLATE_COUNT; t++ )
            templates[t] = Editor_CombatTemplateName((enum Editor_CombatTemplate)t);
        self->dropdown_template =
            ToriRSChrome_Dropdown(chrome, self->panel, "Template", templates, EDITOR_COMBAT_TEMPLATE_COUNT, 0);
    }
    enc_tip(ui, self->dropdown_template, "caster copies npc/scripts/dark_wizard.rs2's swing");
    self->row_combat = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_combat_file = ToriRSChrome_Label(chrome, self->panel, "");
    self->button_open_builder = ToriRSChrome_Button(chrome, self->panel, "Open in step builder");
    ToriRSChrome_ButtonSetFill(chrome, self->button_open_builder, 1);
    enc_tip(ui, self->button_open_builder, "the template as the npc's [ai_opplayer2] steps");

    /* Drops */
    ToriRSChrome_PanelBeginTab(chrome, self->panel, ENC_TAB_DROPS);
    self->row_drops = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_drop_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    self->list_drops = ToriRSChrome_VList(chrome, self->panel, "", ENC_DROP_ROWS, 0);
    assert(self->list_drops >= 0);
    self->button_drops_read = ToriRSChrome_Button(chrome, self->panel, "Read again");
    enc_tip(ui, self->button_drops_read, "read the npc's [ai_queue3] script again");

    /* Test */
    ToriRSChrome_PanelBeginTab(chrome, self->panel, ENC_TAB_TEST);
    self->button_run = ToriRSChrome_Button(chrome, self->panel, "RUN");
    enc_tip(ui, self->button_run, "save all, build, reload the lane");
    edp_button_chord(ui, self->button_run, EDITOR_HK_FILE_RUN);
    self->button_place = ToriRSChrome_Button(chrome, self->panel, "Place player at set");
    enc_tip(ui, self->button_place, "the play account to the set's centre");
    edp_button_chord(ui, self->button_place, EDITOR_HK_TOOL_PLACE_PLAYER);
    self->check_follow = ToriRSChrome_Checkbox(chrome, self->panel, "Follow the play account", 0);
    enc_tip(ui, self->check_follow, "the World's camera tracks where it stands");
    self->row_log = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_log_find = ToriRSChrome_TextInput(chrome, self->panel, "Find", "");
    self->list_log = ToriRSChrome_VList(chrome, self->panel, "", ENC_LOG_ROWS, 0);
    assert(self->list_log >= 0);

    ToriRSChrome_PanelBeginTab(chrome, self->panel, -1);
    self->row_status = ToriRSChrome_Label(chrome, self->panel, "");
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "encounter";
    desc.title = "Encounters";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = enc_tick;
    desc.activate = enc_activate;
    desc.set_visible = enc_set_visible;
    desc.destroy = enc_destroy;
    Editor_UiAddPanel(ui, &desc);
}

/* ---- entry points ------------------------------------------------------------------- */

int
Editor_Encounter_GroupSelection(struct EditorUi* ui)
{
    struct EncounterPanel* self;

    assert(ui);
    self = enc_self(ui);
    assert(self);
    Editor_UiPanelOpenBeside(ui, "encounter", "spawns");
    self->wanted = 1;
    ToriRSChrome_PanelSetActiveTab(ui->chrome, self->panel, ENC_TAB_SET);
    return enc_add_selection(ui, self);
}

int
Editor_Encounter_Show(
    struct EditorUi* ui,
    const char* name)
{
    struct EncounterPanel* self;

    assert(ui);
    assert(name);
    self = enc_self(ui);
    assert(self);
    (void)enc_refresh(ui, self);
    if( Editor_EncountersFind(&self->file, name) < 0 )
        return 0;
    enc_select_set(self, name);
    if( !ui->chrome->panels[self->panel].visible )
        Editor_UiPanelOpenBeside(ui, "encounter", "spawns");
    self->wanted = 1;
    return 1;
}

int
Editor_Encounter_Rings(
    struct EditorUi* ui,
    struct Editor_EncounterRing* out,
    int max)
{
    struct EncounterPanel* self;
    int count = 0;

    assert(ui);
    assert(out || max == 0);
    self = enc_self(ui);
    if( !self || !ui->chrome->panels[self->panel].visible || !ToriRSChrome_Checked(ui->chrome, self->check_rings) )
        return 0;
    for( int n = 0; n < Editor_EncountersCount(&self->file); n++ )
    {
        const struct Editor_Encounter* set = Editor_EncountersNth(&self->file, n);
        uint32_t const rgb = k_ring_rgb[Editor_EncountersColour(&self->file, n)];
        for( int i = 0; i < set->spawn_count && count < max; i++ )
        {
            const struct Editor_EncounterSpawn* s = &set->spawns[i];
            const struct EncRecord* r = NULL;
            int hunt;
            int wander;

            if( s->is_obj )
                continue;
            /* Rings only for npcs whose record the panel holds (the shown
             * set's, and any looked at): no catalog request from a draw. */
            for( int k = 0; k < ENC_RECORDS_MAX && !r; k++ )
                if( self->records[k].used && strcmp(self->records[k].name, s->name) == 0 )
                    r = &self->records[k];
            if( !r )
                continue;
            wander = enc_value_int(r, "wanderrange", "", MAP_EDITOR_WANDER_DEFAULT);
            hunt = enc_value_int(r, "param", "huntrange", -1);
            if( wander > MAP_EDITOR_WANDER_RANGE_MAX )
                wander = MAP_EDITOR_WANDER_RANGE_MAX;
            if( hunt > MAP_EDITOR_WANDER_RANGE_MAX )
                hunt = MAP_EDITOR_WANDER_RANGE_MAX;
            out[count].x0 = s->x - wander;
            out[count].z0 = s->z - wander;
            out[count].x1 = s->x + wander;
            out[count].z1 = s->z + wander;
            out[count].level = s->level;
            out[count].hunt = 0;
            out[count].rgb = rgb;
            out[count].current = strcmp(set->name, self->set_name) == 0;
            count++;
            if( hunt >= 0 && count < max )
            {
                out[count] = out[count - 1];
                out[count].x0 = s->x - hunt;
                out[count].z0 = s->z - hunt;
                out[count].x1 = s->x + hunt;
                out[count].z1 = s->z + hunt;
                out[count].hunt = 1;
                count++;
            }
        }
    }
    return count;
}

int
Editor_Encounter_Widgets(
    struct EditorUi* ui,
    struct Editor_EncounterWidgets* out)
{
    struct EncounterPanel* self;

    assert(ui);
    assert(out);
    self = enc_self(ui);
    if( !self )
        return 0;
    out->panel = self->panel;
    out->tabs = self->tabs;
    out->row_title = self->row_title;
    out->dropdown_set = self->dropdown_set;
    out->input_name = self->input_name;
    out->button_new = self->button_new;
    out->button_save = self->button_save;
    out->input_spawn = self->input_spawn;
    out->button_add_name = self->button_add_name;
    out->button_add_selection = self->button_add_selection;
    out->dropdown_colour = self->dropdown_colour;
    out->list_spawns = self->list_spawns;
    out->row_selected = self->row_selected;
    for( int f = 0; f < ENC_SET_FIELDS; f++ )
        out->input_fields[f] = self->input_fields[f];
    out->dropdown_huntmode = self->dropdown_huntmode;
    out->button_apply_fields = self->button_apply_fields;
    out->row_rings = self->row_rings;
    out->row_hunts = self->row_hunts;
    out->row_ledger = self->row_ledger;
    for( int i = 0; i < EDITOR_NPCSTATS_LEVELS; i++ )
        out->input_levels[i] = self->input_levels[i];
    for( int i = 0; i < EDITOR_NPCSTATS_BONUSES; i++ )
        out->input_bonuses[i] = self->input_bonuses[i];
    out->button_apply_stats = self->button_apply_stats;
    out->row_melee = self->row_melee;
    out->row_ranged = self->row_ranged;
    out->row_magic = self->row_magic;
    out->row_level = self->row_level;
    out->dropdown_template = self->dropdown_template;
    out->row_combat = self->row_combat;
    out->row_combat_file = self->row_combat_file;
    out->button_open_builder = self->button_open_builder;
    out->row_drops = self->row_drops;
    out->list_drops = self->list_drops;
    out->button_run = self->button_run;
    out->button_place = self->button_place;
    out->row_log = self->row_log;
    out->list_log = self->list_log;
    out->row_status = self->row_status;
    return 1;
}
