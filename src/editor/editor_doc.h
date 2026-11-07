#ifndef SRC_EDITOR_EDITOR_DOC_H
#define SRC_EDITOR_EDITOR_DOC_H

/**
 * The editor's document: every document currently open, in authored form,
 * keyed by (kind, key) — docs/MAPED_DESIGN.html #documents.
 *
 *   SQUARE  map squares (`maps/m<x>_<z>.jm2` + `.jl2`, or a lane's copy),
 *           parsed into Editor_Square by editor_jm2.c.
 *   SPAWN   a square's EDITED spawn file
 *           (`server/scripts/areas/edited/configs/m<x>_<z>.spawn`), parsed
 *           into lines and rows by editor_spawn.c.
 *   CFG     one config record ("loc:newbie_door1"): the `[name]` block's text.
 *   IFACE   one interface ("if:bank"): the `.if` file's text.
 *   TEXT    a script (`rs2:<path under server/scripts>`, `cs2:<name>`), or a
 *           ledger / membership / member-index file an id allocation appends
 *           to (`pack:<ns>.alloc|client|server|pack`, `compack:<type>`).
 *   AREAS   the named areas and square collections ("areas",
 *           `editor/areas.txt`), held as text; AREA_* / COLLECTION_*
 *           commands go through the codec in editor_areas.h.
 *
 * CFG, IFACE and TEXT documents are held as TEXT. CFG_* commands go through
 * the config codec (editor_cfg.h, Editor_CfgApply); IF_* commands go through
 * the interface codec (editor_if.h, Editor_IfDocApply).
 *
 * The document is the source of truth while the editor runs. It is loaded
 * from the text in OSRS-Content rather than from the baked cache, and it is
 * what a save writes back — so the bake is not in the editing loop and cannot
 * go stale underneath the editor.
 *
 * Going the other way — authored tiles out to the terrain the world builder
 * meshes — is editor_derive.h. It is separate because it is the only part of
 * the editor that needs the cache codec, and keeping it out of here leaves the
 * document layer as plain data that tests can exercise without a cache.
 */

#include "editor_cmd.h"
#include "editor_spawn.h"
#include "editor_types.h"

#include <stddef.h>
#include <stdint.h>

struct RSCache;

/** Squares open at once. The world load path tops out at 64 (an 8x8 region),
 *  and there is no reason to hold document state for squares it cannot show. */
#define EDITOR_DOC_MAX_SQUARES 64

/** One square's edited spawn file. Heap-allocated; the pointer is stable for
 *  the document's lifetime. */
struct Editor_SpawnDoc
{
    char key[EDITOR_DOC_KEY_MAX];
    int map_x;
    int map_z;
    /** 1 once the file (or its absence) has been read into `file`. */
    int loaded;
    int dirty;
    struct Editor_SpawnFile file;
};

/** A CFG, IFACE or TEXT document. Heap-allocated; stable pointer. */
struct Editor_TextDoc
{
    enum Editor_DocKind kind;
    char key[EDITOR_DOC_KEY_MAX];
    /** Owned, NUL-terminated, `length` bytes before the terminator. */
    char* text;
    size_t length;
    size_t capacity;
    int loaded;
    int dirty;
    /** Bumps on every change to `text` (set or splice): what a reader that
     *  caches a parse of it (the Areas panel, the world overlay) keys on. */
    uint32_t revision;
};

/** One open document, whichever kind. Exactly one pointer is non-NULL. */
struct Editor_DocHandle
{
    enum Editor_DocKind kind;
    struct Editor_Square* square;
    struct Editor_SpawnDoc* spawn;
    struct Editor_TextDoc* text;
};

/**
 * NEVER a stack local: 64 squares of 16,384 tiles is about 8 MB, which
 * overflows a default stack before the first field is written. The session owns
 * one on the heap (struct Editor), and tests must allocate theirs the same way.
 */
struct Editor_Doc
{
    struct Editor_Square squares[EDITOR_DOC_MAX_SQUARES];
    int square_count;
    /** Per square, by index: where it is stored ("" = the content tree's
     *  `maps/`, else "ported/<lane>/maps"), and whether DOC_CREATE made it
     *  (a created square writes its `.filepack` and both halves on save). */
    char square_storage[EDITOR_DOC_MAX_SQUARES][EDITOR_DOC_STORAGE_MAX];
    unsigned char square_created[EDITOR_DOC_MAX_SQUARES];

    /** Owned arrays of owned pointers. */
    struct Editor_SpawnDoc** spawns;
    int spawn_count;
    int spawn_capacity;
    struct Editor_TextDoc** texts;
    int text_count;
    int text_capacity;

