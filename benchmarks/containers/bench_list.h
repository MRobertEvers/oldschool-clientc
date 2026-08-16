#ifndef BENCH_LIST_H
#define BENCH_LIST_H

/*
 * Singly-linked list of BenchObjects, with two node allocation strategies
 * that share one traversal path:
 *   - malloc: each node from its own malloc (scattered pointer chasing)
 *   - pool:   nodes carved from one contiguous block
 */

#include "bench_object.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct BenchListNode
{
    uint32_t id;
    struct BenchObject obj;
    struct BenchListNode* next;
};

enum BenchListAllocKind
{
    BENCH_LIST_MALLOC = 0,
    BENCH_LIST_POOL = 1
};

struct BenchList
{
    struct BenchListNode* head;
    enum BenchListAllocKind kind;
    /* Pool mode only: contiguous node storage. */
    struct BenchListNode* pool;
    size_t pool_cap;
    size_t pool_used;
};

static inline void
bench_list_init(struct BenchList* list, enum BenchListAllocKind kind, size_t n)
{
    assert(list);
    memset(list, 0, sizeof(*list));
    list->kind = kind;
    if( kind == BENCH_LIST_POOL )
    {
        if( n == 0 )
            return;
        list->pool = (struct BenchListNode*)calloc(n, sizeof(struct BenchListNode));
        if( !list->pool )
            bench_die("bench_list pool alloc failed");
        list->pool_cap = n;
        list->pool_used = 0;
    }
}

static inline struct BenchListNode*
bench_list_alloc_node(struct BenchList* list)
{
    assert(list);
    if( list->kind == BENCH_LIST_POOL )
    {
        assert(list->pool_used < list->pool_cap);
        return &list->pool[list->pool_used++];
    }
    struct BenchListNode* node =
        (struct BenchListNode*)calloc(1, sizeof(struct BenchListNode));
    if( !node )
        bench_die("bench_list node alloc failed");
    return node;
}

/* Prepend. Insertion order is later shuffled by the harness before insert. */
static inline void
bench_list_insert(struct BenchList* list, const struct BenchObject* obj)
{
    assert(list);
    assert(obj);
    struct BenchListNode* node = bench_list_alloc_node(list);
    node->id = obj->id;
    node->obj = *obj;
    node->next = list->head;
    list->head = node;
}

static inline const struct BenchObject*
bench_list_find(const struct BenchList* list, uint32_t id)
{
    assert(list);
    for( const struct BenchListNode* n = list->head; n; n = n->next )
    {
        if( n->id == id )
            return &n->obj;
    }
    return NULL;
}

static inline void
bench_list_free(struct BenchList* list)
{
    assert(list);
    if( list->kind == BENCH_LIST_MALLOC )
    {
        struct BenchListNode* n = list->head;
        while( n )
        {
            struct BenchListNode* next = n->next;
            free(n);
            n = next;
        }
    }
    else
    {
        free(list->pool);
    }
    memset(list, 0, sizeof(*list));
}

#endif /* BENCH_LIST_H */
