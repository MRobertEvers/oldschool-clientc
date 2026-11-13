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
 * parks on a model load (game/test/task_order_test.c). The frame queue
 * overlaps: nothing downstream cares which of a region's models lands first,
 * and every read a pass produces leaves in one batch.
 *
 * Those two are DRAINED: the frame steps them to a fixed point. A third kind
 * of queue is GIVEN TIME instead -- work nothing in the frame waits on, run
 * for a slice whenever its owner has time to spare (TaskRunner_RunUntil). Its
 * passes carry a deadline on the queue's own clock (TaskRunner_SetClock); the
 * walk checks it between tasks and a long task checks it itself
 * (PT_TASK_YIELD_IF_SPENT), and a pass that stopped for it says so
 * (TASK_RUNNER_SPENT). A drained queue's passes have no deadline and never
 * read a clock.
 */
struct TaskRunner
{
    struct ToriRS_TaskQueue* queue;
    struct ToriRS_IOBatch* io;
    Platform_IO* px;
    /** May the walk pass over a parked task and run the ones behind it? */
    int parallel;
    /* A CS2 task has been enqueued (TaskRunner_AddRenderBlockingSerialTask) and the tree must
     * not be published until its whole host follow-up fixed point has settled
     * (app_settle_cs2_frame clears it). */
    int frame_settle_pending;
    /** Did the last Step (or any pass of the last SettleFrame) run anything? */
    int progressed;
    /* What the task asked to be drawn, valid only while the last Step
     * returned TASK_RUNNER_RENDER. */
    struct ToriRS_RenderRequest render;
    struct TaskRunnerTelemetry telemetry;
    /* TORIRS_TASK_SLOW_MS: name every task whose single run took at least
     * this long (0: off). Needs the queue's clock (TaskRunner_SetClock). */
    uint64_t slow_task_us;
};

/* The most reads one pass with a deadline queues before it stops the walk,
 * so that what Process has to answer at its end stays inside a slice. */
#define TASK_RUNNER_SLICE_READS_MAX 128

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
    /** The pass's slice ran out with ready work still queued (only a pass
     *  given a deadline, TaskRunner_StepUntil). Not progress: stepping again
     *  inside the same slice is exactly what the deadline forbids. */
    TASK_RUNNER_SPENT,
};

/*
 * Give a queue a clock, so its passes can be given a deadline.
 *
 * `now_us` is any monotonic microsecond clock; the client's is the
 * platform's, a test's is a counter it advances itself. A queue with no clock
 * can still be stepped, but not with a deadline.
 */
static inline void
TaskRunner_SetClock(
    struct TaskRunner* runner,
    uint64_t (*now_us)(void* ud),
    void* now_ud)
{
    assert(runner);
    assert(runner->io);
    assert(now_us);
    runner->io->slice.now_us = now_us;
    runner->io->slice.now_ud = now_ud;
}

static inline uint64_t
TaskRunner_Now(struct TaskRunner const* runner)
{
    assert(runner);
    assert(runner->io);
    assert(runner->io->slice.now_us);
    return runner->io->slice.now_us(runner->io->slice.now_ud);
}

/*
 * A unit of work handed to a queue that is given time, and when it must be
 * done by.
 *
 * `join` counts the job's root task the way a parent counts its fan-out
 * (ToriRS_TaskQueue_AddParallelPoolSubTask), so the job is done when that
 * task has ENDED, however it ended. The root fans its own work out on the
 * same queue and joins it, so "root ended" is "everything ended".
 *
 * `must_finish_by_us` is the job's own deadline on the queue's clock: past it,
 * the owner stops waiting for slices and finishes the job now (`promoted`),
 * because some answer is needed by then whatever it costs a frame. Who
 * promotes, and how, is the owner's: this is only the record.
 */
struct ToriRS_TaskJob
{
    int join;
    uint64_t must_finish_by_us;
    int promoted;
};

