/*
 * Allocation and container primitives for the CS2 compiler/decompiler.
 *
 * The reference implementation (RuneStar/cs2, Kotlin) leans on a garbage
 * collector: the IR is a graph of small immutable-ish nodes that reference each
 * other freely, and nothing is ever explicitly released. Reproducing that
 * ownership graph with malloc/free would mean inventing a lifetime story the
 * original never had.
 *
 * Instead everything a decompile produces lives in one arena that is released
 * whole. Node lifetimes then match the reference exactly (nothing dies early)
 * without any node owning any other, which is what lets the IR passes rewire
 * expressions as freely as the Kotlin does.
 */
#ifndef RSCACHE_CS2_SUPPORT_H
#define RSCACHE_CS2_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Arena
 * ---------------------------------------------------------------------- */

struct RSCache_CS2_ArenaBlock;
struct RSCache_CS2_Vec;

struct RSCache_CS2_Arena
{
    struct RSCache_CS2_ArenaBlock* head;
    size_t bytes_allocated;

    /* Vectors embedded in arena-allocated nodes. Their backing arrays grow, so
     * they cannot live in the arena themselves; registering them here keeps the
     * arena the single place a decompile's memory is released. */
    struct RSCache_CS2_Vec** tracked_vecs;
    int tracked_count;
    int tracked_capacity;
};

void
RSCache_CS2_ArenaInit(struct RSCache_CS2_Arena* arena);

/** Free `vec` when the arena is freed. */
void
RSCache_CS2_ArenaTrackVec(struct RSCache_CS2_Arena* arena, struct RSCache_CS2_Vec* vec);

/** Never returns NULL: an exhausted arena aborts, as a failed GC would. */
void*
RSCache_CS2_ArenaAlloc(struct RSCache_CS2_Arena* arena, size_t size);

char*
RSCache_CS2_ArenaStrDup(struct RSCache_CS2_Arena* arena, const char* s);

char*
RSCache_CS2_ArenaStrDupN(struct RSCache_CS2_Arena* arena, const char* s, size_t n);

void
RSCache_CS2_ArenaFree(struct RSCache_CS2_Arena* arena);

/* -------------------------------------------------------------------------
 * Vector of pointers
 * ---------------------------------------------------------------------- */

struct RSCache_CS2_Vec
{
    void** items;
    int count;
    int capacity;
};

void
RSCache_CS2_VecInit(struct RSCache_CS2_Vec* vec);

void
RSCache_CS2_VecPush(struct RSCache_CS2_Vec* vec, void* item);

void
RSCache_CS2_VecInsert(struct RSCache_CS2_Vec* vec, int index, void* item);

void
RSCache_CS2_VecRemoveAt(struct RSCache_CS2_Vec* vec, int index);

/** Removes the first pointer-equal entry. Returns false when absent. */
bool
RSCache_CS2_VecRemove(struct RSCache_CS2_Vec* vec, void* item);

int
RSCache_CS2_VecIndexOf(const struct RSCache_CS2_Vec* vec, void* item);

bool
RSCache_CS2_VecContains(const struct RSCache_CS2_Vec* vec, void* item);

/** Push only when absent — the set discipline the Kotlin gets from HashSet. */
bool
RSCache_CS2_VecAddUnique(struct RSCache_CS2_Vec* vec, void* item);

void
RSCache_CS2_VecClear(struct RSCache_CS2_Vec* vec);

void
RSCache_CS2_VecFree(struct RSCache_CS2_Vec* vec);

/** Copy `vec` into arena memory as a NULL-free array of `count` pointers. */
void**
RSCache_CS2_VecToArena(const struct RSCache_CS2_Vec* vec, struct RSCache_CS2_Arena* arena);

/* -------------------------------------------------------------------------
 * Vector of ints
 * ---------------------------------------------------------------------- */

struct RSCache_CS2_IntVec
{
    int* items;
    int count;
    int capacity;
};

void
RSCache_CS2_IntVecInit(struct RSCache_CS2_IntVec* vec);

