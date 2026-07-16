#ifndef ASYNCIO_H
#define ASYNCIO_H

#define MINIPT_ENABLE_USER_PTR 1
#include <3rd/minipt.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TORIRS_IOITEM_MAX_PATH 256
#define TORIRS_IO_MAX_ITEMS 32
#define TORIRS_TASK_QUEUE_MAX_TASKS 32

#define TORIRS_ASYNCIO_STAT_ERROR -1
#define TORIRS_ASYNCIO_STAT_YIELD 0
#define TORIRS_ASYNCIO_STAT_DONE 1

enum ToriRS_IOKind
{
    TORIRS_IOK_CACHE = 0,
    TORIRS_IOK_CONFIG_FILE,
    TORIRS_IOK_SCRIPT,
    TORIRS_IOK_REFERENCE_TABLE,
};

struct IOItem_Cache
{
    int epoch;
    int table_id;
    int archive_id;
    int flags;
};

struct IOItem_ConfigFile
{
    char path[TORIRS_IOITEM_MAX_PATH];
};

struct IOItem_Script
{
    char path[TORIRS_IOITEM_MAX_PATH];
};

struct IOItem_ReferenceTable
{
    int table_id;
};

struct ToriRS_IOItem
{
    enum ToriRS_IOKind kind;
    union
    {
        struct IOItem_Cache cache;
        struct IOItem_ConfigFile config_file;
        struct IOItem_Script script;
        struct IOItem_ReferenceTable reference_table;
    } u;

    int error_code;
    void* data;
    int data_size;
};

#define IOITEM_DATA(item) (item->data)
#define IOITEM_DATA_SIZE(item) (item->data_size)
#define IOITEM_ERROR_CODE(item) (item->error_code)

#define IOITEM_FREE_DATA(item)                                                                     \
    do                                                                                             \
    {                                                                                              \
        free(IOITEM_DATA(item));                                                                   \
        IOITEM_DATA(item) = NULL;                                                                  \
        IOITEM_DATA_SIZE(item) = 0;                                                                \
    } while( 0 )

struct ToriRS_IO
{
    struct ToriRS_IOItem io_slots[TORIRS_IO_MAX_ITEMS];
    int live_no;
    int current_slot;
};

struct ToriRS_TaskVTable;

struct ToriRS_Task
{
    struct ToriRS_TaskVTable* vtable;

    struct ToriRS_Task* next;
    struct ToriRS_Task* prev;
};

struct ToriRS_TaskVTable
{
    int (*run)(
        struct ToriRS_Task* task,
        struct ToriRS_IO* io);
    void (*free)(struct ToriRS_Task* task);
};

struct ToriRS_TaskQueue
{
    struct ToriRS_Task* head;
    struct ToriRS_Task* tail;
};

static inline void
task_free(struct ToriRS_Task* task)
{
    if( task->vtable->free )
        task->vtable->free(task);
    free(task);
}

static inline int
task_run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    assert(task != NULL);
    assert(io != NULL);
    assert(task->vtable->run);

    return task->vtable->run(task, io);
}

static inline struct ToriRS_IO*
ToriRS_IO_New(void)
{
    struct ToriRS_IO* io = malloc(sizeof(struct ToriRS_IO));
    assert(io != NULL);
    memset(io, 0, sizeof(struct ToriRS_IO));
    return io;
}

static inline void
ToriRS_IO_PushCache(
    struct ToriRS_IO* io,
    int epoch,
    int table_id,
    int archive_id,
    int flags)
{
    assert(io != NULL);
    assert(table_id >= 0);
    assert(archive_id >= 0);
    assert(flags >= 0);
    struct ToriRS_IOItem* item = &io->io_slots[io->live_no];
    memset(item, 0, sizeof(struct ToriRS_IOItem));
    io->live_no++;

    item->kind = TORIRS_IOK_CACHE;
    item->u.cache.epoch = epoch;
    item->u.cache.table_id = table_id;
    item->u.cache.archive_id = archive_id;
    item->u.cache.flags = flags;
}

static inline void
ToriRS_IO_PushConfigFile(
    struct ToriRS_IO* io,
    const char* path)
{
    assert(io != NULL);
    assert(path != NULL);
    struct ToriRS_IOItem* item = &io->io_slots[io->live_no];
    memset(item, 0, sizeof(struct ToriRS_IOItem));
    io->live_no++;

    item->kind = TORIRS_IOK_CONFIG_FILE;
    strcpy(item->u.config_file.path, path);
}

