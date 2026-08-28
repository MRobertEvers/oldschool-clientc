#ifndef ASYNCIO_H
#define ASYNCIO_H

#define MINIPT_ENABLE_USER_PTR 1
#include <3rd/minipt.h>

#include "perf/torirs_perf.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TORIRS_IOITEM_MAX_PATH 256
#define TORIRS_IO_MAX_ITEMS 32
#define TORIRS_TASK_QUEUE_MAX_TASKS 32

#define TORIRS_ASYNCIO_STAT_ERROR -1
#define TORIRS_ASYNCIO_STAT_YIELD 0
#define TORIRS_ASYNCIO_STAT_DONE 1
/* The head task parked on client state rather than on a read (see
 * TASK_AWAIT_STATE). Nothing this queue can do will unblock it, so the caller
 * must hand control back to the frame loop instead of stepping again. */
#define TORIRS_ASYNCIO_STAT_BLOCKED 2
/* The head task asked for a frame to be drawn before it is resumed, and
 * said what should be on it. Distinct from YIELD because a yield is not a
 * frame boundary -- the runner settles as many of those as it likes -- and
 * distinct from BLOCKED because this task is not waiting on anything. It is
 * the only way a long load can show its own progress. */
#define TORIRS_ASYNCIO_STAT_RENDER 3

#define TORIRS_IO_CACHE_DAT2 0
#define TORIRS_IO_CACHE_DAT1 1
/*
 * Dat1 map chunks. A dat1 cache addresses map archives through the versionlist
 * "map_index" (region -> terrain/loc archive id), which only the disk layer
 * holds, so these two flags carry a map square id in archive_id and let the
 * platform resolve the real archive. Dat2 needs no equivalent: its map archive
 * ids come from the maps reference table, which tasks can read themselves.
 */
#define TORIRS_IO_CACHE_DAT1_MAP_TERRAIN 2
#define TORIRS_IO_CACHE_DAT1_MAP_SCENERY 3

