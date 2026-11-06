#ifndef SRC_EDITOR_EDITOR_ENCOUNTERS_H
#define SRC_EDITOR_EDITOR_ENCOUNTERS_H

/**
 * ENCOUNTERS (workbench 6.7, design #encounter "Encounter builder"): named
 * sets of spawns -- a goblin camp, a guard post -- that the encounter builder
 * tunes together, draws as rings in the World and drops the play account
 * into. A set is not a new kind of content: its spawns are rows the .spawn
 * files already hold, its numbers are the npc records' fields and the stats
 * ledger. The document only says which spawns belong together and how to
 * colour them.
 *
 * WHERE IT LIVES: `<content_dir>/editor/encounters.txt` (TEXT document key
 * "editor:encounters.txt"), beside the AREAS, PALETTES and STAMPS documents
 * and for their reasons: the team's names, versioned with the content they
 * name, and outside every pack input (nothing that builds the game reads
 * `editor/`), so naming a set can never change a pack.
 *
 *     # ToriRSMapEd encounter sets (editor/encounters.txt).
 *     # encounter <name> [| colour <0..7>] [| spawn <npc|obj>:<name> <x> <z> <level> [<count>]]... [| note <text>]
 *     encounter goblin_camp_east | colour 2 | spawn npc:goblin_unarmed_melee_4 3246 3237 0 | note by the river
 *
 * GRAMMAR. A line is a comment (`#` or `//` first), blank, or one record: the
 * keyword, the name ([A-Za-z0-9_-], under 48 bytes), then `|`-separated
 * segments in any order: `colour` and `note` at most once, `spawn` as often
 * as the set has spawns (at most EDITOR_ENCOUNTER_SPAWNS_MAX, no row twice).
 * A spawn's tile is ABSOLUTE, as the .spawn file states it; its count is an
 * obj stack (1 when absent). A note holds no `|`. Names are unique.
 *
 * LINE-PRESERVING, like editor_areas.c and editor_stamps.c: every line keeps
 * its own bytes, so parse -> emit is byte-identical (comments, blank lines,
 * CRLF, a missing final newline). A verb (set, rename, remove) is the whole
 * new text of the document planned from the old (Editor_EncountersTextWith /
 * Without) and written as TEXT_SPLICE commands (Editor_StampSplices, the
 * common head and tail kept), ONE undo step; rows the editor writes are
 * Editor_EncounterFormat's. The first record added to an empty document
 * brings the two header lines above with it, and removing the last record of
 * a document that is exactly that header takes them back, so an add and its
 * undo leave an absent file absent.
 *
 * Also here, because the encounter builder is their only reader and each is
 * pure text -> rows: the Drops tab's reader of an npc's death-drop script
 * (Editor_DropsRead) and the Combat tab's ai templates
 * (Editor_CombatTemplateBody).
 *
 * Pure: no document, no host. Malformed text is data (the parser reports the
 * line); NULL arguments assert.
 */

#include "editor_cmd.h"

#include <stddef.h>

/** The TEXT document key, and its path relative to the content dir
 *  (Editor_HostDocPath maps "editor:<file>" to "editor/<file>"). */
#define EDITOR_ENCOUNTERS_DOC_KEY "editor:encounters.txt"
#define EDITOR_ENCOUNTERS_FILE_PATH "editor/encounters.txt"

/** Bytes of a set's name / note, the terminator included. */
#define EDITOR_ENCOUNTER_NAME_MAX 48
#define EDITOR_ENCOUNTER_NOTE_MAX 160
/** Spawns one set holds. */
#define EDITOR_ENCOUNTER_SPAWNS_MAX 64
/** The ring palette a set's colour indexes; -1 = by its place in the file. */
#define EDITOR_ENCOUNTER_COLOURS 8
/** Tiles are 0..EDITOR_ENCOUNTER_TILE_MAX. */
#define EDITOR_ENCOUNTER_TILE_MAX 65535

struct Editor_EncounterSpawn
{
    /** 1 for an obj spawn, 0 for an npc. */
    int is_obj;
    /** The content name the .spawn row states (a pack symbol). */
    char name[EDITOR_CMD_NAME_MAX];
    int x;
    int z;
    int level;
    /** The obj stack size; 1 when the row states none. */
    int count;
};

struct Editor_Encounter
{
    char name[EDITOR_ENCOUNTER_NAME_MAX];
    /** 0..EDITOR_ENCOUNTER_COLOURS-1, or -1 (by its place in the file). */
    int colour;
    char note[EDITOR_ENCOUNTER_NOTE_MAX];
    struct Editor_EncounterSpawn spawns[EDITOR_ENCOUNTER_SPAWNS_MAX];
    int spawn_count;
};

enum Editor_EncountersLineKind
{
    /** Blank or a comment: carried, never interpreted. */
    EDITOR_ENCOUNTERS_LINE_TEXT = 0,
    EDITOR_ENCOUNTERS_LINE_RECORD,
};

