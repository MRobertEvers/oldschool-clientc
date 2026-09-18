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

/*
 * Tasks and the reads they make.
 *
 * A task is a protothread on a queue. When it needs bytes it fills in ITS OWN
 * item (every task owns exactly one, `ToriRS_Task::io`), yields, and is not
 * resumed until the platform has answered that item. That is the whole model;
 * the two seams where it touches something outside itself are named where
 * they occur:
 *
 *   - the PLATFORM seam: `ToriRS_IOItem::pending`, written only by the
 *     executor (platform/platform_io.h). A task whose item is pending is
 *     skipped by the runner.
 *   - the OTHER-QUEUE seam: `ToriRS_Task::blocked` (TASK_AWAIT_STATE,
 *     PT_TASK_JOIN). A task waiting on state this queue cannot change is
 *     stepped once per pass, never spun.
 *
 * Everything else -- a child run inline, a fan-out of siblings, a request to
 * have the screen drawn -- is composition over those two.
 */

#define TORIRS_IOITEM_MAX_PATH 256
/* The active list's opening size; it grows on demand. */
#define TORIRS_IO_MAX_ITEMS 32

#define TORIRS_ASYNCIO_STAT_ERROR -1
#define TORIRS_ASYNCIO_STAT_YIELD 0
#define TORIRS_ASYNCIO_STAT_DONE 1
/* The task parked on client state rather than on a read (see
 * TASK_AWAIT_STATE). Nothing this queue can do will unblock it, so the caller
 * must hand control back to the frame loop instead of stepping again. */
#define TORIRS_ASYNCIO_STAT_BLOCKED 2
/* The task asked for a frame to be drawn before it is resumed, and said what
 * should be on it. Distinct from YIELD because a yield is not a frame
 * boundary -- the runner settles as many of those as it likes -- and distinct
 * from BLOCKED because this task is not waiting on anything. It is the only
 * way a long load can show its own progress. */
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
    /*
     * Make a set of cache groups RESIDENT without decoding any of them.
     *
     * The loading screen's `groups=all` fill wants exactly this: every group
     * of a table pulled through the producer and into the local store, so
     * that the reads which come after login are local. Asking for each group
     * as an ordinary CACHE read did that too, and paid for it three times
     * over -- one task, one item and one executor round trip per group, plus
     * a decode whose result was freed on the spot. On the browser lane that
     * was 24,000 tasks and 24,000 bzip2 passes for bytes nobody looked at.
     *
     * Addressed like a CACHE item (the same union member: epoch, table,
     * flags) with `archive_id` holding the COUNT and `data` LENDING an array
     * of that many group ids, exactly as FILE_WRITE lends its bytes: the
     * executor reads the ids before it does anything asynchronous and never
     * frees them. It answers with error_code 0 once every group has either
     * landed or been refused, and `data_size` says how many landed. A group
     * the cache does not hold is not a failure -- the reference table listed
     * it, the producer has nothing to say -- so only a dead transport
     * answers -1.
     */
    TORIRS_IOK_CACHE_PREFETCH,
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

/*
 * One platform request and its answer.
 *
 * Filled by a ToriRS_IO_Queue* call, handed to the executor by the runner,
 * answered in place (`error_code`, `data`, `data_size`), read by the task, and
 * released with ToriRS_IO_ClearItem. Its layout is published to the browser
 * executor by asyncio_abi.c, so a field added here is a field added there.
 */
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

    /*
     * THE PLATFORM SEAM. How many requests the executor still has on a wire
     * for this item: one for an ordinary read it could not answer inside
     * Process, one per group for a PREFETCH, zero once every answer is in.
     * Written only by the executor; read by the runner, which will not resume
     * a task while its item is pending. A synchronous answer never sets it.
     */
    int pending;
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
 * Release a served item.
 *
 * `data` is freed for every kind that FILLED it -- a read's payload belongs to
 * the item and this is where it dies. FILE_WRITE is the one kind that carries
 * data the other way: the bytes are the caller's, borrowed for the duration of
 * the request, and QueueFileWrite says in as many words that freeing them is
 * still the caller's job. PREFETCH lends its id array the same way.
 */