enum ToriRS_IOKind
{
    TORIRS_IOK_NONE = 0,
    TORIRS_IOK_CACHE,
    TORIRS_IOK_CONFIG_FILE,
    TORIRS_IOK_SCRIPT,
    TORIRS_IOK_REFERENCE_TABLE,
    /*
     * A file the *client* owns, at a path the client names: its saved
     * settings. Unlike CONFIG_FILE and SCRIPT the path is used verbatim rather
     * than under a base directory, because it is the player's (TORIRS_PREFS may
     * name anywhere) and not part of the cache install.
     *
     * FILE_WRITE is the first item that carries data *into* the platform:
     * `data`/`data_size` are the bytes to write, borrowed for the duration of
     * the request, and the platform must neither free nor replace them. Every
     * other kind fills those two fields in on the way back.
     */
    TORIRS_IOK_FILE_READ,
    TORIRS_IOK_FILE_WRITE,
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

struct IOItem_File
{
    char path[TORIRS_IOITEM_MAX_PATH];
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
        struct IOItem_File file;
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

/*
 * The queue of outstanding platform requests.
 *
 * ## Slots, and why a task's `0` is not slot 0
 *
 * Every task asks for its reads at slot `0` -- that is what the hundred-odd
 * call sites in the engine say, and it was literal while exactly one task
 * could have a read outstanding. It no longer is: `slot_base` is added to
 * whatever a task names, so `0` means "this task's first slot" and several
 * tasks can be waiting on the platform at once (see TaskRunner_Step, which
 * sets it around each run).
 *
 * That is the whole of what made cache reads serial. The IO layer has always
 * held 32 items and every executor has always processed the whole active list
 * in one pass; the runner simply never handed it more than one, so a boot that
 * reads two and a half thousand groups paid two and a half thousand round
 * trips end to end.
 *
 * `slots_taken` is ownership, not busyness: a slot is taken from the moment
 * the runner gives it to a task until that task's read has been answered and
 * consumed. An item's `kind` cannot answer that question -- it is NONE both
 * before a read is queued and after it is decoded.
 */
struct ToriRS_IO
{
    struct ToriRS_IOItem io_slots[TORIRS_IO_MAX_ITEMS];
    int active[TORIRS_IO_MAX_ITEMS];
    int active_count;
    /** Bit per slot; see the note above. */
    uint32_t slots_taken;
    /** What a task's slot 0 currently addresses. */
    int slot_base;
};

/**
 * The item a task means by `slot_id`, which is `slot_id` slots into the block
 * the runner gave it.
 */
static inline struct ToriRS_IOItem*
ToriRS_IO_TaskSlot(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io != NULL);
    assert(slot_id >= 0);
    assert(io->slot_base >= 0);
    assert(io->slot_base + slot_id < TORIRS_IO_MAX_ITEMS);
    return &io->io_slots[io->slot_base + slot_id];
}

/** Take a slot for a task, or -1 when every one is already owned. */
static inline int
ToriRS_IO_SlotAlloc(struct ToriRS_IO* io)
{
    assert(io != NULL);
    for( int i = 0; i < TORIRS_IO_MAX_ITEMS; i++ )
    {
        if( (io->slots_taken & (1u << i)) == 0 )
        {
            io->slots_taken |= (1u << i);
            return i;
        }
    }
    return -1;
}

static inline void
ToriRS_IO_SlotRelease(
    struct ToriRS_IO* io,
    int slot)
{
    assert(io != NULL);
    assert(slot >= 0);
    assert(slot < TORIRS_IO_MAX_ITEMS);
    assert(io->slots_taken & (1u << slot));
    io->slots_taken &= ~(1u << slot);
}

/*
 * What a task wants on screen while it works.
 *
 * The render step cannot guess: during boot there is no tree to draw, and
 * once there is one, drawing it is wrong until the assets behind it have
 * landed. So the task that knows what stage it is at says what to draw and
 * what to say, and the frame loop obeys rather than deciding.
 *
 * `caption` is borrowed. It has to outlive the frame it is drawn on, which
 * in practice means a string table's storage or a literal -- never a task
 * local, which is gone the moment the protothread suspends.
 */
enum ToriRS_RenderIntent
{
    /** Nothing asked for; the frame loop draws whatever it would anyway. */
    TORIRS_RENDER_NONE = 0,
    /** The startup bar, for work that runs before any asset exists. */
    TORIRS_RENDER_BOOT_BAR,
    /** The title tree, for work that runs behind an already-built screen. */
    TORIRS_RENDER_TITLE
};

struct ToriRS_RenderRequest
{
    int intent;
    /** 0..100, or -1 for a screen that carries no bar. */
    int percent;
    char const* caption;
};

struct ToriRS_TaskVTable;

struct ToriRS_Task
{
    struct ToriRS_TaskVTable* vtable;
    char name[32];

    /* Set by TASK_AWAIT_STATE for the duration of one yield: this task is
     * waiting on state only some OTHER queue can change. Cleared on every
     * resume, so it always describes the yield that just happened. */
    int blocked;

    /* Set by PT_TASK_YIELD_TO_RENDER for the duration of one yield: this
     * task wants a frame published before it is resumed, and `render` says
     * what should be on it. Cleared on every resume, exactly like
     * `blocked` -- a request describes one yield, never a standing mode. */
    int wants_render;
    struct ToriRS_RenderRequest render;

    /*
     * The IO slot this task's reads live in, or -1 while it holds none.
     *
     * Held only for as long as a read is actually outstanding -- the runner
     * gives one out before a run and takes it back the moment the task yields
     * without having queued anything. That is what keeps a chain of tasks from
     * deadlocking on the slot table: a parent parked on PT_TASK_AWAITSELF is
     * waiting on its child, not on the platform, so it holds nothing while the
     * child needs a slot of its own.
     */
    int io_slot;

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
    else
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

