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

    for( int i = 0; i < queue->count; i++ )
    {
        struct LibToriRS_IOQueueItem* item = &queue->items[i];
        if( item->kind == TORIRSIO_KIND_CONFIG_FILE && item->data )
            free(item->data);
    }

    memset(queue, 0, sizeof(struct LibToriRS_IOQueue));
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
    item->kind = TORIRSIO_KIND_CACHE;
    item->table_id = table_id;
    item->archive_id = archive_id;
    item->flags = flags;
    item->status = TORIRSIO_PENDING;
    queue->count++;
}

bool
LibToriRS_IOQueuePushConfigFile(
    struct LibToriRS_IOQueue* queue,
    const char* path)
{
    if( !queue || !path || path[0] == '\0' )
        return false;
    if( queue->count >= LIBTORIRS_IOQUEUE_MAX_SIZE )
        return false;

    struct LibToriRS_IOQueueItem* item = &queue->items[queue->count];
    memset(item, 0, sizeof(struct LibToriRS_IOQueueItem));
    item->kind = TORIRSIO_KIND_CONFIG_FILE;
    strncpy(item->path, path, LIBTORIRS_IOQUEUE_PATH_MAX - 1);
    item->path[LIBTORIRS_IOQUEUE_PATH_MAX - 1] = '\0';
    item->status = TORIRSIO_PENDING;
    queue->count++;
    return true;
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

struct LibToriRS_IOQueueItem*
LibToriRS_IOQueuePopReadPtr(struct LibToriRS_IOQueue* queue)
{
    assert(queue);
    assert(queue->count > 0);
    assert(queue->read_head < queue->count);
    struct LibToriRS_IOQueueItem* request = &queue->items[queue->read_head];
    queue->read_head++;
    return request;
}