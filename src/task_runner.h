#ifndef SRC_TASK_RUNNER_H
#define SRC_TASK_RUNNER_H

#include "asyncio.h"
#include "perf/torirs_perf.h"
#include "platform/platform_io.h"
#include "task_runner_telemetry.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * THE THREE QUEUES of the App, and what the runner enforces for each (the
 * frame contract is stated once, in app.h beside the three runners; `role`
 * below is how a runner knows which one it is):
 *
 *   MAIN (app->packet_runner)  strict FIFO (`parallel` 0): the server chose
 *       the order of VARP, PLAYER_INFO, VARP and the client applies it even
 *       when the middle one waits on a model (game/test/task_order_test.c).
 *       It is pumped until a task asks for a frame (RENDER), its head waits,
 *       or it is empty -- the boot runs here for that reason. The only queue
 *       on which a task may ask for a frame.
 *   PARALLEL (app->frame_runner)  overlapping (`parallel` 1): IO-heavy work
 *       that must not hold the packet pump. DRAINED: each pump steps every
 *       task that can run, then keeps stepping until every task is done,
 *       waiting on a read, or joined on tasks that are. No clock, no step
 *       count a real frame reaches, no task's request ends it.
 *   BACKGROUND (app->background_runner)  overlapping: CPU-heavy work, GIVEN
 *       TIME: each pump (TaskRunner_RunUntil) steps every task that can run
 *       ONCE whatever the clock says, then keeps stepping until every task is
 *       done or the slice is up. The only queue whose passes carry a
 *       deadline: the walk checks it between tasks, a long task checks it
 *       itself (PT_TASK_YIELD_IF_SPENT), and a pass that stopped for it says
 *       so (TASK_RUNNER_SPENT).
 *
 * A pump is TaskRunner_PumpBegin .. TaskRunner_PumpEnd; the runner numbers
 * them and stamps each task with the pump it last had a turn in
 * (ToriRS_Task::turn), which is how a timed walk knows whom it may pass over
 * when the slice is up (only those that have had their turn) and how
 * PumpEnd checks the guarantee. A runner with no role (a test's, a private
 * drag runner) is held to none of the role checks.
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
    /** The owner's latch: the work it last settled on this queue did not reach
     * a fixed point, and part of it is still parked (TaskRunner_SetHadWork).
     * The runner itself never sets or reads it; the queue's owner does, after
     * a settle or a pump. A queue whose owner never settles a transaction on
     * it leaves it zero. */
    int had_work;
    /** Did the last Step (or any pass of the last SettleFrame) run anything? */
    int progressed;
    /* What the task asked to be drawn, valid only while the last Step
     * returned TASK_RUNNER_RENDER. */
    struct ToriRS_RenderRequest render;
    /** A task on this (the main) queue asked for a frame (TASK_RUNNER_RENDER)
     *  and the owner has not yet said a frame was shown since
     *  (TaskRunner_RenderShown). The request means "show a frame before I am
     *  resumed", so no pump -- between frames, or at the top of the frame
     *  that is to show it -- steps this queue while it is set (App_PumpAsync). */
    int render_owed;
    struct TaskRunnerTelemetry telemetry;
    /* TORIRS_TASK_SLOW_MS: name every task whose single run took at least
     * this long (0: off). Needs the queue's clock (TaskRunner_SetClock). */
    uint64_t slow_task_us;

    /** Which of the App's queues this is (enum TaskRunnerRole); 0 for a
     *  runner that is none of them. Changes only what is asserted and what
     *  ends a pump, never how a pass walks (`parallel` decides that). */
    int role;
    /** The pump in progress, or the last one (never 0 once one has begun). */
    uint32_t pump;
    /** Between TaskRunner_PumpBegin and TaskRunner_PumpEnd. */
    int in_pump;
    /** A pass of this runner is on the stack: a task of THIS queue is
     *  running (the "who steps the background queue" check reads it). */
    int stepping;
    /** Why the last pump ended (TORIRS_QUEUE_CONTRACT_TRACE). */
    char const* pump_end;
    /** A parallel task whose one step took at least this long is computing,
     *  not waiting: named once per name per session (0: off). */
    uint64_t compute_step_us;
    /** Set while the App boots: the boot's loads are not what that
     *  threshold measures. */
    int compute_check_off;
    /** The longest single step run on this queue so far, and whose: on the
     *  background queue the unit by which a slice can be overrun. */
    uint64_t longest_step_us;
    char longest_step_name[32];
    char compute_named[16][32];
    int compute_named_count;
};