    task->blocked = 0;
    task->wants_render = 0;
    return task->vtable->run(task, io);
}

/*
 * Wait for a condition this queue cannot make true.
 *
 * A plain PT_YIELD means "I asked the platform for something; resume me when
 * it lands", and the runner honours that by stepping the queue again straight
 * away. A task that instead waits on client state — a tree rebuild running on
 * another queue, say — must NOT be stepped again: that state can only change
 * once the frame loop gets its turn back, so re-running the task in place is
 * an unbreakable busy-wait, not a wait.
 *
 * This marks the yield so the runner can tell the two apart and end the frame.
 * The condition is re-tested on each resume, i.e. once per frame at most.
 */
#define TASK_AWAIT_STATE(task, pt, cond)                                                           \
    do                                                                                             \
    {                                                                                              \
        while( !(cond) )                                                                           \
        {                                                                                          \
            (task)->blocked = 1;                                                                   \
            PT_YIELD(pt);                                                                          \
        }                                                                                          \
    } while( 0 )

/*
 * Suspend so the frame loop can draw, and say what it should draw.
 *
 * Opt-in on purpose. The default remains that a task yields as often as it
 * likes and the runner settles all of it before publishing anything, which
 * is what keeps an ordinary frame from being torn into pieces by whatever
 * incidental IO a task happens to do. A task that wants the screen updated
 * mid-work has to say so here, and has to say what goes on it.
 *
 * PT_ prefix for the reason every other one has it: control leaves here and
 * comes back an unbounded amount of client activity later. See
 * PT_TASK_AWAITSELF for what that costs you.
 */
#define TASK_YIELD_TO_RENDER(task, pt, intent_, percent_, caption_)                            \
    do                                                                                         \
    {                                                                                          \
        (task)->wants_render = 1;                                                              \
        (task)->render.intent = (intent_);                                                     \
        (task)->render.percent = (percent_);                                                   \
        (task)->render.caption = (caption_);                                                   \
        PT_YIELD(pt);                                                                          \
    } while( 0 )

static inline void
push_active(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io != NULL);
    assert(slot_id >= 0);
    assert(slot_id < TORIRS_IO_MAX_ITEMS);
    /* One entry per slot per pass is the most a queue can produce, and the
     * list is as long as the slot table -- but a task that queues twice
     * without yielding would push twice, and running off the end of this
     * array is not something to discover from the symptom. */
    assert(io->active_count < TORIRS_IO_MAX_ITEMS);
    io->active[io->active_count++] = slot_id;
}

static inline struct ToriRS_IO*
ToriRS_IO_New(void)
{
    struct ToriRS_IO* io = malloc(sizeof(struct ToriRS_IO));
    assert(io != NULL);
    memset(io, 0, sizeof(struct ToriRS_IO));
    io->active_count = 0;
    return io;
}

static inline void
ToriRS_IO_QueueCache(
    struct ToriRS_IO* io,
    int slot_id,
    int epoch,
    int table_id,
    int archive_id,
    int flags)
{
    assert(io != NULL);
    assert(table_id >= 0);
    assert(archive_id >= 0);
    assert(flags >= 0);
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    memset(item, 0, sizeof(struct ToriRS_IOItem));

    item->kind = TORIRS_IOK_CACHE;
    item->u.cache.epoch = epoch;
    item->u.cache.table_id = table_id;
    item->u.cache.archive_id = archive_id;
    item->u.cache.flags = flags;
    push_active(io, io->slot_base + slot_id);
}

static inline void
ToriRS_IO_QueueConfigFile(
    struct ToriRS_IO* io,
    int slot_id,
    const char* path)
{
    assert(io != NULL);
    assert(path != NULL);
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    memset(item, 0, sizeof(struct ToriRS_IOItem));

    item->kind = TORIRS_IOK_CONFIG_FILE;
    strcpy(item->u.config_file.path, path);
    push_active(io, io->slot_base + slot_id);
}

static inline void
ToriRS_IO_QueueScript(
    struct ToriRS_IO* io,
    int slot_id,
    const char* path)
{
    assert(io != NULL);
    assert(path != NULL);
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    memset(item, 0, sizeof(struct ToriRS_IOItem));

    item->kind = TORIRS_IOK_SCRIPT;
    strcpy(item->u.script.path, path);
    push_active(io, io->slot_base + slot_id);
}

