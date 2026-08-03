#ifndef RS_LOOT_STORE_H
#define RS_LOOT_STORE_H

#include <stdbool.h>

/*
 * Client-native loot tracker store.
 *
 * The OSRS loot tracker is a client-side feature: kill loot is recorded into a
 * per-source grouping that CS2 scripts query through the 7600-family host ops.
 * No game packet populates it — the server hook (AddKillLoot) feeds the store
 * directly when an NPC death drop is rolled.
 *
 * Data model, reverse-engineered from decompiled CS2 (scripts 7166, 4298,
 * 4452, 7200, 7159):
 *
 *   Sources  — keyed by (auto-id, name). Each holds rows of (obj_id, qty,
 *              value). A source is e.g. "Goblin" or "Kalphite Queen".
 *
 *   Aux lists — three kind-indexed string lists (kind 0 = main loot entries,
 *              kind 1 = sources via ops 7625/7626, kind 2 = ground-item names
 *              via 7619/7620). Ops 7401/7404/7407/7408/7409 operate on these.
 *
 *   Ignore   — source names the user has muted; ops 7614/7616 add,
 *              7617 removes, 7621 clears.
 *
 *   Query    — a cursor set by 7605 (begin) and walked by 7606 (index→id).
 *
 * Modelled on VarCManager: Init/Free/ResetAll, grow-on-demand, asserts for
 * programming errors per .cursor/rules/c-assert-invariants.mdc.
 */

#define LOOT_AUX_KIND_MAX 3

struct LootRow
{
    int obj_id;
    int qty;
    int value;
};

struct LootSource
{
    int id;
    char* name;
    struct LootRow* rows;
    int row_count;
    int row_cap;
};

struct LootAuxList
{
    char** entries;
    int count;
    int cap;
};

struct LootStore
{
    struct LootSource* sources;
    int source_count;
    int source_cap;
    int next_source_id;

    char** ignored;
    int ignored_count;
    int ignored_cap;

    struct LootAuxList aux[LOOT_AUX_KIND_MAX];

    int* query_ids;
    int query_count;
    int query_cap;
};

void
LootStore_Init(struct LootStore* store);

void
LootStore_Free(struct LootStore* store);

void
LootStore_ResetAll(struct LootStore* store);

/* --- Populate hook (server/engine side) --------------------------------- */

/** Record one loot drop. Creates the source group if it does not exist;
 *  appends or merges a row for obj_id. */
void
LootStore_AddKillLoot(
    struct LootStore* store,
    const char* source_name,
    int obj_id,
    int qty,
    int value);

/* --- Source queries (backing ops 7601..7606, 7630) ----------------------- */

/** Total number of recorded sources (op 7601). */
int
LootStore_SourceCount(const struct LootStore* store);

/** Source id → name, or "" if not found (op 7602 / 7630). */
const char*
LootStore_SourceName(
    const struct LootStore* store,
    int source_id);

/** Source name → number of distinct item rows (op 7603). */
int
LootStore_SourceItemCount(
    const struct LootStore* store,
    const char* source_name);

/** Source name → total value aggregate (op 7604). */
int
LootStore_SourceTotalValue(
    const struct LootStore* store,
    const char* source_name);

/** Begin a query: populate query_ids from sources or aux list and return the
 *  result count (op 7605). kind=1 queries sources, kind=0/2 queries aux. */
int
LootStore_BeginQuery(
    struct LootStore* store,
    int start,
    int limit,
    int kind);

/** Read the source id at query index (op 7606). Returns -1 on out-of-range. */
int
LootStore_QueryId(
    const struct LootStore* store,
    int index);

/* --- Per-source row access (backing ops 7609..7612) --------------------- */

/** Count of rows under a source looked up by name (op 7609). */
int
LootStore_RowCountByName(
    const struct LootStore* store,
    const char* source_name);

/** Count of rows under a source looked up by id (op 7610). */
int
LootStore_RowCountById(
    const struct LootStore* store,
    int source_id);

/** Fetch (obj_id, qty) by source name + row index (op 7611). Returns false
 *  on miss. */
bool
LootStore_RowByName(
    const struct LootStore* store,
    const char* source_name,
    int index,
    int* out_obj_id,
    int* out_qty);

/** Fetch (obj_id, qty) by source id + row index (op 7612). Returns false
 *  on miss. */
bool
LootStore_RowById(
    const struct LootStore* store,
    int source_id,
    int index,
    int* out_obj_id,
    int* out_qty);

/* --- Aux string lists (backing ops 7401/7404/7407/7408/7409) ------------ */

/** Upsert a string into aux list `kind` (op 7401). flag is unused for now. */
void
LootStore_AuxUpsert(
    struct LootStore* store,
    int kind,
    const char* str,
    int flag);

/** Remove a string from aux list `kind` (op 7404). */
void
LootStore_AuxRemove(
    struct LootStore* store,
    int kind,
    const char* str,
    int flag);

/** Length of aux list `kind` (op 7407). */
int
LootStore_AuxCount(
    const struct LootStore* store,
    int kind);

/** Lookup: does `str` exist in aux list `kind`? (op 7408). */
int
LootStore_AuxLookup(
    const struct LootStore* store,
    int kind,
    const char* str,
    int arg3,
    int arg4);

/** Retrieve string at index from aux list `kind` (op 7406). */
const char*
LootStore_AuxGet(
    const struct LootStore* store,
    int kind,
    int index);

/** Clear aux list `kind` (op 7409). */
void
LootStore_AuxClear(
    struct LootStore* store,
    int kind);

/* --- Shorthand accessors for specific aux kinds (7619/7620/7625/7626) --- */

/** Count of kind-2 ground-item names (op 7619). */
int
LootStore_GroundItemCount(const struct LootStore* store);

/** Index → ground-item name string (op 7620). */
const char*
LootStore_GroundItemName(
    const struct LootStore* store,
    int index);

/** Count of kind-1 source names (op 7625). */
int
LootStore_SourceListCount(const struct LootStore* store);

/** Index → source list name string (op 7626). */
const char*
LootStore_SourceListName(
    const struct LootStore* store,
    int index);

/* --- Ignore list (backing ops 7614/7616/7617/7621) ---------------------- */

void
LootStore_IgnoreAdd(
    struct LootStore* store,
    const char* name);

void
LootStore_IgnoreRemove(
    struct LootStore* store,
    const char* name);

void
LootStore_IgnoreClear(struct LootStore* store);

bool
LootStore_IsIgnored(
    const struct LootStore* store,
    const char* name);

/* --- Removal (backing op 7613/7615) ------------------------------------- */

/** Clear the entire store (op 7613). */
void
LootStore_ClearAll(struct LootStore* store);

/** Remove a source by id (op 7615). */
void
LootStore_RemoveById(
    struct LootStore* store,
    int source_id);

/* --- Aux count total (op 7608) ------------------------------------------ */

/** Total entry count across all aux lists (op 7608). */
int
LootStore_AuxCountTotal(const struct LootStore* store);

#endif /* RS_LOOT_STORE_H */