void
RSCache_CS2_IntVecPush(struct RSCache_CS2_IntVec* vec, int value);

bool
RSCache_CS2_IntVecContains(const struct RSCache_CS2_IntVec* vec, int value);

bool
RSCache_CS2_IntVecAddUnique(struct RSCache_CS2_IntVec* vec, int value);

void
RSCache_CS2_IntVecClear(struct RSCache_CS2_IntVec* vec);

void
RSCache_CS2_IntVecFree(struct RSCache_CS2_IntVec* vec);

/* -------------------------------------------------------------------------
 * Hash maps
 *
 * Two flavours, because the reference uses both and they are not
 * interchangeable: Kotlin `data class` keys (Variable, Prototype) compare by
 * value, plain `class` keys (Element.Constant, Expression.Operation) compare by
 * identity. Value-keyed maps here are replaced by interning the key — see
 * cs2_ir.h — so both end up as pointer maps, with the int map serving script
 * ids and opcode ids.
 * ---------------------------------------------------------------------- */

struct RSCache_CS2_MapEntry
{
    const void* key;
    void* value;
    bool occupied;
};

struct RSCache_CS2_Map
{
    struct RSCache_CS2_MapEntry* entries;
    int count;
    int capacity;
};

void
RSCache_CS2_MapInit(struct RSCache_CS2_Map* map);

void
RSCache_CS2_MapPut(struct RSCache_CS2_Map* map, const void* key, void* value);

void*
RSCache_CS2_MapGet(const struct RSCache_CS2_Map* map, const void* key);

bool
RSCache_CS2_MapContains(const struct RSCache_CS2_Map* map, const void* key);

/** Returns the removed value, or NULL when the key was absent. */
void*
RSCache_CS2_MapRemove(struct RSCache_CS2_Map* map, const void* key);

void
RSCache_CS2_MapFree(struct RSCache_CS2_Map* map);

struct RSCache_CS2_IntMapEntry
{
    int key;
    void* value;
    bool occupied;
};

struct RSCache_CS2_IntMap
{
    struct RSCache_CS2_IntMapEntry* entries;
    int count;
    int capacity;
};

void
RSCache_CS2_IntMapInit(struct RSCache_CS2_IntMap* map);

void
RSCache_CS2_IntMapPut(struct RSCache_CS2_IntMap* map, int key, void* value);

void*
RSCache_CS2_IntMapGet(const struct RSCache_CS2_IntMap* map, int key);

bool
RSCache_CS2_IntMapContains(const struct RSCache_CS2_IntMap* map, int key);

void*
RSCache_CS2_IntMapRemove(struct RSCache_CS2_IntMap* map, int key);

void
RSCache_CS2_IntMapFree(struct RSCache_CS2_IntMap* map);

/* -------------------------------------------------------------------------
 * String builder
 * ---------------------------------------------------------------------- */

struct RSCache_CS2_StrBuf
{
    char* data;
    int length;
    int capacity;
};

void
RSCache_CS2_StrBufInit(struct RSCache_CS2_StrBuf* buf);

void
RSCache_CS2_StrBufAppend(struct RSCache_CS2_StrBuf* buf, const char* s);

void
RSCache_CS2_StrBufAppendN(struct RSCache_CS2_StrBuf* buf, const char* s, int n);

void
RSCache_CS2_StrBufAppendChar(struct RSCache_CS2_StrBuf* buf, char c);

void
RSCache_CS2_StrBufAppendInt(struct RSCache_CS2_StrBuf* buf, int value);

void
RSCache_CS2_StrBufAppendFormat(struct RSCache_CS2_StrBuf* buf, const char* fmt, ...);

void
RSCache_CS2_StrBufClear(struct RSCache_CS2_StrBuf* buf);

/** NUL-terminated view of the contents; invalidated by the next append. */
const char*
RSCache_CS2_StrBufCStr(struct RSCache_CS2_StrBuf* buf);

void
RSCache_CS2_StrBufFree(struct RSCache_CS2_StrBuf* buf);

#endif