    /** Cache identity, for the tile-width flags the terrain codec needs. The
     *  widths are an era difference (u8 before OldSchool 209, u16 after), so
     *  deriving with the wrong one shifts every tile. Borrowed, not owned.
     *
     *  NULL is a legal state with a precise meaning: this document never
     *  derives. ToriRSMapEd's authoritative copy is one — the server parses,
     *  applies commands and emits, while deriving-for-render happens in each
     *  client's mirror, which does carry a profile. Editor_SquareDeriveTerrain
     *  asserts its profile argument, so a NULL-profile doc that tries to
     *  derive stops there, at the call that meant it. */
    const struct RSCache* profile;
};

/* ---- square ------------------------------------------------------------- */

void
Editor_SquareInit(
    struct Editor_Square* square,
    int map_x,
    int map_z);

/** Frees the square's owned buffers and zeroes it. NULL-tolerant. */
void
Editor_SquareFree(struct Editor_Square* square);

/** Append a placement. Returns its index; grows the list as needed. */
int
Editor_SquareLocAdd(
    struct Editor_Square* square,
    const struct Editor_Loc* loc);

/**
 * Insert a placement where the cache orders it (loc id, level, x, z, shape,
 * rotation) and return its index. What an edit uses -- Editor_SquareLocAdd
 * keeps the file's own order for the parser -- so a deleted loc that comes
 * back on undo lands where it stood, and the .jl2 saves byte for byte.
 */
int
Editor_SquareLocInsert(
    struct Editor_Square* square,
    const struct Editor_Loc* loc);

void
Editor_SquareLocRemoveAt(
    struct Editor_Square* square,
    int index);

/**
 * Index of the placement at `(level, x, z)` with this shape, or -1.
 *
 * Shape is part of the key because a tile legitimately holds several locs at
 * once — a wall, its decoration, and a ground decor occupy the same tile in
 * different layers, and the cache addresses them by shape. Matching on
 * position alone would make an edit to one silently hit another.
 */
int
Editor_SquareLocFind(
    const struct Editor_Square* square,
    int level,
    int x,
    int z,
    int shape);

/** Index of the placement equal to `loc` in every field (id, shape,
 *  rotation, level, x, z), or -1: what a delete or a move names. */
int
Editor_SquareLocFindExact(
    const struct Editor_Square* square,
    const struct Editor_Loc* loc);

/* ---- keys --------------------------------------------------------------- */

/**
 * 1 when `key` can name a document of `kind`: a square key for SQUARE and
 * SPAWN, "<type>:<record>" for CFG, "if:<name>" for IFACE, "rs2:<path>" /
 * "cs2:<name>" / "pack:<ns>.{alloc,client,server,pack}" / "compack:<type>"
 * (the last two are what an id allocation appends to, editor_alloc.h) /
 * "wma:<name>" (a world-map area file, editor_wma.h) for TEXT; printable, under EDITOR_DOC_KEY_MAX, no "..", no
 * absolute or backslashed path. Keys arrive off the wire, so this is a
 * validity test, not an assertion.
 */
int
Editor_DocKeyValid(
    enum Editor_DocKind kind,
    const char* key);

/** 1 for a square storage: "" or "maps" (the content tree's maps/),
 *  "ported/<lane>/maps", or EDITOR_DOC_SCRATCH_STORAGE. */
int
Editor_DocStorageValid(const char* storage);

/**
 * A scratch square: one made to be edited before it has a place
 * (docs/MAPED_STATUS.md "Scratch squares"). DOC_CREATE makes it in this
 * storage at one of the reserved slots below, far east of any content (the
 * content tree's squares stop at x 65); the file layer has no path for it,
 * so it is never read from or written to disk, and a save skips it. Save
 * places it instead: the Square map's Place copies it to the cell and
 * storage the user picks, then clears it, and a cleared (blank) scratch
 * square is a free slot DOC_CREATE may fill again.
 */
#define EDITOR_DOC_SCRATCH_STORAGE "scratch"
#define EDITOR_DOC_SCRATCH_SLOTS 8

/** 1 for EDITOR_DOC_SCRATCH_STORAGE. */
int
Editor_DocStorageIsScratch(const char* storage);

/** Scratch slot `slot`'s square (0 <= slot < EDITOR_DOC_SCRATCH_SLOTS): two
 *  apart, so no two slots are ever neighbours in one world. */
void
Editor_DocScratchSlot(
    int slot,
    int* out_map_x,
    int* out_map_z);

/** Whether (map_x, map_z) is one of the scratch slots. */
int
Editor_DocIsScratchSlot(
    int map_x,
    int map_z);

/** Whether the square holds nothing: every tile the zero tile, no loc. */
int
Editor_SquareIsBlank(const struct Editor_Square* square);

/**
 * Split a square key that may carry its storage ("ported/x/maps/m16_101",
 * "maps/m50_50", "m50_50") into the storage ("" for maps/) and the bare key.
 * Returns 0 when either half is invalid.
 */
