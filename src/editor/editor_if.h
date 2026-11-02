#ifndef SRC_EDITOR_EDITOR_IF_H
#define SRC_EDITOR_EDITOR_IF_H

/**
 * The interface codec: `interfaces/<name>.if` and `interfaces/<name>.compack`
 * (docs/MAPED_DESIGN.html #documents "Interface", #cs2).
 *
 * The `.if` grammar is the config grammar (editor_cfg.h) with cachepack's
 * component vocabulary on top: a `// Interface N — K components.` preamble,
 * then one `[name]` block per component, each a run of `key=value` lines in
 * the order cachepack's emitter writes them (cp_decode.c emit_component), and
 * a blank line. The `.compack` beside it is the member index, `id=name` lines
 * (plus comments) that tie each block to its file id in the archive.
 *
 * What editor_if_test settles over all 982 interfaces, rather than guessing:
 *
 *   - every file re-emits byte for byte from its index (preamble, blocks);
 *   - every key in every block is in the vocabulary below, in canonical
 *     order, and occurs once, so a field added by the editor at its canonical
 *     place and removed again leaves the bytes it found;
 *   - `layer=` is the parent's FULL component id, `(interface << 16) | child`
 *     (the decoder adds `id & 0xFFFF0000` to the u16 on the wire,
 *     dat2_component.c), never the bare child; `linked=` is the bare child;
 *   - the `type=` set the tree uses (reported; the test fails on a type the
 *     client's component converter does not know);
 *   - which compacks are POSITIONAL — entry i is `i=<name of block i>` and
 *     there is one entry per block — the condition for structure edits,
 *     because only then is a block's position its file id;
 *   - the typed layer (editor_if_typed.c): every block reads into an
 *     RSCache_Dat2Component and the component re-emits that block's lines
 *     byte for byte, which is cachepack's reader and emitter agreeing with
 *     ours on every field the tree carries.
 *
 * Commands (the layouts are brief section 2's; the meanings are fixed here):
 *
 *   IF_FIELD      `component` is the block's POSITION in the `.if` (which is
 *                 its child id when the compack is positional). `key` is a
 *                 concrete key ("x", "op3", "onload") or "@name" for the
 *                 block's header name; `key@L` (editor_cfg's field spec)
 *                 pins the line — a removal names the line it takes, so its
 *                 undo puts the line back exactly there even in a block the
 *                 tree hand-ordered. before/after are "" for "no such
 *                 line" and "=<raw value>" for a line, raw meaning exactly
 *                 the bytes after the '=' in the file (escapes included) —
 *                 the '=' is what tells "absent" from the very common
 *                 present-and-empty `name=`. A new line goes at its canonical
 *                 place; a value that cachepack would refuse (a non-number
 *                 for an int key, a bad hook) is refused here.
 *   IF_COMPONENT  child indices throughout, `-1` = no parent (no `layer=`).
 *                 add (has_after): a FRESH block — `[com_<k>]` (or
 *                   `com_<k>_<j>` when taken), `if3=yes`, `type=0`, its
 *                   `layer=`, `name=`, `targetverb=` — inserted at position
 *                   `index_after`; `parent_after` in the numbering after the
 *                   insert. Every reference to a later component moves up
 *                   one (below), and the preamble's count follows.
 *                 remove (has_before): the inverse; the block at
 *                   `index_before` must be exactly the fresh block and
 *                   nothing may reference it. Bring a block back to fresh
 *                   with IF_FIELDs in the same stroke first
 *                   (Editor_IfPlanDelete), so undo restores every field.
 *                 both: `layer=` of block `index_before` changes from
 *                   `parent_before` to `parent_after` (both in the numbering
 *                   BEFORE the move; no cycle allowed), then the block moves
 *                   from `index_before` to `index_after`.
 *
 * A structural edit renumbers: every `layer=`, every `linked=`, and every
 * `i:` hook argument that is a full id of this interface's own component
 * (interface > 0 only — in interface 0 a full id is a small int and cannot be
 * told from one) is remapped, so the tree keeps its shape and its hooks keep
 * their targets. Scripts that name components by NUMBER outside this file are
 * out of reach; the cache names them through the compack, which a positional
 * rewrite keeps in step (Editor_IfCompackForSave).
 */