static inline void
ToriRS_IO_PushScript(
    struct ToriRS_IO* io,
    const char* path)
{
    assert(io != NULL);
    assert(path != NULL);
    struct ToriRS_IOItem* item = &io->io_slots[io->live_no];
    memset(item, 0, sizeof(struct ToriRS_IOItem));
    io->live_no++;

    item->kind = TORIRS_IOK_SCRIPT;
    strcpy(item->u.script.path, path);
}

static inline void
ToriRS_IO_PushReferenceTable(
    struct ToriRS_IO* io,
    int table_id)
{
    assert(io != NULL);
    assert(table_id >= 0);
    struct ToriRS_IOItem* item = &io->io_slots[io->live_no];
    memset(item, 0, sizeof(struct ToriRS_IOItem));
    io->live_no++;

    item->kind = TORIRS_IOK_REFERENCE_TABLE;
    item->u.reference_table.table_id = table_id;
}

static inline void
ToriRS_IO_Free(struct ToriRS_IO* io)
{
    assert(io != NULL);
    free(io);
}

static inline struct ToriRS_TaskQueue*
ToriRS_TaskQueue_New(void)
{
    struct ToriRS_TaskQueue* queue = malloc(sizeof(struct ToriRS_TaskQueue));
    assert(queue != NULL);
    memset(queue, 0, sizeof(struct ToriRS_TaskQueue));
    queue->head = NULL;
    queue->tail = NULL;
    return queue;
}

static inline int
ToriRS_TaskQueue_Add(
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_Task* task)
{
    assert(queue != NULL);
    assert(task != NULL);
    if( queue->head == NULL )
    {
        queue->head = task;
        queue->tail = task;
    }
    else
    {
        queue->tail->next = task;
        task->prev = queue->tail;
        queue->tail = task;
    }

    return 0;
}

static inline void
ToriRS_TaskQueue_Remove(
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_Task* task)
{
    assert(queue != NULL);
    assert(task != NULL);
    if( task->prev != NULL )
        task->prev->next = task->next;
    if( task->next != NULL )
        task->next->prev = task->prev;

    task_free(task);
}

static inline int
ToriRS_TaskQueue_Run(
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_IO* io)
{
    assert(queue != NULL);
    int res = TORIRS_ASYNCIO_STAT_DONE;
    struct ToriRS_Task* task = NULL;
    while( queue->head != NULL )
    {
        task = queue->head;
        res = task_run(task, io);

        switch( res )
        {
        case TORIRS_ASYNCIO_STAT_YIELD:
            goto done_loop;
            break;
        case TORIRS_ASYNCIO_STAT_DONE:
            ToriRS_TaskQueue_Remove(queue, task);
            goto done_loop;
            break;
        default:
            assert(0);
            break;
        }
    }

done_loop:
    return res;
}

static inline void
ToriRS_TaskQueue_Free(struct ToriRS_TaskQueue* queue)
{
    assert(queue != NULL);
    while( queue->head != NULL )
    {
        struct ToriRS_Task* task = queue->head;
        queue->head = task->next;
        if( task->vtable->free )
            task->vtable->free(task);
        free(task);
    }
    free(queue);
}

/**
 * Drive a heap-allocated child Task to completion from a parent protothread.
 * child_expr is evaluated once and stored in (pt)->user across yields.
 */
#define TASK_AWAITEX(pt, ctx, child_expr)                                                          \
    do                                                                                             \
    {                                                                                              \
        (pt)->lc = __LINE__;                                                                       \
        if( !(pt)->user )                                                                          \
            (pt)->user = (child_expr);                                                             \
    case __LINE__:                                                                                 \
    {                                                                                              \
        struct ToriRS_Task* _child = (pt)->user;                                                   \
        int _await_res = task_run(_child, ctx);                                                    \
        if( _await_res != PT_ENDED && _await_res != PT_EXITED )                                    \
            return _await_res;                                                                     \
        task_free(_child);                                                                         \
        (pt)->user = NULL;                                                                         \
    }                                                                                              \
    } while( 0 )

#define TASK_REQUEST(pt, ctx, id, expr)                                                            \
    do                                                                                             \
    {                                                                                              \
        (ctx)->io->current_slot = (id);                                                            \
        (expr);                                                                                    \
        (ctx)->io->current_slot = -1;                                                              \
    } while( 0 )

#endif // ASYNCIO_H