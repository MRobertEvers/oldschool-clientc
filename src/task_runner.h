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
 *       task that can run, then keeps stepping until every task is done or
 *       PARKED -- on its read, a trigger, a count or a cycle (task_trigger.h).
 *       No clock, no step count a real frame reaches, no task's request ends
 *       it. A parked task is passed over and costs nothing.
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
    /** A parallel task whose one step COST at least this much processor
     *  time is computing, not waiting: named once per name per session (0:
     *  off). Read on `cost_now_us` when the runner has one (the stepping
     *  thread's CPU clock, TaskRunner_SetCostClock), else on the queue's
     *  wall clock; the breach line gives both. */
    uint64_t compute_step_us;
    /** The clock a step's cost is read on (NULL: the wall clock). */
    uint64_t (*cost_now_us)(void);
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
    /** Nothing ran: every task left is PARKED -- on a read the platform has
     *  not answered, or on a trigger, a count or a cycle (task_trigger.h).
     *  Only what it parked on can move it: a read landing (the platform), a
     *  trigger firing, a count reaching zero, a clock advancing. Stepping
     *  again before one of those happens runs nothing. */
    TASK_RUNNER_WAITING,
    /** A task on the MAIN queue asked for a frame before it is resumed, and
     *  said what should be on it (runner->render). The main queue's pump
     *  stops here so the frame is actually seen. No other queue returns it:
     *  there the request is a contract violation, reported and run past. */
    TASK_RUNNER_RENDER,
    /** The pass's slice ran out with ready work still queued (only a pass
     *  given a deadline, TaskRunner_StepUntil). Not progress: stepping again
     *  inside the same slice is exactly what the deadline forbids. The
     *  BACKGROUND queue's alone (and a role-less test runner's): it is the
     *  only queue that is time-sliced. */
    TASK_RUNNER_SPENT,
    /** The MAIN queue's paced pass reached its deadline and stopped BETWEEN
     *  tasks, ready work still queued (the paced catch-up after a background
     *  rebuild's commit, App_PacedDeadline). Not a slice: no task stopped
     *  for it, and none could (ToriRS_Slice::sliced). The owner treats it
     *  as it treats a parked head: not settled, resume on a later turn. */
    TASK_RUNNER_PACED,
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
 * The between-frames pump's check (App_PumpAsync from_frame 0: the browser's
 * torirs_web_io_pump, the native loop's naps), on its way out: what it
 * stepped besides the parallel queue, counted by the main and background
 * queues' own pass counters (telemetry.passes) across the pump. It may step
 * the main queue only while the boot runs there; after the boot the main
 * queue is the packet pump's, a frame's. It never steps the background
 * queue, which is given its floor and its slack by the frame and the loop.
 * `main_head` names the main queue's head for the line (NULL: empty).
 */