#include "editor_cfg.h"
#include "editor_cmd.h"

#include <stddef.h>

struct Editor_Doc;
struct RSCache_Dat2Component;

/* ---- the vocabulary ------------------------------------------------------ */

enum Editor_IfValueKind
{
    EDITOR_IF_VALUE_INT = 0,
    EDITOR_IF_VALUE_BOOL,
    /** Escaped text (cachepack's cp_lines_add_str). */
    EDITOR_IF_VALUE_STR,
    /** `i:<int>` / `s:<text>` arguments, comma-joined; the first is the
     *  script id. */
    EDITOR_IF_VALUE_HOOK,
    /** Comma-joined ints (varptriggers, invtriggers, stattriggers). */
    EDITOR_IF_VALUE_TRIGGERS,
    /** `x,y,graphic` (invslotN). */
    EDITOR_IF_VALUE_INVSLOT,
    /** `opcode,operand` (cs1cmpN). */
    EDITOR_IF_VALUE_CS1CMP,
    /** Comma-joined ints, possibly none (cs1scriptN). */
    EDITOR_IF_VALUE_CS1SCRIPT,
    /** `empty=yes`: a zero-length archive member. */
    EDITOR_IF_VALUE_MARKER,
};

/**
 * The inspector's groups — the order struct ToriRS_Component (and the
 * component's `.if` block) lays its fields out in.
 */
enum Editor_IfGroup
{
    EDITOR_IF_GROUP_IDENTITY = 0,  /* if3, type, clientcode, empty */
    EDITOR_IF_GROUP_GEOMETRY,      /* x, y, width, height, modes, layer, scroll */
    EDITOR_IF_GROUP_GRAPHIC,       /* hidden, trans, colour, fill, graphic, flips, line */
    EDITOR_IF_GROUP_TEXT,          /* text, font, alignment, shadow */
    EDITOR_IF_GROUP_MODEL,         /* model, zoom, angles, offsets, anim, actives */
    EDITOR_IF_GROUP_INVENTORY,     /* margins, invslots, objops */
    EDITOR_IF_GROUP_INTERACTION,   /* ops, click mask, target verb, drag, button */
    EDITOR_IF_GROUP_SCRIPTS,       /* hooks, triggers, cs1 */
    EDITOR_IF_GROUP_COUNT,
};

struct Editor_IfKeyInfo
{
    /** The key, or for an indexed key its prefix ("op" for op1..). */
    const char* key;
    unsigned char kind;    /* enum Editor_IfValueKind */
    unsigned char group;   /* enum Editor_IfGroup */
    /** 1 for an indexed family (`op<N>`, N from 1). */
    unsigned char indexed;
    /** Indexed: the highest N cachepack accepts (0 = unbounded). */
    unsigned short index_max;
    /** What the inspector shows. */
    const char* label;
};

/** Number of vocabulary rows, in canonical (emitter) order. */
int
Editor_IfKeyCount(void);

const struct Editor_IfKeyInfo*
Editor_IfKeyAt(int row);

/**
 * The row a concrete key belongs to ("op3" -> the `op` row, index 2), or -1
 * for a key cachepack does not read. `out_index` (may be NULL) gets the
 * zero-based index of an indexed key, else 0.
 */
int
Editor_IfKeyLookup(
    const char* key,
    int* out_index);

/** Canonical rank of a concrete key (row and index folded), or -1. */
int
Editor_IfKeyRank(const char* key);

/**
 * 1 when `raw` (the bytes after '=', escapes included) is a value cachepack
 * reads for `key` and survives as a line: no line break, no unescaped `//`,
 * no unescaped trailing blank.
 */
int
Editor_IfValueValid(
    const char* key,
    const char* raw);

/* ---- the file ------------------------------------------------------------ */

struct Editor_IfFile
{
    /** Borrowed. */
    const char* text;
    size_t length;
    /** From the preamble's `// Interface N — K components.`; -1 when the
     *  preamble does not say. */
    int interface_id;
    int stated_count;
    /** Block extents, names (editor_cfg's index). */
    struct Editor_CfgFile index;
};

