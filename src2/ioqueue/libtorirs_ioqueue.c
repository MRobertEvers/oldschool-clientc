#include "libtorirs_ioqueue.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct LibToriRS_IOQueue*
LibToriRS_IOQueueNew(void)
{
    struct LibToriRS_IOQueue* queue = malloc(sizeof(struct LibToriRS_IOQueue));
    if( !queue )
        return NULL;
    memset(queue, 0, sizeof(struct LibToriRS_IOQueue));
    return queue;
}

void
LibToriRS_IOQueueFree(struct LibToriRS_IOQueue* queue)
{
    if( !queue )
        return;
    free(queue);
    queue = NULL;
}

void
LibToriRS_IOQueueClear(struct LibToriRS_IOQueue* queue)
{
    if( !queue )
        return;
    assert(queue->read_head == queue->count);
    memset(queue, 0, sizeof(struct LibToriRS_IOQueue));
    queue->count = 0;
}

void
LibToriRS_IOQueuePush(
    struct LibToriRS_IOQueue* queue,
    int table_id,
    int archive_id,
    int flags)
{
    if( !queue )
        return;
    if( queue->count >= LIBTORIRS_IOQUEUE_MAX_SIZE )
        return;
    struct LibToriRS_IOQueueItem* item = &queue->items[queue->count];
    memset(item, 0, sizeof(struct LibToriRS_IOQueueItem));
    item->table_id = table_id;
    item->archive_id = archive_id;
    item->flags = flags;
    queue->count++;
}

bool
LibToriRS_IOQueueIsEmpty(struct LibToriRS_IOQueue* queue)
{
    if( !queue )
        return true;
    return queue->count == 0;
}

bool
LibToriRS_IOQueuePopWrite(
    struct LibToriRS_IOQueue* queue,
    struct LibToriRS_IOQueueItem* in)
{
    assert(queue);
    assert(queue->count < LIBTORIRS_IOQUEUE_MAX_SIZE);
    memcpy(&queue->items[queue->count], in, sizeof(struct LibToriRS_IOQueueItem));
    queue->count++;
    return true;
}

bool
LibToriRS_IOQueuePopRead(
    struct LibToriRS_IOQueue* queue,
    struct LibToriRS_IOQueueItem* out)
{
    assert(queue);
    assert(queue->count > 0);
    assert(queue->read_head < queue->count);
    memcpy(out, &queue->items[queue->read_head], sizeof(struct LibToriRS_IOQueueItem));
    queue->read_head++;
    return true;
}

struct LibToriRS_IORequest*
LibToriRS_IOQueuePopReadPtr(struct LibToriRS_IOQueue* queue)
{
    assert(queue);
    assert(queue->count > 0);
    assert(queue->read_head < queue->count);
    struct LibToriRS_IORequest* request = &queue->items[queue->read_head];
    queue->read_head++;
    return request;
}