#ifndef SRC_EDITOR_EDITOR_SPAWN_H
#define SRC_EDITOR_EDITOR_SPAWN_H

/**
 * The `.spawn` text codec: where a square's npcs and ground objs stand.
 *
 *     // any comment, kept verbatim
 *     ==== NPC ====
 *     giantspider1                                  3200  3238 0
 *     -goblin_unarmed_melee_4                       3202  3253 0   // suppressed
 *
 *     ==== OBJ ====
 *     knife                                         3205  3212 0 1
 *
 * The grammar the server loader reads (torirs_server_content.c,
 * load_spawn_config): `====` banners naming NPC or OBJ, rows `name x z level
 * [count]` in absolute world tiles, `//` comments. One addition, the
 * SUPPRESSION row (docs/MAPED_DESIGN.html #placement): a leading `-` names a
 * spawn some other file makes — the generated `areas/world` one — and the
 * loader removes it once every file is merged. That is how the editor deletes
 * a generated spawn without writing into a file whose next regeneration
 * would undo it.
 *
 * The codec is LINE-preserving rather than record-normalising: every line
 * keeps its own bytes, so a file the editor parses and emits comes back byte
 * for byte (editor_spawn_test proves it over every generated file), and a
 * save touches exactly the rows that were edited. Rows the editor adds are
 * formatted the way tools/gen_spawns.py formats its own: `%-44s %5d %5d %d`.
 *
 * Malformed text is data: the parser reports the line. NULL arguments are
 * contract violations and assert.
 */

#include "editor_cmd.h"

#include <stddef.h>

enum Editor_SpawnSection
{
    EDITOR_SPAWN_SECTION_NONE = 0,
    EDITOR_SPAWN_SECTION_NPC,
    EDITOR_SPAWN_SECTION_OBJ,
};

enum Editor_SpawnLineKind
{
    /** A blank line or a `//` comment: carried, never interpreted. */
    EDITOR_SPAWN_LINE_TEXT = 0,
    /** A `====` banner. */
    EDITOR_SPAWN_LINE_HEADER,
    /** A spawn or suppression row. */
    EDITOR_SPAWN_LINE_ROW,
};

struct Editor_SpawnRow
{
    /** 1 in an OBJ section, 0 in an NPC one. */
    int is_obj;
    /** 1 for a `-name` suppression row. */
    int suppress;
    /** The content name, without the suppression `-`. */
    char name[EDITOR_CMD_NAME_MAX];
    int abs_x;
    int abs_z;
    int level;
    /** The obj stack size; 1 when the row states none. */
    int count;
};

struct Editor_SpawnLine
{
    enum Editor_SpawnLineKind kind;
    /** The section the line sits in (for a header, the one it opens). */
    enum Editor_SpawnSection section;
    /** EDITOR_SPAWN_LINE_ROW only. */
    struct Editor_SpawnRow row;
    /** Owned, NUL-terminated: the line's bytes without its newline. */
    char* text;
};

struct Editor_SpawnFile
{
    /** Owned. */
    struct Editor_SpawnLine* lines;
    int line_count;
    int line_capacity;
    /** The text ended with a newline. Emit restores exactly what it read. */
    int final_newline;
};

struct Editor_SpawnParseResult
{
    /** 1 when the whole text parsed. */
    int ok;
    /** 1-based line of the first failure, 0 when ok. */
    int line;
};

/** Zero a file. */
void
Editor_SpawnInit(struct Editor_SpawnFile* file);

/** Frees every line and zeroes the file. NULL-tolerant. */
void
Editor_SpawnFree(struct Editor_SpawnFile* file);

/**
 * Parse `.spawn` text, replacing whatever `file` held. `text` need not be
 * NUL-terminated; NULL with length 0 is an empty file (a square with no
 * edited spawns yet).
 */
struct Editor_SpawnParseResult
Editor_SpawnParse(
    struct Editor_SpawnFile* file,
    const char* text,
    size_t length);

/** Emit the file. snprintf's sizing contract: returns the full length,
 *  writes what fits, NUL-terminates whenever any byte fit. */
size_t
Editor_SpawnEmit(
    const struct Editor_SpawnFile* file,
    char* out,
    size_t out_capacity);

/** Index of the first row equal to `row` (section, suppression, name, tile,
 *  level, and count for objs), or -1. */
int
Editor_SpawnFind(
    const struct Editor_SpawnFile* file,
    const struct Editor_SpawnRow* row);

/**
 * Add a row at the end of its section, opening the section (after a blank
 * separator line) when the file has none. Returns the new line's index.
 */
int
Editor_SpawnInsert(
    struct Editor_SpawnFile* file,
    const struct Editor_SpawnRow* row);

void
Editor_SpawnRemoveAt(
    struct Editor_SpawnFile* file,
    int index);

/** Rows (spawns and suppressions) in the file. */
int
Editor_SpawnRowCount(const struct Editor_SpawnFile* file);

/** Format a row the way gen_spawns.py does. Returns the length (snprintf). */
int
Editor_SpawnFormatRow(
    const struct Editor_SpawnRow* row,
    char* out,
    size_t out_capacity);

#endif
