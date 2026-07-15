#include "world_entity.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define WORLD_ENTITY_INITIAL_CAPACITY 64
#define WORLD_ENTITY_GROWTH_FACTOR    2

static bool
world_entity_pool_grow(struct World_EntityPool* pool)
{
    int new_cap = pool->capacity ? pool->capacity * WORLD_ENTITY_GROWTH_FACTOR
                                 : WORLD_ENTITY_INITIAL_CAPACITY;
    void* grown_items =
        realloc(pool->items, (size_t)new_cap * (size_t)pool->element_size);
    struct World_EntityPoolNode* grown_nodes =
        realloc(pool->nodes, (size_t)new_cap * sizeof(struct World_EntityPoolNode));
    if( !grown_items || !grown_nodes )
    {
        free(grown_items);
        free(grown_nodes);
        return false;
    }

    pool->items = grown_items;
    pool->nodes = grown_nodes;
    pool->capacity = new_cap;
    return true;
}

void
World_EntityPoolInit(
    struct World_EntityPool* pool,
    int element_size)
{
    if( !pool )
        return;
    memset(pool, 0, sizeof(*pool));
    pool->element_size = element_size;
    pool->head = WORLD_ENTITY_NIL;
    pool->tail = WORLD_ENTITY_NIL;
    pool->free_head = WORLD_ENTITY_NIL;
}

void
World_EntityPoolFree(struct World_EntityPool* pool)
{
    if( !pool )
        return;
    free(pool->items);
    free(pool->nodes);
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
    return (char*)pool->items + (size_t)index * (size_t)pool->element_size;
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
        if( index >= pool->capacity && !world_entity_pool_grow(pool) )
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
        if( !world_entity_pool_grow(pool) )
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
        if( !world_entity_pool_grow(pool) )
            return false;
    }

    if( slot_count > pool->count )
    {
        memset(
            (char*)pool->items + (size_t)pool->count * (size_t)pool->element_size,
            0,
            (size_t)(slot_count - pool->count) * (size_t)pool->element_size);
        for( int i = pool->count; i < slot_count; i++ )
        {
            pool->nodes[i].prev = WORLD_ENTITY_NIL;
            pool->nodes[i].next = WORLD_ENTITY_NIL;
            pool->nodes[i].active = false;
        }
        pool->count = slot_count;
    }

    return true;
}

void
World_EntityPoolReset(struct World_EntityPool* pool)
{
    if( !pool )
        return;

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
}

void
World_EntityListInit(struct World_EntityList* list)
{
    if( !list )
        return;
    memset(list, 0, sizeof(*list));
    World_EntityPoolInit(&list->terrain, (int)sizeof(struct WorldEntity_Terrain));
    World_EntityPoolInit(&list->scenery, (int)sizeof(struct WorldEntity_Scenery));
    World_EntityPoolInit(&list->player, (int)sizeof(struct WorldEntity_Player));
    World_EntityPoolInit(&list->npc, (int)sizeof(struct WorldEntity_NPC));
    World_EntityPoolInit(&list->projectile, (int)sizeof(struct WorldEntity_Projectile));
    World_EntityPoolInit(&list->spotanim, (int)sizeof(struct WorldEntity_Spotanim));
}

void
World_EntityListFree(struct World_EntityList* list)
{
    if( !list )
        return;
    World_EntityPoolFree(&list->terrain);
    World_EntityPoolFree(&list->scenery);
    World_EntityPoolFree(&list->player);
    World_EntityPoolFree(&list->npc);
    World_EntityPoolFree(&list->projectile);
    World_EntityPoolFree(&list->spotanim);
}
