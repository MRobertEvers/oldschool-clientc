#ifndef SRC_TASK_RUNNER_H
#define SRC_TASK_RUNNER_H

#include "asyncio.h"
#include "platform/platform_x_io.h"

#include <assert.h>

/*
 * Single owner of the "drive the task queue" idiom. Everything that needs
 * async work done goes through one of these two calls instead of hand-rolling
 * a Run/Process loop.
 *
 * Native: PlatformX_IO_Process satisfies IO synchronously, so each Step makes
 * forward progress and Drain terminates within the call.
 * WASM (future shell): Process only initiates fetches; Step returns PENDING
 * and the browser loop resumes us next frame — Drain must not be used there.
 */

struct TaskRunner
{
    struct ToriRS_TaskQueue* queue;
    struct ToriRS_IO* io;
    struct PlatformX_IO* px;
    /* A CS2 task has joined this queue and the tree/display list must not be
     * published until its whole host follow-up fixed point has settled. */
    int frame_settle_pending;
};

enum TaskRunnerStat
{
    TASK_RUNNER_IDLE = 0,
    TASK_RUNNER_PENDING,
};

/** One scheduler pass: run the queue until it yields for IO, then hand the IO
 * list to the platform. */
static inline enum TaskRunnerStat
TaskRunner_Step(struct TaskRunner* runner)
{
    assert(runner && runner->queue && runner->io && runner->px);
    /* A read the platform has not answered yet: the head task is parked right
     * after its PT_YIELD and running it would resume it over an empty slot.
     * Always false on a synchronous backend, so native behaviour is unchanged. */
    if( PlatformX_IO_Pending(runner->px, runner->io) )
        return TASK_RUNNER_PENDING;
    if( ToriRS_TaskQueue_Run(runner->queue, runner->io) == TORIRS_ASYNCIO_STAT_YIELD )
    {
        PlatformX_IO_Process(runner->px, runner->io);
        return TASK_RUNNER_PENDING;
    }
    return TASK_RUNNER_IDLE;
}

/** Blocking drain — native and tests only. */
static inline void
TaskRunner_Drain(struct TaskRunner* runner)
{
    while( TaskRunner_Step(runner) == TASK_RUNNER_PENDING )
    {
    }
}

/** Settle every task that can make progress before a frame is published.
 *
 * A cooperative task is allowed to yield arbitrarily often; a yield is not a
 * frame boundary.  Keep stepping until the queue is empty.  The sole reason
 * to return PENDING is a real platform request which has not completed yet.
 * That distinction matters on both hosts: browser reads are asynchronous, and
 * a native JS5-backed cache miss can be asynchronous too.  Callers must retain
 * the last settled frame while this returns PENDING and resume on the next
 * host turn. */
static inline enum TaskRunnerStat
TaskRunner_SettleFrame(struct TaskRunner* runner)
{
    enum TaskRunnerStat stat;

    do
    {
        stat = TaskRunner_Step(runner);
    } while( stat == TASK_RUNNER_PENDING &&
             !PlatformX_IO_Pending(runner->px, runner->io) );

    return stat;
}

#endif
