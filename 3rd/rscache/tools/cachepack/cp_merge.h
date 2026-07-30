#ifndef RSCACHE_TOOLS_CACHEPACK_CP_MERGE_H
#define RSCACHE_TOOLS_CACHEPACK_CP_MERGE_H

/*
 * One record from two layers.
 *
 * `configs/all.npc` (rank 0) is the machine export from the cache; a file under `server/scripts` of the same extension (rank 1) is what a person
 * authored on top. Today they have two readers that never meet — rank 0 is read
 * only by `cachepack pack`, rank 1 only by `mock230_content.c` — which is the
 * problem `docs/CONTENT_ARCHITECTURE.md` §3.5 calls the deepest in the toolchain.
 * Merging them is what lets one encoder see the whole record.
 *
 * Four rules, each because something can currently go wrong quietly:
 *
 *   1. **Order is rank, then path**, never `readdir` order. `cp_walk` supplies it;
 *      `cp_assets.h` records the same lesson for archive members.
 *   2. **Merge per key, not per block.** `[goblin]` in `lumbridge.npc` states 8
 *      keys; the 16 in `all.npc` — models, anims, name — must survive. Replacing
 *      the block would blank every goblin.
 *   3. **A later rank overrides a single-valued key; the same rank stating one
 *      twice is an error**, not last-wins.
 *   4. **`param` is a map, not a single.** It is keyed by param name, so a later
 *      rank overrides *that* param and leaves the others. Treating it as a single
 *      would drop a record's whole param table; treating it as a list would
 *      duplicate an entry the cache already states.
 *
 * Measured on this tree: for npc the two layers share exactly one key (`param`),
 * so the merge is very nearly a union. That is a property of the content, not a
 * guarantee — rule 3 is what catches it changing.
 */

#include "cp_text.h"

struct CP_MergedLine
{
    char* key;
    char* value;
    /** Which rank stated it, for reporting who won. */
    int rank;
    /** The file it came from, borrowed. */
    const char* origin;
};

struct CP_MergedRecord
{
    char* debugname;
    struct CP_MergedLine* lines;
    int count;
    int capacity;
    /** 1 when any rank above 0 contributed a key. */
    int overlaid;
};

struct CP_MergeSet
{
    struct CP_MergedRecord* records;
    int count;
    int capacity;
    /** Records whose keys came from more than one layer. */
    int overlaid_count;
    /**
     * Keys rank 0 states more than once in any one block — the type's
     * multi-valued keys, learned from the machine export.
     *
     * Held per *type*, not per record, because a record can exist only at rank 1:
     * the server allocates its own enums and params (bases 5995 and 2634), and
     * `displaymessage_enum` has no rank-0 block to learn `val`'s arity from.
     */
    char** multi;
    int multi_count;
    int multi_capacity;
};

/**
 * Fold `file` into `set` at `rank`.
 *
 * Blocks are matched by name. A block the set has not seen is appended; one it has
 * is merged per key. Returns 0 on a rule violation (a duplicate key at the same
 * rank), having reported it.
 */
int
cp_merge_add(
    struct CP_MergeSet* set,
    const struct CP_ConfigFile* file,
    int rank,
    const char* origin);

/** The merged record named `debugname`, or NULL. */
const struct CP_MergedRecord*
cp_merge_find(
    const struct CP_MergeSet* set,
    const char* debugname);

void
cp_merge_free(struct CP_MergeSet* set);

#endif
