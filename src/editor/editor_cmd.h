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
 * ("loc:newbie_door1"), an interface ("if:bank"), a script text
 * ("rs2:edited/doors/my_door.rs2", "cs2:<name>") or the named areas
 * ("areas", editor_areas.h). The layout below is fixed
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
    /** The named areas and square collections (editor_areas.h), one
     *  document per content tree, key "areas", held as text. */
    EDITOR_DOC_AREAS,
    /** The catalog palettes (editor_palette.h): one document per content
     *  tree, key "palettes", held as text. */
    EDITOR_DOC_PALETTES,
    /** The scenes (editor_scenes.h, workbench 6.5): one document per content
     *  tree, key "scenes", held as text. */
    EDITOR_DOC_SCENES,
};

#define EDITOR_DOC_KIND_COUNT 8
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
    /**
     * One spawn row of a SPAWN document rewritten IN PLACE: `before` becomes
     * `after` on the same line (the edit form, a move), so a save touches
     * exactly that line and an undo puts it back where it was. Both rows sit
     * in the same document and the same section (npc/obj); an edit that
     * changes the kind or the square is a SPAWN delete + add instead. The
     * suppression mark may change (an edited row can become a `-name` row and
     * back). `before_text` (may be "") is the line's exact bytes, so the
     * inverse restores a hand-formatted row byte for byte; a trailing `//`
     * comment is kept either way. has_before and has_after are both 1.
     */
    EDITOR_CMD_SPAWN_EDIT,
    /**
     * AREAS documents (editor_areas.h). One record line, by name: SET upserts
     * (has_after; has_before too when it replaces a record, whose exact line
     * it carries -- a rename is a SET whose names differ), REMOVE takes one
     * out (has_before only) and remembers the line index it stood at, so the
     * inverse puts it back there. `u.area` holds both lines.
     */
    EDITOR_CMD_AREA_SET,
    EDITOR_CMD_AREA_REMOVE,
    EDITOR_CMD_COLLECTION_SET,
    EDITOR_CMD_COLLECTION_REMOVE,
    /**
     * PALETTES documents (editor_palette.h). One header or entry line, by key
     * (palette, slot): SET upserts (has_after; has_before too when it rewrites
     * a line, whose exact text it carries -- a rename or a move to another
     * slot is a SET whose keys differ), REMOVE takes one out (has_before only)
     * and remembers the index it stood at. `u.area` holds both lines.
     */
    EDITOR_CMD_PALETTE_SET,
    EDITOR_CMD_PALETTE_REMOVE,
    /**
     * SCENES documents (editor_scenes.h). One record line, by key (scene,
     * the segment kinds it holds): SET upserts (has_after; has_before too
     * when it rewrites a line, whose exact text it carries -- a rename is a
     * SET whose names differ), REMOVE takes one out (has_before only) and
     * remembers the index it stood at. `u.area` holds both lines.
     */
    EDITOR_CMD_SCENE_SET,
    EDITOR_CMD_SCENE_REMOVE,
};

#define EDITOR_CMD_KIND_COUNT 18

/** Bytes a TEXT_SPLICE carries, before and after together. A regeneration
 *  larger than this is several splices in one stroke. */
#define EDITOR_CMD_SPLICE_MAX 1024
#define EDITOR_CMD_NAME_MAX 64
#define EDITOR_CMD_VALUE_MAX 192

/** One spawn row as SPAWN_EDIT carries it (editor_spawn.h's Editor_SpawnRow,
 *  which this header cannot include). */