/** Read a client-owned file whole. `path` is used as given. */
static inline void
ToriRS_IO_QueueFileRead(
    struct ToriRS_IO* io,
    int slot_id,
    const char* path)
{
    assert(io != NULL);
    assert(path != NULL);
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    memset(item, 0, sizeof(struct ToriRS_IOItem));

    item->kind = TORIRS_IOK_FILE_READ;
    snprintf(item->u.file.path, sizeof(item->u.file.path), "%s", path);
    push_active(io, io->slot_base + slot_id);
}

/**
 * Write `data` to `path`.
 *
 * The buffer is borrowed, not handed over: it must outlive the yield that waits
 * for this item, and freeing it is still the caller's job. The platform reads
 * it and reports only `error_code`.
 */
static inline void
ToriRS_IO_QueueFileWrite(
    struct ToriRS_IO* io,
    int slot_id,
    const char* path,
    void* data,
    int data_size)
{
    assert(io != NULL);
    assert(path != NULL);
    assert(data != NULL || data_size == 0);
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    memset(item, 0, sizeof(struct ToriRS_IOItem));

    item->kind = TORIRS_IOK_FILE_WRITE;
    snprintf(item->u.file.path, sizeof(item->u.file.path), "%s", path);
    item->data = data;
    item->data_size = data_size;
    push_active(io, io->slot_base + slot_id);
}

static inline void
ToriRS_IO_QueueReferenceTable(
    struct ToriRS_IO* io,
    int slot_id,
    int table_id)
{
    assert(io != NULL);
    assert(table_id >= 0);
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    memset(item, 0, sizeof(struct ToriRS_IOItem));

    item->kind = TORIRS_IOK_REFERENCE_TABLE;
    item->u.reference_table.table_id = table_id;
    push_active(io, io->slot_base + slot_id);
}

static inline void
ToriRS_IO_Free(struct ToriRS_IO* io)
{
    assert(io != NULL);
    free(io);
}

/*
 * Release a served item.
 *
 * `data` is freed for every kind that FILLED it -- a read's payload belongs to
 * the item and this is where it dies. FILE_WRITE is the one kind that carries
 * data the other way: the bytes are the caller's, borrowed for the duration of
 * the request, and QueueFileWrite says in as many words that freeing them is
 * still the caller's job. Freeing them here made that promise false, and the
 * caller -- which frees them itself, exactly as told -- was freeing them a
 * second time.
 *
 * Tested by nothing for a long while because only three tasks write files, and
 * the one that ran on every boot (Task_PrefsSave) had worked around it by
 * detaching the pointer before calling this. The two that did not were the
 * plugin settings save and the plugin asset write, neither of which runs until
 * a plugin actually stores something -- so the first plugin to do it aborted
 * the client.
 */
static inline void
ToriRS_IO_ClearItem(struct ToriRS_IOItem* item)
{
    assert(item != NULL);
    if( item->kind != TORIRS_IOK_FILE_WRITE )
        free(item->data);
    item->kind = TORIRS_IOK_NONE;
    item->error_code = 0;
    item->data = NULL;
    item->data_size = 0;
}

static inline void
ToriRS_IO_ResetActive(struct ToriRS_IO* io)
{
    assert(io != NULL);
    memset(io->active, 0, io->active_count * sizeof(int));
    io->active_count = 0;
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
    /* Owned by nobody until the runner hands one over. Set here rather than in
     * each CreateTask_*: they calloc, and 0 is a real slot. */
    task->io_slot = -1;
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

    if( queue->head == task )
        queue->head = task->next;
    if( queue->tail == task )
        queue->tail = task->prev;

    task_free(task);
}

/* Task tracing is a boot-time decision, and the queue drains many tasks per
 * frame -- resolve the env once instead of walking environ per completion. */
static inline int
torirs_task_log_enabled(void)
{
    static int enabled = -1;
    if( enabled < 0 )
        enabled = getenv("TORIRS_TASK_LOG") != NULL;
    return enabled;
}

