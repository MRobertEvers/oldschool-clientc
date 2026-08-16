#ifndef TORIRS_LRU_H
#define TORIRS_LRU_H

/*
 * Intrusive doubly-linked LRU + open-addressed id map.
 *
 * Entries embed TorirsLruNode and are owned by the caller. Touch moves to MRU;
 * EvictOldest returns the LRU entry (caller frees payload). Capacity is a soft
 * max on live entries — insert past capacity returns the evicted entry so the
 * caller can free it in the same step.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct TorirsLruNode
{
    int64_t key;
    struct TorirsLruNode* prev;
    struct TorirsLruNode* next;
    /** Index into the open-addressed map, or -1 if unlinked. */
    int32_t map_slot;
};

struct TorirsLru
{
    struct TorirsLruNode* head; /* MRU */
    struct TorirsLruNode* tail; /* LRU */
    struct TorirsLruNode** slots;
    uint32_t cap;
    uint32_t size;
    uint32_t max_entries;
};

/** Initialize an empty LRU. max_entries=0 means unbounded (no auto-evict). */
bool TorirsLru_Init(struct TorirsLru* lru, uint32_t initial_cap, uint32_t max_entries);
void TorirsLru_Free(struct TorirsLru* lru);

/** Look up by key. Does not touch. */
struct TorirsLruNode* TorirsLru_Find(struct TorirsLru const* lru, int64_t key);

/** Insert a fresh node (must not already be in any LRU). Evicts LRU if over
 *  max_entries and returns the victim via *evicted_out (may be NULL). */
void TorirsLru_Insert(
    struct TorirsLru* lru,
    struct TorirsLruNode* node,
    int64_t key,
    struct TorirsLruNode** evicted_out);

/** Move an existing node to MRU. */
void TorirsLru_Touch(struct TorirsLru* lru, struct TorirsLruNode* node);

/** Unlink without freeing. */
void TorirsLru_Remove(struct TorirsLru* lru, struct TorirsLruNode* node);

/** Pop the LRU node, or NULL if empty. */
struct TorirsLruNode* TorirsLru_EvictOldest(struct TorirsLru* lru);

/** Clear all links; does not free nodes (caller walks first if needed). */
void TorirsLru_Clear(struct TorirsLru* lru);

#ifdef __cplusplus
}
#endif

#endif /* TORIRS_LRU_H */
