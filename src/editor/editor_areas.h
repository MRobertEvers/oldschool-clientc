#ifndef SRC_EDITOR_EDITOR_AREAS_H
#define SRC_EDITOR_EDITOR_AREAS_H

/**
 * The AREAS document (EDITOR_DOC_AREAS, key "areas"): named areas of the
 * world and named collections of squares, shared by everyone who edits the
 * content tree.
 *
 * WHERE IT LIVES: `<content_dir>/editor/areas.txt`.
 *
 *   - In the content tree, not in the device's preferences, because the
 *     names are the team's: "Lumbridge castle" means the same tiles to every
 *     window and every person who checks the tree out, and it is versioned
 *     and reviewed with the content it names.
 *   - Under its own `editor/` directory because nothing that builds the game
 *     reads it: sscompile reads server/scripts, cachepack reads configs,
 *     maps, interfaces and the asset trees, the catalog indexes those. A file
 *     no pack step reads cannot change a pack, so naming an area is never a
 *     content change and can never break a build.
 *   - One file, because the set is small (dozens of rows) and edited by
 *     everyone; one record per line, so a diff and a merge are line-wise.
 *
 *     # ToriRSMapEd named areas and square collections (editor/areas.txt).
 *     # area <name> | level <0..3|all> | rects <x0> <z0> <x1> <z1>[, ...] [| colour <0..7>] [| note <text>]
 *     # collection <name> | squares <key> ... [| note <text>]
 *     area Lumbridge castle | level 0 | rects 3205 3208 3225 3226 | colour 2 | note The castle and its yard
 *     collection Lumbridge | squares m50_50 m50_51
 *
 * GRAMMAR. A line is a comment (`#` or `//` first), blank, or one record:
 * the keyword (`area` / `collection`), the name, then `|`-separated
 * segments in any order, each at most once. Rectangles are ABSOLUTE world
 * tiles, inclusive on both corners; `level all` (stored -1) spans every
 * plane. A square key is the picker's: "m50_50", or with its storage
 * "ported/<lane>/maps/m16_101". A name is unique per kind, holds no `|`,
 * does not start or end with a space; a note holds no `|`.
 *
 * LINE-PRESERVING, like editor_spawn.c: every line keeps its own bytes, so
 * parse -> emit is byte-identical (editor_areas_test), a save touches only
 * the edited rows, and an undo puts a row back byte for byte (a command
 * carries the old line's exact text). Rows the editor writes are formatted
 * by Editor_AreaFormat / Editor_CollectionFormat. The first record added to
 * an empty file brings the three header lines above with it, and removing
 * the last record of a file that is exactly that header takes them back, so
 * an add and its undo leave an absent file absent.
 *
 * COMMANDS (editor_cmd.h): AREA_SET / COLLECTION_SET upsert a record by
 * name, carrying the old line for undo (a rename is a SET whose before and
 * after names differ); AREA_REMOVE / COLLECTION_REMOVE carry the removed
 * line and the index it stood at, so an undo puts it back there.
 *
 * Pure: no document, no content tree, no host. The controller
 * (torirsmapedctl) links it to format and parse rows; editor_cmd.c links it
 * to apply AREA_* / COLLECTION_* to an AREAS text document. Malformed text
 * is data (the parser reports the line); NULL arguments assert.
 */

#include "editor_cmd.h"

#include <stddef.h>

#define EDITOR_AREAS_DOC_KEY "areas"
/** Relative to the content dir (editor_host_local.c Editor_HostDocPath). */
#define EDITOR_AREAS_FILE_PATH "editor/areas.txt"

/** Bytes of a name / note, the terminator included. */
#define EDITOR_AREA_NAME_MAX 64
#define EDITOR_AREA_NOTE_MAX 160
/** Rectangles one area holds. */
#define EDITOR_AREA_RECTS_MAX 16
/** The palette an area's colour indexes; -1 = by its place in the file. */
#define EDITOR_AREA_COLOURS 8
/** Squares one collection holds, and the bytes of one key. */
#define EDITOR_COLLECTION_SQUARES_MAX 64
#define EDITOR_COLLECTION_KEY_MAX 64
/** Bytes of one record line, the terminator excluded. Two of them (the line
 *  before and after an edit) ride in one command, EDITOR_CMD_SPLICE_MAX. */