/*
 * Run ONE task, once, and reap it if it finished.
 *
 * This used to be `ToriRS_TaskQueue_Run`, which ran the head and nothing else:
 * it looped until the head yielded and then returned, so the queue could only
 * ever have one task in flight and therefore only one platform read
 * outstanding. Every cache miss cost a whole round trip end to end, and a boot
 * that reads a couple of thousand groups paid for them one after another.
 *
 * WHICH task to run is the runner's decision now (TaskRunner_Step), because
 * that is where the platform can be asked whether a given task's read has
 * landed. This function is left with the part that is genuinely the queue's:
 * run it, translate the protothread's answer, and unlink it when it ends.
 *
 * Returns one of TORIRS_ASYNCIO_STAT_*. On DONE the task has been freed and
 * the caller must not touch it again.
 */
static inline int
ToriRS_TaskQueue_RunTask(
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_IO* io,
    struct ToriRS_Task* task)
{
    int res;

    assert(queue != NULL);
    assert(task != NULL);

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_TASK_RESUMES, 1);
    res = task_run(task, io);

    // Return states for the protothread functions
    // #define PT_WAITING 0
    // #define PT_YIELDED 1
    // #define PT_EXITED 2
    // #define PT_ENDED 3
    switch( res )
    {
    case PT_YIELDED:
        /* Parked on client state (TASK_AWAIT_STATE): only another queue can
         * make that true, so stepping this task again is a busy-wait. */
        if( task->blocked )
            return TORIRS_ASYNCIO_STAT_BLOCKED;
        /* Asked for a frame before it is resumed. Still queued, still parked,
         * so its request is intact for the caller to read. */
        if( task->wants_render )
            return TORIRS_ASYNCIO_STAT_RENDER;
        /* Parked on IO, or simply slicing. Either way it made progress. */
        return TORIRS_ASYNCIO_STAT_YIELD;
    case PT_ENDED:
        /* Clean completion (reached PT_END). This is the normal, healthy path
         * for every task; it is only logged when task tracing is on (set
         * TORIRS_TASK_LOG) so the console is not spammed. A task that failed
         * prints its own diagnostic before exiting. */
        if( torirs_task_log_enabled() )
            fprintf(stderr, "Task %s completed\n", task->name);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_TASK_ENDS, 1);
        ToriRS_TaskQueue_Remove(queue, task);
        return TORIRS_ASYNCIO_STAT_DONE;
    case PT_EXITED:
        /* Early return via PT_EXIT. Some are benign guard clauses; others
         * follow an error the task already logged. Distinguished from a clean
         * end and gated behind the same trace flag. */
        if( torirs_task_log_enabled() )
            fprintf(stderr, "Task %s exited early (PT_EXIT)\n", task->name);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_TASK_ENDS, 1);
        ToriRS_TaskQueue_Remove(queue, task);
        return TORIRS_ASYNCIO_STAT_DONE;
    default:
        fprintf(stderr, "Task %s exited with unknown result\n", task->name);
        assert(0);
        return TORIRS_ASYNCIO_STAT_ERROR;
    }
}

static inline void
ToriRS_TaskQueue_Free(struct ToriRS_TaskQueue* queue)
{
    assert(queue != NULL);
    while( queue->head != NULL )
    {
        struct ToriRS_Task* task = queue->head;
        queue->head = task->next;
        /* vtable->free frees the task allocation itself (every task's Free
         * does) — freeing again here double-frees tasks still queued at
         * shutdown, which the async pipelines now legitimately leave behind. */
        task_free(task);
    }
    free(queue);
}

/**
 * Drive a heap-allocated child Task to completion from a parent protothread.
 * child_expr is evaluated once and stored in (pt)->user across yields.
 */
