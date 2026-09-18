#ifndef SRC_TASK_RUNNER_H
#define SRC_TASK_RUNNER_H

#include "asyncio.h"
#include "perf/torirs_perf.h"
#include "platform/platform_io.h"

#include <assert.h>

/*
 * Single owner of the "drive the task queue" idiom. Everything that needs
 * async work done goes through one of these two calls instead of hand-rolling
 * a Run/Process loop.
 *
 * Native: Platform_IO_Process satisfies IO synchronously, so each Step makes
 * forward progress and Drain terminates within the call.
 * WASM (future shell): Process only initiates fetches; Step returns PENDING
 * and the browser loop resumes us next frame — Drain must not be used there.
 */

/*
 * What the runner can say about how a queue is being driven.
 *
 * Counted, never sampled, so the numbers answer a question exactly: how many
 * passes ran nothing because every answer was still on the wire (the frame
 * had to end), how many reads each KIND of task issued and how many of those
 * were issued one after another by a task that had just been resumed on the
 * previous one landing. That last number is the serialized-request count --
 * a CS2 script resolving sprites one per yield, a map load asking for a
 * reference table then a square then a loc -- and it is what says whether
 * the platform is slow or the client is asking one thing at a time.
 *
 * Read by boot_telemetry.c (the report and the JSON the browser page pulls)
 * and by nothing else; it changes no decision.
 */
#define TASK_RUNNER_TELEMETRY_TASKS 48

struct TaskRunnerTaskTelemetry
{
    char name[32];
    long runs;
    long reads;
    /** Reads issued by a task resumed on its previous read landing. */
    long chained_reads;
    int max_chain;
};

struct TaskRunnerTelemetry
{
    long passes;
    long tasks_run;
    long reads_issued;
    /** Process calls that were handed at least one item. */
    long batches;
    int max_batch;
    /** Passes that stepped nothing while reads were still out. */
    long passes_waiting;
    /** Passes driven by the platform's landed hook rather than the frame. */
    long pump_passes;
    /** Set by the host around a pump-driven step; see App_PumpAsync. */
    int in_pump;
    struct TaskRunnerTaskTelemetry by_task[TASK_RUNNER_TELEMETRY_TASKS];
    int by_task_count;
    int by_task_overflow;
};

/* The row for a task's NAME, made on first sight; NULL once the table is
 * full, which is counted rather than asserted because a name is not a bug. */
static inline struct TaskRunnerTaskTelemetry*
TaskRunnerTelemetry_Row(
    struct TaskRunnerTelemetry* t,
    struct ToriRS_Task const* task)
{
    struct TaskRunnerTaskTelemetry* row;

    assert(t);
    assert(task);
    for( int i = 0; i < t->by_task_count; i++ )
    {
        if( strcmp(t->by_task[i].name, task->name) == 0 )
            return &t->by_task[i];
    }
    if( t->by_task_count == TASK_RUNNER_TELEMETRY_TASKS )
    {
        t->by_task_overflow++;
        return NULL;
    }
    row = &t->by_task[t->by_task_count++];
    memset(row, 0, sizeof(*row));
    strncpy(row->name, task->name, sizeof(row->name) - 1);
    return row;
}

struct TaskRunner
{
    struct ToriRS_TaskQueue* queue;
    struct ToriRS_IO* io;
    Platform_IO* px;
    /* A CS2 task has joined this queue and the tree/display list must not be
     * published until its whole host follow-up fixed point has settled. */
    int frame_settle_pending;
    /*
     * May this queue's tasks overlap?
     *
     * Off by default, and deliberately: strict FIFO is what a queue carrying
     * PACKETS needs -- the server chose the order of VARP, PLAYER_INFO, VARP
     * and the client must apply it, even when the middle one parks on a model
     * load (game/test/task_order_test.c pins exactly this).
     *
     * On for a queue carrying ASSET LOADS, where nothing downstream cares
     * which of a region's models lands first. That is where the cost was: one
     * network round trip per read, in a line, for the couple of thousand
     * groups a login streams.
     */
    int parallel;
    /*
     * Did the last pass advance anything?
     *
     * The frame loop's question, not the queue's. With several reads in flight
     * a pass almost always leaves something outstanding, so "is anything
     * pending" stopped being a useful reason to end the frame -- it capped the
     * client at one batch per frame while answers were arriving in under a
     * millisecond. What actually means "nothing more can happen here" is a
     * pass that ran nothing AND has reads outstanding.
     */
    int progressed;
    /* What the head task asked to be drawn, valid only while the last Step
     * returned TASK_RUNNER_RENDER. */
    struct ToriRS_RenderRequest render;
    struct TaskRunnerTelemetry telemetry;
};