/* Which queue a runner is. 0 (none) is a runner of a test or a private
 * runner (the inventory drag's): it is held to no role's rules. */
enum TaskRunnerRole
{
    TASK_RUNNER_ROLE_NONE = 0,
    TASK_RUNNER_ROLE_MAIN,
    TASK_RUNNER_ROLE_PARALLEL,
    TASK_RUNNER_ROLE_BACKGROUND,
};

/* The most reads one pass with a deadline queues before it stops the walk,
 * so that what Process has to answer at its end stays inside a slice. A
 * task that has not yet had its turn this pump is run past it all the same
 * (the pump guarantee). */
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
     *  state some OTHER queue owns, or is joined on tasks that do. Stepping
     *  again is a busy-wait. */
    TASK_RUNNER_BLOCKED,
    /** A task on the MAIN queue asked for a frame before it is resumed, and
     *  said what should be on it (runner->render). The main queue's pump
     *  stops here so the frame is actually seen. No other queue returns it:
     *  there the request is a contract violation, reported and run past. */
    TASK_RUNNER_RENDER,
    /** The pass's slice ran out with ready work still queued (only a pass
     *  given a deadline, TaskRunner_StepUntil). Not progress: stepping again
     *  inside the same slice is exactly what the deadline forbids. */
    TASK_RUNNER_SPENT,
};

/* ---- the frame contract's run-time checks (TORIRS_QUEUE_CONTRACT_*) ----
 *
 * In the style of the UI scan meter (CLAUDE.md): a breach prints one
 * `queue_contract:` line naming the site and the task, a debug build asserts,
 * TORIRS_QUEUE_CONTRACT_STRICT=1 aborts in any build, and
 * TORIRS_QUEUE_CONTRACT_TRACE=1 prints why each pump ended and what it left
 * (2: every pump, not only those that left work behind).
 */
static inline int
TaskRunner_ContractStrict(void)
{
    static int strict = -1;
    if( strict < 0 )
    {
        char const* env = getenv("TORIRS_QUEUE_CONTRACT_STRICT");
        strict = (env && env[0] && env[0] != '0') ? 1 : 0;
    }
    return strict;
}

static inline int
TaskRunner_ContractTrace(void)
{
    static int trace = -1;
    if( trace < 0 )
    {
        char const* env = getenv("TORIRS_QUEUE_CONTRACT_TRACE");
        trace = (env && env[0]) ? atoi(env) : 0;
    }
    return trace;
}

/* One breach: the line, then an abort when strict. The caller asserts
 * straight after, so a debug build stops at the frame that broke it. */
static inline void
TaskRunner_ContractBreach(char const* line)
{
    assert(line);
    fprintf(stderr, "queue_contract: %s\n", line);
    fflush(stderr);
    if( TaskRunner_ContractStrict() )
        abort();
}

static inline char const*
TaskRunner_RoleName(int role)
{
    if( role == TASK_RUNNER_ROLE_MAIN )
        return "main";
    if( role == TASK_RUNNER_ROLE_PARALLEL )
        return "parallel";
    if( role == TASK_RUNNER_ROLE_BACKGROUND )
        return "background";
    return "unnamed";
}

/* May a task on this runner end a step with `stat`? RENDER only on the main
 * queue, SPENT only on the background queue; a runner with no role allows
 * both (the tests of the result kinds themselves run on one). */
