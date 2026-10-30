#ifndef SRC_EDITOR_EDITOR_CMD_H
#define SRC_EDITOR_EDITOR_CMD_H

/**
 * Edits as undoable commands.
 *
 * Every change to the document goes through here, so that undo, save-dirty
 * tracking and rebuild scheduling cannot drift apart: applying a command is
 * the one place that mutates a tile, a placement, a spawn row or a text, and
 * it is the one place that marks the document dirty.
 *
 * Two things this deliberately does NOT do, both learned from the reference
 * editor:
 *
 *   - It stores deltas, not snapshots. Snapshotting the whole document on
 *     every edit (a 4x64x64 tile array plus three entity lists, 30 deep) makes
 *     a brush stroke quadratic and puts a hard ceiling on history.
 *   - History survives changing level and changing square. Clearing it on a
 *     view change throws away work the user can still see.
 *
 * The commands carry AUTHORED tile records, never derived ones. Undoing a
 * height edit on a procedurally-heighted tile has to restore *absence* — the
 * state where the file says nothing and the noise routine owns the tile — and
 * a snapshot of the resolved height cannot express that.
 *
 * DOCUMENTS (docs/MAPED_DESIGN.html #documents). A command names the
 * document it edits by (doc_kind, doc_key): a map square ("m50_50"), a
 * square's edited spawn file ("m50_50"), one config record
 * ("loc:newbie_door1"), an interface ("if:bank") or a script text
 * ("rs2:edited/doors/my_door.rs2", "cs2:<name>"). The layout below is fixed
 * by docs/MAPED_IMPLEMENTATION.md section 2 and shared by every owner.
 */

#include "editor_types.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct Editor_Doc;

enum Editor_DocKind
{
    EDITOR_DOC_SQUARE = 0,
    EDITOR_DOC_SPAWN,
    EDITOR_DOC_CFG,
    EDITOR_DOC_IFACE,
    EDITOR_DOC_TEXT,
};

#define EDITOR_DOC_KIND_COUNT 5
/** Bytes in a document key, the terminator included. */
#define EDITOR_DOC_KEY_MAX 128
/** Bytes in a square's storage name ("" = the content tree's maps/,
 *  "ported/<lane>/maps" = a lane), the terminator included. */
#define EDITOR_DOC_STORAGE_MAX 128

/** One document, by name. What FACT_SAVED lists and the save paths take. */
struct Editor_DocRef
{
    enum Editor_DocKind kind;
    char key[EDITOR_DOC_KEY_MAX];
};

enum Editor_CmdKind
{
    /** One tile's authored record, before -> after. SQUARE documents. */
    EDITOR_CMD_TILE = 0,
    /**
     * One placement. `has_before`/`has_after` spell which kind of edit it is:
     * add (after only), delete (before only), or move/rotate/replace (both).
     * SQUARE documents.
     */
    EDITOR_CMD_LOC,
    /** One spawn row on a SPAWN document: add (has_after) or delete
     *  (has_before). A move is two commands in one stroke. */
    EDITOR_CMD_SPAWN,
    /** One suppression row (`-name x z level`) on a SPAWN document: add
     *  (has_after) hides a generated spawn, delete (has_before) restores it. */
    EDITOR_CMD_SPAWN_SUPPRESS,
    /** One `key=value` of one config record; "" spells absent. */
    EDITOR_CMD_CFG_FIELD,
    /** A config record itself: add (has_after), remove (has_before), rename
     *  (both). */
    EDITOR_CMD_CFG_BLOCK,
    /** One field of one interface component; "" spells absent. */
    EDITOR_CMD_IF_FIELD,
    /** An interface component: add (has_after), remove (has_before),
     *  reparent / reorder (both). */
    EDITOR_CMD_IF_COMPONENT,
    /** Replace `before` with `after` at `offset` in a text document. */
    EDITOR_CMD_TEXT_SPLICE,
};

#define EDITOR_CMD_KIND_COUNT 9

/** Bytes a TEXT_SPLICE carries, before and after together. A regeneration
 *  larger than this is several splices in one stroke. */
#define EDITOR_CMD_SPLICE_MAX 1024
#define EDITOR_CMD_NAME_MAX 64
#define EDITOR_CMD_VALUE_MAX 192

