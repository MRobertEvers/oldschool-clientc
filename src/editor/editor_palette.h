#ifndef SRC_EDITOR_EDITOR_PALETTE_H
#define SRC_EDITOR_EDITOR_PALETTE_H

/**
 * The PALETTES document (EDITOR_DOC_PALETTES, key "palettes"): named strips
 * of up to ten catalog records -- locs, npcs, objs -- to switch between
 * quickly while building (the Catalog palette panel,
 * panels/edpanel_catalog_palette.c), shared by everyone who edits the content
 * tree.
 *
 * WHERE IT LIVES: `<content_dir>/editor/palettes.txt`, beside the AREAS
 * document (editor_areas.h) and for the same reasons: the team's palettes are
 * versioned and reviewed with the content they name, and nothing that builds
 * the game reads `editor/`, so a palette can never change a pack.
 *
 *     # ToriRSMapEd catalog palettes (editor/palettes.txt).
 *     # palette <name> [| note <text>]
 *     # palette <name> | slot <1..10> | <loc|npc|obj>:<record> [| shape <0..22> rot <0..3>] [| note <text>]
 *     palette Lumbridge | note The castle grounds
 *     palette Lumbridge | slot 1 | loc:oaktree
 *     palette Lumbridge | slot 2 | loc:loc_1902 | shape 0 rot 1
 *     palette Lumbridge | slot 3 | npc:hans
 *
 * GRAMMAR. A line is a comment (`#` or `//` first), blank, or one record:
 * `palette`, the palette's name, then `|`-separated segments in any order,
 * each at most once. A HEADER (no `slot`, no record) names a palette that may
 * hold nothing yet and carries its note; an ENTRY puts one record in one slot
 * (1..10). A record is `<kind>:<name>`, the name the catalog knows (a pack
 * symbol). `shape S rot R` is a loc's remembered placement pose (the Place
 * tool's shape and rotation; absent = the loc's default). A palette exists
 * while any line names it; a header is optional. Keys are unique: one header
 * per palette, one entry per (palette, slot). A name holds no `|`, does not
 * start or end with a space; a note holds no `|`.
 *
 * LINE-PRESERVING, like editor_areas.c: parse -> emit is byte-identical
 * (comments, blank lines, CRLF, a missing final newline), a save touches only
 * the edited rows, and an undo puts a row back byte for byte (a command
 * carries the old line's exact text). A new entry goes after the last line of
 * its palette (so a palette's lines stay together), a new header or the first
 * line of a new palette at the end. The first record added to an empty file
 * brings the three header lines above with it, and removing the last record
 * of a file that is exactly that header takes them back.
 *
 * COMMANDS (editor_cmd.h): PALETTE_SET upserts one line by key (has_after;
 * has_before too when it rewrites a line, whose exact text it carries -- a
 * rename or a move to another slot is a SET whose keys differ), PALETTE_REMOVE
 * carries the removed line and the index it stood at. Renaming or deleting a
 * palette is one SET / REMOVE per line in one stroke (Editor_PalettePlan*).
 * Both ride in `u.area` (`i32 line, string before, string after` on the wire).
 *
 * Pure: no document, no content tree, no host. Malformed text is data (the
 * parser reports the line); NULL arguments assert.
 */

#include "editor_cmd.h"

#include <stddef.h>

#define EDITOR_PALETTES_DOC_KEY "palettes"
/** Relative to the content dir (editor_host_local.c Editor_HostDocPath). */
#define EDITOR_PALETTES_FILE_PATH "editor/palettes.txt"

/** Slots one palette holds, numbered 1..EDITOR_PALETTE_SLOTS. */
#define EDITOR_PALETTE_SLOTS 10
/** Bytes of a palette name / record name / note, the terminator included. */
#define EDITOR_PALETTE_NAME_MAX 64
#define EDITOR_PALETTE_RECORD_MAX 64
#define EDITOR_PALETTE_NOTE_MAX 160
/** A loc's shapes are 0..EDITOR_PALETTE_SHAPE_MAX. */
#define EDITOR_PALETTE_SHAPE_MAX 22
/** Bytes of one record line, the terminator excluded: two ride in one
 *  command (EDITOR_CMD_SPLICE_MAX). */
#define EDITOR_PALETTES_LINE_MAX (EDITOR_CMD_SPLICE_MAX / 2 - 1)
/** Commands one plan (a rename, a delete, a move) may need: every line of a
 *  palette. */
#define EDITOR_PALETTE_PLAN_MAX (EDITOR_PALETTE_SLOTS + 1)

