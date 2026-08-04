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
 * Data model (scripts 7166, 7179, 7199, 1791, 1792, 7200, debug 1223-1228):
 *
 *   Sources       — keyed by (auto-id, name). Rows of (obj_id, qty, value).
 *                   kill_count bumped once per distinct LOOT_ADD event_id.
 *                   Opcode 7604 returns kill_count (UI "Name x N"), not GP.
 *   Aux lists     — scratch string lists kinds 0..4 (7400-family). Scripts
 *                   1792/7200 rebuild kinds 1/2 from the persistent ignore
 *                   lists; Ground Items highlight/filter use kinds 3/4.
 *   Item ignore   — 7616/7617/7621; also backs 1-based 7619/7620.
 *   Source ignore — 7622/7623; also backs 1-based 7625/7626.
 *   Clear source  — 7614 by name; 7615 by id; 7613 clears all sources.
 *   Query         — 7605 begin / 7606 index→id. Kinds 1, 2, and 3 = sources.
 *
 * Modelled on VarCManager: Init/Free/ResetAll, grow-on-demand, asserts for
 * programming errors per .cursor/rules/c-assert-invariants.mdc.
 */

#define LOOT_AUX_KIND_MAX 5

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
    /* Kill count for opcode 7604 / "Name x N". Bumped once per distinct
     * event_id so multi-item drops from one death count as one kill. */
    int kill_count;
    int last_event_id;
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

    /* Item ignore (7616/7617/7621); backs 7619/7620. */
    char** item_ignored;
    int item_ignored_count;
    int item_ignored_cap;

    /* Source ignore (7622/7623); backs 7625/7626. */
    char** source_ignored;
    int source_ignored_count;
    int source_ignored_cap;

    struct LootAuxList aux[LOOT_AUX_KIND_MAX];

    int* query_ids;
    int query_count;
    int query_cap;

    /* Monotonic ids for debug/seed paths that call AddKillLoot without a
     * server event_id (App_LootNotifyKill). */
    int next_event_id;
};

void
LootStore_Init(struct LootStore* store);

void
LootStore_Free(struct LootStore* store);

void
LootStore_ResetAll(struct LootStore* store);

/* --- Populate hook (server/engine side) --------------------------------- */

/** Record one loot drop. Creates the source group if it does not exist;
 *  appends or merges a row for obj_id. Increments kill_count when event_id
 *  differs from the source's last seen event (multi-item kills share one id).
 *  Does not consult ignore lists. */
void
LootStore_AddKillLoot(
    struct LootStore* store,
    const char* source_name,
    int obj_id,
    int qty,
    int value,
    int event_id);

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

/** Source name → kill count (op 7604). Scripts 7166/7160 store this as the
 *  component scroll height that becomes "Name x N" / Total count. */
int
LootStore_SourceKillCount(
    const struct LootStore* store,
    const char* source_name);

/** Begin a query: populate query_ids from sources and return the result
 *  count (op 7605). Kinds 1, 2, and 3 all walk the source list. */
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

int
LootStore_RowCountByName(
    const struct LootStore* store,
    const char* source_name);

int
LootStore_RowCountById(
    const struct LootStore* store,
    int source_id);

/** 1-based row index (ops 7611 / script 4298). */
bool
LootStore_RowByName(
    const struct LootStore* store,
    const char* source_name,
    int index_1based,
    int* out_obj_id,
    int* out_qty);

/** 1-based row index (ops 7612 / script 4452). */
bool
LootStore_RowById(
    const struct LootStore* store,
    int source_id,
    int index_1based,
    int* out_obj_id,
    int* out_qty);

/* --- Aux string lists (backing ops 7401/7404/7407/7408/7409) ------------ */

void
LootStore_AuxUpsert(
    struct LootStore* store,
    int kind,
    const char* str,
    int flag);

void
LootStore_AuxRemove(
    struct LootStore* store,
    int kind,
    const char* str,
    int flag);

int
LootStore_AuxCount(
    const struct LootStore* store,
    int kind);

int
LootStore_AuxLookup(
    const struct LootStore* store,
    int kind,
    const char* str,
    int arg3,
    int arg4);

const char*
LootStore_AuxGet(
    const struct LootStore* store,
    int kind,
    int index);

void
LootStore_AuxClear(
    struct LootStore* store,
    int kind);

int
LootStore_AuxCountTotal(const struct LootStore* store);

/* --- Item ignore (7616/7617/7621) + 1-based 7619/7620 -------------------- */

void
LootStore_ItemIgnoreAdd(
    struct LootStore* store,
    const char* name);

void
LootStore_ItemIgnoreRemove(
    struct LootStore* store,
    const char* name);

void
LootStore_ItemIgnoreClear(struct LootStore* store);

bool
LootStore_IsItemIgnored(
    const struct LootStore* store,
    const char* name);

/** Count of item-ignore entries (op 7619). */
int
LootStore_ItemIgnoreCount(const struct LootStore* store);

/** 1-based index → item-ignore name (op 7620). Returns "" if out of range. */
const char*
LootStore_ItemIgnoreName(
    const struct LootStore* store,
    int index_1based);

/* Compat aliases used by older host/test call sites. */
#define LootStore_IgnoreAdd LootStore_ItemIgnoreAdd
#define LootStore_IgnoreRemove LootStore_ItemIgnoreRemove
#define LootStore_IgnoreClear LootStore_ItemIgnoreClear
#define LootStore_IsIgnored LootStore_IsItemIgnored
#define LootStore_GroundItemCount LootStore_ItemIgnoreCount
#define LootStore_GroundItemName LootStore_ItemIgnoreName

/* --- Source ignore (7622/7623) + 1-based 7625/7626 ----------------------- */

void
LootStore_SourceIgnoreAdd(
    struct LootStore* store,
    const char* name);

void
LootStore_SourceIgnoreRemove(
    struct LootStore* store,
    const char* name);

bool
LootStore_IsSourceIgnored(
    const struct LootStore* store,
    const char* name);

/** Count of source-ignore entries (op 7625). */
int
LootStore_SourceIgnoreCount(const struct LootStore* store);

/** 1-based index → source-ignore name (op 7626). */
const char*
LootStore_SourceIgnoreName(
    const struct LootStore* store,
    int index_1based);

#define LootStore_SourceListCount LootStore_SourceIgnoreCount
#define LootStore_SourceListName LootStore_SourceIgnoreName

/* --- Removal (backing ops 7613/7614/7615) -------------------------------- */

/** Clear all recorded sources (op 7613). Does not clear ignore/aux. */
void
LootStore_ClearAll(struct LootStore* store);

/** Remove the source with the given name (op 7614). */
void
LootStore_ClearSourceByName(
    struct LootStore* store,
    const char* name);

/** Remove a source by id (op 7615). */
void
LootStore_RemoveById(
    struct LootStore* store,
    int source_id);

#endif /* RS_LOOT_STORE_H */
