#include "engine/torirs_lru.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static uint32_t
torirs_lru_hash(int64_t key)
{
    uint64_t x = (uint64_t)key;
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    x ^= x >> 31;
    return (uint32_t)x;
}

static bool
torirs_lru_grow(struct TorirsLru* lru)
{
    uint32_t new_cap = lru->cap ? lru->cap * 2u : 16u;
    struct TorirsLruNode** slots =
        (struct TorirsLruNode**)calloc(new_cap, sizeof(*slots));
    if( !slots )
        return false;

    for( uint32_t i = 0; i < lru->cap; i++ )
    {
        struct TorirsLruNode* n = lru->slots[i];
        if( !n )
            continue;
        uint32_t h = torirs_lru_hash(n->key) & (new_cap - 1);
        while( slots[h] )
            h = (h + 1) & (new_cap - 1);
        slots[h] = n;
        n->map_slot = (int32_t)h;
    }
    free(lru->slots);
    lru->slots = slots;
    lru->cap = new_cap;
    return true;
}

bool
TorirsLru_Init(struct TorirsLru* lru, uint32_t initial_cap, uint32_t max_entries)
{
    assert(lru);
    memset(lru, 0, sizeof(*lru));
    if( initial_cap < 16 )
        initial_cap = 16;
    /* Power of two. */
    uint32_t cap = 16;
    while( cap < initial_cap )
        cap <<= 1;
    lru->slots = (struct TorirsLruNode**)calloc(cap, sizeof(*lru->slots));
    if( !lru->slots )
        return false;
    lru->cap = cap;
    lru->max_entries = max_entries;
    return true;
}

void
TorirsLru_Free(struct TorirsLru* lru)
{
    if( !lru )
        return;
    free(lru->slots);
    memset(lru, 0, sizeof(*lru));
}

struct TorirsLruNode*
TorirsLru_Find(struct TorirsLru const* lru, int64_t key)
{
    assert(lru);
    if( !lru->slots || lru->cap == 0 )
        return NULL;
    uint32_t mask = lru->cap - 1;
    uint32_t h = torirs_lru_hash(key) & mask;
    for( ;; )
    {
        struct TorirsLruNode* n = lru->slots[h];
        if( !n )
            return NULL;
        if( n->key == key )
            return n;
        h = (h + 1) & mask;
    }
}

static void
torirs_lru_list_unlink(struct TorirsLru* lru, struct TorirsLruNode* node)
{
    if( node->prev )
        node->prev->next = node->next;
    else
        lru->head = node->next;
    if( node->next )
        node->next->prev = node->prev;
    else
        lru->tail = node->prev;
    node->prev = NULL;
    node->next = NULL;
}

static void
torirs_lru_list_push_mru(struct TorirsLru* lru, struct TorirsLruNode* node)
{
    node->prev = NULL;
    node->next = lru->head;
    if( lru->head )
        lru->head->prev = node;
    else
        lru->tail = node;
    lru->head = node;
}

static void
torirs_lru_map_remove(struct TorirsLru* lru, struct TorirsLruNode* node)
{
    if( node->map_slot < 0 || !lru->slots )
        return;
    uint32_t mask = lru->cap - 1;
    uint32_t i = (uint32_t)node->map_slot;
    lru->slots[i] = NULL;
    node->map_slot = -1;

    /* Rehash the probe chain after the hole. */
    uint32_t j = (i + 1) & mask;
    while( lru->slots[j] )
    {
        struct TorirsLruNode* n = lru->slots[j];
        lru->slots[j] = NULL;
        n->map_slot = -1;
        uint32_t h = torirs_lru_hash(n->key) & mask;
        while( lru->slots[h] )
            h = (h + 1) & mask;
        lru->slots[h] = n;
        n->map_slot = (int32_t)h;
        j = (j + 1) & mask;
    }
}

void
TorirsLru_Remove(struct TorirsLru* lru, struct TorirsLruNode* node)
{
    assert(lru && node);
    if( node->map_slot < 0 )
        return;
    torirs_lru_map_remove(lru, node);
    torirs_lru_list_unlink(lru, node);
    assert(lru->size > 0);
    lru->size--;
}

void
TorirsLru_Touch(struct TorirsLru* lru, struct TorirsLruNode* node)
{
    assert(lru && node);
    if( node->map_slot < 0 )
        return;
    if( lru->head == node )
        return;
    torirs_lru_list_unlink(lru, node);
    torirs_lru_list_push_mru(lru, node);
}

struct TorirsLruNode*
TorirsLru_EvictOldest(struct TorirsLru* lru)
{
    assert(lru);
    struct TorirsLruNode* victim = lru->tail;
    if( !victim )
        return NULL;
    TorirsLru_Remove(lru, victim);
    return victim;
}

void
TorirsLru_Insert(
    struct TorirsLru* lru,
    struct TorirsLruNode* node,
    int64_t key,
    struct TorirsLruNode** evicted_out)
{
    assert(lru && node);
    if( evicted_out )
        *evicted_out = NULL;

    if( lru->max_entries > 0 && lru->size >= lru->max_entries )
    {
        struct TorirsLruNode* victim = TorirsLru_EvictOldest(lru);
        if( evicted_out )
            *evicted_out = victim;
    }

    if( lru->size * 2u >= lru->cap )
    {
        if( !torirs_lru_grow(lru) )
            return;
    }

    uint32_t mask = lru->cap - 1;
    uint32_t h = torirs_lru_hash(key) & mask;
    while( lru->slots[h] )
        h = (h + 1) & mask;

    node->key = key;
    node->map_slot = (int32_t)h;
    node->prev = NULL;
    node->next = NULL;
    lru->slots[h] = node;
    torirs_lru_list_push_mru(lru, node);
    lru->size++;
}

void
TorirsLru_Clear(struct TorirsLru* lru)
{
    assert(lru);
    struct TorirsLruNode* n = lru->head;
    while( n )
    {
        struct TorirsLruNode* next = n->next;
        n->prev = NULL;
        n->next = NULL;
        n->map_slot = -1;
        n = next;
    }
    if( lru->slots )
        memset(lru->slots, 0, (size_t)lru->cap * sizeof(*lru->slots));
    lru->head = NULL;
    lru->tail = NULL;
    lru->size = 0;
}