struct Editor_EncountersLine
{
    enum Editor_EncountersLineKind kind;
    /** Owned, NUL-terminated: the line's bytes without its newline (a CR
     *  before the newline is kept, so CRLF round-trips). */
    char* text;
    /** RECORD: owned. */
    struct Editor_Encounter* encounter;
};

struct Editor_EncountersFile
{
    struct Editor_EncountersLine* lines;
    int line_count;
    int line_capacity;
    /** The text ended with a newline. */
    int final_newline;
};

struct Editor_EncountersParseResult
{
    int ok;
    /** 1-based line of the first failure, 0 when ok. */
    int line;
};

/* ---- names ------------------------------------------------------------- */

/** 1..47 bytes of [A-Za-z0-9_-]. */
int
Editor_EncounterNameValid(const char* name);

/** Under EDITOR_ENCOUNTER_NOTE_MAX bytes, no `|`, no line break, no leading
 *  or trailing space ("" is no note). */
int
Editor_EncounterNoteValid(const char* note);

/** A spawn's content name: 1..63 bytes of [A-Za-z0-9_]. */
int
Editor_EncounterSpawnNameValid(const char* name);

/* ---- the file ---------------------------------------------------------- */

void
Editor_EncountersInit(struct Editor_EncountersFile* file);

/** Frees every line and zeroes the file. NULL-tolerant. */
void
Editor_EncountersFree(struct Editor_EncountersFile* file);

/** Parse, replacing what `file` held. NULL with length 0 is an empty file.
 *  A malformed record or a name stated twice is a failure. */
struct Editor_EncountersParseResult
Editor_EncountersParse(
    struct Editor_EncountersFile* file,
    const char* text,
    size_t length);

/** snprintf's sizing contract. */
size_t
Editor_EncountersEmit(
    const struct Editor_EncountersFile* file,
    char* out,
    size_t out_capacity);

/** One line on its own: TEXT for a blank or comment line, RECORD, or -1
 *  when it does not parse. `out` (may be NULL) receives a record. */
int
Editor_EncountersParseLine(
    const char* text,
    struct Editor_Encounter* out);

/** The line the editor writes for `encounter`: colour, the spawns in order,
 *  the note. snprintf's contract; returns the length. */
size_t
Editor_EncounterFormat(
    const struct Editor_Encounter* encounter,
    char* out,
    size_t out_capacity);

/** Records the file holds. */
int
Editor_EncountersCount(const struct Editor_EncountersFile* file);

/** The `nth` record, NULL past the end. */
const struct Editor_Encounter*
Editor_EncountersNth(
    const struct Editor_EncountersFile* file,
    int nth);

/** Line index of the record named `name`, or -1. */
int
Editor_EncountersFind(
    const struct Editor_EncountersFile* file,
    const char* name);

/** The colour a record draws in: its own, else its place among the records
 *  modulo the palette. */
int
Editor_EncountersColour(
    const struct Editor_EncountersFile* file,
    int nth);

/* ---- one set ------------------------------------------------------------ */

/** An empty set named `name` (which must be valid), colour -1. */
void
Editor_EncounterInit(
    struct Editor_Encounter* encounter,
    const char* name);

/** 1 when the set is one the file can hold: a valid name and note, a colour
 *  in range, every spawn valid and none twice. */
int
Editor_EncounterValid(const struct Editor_Encounter* encounter);

/** Index of the spawn equal to `spawn` (kind, name, tile, level, count), or
 *  -1. */
int
Editor_EncounterFindSpawn(
    const struct Editor_Encounter* encounter,
    const struct Editor_EncounterSpawn* spawn);

/** Append a spawn. 0 when the set is full, the spawn is not valid or the
 *  set holds it already. */
int
Editor_EncounterAddSpawn(
    struct Editor_Encounter* encounter,
    const struct Editor_EncounterSpawn* spawn);

/** Remove spawn `index` (< spawn_count). */
void
Editor_EncounterRemoveSpawn(
    struct Editor_Encounter* encounter,
    int index);

/** The set's centre: the mean of its spawns' tiles (rounded to the nearest
 *  tile) on the level most of them stand on (the lowest on a tie). 0 for a
 *  set with no spawns. */
int
Editor_EncounterCentre(
    const struct Editor_Encounter* encounter,
    int* out_x,
    int* out_z,
    int* out_level);

/** Distinct npc names among the set's spawns, in first-seen order, into
 *  `out` (at most `max`). Returns the count. */
int
Editor_EncounterNpcNames(
    const struct Editor_Encounter* encounter,
    char out[][EDITOR_CMD_NAME_MAX],
    int max);

/* ---- the verbs: the document's new text ---------------------------------- */

/**
 * The text of `file` with `encounter` written: in place of the record named
 * `old_name` (a rename when the names differ), or, with `old_name` NULL or
 * "", appended (the header lines first when the file holds no line at all).
 * Writes the new text into `out` (snprintf's contract) and its length into
 * `*out_length`. Returns 1, or 0 -- nothing written -- when the set is not
 * valid, `old_name` names no record, or another record holds the name.
 */