struct Editor_CmdSpawnRow
{
    int is_obj;
    int suppress;
    char name[EDITOR_CMD_NAME_MAX];
    int abs_x;
    int abs_z;
    int level;
    /** The obj stack size; 1 for an npc. */
    int count;
};

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
        /** SPAWN_EDIT: the row before and after, and the line's old bytes. */
        struct
        {
            struct Editor_CmdSpawnRow before;
            struct Editor_CmdSpawnRow after;
            char before_text[EDITOR_CMD_VALUE_MAX];
        } spawn_edit;
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
        /** AREA_* / COLLECTION_* (and PALETTE_*, editor_palette.h, and
         *  SCENE_*, editor_scenes.h): the record line before then after, back to
         *  back in `text` (each at most EDITOR_AREAS_LINE_MAX bytes), and the
         *  line index `before` stood at (-1: unknown, an undone remove
         *  appends). */
        struct
        {
            int line;
            uint32_t before_len;
            uint32_t after_len;
            char text[EDITOR_CMD_SPLICE_MAX];
        } area;
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
 *                         `kind`: TILE/LOC squares, SPAWN* (SPAWN,
 *                         SPAWN_SUPPRESS, SPAWN_EDIT) spawn files,
 *                         CFG_* records, IF_* interfaces, TEXT_SPLICE any
 *                         text-backed document (CFG, IFACE, TEXT), AREA_*
 *                         and COLLECTION_* the AREAS document.
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
        return kind == EDITOR_CMD_SPAWN || kind == EDITOR_CMD_SPAWN_SUPPRESS
               || kind == EDITOR_CMD_SPAWN_EDIT;
    case EDITOR_DOC_CFG:
        return kind == EDITOR_CMD_CFG_FIELD || kind == EDITOR_CMD_CFG_BLOCK
               || kind == EDITOR_CMD_TEXT_SPLICE;
    case EDITOR_DOC_IFACE:
        return kind == EDITOR_CMD_IF_FIELD || kind == EDITOR_CMD_IF_COMPONENT
               || kind == EDITOR_CMD_TEXT_SPLICE;
    case EDITOR_DOC_TEXT:
        return kind == EDITOR_CMD_TEXT_SPLICE;
    case EDITOR_DOC_AREAS:
        return kind == EDITOR_CMD_AREA_SET || kind == EDITOR_CMD_AREA_REMOVE
               || kind == EDITOR_CMD_COLLECTION_SET || kind == EDITOR_CMD_COLLECTION_REMOVE;
    case EDITOR_DOC_PALETTES:
        return kind == EDITOR_CMD_PALETTE_SET || kind == EDITOR_CMD_PALETTE_REMOVE;
    case EDITOR_DOC_SCENES:
        return kind == EDITOR_CMD_SCENE_SET || kind == EDITOR_CMD_SCENE_REMOVE;
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

/* ---- the history as a mirror sees it ---------------------------------------
 *
 * The undo stack above is the AUTHORITY's (torirs_maped.c `maped->undo`); a
 * window is a mirror and holds no stack. What a mirror does see is the
 * authority's FACT_CMD stream -- every command, in order, with its direction
 * -- and its own intents (Editor_Apply, a stroke, Undo, Redo). Editor_History
 * is the list of undo groups DERIVED from exactly that, for the History panel:
 * one summary row per group, with where the applied position is. It is never
 * applied to anything and never sent anywhere; the authority's stack stays the
 * one source of truth, and every Undo / Redo still goes to it.
 *
 * How the facts are grouped:
 *   - this window's own edits: a group per Editor_Apply outside a stroke, per
 *     stroke, per DOC_CREATE / SAVE_AS -- exactly the authority's grouping,
 *     because the same calls make both;
 *   - another window's edits: a group per pump of facts (that window's stroke
 *     boundaries are not on the wire), so a long drag elsewhere can show as
 *     several rows; a Undo from THIS window re-cuts the rows it consumed to the
 *     authority's answer (Editor_HistoryReplayEnd);
 *   - INVERSE facts undo the newest applied group; FORWARD facts during this
 *     window's Redo redo the next one, and a forward fact from elsewhere whose
 *     command is the next redo group's first is taken as that window's Redo
 *     (a new edit would truncate the tail, which is what anything else does).
 */

/** Groups the derived history keeps; the oldest fall off past it. */
#define EDITOR_HISTORY_GROUPS_MAX 1024
/** Bytes of a group's intent label ("New square m1_1 (copy of m50_50)"). */
#define EDITOR_HISTORY_LABEL_MAX 96

/** One undo group: what its commands did, tallied as they arrived. */
struct Editor_HistoryGroup
{
    int cmd_count;
    /** Editor_CmdHash of the first and last command: a remote Redo is matched
     *  on the first, a remote Undo starts from the last. */
    uint32_t first_hash;
    uint32_t last_hash;
    /** The first command's document; `doc_keys` 2 once another key joined. */
    enum Editor_DocKind doc_kind;
    char doc_key[EDITOR_DOC_KEY_MAX];
    int doc_keys;
    /** What the window that made it called the action ("Raise", "nudge
     *  locs"; "" = derive the verb from the tallies), or the whole row text
     *  for an intent whose commands do not say what it was (DOC_CREATE). */
    char verb[32];
    char label[EDITOR_HISTORY_LABEL_MAX];
    /** Cut or merged to follow the authority's count; tallies are the
     *  pre-cut group's. */
    int approximate;
    int kinds[EDITOR_CMD_KIND_COUNT];

    /* TILE: absolute bounds, the first level, and what changed. */
    int tile_level;
    int tile_x0;
    int tile_z0;
    int tile_x1;
    int tile_z1;
    int raised;
    int lowered;
    int underlay;
    int overlay;
    int flags;
    int cleared;