#define TASK_AWAITEX(task, pt, ctx, child_expr)                                                          \
    do                                                                                             \
    {                                                                                              \
        (pt)->lc = __LINE__;                                                                       \
        if( !(pt)->user )                                                                          \
            (pt)->user = (child_expr);                                                             \
        __attribute__((fallthrough));                                                              \
    case __LINE__:                                                                                 \
    {                                                                                              \
        struct ToriRS_Task* _child = (pt)->user;                                                   \
        int _await_res = task_run(_child, ctx);                                                    \
        if( _await_res != PT_ENDED && _await_res != PT_EXITED )                                    \
        {                                                                                          \
            /* A child that asked for the screen asked on its own behalf,                          \
             * but only the QUEUED task is ever looked at -- so carry the                          \
             * request up to the one the runner can see. Without this a                            \
             * nested loader can never show progress, however loudly it                            \
             * asks, because nothing ever reads the child. */                                      \
            if( _child->wants_render )                                                             \
            {                                                                                      \
                (task)->wants_render = 1;                                                          \
                (task)->render = _child->render;                                                   \
            }                                                                                      \
            return _await_res;                                                                     \
        }                                                                                          \
        task_free(_child);                                                                         \
        (pt)->user = NULL;                                                                         \
    }                                                                                              \
    } while( 0 )

/**
 * Like TASK_AWAITEX, but skip when child_expr evaluates to NULL
 * (CreateTask_*Load returns NULL when already cached).
 */
#define TASK_AWAITEX_IF(task, pt, ctx, expr)                                                             \
    do                                                                                             \
    {                                                                                              \
        (pt)->lc = __LINE__;                                                                       \
        if( !(pt)->user )                                                                          \
            (pt)->user = (expr);                                                                   \
        __attribute__((fallthrough));                                                              \
    case __LINE__:                                                                                 \
    {                                                                                              \
        struct ToriRS_Task* _child = (pt)->user;                                                   \
        if( _child )                                                                               \
        {                                                                                          \
            int _await_res = task_run(_child, ctx);                                                \
            if( _await_res != PT_ENDED && _await_res != PT_EXITED )                                \
            {                                                                                      \
                /* A child that asked for the screen asked on its own behalf,                          \
                 * but only the QUEUED task is ever looked at -- so carry the                          \
                 * request up to the one the runner can see. Without this a                            \
                 * nested loader can never show progress, however loudly it                            \
                 * asks, because nothing ever reads the child. */                                      \
                if( _child->wants_render )                                                             \
                {                                                                                      \
                    (task)->wants_render = 1;                                                          \
                    (task)->render = _child->render;                                                   \
                }                                                                                      \
                return _await_res;                                                                 \
            }                                                                                      \
            task_free(_child);                                                                     \
        }                                                                                          \
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

/**
 * PT_ prefix, matching PT_BEGIN/PT_INIT/PT_END: these SUSPEND the protothread.
 *
 * The prefix is the point. Control leaves this function here and comes back
 * later, and "later" is an unbounded amount of client activity: entities spawn
 * and despawn, and both world-pool indices and scene element ids are RECYCLED
 * across it. Anything DERIVED from those before the await -- a resolved pool
 * index, an element id, a pointer into a pool -- may name a different entity
 * afterwards while still looking perfectly valid, because the task struct
 * faithfully preserved the integer. That is the trap: hoisting state into
 * `self->` is what makes a protothread work, and it is also what makes a stale
 * index indistinguishable from a fresh one.
 *
 * Hold the STABLE identity across an await (a server slot or pid) and re-derive
 * after it; never the derived index. That exact mistake shipped: an npc retype
 * cached its target's pool index and element id before three of these and
 * applied them after, so calling a slow-loading familiar re-placed whatever
 * creature had inherited those indices in the meantime.
 */
#define PT_TASK_AWAITSELF(expr) TASK_AWAITEX(&(self->task), &(self->pt), io, expr)

/**
 * Publish a frame showing `intent` at `percent` saying `caption`, then
 * carry on. @see TASK_YIELD_TO_RENDER.
 */
#define PT_TASK_YIELD_TO_RENDER(intent_, percent_, caption_)                                   \
    TASK_YIELD_TO_RENDER(&(self->task), &(self->pt), (intent_), (percent_), (caption_))

/**
 * Like PT_TASK_AWAITSELF, but skip when child_expr evaluates to NULL
 * (CreateTask_*Load returns NULL when already cached). Still a suspension
 * point whenever the expression is non-NULL -- see above.
 */
#define PT_TASK_AWAITSELF_IF(expr) TASK_AWAITEX_IF(&(self->task), &(self->pt), io, expr)

#endif // ASYNCIO_H