static inline void
ToriRS_IO_ClearItem(struct ToriRS_IOItem* item)
{
    assert(item != NULL);
    if( item->kind != TORIRS_IOK_FILE_WRITE && item->kind != TORIRS_IOK_CACHE_PREFETCH )
        free(item->data);
    item->kind = TORIRS_IOK_NONE;
    item->error_code = 0;
    item->data = NULL;
    item->data_size = 0;
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
     * resume, so it always describes the yield that just happened.
     *
     * Carried up from an awaited child (TASK_AWAITEX), exactly like
     * `wants_render` and for the same reason: the runner never sees a child,
     * so a block a child raises is only a block if its parent says so. */
    int blocked;

    /* Set by PT_TASK_YIELD_TO_RENDER for the duration of one yield: this
     * task wants a frame published before it is resumed, and `render` says
     * what should be on it. Cleared on every resume, exactly like
     * `blocked` -- a request describes one yield, never a standing mode. */
    int wants_render;
    struct ToriRS_RenderRequest render;

    /*
     * The one read this task may have out.
     *
     * A task names its item as "slot 0" (ToriRS_IO_TaskSlot) -- the hundred
     * call sites in the engine say `0`, and that has always meant "mine". A
     * child run inline by PT_TASK_AWAITSELF reads through its parent's item,
     * because the parent is the task the runner sees.
     *
     * Owned by the task, so it never moves and needs no table: a region
     * rebuild with four hundred loads out has four hundred tasks, each with
     * its item where it always was.
     */
    struct ToriRS_IOItem io;

    /*
     * This task is part of a frame's CS2 visual transaction: the tree it
     * mutates must not be published until it has finished.
     *
     * Set by TaskRunner_AddSettling (task_runner.h) and by nothing else.
     * TaskRunner_SettleFrame steps the runner only while such a task remains
     * queued; every task added with a plain ToriRS_TaskQueue_Add is a stream
     * -- a music track, a sound, an npc's body -- and a frame published over
     * a stream is a correct frame. Before the flag existed the settle waited
     * for the WHOLE queue: a music track chaining 44 reads through the
     * browser held the last frame on screen for 1.6 s at the Inferno's door.
     *
     * Children joined on a settling task (AddJoined) do not carry it: the
     * parent stays queued, parked on the join, and that is what holds the
     * frame.
     */
    int settles_frame;

    /*
     * The fan-out this task belongs to, or NULL.
     *
     * A task queued as a SIBLING on another task's behalf (ToriRS_TaskQueue_AddJoined)
     * carries a pointer to that task's outstanding count, and the queue
     * decrements it when this task ends -- however it ends. That is what lets
     * the parent wait for "every load I queued has finished" (PT_TASK_JOIN)
     * rather than for "every record I asked for is resident", which a record
     * the cache cannot serve never satisfies.
     *
     * The count lives in the parent, and the parent must outlive its siblings.
     * It does by construction: it is parked on the count reaching zero, and a
     * queue freed whole (ToriRS_TaskQueue_Free) frees without decrementing.
     */
    int* join;

    /* Telemetry only (task_runner_telemetry.h): reads issued back to back by
     * this task, each resumed on the previous one landing. */
    int read_chain;

    struct ToriRS_Task* next;
    struct ToriRS_Task* prev;
};

/*
 * The reads a pass has produced, and whose item "slot 0" names right now.
 *
 * Handed to Platform_IO_Process once per pass: every item queued since the
 * last hand-over goes out together, which is the whole of what makes a boot
 * stream instead of dribble. Nothing here outlives a pass except `task`,
 * which the queue sets around each run.
 */
struct ToriRS_IO
{
    /** The task whose item ToriRS_IO_TaskSlot answers with. Set by
     *  ToriRS_TaskQueue_RunTask around each run; a caller running a task by
     *  hand (task_run) sets it itself. */
    struct ToriRS_Task* task;
    /** Items queued since the last Process, in queue order. */
    struct ToriRS_IOItem** active;
    int active_count;
    int active_capacity;
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

/**
 * The item a task means by `slot_id`.
 *
 * One item per task, so the only slot a task may name is 0. A task that
 * parked a read at 1 was once judged idle, given a different base next pass,
 * and took its answer off another task's slot; the sequence loader did
 * exactly that and it read as scattered "failed to decode".
 */
static inline struct ToriRS_IOItem*
ToriRS_IO_TaskSlot(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io != NULL);
    assert(slot_id == 0);
    assert(io->task != NULL);
    (void)slot_id;
    return &io->task->io;
}

