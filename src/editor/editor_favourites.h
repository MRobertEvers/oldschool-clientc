#ifndef SRC_EDITOR_EDITOR_FAVOURITES_H
#define SRC_EDITOR_EDITOR_FAVOURITES_H

/**
 * The FAVOURITES document (workbench 6.2, design section 6 "Shortcuts over
 * the index"): the catalog records one builder reaches for again and again,
 * starred from the Catalog's pane and listed by its "favourites" chip.
 *
 * WHERE IT LIVES: `<content_dir>/editor/favourites.txt`, beside the AREAS
 * and PALETTES documents (editor_areas.h, editor_palette.h), which nothing
 * that builds the game reads. Unlike those two it is ONE PERSON'S: a palette
 * is the team's strip of records for a place, a favourite is what you keep
 * at hand, so a tree that is shared keeps the file out of its history
 * (its own ignore rule) and every builder has their own.
 *
 *     # ToriRSMapEd favourites (editor/favourites.txt): yours, not the team's.
 *     # favourite <loc|npc|obj>:<record> [| shape <0..22> rot <0..3>]
 *     favourite loc:fence_wood | shape 0 rot 1
 *     favourite npc:hans
 *
 * GRAMMAR. A line is a comment (`#` or `//` first), blank, or one record:
 * `favourite`, the record `<kind>:<name>` (the name the catalog knows), and
 * for a loc optionally `| shape S rot R`, the pose Place stamps it at. One
 * line per record: a second favourite of the same kind and name is a
 * failure.
 *
 * A TEXT DOCUMENT, NOT A NEW KIND: the mirror holds it as EDITOR_DOC_TEXT
 * under key "editor:favourites.txt", and every edit is one TEXT_SPLICE
 * through the authority (Editor_FavouritesAddCommand / RemoveCommand plan
 * it), so the server, the wire and the undo stack need nothing new: an undo
 * is the splice's own inverse, byte for byte. LINE-PRESERVING, like
 * editor_areas.c: parse -> emit is byte-identical (comments, blank lines,
 * CRLF, a missing final newline), an add appends one line (the header lines
 * with it into an empty file), a remove takes exactly its line's bytes, and
 * re-posing a favourite rewrites its line in place.
 *
 * Pure: no document, no content tree, no host. Malformed text is data (the
 * parser reports the line); NULL arguments assert.
 */

#include "editor_cmd.h"

#include <stddef.h>

#define EDITOR_FAVOURITES_DOC_KEY "editor:favourites.txt"
/** Relative to the content dir (editor_host_local.c Editor_HostDocPath). */
#define EDITOR_FAVOURITES_FILE_PATH "editor/favourites.txt"
/** Bytes of a record name, the terminator included. */
#define EDITOR_FAVOURITE_RECORD_MAX 64
/** The header lines an empty file gains with its first favourite. */
#define EDITOR_FAVOURITES_HEADER                                                                   \
    "# ToriRSMapEd favourites (editor/favourites.txt): yours, not the team's.\n"                  \
    "# favourite <loc|npc|obj>:<record> [| shape <0..22> rot <0..3>]\n"

struct Editor_FavouriteRow
{
    /** enum Editor_PaletteKind: 0 loc, 1 npc, 2 obj (the catalog's order). */
    int kind;
    char record[EDITOR_FAVOURITE_RECORD_MAX];
    /** A loc's pose: shape 0..22 and rotation 0..3, or shape -1 (rotation
     *  0) for none. */
    int shape;
    int rotation;
};

struct Editor_FavouritesLine
{
    /** 1 for a favourite, 0 for a blank or comment line (carried as it is). */
    int is_row;
    struct Editor_FavouriteRow row;
    /** The line's bytes in the parsed text, its newline (CR LF or LF)
     *  included: what a remove splices out. */
    size_t offset;
    size_t length;
};

struct Editor_FavouritesFile
{
    /** Owned. */
    struct Editor_FavouritesLine* lines;
    int line_count;
    int line_capacity;
    /** Owned copy of the text parsed, NUL-terminated. */
    char* text;
    size_t length;
};

struct Editor_FavouritesParseResult
{
    int ok;
    /** 1-based line of the first failure, 0 when ok. */
    int line;
};

void
Editor_FavouritesInit(struct Editor_FavouritesFile* file);

/** NULL-tolerant. */
void
Editor_FavouritesFree(struct Editor_FavouritesFile* file);

/** Parse, replacing what `file` held. NULL with length 0 is an empty file. */
struct Editor_FavouritesParseResult
Editor_FavouritesParse(
    struct Editor_FavouritesFile* file,
    const char* text,
    size_t length);

/** The text back, byte for byte. snprintf's sizing contract. */
size_t
Editor_FavouritesEmit(
    const struct Editor_FavouritesFile* file,
    char* out,
    size_t out_capacity);

/** Favourites the file holds. */
int
Editor_FavouritesCount(const struct Editor_FavouritesFile* file);

/** The `nth` favourite, in file order. */
const struct Editor_FavouriteRow*
Editor_FavouritesAt(
    const struct Editor_FavouritesFile* file,
    int nth);

/** Line index of the favourite (kind, record), -1 when it is not one. */
int
Editor_FavouritesFind(
    const struct Editor_FavouritesFile* file,
    int kind,
    const char* record);

/** Parse one line on its own (no newline): 1 a favourite into `out_row`
 *  (may be NULL), 0 a blank or comment line, -1 when it does not parse. */
int
Editor_FavouritesParseLine(
    const char* text,
    size_t length,
    struct Editor_FavouriteRow* out_row);

/** The line the editor writes, no newline. snprintf's contract. */
int
Editor_FavouriteFormat(
    const struct Editor_FavouriteRow* row,
    char* out,
    size_t out_capacity);

/** Every field in range (a pose only on a loc) and a record name the
 *  grammar takes. */
int
Editor_FavouriteRowValid(const struct Editor_FavouriteRow* row);

/**
 * The TEXT_SPLICE that makes `row` a favourite of the file `file` parsed:
 * a new line at the end (the header lines first into an empty file, a
 * newline first after a last line that has none), or -- for a record that is
 * a favourite already with another pose -- its line rewritten in place.
 * Returns 1 and `out`, 0 when it is a favourite already with that pose, the
 * row is not valid, or the splice would not fit EDITOR_CMD_SPLICE_MAX.
 */
int
Editor_FavouritesAddCommand(
    struct Editor_Cmd* out,
    const struct Editor_FavouritesFile* file,
    const struct Editor_FavouriteRow* row);

/** The TEXT_SPLICE that takes favourite (kind, record) out, its newline
 *  with it. 0 when it is not a favourite. */
int
Editor_FavouritesRemoveCommand(
    struct Editor_Cmd* out,
    const struct Editor_FavouritesFile* file,
    int kind,
    const char* record);

#endif