/** Hand `task` to `runner` as the root of `job`. */
static inline void
ToriRS_TaskJob_Start(
    struct ToriRS_TaskJob* job,
    struct TaskRunner* runner,
    struct ToriRS_Task* task,
    uint64_t must_finish_by_us)
{
    assert(job);
    assert(runner);
    assert(task);
    assert(job->join == 0);
    job->must_finish_by_us = must_finish_by_us;
    job->promoted = 0;
    ToriRS_TaskQueue_AddParallelPoolSubTask(runner->queue, task, &job->join);
}

static inline int
ToriRS_TaskJob_Done(struct ToriRS_TaskJob const* job)
{
    assert(job);
    return job->join == 0;
}

/*
 * Enqueue a task that belongs to the frame's CS2 visual transaction.
 *
 * The one way a task gets its `render_blocking` flag (asyncio.h). It also arms
 * the runner's frame_settle_pending, so the two facts cannot drift apart:
 * a settle that is pending has a flagged task to wait for, and a flagged
 * task always has a settle pending to run it before the frame publishes.
 *
 * On the asset runner that is every script, hook and transmit painter
 * (game/rs_cs2_dispatch.c). On the packet queue it is every packet: an
 * ordered application IS a transaction, so app_net.c adds with this too.
 */
static inline void
TaskRunner_AddRenderBlockingSerialTask(
    struct TaskRunner* runner,
    struct ToriRS_Task* task)
{
    assert(runner);
    assert(task);
    task->render_blocking = 1;
    ToriRS_TaskQueue_Add(runner->queue, task);
    runner->frame_settle_pending = 1;
}

/* Is any task of the frame's transaction still queued? The queue is short --
 * tens of tasks at the worst of a login -- so a walk per settle pass is
 * cheaper than keeping a count in step with every exit a task has. */
