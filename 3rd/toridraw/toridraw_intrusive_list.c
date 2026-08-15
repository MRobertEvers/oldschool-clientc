#include "toridraw_intrusive_list.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TORIDRAW_INTRUSIVE_INITIAL_CAPACITY 64
#define TORIDRAW_INTRUSIVE_GROWTH_FACTOR    2

static bool
td_intrusive_grow(struct ToriDraw_IntrusiveList* list)
{
    int new_cap = list->capacity ? list->capacity * TORIDRAW_INTRUSIVE_GROWTH_FACTOR
                                 : TORIDRAW_INTRUSIVE_INITIAL_CAPACITY;
    struct ToriDraw_IntrusiveNode* grown =
        realloc(list->nodes, (size_t)new_cap * sizeof(struct ToriDraw_IntrusiveNode));
    if( !grown )
        return false;
    list->nodes = grown;
    list->capacity = new_cap;
    return true;
}

void
ToriDraw_IntrusiveListInit(struct ToriDraw_IntrusiveList* list)
{
    assert(list);
    memset(list, 0, sizeof(*list));
    list->head = TORIDRAW_INTRUSIVE_NIL;
    list->tail = TORIDRAW_INTRUSIVE_NIL;
    list->free_head = TORIDRAW_INTRUSIVE_NIL;
}

void
ToriDraw_IntrusiveListFree(struct ToriDraw_IntrusiveList* list)
{
    if( !list )
        return;
    free(list->nodes);
    memset(list, 0, sizeof(*list));
    list->head = TORIDRAW_INTRUSIVE_NIL;
    list->tail = TORIDRAW_INTRUSIVE_NIL;
    list->free_head = TORIDRAW_INTRUSIVE_NIL;
}

int
ToriDraw_IntrusiveListAlloc(
    struct ToriDraw_IntrusiveList* list,
    void* data)
{
    int index;

    assert(list);

    if( list->free_head != TORIDRAW_INTRUSIVE_NIL )
    {
        index = list->free_head;
        list->free_head = list->nodes[index].next;
    }
    else
    {
        index = list->count;
        if( index >= list->capacity && !td_intrusive_grow(list) )
            return TORIDRAW_INTRUSIVE_NIL;
        list->count++;
        list->nodes[index].prev = TORIDRAW_INTRUSIVE_NIL;
        list->nodes[index].next = TORIDRAW_INTRUSIVE_NIL;
    }

    list->nodes[index].data = data;
    list->nodes[index].prev = list->tail;
    list->nodes[index].next = TORIDRAW_INTRUSIVE_NIL;

    if( list->tail != TORIDRAW_INTRUSIVE_NIL )
        list->nodes[list->tail].next = index;
    else
        list->head = index;
    list->tail = index;

    return index;
}

void
ToriDraw_IntrusiveListRelease(
    struct ToriDraw_IntrusiveList* list,
    int index)
{
    int prev_idx;
    int next_idx;

    assert(list);
    if( index < 0 || index >= list->count )
        return;
    if( list->nodes[index].prev == TORIDRAW_INTRUSIVE_FREE )
        return;

    prev_idx = list->nodes[index].prev;
    next_idx = list->nodes[index].next;

    if( prev_idx != TORIDRAW_INTRUSIVE_NIL )
        list->nodes[prev_idx].next = next_idx;
    else
        list->head = next_idx;

    if( next_idx != TORIDRAW_INTRUSIVE_NIL )
        list->nodes[next_idx].prev = prev_idx;
    else
        list->tail = prev_idx;

    list->nodes[index].prev = TORIDRAW_INTRUSIVE_FREE;
    list->nodes[index].next = list->free_head;
    list->free_head = index;
}
