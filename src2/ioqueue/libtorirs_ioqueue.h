#ifndef LIBTORIRS_IOQUEUE_H
#define LIBTORIRS_IOQUEUE_H

#include <3rd/minipt.h>

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define LIBTORIRS_IOQUEUE_MAX_SIZE 128
#define LIBTORIRS_IOQUEUE_PATH_MAX 256

enum LibToriRS_IOStat
{
    TORIRSIO_STAT_YIELD = 0,
    TORIRSIO_STAT_DONE,
};

enum LibToriRS_IOKind
{
    TORIRSIO_KIND_CACHE = 0,
    TORIRSIO_KIND_CONFIG_FILE,
    TORIRSIO_KIND_SCRIPT,
    TORIRSIO_KIND_REFERENCE_TABLE,
};

struct LibToriRS_IOQueueItem_Cache
{
    int epoch;
    int table_id;
    int archive_id;
    int flags;
};

struct LibToriRS_IOQueueItem_ConfigFile
{
    char path[LIBTORIRS_IOQUEUE_PATH_MAX];
};

struct LibToriRS_IOQueueItem_Script
{
    char path[LIBTORIRS_IOQUEUE_PATH_MAX];
};

struct LibToriRS_IOQueueItem_ReferenceTable
{
    int table_id;
};

struct IOSlot
{
    int id;
};

struct LibToriRS_IOQueueItem
{
    enum LibToriRS_IOKind kind;
    enum LibToriRS_IOStat status;
    int run_id;
    int slot_id;
    bool consumed;

    int error_code;
    void* data;
    int data_size;
    union
    {
        struct LibToriRS_IOQueueItem_Cache cache;
        struct LibToriRS_IOQueueItem_ConfigFile config_file;
        struct LibToriRS_IOQueueItem_Script script;
        struct LibToriRS_IOQueueItem_ReferenceTable reference_table;
    } u;
};

struct LibToriRS_IOQueue
{
    struct LibToriRS_IOQueueItem items[LIBTORIRS_IOQUEUE_MAX_SIZE];
    int count;
    int read_head;
    int run_counter;
    int current_slot;
};

struct LibToriRS_IOContext
{
    struct LibToriRS_IOQueue* io;
    void* user;
};

#define LIBTORIRS_IOBATCH_MAX LIBTORIRS_IOQUEUE_MAX_SIZE

struct LibToriRS_IOBatch
{
    int count;
    int user[LIBTORIRS_IOBATCH_MAX];
};

static inline void
LibToriRS_IOBatchReset(struct LibToriRS_IOBatch* batch)
{
    batch->count = 0;
}

static inline int
LibToriRS_IOBatchAdd(
    struct LibToriRS_IOBatch* batch,
    int user_value)
{
    assert(batch->count < LIBTORIRS_IOBATCH_MAX);
    int slot = batch->count;
    batch->user[slot] = user_value;
    batch->count++;
    return slot;
}

static inline int
LibToriRS_IOBatchCount(const struct LibToriRS_IOBatch* batch)
{
    return batch->count;
}

static inline int
LibToriRS_IOBatchUser(
    const struct LibToriRS_IOBatch* batch,
    int slot)
{
    assert(slot >= 0 && slot < batch->count);
    return batch->user[slot];
}

static inline bool
LibToriRS_IOBatchEmpty(const struct LibToriRS_IOBatch* batch)
{
    return batch->count == 0;
}

#define IO_REQUEST(ctx, id, expr)                                                                  \
    do                                                                                             \
    {                                                                                              \
        (ctx)->io->current_slot = (id);                                                            \
        (expr);                                                                                    \
        (ctx)->io->current_slot = -1;                                                              \
    } while( 0 )

static inline void
LibToriRS_IOQueueItemFreeData(struct LibToriRS_IOQueueItem* item)
{
    assert(item);
    if( !item->data )
        return;

    if( item->kind == TORIRSIO_KIND_CONFIG_FILE || item->kind == TORIRSIO_KIND_SCRIPT )
        free(item->data);

    item->data = NULL;
    item->data_size = 0;
}

static inline struct LibToriRS_IOQueue*
LibToriRS_IOQueueNew(void)
{
    struct LibToriRS_IOQueue* queue = malloc(sizeof(struct LibToriRS_IOQueue));
    assert(queue);
    memset(queue, 0, sizeof(struct LibToriRS_IOQueue));
    queue->current_slot = -1;
    return queue;
}