    /* LOC */
    int loc_adds;
    int loc_removes;
    int loc_moves;
    int loc_turns;
    /** Same tile, another loc or shape: a place onto a taken shape, a swap. */
    int loc_replaces;
    int loc_id;

    /* SPAWN / SPAWN_SUPPRESS / SPAWN_EDIT */
    int spawn_adds;
    int spawn_removes;
    /** SPAWN_EDIT rows rewritten in place, and how many of them moved. */
    int spawn_edits;
    int spawn_moves;
    int spawn_is_obj;
    int suppress_adds;
    int suppress_removes;
    char name[EDITOR_CMD_NAME_MAX];

    /* CFG_* / IF_*: the first block or component's key. */
    int block_adds;
    int block_removes;
    int block_renames;
    int component;
    char field[EDITOR_CMD_NAME_MAX];
};

struct Editor_History
{
    /** EDITOR_HISTORY_GROUPS_MAX, allocated on the first group. */
    struct Editor_HistoryGroup* groups;
    int count;
    /** Groups [0, applied) are applied; the rest are the redo tail. */
    int applied;
    /** Groups that fell off the old end. */
    int dropped;
    /** Bumps on every change: the panel rebuilds only when it moves. */
    uint32_t serial;

    /** 0: the next new forward fact opens a group (with `next_verb` and
     *  `next_label`). */
    int open;
    char next_verb[32];
    char next_label[EDITOR_HISTORY_LABEL_MAX];

    /** A group being undone (-1) or redone (+1) fact by fact, and how many of
     *  its commands are still to come / have come. */
    int replay;
    int replay_left;
    int replay_done;
    /** This window's own Undo / Redo is in progress (ReplayBegin..End) and
     *  how many groups it finished so far. */
    int local_replay;
    int local_groups;
    /** Non-zero while the facts arriving answer this window's own new edit
     *  (Editor_Apply's echo): never a Redo, even when the command is the
     *  redo tail's first -- the user undid a place and placed it again, and
     *  the authority's push dropped the tail (Editor_HistoryOwnEdit). */
    int own_edit;
};

/** The facts that follow (until called with 0) are this window's own new
 *  edit: they open or extend a group, never redo one. */
void
Editor_HistoryOwnEdit(
    struct Editor_History* history,
    int on);

/** A stable 32-bit hash of a command's bytes (FNV-1a over the struct; the
 *  wire decode zeroes the struct first, so equal commands hash equal). */
uint32_t
Editor_CmdHash(const struct Editor_Cmd* command);

void
Editor_HistoryInit(struct Editor_History* history);

/** Frees the groups. NULL-tolerant. */
void
Editor_HistoryFree(struct Editor_History* history);

/** The next NEW forward fact opens a group. `label` (may be NULL) is its
 *  whole row text, for an intent whose commands do not say what it was
 *  (DOC_CREATE, SAVE_AS). */
void
Editor_HistoryBreak(
    struct Editor_History* history,
    const char* label);

/** Name the action of the group being recorded (or about to be): "Raise",
 *  "nudge locs". The row keeps the derived counts and document after it. */
void
Editor_HistoryVerb(
    struct Editor_History* history,
    const char* verb);

/** This window's Undo (direction -1) or Redo (+1) is about to run. */
void
Editor_HistoryReplayBegin(
    struct Editor_History* history,
    int direction);

/** It answered `count` commands: re-cut the groups it consumed to that
 *  count (the authority's group boundary). */
void
Editor_HistoryReplayEnd(
    struct Editor_History* history,
    int count);

/** A pump of facts from elsewhere ended: a group half undone / redone is
 *  cut where the facts stopped. */
void
Editor_HistoryBatchEnd(struct Editor_History* history);

/** One FACT_CMD, as the mirror applied it. */
void
Editor_HistoryOnFact(
    struct Editor_History* history,
    int direction,
    const struct Editor_Cmd* command);

/** Commands of the applied groups (what a jump compares against). */
int
Editor_HistoryAppliedCommands(const struct Editor_History* history);

/** Commands of groups [0, groups). */
int
Editor_HistoryCommandsThrough(
    const struct Editor_History* history,
    int groups);

/** The row text for one group: "Raise 9 tiles m50_50 L0", "Place loc 1530
 *  m50_50", "Paste 12x8 m50_50 L0", "Spawn npc goblin 3200,3238",
 *  "Cfg loc:door.name". Returns `out`. */
const char*
Editor_HistoryGroupName(
    const struct Editor_HistoryGroup* group,
    char* out,
    int out_size);

#endif