static inline int
TaskRunner_ResultLegal(
    struct TaskRunner const* runner,
    int stat)
{
    assert(runner);
    if( stat == TORIRS_ASYNCIO_STAT_RENDER )
        return runner->role == TASK_RUNNER_ROLE_NONE || runner->role == TASK_RUNNER_ROLE_MAIN;
    if( stat == TORIRS_ASYNCIO_STAT_SPENT )
        return runner->role == TASK_RUNNER_ROLE_NONE || runner->role == TASK_RUNNER_ROLE_BACKGROUND;
    return 1;
}

/*
 * The frame contract's "who steps the background queue" check: it is given
 * time by its own pump and by nothing else -- never from inside a task of
 * any queue (a runner's `stepping`), nor from inside a site the App names
 * while it runs (`site`: the script settle, a logic tick and so the packet
 * pump, the parallel pump; NULL outside them). `owner` is 1 only for a job's
 * owner finishing its own job at its deadline (App_WorldJobPoll), the one
 * exception, which is allowed from a site but never from inside a task.
 */
static inline void
TaskRunner_CheckBackgroundEntry(
    struct TaskRunner const* main_queue,
    struct TaskRunner const* parallel_queue,
    struct TaskRunner const* background_queue,
    char const* site,
    int owner)
{
    char line[192];

    assert(main_queue);
    assert(parallel_queue);
    assert(background_queue);
    if( main_queue->stepping || parallel_queue->stepping || background_queue->stepping )
    {
        snprintf(
            line,
            sizeof(line),
            "the background queue was stepped from inside a task of the %s queue",
            main_queue->stepping ? "main" : parallel_queue->stepping ? "parallel" : "background");
        TaskRunner_ContractBreach(line);
        assert(!main_queue->stepping);
        assert(!parallel_queue->stepping);
        assert(!background_queue->stepping);
    }
    if( site && !owner )
    {
        snprintf(line, sizeof(line), "the background queue was stepped from inside %s", site);
        TaskRunner_ContractBreach(line);
        assert(!site);
    }
}

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

/* Has a task on this queue asked for a frame that has not been shown yet?
 * (`render_owed`: set when a pass returns TASK_RUNNER_RENDER.) */
static inline int
TaskRunner_RenderOwed(struct TaskRunner const* runner)
{
    assert(runner);
    return runner->render_owed;
}

/* The owner's word that a frame has been shown since the last render request:
 * the queue may be stepped again. The frame calls it at its end (App_RunOnce),
 * so a request made between frames is paid by the next frame, not by the
 * pump at that frame's top. */
static inline void
TaskRunner_RenderShown(struct TaskRunner* runner)
{
    assert(runner);
    runner->render_owed = 0;
}

/* Latch, or clear, the owner's "last pump left work parked" (`had_work`). */
static inline void
TaskRunner_SetHadWork(
    struct TaskRunner* runner,
    int had_work)
{
    assert(runner);
    runner->had_work = had_work;
}

