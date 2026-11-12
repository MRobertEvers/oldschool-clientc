#ifndef SRC_EDITOR_EDITOR_STAMPS_H
#define SRC_EDITOR_EDITOR_STAMPS_H

/**
 * STAMPS (workbench 6.3, design #placing "Stamps"): a loc selection saved
 * with its relative tiles, levels, shapes and rotations -- and, optionally,
 * the terrain tokens and the spawns under it -- to be placed again as one
 * piece. The fastest way to make a second house that matches the first.
 *
 * WHERE THEY LIVE: one file per stamp, `<content_dir>/editor/stamps/<name>.stamp`
 * (TEXT document key "stamp:<name>"), and the list of them,
 * `editor/stamps/index.txt` (key "stamps:index"), in the content tree beside
 * the AREAS and PALETTES documents and for their reasons: lane files like
 * everything else (design #scenes: a stamp made in a scratch lane publishes
 * with the lane, or dies with it), and nothing that builds the game reads
 * `editor/`.
 *
 *     # ToriRSMapEd stamp (editor/stamps/<name>.stamp).
 *     # stamp <name> [| note <text>]
 *     # loc <dx> <dz> <dlevel> <loc> <shape> <rot> [<w>x<h>]
 *     # tile <dx> <dz> <dlevel> [h<raw>] [u<id>] [o<id>/<shape>/<rot>] [f<bits>]
 *     # floor <dx> <dz> <dlevel> [o<id>/<shape>/<rot>] [f<bits>]
 *     # spawn <npc|obj> <name> <dx> <dz> <dlevel> [<count>]
 *     stamp smithy | note the forge's walls and anvil
 *     loc 0 0 0 1902 0 1
 *     loc 1 0 0 1902 0 1
 *     loc 2 1 0 2097 10 0 2x1
 *     tile 0 0 0 h12 u3
 *     spawn npc smith 1 1 0
 *
 * Offsets are tiles from the stamp's ORIGIN (the primary selected loc's
 * tile; `dlevel` from its level). A loc's `<w>x<h>` is its placed footprint
 * (1x1 when absent), what a turn of the stamp needs to keep a multi-tile loc
 * on the tiles it covered. A tile's tokens are the `.jm2` record's own (raw
 * height, underlay, overlay id / shape / rotation, flags); a bare `tile` line
 * is a tile with no tokens (the drop clears it to absent). A `floor` is what
 * a BUILDING leaves on a tile and nothing else of it (2026-11-11): its
 * overlay and its flags (the "remove roof" bit indoors), laid ON the tile it
 * lands on, whose height and underlay stay as they are -- so a house stamped
 * on a hillside brings its floorboards and not the ground it was built on,
 * which a `tile` would. The index holds header lines only, one per stamp.
 *
 * LINE-PRESERVING, like editor_palette.c: parse -> emit is byte-identical
 * (comments, blank lines, CRLF, a missing final newline). A stamp document is
 * written through the authority as TEXT_SPLICE commands planned from the old
 * text to the new (Editor_StampSplices): the common head and tail are kept,
 * so a save touches only the changed lines and an undo puts them back byte
 * for byte.
 *
 * TURNING (Q while a stamp is armed): a quarter clockwise about the origin
 * tile -- each piece's offset (x, z) -> (z, -x), a footprint's SW corner
 * following its far corner, its size swapping, every loc's rotation and every
 * overlay's rotation one quarter on.
 *
 * Pure: no document, no host. Malformed text is data (the parser reports the
 * line); NULL arguments assert. The document side (capture from a selection
 * or a rectangle, the drop's commands, the panel actions) is
 * panels/edpanel_ops_stamps.c.
 */

#include "editor_cmd.h"
#include "editor_types.h"

#include <stddef.h>

/** Relative to the content dir: `<dir>/<name>.stamp`, and the list. */
#define EDITOR_STAMPS_DIR "editor/stamps"
#define EDITOR_STAMPS_INDEX_PATH "editor/stamps/index.txt"
/** TEXT document keys. */
#define EDITOR_STAMP_KEY_PREFIX "stamp:"
#define EDITOR_STAMPS_INDEX_KEY "stamps:index"

/** Bytes of a stamp name (the terminator included): [A-Za-z0-9_-], what a
 *  file name and a document key both take. */
