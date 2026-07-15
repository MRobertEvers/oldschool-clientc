#ifndef TORIDRAW_INTRUSIVE_LIST_H
#define TORIDRAW_INTRUSIVE_LIST_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#define TORIDRAW_INTRUSIVE_NIL  (-1)
#define TORIDRAW_INTRUSIVE_FREE (-2)

struct ToriDraw_IntrusiveNode
{
    int prev;
    int next;
    void* data;
};

struct ToriDraw_IntrusiveList
{
    struct ToriDraw_IntrusiveNode* nodes;
    int capacity;
    int count;
    int head;
    int tail;
    int free_head;
};

void
ToriDraw_IntrusiveListInit(struct ToriDraw_IntrusiveList* list);

void
ToriDraw_IntrusiveListFree(struct ToriDraw_IntrusiveList* list);

int
ToriDraw_IntrusiveListAlloc(
    struct ToriDraw_IntrusiveList* list,
    void* data);

void
ToriDraw_IntrusiveListRelease(
    struct ToriDraw_IntrusiveList* list,
    int index);

static inline void*
ToriDraw_IntrusiveListGet(
    const struct ToriDraw_IntrusiveList* list,
    int index)
{
    assert(list);
    if( index < 0 || index >= list->count )
        return NULL;
    return list->nodes[index].data;
}

static inline bool
ToriDraw_IntrusiveListIsLive(
    const struct ToriDraw_IntrusiveList* list,
    int index)
{
    assert(list);
    if( index < 0 || index >= list->count )
        return false;
    return list->nodes[index].prev != TORIDRAW_INTRUSIVE_FREE;
}

#endif
