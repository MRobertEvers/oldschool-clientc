#ifndef SRC_TASK_RUNNER_H
#define SRC_TASK_RUNNER_H

#include "asyncio.h"
#include "perf/torirs_perf.h"
#include "platform/platform_io.h"
#include "task_runner_telemetry.h"

#include <assert.h>

/*
 * The one way a task queue is driven.
 *
 * A pass (TaskRunner_Step) is three things in a row, and the two seams are
 * the platform's two calls:
 *
 *   1. Platform_IO_Pump    -- answers that arrived since the last pass land.
 *   2. walk the queue      -- every task whose item is not pending is run
 *                             once; the reads they queue collect in `io`.
 *   3. Platform_IO_Process -- those reads go out together.
 *
 * On the desktop Process answers a disk read before it returns and only a
 * remote miss (JS5, on-demand) is left pending; in the browser every read
 * is left pending and lands between passes. The runner does not know which,
 * and does not ask: a task's item is pending, or it is not.
 *
 * The two queues the client runs differ in ONE setting, `parallel`. The
 * packet pipeline is a strict FIFO -- the server chose the order of VARP,
 * PLAYER_INFO, VARP and the client must apply it, even when the middle one
 * parks on a model load (game/test/task_order_test.c). The asset queue
 * overlaps: nothing downstream cares which of a region's models lands first,
 * and every read a pass produces leaves in one batch.
 */
struct TaskRunner
{
    struct ToriRS_TaskQueue* queue;
    struct ToriRS_IO* io;
    Platform_IO* px;
    /** May the walk pass over a parked task and run the ones behind it? */
    int parallel;
    /* A CS2 task has been enqueued (TaskRunner_AddSettling) and the tree must
     * not be published until its whole host follow-up fixed point has settled
     * (app_settle_cs2_frame clears it). */
    int frame_settle_pending;
    /** Did the last Step (or any pass of the last SettleFrame) run anything? */
    int progressed;
    /* What the task asked to be drawn, valid only while the last Step
     * returned TASK_RUNNER_RENDER. */
    struct ToriRS_RenderRequest render;
    struct TaskRunnerTelemetry telemetry;
};

enum TaskRunnerStat
{
    /** The queue is empty. */
    TASK_RUNNER_IDLE = 0,
    /** The pass ran something. More may be ready now: step again. */
    TASK_RUNNER_PROGRESSED,
    /** Nothing ran: every task left is parked on a read the platform has not
     *  answered. Only the platform can move this, so the frame has to end
     *  (browser) or the wire has to be pumped (desktop remote miss). */
    TASK_RUNNER_WAITING,
    /** Nothing ran and nothing is on the wire: every task left waits on
     *  state some OTHER queue owns. Stepping again is a busy-wait. */
    TASK_RUNNER_BLOCKED,
    /** A task asked for a frame before it is resumed, and said what should
     *  be on it (runner->render). Every loop stops here so the frame is
     *  actually seen. */
    TASK_RUNNER_RENDER,
};

/*
 * Enqueue a task that belongs to the frame's CS2 visual transaction.
 *
 * The one way a task gets its `settles_frame` flag (asyncio.h). It also arms
 * the runner's frame_settle_pending, so the two facts cannot drift apart:
 * a settle that is pending has a flagged task to wait for, and a flagged
 * task always has a settle pending to run it before the frame publishes.
 *
 * On the asset runner that is every script, hook and transmit painter
 * (game/rs_cs2_dispatch.c). On the packet FIFO it is every packet: an
 * ordered application IS a transaction, so app_net.c adds with this too.
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

/** How many tasks on this queue have a read on the wire. Diagnostics. */
static inline int
TaskRunner_ReadsOutstanding(struct TaskRunner const* runner)
{
    struct ToriRS_Task const* task;
    int count = 0;

    assert(runner);
    assert(runner->queue);
    for( task = runner->queue->head; task; task = task->next )
        if( task->io.pending )
            count++;
    return count;
}

/*
 * One pass: land what has arrived, run every task that can run, send what
 * they asked for.
 *
 * The walk is head-first and a task is only ever passed over while its item
 * is pending -- something this pass cannot deliver. A task that awaits
 * another (PT_TASK_AWAITSELF) runs its child inline, in its own item, so a
 * chain stays a chain. A task that fans loads out appends them behind itself
 * (ToriRS_TaskQueue_AddParallelPoolSubTask), and they belong to THIS pass: their reads are
 * the ones meant to go out together, so the successor is re-read after the
 * run.
 *
 * RENDER ends the walk: the point of the request is that this frame reaches
 * the screen, and stepping other tasks past it would publish their work on
 * it too. The reads queued so far still go out.
 */
