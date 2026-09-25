#ifndef RSCACHE_TOOLS_CACHEPACK_CP_MERGE_H
#define RSCACHE_TOOLS_CACHEPACK_CP_MERGE_H

/*
 * One record from two layers.
 *
 * `configs/all.npc` (rank 0) is the machine export from the cache; a file under `server/scripts` of the same extension (rank 1) is what a person
 * authored on top. Today they have two readers that never meet — rank 0 is read
 * only by `cachepack pack`, rank 1 only by `torirs_server_content.c` — which is the
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
    /**
     * The rank of the layer that first declared the block.
     *
     * Not derivable from the lines, which is the whole reason it is here: 3,280 of
     * this cache's enums decode to a `[enum_0]` with **no keys at all**, so
     * "does any line have rank 0" answers no for a record the cache certainly
     * states. Getting that wrong made every empty record look tree-added and
     * dropped them from the pack.
     */
    int origin_rank;
};

/** One open-addressing slot: the name's hash, and its index in `records` + 1
 *  (0 meaning empty, so a zeroed table is an empty table). */
struct CP_MergeIndexSlot
{
    unsigned hash;
    int record_plus_one;
};

struct CP_MergeSet
{
    struct CP_MergedRecord* records;
    int count;
    int capacity;
    /*
     * debugname -> record, so merging is linear rather than quadratic.
     *
     * find_or_add used to strcmp its way through every record merged so far,
     * once per record in the layer. A config type is not a handful of records —
     * `loc` merges 61,413 — so that one type ran on the order of 1.9e9 string
     * comparisons before a single byte was encoded.
     *
     * Indices, not pointers: `records` is realloc'd as it grows, and a stored
     * pointer would dangle the moment it did.
     */
    struct CP_MergeIndexSlot* index;
    int index_capacity;
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

    /*
     * Every key rank 0 states, at any arity.
     *
     * Arity is learned from rank 0, which works only for keys rank 0 uses. The
     * two layers do not share a vocabulary: a dbrow's values are `values=` in
     * the machine export and `data=` in the authored tree, so `data` appears
     * nowhere at rank 0 and its arity can never be observed there. Eight
     * authored files repeat it — the areas' coord_pair rows, trawler, the
     * Legends zones — and every one was rejected as an ambiguous duplicate.
     *
     * So the rule is split by what rank 0 knows. A key rank 0 states is one
     * rank 0 has an opinion about, and a rank-1 repeat of a key it calls
     * single-valued is the real ambiguity the check exists for. A key rank 0
     * never states is one the authored layer alone defines, and a repeat there
     * is a list — the same conclusion `learn_arity` would have drawn.
     */
    char** seen0;
    int seen0_count;
    int seen0_capacity;
};

/**
 * The rank a source file merges at.
 *
 * Three layers, not two, and the third one exists because the tree has files
 * nobody wrote by hand. `index == 0` is the cache export (rank 0). Above it the
 * authored layer splits: `*.generated.*` — `combat_stats.generated.npc`,
 * `npc_anims.generated.npc`, the machine tables — merges at rank 1, and a file
 * a person wrote at rank 2, so the person wins.
 *
 * That is not this reader's opinion; it is `torirs_server_content.c`'s, which has
 * always loaded npc configs in three passes (`load_npc_default_config`, then
 * `load_npc_generated_config`, then `load_npc_authored_config`). Flattening
 * both authored classes to one rank here made the merge resolve ties by
 * directory order instead — `npc/` sorts after `areas/`, so a generated anim
 * table silently outranked the hand-written God Wars block — and the two
 * readers disagreed about 56 npcs while each was internally consistent. The
 * server band is validated against the runtime's parse, so every one of those
 * surfaced as a mismatched archive that no amount of re-packing could settle.
 *
 * Ties *within* a class stay a hard error, which is what the rule is for: two
 * hand-written files arguing about one npc is a content bug with no defined
 * answer.
 */
int
cp_merge_rank_for(int index, const char* path);

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