/** A record's kind, in the catalog's order (CacheProvider_CatalogKind,
 *  TORIRSMAPED_CATALOG_LOC/NPC/OBJ), so a kind is a pick kind as it is. */
enum Editor_PaletteKind
{
    EDITOR_PALETTE_LOC = 0,
    EDITOR_PALETTE_NPC = 1,
    EDITOR_PALETTE_OBJ = 2,
};

enum Editor_PalettesLineKind
{
    /** Blank or a comment: carried, never interpreted. */
    EDITOR_PALETTES_LINE_TEXT = 0,
    EDITOR_PALETTES_LINE_HEADER,
    EDITOR_PALETTES_LINE_ENTRY,
};

/** One header or entry. */
struct Editor_PaletteRow
{
    char palette[EDITOR_PALETTE_NAME_MAX];
    /** 1..EDITOR_PALETTE_SLOTS for an entry, 0 for a header. */
    int slot;
    /** An entry's enum Editor_PaletteKind; -1 for a header. */
    int kind;
    char record[EDITOR_PALETTE_RECORD_MAX];
    /** A loc entry's remembered pose: shape 0..22 and rotation 0..3, or
     *  shape -1 (rotation 0) for none. */
    int shape;
    int rotation;
    char note[EDITOR_PALETTE_NOTE_MAX];
};

struct Editor_PalettesLine
{
    enum Editor_PalettesLineKind kind;
    /** Owned, NUL-terminated: the line's bytes without its newline. */
    char* text;
    /** Owned; set for a header or an entry, NULL for TEXT. */
    struct Editor_PaletteRow* row;
};

struct Editor_PalettesFile
{
    struct Editor_PalettesLine* lines;
    int line_count;
    int line_capacity;
    /** The text ended with a newline. */
    int final_newline;
};

struct Editor_PalettesParseResult
{
    int ok;
    /** 1-based line of the first failure, 0 when ok. */
    int line;
};

/* ---- the file ---------------------------------------------------------- */

void
Editor_PalettesInit(struct Editor_PalettesFile* file);

/** Frees every line and zeroes the file. NULL-tolerant. */
void
Editor_PalettesFree(struct Editor_PalettesFile* file);

/** Parse, replacing what `file` held. NULL with length 0 is an empty file.
 *  A duplicate key (a second header of a palette, a second entry in a slot)
 *  is a failure. */
struct Editor_PalettesParseResult
Editor_PalettesParse(
    struct Editor_PalettesFile* file,
    const char* text,
    size_t length);

/** snprintf's sizing contract. */
size_t
Editor_PalettesEmit(
    const struct Editor_PalettesFile* file,
    char* out,
    size_t out_capacity);

/** `dst` becomes a copy of `src` (Free'd first). */
void
Editor_PalettesCopy(
    struct Editor_PalettesFile* dst,
    const struct Editor_PalettesFile* src);

/** Line index of palette `name`'s header / of its entry in `slot`, or -1. */
int
Editor_PalettesFindHeader(
    const struct Editor_PalettesFile* file,
    const char* name);

int
Editor_PalettesFindEntry(
    const struct Editor_PalettesFile* file,
    const char* name,
    int slot);

/** The entry of palette `name` in `slot`, NULL when the slot is empty. */
const struct Editor_PaletteRow*
Editor_PalettesEntry(
    const struct Editor_PalettesFile* file,
    const char* name,
    int slot);

/** Distinct palettes, in the order of each one's first line. */
int
Editor_PalettesCount(const struct Editor_PalettesFile* file);

/** The `nth` palette's name (a pointer into the file), NULL past the end. */
const char*
Editor_PalettesName(
    const struct Editor_PalettesFile* file,
    int nth);

/** 1 when any line names palette `name`. */
int
Editor_PalettesHas(
    const struct Editor_PalettesFile* file,
    const char* name);

/** Entries palette `name` holds. */
int
Editor_PalettesSlotCount(
    const struct Editor_PalettesFile* file,
    const char* name);

/** The lowest empty slot of palette `name` (1..EDITOR_PALETTE_SLOTS), 0 when
 *  all ten hold a record. A palette nobody named yet has every slot free. */
int
Editor_PalettesFirstFreeSlot(
    const struct Editor_PalettesFile* file,
    const char* name);

/* ---- rows ------------------------------------------------------------------ */

/** A palette name the grammar takes: 1..63 printable bytes, no `|`, no
 *  leading or trailing space, not starting with `#` or `//`. */
int
Editor_PaletteNameValid(const char* name);

