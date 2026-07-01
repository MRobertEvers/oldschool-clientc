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
    queue->current_slot = -1;
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

    int run_counter = queue->run_counter;
    memset(queue, 0, sizeof(struct LibToriRS_IOQueue));
    queue->run_counter = run_counter;
    queue->current_slot = -1;
}

int
LibToriRS_IOQueueBeginRun(struct LibToriRS_IOQueue* queue)
{
    if( !queue )
        return 0;
    return ++queue->run_counter;
}

bool
LibToriRS_IOQueueRunComplete(
    struct LibToriRS_IOQueue* queue,
    int run_id)
{
    if( !queue || run_id <= 0 )
        return true;

    for( int i = 0; i < queue->count; i++ )
    {
        struct LibToriRS_IOQueueItem const* item = &queue->items[i];
        if( item->run_id != run_id )
            continue;
        if( item->status != TORIRSIO_STAT_DONE )
            return false;
    }

    return true;
}

void
LibToriRS_IOQueuePushCache(
    struct LibToriRS_IOQueue* queue,
    int table_id,
    int archive_id,
    int flags)
{
    if( !queue )
        return;
    if( queue->count >= LIBTORIRS_IOQUEUE_MAX_SIZE )
        return;

    struct LibToriRS_IOQueueItem item = { 0 };
    item.kind = TORIRSIO_KIND_CACHE;
    item.status = TORIRSIO_STAT_YIELD;
    item.u.cache.table_id = table_id;
    item.u.cache.archive_id = archive_id;
    item.u.cache.flags = flags;
    if( !LibToriRS_IOQueuePopWrite(queue, &item) )
        return;
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
    item->run_id = queue->run_counter;
    item->slot_id = queue->current_slot;
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
    item->run_id = queue->run_counter;
    item->slot_id = queue->current_slot;
    strncpy(item->u.script.path, path, LIBTORIRS_IOQUEUE_PATH_MAX - 1);
    item->u.script.path[LIBTORIRS_IOQUEUE_PATH_MAX - 1] = '\0';
    queue->count++;
    return true;
}

void
LibToriRS_IOQueuePushReferenceTable(
    struct LibToriRS_IOQueue* queue,
    int table_id)
{
    if( !queue )
        return;
    if( queue->count >= LIBTORIRS_IOQUEUE_MAX_SIZE )
        return;

    struct LibToriRS_IOQueueItem item = { 0 };
    item.kind = TORIRSIO_KIND_REFERENCE_TABLE;
    item.status = TORIRSIO_STAT_YIELD;
    item.u.reference_table.table_id = table_id;
    if( !LibToriRS_IOQueuePopWrite(queue, &item) )
        return;
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
    struct LibToriRS_IOQueueItem* item = &queue->items[queue->count];
    memcpy(item, in, sizeof(struct LibToriRS_IOQueueItem));
    item->run_id = queue->run_counter;
    item->slot_id = queue->current_slot;
    queue->count++;
    return true;
}

bool
LibToriRS_IOQueuePopRead(
    struct LibToriRS_IOQueue* queue,
    struct LibToriRS_IOQueueItem* out)
{
    if( !queue || !out )
        return false;
    if( queue->read_head >= queue->count )
        return false;

    struct LibToriRS_IOQueueItem* slot = &queue->items[queue->read_head];
    memcpy(out, slot, sizeof(struct LibToriRS_IOQueueItem));
    if( slot->kind == TORIRSIO_KIND_CONFIG_FILE || slot->kind == TORIRSIO_KIND_SCRIPT )
    {
        slot->data = NULL;
        slot->data_size = 0;
    }
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

struct LibToriRS_IOQueueItem*
LibToriRS_IOQueueFindBySlot(
    struct LibToriRS_IOQueue* queue,
    int slot_id)
{
    if( !queue || slot_id < 0 )
        return NULL;

    for( int i = 0; i < queue->count; i++ )
    {
        struct LibToriRS_IOQueueItem* item = &queue->items[i];
        if( item->slot_id != slot_id )
            continue;
        if( item->consumed )
            continue;
        if( item->status != TORIRSIO_STAT_DONE )
            return NULL;
        item->consumed = true;
        return item;
    }

    return NULL;
}

struct LibToriRS_IOQueueItem*
LibToriRS_IOQueueFindReferenceTable(
    struct LibToriRS_IOQueue* queue,
    int slot_id,
    int table_id)
{
    if( !queue || slot_id < 0 )
        return NULL;

    for( int i = 0; i < queue->count; i++ )
    {
        struct LibToriRS_IOQueueItem* item = &queue->items[i];
        if( item->kind != TORIRSIO_KIND_REFERENCE_TABLE )
            continue;
        if( item->slot_id != slot_id )
            continue;
        if( item->u.reference_table.table_id != table_id )
            continue;
        if( item->consumed )
            continue;
        if( item->status != TORIRSIO_STAT_DONE )
            return NULL;
        item->consumed = true;
        return item;
    }

    return NULL;
}
