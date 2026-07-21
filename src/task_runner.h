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

#endif