/* Did the owner's last pump of this queue leave work parked? */
static inline int
TaskRunner_HadWork(struct TaskRunner const* runner)
{
    assert(runner);
    return runner->had_work;
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
 * Begin a pump: the span over which the pump guarantee is kept (every task
 * that can run gets a turn) and, at TaskRunner_PumpEnd, checked. Numbered,
 * so a task's `turn` says whether it has had its turn in this one.
 */
static inline void
TaskRunner_PumpBegin(struct TaskRunner* runner)
{
    assert(runner);
    assert(!runner->in_pump);
    runner->in_pump = 1;
    runner->pump++;
    if( runner->pump == 0 )
        runner->pump = 1;
}

/*
 * One pass: land what has arrived, run every task that can run, send what
 * they asked for.
 *
 * The walk is head-first and a task is only ever passed over while its item
 * is pending -- something this pass cannot deliver -- or, in a pass given a
 * deadline, once it has had its turn this pump and the slice is up. A task
 * that awaits another (PT_TASK_AWAITSELF) runs its child inline, in its own
 * item, so a chain stays a chain. A task that fans loads out appends them
 * behind itself (ToriRS_TaskQueue_AddParallelPoolSubTask), and they belong
 * to THIS pass: their reads are the ones meant to go out together, so the
 * successor is re-read after the run.
 *
 * RENDER ends the walk on the main queue: the point of the request is that
 * this frame reaches the screen, and stepping other tasks past it would
 * publish their work on it too. The reads queued so far still go out. On any
 * other queue it is a contract violation (asyncio.h): reported, asserted,
 * and run past as a plain yield.
 *
 * With a deadline (TaskRunner_StepUntil) the strict FIFO keeps its old rule:
 * the head always runs, and the clock is read before every task after it. An
 * overlapping queue keeps the pump guarantee instead: a task that has not
 * had its turn this pump runs whatever the clock says, and runs with grace
 * (ToriRS_Slice::grace) so its first PT_TASK_YIELD_IF_SPENT lets one unit of
 * work through; only tasks that HAVE had their turn are passed over once
 * the slice is up, or once the pass has queued TASK_RUNNER_SLICE_READS_MAX
 * reads. A task that stops itself for the slice no longer ends the walk:
 * the tasks behind it still get their turn. A pass with no deadline, which
 * is every pass of the main and the parallel queue, reads no clock for any
 * of this.
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
    int timed;
    int measure;

    assert(runner);
    assert(runner->queue);
    assert(runner->io);
    assert(runner->px);
    timed = runner->io->slice.deadline_us != 0;
    measure = runner->io->slice.now_us &&
              (runner->slow_task_us || runner->compute_step_us || runner->role == TASK_RUNNER_ROLE_BACKGROUND);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_TASK_STEPS, 1);
    TaskRunnerTelemetry_Pass(&runner->telemetry);

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_IO)
    {
        Platform_IO_Pump(runner->px);
    }

    runner->stepping++;
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_QUEUE_RUN)
    {
        for( task = runner->queue->head; task && !render; task = next )
        {
            struct TaskRunnerTaskTelemetry* row;
            int had_turn;
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
            had_turn = task->turn == runner->pump;
            if( timed && !runner->parallel )
            {
                if( runs > 0 && ToriRS_SliceSpent(&runner->io->slice) )
                {
                    spent = 1;
                    break;
                }
                /* A pass with a deadline also stops at a batch the deadline
                 * could not pay for: the reads it has queued go out in
                 * Process, after the walk and past any clock check, and a
                 * desktop executor answers them all there and then -- a
                 * region's thousand-model fan-out queued in under a
                 * millisecond took 11 ms to answer (rev 242, ~11 us a read).
                 * Not SPENT: the time is not up, so the caller may give the
                 * queue another pass, which lands this batch first. */
                if( runs > 0 && runner->io->active_count >= TASK_RUNNER_SLICE_READS_MAX )
                    break;
            }
            else if( timed && had_turn )
            {
                /* Overlapping and timed: only a task that has had its turn
                 * this pump is passed over, for the slice or for the batch;
                 * the walk goes on to the ones that have not. */
                if( spent || ToriRS_SliceSpent(&runner->io->slice) )
                {
                    spent = 1;
                    continue;
                }
                if( runner->io->active_count >= TASK_RUNNER_SLICE_READS_MAX )
                    continue;
            }

            row = TaskRunnerTelemetry_Row(&runner->telemetry, task);
            TaskRunnerTelemetry_Run(&runner->telemetry, row);
            runs++;
            task->turn = runner->pump;
            runner->io->slice.grace = timed && runner->parallel && !had_turn;
            if( measure )
            {
                /* The name is copied first: a task that ends is freed by its run. */
                char name[sizeof(task->name)];
                int const ordinary = !task->render_blocking;
                uint64_t t0 = TaskRunner_Now(runner);
                uint64_t dt;

                memcpy(name, task->name, sizeof(name));
                stat = ToriRS_TaskQueue_RunTask(runner->queue, runner->io, task);
                dt = TaskRunner_Now(runner) - t0;
                if( runner->slow_task_us && dt >= runner->slow_task_us )
                    fprintf(stderr, "task_slow: %s %.2f ms\n", name[0] ? name : "(unnamed)", (double)dt / 1000.0);
                if( dt > runner->longest_step_us )
                {
                    runner->longest_step_us = dt;
                    memcpy(runner->longest_step_name, name, sizeof(runner->longest_step_name));
                }
                if( runner->role == TASK_RUNNER_ROLE_PARALLEL && runner->compute_step_us && ordinary &&
                    !runner->compute_check_off && dt >= runner->compute_step_us )
                {
                    int seen = 0;
                    for( int i = 0; i < runner->compute_named_count; i++ )
                        if( strcmp(runner->compute_named[i], name) == 0 )
                            seen = 1;
                    if( !seen )
                    {
                        char line[192];
                        if( runner->compute_named_count < (int)(sizeof(runner->compute_named) / sizeof(runner->compute_named[0])) )
                            memcpy(runner->compute_named[runner->compute_named_count++], name, sizeof(name));
                        snprintf(
                            line,
                            sizeof(line),
                            "parallel task %s computed %.2f ms in one step (threshold %.2f ms): "
                            "CPU work belongs on the background queue",
                            name[0] ? name : "(unnamed)",
                            (double)dt / 1000.0,
                            (double)runner->compute_step_us / 1000.0);
                        TaskRunner_ContractBreach(line);
                    }
                }
            }
            else
                stat = ToriRS_TaskQueue_RunTask(runner->queue, runner->io, task);
            runner->io->slice.grace = 0;
            if( stat == TORIRS_ASYNCIO_STAT_DONE )
            {
                /* Freed; the successor taken before it ran stands. */
                ran = 1;
                continue;
            }

            TaskRunnerTelemetry_Yielded(row, task);
            next = task->next;
            if( !TaskRunner_ResultLegal(runner, stat) )
            {
                char line[160];
                snprintf(
                    line,
                    sizeof(line),
                    "task %s returned %s on the %s queue (RENDER is the main queue's alone, SPENT "
                    "the background queue's)",
                    task->name[0] ? task->name : "(unnamed)",
                    stat == TORIRS_ASYNCIO_STAT_RENDER ? "RENDER" : "SPENT",
                    TaskRunner_RoleName(runner->role));
                TaskRunner_ContractBreach(line);
                assert(TaskRunner_ResultLegal(runner, stat));
                /* Run past as a plain yield: this queue's pump is not ended
                 * by one task's request. */
                stat = TORIRS_ASYNCIO_STAT_YIELD;
            }
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
    runner->stepping--;

    TaskRunnerTelemetry_PassEnd(&runner->telemetry, runner->io->active_count, ran, waiting);
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_IO)
    {
        Platform_IO_Process(runner->px, runner->io);
    }

    runner->progressed = ran;
    if( render )
    {
        runner->render_owed = 1;
        return TASK_RUNNER_RENDER;
    }
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

