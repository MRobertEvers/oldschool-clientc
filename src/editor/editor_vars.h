#ifndef SRC_EDITOR_EDITOR_VARS_H
#define SRC_EDITOR_EDITOR_VARS_H

/**
 * Varps and the varbits laid over them (docs/MAPED_DESIGN.html #vars).
 *
 * A varbit is a bit range [startbit, endbit] of one varp (its `basevar`).
 * Two varbits that share a bit of the same varp write each other's values,
 * which is a bug nothing reports until a quest stage reads back wrong — so
 * the editor draws each varp's 32 bits with every varbit over it, flags the
 * bits two of them share, and refuses a new range that would share one.
 *
 * The facts come from the text, never the cache: `configs/all.varbit` (the
 * cache half: basevar, startbit, endbit), each server overlay
 * `server/scripts/.../<file>.varbit` laid over it field by field in path
 * order (a later statement of a field wins), and the varp names that
 * `basevar=` values spell (`configs/all.varp.compack`, `pack/varp.alloc`).
 *
 * Names carry kind and id (OSRS-Content/tools/var_prefix_names.py):
 * `varp<id>_<name>`, `varb<id>_<name>`, `varc<id>_<name>`. Editor_VarsName
 * coins one and, like that tool, does not prefix a name twice.
 */

#include "editor_cmd.h"

#include <stddef.h>
#include <stdint.h>

enum Editor_VarKind
{
    EDITOR_VAR_VARP = 0,
    EDITOR_VAR_VARBIT,
    EDITOR_VAR_VARC,
};

/** Bits in a varp. */
#define EDITOR_VARP_BITS 32

/** Bytes of a var name here, the terminator included. Wider than a
 *  command's name (EDITOR_CMD_NAME_MAX): the tree has varbit names of up to
 *  79 bytes, and an index that dropped them would draw their bits free. */
#define EDITOR_VARS_NAME_MAX 128

/**
 * `<prefix><id>_<name>`: "varp", "varb" or "varc". A `name` that already
 * carries this kind's prefix with this id is not prefixed again (the tool's
 * base_name); any other spelling is kept whole. Returns 0 for an empty name,
 * a negative id, or a result that does not fit `out_size`.
 */
int
Editor_VarsName(
    enum Editor_VarKind kind,
    int id,
    const char* name,
    char* out,
    size_t out_size);

/**
 * Read a prefixed name back: kind, id and the bare name. Returns 0 when
 * `name` is not `(varp|varb|varc)<digits>_<rest>`.
 */
int
Editor_VarsNameParse(
    const char* name,
    enum Editor_VarKind* out_kind,
    int* out_id,
    char* out_base,
    size_t base_size);

struct Editor_Varbit
{
    char name[EDITOR_VARS_NAME_MAX];
    /** From the name's prefix or a member line; -1 when neither states it. */
    int id;
    /** The `basevar=` value as written, and the varp it resolves to (-1:
     *  unresolved — no prefix and no varp of that name). */
    char basevar_name[EDITOR_VARS_NAME_MAX];
    int basevar;
    /** -1 when not stated (or `default`). */
    int startbit;
    int endbit;
};

/** One varp name -> id binding. */
struct Editor_VarpName
{
    char name[EDITOR_VARS_NAME_MAX];
    int id;
};

struct Editor_VarsIndex
{
    /** Owned. Records in first-seen order; an overlay edits in place. */
    struct Editor_Varbit* varbits;
    int varbit_count;
    int varbit_capacity;

    /** Owned: varbit ids by name from member lines (`id=name`). */
    struct Editor_VarpName* varbit_members;
    int varbit_member_count;
    int varbit_member_capacity;

    /** Owned: varp names. */
    struct Editor_VarpName* varps;
    int varp_count;
    int varp_capacity;

    /** Owned name -> index slot tables (open addressing, -1 = empty) over
     *  `varbits` and `varps`. */
    int* varbit_slots;
    int varbit_slot_capacity;
    int* varp_slots;
    int varp_slot_capacity;

    /** Owned, built by Editor_VarsIndexFinish: varbit indices with a placed
     *  range, sorted by (basevar, startbit, endbit, index). */
    int* order;
    int order_count;
    int finished;
};