static inline enum TaskRunnerStat
TaskRunner_Step(struct TaskRunner* runner)
{
    struct ToriRS_Task* task;
    struct ToriRS_Task* next;
    int ran = 0;
    int waiting = 0;
    int blocked = 0;
    int render = 0;

    assert(runner);
    assert(runner->queue);
    assert(runner->io);
    assert(runner->px);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_TASK_STEPS, 1);
    TaskRunnerTelemetry_Pass(&runner->telemetry);

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_IO)
    {
        Platform_IO_Pump(runner->px);
    }

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_QUEUE_RUN)
    {
        for( task = runner->queue->head; task && !render; task = next )
        {
            struct TaskRunnerTaskTelemetry* row;
            int stat;

            next = task->next;
            if( task->io.pending )
            {
                waiting = 1;
                if( runner->parallel )
                    continue;
                /* Ordered: the head owes an answer, and nothing behind it
                 * may overtake it. */
                break;
            }

            row = TaskRunnerTelemetry_Row(&runner->telemetry, task);
            TaskRunnerTelemetry_Run(&runner->telemetry, row);
            stat = ToriRS_TaskQueue_RunTask(runner->queue, runner->io, task);
            if( stat == TORIRS_ASYNCIO_STAT_DONE )
            {
                /* Freed; the successor taken before it ran stands. */
                ran = 1;
                continue;
            }

            TaskRunnerTelemetry_Yielded(row, task);
            next = task->next;
            if( stat == TORIRS_ASYNCIO_STAT_RENDER )
            {
                runner->render = task->render;
                render = 1;
            }
            else if( stat == TORIRS_ASYNCIO_STAT_BLOCKED )
                blocked = 1;
            else
                ran = 1;

            /* Strict FIFO: the head yielded, so nothing behind it may run. */
            if( !runner->parallel )
                break;
        }
    }

    TaskRunnerTelemetry_PassEnd(&runner->telemetry, runner->io->active_count, ran, waiting);
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_IO)
    {
        Platform_IO_Process(runner->px, runner->io);
    }

    runner->progressed = ran;
    if( render )
        return TASK_RUNNER_RENDER;
    if( runner->queue->head == NULL )
        return TASK_RUNNER_IDLE;
    if( ran )
        return TASK_RUNNER_PROGRESSED;
    if( waiting )
        return TASK_RUNNER_WAITING;
    /* A non-empty queue in which nothing ran and nothing is on the wire: the
     * walk visited at least one task, and the only way it neither ran nor
     * waited is that it blocked. */
    assert(blocked);
    (void)blocked;
    return TASK_RUNNER_BLOCKED;
}

/** Blocking drain -- native and tests only.
 *
 * Steps until the queue is empty. A pass that finds reads on the wire steps
 * again, because on the desktop the next pass's Pump is what lands them.
 *
 * Returns with work still queued if a task blocks on another queue's state
 * (TASK_RUNNER_BLOCKED); a single-queue drain has no way to satisfy that, and
 * spinning here is the deadlock this status exists to prevent. Callers that
 * must settle such a task step both queues instead -- see App_BootWait.
 *
 * A render request is honoured as a plain yield: a blocking drain has no
 * frame loop to hand the screen to. The task still gets stepped again, which
 * is all it actually needs. */
static inline void
TaskRunner_Drain(struct TaskRunner* runner)
{
    enum TaskRunnerStat stat;

    do
    {
        stat = TaskRunner_Step(runner);
    } while( stat != TASK_RUNNER_IDLE && stat != TASK_RUNNER_BLOCKED );
}

/** Settle the frame's transaction before a frame is published.
 *
 * A cooperative task may yield arbitrarily often; a yield is not a frame
 * boundary. So this steps while passes make progress, and stops for exactly
 * the reasons a pass cannot continue: the transaction is done (IDLE), a read
 * is on the wire (WAITING), a task waits on another queue (BLOCKED), or a
 * task asked for this frame to be seen (RENDER). Callers retain the last
 * settled frame on anything but IDLE and resume on the next host turn.
 *
 * "Done" is when no task carrying settles_frame remains, whatever else the
 * queue still holds: it is shared with every asset stream, and none of those
 * mutates the tree. `runner->progressed` reports whether ANY pass of this
 * call ran something, for a caller interleaving two queues (app_net.c). */
static inline enum TaskRunnerStat
TaskRunner_SettleFrame(struct TaskRunner* runner)
{
    enum TaskRunnerStat stat;
    int progressed = 0;

    for( ;; )
    {
        stat = TaskRunner_Step(runner);
        progressed |= runner->progressed;
        if( stat == TASK_RUNNER_RENDER )
            break;
        if( !TaskRunner_SettlingRemains(runner) )
        {
            stat = TASK_RUNNER_IDLE;
            break;
        }
        if( stat != TASK_RUNNER_PROGRESSED )
            break;
    }
    runner->progressed = progressed;
    return stat;
}

#endif