int
Editor_DocSplitSquareKey(
    const char* full_key,
    char out_storage[EDITOR_DOC_STORAGE_MAX],
    char out_key[EDITOR_DOC_KEY_MAX]);

/* ---- document ----------------------------------------------------------- */

void
Editor_DocInit(
    struct Editor_Doc* doc,
    const struct RSCache* profile);

void
Editor_DocFree(struct Editor_Doc* doc);

/** The open square at these coords, or NULL. */
struct Editor_Square*
Editor_DocFindSquare(
    struct Editor_Doc* doc,
    int map_x,
    int map_z);

/**
 * The open square at these coords, opening an empty one if there is room.
 * Returns NULL only when the document is full.
 */
struct Editor_Square*
Editor_DocOpenSquare(
    struct Editor_Doc* doc,
    int map_x,
    int map_z);

/** Storage of an open square ("" = maps/). */
const char*
Editor_DocSquareStorage(
    const struct Editor_Doc* doc,
    const struct Editor_Square* square);

void
Editor_DocSquareSetStorage(
    struct Editor_Doc* doc,
    const struct Editor_Square* square,
    const char* storage);

/**
 * The open document (kind, key). Returns 1 and fills `out`, or 0 when it is
 * not open. A key that is not valid for the kind is simply not open.
 */
int
Editor_DocFind(
    struct Editor_Doc* doc,
    enum Editor_DocKind kind,
    const char* key,
    struct Editor_DocHandle* out);

/**
 * The open document (kind, key), opening an empty, not-yet-loaded one if it
 * is not. Returns 0 when the key is invalid for the kind or the square table
 * is full.
 */
int
Editor_DocOpen(
    struct Editor_Doc* doc,
    enum Editor_DocKind kind,
    const char* key,
    struct Editor_DocHandle* out);

/** Whether the document has been read in, and whether it has unsaved edits
 *  (for a square: either half). */
int
Editor_DocHandleLoaded(const struct Editor_DocHandle* handle);

int
Editor_DocHandleDirty(const struct Editor_DocHandle* handle);

/** Clear the unsaved flags — a save landed. */
void
Editor_DocHandleClearDirty(struct Editor_DocHandle* handle);

/** 1 when any open document has unsaved edits. */
int
Editor_DocHasUnsaved(const struct Editor_Doc* doc);

/** How many open documents have unsaved edits (a square with both halves
 *  dirty counts once). */
int
Editor_DocUnsavedCount(const struct Editor_Doc* doc);

/* ---- text documents ----------------------------------------------------- */

/** Replace a text document's contents and mark it loaded (not dirty). */
void
Editor_TextDocSet(
    struct Editor_TextDoc* text,
    const char* data,
    size_t length);

/** Replace `remove_length` bytes at `offset` with `insert`. Asserts the range
 *  is inside the text. */
void
Editor_TextDocSplice(
    struct Editor_TextDoc* text,
    size_t offset,
    size_t remove_length,
    const char* insert,
    size_t insert_length);

/**
 * Apply a CFG_FIELD / CFG_BLOCK command to a CFG document — the `[name]`
 * block's text, header through the byte before the next header — through the
 * config codec (editor_cfg.h). Only the edited line changes; every other byte
 * of the block is kept, so an edit and its undo leave the text as it was.
 *
 *   CFG_FIELD   `key` is a field spec, `key[#occurrence][@line]`
 *               (Editor_CfgFieldSpecParse). before "" = must be absent: for
 *               a plain "key" no occurrence may exist; with #N/@L the line is
 *               inserted as that occurrence / at that line. after "" =
 *               removed: the occurrence must hold `before` (and be on line L
 *               when @L is given — a removal should carry it, so its undo
 *               puts the line back where it was). Otherwise the value must
 *               equal `before` and becomes `after`. A new line goes after the
 *               key's last occurrence, else after the record's last field,
 *               else after the header; never after a trailing comment, which
 *               in all.<type> belongs to the next record.
 *   CFG_BLOCK   add: a text with no header gets "[name]\n" at its top (an
 *               empty document becomes "[name]\n"); remove: the header line
 *               goes when no field is left (remove the fields first, in the
 *               same stroke, so undo restores them; blank lines and comments
 *               stay, so the add that undoes it is byte-exact); rename: the
 *               header's name. The document key keeps the old name.
 *
 * Returns 1 when the text changed, 0 when the command does not match it.
 * TEXT_SPLICE on a CFG document is the generic splice in editor_cmd.c.
 */
int
Editor_CfgApply(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction);

/** Apply an IF_FIELD / IF_COMPONENT to an IFACE document through the
 *  interface codec. Command meanings are in editor_if.h. Returns 1 when the
 *  text changed. */
int
Editor_IfApply(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction);

#endif
