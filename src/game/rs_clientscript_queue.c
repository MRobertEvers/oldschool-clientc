#include "game/rs_clientscript_queue.h"

#include <assert.h>
#include <string.h>

void
RS_ClientScriptQueue_Reset(struct RS_ClientScriptQueue* queue)
{
    assert(queue);
    queue->head = 0;
    queue->count = 0;
    queue->held_since_cycle = 0;
}

bool
RS_ClientScriptQueue_Hold(
    struct RS_ClientScriptQueue* queue,
    struct PktRunClientScript const* request,
    uint32_t cycle)
{
    assert(queue);
    assert(request);

    if( queue->count >= RS_CLIENTSCRIPT_QUEUE_MAX )
        return false;
    /* Stamped by the OLDEST payload, not the newest: the backstop asks how
     * long the queue has been waiting, and a later arrival resetting the clock
     * would let a busy tick hold the first one indefinitely. */
    if( queue->count == 0 )
        queue->held_since_cycle = cycle;
    queue->items[(queue->head + queue->count) % RS_CLIENTSCRIPT_QUEUE_MAX] = *request;
    queue->count++;
    return true;
}

int
RS_ClientScriptQueue_Count(struct RS_ClientScriptQueue const* queue)
{
    assert(queue);
    return queue->count;
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
    *out = queue->items[queue->head];
    queue->head = (queue->head + 1) % RS_CLIENTSCRIPT_QUEUE_MAX;
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