static inline void
TaskRunner_CheckBetweenFramesExit(
    int booting,
    long main_passes,
    long background_passes,
    char const* main_head)
{
    char line[192];

    assert(main_passes >= 0);
    assert(background_passes >= 0);
    if( background_passes > 0 )
    {
        snprintf(
            line,
            sizeof(line),
            "the between-frames pump stepped the background queue (%ld passes)",
            background_passes);
        TaskRunner_ContractBreach(line);
        assert(background_passes == 0);
    }
    if( !booting && main_passes > 0 )
    {
        snprintf(
            line,
            sizeof(line),
            "the between-frames pump stepped the main queue after the boot (%ld passes, head %s)",
            main_passes,
            main_head ? main_head : "-");
        TaskRunner_ContractBreach(line);
        assert(main_passes == 0);
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

/*
 * The clock the compute check reads a step's COST on: processor time of the
 * thread that steps the queue (PlatformWindow_ThreadCpuUs on the App's
 * queues), so a step measured on a loaded machine reads what it computed,
 * not how long it waited for a core. Without one the check reads the wall
 * clock. The runner names no platform: the clock is handed in.
 */
static inline void
TaskRunner_SetCostClock(
    struct TaskRunner* runner,
    uint64_t (*cost_now_us)(void))
{
    assert(runner);
    assert(cost_now_us);
    runner->cost_now_us = cost_now_us;
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
 *
 * A job is an OWNER of triggers (task_trigger.h): it has one end
 * (ToriRS_TaskJob_End, called by whoever owns the job where it commits or is
 * cancelled), and every trigger it primed and did not fire is abandoned
 * there. One job struct may serve many jobs in turn; each runs from the
 * prime its owner makes to that end.
 */
struct ToriRS_TaskJob
{
    int join;
    uint64_t must_finish_by_us;
    int promoted;
    struct ToriRS_TriggerOwner owner;
};

/* The job as an owner of triggers, named `name` (borrowed). */
static inline struct ToriRS_TriggerOwner*
ToriRS_TaskJobOwner(
    struct ToriRS_TaskJob* job,
    char const* name)
{
    assert(job);
    assert(name);
    if( job->owner.kind == TORIRS_OWNER_NONE )
        ToriRS_TriggerOwner_Init(&job->owner, TORIRS_OWNER_JOB, name);
    return &job->owner;
}

/* THE ONE PLACE A JOB ENDS: every trigger it still has primed is abandoned.
 * `completed`: it committed (a trigger still primed then is one it forgot to
 * fire); 0: it was cancelled. */
static inline void
ToriRS_TaskJob_End(
    struct ToriRS_TaskJob* job,
    int completed)
{
    assert(job);
    ToriRS_TriggerOwner_End(&job->owner, completed);
}

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
 *
 * SERIAL among themselves, on the parallel queue too: a script waits its turn
 * behind the script queued before it (parked on that task's `ended`, a TURN
 * wait) and is not stepped until it has ended. The reference runs client
 * scripts one at a time, to their end, in the order they were raised; here a
 * script may stop for a required read, and on an overlapping queue the ones
 * behind it then ran from what it had read and not yet written. A burst of
 * keys typed in one frame queues one onKey script per key, the first of
 * which stops for the chat line's procs the first time anything is typed:
 * each read the empty line, and the line kept only the last key (G1, web and
 * native alike). Ordinary tasks still overlap with the scripts and with each
 * other: only render-blocking tasks form this lane.
 */
static inline void
TaskRunner_AddRenderBlockingSerialTask(
    struct TaskRunner* runner,
    struct ToriRS_Task* task)
{
    struct ToriRS_Task* ahead;

    assert(runner);
    assert(task);
    task->render_blocking = 1;
    ahead = runner->queue->serial_tail;
    ToriRS_TaskQueue_Add(runner->queue, task);
    runner->queue->serial_tail = task;
    runner->frame_settle_pending = 1;
    /* A strict-FIFO queue never steps past its head, so only an overlapping
     * one needs the wait made. */
    if( runner->parallel && ahead )
    {
        ToriRS_TaskAskTrigger(task, TORIRS_TASK_WAIT_TURN, &ahead->ended);
        ToriRS_TaskPark(task);
    }
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

/** Is a task on this queue ready -- neither waiting on its read nor parked?
 *  Walks the queue: asked only when something was woken since the owner's
 *  last pump (App_AsyncRunnable), never per pass. */
static inline int
TaskRunner_HasReady(struct TaskRunner const* runner)
{
    struct ToriRS_Task const* task;

    assert(runner);
    assert(runner->queue);
    for( task = runner->queue->head; task; task = task->next )
        if( !task->io.pending && !task->parked )
            return 1;
    return 0;
}

/*
 * The frame contract's "the parallel queue is drained every frame", checked
 * where a frame is PUBLISHED (`site`): no task a pump has stepped may be
 * ready then -- every one is done, or parked on its read, a trigger, a count
 * or a cycle. A ready task that has had a turn is one a pump left behind (a
 * settle that stops when its scripts are done, a packet pump that readied
 * it), applied a frame late; the breach line names it and a debug build
 * asserts.
 *
 * A task never stepped (`turn` 0) is passed over: it was added after the
 * frame's last pump, by the walk that builds what is published (an asset the
 * emit asked for as it drew: AssetLoad:config, a sprite). That is the one
 * path that cannot pump before its publish: the emit list is already built,
 * and a pump after it would change the scene the platform renders under it.
 * Such a task starts at the next frame's top pump and never holds a frame.
 *
 * A task a CANCEL woke is passed over too. The client still cancels after
 * that pump -- the frame's interface rebind (app_if_head_poll) cancels the
 * head task of an interface that closed -- and a cancel makes a parked task
 * ready (rs_pending.c), but all it will do is exit: nothing on the frame is
 * late. Any other wake there is still work left behind. (A pump to a
 * standstill runs a task it woke, so one still ready with this wake was woken
 * after the pump.)
 */
static inline void
TaskRunner_CheckPublishedAtStandstill(
    struct TaskRunner const* parallel_queue,
    char const* site)
{
    struct ToriRS_Task const* task;

    assert(parallel_queue);
    assert(parallel_queue->queue);
    assert(site);
    for( task = parallel_queue->queue->head; task; task = task->next )
        if( !task->io.pending && !task->parked && task->turn != 0 && task->woke != TORIRS_WAKE_CANCELLED )
            break;
    if( task )
    {
        char line[192];

        snprintf(
            line,
            sizeof(line),
            "%s published a frame over a ready parallel task %s: the parallel queue was not "
            "pumped to a standstill",
            site,
            task->name[0] ? task->name : "(unnamed)");
        TaskRunner_ContractBreach(line);
        assert(!task);
    }
}

/** Is a task on this queue ready and still without its turn this pump? A
 *  task a wake readied after the pump's walk had passed it (a fan-out's last
 *  sibling ending behind its parent) is one: guarantee 1 is owed to it too. */
static inline int
TaskRunner_HasReadyWithoutTurn(struct TaskRunner const* runner)
{
    struct ToriRS_Task const* task;

    assert(runner);
    assert(runner->queue);
    for( task = runner->queue->head; task; task = task->next )
        if( !task->io.pending && !task->parked && task->turn != runner->pump )
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
            /* Waiting on its read, or parked on a trigger, a count or a
             * cycle: passed over, and never run until that moves it. */
            if( task->io.pending || task->parked )
            {
                waiting = 1;
                if( runner->parallel )
                    continue;
                /* Ordered: the head waits, and nothing behind it may
                 * overtake it. */
                break;
            }
            had_turn = task->turn == runner->pump;
            if( timed && !runner->parallel )
            {
                /* The clock, read here between tasks: on the main queue this
                 * deadline paces the pump and is never a slice a task sees
                 * (ToriRS_Slice::sliced), so it is not asked through the
                 * task-facing check. */
                if( runs > 0 && TaskRunner_Now(runner) >= runner->io->slice.deadline_us )
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
                uint64_t const c0 = runner->cost_now_us ? runner->cost_now_us() : 0;
                uint64_t t0 = TaskRunner_Now(runner);
                uint64_t dt;
                uint64_t cost;

                memcpy(name, task->name, sizeof(name));
                stat = ToriRS_TaskQueue_RunTask(runner->queue, runner->io, task);
                dt = TaskRunner_Now(runner) - t0;
                /* What the step computed: its thread's processor time where
                 * the runner has that clock, else the wall time it took. */
                cost = runner->cost_now_us ? runner->cost_now_us() - c0 : dt;
                if( runner->slow_task_us && dt >= runner->slow_task_us )
                    fprintf(stderr, "task_slow: %s %.2f ms\n", name[0] ? name : "(unnamed)", (double)dt / 1000.0);
                if( dt > runner->longest_step_us )
                {
                    runner->longest_step_us = dt;
                    memcpy(runner->longest_step_name, name, sizeof(runner->longest_step_name));
                }
                if( runner->role == TASK_RUNNER_ROLE_PARALLEL && runner->compute_step_us && ordinary &&
                    !runner->compute_check_off && cost >= runner->compute_step_us )
                {
                    int seen = 0;
                    for( int i = 0; i < runner->compute_named_count; i++ )
                        if( strcmp(runner->compute_named[i], name) == 0 )
                            seen = 1;
                    if( !seen )
                    {
                        char line[256];
                        if( runner->compute_named_count < (int)(sizeof(runner->compute_named) / sizeof(runner->compute_named[0])) )
                            memcpy(runner->compute_named[runner->compute_named_count++], name, sizeof(name));
                        snprintf(
                            line,
                            sizeof(line),
                            "parallel task %s computed %.2f ms in one step (%s; %.2f ms wall; threshold "
                            "%.2f ms): CPU work belongs on the background queue",
                            name[0] ? name : "(unnamed)",
                            (double)cost / 1000.0,
                            runner->cost_now_us ? "thread CPU time" : "wall time",
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
            else if( stat == TORIRS_ASYNCIO_STAT_SPENT )
            {
                /* It ran, and stopped because the slice is over. */
                ran = 1;
                spent = 1;
            }
            else
                ran = 1; /* a yield, or a park: it ran up to it */

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
    {
        /* SPENT is the background queue's alone; the main queue's deadline
         * is a pace. The parallel queue is never given one (StepUntil). */
        assert(runner->role != TASK_RUNNER_ROLE_PARALLEL);
        return runner->role == TASK_RUNNER_ROLE_MAIN ? TASK_RUNNER_PACED : TASK_RUNNER_SPENT;
    }
    if( ran )
        return TASK_RUNNER_PROGRESSED;
    /* A non-empty queue in which nothing ran: every task the walk visited
     * was parked (the walk visits at least the head). */
    assert(waiting);
    (void)waiting;
    return TASK_RUNNER_WAITING;
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
    if( stat == TASK_RUNNER_RENDER )
        return "render";
    if( stat == TASK_RUNNER_PACED )
        return "paced";
    return "spent";
}

/* Is the head of this (FIFO) queue parked on a trigger, a count or a
 * cycle -- waiting on another queue's work, not on its own read? The packet
 * pump drains the parallel queue for such a head (app_net.c). */
static inline int
TaskRunner_HeadParked(struct TaskRunner const* runner)
{
    assert(runner);
    assert(runner->queue);
    return runner->queue->head && runner->queue->head->parked;
}

/*
 * WHAT IS PARKED ON WHAT. One line per task on this queue that waits --
 * on its read, or parked -- with its chain down to the read, the count, the
 * cycle or the running owner it bottoms out in (ToriRS_TaskChainFormat).
 * Returns how many it printed.
 */
static inline int
TaskRunner_PrintParked(
    struct TaskRunner const* runner,
    FILE* out)
{
    struct ToriRS_Task const* task;
    int n = 0;

    assert(runner);
    assert(runner->queue);
    assert(out);
    for( task = runner->queue->head; task; task = task->next )
    {
        char chain[512];

        if( !task->parked && !task->io.pending )
            continue;
        (void)ToriRS_TaskChainFormat(task, chain, sizeof(chain));
        fprintf(out, "queue_contract: parked %s: %s\n", TaskRunner_RoleName(runner->role), chain);
        n++;
    }
    return n;
}

/*
 * End a pump and check the pump guarantee on what it left. `why` names what
 * ended it (a TaskRunner_StatName, or the owner's word). Returns how many
 * tasks were left in breach.
 *
 * On the parallel and the background queue no task that can run may be left
 * without a turn this pump; on the parallel queue every task left must also
 * be DONE OR PARKED: waiting on its read (its item pending), or parked on a
 * trigger, a count or a cycle. Under TORIRS_QUEUE_CONTRACT_TRACE the parked
 * tasks are named with their kinds (2: each with its whole chain); under
 * TORIRS_QUEUE_CONTRACT_STRICT a task parked on a chain that cannot end --
 * a trigger nobody primed, a count already at zero -- aborts with the chain.
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
    int parked = 0;
    char names[256];
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
        if( task->parked )
        {
            if( TaskRunner_ContractStrict() )
            {
                char chain[512];
                int end = ToriRS_TaskChainFormat(task, chain, sizeof(chain));
                if( end == TORIRS_CHAIN_UNSOUND )
                {
                    char line[640];
                    snprintf(line, sizeof(line), "a parked task can never be woken: %s", chain);
                    TaskRunner_ContractBreach(line);
                }
            }
            parked++;
            if( names_len < (int)sizeof(names) - 48 )
                names_len += snprintf(
                    names + names_len,
                    sizeof(names) - (size_t)names_len,
                    " %s%s[%s]",
                    task->name[0] ? task->name : "(unnamed)",
                    task->render_blocking ? "(script)" : "",
                    ToriRS_TaskWaitName(task->park.kind));
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
        {
            char line[160];
            snprintf(
                line,
                sizeof(line),
                "parallel pump %u (%s) ended with %s still able to run (neither waiting on a read "
                "nor parked)",
                (unsigned)runner->pump,
                why ? why : "?",
                task->name[0] ? task->name : "(unnamed)");
            TaskRunner_ContractBreach(line);
            assert(task->parked);
            breaches++;
        }
    }
    if( TaskRunner_ContractTrace() >= 2 || (TaskRunner_ContractTrace() >= 1 && (parked > 0 || breaches > 0)) )
    {
        fprintf(
            stderr,
            "queue_contract: trace %s pump %u ended %s: %d waiting on a read, %d parked%s%s\n",
            TaskRunner_RoleName(runner->role),
            (unsigned)runner->pump,
            why ? why : "?",
            pending,
            parked,
            parked ? ":" : "",
            names);
        if( TaskRunner_ContractTrace() >= 2 )
            (void)TaskRunner_PrintParked(runner, stderr);
    }
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
    /* The parallel queue is drained, never given a deadline (app.h). */
    if( deadline_us != 0 && runner->role == TASK_RUNNER_ROLE_PARALLEL )
    {
        TaskRunner_ContractBreach("the parallel queue was given a deadline (it is drained, never sliced)");
        assert(runner->role != TASK_RUNNER_ROLE_PARALLEL);
    }
    lone = deadline_us != 0 && !runner->in_pump;
    if( lone )
        TaskRunner_PumpBegin(runner);
    runner->io->slice.deadline_us = deadline_us;
    runner->io->slice.spent = 0;
    runner->io->slice.grace = 0;
    /* THE ONE PLACE a slice is made a slice a task may stop for: only on
     * the background queue (and a role-less test runner). Anywhere else
     * ToriRS_SliceSpent answers "not over" whatever the deadline. */
    runner->io->slice.sliced =
        runner->role == TASK_RUNNER_ROLE_BACKGROUND || runner->role == TASK_RUNNER_ROLE_NONE;
    stat = TaskRunner_Step(runner);
    runner->io->slice.deadline_us = 0;
    runner->io->slice.spent = 0;
    runner->io->slice.grace = 0;
    runner->io->slice.sliced = 0;
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
 * The background queue's pump (a role-less test runner's too); never the main
 * or the parallel queue's.
 *
 * Stops on IDLE, SPENT and WAITING (every task left is parked: the answers
 * come between turns, or another queue has to move); keeps going on
 * PROGRESSED while time
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
    assert(runner->role == TASK_RUNNER_ROLE_BACKGROUND || runner->role == TASK_RUNNER_ROLE_NONE);
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
    /* The slice is up, but a task a wake readied after the walk had passed
     * it -- a parent whose last sibling ended behind it -- has not had its
     * turn this pump: it gets one, whatever the clock says (guarantee 1). A
     * timed pass runs only the tasks still owed theirs. */
    if( stat == TASK_RUNNER_SPENT )
    {
        while( TaskRunner_HasReadyWithoutTurn(runner) )
        {
            enum TaskRunnerStat more = TaskRunner_StepUntil(runner, deadline_us);
            if( more == TASK_RUNNER_IDLE || more == TASK_RUNNER_WAITING )
            {
                stat = more;
                break;
            }
        }
    }
    (void)TaskRunner_PumpEnd(runner, TaskRunner_StatName(stat));
    return stat;
}

/*
 * ONE PUMP of a drained queue -- the parallel queue's, every frame: every
 * task that can run gets a turn, and passes go on until no task is ready
 * (every task done, or parked on its read, a trigger, a count or a cycle).
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
 * Returns with work still queued once every task left is parked on a
 * trigger, a count or a cycle and none has a read out: a single-queue drain
 * cannot move what only another queue (or a clock) fires, and stepping again
 * would run nothing. Callers that must settle such a task step both queues
 * instead -- see App_BootWait.
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
    } while( stat != TASK_RUNNER_IDLE && !(stat == TASK_RUNNER_WAITING && TaskRunner_ReadsOutstanding(runner) == 0) );
}

/** Settle the frame's transaction before a frame is published.
 *
 * A cooperative task may yield arbitrarily often; a yield is not a frame
 * boundary. So this steps while passes make progress, and stops for exactly
 * the reasons a pass cannot continue: the transaction is done (IDLE), every
 * task left is parked (WAITING: on a read, or on another queue's work), or a
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
 * returns TASK_RUNNER_PACED on the main queue (TASK_RUNNER_SPENT on a
 * role-less test runner), which every caller of a settle already treats as
 * "not settled, resume on a later turn" -- the answer a parked read gives.
 * The clock is read between tasks, never inside one -- a task on the main
 * queue that asks whether the slice is over is told no (ToriRS_Slice::
 * sliced) -- and a pass always runs at least one task, so a settle given an
 * expired deadline still moves.
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
            stat = runner->role == TASK_RUNNER_ROLE_MAIN ? TASK_RUNNER_PACED : TASK_RUNNER_SPENT;
            break;
        }
    }
    runner->progressed = progressed;
    return stat;
}

#endif