int
Editor_EncountersTextWith(
    const struct Editor_EncountersFile* file,
    const char* old_name,
    const struct Editor_Encounter* encounter,
    char* out,
    size_t out_capacity,
    size_t* out_length);

/** The text of `file` without the record `name`; a file left exactly the
 *  header lines becomes empty. 0 when there is no such record. */
int
Editor_EncountersTextWithout(
    const struct Editor_EncountersFile* file,
    const char* name,
    char* out,
    size_t out_capacity,
    size_t* out_length);

/* ---- the Drops tab: an npc's death drops as the server's script states them --
 *
 * The tree has no drop-table document: an npc's drops are RuneScript, its
 * `[ai_queue3,<npc>]` death queue (docs/NPC_DROPTABLES_TODO.md; written by
 * tools/wiki_droptable.py or by hand), either inline or through a `~proc` /
 * `@label` it calls. The reader recognises the shape that tool writes and
 * every hand-ported table shares -- unconditional `obj_add(npc_coord, <obj>,
 * <count>, ^lootdrop_duration);` lines (Always), one `def_int $dropint =
 * random(<den>);` and a cumulative `if ($dropint < <k>) { obj_add(...) } else
 * if ...` walk, the guarded `npc_param(death_drop)` restatement -- and counts
 * every other statement as unread. The rows are READ-ONLY: there is no format
 * to write them back in, and the panel says so.
 */

#define EDITOR_DROPS_MAX 64

struct Editor_DropRow
{
    /** The obj's name, or "npc_param(death_drop)" for the remains. */
    char obj[EDITOR_CMD_NAME_MAX];
    /** The count expression verbatim ("1", "calc(2 + random(3))"). */
    char count[EDITOR_CMD_NAME_MAX];
    /** 1 when dropped every death. */
    int always;
    /** The row's share of the walk: numerator / denominator (always: 1/1). */
    int numerator;
    int denominator;
};

struct Editor_DropTable
{
    struct Editor_DropRow rows[EDITOR_DROPS_MAX];
    int row_count;
    /** Statements the reader did not recognise (a condition, a call it does
     *  not follow, a quest gate): the rows are then not the whole table. */
    int unread;
    /** The block's first `~proc` / `@label` call ("" none): where the rows
     *  are when the block itself states none. */
    char call[EDITOR_CMD_NAME_MAX];
    /** The header was found. */
    int found;
};

/** Read the block headed `[<header>]` ("ai_queue3,goblin", "proc,goblin_drop",
 *  "label,goblin_drop") out of `text`, appending its rows to `out` (cleared
 *  first when `out->found` is 0 and it holds no rows). Returns 1 when the
 *  header was found. */
int
Editor_DropsRead(
    const char* text,
    size_t length,
    const char* header,
    struct Editor_DropTable* out);

/** "1/128 (0.78%)" or "always". */
void
Editor_DropChance(
    const struct Editor_DropRow* row,
    char* out,
    size_t out_size);

/* ---- the Combat tab: the ai_* templates -------------------------------------- */

enum Editor_CombatTemplate
{
    /** `~npc_meleeattack;` -- the engine's default swing (combat_stats.rs2),
     *  bound explicitly so the step builder can grow it. */
    EDITOR_COMBAT_MELEE = 0,
    /** The tree's dark wizard's own `[ai_opplayer2]` body
     *  (npc/scripts/dark_wizard.rs2), read from the content tree. */
    EDITOR_COMBAT_CASTER,
    /** `~npc_rangeattack;` -- the generic, param-driven ranged swing
     *  (npc_combat_ranged.rs2; docs/NPC_WIKI_STATS_PLAN.md 0.4). */
    EDITOR_COMBAT_RANGER,
    /** The default swing, then `npc_statheal(hitpoints, 2, 0);`: a starting
     *  point that mends itself, for the step builder to shape. */
    EDITOR_COMBAT_HEALER,
    EDITOR_COMBAT_TEMPLATE_COUNT
};

/** Where the caster template is read from, under server/scripts. */
#define EDITOR_COMBAT_CASTER_SOURCE "npc/scripts/dark_wizard.rs2"

/** "melee", "caster", "ranger", "healer". */
const char*
Editor_CombatTemplateName(enum Editor_CombatTemplate template_kind);

/**
 * The template's trigger (`out_trigger`, e.g. "ai_opplayer2") and body: the
 * statements, one per line, that go under `[<trigger>,<npc>]`. The caster's
 * come from `caster_source` (the text of EDITOR_COMBAT_CASTER_SOURCE; may be
 * NULL with length 0 for the others): the body of its first `[ai_opplayer2,
 * ...]` or `[ai_applayer2,...]` block, verbatim, its trigger kept. Returns
 * the body's length (snprintf's contract), or -1 when the caster source is
 * absent or has no such block.
 */
int
Editor_CombatTemplateBody(
    enum Editor_CombatTemplate template_kind,
    const char* caster_source,
    size_t caster_length,
    char out_trigger[32],
    char* out,
    size_t out_capacity);

#endif