enum TaskRunnerStat
{
    TASK_RUNNER_IDLE = 0,
    TASK_RUNNER_PENDING,
    /* The head task is waiting on state this queue does not own, so stepping
     * it again is a busy-wait. Like PENDING it means "work remains, retain the
     * frame" — every caller tests against IDLE — but unlike PENDING it must
     * end the settle loop rather than extend it. */
    TASK_RUNNER_BLOCKED,
    /* The head task asked for a frame before it is resumed, and said what
     * should be on it (runner->render). Work remains, so like PENDING every
     * caller keeps the frame alive -- but the settle loop must STOP, since
     * the whole point is that this frame reaches the screen. */
    TASK_RUNNER_RENDER,
};

/*
 * One scheduler pass: run every task that can make progress, then hand the
 * queued reads to the platform.
 *
 * ## Why this walks the queue instead of stepping its head
 *
 * It used to do two things that together made every cache read serial: it
 * returned as soon as ANY read was outstanding, and the queue it called ran
 * only its head. So the client held exactly one platform request at a time and
 * a boot spent one network round trip per group, in a strict line -- measured
 * on the browser lane at one in flight across two thousand requests, for a
 * post-login load of ~2500 groups.
 *
 * Neither layer underneath ever required that. The IO queue holds 32 items,
 * the desktop executor answers a whole list per call, and the browser one
 * dispatches every item without awaiting. Only the runner was single-file.
 *
 * So a pass now walks the queue and, for each task, asks the platform whether
 * THAT task's read has landed (Platform_IO_SlotPending). One still waiting is
 * skipped -- resuming it would run it over an empty slot -- and the rest are
 * stepped, each in its own IO slot. What comes out the far end is a list of
 * reads the executor issues together.
 *
 * Order is unchanged in the only sense a task can observe: the walk is
 * head-first, and a task is only ever passed over while it is parked on
 * something this pass cannot deliver. A task that awaits another
 * (PT_TASK_AWAITSELF) runs its child inline, on the parent's own slot, so a
 * chain stays a chain.
 *
 * RENDER ends the pass: the whole point of the request is that this frame
 * reaches the screen, and stepping other tasks past it would publish their
 * work on it too.
 */
/*
 * Enqueue a task that belongs to the frame's CS2 visual transaction.
 *
 * The one way a task gets its `settles_frame` flag (asyncio.h). It also arms
 * the runner's frame_settle_pending, so the two facts cannot drift apart:
 * a settle that is pending has a flagged task to wait for, and a flagged
 * task always has a settle pending to run it before the frame publishes.
 */
static inline void
TaskRunner_AddSettling(
    struct TaskRunner* runner,
    struct ToriRS_Task* task)
{
    assert(runner);
    assert(task);
    task->settles_frame = 1;
    ToriRS_TaskQueue_Add(runner->queue, task);
    runner->frame_settle_pending = 1;
}

/* Is any task of the frame's transaction still queued? The queue is short --
 * tens of tasks at the worst of a login -- so a walk per settle pass is
 * cheaper than keeping a count in step with every exit a task has. */
static inline int
TaskRunner_SettlingRemains(struct TaskRunner const* runner)
{
    struct ToriRS_Task const* task;

    assert(runner);
    assert(runner->queue);
    for( task = runner->queue->head; task; task = task->next )
        if( task->settles_frame )
            return 1;
    return 0;
}

