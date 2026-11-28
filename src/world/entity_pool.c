#include "entity_pool.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* A page of a pool holds a power of two of its entities, at least
 * TORIMEM_PAGE_MIN_BYTES of them: a shift finds an entity's page. */
static int
World_EntityPoolPageShift(int element_size)
{
    int shift = 0;
    assert(element_size > 0);
    while( ((size_t)1 << shift) * (size_t)element_size < TORIMEM_PAGE_MIN_BYTES )
        shift++;
    return shift;
}

static size_t
World_EntityPoolPageBytes(const struct World_EntityPool* pool)
{
    return ((size_t)1 << pool->page_shift) * (size_t)pool->element_size;
}

/* One more page: the entities already in the pool stay where they are. The
 * node table, 12 bytes an entity, still grows by realloc; it is a fiftieth of
 * the size of the scenery it describes. */
static bool
World_EntityPoolGrow(struct World_EntityPool* pool)
{
    int const old_cap = pool->capacity;
    int const new_cap = old_cap + (1 << pool->page_shift);
    unsigned char** pages;
    struct World_EntityPoolNode* grown_nodes;
    assert(new_cap > old_cap);
    pages = ToriMem_Realloc(
        ToriMem_Scene(), pool->pages, (size_t)(pool->page_count + 1) * sizeof(*pages));
    pages[pool->page_count] = ToriMem_PageAlloc(ToriMem_Scene(), World_EntityPoolPageBytes(pool));
    grown_nodes = ToriMem_Realloc(
        ToriMem_Scene(), pool->nodes, (size_t)new_cap * sizeof(struct World_EntityPoolNode));
    pool->pages = pages;
    pool->page_count++;
    pool->nodes = grown_nodes;
    pool->capacity = new_cap;

    for( int i = old_cap; i < new_cap; i++ )
    {
        pool->nodes[i].prev = WORLD_ENTITY_NIL;
        pool->nodes[i].next = WORLD_ENTITY_NIL;
        pool->nodes[i].active = false;
    }
    return true;
}

void
World_EntityPoolInit(
    struct World_EntityPool* pool,
    int element_size)
{
    assert(pool);
    memset(pool, 0, sizeof(*pool));
    pool->element_size = element_size;
    pool->page_shift = element_size > 0 ? World_EntityPoolPageShift(element_size) : 0;
    pool->head = WORLD_ENTITY_NIL;
    pool->tail = WORLD_ENTITY_NIL;
    pool->free_head = WORLD_ENTITY_NIL;
}

void
World_EntityPoolFree(struct World_EntityPool* pool)
{
    if( !pool )
        return;
    for( int i = 0; i < pool->page_count; i++ )
        ToriMem_PageFree(ToriMem_Scene(), pool->pages[i]);
    ToriMem_Free(ToriMem_Scene(), pool->pages);
    ToriMem_Free(ToriMem_Scene(), pool->nodes);
    memset(pool, 0, sizeof(*pool));
    pool->head = WORLD_ENTITY_NIL;
    pool->tail = WORLD_ENTITY_NIL;
    pool->free_head = WORLD_ENTITY_NIL;
}

void*
World_EntityPoolGet(
    const struct World_EntityPool* pool,
    int index)
{
    assert(pool);
    if( index < 0 || index >= pool->count )
        return NULL;
    return World_EntityPoolAt(pool, index);
}