static inline void
LibToriRS_IOQueueClear(struct LibToriRS_IOQueue* queue)
{
    assert(queue);

    for( int i = 0; i < queue->count; i++ )
        LibToriRS_IOQueueItemFreeData(&queue->items[i]);

    int run_counter = queue->run_counter;
    memset(queue, 0, sizeof(struct LibToriRS_IOQueue));
    queue->run_counter = run_counter;
    queue->current_slot = -1;
}

static inline void
LibToriRS_IOQueueFree(struct LibToriRS_IOQueue* queue)
{
    assert(queue);
    LibToriRS_IOQueueClear(queue);
    free(queue);
}

static inline int
LibToriRS_IOQueueBeginRun(struct LibToriRS_IOQueue* queue)
{
    assert(queue);
    return ++queue->run_counter;
}

static inline bool
LibToriRS_IOQueueRunComplete(
    struct LibToriRS_IOQueue* queue,
    int run_id)
{
    assert(queue);
    if( run_id <= 0 )
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

static inline bool
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

static inline void
LibToriRS_IOQueuePushCache(
    struct LibToriRS_IOQueue* queue,
    int table_id,
    int archive_id,
    int flags)
{
    assert(queue);
    assert(queue->count < LIBTORIRS_IOQUEUE_MAX_SIZE);

    struct LibToriRS_IOQueueItem item = { 0 };
    item.kind = TORIRSIO_KIND_CACHE;
    item.status = TORIRSIO_STAT_YIELD;
    item.u.cache.table_id = table_id;
    item.u.cache.archive_id = archive_id;
    item.u.cache.flags = flags;
    if( !LibToriRS_IOQueuePopWrite(queue, &item) )
        return;
}

static inline bool
LibToriRS_IOQueuePushConfigFile(
    struct LibToriRS_IOQueue* queue,
    const char* path)
{
    assert(queue);
    if( !path || path[0] == '\0' )
        return false;
    assert(queue->count < LIBTORIRS_IOQUEUE_MAX_SIZE);

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

static inline bool
LibToriRS_IOQueuePushScript(
    struct LibToriRS_IOQueue* queue,
    const char* path)
{
    assert(queue);
    if( !path || path[0] == '\0' )
        return false;
    assert(queue->count < LIBTORIRS_IOQUEUE_MAX_SIZE);

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

static inline void
LibToriRS_IOQueuePushReferenceTable(
    struct LibToriRS_IOQueue* queue,
    int table_id)
{
    assert(queue);
    assert(queue->count < LIBTORIRS_IOQUEUE_MAX_SIZE);

    struct LibToriRS_IOQueueItem item = { 0 };
    item.kind = TORIRSIO_KIND_REFERENCE_TABLE;
    item.status = TORIRSIO_STAT_YIELD;
    item.u.reference_table.table_id = table_id;
    if( !LibToriRS_IOQueuePopWrite(queue, &item) )
        return;
}

static inline bool
LibToriRS_IOQueueIsEmpty(struct LibToriRS_IOQueue* queue)
{
    assert(queue);
    return queue->count == 0;
}

static inline bool
LibToriRS_IOQueuePopRead(
    struct LibToriRS_IOQueue* queue,
    struct LibToriRS_IOQueueItem* out)
{
    assert(queue);
    assert(out);
    assert(queue->read_head < queue->count);

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

static inline struct LibToriRS_IOQueueItem*
LibToriRS_IOQueuePopReadPtr(struct LibToriRS_IOQueue* queue)
{
    assert(queue);
    assert(queue->count > 0);
    assert(queue->read_head < queue->count);
    struct LibToriRS_IOQueueItem* request = &queue->items[queue->read_head];
    queue->read_head++;
    return request;
}

static inline struct LibToriRS_IOQueueItem*
LibToriRS_IOQueueFindBySlot(
    struct LibToriRS_IOQueue* queue,
    int slot_id)
{
    assert(queue);
    assert(slot_id >= 0);

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

static inline struct LibToriRS_IOQueueItem*
LibToriRS_IOQueueFindReferenceTable(
    struct LibToriRS_IOQueue* queue,
    int slot_id,
    int table_id)
{
    assert(queue);
    assert(slot_id >= 0);

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

#endif