#define EDITOR_AREAS_LINE_MAX (EDITOR_CMD_SPLICE_MAX / 2 - 1)
/** Tiles are 0..EDITOR_AREA_TILE_MAX. */
#define EDITOR_AREA_TILE_MAX 65535

enum Editor_AreasLineKind
{
    /** Blank or a comment: carried, never interpreted. */
    EDITOR_AREAS_LINE_TEXT = 0,
    EDITOR_AREAS_LINE_AREA,
    EDITOR_AREAS_LINE_COLLECTION,
};

/** Absolute world tiles, inclusive, x0 <= x1 and z0 <= z1. */
struct Editor_AreaRect
{
    int x0;
    int z0;
    int x1;
    int z1;
};

struct Editor_Area
{
    char name[EDITOR_AREA_NAME_MAX];
    /** 0..3, or -1: every level. */
    int level;
    /** 0..EDITOR_AREA_COLOURS-1, or -1: by its place in the file. */
    int colour;
    int rect_count;
    struct Editor_AreaRect rects[EDITOR_AREA_RECTS_MAX];
    char note[EDITOR_AREA_NOTE_MAX];
};

struct Editor_Collection
{
    char name[EDITOR_AREA_NAME_MAX];
    int square_count;
    char squares[EDITOR_COLLECTION_SQUARES_MAX][EDITOR_COLLECTION_KEY_MAX];
    char note[EDITOR_AREA_NOTE_MAX];
};

struct Editor_AreasLine
{
    enum Editor_AreasLineKind kind;
    /** Owned, NUL-terminated: the line's bytes without its newline. */
    char* text;
    /** Owned; exactly one is set for a record line, neither for TEXT. */
    struct Editor_Area* area;
    struct Editor_Collection* collection;
};

struct Editor_AreasFile
{
    struct Editor_AreasLine* lines;
    int line_count;
    int line_capacity;
    /** The text ended with a newline. */
    int final_newline;
};

struct Editor_AreasParseResult
{
    int ok;
    /** 1-based line of the first failure, 0 when ok. */
    int line;
};

/* ---- the file ---------------------------------------------------------- */

void
Editor_AreasInit(struct Editor_AreasFile* file);

/** Frees every line and zeroes the file. NULL-tolerant. */
void
Editor_AreasFree(struct Editor_AreasFile* file);

/** Parse, replacing what `file` held. NULL with length 0 is an empty file
 *  (no areas named yet). A duplicate name of the same kind is a failure. */
struct Editor_AreasParseResult
Editor_AreasParse(
    struct Editor_AreasFile* file,
    const char* text,
    size_t length);

/** snprintf's sizing contract. */
size_t
Editor_AreasEmit(
    const struct Editor_AreasFile* file,
    char* out,
    size_t out_capacity);

/** Line index of the area / collection named `name`, or -1. */
int
Editor_AreasFindArea(
    const struct Editor_AreasFile* file,
    const char* name);

int
Editor_AreasFindCollection(
    const struct Editor_AreasFile* file,
    const char* name);

/** Records of each kind, and the nth of them (its line index), -1 past the
 *  end. */
int
Editor_AreasAreaCount(const struct Editor_AreasFile* file);

int
Editor_AreasCollectionCount(const struct Editor_AreasFile* file);

int
Editor_AreasNthArea(
    const struct Editor_AreasFile* file,
    int nth);

int
Editor_AreasNthCollection(
    const struct Editor_AreasFile* file,
    int nth);

/* ---- records ------------------------------------------------------------ */

/** A name the grammar takes: 1..63 printable bytes, no `|`, no leading or
 *  trailing space, not starting with `#` or `//`. */
int
Editor_AreaNameValid(const char* name);

/** A note the grammar takes: < EDITOR_AREA_NOTE_MAX printable bytes, no `|`. */
int
Editor_AreaNoteValid(const char* note);

/** A square key: "m<x>_<z>" or "<storage>/m<x>_<z>" (Editor_DocSplitSquareKey's
 *  shapes), under EDITOR_COLLECTION_KEY_MAX, no space. */