static inline char const*
TaskRunner_StatName(enum TaskRunnerStat stat)
{
    if( stat == TASK_RUNNER_IDLE )
        return "idle";
    if( stat == TASK_RUNNER_PROGRESSED )
        return "progressed";
    if( stat == TASK_RUNNER_WAITING )
        return "waiting";
    if( stat == TASK_RUNNER_BLOCKED )
        return "blocked";
    if( stat == TASK_RUNNER_RENDER )
        return "render";
    return "spent";
}

/*
 * End a pump and check the pump guarantee on what it left. `why` names what
 * ended it (a TaskRunner_StatName, or the owner's word). Returns how many
 * tasks were left in breach.
 *
 * On the parallel and the background queue no task that can run may be left
 * without a turn this pump; on the parallel queue every task left must also
 * be waiting on a read (its item pending) or blocked -- joined on tasks that
 * are, which is every BLOCKED a parallel task may legitimately return (the
 * table of them: pm/PLATFORM_CONFORMANCE.md). A blocked task is named, not
 * breached, under TORIRS_QUEUE_CONTRACT_TRACE: the runner cannot tell a join
 * from a wait on state, and until no state wait remains the trace names them.
 * The main queue is a FIFO pumped until its head waits, and is not checked
 * here.
 */
static inline int
TaskRunner_PumpEnd(
    struct TaskRunner* runner,
    char const* why)
{
    struct ToriRS_Task const* task;
    int breaches = 0;
    int pending = 0;
    int blocked = 0;
    char names[160];
    int names_len = 0;

    assert(runner);
    assert(runner->queue);
    assert(runner->in_pump);
    runner->in_pump = 0;
    runner->pump_end = why;
    names[0] = '\0';
    if( runner->role != TASK_RUNNER_ROLE_PARALLEL && runner->role != TASK_RUNNER_ROLE_BACKGROUND )
        return 0;
    /* TORIRS_SERIAL_ASSETS makes the parallel queue a FIFO for debugging
     * (app.c): the tasks behind a waiting head are held on purpose, so the
     * guarantee is not checked while it is on. */
    if( !runner->parallel )
        return 0;
    for( task = runner->queue->head; task; task = task->next )
    {
        if( task->io.pending )
        {
            pending++;
            continue;
        }
        if( task->turn != runner->pump )
        {
            char line[160];
            snprintf(
                line,
                sizeof(line),
                "%s pump %u (%s) left %s unstepped while it could run",
                TaskRunner_RoleName(runner->role),
                (unsigned)runner->pump,
                why ? why : "?",
                task->name[0] ? task->name : "(unnamed)");
            TaskRunner_ContractBreach(line);
            assert(task->turn == runner->pump);
            breaches++;
            continue;
        }
        if( runner->role != TASK_RUNNER_ROLE_PARALLEL )
            continue;
        if( !task->blocked )
        {
            char line[160];
            snprintf(
                line,
                sizeof(line),
                "parallel pump %u (%s) ended with %s still able to run (neither waiting on a read "
                "nor joined)",
                (unsigned)runner->pump,
                why ? why : "?",
                task->name[0] ? task->name : "(unnamed)");
            TaskRunner_ContractBreach(line);
            assert(task->blocked);
            breaches++;
            continue;
        }
        blocked++;
        if( names_len < (int)sizeof(names) - 40 )
            names_len += snprintf(
                names + names_len,
                sizeof(names) - (size_t)names_len,
                " %s%s",
                task->name[0] ? task->name : "(unnamed)",
                task->render_blocking ? "(script)" : "");
    }
    if( TaskRunner_ContractTrace() >= 2 || (TaskRunner_ContractTrace() >= 1 && (blocked > 0 || breaches > 0)) )
        fprintf(
            stderr,
            "queue_contract: trace %s pump %u ended %s: %d waiting on a read, %d blocked%s%s\n",
            TaskRunner_RoleName(runner->role),
            (unsigned)runner->pump,
            why ? why : "?",
            pending,
            blocked,
            blocked ? ":" : "",
            names);
    return breaches;
}