#define EDITOR_STAMP_NAME_MAX 48
#define EDITOR_STAMP_NOTE_MAX 160
/** Bytes of one formatted line, the terminator excluded. */
#define EDITOR_STAMP_LINE_MAX 255
/** Pieces one stamp holds: a 64 x 64 rectangle of tiles and a full
 *  selection of locs. */
#define EDITOR_STAMP_PIECES_MAX (64 * 64 + 256)
/** Offsets reach this far from the origin (a square and its neighbours). */
#define EDITOR_STAMP_REACH 128

enum Editor_StampPieceKind
{
    EDITOR_STAMP_LOC = 0,
    EDITOR_STAMP_TILE,
    EDITOR_STAMP_SPAWN,
    /** A tile's overlay and flags only, merged into the tile it lands on
     *  (`tile.has_overlay` 0: the overlay is left; `tile.settings`: the bits
     *  set on top of the tile's own). */
    EDITOR_STAMP_FLOOR,
};

struct Editor_StampPiece
{
    enum Editor_StampPieceKind kind;
    int dx;
    int dz;
    int dlevel;
    /* LOC */
    int loc_id;
    int shape;
    int rotation;
    int size_x;
    int size_z;
    /* TILE, FLOOR */
    struct Editor_Tile tile;
    /* SPAWN */
    int is_obj;
    char name[EDITOR_CMD_NAME_MAX];
    int count;
};

enum Editor_StampsLineKind
{
    /** Blank or a comment: carried, never interpreted. */
    EDITOR_STAMPS_LINE_TEXT = 0,
    EDITOR_STAMPS_LINE_HEADER,
    EDITOR_STAMPS_LINE_PIECE,
};

struct Editor_StampsLine
{
    enum Editor_StampsLineKind kind;
    /** Owned, NUL-terminated: the line's bytes without its newline. */
    char* text;
    /** HEADER: the stamp's name and note. */
    char name[EDITOR_STAMP_NAME_MAX];
    char note[EDITOR_STAMP_NOTE_MAX];
    /** PIECE. */
    struct Editor_StampPiece piece;
};

struct Editor_StampsFile
{
    struct Editor_StampsLine* lines;
    int line_count;
    int line_capacity;
    /** The text ended with a newline. */
    int final_newline;
};

struct Editor_StampsParseResult
{
    int ok;
    /** 1-based line of the first failure, 0 when ok. */
    int line;
};

/** One stamp in memory: what the ghost shows, Q turns and the drop places. */
struct Editor_Stamp
{
    char name[EDITOR_STAMP_NAME_MAX];
    char note[EDITOR_STAMP_NOTE_MAX];
    /** Owned, `capacity` of them. */
    struct Editor_StampPiece* pieces;
    int count;
    int capacity;
    /** Quarter turns applied since it was read (0..3), for the readout. */
    int turned;
};

/* ---- names and keys ---------------------------------------------------- */

/** 1..47 bytes of [A-Za-z0-9_-]. */
int
Editor_StampNameValid(const char* name);

/** "stamp:<name>". The name must be valid. */
void
Editor_StampDocKey(
    char out[EDITOR_DOC_KEY_MAX],
    const char* name);

/* ---- the file ---------------------------------------------------------- */

void
Editor_StampsInit(struct Editor_StampsFile* file);

/** Frees every line and zeroes the file. NULL-tolerant. */
void
Editor_StampsFree(struct Editor_StampsFile* file);

/** Parse, replacing what `file` held. NULL with length 0 is an empty file.
 *  A second header, or a piece before any header, is a failure. */
struct Editor_StampsParseResult
Editor_StampsParse(
    struct Editor_StampsFile* file,
    const char* text,
    size_t length);

/** snprintf's sizing contract. */
size_t
Editor_StampsEmit(
    const struct Editor_StampsFile* file,
    char* out,
    size_t out_capacity);

/** Parse one line on its own: the kind (TEXT for a blank or comment line),
 *  or -1 when it does not parse. `out_line` (may be NULL) receives a
 *  header's name / note or a piece. */
int
Editor_StampsParseLine(
    const char* text,
    struct Editor_StampsLine* out_line);

/** The line the editor writes for a piece / a header. snprintf's contract;
 *  returns the length. */
int
Editor_StampFormatPiece(
    const struct Editor_StampPiece* piece,
    char* out,
    size_t out_capacity);