/** A record name: 1..63 bytes, printable, no space, no `|`, no `:`. */
int
Editor_PaletteRecordValid(const char* record);

/** "loc" / "npc" / "obj", "?" for anything else. */
const char*
Editor_PaletteKindName(int kind);

/** Kind of "loc" / "npc" / "obj", -1 for anything else. */
int
Editor_PaletteKindFromName(const char* name);

/** A header of palette `name` (`note` may be NULL). */
void
Editor_PaletteHeaderRow(
    struct Editor_PaletteRow* out,
    const char* name,
    const char* note);

/** An entry: `record` of `kind` in `slot` of palette `name`, no pose. */
void
Editor_PaletteEntryRow(
    struct Editor_PaletteRow* out,
    const char* name,
    int slot,
    int kind,
    const char* record);

/** Every field in range (a pose only on a loc entry) and the formatted row
 *  within EDITOR_PALETTES_LINE_MAX. */
int
Editor_PaletteRowValid(const struct Editor_PaletteRow* row);

/** The line the editor writes. snprintf's contract; returns the length. */
int
Editor_PaletteFormat(
    const struct Editor_PaletteRow* row,
    char* out,
    size_t out_capacity);

/** Parse one line on its own. Returns the kind (TEXT for a blank or comment
 *  line), or -1 when it does not parse. `out_row` (may be NULL) receives a
 *  header or an entry. */
int
Editor_PalettesParseLine(
    const char* text,
    struct Editor_PaletteRow* out_row);

/* ---- commands ------------------------------------------------------------ */

/**
 * PALETTE_SET: `row` becomes line `old_line` (-1: a new line). Fills `out`
 * and returns 1, or 0 (nothing written) when the row is not valid, `old_line`
 * is not a header or entry, the row's key is held by another line, or nothing
 * would change.
 */
int
Editor_PaletteSetCommand(
    struct Editor_Cmd* out,
    const struct Editor_PalettesFile* file,
    int old_line,
    const struct Editor_PaletteRow* row);

/** PALETTE_REMOVE of header-or-entry line `line`. 0 when it is not one, or
 *  its text is longer than a command carries. */
int
Editor_PaletteRemoveCommand(
    struct Editor_Cmd* out,
    const struct Editor_PalettesFile* file,
    int line);

/**
 * The commands of one palette action, planned against a copy of `file` so
 * each holds against the file as the ones before it left it: apply them in
 * order as ONE stroke. Each returns how many it wrote into `out` (capacity
 * `capacity`, EDITOR_PALETTE_PLAN_MAX is always enough), 0 when the action
 * does not fit (no such palette or slot, a taken name, nothing to do).
 *
 *   Add     `row` (an entry) into its slot: a SET that fills an empty slot or
 *           replaces what the slot held.
 *   Rename  every line of palette `old_name` to `new_name` (taken: 0).
 *   Delete  every line of palette `name`.
 *   Move    the entry in slot `from` to slot `to`; when `to` holds an entry
 *           the two swap (REMOVE `to`, SET `from` -> `to`, SET the old `to`
 *           into `from`).
 */
int
Editor_PalettePlanAdd(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_PalettesFile* file,
    const struct Editor_PaletteRow* row);

int
Editor_PalettePlanRename(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_PalettesFile* file,
    const char* old_name,
    const char* new_name);

int
Editor_PalettePlanDelete(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_PalettesFile* file,
    const char* name);

int
Editor_PalettePlanMove(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_PalettesFile* file,
    const char* name,
    int from,
    int to);

/** The command's before / after line ("" when it has none). Writes at most
 *  `out_size` bytes, NUL-terminated. */
void
Editor_PalettesCmdLine(
    const struct Editor_Cmd* command,
    int after,
    char* out,
    size_t out_size);

/**
 * Apply a PALETTE_* command to `file`, forward or (`inverse`) backward.
 * Returns 1 when the file changed; 0 when it does not fit: the line to
 * rewrite or remove is not there with exactly the command's bytes, or the key
 * to add is held.
 */
int
Editor_PalettesApply(
    struct Editor_PalettesFile* file,
    const struct Editor_Cmd* command,
    int inverse);

/** The History row for one PALETTE_* command: "Add oaktree to palette
 *  Lumbridge slot 3", "New palette Lumbridge", "Rename palette A to B",
 *  "Move hans to slot 5 of palette A", "Remove hans from palette A slot 3",
 *  "Delete palette A". */
void
Editor_PalettesCmdLabel(
    const struct Editor_Cmd* command,
    char* out,
    size_t out_size);

#endif
