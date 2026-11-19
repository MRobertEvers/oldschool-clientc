#include "game/rs_clientscript_queue.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stddef.h>

void
RS_ClientScriptQueue_Init(struct RS_ClientScriptQueue* queue)
{
    assert(queue);
    queue->items =
        ToriMem_Calloc(ToriMem_Script(), RS_CLIENTSCRIPT_QUEUE_INITIAL, sizeof(*queue->items));
    assert(queue->items);
    queue->capacity = RS_CLIENTSCRIPT_QUEUE_INITIAL;
    queue->head = 0;
    queue->count = 0;
    queue->held_since_cycle = 0;
}

void
RS_ClientScriptQueue_Free(struct RS_ClientScriptQueue* queue)
{
    if( !queue )
        return;
    ToriMem_Free(ToriMem_Script(), queue->items);
    queue->items = NULL;
    queue->capacity = 0;
    queue->head = 0;
    queue->count = 0;
}

void
RS_ClientScriptQueue_Reset(struct RS_ClientScriptQueue* queue)
{
    assert(queue);
    queue->head = 0;
    queue->count = 0;
    queue->held_since_cycle = 0;
}

/* Double the slots, the held payloads laid out from slot 0 in their order. */
static void
clientscript_queue_grow(struct RS_ClientScriptQueue* queue)
{
    int const capacity = queue->capacity * 2;
    struct PktRunClientScript* items;

    assert(capacity <= RS_CLIENTSCRIPT_QUEUE_CEILING);
    items = ToriMem_Alloc(ToriMem_Script(), (size_t)capacity * sizeof(*items));
    assert(items);
    for( int i = 0; i < queue->count; i++ )
        items[i] = queue->items[(queue->head + i) % queue->capacity];
    ToriMem_Free(ToriMem_Script(), queue->items);
    queue->items = items;
    queue->capacity = capacity;
    queue->head = 0;
}

void
RS_ClientScriptQueue_Hold(
    struct RS_ClientScriptQueue* queue,
    struct PktRunClientScript const* request,
    uint32_t cycle)
{
    assert(queue);
    assert(queue->items);
    assert(request);
    assert(queue->head >= 0);
    assert(queue->head < queue->capacity);

    if( queue->count >= queue->capacity )
        clientscript_queue_grow(queue);
    /* Stamped by the OLDEST payload, not the newest: the backstop asks how
     * long the queue has been waiting, and a later arrival resetting the clock
     * would let a busy tick hold the first one indefinitely. */
    if( queue->count == 0 )
        queue->held_since_cycle = cycle;
    queue->items[(queue->head + queue->count) % queue->capacity] = *request;
    queue->count++;
}

int
RS_ClientScriptQueue_Count(struct RS_ClientScriptQueue const* queue)
{
    assert(queue);
    return queue->count;
}

int
RS_ClientScriptQueue_Capacity(struct RS_ClientScriptQueue const* queue)
{
    assert(queue);
    return queue->capacity;
}

bool
RS_ClientScriptQueue_Pop(
    struct RS_ClientScriptQueue* queue,
    struct PktRunClientScript* out)
{
    assert(queue);
    assert(out);

    if( queue->count <= 0 )
        return false;
    assert(queue->items);
    assert(queue->head < queue->capacity);
    *out = queue->items[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    return true;
}

bool
RS_ClientScriptQueue_FenceOverdue(
    struct RS_ClientScriptQueue const* queue,
    uint32_t cycle,
    uint32_t max_cycles)
{
    assert(queue);

    if( queue->count <= 0 )
        return false;
    return cycle - queue->held_since_cycle >= max_cycles;
}