int
Editor_CollectionKeyValid(const char* key);

/** Every field in range and the formatted row within EDITOR_AREAS_LINE_MAX. */
int
Editor_AreaValid(const struct Editor_Area* area);

int
Editor_CollectionValid(const struct Editor_Collection* collection);

/** The row the editor writes. snprintf's contract; returns the length. */
int
Editor_AreaFormat(
    const struct Editor_Area* area,
    char* out,
    size_t out_capacity);

int
Editor_CollectionFormat(
    const struct Editor_Collection* collection,
    char* out,
    size_t out_capacity);

/** Parse one line on its own. Returns the kind (TEXT for a blank or comment
 *  line), or -1 when it does not parse. `out_area` / `out_collection` (may
 *  be NULL) receive the record. */
int
Editor_AreasParseLine(
    const char* text,
    struct Editor_Area* out_area,
    struct Editor_Collection* out_collection);

/** Add a rectangle (normalised). 0 when full or out of range. */
int
Editor_AreaAddRect(
    struct Editor_Area* area,
    int x0,
    int z0,
    int x1,
    int z1);

/** The tile at the middle of the bounding box of every rectangle. */
void
Editor_AreaCentre(
    const struct Editor_Area* area,
    int* out_x,
    int* out_z);

/** Index of `key` in the collection, or -1. */
int
Editor_CollectionFind(
    const struct Editor_Collection* collection,
    const char* key);

/** Append `key`. 1 when added, 0 when it is already there, invalid, or the
 *  collection is full (or its row would pass EDITOR_AREAS_LINE_MAX). */
int
Editor_CollectionAdd(
    struct Editor_Collection* collection,
    const char* key);

/** 1 when `key` was there and is gone. */
int
Editor_CollectionRemove(
    struct Editor_Collection* collection,
    const char* key);

/** The colour index an area draws with: its own, else its place among the
 *  file's areas (nth). */
int
Editor_AreaColourIndex(
    const struct Editor_Area* area,
    int nth);

/** 0xRRGGBB of palette entry `index` (wrapped into the palette). */
uint32_t
Editor_AreaPaletteRgb(int index);

/* ---- commands ------------------------------------------------------------ */

/**
 * AREA_SET on the AREAS document: `area` becomes the record named
 * `old_name` (NULL or "" = a new record). Fills `out` and returns 1, or 0
 * (nothing written) when the area is not valid, `old_name` names no area,
 * a new name is already taken, or nothing would change.
 */
int
Editor_AreaSetCommand(
    struct Editor_Cmd* out,
    const struct Editor_AreasFile* file,
    const char* old_name,
    const struct Editor_Area* area);

/** AREA_REMOVE of `name`. 0 when there is no such area. */
int
Editor_AreaRemoveCommand(
    struct Editor_Cmd* out,
    const struct Editor_AreasFile* file,
    const char* name);

int
Editor_CollectionSetCommand(
    struct Editor_Cmd* out,
    const struct Editor_AreasFile* file,
    const char* old_name,
    const struct Editor_Collection* collection);

int
Editor_CollectionRemoveCommand(
    struct Editor_Cmd* out,
    const struct Editor_AreasFile* file,
    const char* name);

/** The command's before / after line ("" when it has none). Writes at most
 *  `out_size` bytes, NUL-terminated. */
void
Editor_AreasCmdLine(
    const struct Editor_Cmd* command,
    int after,
    char* out,
    size_t out_size);

/**
 * Apply an AREA_* / COLLECTION_* command to `file`, forward or (`inverse`)
 * backward. Returns 1 when the file changed; 0 when it does not fit: the
 * record to rewrite or remove is not there with exactly the command's bytes,
 * or the name to add is taken.
 */
int
Editor_AreasApply(
    struct Editor_AreasFile* file,
    const struct Editor_Cmd* command,
    int inverse);

/** The History row for one AREA_* / COLLECTION_* command: "Mark area
 *  Lumbridge castle", "Rename area A to B", "Delete collection C". */
void
Editor_AreasCmdLabel(
    const struct Editor_Cmd* command,
    char* out,
    size_t out_size);

#endif