/*
 * One pass that stops when the queue's clock reaches `deadline_us` (0: no
 * deadline, an ordinary TaskRunner_Step), or when it has queued
 * TASK_RUNNER_SLICE_READS_MAX reads. Needs a clock (TaskRunner_SetClock).
 *
 * The deadline lives for this pass only, so a queue that is also stepped
 * plainly elsewhere is never left with one. A timed pass outside any pump is
 * a pump of its own (unchecked): every task in it gets its turn.
 */
static inline enum TaskRunnerStat
TaskRunner_StepUntil(
    struct TaskRunner* runner,
    uint64_t deadline_us)
{
    enum TaskRunnerStat stat;
    int lone;

    assert(runner);
    assert(runner->io);
    assert(deadline_us == 0 || runner->io->slice.now_us);
    lone = deadline_us != 0 && !runner->in_pump;
    if( lone )
        TaskRunner_PumpBegin(runner);
    runner->io->slice.deadline_us = deadline_us;
    runner->io->slice.spent = 0;
    runner->io->slice.grace = 0;
    stat = TaskRunner_Step(runner);
    runner->io->slice.deadline_us = 0;
    runner->io->slice.spent = 0;
    runner->io->slice.grace = 0;
    if( lone )
        runner->in_pump = 0;
    return stat;
}

