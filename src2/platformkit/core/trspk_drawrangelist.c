#include "trspk_drawrangelist.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct TRSPK_DrawRangeList*
trspk_drawrangelist_create(
    uint32_t capacity)
{
    struct TRSPK_DrawRangeList* list =
        (struct TRSPK_DrawRangeList*)malloc(sizeof(struct TRSPK_DrawRangeList));
    assert(list != NULL);

    list->items = (struct TRSPK_DrawRange*)malloc(sizeof(struct TRSPK_DrawRange) * capacity);
    assert(list->items != NULL);

    list->capacity = capacity;
    list->count    = 0;
    list->head_idx = TRSPK_DRAWRANGELIST_NULL_IDX;
    list->tail_idx = TRSPK_DRAWRANGELIST_NULL_IDX;

    return list;
}

void
trspk_drawrangelist_free(
    struct TRSPK_DrawRangeList* list)
{
    free(list->items);
    free(list);
}

void
trspk_drawrangelist_clear(
    struct TRSPK_DrawRangeList* list)
{
    list->count    = 0;
    list->head_idx = TRSPK_DRAWRANGELIST_NULL_IDX;
    list->tail_idx = TRSPK_DRAWRANGELIST_NULL_IDX;
}

struct TRSPK_DrawRange*
trspk_drawrangelist_push(
    struct TRSPK_DrawRangeList* list,
    uint32_t start,
    uint32_t end,
    uint32_t buffer_idx,
    uint32_t config_idx,
    uint32_t min_vertex,
    uint32_t max_vertex)
{
    assert(list->count < list->capacity);

    uint32_t idx = list->count;

    struct TRSPK_DrawRange* item = &list->items[idx];
    item->start      = start;
    item->end        = end;
    item->buffer_idx = buffer_idx;
    item->config_idx = config_idx;
    item->min_vertex = min_vertex;
    item->max_vertex = max_vertex;
    item->next_idx   = TRSPK_DRAWRANGELIST_NULL_IDX;

    if( list->head_idx == TRSPK_DRAWRANGELIST_NULL_IDX )
    {
        list->head_idx = idx;
    }
    else
    {
        list->items[list->tail_idx].next_idx = idx;
    }

    list->tail_idx = idx;
    list->count++;

    return item;
}

struct TRSPK_DrawRange*
trspk_drawrangelist_head(
    const struct TRSPK_DrawRangeList* list)
{
    if( list->head_idx == TRSPK_DRAWRANGELIST_NULL_IDX )
    {
        return NULL;
    }

    return &list->items[list->head_idx];
}

struct TRSPK_DrawRange*
trspk_drawrangelist_next(
    const struct TRSPK_DrawRangeList* list,
    const struct TRSPK_DrawRange* item)
{
    if( item->next_idx == TRSPK_DRAWRANGELIST_NULL_IDX )
    {
        return NULL;
    }

    return &list->items[item->next_idx];
}
