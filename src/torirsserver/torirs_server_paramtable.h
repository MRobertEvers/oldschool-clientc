#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_PARAMTABLE_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_PARAMTABLE_H

#include <stddef.h>
#include <stdint.h>

struct RSCache_Params;

/*
 * One flat, sorted (owner, key) -> value table, shared by every config type
 * that carries a param map.
 *
 * There were two copies of this before — `struct ObjParam` in
 * torirs_server_objinfo.c and `struct NpcParam` in torirs_server_npcinfo.c, each with its
 * own `compare_*`, its own `add_param`, its own `read_params` and its own
 * `qsort`. loc and struct would have made four. That is four chances to
 * reintroduce the one bug in this family that does not announce itself:
 *
 *   A record's own params arrive in the order the *cache* wrote them, and that
 *   order is not ascending by key. Measured on cache.osrs239: 525 of the 599
 *   loc records carrying two or more params are out of key order (87.6%), and
 *   1,847 of struct's 2,833 (65.2%). A binary search over an almost-sorted
 *   array does not crash — it misses, the lookup reports "this record has no
 *   such param", and content pushes the declared default instead of the value.
 *   Wrong everywhere, loud nowhere.
 *
 * So the sort is explicit, it happens once after the whole table is built, and
 * `ToriRSServer_ParamTableFind` asserts nothing about construction order.
 *
 * `sval` is non-NULL exactly when the cache marked the entry a string, and that
 * is a different question from what `configs/all.param` *declares* the param to
 * be. Choose a stack by the declaration (`ToriRSServer_ContentParamType`); read
 * the value by this. When the two disagree the record is wrong and saying so
 * beats guessing which half to believe — see `ToriRSServer_PushTypedParam`.
 */
struct ToriRSServerParamRow
{
    /** The obj / npc / loc / struct id this row hangs off. */
    int32_t owner;
    int32_t key;
    /** The int value, or 0 when this row is a string. */
    int32_t ival;
    /** Owned. NULL unless the cache marked this entry a string. */
    char* sval;
};

struct ToriRSServerParamTable
{
    struct ToriRSServerParamRow* rows;
    int count;
    int capacity;
    /** Set by `_sort`, cleared by `_read`. `_find` refuses an unsorted table. */
    int sorted;
};

/**
 * Copy one record's whole param map into the table.
 *
 * RSCACHE_PARAM_LONG is dropped rather than truncated: eight bytes squeezed
 * onto a 32-bit stack is a wrong answer that looks right, where a missing param
 * reports as "not set" — a state content already handles. cache.osrs239 emits
 * none in the obj, npc, loc or struct tables.
 *
 * String values are strdup'd; the record they came from is normally freed as
 * soon as the decode loop moves on.
 */
void
ToriRSServer_ParamTableRead(
    struct ToriRSServerParamTable* table,
    int owner,
    const struct RSCache_Params* params);

/**
 * Set one int row, replacing any row this owner already has for that key.
 *
 * The entry point the *content overlay* uses, where `_read` is the one the cache
 * uses. A `.loc` block's `param=next_loc_stage,poordooropen` is a param on that
 * record in exactly the sense the cache's own param map is — and until
 * 2026-08-02 it was not: it landed in a private field of
 * `struct ToriRSServerLocDef` that only C could read, so `loc_param(next_loc_stage)`
 * — the reference's own line in `doors/scripts/doors.rs2` — answered the
 * declared default instead. An overlay param no script can read is not a param.
 */
void
ToriRSServer_ParamTableSetInt(
    struct ToriRSServerParamTable* table,
    int owner,
    int key,
    int ival);

/** Sort by (owner, key). Call once, after the last `_read` / `_set_int`. */
void
ToriRSServer_ParamTableSort(struct ToriRSServerParamTable* table);

/** The row, or NULL when this owner does not carry this param. */
const struct ToriRSServerParamRow*
ToriRSServer_ParamTableFind(
    const struct ToriRSServerParamTable* table,
    int owner,
    int key);

void
ToriRSServer_ParamTableFree(struct ToriRSServerParamTable* table);

/** Retained bytes, for the one-line load report each table prints. */
size_t
ToriRSServer_ParamTableBytes(const struct ToriRSServerParamTable* table);

#endif