static inline void
task_free(struct ToriRS_Task* task)
{
    assert(task != NULL);
    /* A task that ends with an answer it never consumed would leak it; one
     * that ends with a read still queued would leave the executor an item to
     * fill that no longer exists. Both are released here, once. */
    ToriRS_IO_ClearItem(&task->io);
    if( task->vtable->free )
        task->vtable->free(task);
    else
        free(task);
}

/** Run `task` once in the io context `io` names. The caller has set
 *  `io->task`: the queue does so around every run, and a child run inline by
 *  TASK_AWAITEX runs in its parent's context on purpose. */
static inline int
task_run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    assert(task != NULL);
    assert(io != NULL);
    assert(io->task != NULL);
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

/* Append an item to this pass's batch; the list grows rather than refusing,
 * because a refusal here would be a bound on how much of the client's work
 * may be in flight, and no number is right for every scene. */
static inline void
push_active(
    struct ToriRS_IO* io,
    struct ToriRS_IOItem* item)
{
    assert(io != NULL);
    assert(item != NULL);
    if( io->active_count == io->active_capacity )
    {
        io->active_capacity = io->active_capacity ? io->active_capacity * 2 : TORIRS_IO_MAX_ITEMS;
        io->active = realloc(io->active, (size_t)io->active_capacity * sizeof(*io->active));
        assert(io->active);
    }
    io->active[io->active_count++] = item;
}

static inline struct ToriRS_IO*
ToriRS_IO_New(void)
{
    struct ToriRS_IO* io = calloc(1, sizeof(struct ToriRS_IO));
    assert(io != NULL);
    return io;
}

static inline void
ToriRS_IO_Free(struct ToriRS_IO* io)
{
    assert(io != NULL);
    free(io->active);
    free(io);
}

/* The item a Queue* call fills: the task's own, which must be free -- a task
 * queuing over an answer it has not cleared would leak the payload, and one
 * queuing over a read still on the wire would have two answers land in one
 * place. */
static inline struct ToriRS_IOItem*
io_item_to_fill(
    struct ToriRS_IO* io,
    int slot_id)
{
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_NONE);
    assert(item->pending == 0);
    memset(item, 0, sizeof(*item));
    return item;
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
    struct ToriRS_IOItem* item;

    assert(io != NULL);
    assert(table_id >= 0);
    assert(archive_id >= 0);
    assert(flags >= 0);
    item = io_item_to_fill(io, slot_id);
    item->kind = TORIRS_IOK_CACHE;
    item->u.cache.epoch = epoch;
    item->u.cache.table_id = table_id;
    item->u.cache.archive_id = archive_id;
    item->u.cache.flags = flags;
    push_active(io, item);
}

static inline void
ToriRS_IO_QueueConfigFile(
    struct ToriRS_IO* io,
    int slot_id,
    const char* path)
{
    struct ToriRS_IOItem* item;

    assert(io != NULL);
    assert(path != NULL);
    item = io_item_to_fill(io, slot_id);
    item->kind = TORIRS_IOK_CONFIG_FILE;
    strcpy(item->u.config_file.path, path);
    push_active(io, item);
}

static inline void
ToriRS_IO_QueueScript(
    struct ToriRS_IO* io,
    int slot_id,
    const char* path)
{
    struct ToriRS_IOItem* item;

    assert(io != NULL);
    assert(path != NULL);
    item = io_item_to_fill(io, slot_id);
    item->kind = TORIRS_IOK_SCRIPT;
    strcpy(item->u.script.path, path);
    push_active(io, item);
}

/** Read a client-owned file whole. `path` is used as given. */
static inline void
ToriRS_IO_QueueFileRead(
    struct ToriRS_IO* io,
    int slot_id,
    const char* path)
{
    struct ToriRS_IOItem* item;

    assert(io != NULL);
    assert(path != NULL);
    item = io_item_to_fill(io, slot_id);
    item->kind = TORIRS_IOK_FILE_READ;
    snprintf(item->u.file.path, sizeof(item->u.file.path), "%s", path);
    push_active(io, item);
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
    struct ToriRS_IOItem* item;

    assert(io != NULL);
    assert(path != NULL);
    assert(data != NULL || data_size == 0);
    item = io_item_to_fill(io, slot_id);
    item->kind = TORIRS_IOK_FILE_WRITE;
    snprintf(item->u.file.path, sizeof(item->u.file.path), "%s", path);
    item->data = data;
    item->data_size = data_size;
    push_active(io, item);
}

