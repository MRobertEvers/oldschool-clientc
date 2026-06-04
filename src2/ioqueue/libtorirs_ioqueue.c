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
    LibToriRS_IOQueueClear(queue);
    free(queue);
}

static void
ioqueue_item_free_data(struct LibToriRS_IOQueueItem* item)
{
    if( !item || !item->data )
        return;

    if( item->kind == TORIRSIO_KIND_CONFIG_FILE || item->kind == TORIRSIO_KIND_SCRIPT )
        free(item->data);

    item->data = NULL;
    item->data_size = 0;
}

void
LibToriRS_IOQueueClear(struct LibToriRS_IOQueue* queue)
{
    if( !queue )
        return;

    for( int i = 0; i < queue->count; i++ )
        ioqueue_item_free_data(&queue->items[i]);

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
    item->status = TORIRSIO_STAT_YIELD;
    item->u.cache.table_id = table_id;
    item->u.cache.archive_id = archive_id;
    item->u.cache.flags = flags;
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
    item->status = TORIRSIO_STAT_YIELD;
    strncpy(item->u.config_file.path, path, LIBTORIRS_IOQUEUE_PATH_MAX - 1);
    item->u.config_file.path[LIBTORIRS_IOQUEUE_PATH_MAX - 1] = '\0';
    queue->count++;
    return true;
}

bool
LibToriRS_IOQueuePushScript(
    struct LibToriRS_IOQueue* queue,
    const char* path)
{
    if( !queue || !path || path[0] == '\0' )
        return false;
    if( queue->count >= LIBTORIRS_IOQUEUE_MAX_SIZE )
        return false;

    struct LibToriRS_IOQueueItem* item = &queue->items[queue->count];
    memset(item, 0, sizeof(struct LibToriRS_IOQueueItem));
    item->kind = TORIRSIO_KIND_SCRIPT;
    item->status = TORIRSIO_STAT_YIELD;
    strncpy(item->u.script.path, path, LIBTORIRS_IOQUEUE_PATH_MAX - 1);
    item->u.script.path[LIBTORIRS_IOQUEUE_PATH_MAX - 1] = '\0';
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