struct Editor_Cmd
{
    enum Editor_DocKind doc_kind;
    /** "m50_50" | "m50_50" (spawn) | "loc:newbie_door1" | "if:bank" |
     *  "rs2:edited/doors/my_door.rs2" */
    char doc_key[EDITOR_DOC_KEY_MAX];
    enum Editor_CmdKind kind;
    int has_before;
    int has_after;
    union
    {
        struct
        {
            int level;
            int x;
            int z;
            struct Editor_Tile before;
            struct Editor_Tile after;
        } tile;
        struct
        {
            struct Editor_Loc before;
            struct Editor_Loc after;
        } loc;
        /** before = has_before, after = has_after: one row either way. */
        struct
        {
            int is_obj;
            char name[EDITOR_CMD_NAME_MAX];
            int abs_x;
            int abs_z;
            int level;
            int count;
        } spawn;
        /** "" = absent. */
        struct
        {
            char key[EDITOR_CMD_NAME_MAX];
            char before[EDITOR_CMD_VALUE_MAX];
            char after[EDITOR_CMD_VALUE_MAX];
        } cfg_field;
        /** add: has_after; remove: has_before; rename: both. */
        struct
        {
            char name[EDITOR_CMD_NAME_MAX];
            char new_name[EDITOR_CMD_NAME_MAX];
        } cfg_block;
        struct
        {
            int component;
            char key[EDITOR_CMD_NAME_MAX];
            char before[EDITOR_CMD_VALUE_MAX];
            char after[EDITOR_CMD_VALUE_MAX];
        } if_field;
        struct
        {
            int component;
            int parent_before;
            int parent_after;
            int index_before;
            int index_after;
        } if_component;
        /** before text then after text, back to back in `text`. */
        struct
        {
            uint32_t offset;
            uint32_t before_len;
            uint32_t after_len;
            char text[EDITOR_CMD_SPLICE_MAX];
        } text_splice;
    } u;
};

/* ---- building and naming -------------------------------------------------
 *
 * Inline, because every participant on the wire needs them — the command
 * codec, a controller that links no document — and a .c for them would drag
 * the document layer into a binary that has none.
 *
 *   Editor_CmdInitSquare  zero a command and address it to the square at
 *                         (map_x, map_z): doc_kind SQUARE, key "m<x>_<z>".
 *   Editor_CmdInit        zero a command and address it to (doc_kind, key).
 *   Editor_SquareKey      "m<x>_<z>", the key of a square and of its spawns.
 *   Editor_SquareKeyParse 0 when `key` is not a square key — keys arrive off
 *                         the wire, so a bad one is data, not a violation.
 *   Editor_CmdKindFits    whether documents of `doc_kind` take edits of
 *                         `kind`: TILE/LOC squares, SPAWN* spawn files,
 *                         CFG_* records, IF_* interfaces, TEXT_SPLICE any
 *                         text-backed document (CFG, IFACE, TEXT).
 */

static inline void
Editor_SquareKey(
    char out[EDITOR_DOC_KEY_MAX],
    int map_x,
    int map_z)
{
    assert(out);
    snprintf(out, EDITOR_DOC_KEY_MAX, "m%d_%d", map_x, map_z);
}

static inline int
Editor_SquareKeyParse(
    const char* key,
    int* out_map_x,
    int* out_map_z)
{
    int map_x = 0;
    int map_z = 0;
    int digits = 0;
    const char* at;

    assert(key);
    assert(out_map_x);
    assert(out_map_z);

    /* Strictly m<digits>_<digits>: sscanf would accept "m50_50junk" and a
     * leading sign, and both would alias a real square. */
    if( key[0] != 'm' )
        return 0;
    for( at = key + 1; *at >= '0' && *at <= '9'; at++, digits++ )
    {
        map_x = map_x * 10 + (*at - '0');
        if( map_x > 1023 )
            return 0;
    }
    if( digits == 0 || *at != '_' )
        return 0;
    digits = 0;
    for( at++; *at >= '0' && *at <= '9'; at++, digits++ )
    {
        map_z = map_z * 10 + (*at - '0');
        if( map_z > 1023 )
            return 0;
    }
    if( digits == 0 || *at != '\0' )
        return 0;
    *out_map_x = map_x;
    *out_map_z = map_z;
    return 1;
}

static inline void
Editor_CmdInit(
    struct Editor_Cmd* command,
    enum Editor_DocKind doc_kind,
    const char* doc_key,
    enum Editor_CmdKind kind)
{
    assert(command);
    assert(doc_key);
    assert(strlen(doc_key) < EDITOR_DOC_KEY_MAX);

    memset(command, 0, sizeof(*command));
    command->doc_kind = doc_kind;
    snprintf(command->doc_key, sizeof(command->doc_key), "%s", doc_key);
    command->kind = kind;
}

static inline void
Editor_CmdInitSquare(
    struct Editor_Cmd* command,
    enum Editor_CmdKind kind,
    int map_x,
    int map_z)
{
    char key[EDITOR_DOC_KEY_MAX];

    assert(kind == EDITOR_CMD_TILE || kind == EDITOR_CMD_LOC);

    Editor_SquareKey(key, map_x, map_z);
    Editor_CmdInit(command, EDITOR_DOC_SQUARE, key, kind);
}