/**
 * Make `count` groups of `table_id` resident, without decoding them.
 *
 * `ids` is borrowed for the duration of the request, as FILE_WRITE's bytes
 * are: it must outlive the yield that waits for this item, and freeing it is
 * still the caller's job. See TORIRS_IOK_CACHE_PREFETCH.
 */
static inline void
ToriRS_IO_QueueCachePrefetch(
    struct ToriRS_IO* io,
    int slot_id,
    int epoch,
    int table_id,
    int flags,
    int* ids,
    int count)
{
    struct ToriRS_IOItem* item;

    assert(io != NULL);
    assert(table_id >= 0);
    assert(flags >= 0);
    assert(count > 0);
    assert(ids != NULL);
    item = io_item_to_fill(io, slot_id);
    item->kind = TORIRS_IOK_CACHE_PREFETCH;
    item->u.cache.epoch = epoch;
    item->u.cache.table_id = table_id;
    item->u.cache.archive_id = count;
    item->u.cache.flags = flags;
    item->data = ids;
    item->data_size = count * (int)sizeof(int);
    push_active(io, item);
}

/** How many of a prefetch's groups landed, once the item has been answered. */
static inline int
ToriRS_IO_PrefetchLanded(
    struct ToriRS_IO* io,
    int slot_id)
{
    struct ToriRS_IOItem* item;

    assert(io != NULL);
    item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE_PREFETCH);
    return item->error_code == 0 ? item->data_size : 0;
}

static inline void
ToriRS_IO_QueueReferenceTable(
    struct ToriRS_IO* io,
    int slot_id,
    int table_id)
{
    struct ToriRS_IOItem* item;

    assert(io != NULL);
    assert(table_id >= 0);
    item = io_item_to_fill(io, slot_id);
    item->kind = TORIRS_IOK_REFERENCE_TABLE;
    item->u.reference_table.table_id = table_id;
    push_active(io, item);
}

/** The executor has taken this pass's batch. The items stay wherever they
 *  are -- what is outstanding is on them, not here. */
static inline void
ToriRS_IO_ResetActive(struct ToriRS_IO* io)
{
    assert(io != NULL);
    io->active_count = 0;
}

static inline struct ToriRS_TaskQueue*
ToriRS_TaskQueue_New(void)
{
    struct ToriRS_TaskQueue* queue = calloc(1, sizeof(struct ToriRS_TaskQueue));
    assert(queue != NULL);
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

/**
 * Queue `task` as one of a fan-out that `join` counts.
 *
 * `task` may be NULL -- every CreateTask_*Load returns NULL for a record that
 * is already resident, and a fan-out over a list of ids has no reason to test
 * each one before calling this. A NULL is simply not counted.
 *
 * `*join` is the number of siblings still running; the queue decrements it as
 * each one ends. The parent waits on it with PT_TASK_JOIN. Returns how many
 * tasks were actually queued (0 or 1), so a caller can tally a fan-out.
 */
static inline int
ToriRS_TaskQueue_AddJoined(
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_Task* task,
    int* join)
{
    assert(queue != NULL);
    assert(join != NULL);
    if( !task )
        return 0;
    task->join = join;
    (*join)++;
    ToriRS_TaskQueue_Add(queue, task);
    return 1;
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

    /* One fewer sibling outstanding, whichever way this one ended -- a load
     * that exited early on a bad record counts exactly like one that landed,
     * because what the parent is waiting for is the END, not the record. */
    if( task->join )
    {
        assert(*task->join > 0);
        (*task->join)--;
    }

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
 * WHICH task to run is the runner's decision (TaskRunner_Step), because that
 * is where a task's item can be seen to be answered or not. This is the part
 * that is genuinely the queue's: run it in its own io context, translate the
 * protothread's answer, and unlink it when it ends.
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
    assert(io != NULL);
    assert(task != NULL);

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_TASK_RESUMES, 1);
    io->task = task;
    res = task_run(task, io);
    io->task = NULL;

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
        /* Clean completion (reached PT_END). Logged only under
         * TORIRS_TASK_LOG; a task that failed prints its own diagnostic
         * before exiting. */
        if( torirs_task_log_enabled() )
            fprintf(stderr, "Task %s completed\n", task->name);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_TASK_ENDS, 1);
        ToriRS_TaskQueue_Remove(queue, task);
        return TORIRS_ASYNCIO_STAT_DONE;
    case PT_EXITED:
        /* Early return via PT_EXIT. Some are benign guard clauses; others
         * follow an error the task already logged. */
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