/*
 * Give a queue time: ONE PUMP of it. Every task that can run gets a turn
 * (the first pass runs each one whatever the clock says); then it is stepped
 * until `deadline_us` passes or it has nothing more it can do now. The way
 * the background queue is run -- "until the frame deadline", and again next
 * time there is time -- and it keeps its guarantee even when that overruns
 * the frame: the owner's rule is that the background queue always makes some
 * progress, deadline or not.
 *
 * Stops on IDLE, SPENT, WAITING (the answers come between turns) and BLOCKED
 * (the other queue has to move); keeps going on PROGRESSED while time
 * remains. With `deadline_us` 0 it has no limit and runs to one of those
 * answers -- what an owner that has stopped waiting does
 * (ToriRS_TaskJob::promoted). The pump is checked when it ends
 * (TaskRunner_PumpEnd).
 */
static inline enum TaskRunnerStat
TaskRunner_RunUntil(
    struct TaskRunner* runner,
    uint64_t deadline_us)
{
    enum TaskRunnerStat stat;

    assert(runner);
    TaskRunner_PumpBegin(runner);
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
    (void)TaskRunner_PumpEnd(runner, TaskRunner_StatName(stat));
    return stat;
}

/*
 * ONE PUMP of a drained queue -- the parallel queue's, every frame: every
 * task that can run gets a turn, and passes go on until nothing more can
 * move (every task done, waiting on a read, or joined on tasks that are).
 * No clock, and no task's request ends it.
 *
 * `step_limit` is a runaway backstop, never a bound a real pump reaches:
 * `*steps` says how many passes ran (`progressed` whether any ran a task),
 * and an owner that sees the limit
 * reached with the queue still progressing aborts (App_PumpAsync). Such a
 * pump is not checked, since the owner is about to say why.
 */
static inline enum TaskRunnerStat
TaskRunner_PumpToStandstill(
    struct TaskRunner* runner,
    int step_limit,
    int* steps)
{
    enum TaskRunnerStat stat;
    int passes = 0;
    int progressed = 0;

    assert(runner);
    assert(step_limit > 0);
    assert(steps);
    TaskRunner_PumpBegin(runner);
    do
    {
        stat = TaskRunner_Step(runner);
        progressed |= runner->progressed;
        passes++;
    } while( stat == TASK_RUNNER_PROGRESSED && passes < step_limit );
    /* Whether ANY pass ran something, as a settle reports it. */
    runner->progressed = progressed;
    *steps = passes;
    if( stat == TASK_RUNNER_PROGRESSED )
        runner->in_pump = 0;
    else
        (void)TaskRunner_PumpEnd(runner, TaskRunner_StatName(stat));
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
 * call ran something, for a caller interleaving two queues (app_net.c).
 *
 * So a settle is NOT a pump of the parallel queue: it stops when the
 * render-blocking tasks are done, with ordinary tasks still able to move.
 * The frame drains those after it (TaskRunner_PumpToStandstill in
 * App_PumpAsync). RENDER can only come from the main queue (the packet
 * queue's settle in app_net.c, the boot's in App_PumpAsync). */
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
 *
 * Used on the MAIN queue only: the packet pump's paced catch-up after a
 * background rebuild's commit (app_net.c, App_PacedDeadline). The parallel
 * queue is never given a deadline (app.h, the frame contract).
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
