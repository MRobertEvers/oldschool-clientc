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

struct TaskRunner
{
    struct ToriRS_TaskQueue* queue;
    struct ToriRS_IO* io;
    Platform_IO* px;
    /* A CS2 task has joined this queue and the tree/display list must not be
     * published until its whole host follow-up fixed point has settled. */
    int frame_settle_pending;
    /* What the head task asked to be drawn, valid only while the last Step
     * returned TASK_RUNNER_RENDER. */
    struct ToriRS_RenderRequest render;
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

/** One scheduler pass: run the queue until it yields for IO, then hand the IO
 * list to the platform. */
static inline enum TaskRunnerStat
TaskRunner_Step(struct TaskRunner* runner)
{
    int stat;

    assert(runner && runner->queue && runner->io && runner->px);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_TASK_STEPS, 1);
    /* A read the platform has not answered yet: the head task is parked right
     * after its PT_YIELD and running it would resume it over an empty slot.
     * Always false on a synchronous backend, so native behaviour is unchanged. */
    {
        int pending = 0;
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_IO)
        {
            pending = Platform_IO_Pending(runner->px, runner->io);
        }
        if( pending )
            return TASK_RUNNER_PENDING;
    }
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_QUEUE_RUN)
    {
        stat = ToriRS_TaskQueue_Run(runner->queue, runner->io);
    }
    if( stat == TORIRS_ASYNCIO_STAT_BLOCKED )
    {
        /* A blocked yield requests nothing, but an earlier task in this same
         * pass may have left items queued; draining them here keeps the IO
         * list's lifetime identical on both exits. */
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_IO)
        {
            Platform_IO_Process(runner->px, runner->io);
        }
        return TASK_RUNNER_BLOCKED;
    }
    if( stat == TORIRS_ASYNCIO_STAT_RENDER )
    {
        /* The head is still queued and still parked on its yield, so its
         * request is intact; copy it out before anything else touches the
         * queue. Any IO an earlier task in this pass left queued is drained
         * here, exactly as the other two exits do. */
        assert(runner->queue->head);
        runner->render = runner->queue->head->render;
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_IO)
        {
            Platform_IO_Process(runner->px, runner->io);
        }
        return TASK_RUNNER_RENDER;
    }
    if( stat == TORIRS_ASYNCIO_STAT_YIELD )
    {
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TASK_IO)
        {
            Platform_IO_Process(runner->px, runner->io);
        }
        return TASK_RUNNER_PENDING;
    }
    return TASK_RUNNER_IDLE;
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