/*
 * Drain the head, and only the head: strict FIFO, with no platform in the
 * loop. Tests drive a queue with this; the client drives its queues through
 * TaskRunner_Step, whose `parallel` flag is where the FIFO/overlap choice
 * lives. Returns when the head yields for any reason, or when the queue
 * empties.
 */
static inline int
ToriRS_TaskQueue_Run(
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_IO* io)
{
    assert(queue != NULL);
    while( queue->head != NULL )
    {
        int stat = ToriRS_TaskQueue_RunTask(queue, io, queue->head);
        if( stat != TORIRS_ASYNCIO_STAT_DONE )
            return stat;
    }
    return TORIRS_ASYNCIO_STAT_DONE;
}

static inline void
ToriRS_TaskQueue_Free(struct ToriRS_TaskQueue* queue)
{
    assert(queue != NULL);
    while( queue->head != NULL )
    {
        struct ToriRS_Task* task = queue->head;
        queue->head = task->next;
        /* Freed without decrementing any join: the parent that counted it is
         * in this same queue, ahead of or behind it, and is going the same
         * way. The async pipelines legitimately leave tasks behind at
         * shutdown. */
        task_free(task);
    }
    free(queue);
}

/**
 * Drive a heap-allocated child Task to completion from a parent protothread.
 * child_expr is evaluated once and stored in (pt)->user across yields.
 *
 * The child runs in the PARENT's io context: its reads are the parent's
 * reads, in the parent's item, because the parent is the task the runner
 * sees. So too its block and its render request, carried up below.
 */
#define TASK_AWAITEX(task, pt, ctx, child_expr)                                                    \
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
            if( _child->wants_render )                                                             \
            {                                                                                      \
                (task)->wants_render = 1;                                                          \
                (task)->render = _child->render;                                                   \
            }                                                                                      \
            if( _child->blocked )                                                                  \
                (task)->blocked = 1;                                                               \
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
#define TASK_AWAITEX_IF(task, pt, ctx, expr)                                                       \
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
                if( _child->wants_render )                                                         \
                {                                                                                  \
                    (task)->wants_render = 1;                                                      \
                    (task)->render = _child->render;                                               \
                }                                                                                  \
                if( _child->blocked )                                                              \
                    (task)->blocked = 1;                                                           \
                return _await_res;                                                                 \
            }                                                                                      \
            task_free(_child);                                                                     \
        }                                                                                          \
        (pt)->user = NULL;                                                                         \
    }                                                                                              \
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

/**
 * Wait until every sibling counted by `join_field` (an int on `self`, filled
 * by ToriRS_TaskQueue_AddJoined) has ended.
 *
 * The siblings run on their own queue -- the parallel asset queue, where the
 * runner puts all their reads on the wire together -- so this is a wait on
 * state THIS queue does not own, and it says so (`blocked`, see
 * TASK_AWAIT_STATE): the settle loop ends, the asset queue gets its turn, and
 * the parent is resumed once per pass rather than spun. Unlike a residency
 * wait it needs no budget: a task always ends, so the count always reaches
 * zero.
 *
 * A PT_ suspension point; see PT_TASK_AWAITSELF for what that costs you.
 */
#define PT_TASK_JOIN(join_field)                                                                   \
    do                                                                                             \
    {                                                                                              \
        while( (self)->join_field > 0 )                                                            \
        {                                                                                          \
            (self)->task.blocked = 1;                                                              \
            PT_YIELD(&(self)->pt);                                                                 \
        }                                                                                          \
    } while( 0 )

#endif