static inline int
TaskRunner_RenderBlockingRemains(struct TaskRunner const* runner)
{
    struct ToriRS_Task const* task;

    assert(runner);
    assert(runner->queue);
    for( task = runner->queue->head; task; task = task->next )
        if( task->render_blocking )
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
 *
 * So does a slice that has run out (TaskRunner_StepUntil): the clock is read
 * before every task but the first this pass runs -- one task always runs, or
 * a late caller would never move the queue at all -- and a task that stops
 * itself for it (PT_TASK_YIELD_IF_SPENT) ends the walk the same way. A pass
 * with no deadline, which is every pass of a drained queue, does neither.
 */
static inline enum TaskRunnerStat
TaskRunner_Step(struct TaskRunner* runner)
{
    struct ToriRS_Task* task;
    struct ToriRS_Task* next;
    int ran = 0;
    int runs = 0;
    int waiting = 0;
    int blocked = 0;
    int render = 0;
    int spent = 0;

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
        for( task = runner->queue->head; task && !render && !spent; task = next )
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
            if( runs > 0 && ToriRS_SliceSpent(&runner->io->slice) )
            {
                spent = 1;
                break;
            }
            /* A pass with a deadline also stops at a batch the deadline could
             * not pay for: the reads it has queued go out in Process, after
             * the walk and past any clock check, and a desktop executor answers
             * them all there and then -- a region's thousand-model fan-out
             * queued in under a millisecond took 11 ms to answer (rev 242,
             * ~11 us a read). Not SPENT: the time is not up, so the caller may
             * give the queue another pass, which lands this batch first. */
            if( runs > 0 && runner->io->slice.deadline_us != 0 &&
                runner->io->active_count >= TASK_RUNNER_SLICE_READS_MAX )
                break;

            row = TaskRunnerTelemetry_Row(&runner->telemetry, task);
            TaskRunnerTelemetry_Run(&runner->telemetry, row);
            runs++;
            if( runner->slow_task_us )
            {
                /* The name is copied first: a task that ends is freed by its run. */
                char name[sizeof(task->name)];
                uint64_t t0 = TaskRunner_Now(runner);
                uint64_t dt;

                memcpy(name, task->name, sizeof(name));
                stat = ToriRS_TaskQueue_RunTask(runner->queue, runner->io, task);
                dt = TaskRunner_Now(runner) - t0;
                if( dt >= runner->slow_task_us )
                    fprintf(stderr, "task_slow: %s %.2f ms\n", name[0] ? name : "(unnamed)", (double)dt / 1000.0);
            }
            else
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
            else if( stat == TORIRS_ASYNCIO_STAT_SPENT )
            {
                /* It ran, and stopped because the slice is over. */
                ran = 1;
                spent = 1;
            }
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
    if( spent )
        return TASK_RUNNER_SPENT;
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

/*
 * One pass that stops when the queue's clock reaches `deadline_us` (0: no
 * deadline, an ordinary TaskRunner_Step), or when it has queued
 * TASK_RUNNER_SLICE_READS_MAX reads. Needs a clock (TaskRunner_SetClock).
 *
 * The deadline lives for this pass only, so a queue that is also stepped
 * plainly elsewhere is never left with one.
 */
static inline enum TaskRunnerStat
TaskRunner_StepUntil(
    struct TaskRunner* runner,
    uint64_t deadline_us)
{
    enum TaskRunnerStat stat;

    assert(runner);
    assert(runner->io);
    assert(deadline_us == 0 || runner->io->slice.now_us);
    runner->io->slice.deadline_us = deadline_us;
    runner->io->slice.spent = 0;
    stat = TaskRunner_Step(runner);
    runner->io->slice.deadline_us = 0;
    runner->io->slice.spent = 0;
    return stat;
}

/*
 * Give a queue time: step it until `deadline_us` passes or it has nothing more
 * it can do now. The way a queue nothing waits on is run -- "until the frame
 * deadline", and again next time there is time.
 *
 * Stops on IDLE, SPENT, WAITING (the answers come between turns), BLOCKED
 * (the other queue has to move) and RENDER, exactly as a drain does; keeps
 * going on PROGRESSED while time remains. With `deadline_us` 0 it has no
 * limit and runs to one of those answers -- what an owner that has stopped
 * waiting does (ToriRS_TaskJob::promoted).
 */
static inline enum TaskRunnerStat
TaskRunner_RunUntil(
    struct TaskRunner* runner,
    uint64_t deadline_us)
{
    enum TaskRunnerStat stat;

    assert(runner);
    for( ;; )
    {
        stat = TaskRunner_StepUntil(runner, deadline_us);
        if( stat != TASK_RUNNER_PROGRESSED )
            break;
        if( deadline_us != 0 && TaskRunner_Now(runner) >= deadline_us )
        {
            stat = TASK_RUNNER_SPENT;
            break;
        }
    }
    return stat;
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
 * "Done" is when no task carrying render_blocking remains, whatever else the
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
        if( !TaskRunner_RenderBlockingRemains(runner) )
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

/*
 * TaskRunner_SettleFrame with a deadline on the queue's clock (0: none, an
 * ordinary settle). When the deadline passes with ready work still queued it
 * returns TASK_RUNNER_SPENT, which every caller of a settle already treats as
 * "not settled, resume on a later turn" -- the answer a parked read gives.
 * The clock is read between tasks, never inside one, and a pass always runs
 * at least one task, so a settle given an expired deadline still moves.
 */
static inline enum TaskRunnerStat
TaskRunner_SettleFrameUntil(
    struct TaskRunner* runner,
    uint64_t deadline_us)
{
    enum TaskRunnerStat stat;
    int progressed = 0;

    assert(runner);
    if( deadline_us == 0 )
        return TaskRunner_SettleFrame(runner);
    for( ;; )
    {
        stat = TaskRunner_StepUntil(runner, deadline_us);
        progressed |= runner->progressed;
        if( stat == TASK_RUNNER_RENDER )
            break;
        if( !TaskRunner_RenderBlockingRemains(runner) )
        {
            stat = TASK_RUNNER_IDLE;
            break;
        }
        if( stat != TASK_RUNNER_PROGRESSED )
            break;
        if( TaskRunner_Now(runner) >= deadline_us )
        {
            stat = TASK_RUNNER_SPENT;
            break;
        }
    }
    runner->progressed = progressed;
    return stat;
}

#endif