/** The bits of one varp. */
struct Editor_VarpBits
{
    int varp;
    /** Bit n set: some varbit covers bit n. */
    uint32_t used;
    /** Bit n set: two or more varbits cover bit n. */
    uint32_t overlapped;
    /** Indices into `varbits` of every varbit placed on this varp, by
     *  startbit; `varbit_count` may exceed the array (then it is cut). */
    int varbits[64];
    int varbit_count;
};

void
Editor_VarsIndexInit(struct Editor_VarsIndex* index);

/** Frees and zeroes. NULL-tolerant. */
void
Editor_VarsIndexFree(struct Editor_VarsIndex* index);

/**
 * Feed varp names: `[varp<id>_<name>]` headers (all.varp, an overlay) and
 * `<id>=<name>` member lines (all.varp.compack, pack/varp.alloc). The first
 * binding of a name wins.
 */
void
Editor_VarsAddVarps(
    struct Editor_VarsIndex* index,
    const char* text,
    size_t length);

/**
 * Feed varbit text: `<id>=<name>` member lines (all.varbit.compack) and
 * `[name]` blocks with basevar / startbit / endbit (all.varbit, then each
 * overlay). A block naming a varbit already read edits that record: each
 * field it states replaces the one before.
 */
void
Editor_VarsAddVarbits(
    struct Editor_VarsIndex* index,
    const char* text,
    size_t length);

/**
 * Feed the tree: all.varp.compack and pack/varp.alloc (varp names),
 * all.varbit.compack, all.varbit, then every `*.varbit` under server/scripts
 * in sorted path order. Read-only; finishes the index. Returns 0 when
 * `configs/all.varbit` cannot be read.
 */
int
Editor_VarsIndexLoad(
    struct Editor_VarsIndex* index,
    const char* content_dir);

/** Resolve every basevar and sort. Feeding more text unfinishes. */
void
Editor_VarsIndexFinish(struct Editor_VarsIndex* index);

/** The varp named `name` (also as its prefixed spelling), or -1. */
int
Editor_VarsVarpId(
    const struct Editor_VarsIndex* index,
    const char* name);

/** The varbit record named `name`, or -1 (its index in `varbits`). */
int
Editor_VarsVarbitFind(
    const struct Editor_VarsIndex* index,
    const char* name);

/** Lay every varbit over `varp`'s 32 bits. Needs a finished index. Returns
 *  the number of varbits placed on it. */
int
Editor_VarsBitMap(
    const struct Editor_VarsIndex* index,
    int varp,
    struct Editor_VarpBits* out);

/**
 * May [startbit, endbit] of `varp` hold a new varbit? 1 = free; 0 = a varbit
 * already covers a bit of it (the first such, by startbit, into
 * `*out_conflict` as an index into `varbits`); -1 = not a range (start > end,
 * or outside 0..31). The varbit at index `ignore` (-1: none) is left out, so
 * a varbit's own range can be moved.
 */
int
Editor_VarsRangeFree(
    const struct Editor_VarsIndex* index,
    int varp,
    int startbit,
    int endbit,
    int ignore,
    int* out_conflict);

/** The lowest start of `width` free bits of `varp`, or -1. */
int
Editor_VarsFindFree(
    const struct Editor_VarsIndex* index,
    int varp,
    int width);

/**
 * Every pair of varbits on one varp sharing a bit. Calls `each` (may be NULL)
 * per pair with the two varbit indices; returns the number of pairs.
 */
int
Editor_VarsOverlaps(
    const struct Editor_VarsIndex* index,
    void (*each)(void* user_data, int first, int second),
    void* user_data);

/**
 * The fields of a new varbit `name` on `varp` bits [startbit, endbit] as
 * CFG_FIELD adds for the CFG document "varbit:<name>": basevar (the varp's
 * spelling), startbit, endbit. The record's CFG_BLOCK add and its id come
 * from editor_alloc (EDITOR_ALLOC_MEMBER_INDEX). Refuses — returns 0, emits
 * nothing — an unknown varp, a range that is not one, or one a varbit
 * already covers. Returns the number of commands (3), or 0.
 */
int
Editor_VarsEmitVarbit(
    const struct Editor_VarsIndex* index,
    const char* name,
    int varp,
    int startbit,
    int endbit,
    struct Editor_Cmd* out,
    int max);

#endif
