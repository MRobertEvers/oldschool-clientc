#ifndef CONTOUR_GROUND_QUEUE_U_C
#define CONTOUR_GROUND_QUEUE_U_C

#include "contour_ground_queue.h"

#include <assert.h>
#include <stdlib.h>

int
contour_ground_q_count(struct ContourGroundQueue* q)
{
    assert(q);
    return q->count;
}

struct ContourGroundQueueEntry*
contour_ground_q_at(
    struct ContourGroundQueue* q,
    int index)
{
    assert(q);
    assert(index >= 0 && index < q->count);
    return &q->entries[index];
}

void
contour_ground_q_push(
    struct ContourGroundQueue* q,
    int element_id,
    int loc_id,
    int shape_select,
    int rotation,
    int size_x,
    int size_z,
    int level)
{
    assert(q);
    if( element_id < 0 )
        return;

    if( q->count >= q->cap )
    {
        int ncap = q->cap ? q->cap * 2 : 512;
        struct ContourGroundQueueEntry* next =
            realloc(q->entries, (size_t)ncap * sizeof(struct ContourGroundQueueEntry));
        if( !next )
            return;
        q->entries = next;
        q->cap = ncap;
    }

    struct ContourGroundQueueEntry* r = &q->entries[q->count++];
    r->element_id = element_id;
    r->loc_id = loc_id;
    r->shape_select = shape_select;
    r->rotation = rotation;
    r->size_x = size_x;
    r->size_z = size_z;
    r->level = level;
}

void
contour_ground_q_clear(struct ContourGroundQueue* q)
{
    assert(q);
    q->count = 0;
}

void
contour_ground_q_free(struct ContourGroundQueue* q)
{
    if( !q )
        return;
    free(q->entries);
    q->entries = NULL;
    q->count = 0;
    q->cap = 0;
}

#endif