int
Editor_StampFormatHeader(
    const char* name,
    const char* note,
    char* out,
    size_t out_capacity);

/** Line index of the header naming `name` (the index lists one per stamp),
 *  or -1. */
int
Editor_StampsFindHeader(
    const struct Editor_StampsFile* file,
    const char* name);

/** Headers the file holds (the index's stamps). */
int
Editor_StampsHeaderCount(const struct Editor_StampsFile* file);

/** The `nth` header's name, NULL past the end. */
const char*
Editor_StampsHeaderName(
    const struct Editor_StampsFile* file,
    int nth);

/**
 * `old_text` (a stamp's file, or the index) with the header line naming
 * `name` rewritten to name `new_name` (its note kept), or taken out when
 * `new_name` is NULL: what a rename and a delete write. Every other line is
 * carried byte for byte. Unchanged when no header names `name`. Written into
 * `out` (snprintf's contract); returns the length.
 */
size_t
Editor_StampsHeaderEdit(
    const char* old_text,
    size_t old_length,
    const char* name,
    const char* new_name,
    char* out,
    size_t out_capacity);

/* ---- the stamp --------------------------------------------------------- */

void
Editor_StampInit(struct Editor_Stamp* stamp);

/** NULL-tolerant. */
void
Editor_StampFree(struct Editor_Stamp* stamp);

/** No pieces, no name; keeps its storage. */
void
Editor_StampClear(struct Editor_Stamp* stamp);

/** Append one piece. 0 when the stamp is full (EDITOR_STAMP_PIECES_MAX) or
 *  the piece's offsets are past EDITOR_STAMP_REACH. */
int
Editor_StampAdd(
    struct Editor_Stamp* stamp,
    const struct Editor_StampPiece* piece);

/** `dst` becomes a copy of `src`. */
void
Editor_StampCopy(
    struct Editor_Stamp* dst,
    const struct Editor_Stamp* src);

/** The stamp a parsed file holds (its header's name and note, its pieces in
 *  file order). 0 when the file has no header. */
int
Editor_StampFromFile(
    const struct Editor_StampsFile* file,
    struct Editor_Stamp* stamp);

/** A new file's whole text for `stamp`: the comment lines above, the header,
 *  every piece. snprintf's contract. */
size_t
Editor_StampEmitNew(
    const struct Editor_Stamp* stamp,
    char* out,
    size_t out_capacity);

/** One piece turned `quarters` clockwise about the origin tile. */
void
Editor_StampPieceRotate(
    struct Editor_StampPiece* piece,
    int quarters);

/** Every piece turned `quarters` (any integer; 4 is a whole turn) clockwise
 *  about the origin, as one unit. */
void
Editor_StampRotate(
    struct Editor_Stamp* stamp,
    int quarters);

/** Count of the stamp's pieces of `kind`. */
int
Editor_StampCount(
    const struct Editor_Stamp* stamp,
    enum Editor_StampPieceKind kind);

/** "smithy: 3 locs, 1 tile, 1 spawn" for a readout; ", 35 floors" after
 *  them when it holds any. */
void
Editor_StampSummary(
    const struct Editor_Stamp* stamp,
    char* out,
    size_t out_size);

/* ---- writing a document ------------------------------------------------ */

/**
 * The TEXT_SPLICE commands that turn document `doc_key`'s text `old_text`
 * into `new_text`: the common head and tail kept, the old middle removed and
 * the new one inserted in chunks a command carries (EDITOR_CMD_SPLICE_MAX),
 * each against the text the ones before it left. Apply them in order as ONE
 * stroke. Returns the count written; 0 when the two are equal; -1 when
 * `max` is too few (nothing is to be applied).
 */
int
Editor_StampSplices(
    const char* doc_key,
    const char* old_text,
    size_t old_length,
    const char* new_text,
    size_t new_length,
    struct Editor_Cmd* out,
    int max);

/** The index's text with `name` listed: `old_text` unchanged when it
 *  already is, else with a header line appended (the comment lines first
 *  when the index is empty). Written into `out` (snprintf's contract);
 *  returns the length. */
size_t
Editor_StampsIndexWith(
    const char* old_text,
    size_t old_length,
    const char* name,
    char* out,
    size_t out_capacity);

#endif