/** Index `text`. Borrows it. */
void
Editor_IfIndex(
    struct Editor_IfFile* file,
    const char* text,
    size_t length);

/** NULL-tolerant. */
void
Editor_IfFree(struct Editor_IfFile* file);

static inline int
Editor_IfComponentCount(const struct Editor_IfFile* file)
{
    return file->index.block_count;
}

/** Block `position`'s name into `out` (truncated to `out_size`). */
void
Editor_IfBlockName(
    const struct Editor_IfFile* file,
    int position,
    char* out,
    size_t out_size);

/** Position of the block named `name`, or -1. */
int
Editor_IfFind(
    const struct Editor_IfFile* file,
    const char* name);

/**
 * The raw value of `key` (occurrence 0) in block `position` into `out`;
 * returns 1 when the line is there. A value longer than `out_size - 1` is
 * cut and the return is 2, so a caller can tell (and refuse to edit it).
 */
int
Editor_IfBlockValue(
    const struct Editor_IfFile* file,
    int position,
    const char* key,
    char* out,
    size_t out_size);

/**
 * The block's parent as a CHILD index: `layer=`'s low half when its high half
 * is this interface, -1 when there is no `layer=`, -2 when the line is there
 * but names another interface or does not parse.
 */
int
Editor_IfBlockParent(
    const struct Editor_IfFile* file,
    int position);

/** The preamble cachepack writes ("// Interface N — K components." and two
 *  more lines, then a blank). Returns the bytes `snprintf` would write. */
int
Editor_IfEmitPreamble(
    const char* interface_name,
    int interface_id,
    int component_count,
    char* out,
    size_t out_size);

/**
 * A new interface's `.if` text: the preamble and one fresh root block
 * (`[universe]`, if3, type 0, widthmode/heightmode 1 — what cachepack writes
 * for a full-size root layer). Owned (ToriMem_Editor), terminated.
 */
char*
Editor_IfNewInterfaceText(
    const char* interface_name,
    int interface_id,
    size_t* out_length);

/* ---- editing ------------------------------------------------------------- */

/**
 * Apply one IF_FIELD / IF_COMPONENT to the `.if` text, forward or inverse.
 * Returns an owned (ToriMem_Editor) terminated text and its length, or NULL
 * when the command does not fit the text (which is then untouched).
 */
char*
Editor_IfEdit(
    const char* text,
    size_t length,
    const struct Editor_Cmd* command,
    int forward,
    size_t* out_length);

/**
 * Editor_IfApply's body: the IFACE document `command` addresses, through
 * Editor_IfEdit. Returns 1 when the text changed (and marks it dirty).
 */
int
Editor_IfDocApply(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction);

/** The fresh block's name for position `position`, given the other blocks'
 *  names in `file` (the block at `skip` is ignored; -1 for none). */
void
Editor_IfFreshName(
    const struct Editor_IfFile* file,
    int position,
    int skip,
    char* out,
    size_t out_size);

/**
 * Build the stroke that deletes component `position` and everything under
 * it, deepest first: for each, IF_FIELD removals of every line but the fresh
 * ones (and `type`/@name back to fresh), then IF_COMPONENT remove. Writes at
 * most `max` commands addressed to `doc_key`; returns how many, or -1 when
 * `max` is too small or a value is too long for a command (the delete would
 * not undo exactly, so it is not offered).
 */
int
Editor_IfPlanDelete(
    const char* text,
    size_t length,
    const char* doc_key,
    int position,
    struct Editor_Cmd* out,
    int max);

/**
 * One IF_FIELD that sets `key` of block `position` to `raw` (NULL removes the
 * line; "" is a present, empty value), with the current value as `before`.
 * `key` may be "@name". Returns 0 when nothing would change, the key or value
 * is not valid, or a value does not fit a command.
 */
int
Editor_IfPlanField(
    const char* text,
    size_t length,
    const char* doc_key,
    int position,
    const char* key,
    const char* raw,
    struct Editor_Cmd* out);

