#ifndef SRC_GAME_RS_PARALLEL_TASK_H
#define SRC_GAME_RS_PARALLEL_TASK_H

/*
 * THE SHAPE OF A PARALLEL TASK: work a packet handler hands to the parallel
 * queue because it waits on something that must not hold the packet pump.
 *
 * The handler makes ONE task carrying its own data, copied from the packet
 * (the resolved tile, ids, labels, the view, the scene serial, the arrival
 * cycle), adds it to the parallel queue as an ordinary (not render-blocking)
 * task, and returns. To see what happens next, read the task's run function,
 * which reads top to bottom:
 *
 *   1. wait for what it needs: PT_PARALLEL_AWAIT_ASSET (again for each link
 *      of a chain that is only discovered step by step);
 *   2. if it was cancelled or superseded while it waited, exit;
 *   3. check the game still wants it (the scene serial, the target and its
 *      generation, how late it is); exit if not;
 *   4. apply, reading the asset from the cache at that moment.
 *
 * A policy (too late, failed to load, replaced) is an `if` in that body. No
 * `switch` between PT_BEGIN and PT_END. Where two tasks for one key must BOTH
 * apply, in order, the later one waits its turn (PT_PARALLEL_AWAIT_TURN)
 * before it applies.
 *
 * The task embeds, after its `struct ToriRS_Task task` and `struct pt pt`,
 * the common header as a member named `par`:
 *
 *     struct Task_Example
 *     {
 *         struct ToriRS_Task task;
 *         struct pt pt;
 *         struct RS_ParallelTask par;
 *         ...its own data...
 *     };
 *
 * The macros below read `self->task`, `self->pt` and `self->par`, as
 * PT_TASK_JOIN does. The header is listed in the pending index (game/
 * rs_pending.h) from when the task is added (RS_ParallelTask_Add) to when it
 * is freed (RS_ParallelTask_Release, the one call its free makes), so a later
 * packet, a rebuild, a logout or a test can find it and cancel it.
 *
 * App-free: the client's half (which queue, which index, which clock, "is my
 * scene still live") is app/app_parallel_task.h. The worked example is
 * game/test/rs_parallel_task_test.c.
 */

#include "asyncio.h"
#include "engine/asset_requests.h"
#include "game/rs_pending.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * List `par` in `index` (see RS_Pending_List) and add `task` to `queue`, the
 * parallel queue, as an ordinary task. `task` is the task `par` is embedded
 * in, made and initialised (vtable, PT_INIT) and on no queue.
 */
static inline void
RS_ParallelTask_Add(
    struct ToriRS_TaskQueue* queue,
    struct RS_Pending* index,
    struct ToriRS_Task* task,
    struct RS_ParallelTask* par,
    int kind,
    struct RS_PendingKey key,
    int view,
    uint32_t scene_serial,
    uint32_t arrived_cycle)
{
    assert(queue);
    assert(index);
    assert(task);
    assert(task->vtable);
    assert(!task->render_blocking);
    assert(par);
    RS_Pending_List(index, par, kind, key, view, scene_serial, arrived_cycle);
    ToriRS_TaskQueue_Add(queue, task);
}

/**
 * The task is being freed: stop counting any load it still waits on, and
 * take it out of the index. The one call a parallel task's free makes,
 * however the task ended (applied, exited, or freed with its queue).
 */
static inline void
RS_ParallelTask_Release(
    struct RS_Pending* index,
    struct Assets* assets,
    struct RS_ParallelTask* par)
{
    assert(index);
    assert(assets);
    assert(par);
    if( par->join > 0 )
        Assets_Unjoin(assets, &par->join);
    RS_Pending_Unlist(index, par);
}

/** Was it cancelled (a newer task replaced it, or its scope was cancelled)? */
static inline bool
RS_ParallelTask_Cancelled(struct RS_ParallelTask const* par)
{
    assert(par);
    return par->cancelled != 0;
}

/** How many cycles after its packet arrived `now_cycle` is. */
static inline uint32_t
RS_ParallelTask_LateCycles(
    struct RS_ParallelTask const* par,
    uint32_t now_cycle)
{
    assert(par);
    return now_cycle - par->arrived_cycle;
}

/** Did the last awaited asset arrive? (FAILED: it failed; DROPPED: its load
 *  was abandoned or replaced, or the wait was cut short by a cancel.) */
static inline bool
RS_ParallelTask_AssetHere(struct RS_ParallelTask const* par)
{
    assert(par);
    return par->outcome == ASSET_OUTCOME_LANDED;
}

/**
 * Wait for asset `key` in table `assets`: Assets_AwaitOutcome on the header's
 * join, then PT_TASK_JOIN's wait, which also ends at once if the task is
 * cancelled meanwhile (the wait is then dropped from the table). Afterwards
 * `self->par.outcome` says how it ended (RS_ParallelTask_AssetHere). Usable
 * any number of times in one body, for a chain. A suspension point: see
 * PT_TASK_AWAITSELF for what that costs (hold stable ids across it, never a
 * derived index or a pointer into the cache).
 */
#define PT_PARALLEL_AWAIT_ASSET(assets, key)                                                       \
    do                                                                                             \
    {                                                                                              \
        assert(self->par.join == 0);                                                               \
        (void)Assets_AwaitOutcome((assets), (key), &self->par.join, &self->par.outcome);           \
        while( self->par.join > 0 && !self->par.cancelled )                                        \
        {                                                                                          \
            self->task.blocked = 1;                                                                \
            PT_YIELD(&self->pt);                                                                   \
        }                                                                                          \
        if( self->par.join > 0 )                                                                   \
        {                                                                                          \
            Assets_Unjoin((assets), &self->par.join);                                              \
            self->par.outcome = ASSET_OUTCOME_DROPPED;                                             \
        }                                                                                          \
    } while( 0 )

/**
 * Wait until no task of this task's kind for its key, added before it, is
 * still pending in `index` (or this task is cancelled). Cancelled earlier
 * tasks do not count: they will not apply. A suspension point.
 */
#define PT_PARALLEL_AWAIT_TURN(index)                                                              \
    TASK_AWAIT_STATE(                                                                              \
        &self->task, &self->pt, self->par.cancelled || !RS_Pending_EarlierPending((index), &self->par))

#endif /* SRC_GAME_RS_PARALLEL_TASK_H */