static inline enum TaskRunnerStat
TaskRunner_Step(struct TaskRunner* runner)
{
    struct ToriRS_Task* task;
    struct ToriRS_Task* next;
    struct ToriRS_Task* render_task = NULL;
    int ran = 0;
    int waiting_io = 0;
    int blocked = 0;

    assert(runner && runner->queue && runner->io && runner->px);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_TASK_STEPS, 1);
    runner->telemetry.passes++;
    if( runner->telemetry.in_pump )
        runner->telemetry.pump_passes++;

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_QUEUE_RUN)
    {
        for( task = runner->queue->head; task && !render_task; task = next )
        {
            int slot;
            int stat;

            next = task->next;

            if( task->io_slot >= 0 )
            {
                int pending = 0;
                TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_IO)
                {
                    pending =
                        Platform_IO_SlotPending(runner->px, runner->io, task->io_slot);
                }
                /* Its answer is still on the wire. Nothing this pass can do
                 * for it, and running it would resume it over an empty slot. */
                if( pending )
                {
                    waiting_io = 1;
                    /* Ordered: the head owes an answer, and nothing behind it
                     * may overtake it. */
                    if( !runner->parallel )
                        break;
                    continue;
                }
            }
            else
            {
                task->io_slot = ToriRS_IO_SlotAlloc(runner->io);
                /* Every slot is already owned by a task with a read out. That
                 * is the concurrency ceiling doing its job, not an error: the
                 * rest of the queue waits for one of them to be answered. */
                if( task->io_slot < 0 )
                {
                    waiting_io = 1;
                    break;
                }
            }

            /* Remembered because RunTask may free the task, and the slot has
             * to be given back either way. */
            slot = task->io_slot;
            /* The row is taken BEFORE the run: a task that ends is freed by
             * RunTask, and its name with it. */
            {
                struct TaskRunnerTaskTelemetry* row =
                    TaskRunnerTelemetry_Row(&runner->telemetry, task);
                if( row )
                    row->runs++;
                runner->telemetry.tasks_run++;
                runner->io->slot_base = slot;
                stat = ToriRS_TaskQueue_RunTask(runner->queue, runner->io, task);
                runner->io->slot_base = 0;

                /* Did this run queue a read? Counted per task name, and as a
                 * chain when the task was resumed on its previous read
                 * landing and immediately asked for the next -- see
                 * TaskRunnerTelemetry. */
                if( stat != TORIRS_ASYNCIO_STAT_DONE )
                {
                    if( runner->io->io_slots[slot].kind != TORIRS_IOK_NONE )
                    {
                        task->read_chain++;
                        if( row )
                        {
                            row->reads++;
                            if( task->read_chain >= 2 )
                                row->chained_reads++;
                            if( task->read_chain > row->max_chain )
                                row->max_chain = task->read_chain;
                        }
                    }
                    else
                        task->read_chain = 0;
                }
            }

            /*
             * Re-read the successor of a task that is still here: a task that
             * fans loads out appends them behind itself (ToriRS_TaskQueue_AddJoined),
             * and they belong to THIS pass -- their reads are the ones meant
             * to go out together. With the successor taken before the run, a
             * fan-out at the tail saw NULL and its siblings waited a whole
             * pass, which on the frame loop is a whole frame per stage. A task
             * that ended has been freed, so the successor taken before stands.
             */
            if( stat != TORIRS_ASYNCIO_STAT_DONE )
                next = task->next;

            if( stat == TORIRS_ASYNCIO_STAT_DONE )
            {
                /* A task that ends with a read still queued is a task that
                 * asked for something and walked away; the item would sit in
                 * the slot forever and the slot would never come back. */
                if( runner->io->io_slots[slot].kind != TORIRS_IOK_NONE )
                    ToriRS_IO_ClearItem(&runner->io->io_slots[slot]);
                ToriRS_IO_SlotRelease(runner->io, slot);
                ran = 1;
                continue;
            }

            /*
             * A slot is held only while a read is actually outstanding. A task
             * parked on anything else -- another queue's state, a frame, a
             * plain cooperative yield -- gives it back, which is what keeps a
             * queue full of parked tasks from owning the whole table.
             */
            if( runner->io->io_slots[slot].kind == TORIRS_IOK_NONE )
            {
                ToriRS_IO_SlotRelease(runner->io, slot);
                task->io_slot = -1;
            }

            if( stat == TORIRS_ASYNCIO_STAT_RENDER )
            {
                render_task = task;
                break;
            }
            if( stat == TORIRS_ASYNCIO_STAT_BLOCKED )
                blocked = 1;
            else
                ran = 1;

            /* Strict FIFO: the head yielded, so nothing behind it may run --
             * see TaskRunner::parallel. A task that ENDED is different, and
             * the loop continues past it either way. */
            if( !runner->parallel )
                break;
        }
    }

    /* Every read this pass produced, handed over together -- which is the
     * whole point of the walk above. */
    if( runner->io->active_count > 0 )
    {
        runner->telemetry.batches++;
        runner->telemetry.reads_issued += runner->io->active_count;
        if( runner->io->active_count > runner->telemetry.max_batch )
            runner->telemetry.max_batch = runner->io->active_count;
    }
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_IO)
    {
        Platform_IO_Process(runner->px, runner->io);
    }

    runner->progressed = ran;
    if( !ran && waiting_io )
        runner->telemetry.passes_waiting++;

    if( render_task )
    {
        runner->render = render_task->render;
        return TASK_RUNNER_RENDER;
    }
    if( runner->queue->head == NULL )
        return TASK_RUNNER_IDLE;
    if( ran || waiting_io )
        return TASK_RUNNER_PENDING;
    /* Nothing ran, nothing is on the wire: everything left is waiting on
     * another queue, and only the frame loop can move that. */
    if( blocked )
        return TASK_RUNNER_BLOCKED;
    return TASK_RUNNER_PENDING;
}