static inline int
Editor_CmdKindFits(
    enum Editor_DocKind doc_kind,
    enum Editor_CmdKind kind)
{
    switch( doc_kind )
    {
    case EDITOR_DOC_SQUARE:
        return kind == EDITOR_CMD_TILE || kind == EDITOR_CMD_LOC;
    case EDITOR_DOC_SPAWN:
        return kind == EDITOR_CMD_SPAWN || kind == EDITOR_CMD_SPAWN_SUPPRESS;
    case EDITOR_DOC_CFG:
        return kind == EDITOR_CMD_CFG_FIELD || kind == EDITOR_CMD_CFG_BLOCK
               || kind == EDITOR_CMD_TEXT_SPLICE;
    case EDITOR_DOC_IFACE:
        return kind == EDITOR_CMD_IF_FIELD || kind == EDITOR_CMD_IF_COMPONENT
               || kind == EDITOR_CMD_TEXT_SPLICE;
    case EDITOR_DOC_TEXT:
        return kind == EDITOR_CMD_TEXT_SPLICE;
    }
    return 0;
}

/* ---- history ------------------------------------------------------------- */

/** Commands held at most. A stroke can be thousands, so this is sized for
 *  work, not for a demo. The stack grows to it on demand: a command is ~1.2 KB
 *  now that a text splice rides in it, and a fixed array would cost the server
 *  80 MB before the first edit. */
#define EDITOR_UNDO_MAX 65536

struct Editor_UndoStack
{
    /** Owned, grown on demand up to EDITOR_UNDO_MAX. */
    struct Editor_Cmd* commands;
    /** 1 where a command begins an undo group (a brush stroke, a paste). */
    unsigned char* stroke_start;
    int capacity;

    /** Commands recorded. */
    int count;
    /**
     * How many of them are currently applied. Undo moves this down without
     * discarding anything, which is what makes redo possible; the next new
     * command truncates the tail above it, as every editor does.
     */
    int applied;

    /** Set between StrokeBegin and StrokeEnd. */
    int in_stroke;
    int stroke_opened;
};

/* ---- applying ------------------------------------------------------------ */

enum Editor_CmdDirection
{
    EDITOR_CMD_FORWARD = 0,
    EDITOR_CMD_INVERSE,
};

/**
 * Apply one command to the document it names and mark that document dirty.
 *
 * Dispatches on doc_kind: TILE/LOC on a square, SPAWN/SPAWN_SUPPRESS on a
 * spawn document and TEXT_SPLICE on a text document apply here; CFG_* and
 * IF_* go to Editor_CfgApply / Editor_IfApply (editor_doc.h).
 *
 * Undo runs the same function with EDITOR_CMD_INVERSE rather than a separate
 * path, so a reverted edit dirties and repaints exactly like a forward one.
 * Returns 1 when the document changed; 0 when the document is not open here
 * or the command does not fit it (a splice whose `before` text is not where
 * it says, a delete of a row that is not there).
 */
int
Editor_CmdApply(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction);

/**
 * The squares whose meshes this command invalidates, as (map_x, map_z) pairs.
 *
 * Usually just the edited square, but not always, and the exceptions are why
 * this is a function rather than an assumption at the call site. Terrain is
 * not built per tile in isolation:
 *
 *   - Tile meshes sample a 65x65 grid of corner heights, so a height change on
 *     an edge tile moves vertices belonging to the neighbouring square.
 *   - Terrain colour blends underlays across a sliding window several tiles
 *     wide, so an underlay change near a border shifts colours on the far side
 *     of it.
 *
 * The reference editor never had to solve this because it can only open one
 * square; an editor that shows a region has to, or edits along a seam leave a
 * visible crack until the neighbour is reloaded.
 *
 * Commands on any document other than a square remesh nothing and return 0.
 *
 * Writes at most `max` pairs and returns the count.
 */
int
Editor_CmdRebuildSpan(
    const struct Editor_Cmd* command,
    int* out_coords,
    int max);

/* ---- history ------------------------------------------------------------- */

void
Editor_UndoInit(struct Editor_UndoStack* stack);

/** Frees the stack's storage and zeroes it. NULL-tolerant. */
void
Editor_UndoFree(struct Editor_UndoStack* stack);

/**
 * Open an undo group. Every command pushed until StrokeEnd undoes as one step
 * — a brush drag is one Ctrl+Z, not four hundred.
 */
void
Editor_UndoStrokeBegin(struct Editor_UndoStack* stack);

void
Editor_UndoStrokeEnd(struct Editor_UndoStack* stack);

/**
 * Record an already-applied command. Truncates any redo tail.
 * Returns 1 when it was recorded, 0 when the stack is full.
 */
int
Editor_UndoPush(
    struct Editor_UndoStack* stack,
    const struct Editor_Cmd* command);

/**
 * Revert the newest group. Returns the number of commands reverted, 0 when
 * there is nothing to undo. Each reverted command is passed to `on_reverted`
 * (may be NULL) so the caller can collect the squares needing a rebuild.
 */
int
Editor_UndoUndo(
    struct Editor_UndoStack* stack,
    struct Editor_Doc* doc,
    void (*on_reverted)(void* user_data, const struct Editor_Cmd* command),
    void* user_data);

/** Re-apply the group undone last. Same reporting contract as Undo. */
int
Editor_UndoRedo(
    struct Editor_UndoStack* stack,
    struct Editor_Doc* doc,
    void (*on_reapplied)(void* user_data, const struct Editor_Cmd* command),
    void* user_data);

#endif
