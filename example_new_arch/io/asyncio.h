#ifndef ASYNCIO_H
#define ASYNCIO_H

#define MINIPT_ENABLE_USER_PTR 1
#include <3rd/minipt.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define IOITEM_MAX_PATH 256
#define IO_MAX_ITEMS 32
#define TASK_QUEUE_MAX_TASKS 32

#define ASYNCIO_STAT_ERROR -1
#define ASYNCIO_STAT_YIELD 0
#define ASYNCIO_STAT_DONE 1

enum IOKind
{
    IOK_CACHE = 0,
    IOK_CONFIG_FILE,
    IOK_SCRIPT,
    IOK_REFERENCE_TABLE,
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
    char path[IOITEM_MAX_PATH];
};

struct IOItem_Script
{
    char path[IOITEM_MAX_PATH];
};

struct IOItem_ReferenceTable
{
    int table_id;
};

struct IOItem
{
    enum IOKind kind;
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

struct IO
{
    struct IOItem io_slots[IO_MAX_ITEMS];
    int live_no;
};

// Forward declare vtable;
struct TaskVTable;

struct Task
{
    struct TaskVTable* vtable;

    struct Task* next;
    struct Task* prev;
};

struct TaskVTable
{
    int (*run)(
        struct Task* task,
        struct IO* io);
    void (*free)(struct Task* task);
};

struct TaskQueue
{
    struct Task* head;
    struct Task* tail;
};

static inline void
task_free(struct Task* task)
{
    if( task->vtable->free )
        task->vtable->free(task);
    free(task);
}

static inline int
task_run(
    struct Task* task,
    struct IO* io)
{
    assert(task != NULL);
    assert(io != NULL);
    assert(task->vtable->run);

    return task->vtable->run(task, io);
}

struct IO*
IO_New(void)
{
    struct IO* io = malloc(sizeof(struct IO));
    assert(io != NULL);
    memset(io, 0, sizeof(struct IO));
    return io;
}

void
IO_PushCache(
    struct IO* io,
    int epoch,
    int table_id,
    int archive_id,
    int flags)
{
    assert(io != NULL);
    assert(table_id >= 0);
    assert(archive_id >= 0);
    assert(flags >= 0);
    struct IOItem* item = &io->io_slots[io->live_no];
    memset(item, 0, sizeof(struct IOItem));
    io->live_no++;

    item->kind = IOK_CACHE;
    item->u.cache.epoch = epoch;
    item->u.cache.table_id = table_id;
    item->u.cache.archive_id = archive_id;
    item->u.cache.flags = flags;
}

void
IO_PushConfigFile(
    struct IO* io,
    const char* path)
{
    assert(io != NULL);
    assert(path != NULL);
    struct IOItem* item = &io->io_slots[io->live_no];
    memset(item, 0, sizeof(struct IOItem));
    io->live_no++;

    item->kind = IOK_CONFIG_FILE;
    strcpy(item->u.config_file.path, path);
}

void
IO_PushScript(
    struct IO* io,
    const char* path)
{
    assert(io != NULL);
    assert(path != NULL);
    struct IOItem* item = &io->io_slots[io->live_no];
    memset(item, 0, sizeof(struct IOItem));
    io->live_no++;

    item->kind = IOK_SCRIPT;
    strcpy(item->u.script.path, path);
}

void
IO_PushReferenceTable(
    struct IO* io,
    int table_id)
{
    assert(io != NULL);
    assert(table_id >= 0);
    struct IOItem* item = &io->io_slots[io->live_no];
    memset(item, 0, sizeof(struct IOItem));
    io->live_no++;

    item->kind = IOK_REFERENCE_TABLE;
    item->u.reference_table.table_id = table_id;
}

void
IO_Free(struct IO* io)
{
    assert(io != NULL);
    free(io);
}

struct TaskQueue*
TaskQueue_New(void)
{
    struct TaskQueue* queue = malloc(sizeof(struct TaskQueue));
    assert(queue != NULL);
    memset(queue, 0, sizeof(struct TaskQueue));
    queue->head = NULL;
    queue->tail = NULL;
    return queue;
}

int
TaskQueue_Add(
    struct TaskQueue* queue,
    struct Task* task)
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

void
TaskQueue_Remove(
    struct TaskQueue* queue,
    struct Task* task)
{
    assert(queue != NULL);
    assert(task != NULL);
    if( task->prev != NULL )
        task->prev->next = task->next;
    if( task->next != NULL )
        task->next->prev = task->prev;

    task_free(task);
}

int
TaskQueue_Run(
    struct TaskQueue* queue,
    struct IO* io)
{
    assert(queue != NULL);
    int res = ASYNCIO_STAT_DONE;
    struct Task* task = NULL;
    while( queue->head != NULL )
    {
        task = queue->head;
        res = task_run(task, io);

        switch( res )
        {
        case ASYNCIO_STAT_YIELD:
            goto done_loop;
            break;
        case ASYNCIO_STAT_DONE:
            TaskQueue_Remove(queue, task);
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

void
TaskQueue_Free(struct TaskQueue* queue)
{
    assert(queue != NULL);
    while( queue->head != NULL )
    {
        struct Task* task = queue->head;
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
        struct Task* _child = (pt)->user;                                                          \
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