/** Blocking drain — native and tests only.
 *
 * Returns with work still queued if the head blocks on another queue's state
 * (TASK_RUNNER_BLOCKED); a single-queue drain has no way to satisfy that, and
 * spinning here is the deadlock this status exists to prevent. Callers that
 * must settle such a task step both queues instead — see App_BootWait. */
static inline void
TaskRunner_Drain(struct TaskRunner* runner)
{
    enum TaskRunnerStat stat;

    /* A render request is honoured as a plain yield here: a blocking drain
     * has no frame loop to hand the screen to, and stopping for one would
     * leave the queue half-run. The task still gets stepped again, which is
     * all it actually needs; only the picture is lost, and in a drain there
     * is nobody to show it to. */
    do
    {
        stat = TaskRunner_Step(runner);
    } while( stat == TASK_RUNNER_PENDING || stat == TASK_RUNNER_RENDER );
}

/** Settle every task that can make progress before a frame is published.
 *
 * A cooperative task is allowed to yield arbitrarily often; a yield is not a
 * frame boundary.  Keep stepping until the queue is empty.  The sole reason
 * to return PENDING is a real platform request which has not completed yet.
 * That distinction matters on both hosts: browser reads are asynchronous, and
 * a native JS5-backed cache miss can be asynchronous too.  Callers must retain
 * the last settled frame while this returns PENDING and resume on the next
 * host turn.
 *
 * BLOCKED is the third exit and the reason the loop is not simply "step until
 * IDLE": a task waiting on another queue's state cannot be settled from here
 * at all, and the frame has to end for that other queue to run.  Callers treat
 * it exactly like PENDING — retain the frame, resume next turn. */
static inline enum TaskRunnerStat
TaskRunner_SettleFrame(struct TaskRunner* runner)
{
    enum TaskRunnerStat stat;

    do
    {
        int pending = 0;
        stat = TaskRunner_Step(runner);
        /* RENDER ends the loop for the opposite reason to BLOCKED: not
         * because nothing more can be done, but because the task asked for
         * this frame to be seen. Settling past it would draw the finished
         * state and the request would have achieved nothing. */
        if( stat == TASK_RUNNER_RENDER )
            break;
        /*
         * The transaction is settled once no task of it remains, whatever
         * else the queue still holds. The queue is shared with every asset
         * stream -- music, sounds, entity bodies, the animations a new npc
         * binds -- and none of those mutates the tree, so a frame published
         * over them is a correct frame. Waiting for them was the largest
         * single freeze at the Inferno's door: 1.6 s of retained frame while
         * the unlocked track's 44 reads came back one by one.
         */
        if( stat != TASK_RUNNER_IDLE && !TaskRunner_SettlingRemains(runner) )
        {
            stat = TASK_RUNNER_IDLE;
            break;
        }
        if( stat != TASK_RUNNER_PENDING )
            break;
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_IO)
        {
            pending = Platform_IO_Pending(runner->px, runner->io);
        }
        if( pending )
            break;
    } while( 1 );

    return stat;
}

#endif