/* ---- the compack --------------------------------------------------------- */

/**
 * 1 when `compack` is positional for `if_text`: its entries, in order, are
 * exactly `0=<block 0>` .. `<n-1>=<block n-1>` (comments and blank lines
 * anywhere). Only then may structure edits run — position is file id.
 */
int
Editor_IfCompackPositional(
    const char* compack,
    size_t compack_length,
    const char* if_text,
    size_t if_length);

/**
 * The compack to save beside `new_if`, given the compack on disk and the
 * `.if` it was written for (`old_if`). Returns 1 with `*out` NULL when the
 * compack stays as it is (the block names did not change), 1 with an owned
 * text when it must be rewritten (positional, structure changed: entry lines
 * become `i=<name>`, comments keep their places), 0 when the names changed
 * on a compack that is not positional — a save that cannot keep ids honest.
 */
int
Editor_IfCompackForSave(
    const char* compack,
    size_t compack_length,
    const char* old_if,
    size_t old_if_length,
    const char* new_if,
    size_t new_if_length,
    char** out,
    size_t* out_length);

/* ---- strings and hooks --------------------------------------------------- */

/**
 * The raw value cachepack writes for a string field holding `text`
 * (cp_lines_add_str's escapes, then line_escape's). Returns the bytes needed
 * (excluding the terminator); `out` is terminated when it fits.
 */
size_t
Editor_IfStringRaw(
    const char* text,
    char* out,
    size_t out_size);

/** The text a string field's raw value holds (cachepack's cp_unescape). */
void
Editor_IfStringText(
    const char* raw,
    char* out,
    size_t out_size);

#define EDITOR_IF_HOOK_ARGS_MAX 64
#define EDITOR_IF_HOOK_TEXT_MAX 256

struct Editor_IfHookArg
{
    int is_string;
    int value;
    /** Unescaped text of a string argument. */
    char text[EDITOR_IF_HOOK_TEXT_MAX];
};

/** A hook line: args[0] is the script id. */
struct Editor_IfHook
{
    int arg_count;
    struct Editor_IfHookArg args[EDITOR_IF_HOOK_ARGS_MAX];
};

/** Parse a hook's raw value. Returns 0 when it is not one cachepack reads
 *  (or holds more than EDITOR_IF_HOOK_ARGS_MAX arguments). */
int
Editor_IfHookParse(
    const char* raw,
    struct Editor_IfHook* out);

/** The raw value for `hook`, as cachepack's emitter spells it. Returns the
 *  bytes needed (excluding the terminator); `out` is terminated when it fits. */
size_t
Editor_IfHookFormat(
    const struct Editor_IfHook* hook,
    char* out,
    size_t out_size);

/** CS2's event placeholders for hook arguments (cs2vm2.h CS2VM_SCRIPT_ARG_*). */
struct Editor_IfEventArg
{
    int value;
    const char* name;
};

int
Editor_IfEventArgCount(void);

const struct Editor_IfEventArg*
Editor_IfEventArgAt(int i);

/** The placeholder's name for `value`, or NULL. */
const char*
Editor_IfEventArgName(int value);

/* ---- the typed layer (editor_if_typed.c; links rscache) ----------------- */

/**
 * Read block `position` of `file` into `out` exactly as cachepack's
 * interface_read does: RSCache_Dat2ComponentInit first, `id` =
 * `(interface << 16) | position`, then every line through the key's reader.
 * Returns 1, or 0 (with `out` still owning what it read; free it) naming the
 * first line it could not read in `out_bad_key` (may be NULL).
 */
int
Editor_IfBlockReadComponent(
    const struct Editor_IfFile* file,
    int position,
    struct RSCache_Dat2Component* out,
    char* out_bad_key,
    size_t bad_key_size);

/**
 * The `key=value` lines cachepack's emit_component writes for `component`,
 * each ending in '\n', against the decoder's defaults. Returns the bytes
 * needed (excluding the terminator); `out` is terminated when it fits.
 */
size_t
Editor_IfComponentEmit(
    const struct RSCache_Dat2Component* component,
    char* out,
    size_t out_size);

#endif
