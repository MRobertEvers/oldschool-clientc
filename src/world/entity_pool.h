#ifndef WORLD_ENTITY_POOL_H
#define WORLD_ENTITY_POOL_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#define WORLD_ENTITY_NIL (-1)

struct World_EntityPoolNode
{
    int prev;
    int next;
    bool active;
};

struct World_EntityPool
{
    void* items;
    struct World_EntityPoolNode* nodes;
    int element_size;
    int capacity;
    int count;
    int active_count;
    int head;
    int tail;
    int free_head;
    /** Bumped whenever the LIVE SET changes -- an entity activated,
     *  released, or the whole pool reset.
     *
     *  Says THAT the set changed and never WHICH entity, so it can only
     *  invalidate a cached pass, not maintain one -- and invalidating means
     *  walking the pool again, which for scenery is the 23k-entity walk the
     *  caching was for. Where that matters, World::scenery_changed carries
     *  the identities instead. Kept because "did anything change at all" is
     *  a cheap question with no other answer. */
    unsigned int epoch;
};

void
World_EntityPoolInit(
    struct World_EntityPool* pool,
    int element_size);

void
World_EntityPoolFree(struct World_EntityPool* pool);

int
World_EntityPoolAlloc(struct World_EntityPool* pool);

void
World_EntityPoolRelease(
    struct World_EntityPool* pool,
    int index);

bool
World_EntityPoolEnsureSlot(
    struct World_EntityPool* pool,
    int index);

bool
World_EntityPoolReserve(
    struct World_EntityPool* pool,
    int slot_count);

void
World_EntityPoolReset(struct World_EntityPool* pool);

void*
World_EntityPoolGet(
    const struct World_EntityPool* pool,
    int index);

/** The same slot as World_EntityPoolGet, for callers that have already settled
 *  that the index is one -- typically the line above, with
 *  `assert(World_EntityPoolIsActive(pool, idx))` or a checked
 *  World_EntityPoolAlloc. An out-of-range index is the caller's bug and stops
 *  here, so the result is never NULL and no call site needs to test it.
 *
 *  This is not only style. World_EntityPoolGet's NULL return survives inlining,
 *  and the OPT=1 lane compiles with -DNDEBUG, which erases the assert that ruled
 *  it out -- LTO then reads every write through the result as a write through a
 *  null pointer and reports it as -Wstringop-overflow. */
static inline void*
World_EntityPoolAt(
    const struct World_EntityPool* pool,
    int index)
{
    assert(pool);
    assert(index >= 0);
    assert(index < pool->count);
    assert(pool->items);
    return (char*)pool->items + (size_t)index * (size_t)pool->element_size;
}

static inline bool
World_EntityPoolIsActive(
    const struct World_EntityPool* pool,
    int index)
{
    assert(pool);
    assert(index >= 0 && index < pool->count);
    return pool->nodes[index].active;
}

static inline int
World_EntityPoolHead(const struct World_EntityPool* pool)
{
    assert(pool);
    return pool->head;
}

static inline int
World_EntityPoolNext(
    const struct World_EntityPool* pool,
    int index)
{
    assert(pool);
    assert(index >= 0 && index < pool->count);
    return pool->nodes[index].next;
}

#endif