int
World_EntityPoolAlloc(struct World_EntityPool* pool)
{
    int index;

    assert(pool);
    if( pool->element_size <= 0 )
        return WORLD_ENTITY_NIL;

    if( pool->free_head != WORLD_ENTITY_NIL )
    {
        index = pool->free_head;
        pool->free_head = pool->nodes[index].next;
    }
    else
    {
        index = pool->count;
        if( index >= pool->capacity && !World_EntityPoolGrow(pool) )
            return WORLD_ENTITY_NIL;
        pool->count++;
        pool->nodes[index].prev = WORLD_ENTITY_NIL;
        pool->nodes[index].next = WORLD_ENTITY_NIL;
        pool->nodes[index].active = false;
    }

    memset(World_EntityPoolGet(pool, index), 0, (size_t)pool->element_size);

    pool->nodes[index].active = true;
    pool->nodes[index].prev = pool->tail;
    pool->nodes[index].next = WORLD_ENTITY_NIL;

    if( pool->tail != WORLD_ENTITY_NIL )
        pool->nodes[pool->tail].next = index;
    else
        pool->head = index;
    pool->tail = index;
    pool->active_count++;
    pool->epoch++;

    return index;
}

void
World_EntityPoolRelease(
    struct World_EntityPool* pool,
    int index)
{
    int prev_idx;
    int next_idx;

    assert(pool);
    if( index < 0 || index >= pool->count )
        return;
    if( !pool->nodes[index].active )
        return;

    prev_idx = pool->nodes[index].prev;
    next_idx = pool->nodes[index].next;

    if( prev_idx != WORLD_ENTITY_NIL )
        pool->nodes[prev_idx].next = next_idx;
    else
        pool->head = next_idx;

    if( next_idx != WORLD_ENTITY_NIL )
        pool->nodes[next_idx].prev = prev_idx;
    else
        pool->tail = prev_idx;

    pool->nodes[index].active = false;
    pool->nodes[index].prev = WORLD_ENTITY_NIL;
    pool->nodes[index].next = pool->free_head;
    pool->free_head = index;
    pool->active_count--;
    pool->epoch++;
}

bool
World_EntityPoolEnsureSlot(
    struct World_EntityPool* pool,
    int index)
{
    assert(pool);
    if( pool->element_size <= 0 || index < 0 )
        return false;

    while( index >= pool->capacity )
    {
        if( !World_EntityPoolGrow(pool) )
            return false;
    }

    if( index >= pool->count )
        pool->count = index + 1;

    if( pool->nodes[index].active )
        return true;

    pool->nodes[index].active = true;
    pool->nodes[index].prev = pool->tail;
    pool->nodes[index].next = WORLD_ENTITY_NIL;

    if( pool->tail != WORLD_ENTITY_NIL )
        pool->nodes[pool->tail].next = index;
    else
        pool->head = index;
    pool->tail = index;
    pool->active_count++;
    pool->epoch++;

    memset(World_EntityPoolGet(pool, index), 0, (size_t)pool->element_size);
    return true;
}

bool
World_EntityPoolReserve(
    struct World_EntityPool* pool,
    int slot_count)
{
    assert(pool);
    if( pool->element_size <= 0 || slot_count <= 0 )
        return true;

    int const last = slot_count - 1;
    while( last >= pool->capacity )
    {
        if( !World_EntityPoolGrow(pool) )
            return false;
    }

    if( slot_count > pool->count )
    {
        int const first = pool->count;
        pool->count = slot_count;
        for( int i = first; i < slot_count; i++ )
        {
            memset(World_EntityPoolAt(pool, i), 0, (size_t)pool->element_size);
            pool->nodes[i].prev = WORLD_ENTITY_NIL;
            pool->nodes[i].next = WORLD_ENTITY_NIL;
            pool->nodes[i].active = false;
        }
    }

    return true;
}

void
World_EntityPoolReset(struct World_EntityPool* pool)
{
    assert(pool);

    for( int i = 0; i < pool->count; i++ )
    {
        pool->nodes[i].active = false;
        pool->nodes[i].prev = WORLD_ENTITY_NIL;
        pool->nodes[i].next = WORLD_ENTITY_NIL;
    }

    pool->head = WORLD_ENTITY_NIL;
    pool->tail = WORLD_ENTITY_NIL;
    pool->free_head = WORLD_ENTITY_NIL;
    pool->active_count = 0;
    pool->count = 0;
    pool->epoch++;
}
