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
 * WHAT A PARALLEL TASK MAY WAIT ON is closed, and these are the ways. Each
 * PARKS the task (task_trigger.h): it is not stepped again until what it
 * parked on wakes it -- nothing is polled.
 *
 *   (a) an asset            PT_PARALLEL_AWAIT_ASSET, PT_PARALLEL_AWAIT_ASSETS:
 *                           parked on the header's join, which the assets
 *                           table counts the loads on
 *   (b) tasks it fanned out PT_PARALLEL_AWAIT_JOIN (PT_TASK_JOIN when it
 *                           cannot be cancelled): parked on the join count
 *   (c) another task        PT_PARALLEL_AWAIT_TURN: parked on the `ended`
 *       ending              trigger of the earlier task for the same key;
 *                           PT_PARALLEL_AWAIT_REBUILD: parked on the view's
 *                           built trigger, which the rebuild primes
 *   (d) a client cycle      PT_PARALLEL_AWAIT_CYCLE ("not before cycle N"):
 *                           parked on the clock
 *
 * A cancel through the pending index wakes a parked task (RS_Pending_Cancel),
 * so every one of them also ends when the task is cancelled, and step 2
 * above follows each.
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
    /* So a cancel can wake it, and a later task for its key wait for its
     * end (PT_PARALLEL_AWAIT_TURN). */
    par->task = task;
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

/** Has cycle `now` reached `due`? A signed difference, so it holds across
 *  the wrap of a 32-bit cycle count. */
static inline bool
RS_ParallelTask_CycleReached(
    uint32_t now,
    uint32_t due)
{
    return ToriRS_CycleReached(now, due) != 0;
}

/*
 * The turn's one step: is a task of `par`'s kind for its key, listed before
 * it and not cancelled, still pending in `index`? Then ask to park `task` on
 * that task's end and return 1 (the caller yields); 0 when it is its turn.
 */
static inline int
RS_ParallelTask_AskTurn(
    struct RS_Pending* index,
    struct RS_ParallelTask* par,
    struct ToriRS_Task* task)
{
    struct RS_ParallelTask* earlier;

    assert(index);
    assert(par);
    assert(task);
    earlier = RS_Pending_EarlierTask(index, par);
    if( !earlier )
        return 0;
    /* Listed by RS_ParallelTask_Add, so it is a task, queued (its end is
     * primed) until it is freed, which unlists it. */
    assert(earlier->task);
    RS_Pending_NoteTurnWait(index);
    ToriRS_TaskAskTrigger(task, TORIRS_TASK_WAIT_TURN, ToriRS_TaskEnded(earlier->task));
    return 1;
}

/*
 * The rebuild's one step: must the task wait for its view's scene? It must
 * while `built_trigger` is primed (a rebuild of the view is under way, or
 * the view awaits its first one) or while the view is not `built`. Then ask
 * to park on it and return 1. A view that is not built with nothing primed
 * to build it is a wait nothing would end: ToriRS_TaskAskTrigger aborts,
 * naming both. `built_trigger` NULL: the view is gone, and `built` says so.
 */
static inline int
RS_ParallelTask_AskRebuild(
    struct ToriRS_Task* task,
    struct ToriRS_Trigger* built_trigger,
    bool built)
{
    assert(task);
    if( !built_trigger )
    {
        assert(built);
        return 0;
    }
    if( built && !ToriRS_Trigger_Primed(built_trigger) )
        return 0;
    ToriRS_TaskAskTrigger(task, TORIRS_TASK_WAIT_REBUILD, built_trigger);
    return 1;
}

/** Did the last awaited asset arrive? (FAILED: it failed; DROPPED: its load
 *  was abandoned or replaced, or the wait was cut short by a cancel.) */
static inline bool
RS_ParallelTask_AssetHere(struct RS_ParallelTask const* par)
{
    assert(par);
    return par->outcome == ASSET_OUTCOME_LANDED;
}

/*
 * Park on the header's join until it is 0 or the task is cancelled (a cancel
 * wakes it). The body of (a) and (b).
 */
#define RS_PARALLEL_PARK_COUNT(kind, count_ptr)                                                    \
    do                                                                                             \
    {                                                                                              \
        while( *(count_ptr) > 0 && !self->par.cancelled )                                          \
        {                                                                                          \
            ToriRS_TaskAskCount(&self->task, (kind), (count_ptr), 0);                              \
            PT_YIELD(&self->pt);                                                                   \
        }                                                                                          \
    } while( 0 )

/**
 * Wait for asset `key` in table `assets`: Assets_AwaitOutcome on the header's
 * join, then park on that join until the load ends -- its end takes the join
 * to 0 -- or the task is cancelled (the wait is then dropped from the
 * table). Afterwards `self->par.outcome` says how it ended
 * (RS_ParallelTask_AssetHere). Usable any number of times in one body, for a
 * chain. A suspension point: see PT_TASK_AWAITSELF for what that costs (hold
 * stable ids across it, never a derived index or a pointer into the cache).
 */
#define PT_PARALLEL_AWAIT_ASSET(assets, key)                                                       \
    do                                                                                             \
    {                                                                                              \
        assert(self->par.join == 0);                                                               \
        (void)Assets_AwaitOutcome((assets), (key), &self->par.join, &self->par.outcome);           \
        RS_PARALLEL_PARK_COUNT(TORIRS_TASK_WAIT_ASSET, &self->par.join);                           \
        if( self->par.join > 0 )                                                                   \
        {                                                                                          \
            Assets_Unjoin((assets), &self->par.join);                                              \
            self->par.outcome = ASSET_OUTCOME_DROPPED;                                             \
        }                                                                                          \
    } while( 0 )

/**
 * (a), many at once: wait for every load the task counted on its header's
 * join (several Assets_Await on `&self->par.join`, a fan-out of independent
 * records with one consumer), or for a cancel, which drops the waits. No
 * outcome: the body asks the cache what arrived. A suspension point.
 */
#define PT_PARALLEL_AWAIT_ASSETS(assets)                                                           \
    do                                                                                             \
    {                                                                                              \
        RS_PARALLEL_PARK_COUNT(TORIRS_TASK_WAIT_ASSET, &self->par.join);                           \
        if( self->par.join > 0 )                                                                   \
            Assets_Unjoin((assets), &self->par.join);                                              \
    } while( 0 )

/**
 * (b) wait until every sibling counted on `self->join_field` (an int on the
 * task, filled by ToriRS_TaskQueue_AddParallelPoolSubTask on `queue`) has
 * ended, or the task is cancelled; on a cancel the siblings are detached
 * (they run on and end on their own, counting into nothing). A suspension
 * point.
 */
#define PT_PARALLEL_AWAIT_JOIN(queue, join_field)                                                  \
    do                                                                                             \
    {                                                                                              \
        RS_PARALLEL_PARK_COUNT(TORIRS_TASK_WAIT_JOIN, &self->join_field);                          \
        if( self->join_field > 0 )                                                                 \
            ToriRS_TaskQueue_DetachJoin((queue), &self->join_field);                               \
    } while( 0 )

/**
 * (c) wait until no task of this task's kind for its key, added before it,
 * is still pending in `index` (or this task is cancelled): parked on the
 * `ended` trigger of the latest earlier one, again after each wake, so the
 * tasks for one key apply in the order they were added. Cancelled earlier
 * tasks do not count: they will not apply. A suspension point.
 */
#define PT_PARALLEL_AWAIT_TURN(index)                                                              \
    do                                                                                             \
    {                                                                                              \
        while( !self->par.cancelled && RS_ParallelTask_AskTurn((index), &self->par, &self->task) ) \
            PT_YIELD(&self->pt);                                                                   \
    } while( 0 )

/**
 * (c) wait out the rebuild of this task's view: parked on the view's built
 * trigger (`built_trigger`, re-read on every resume; NULL for a view that is
 * gone) while it is primed -- a rebuild of the view is under way, or the
 * view awaits its first one -- or while `built` (the view has a built scene,
 * or is gone) does not hold; or until the task is cancelled. Both halves,
 * because a view can be waiting for a rebuild that has not been sent yet (a
 * world entity's deck, spawned before its REBUILD_WORLDENTITY): the view
 * itself primes its trigger until then. A suspension point.
 */
#define PT_PARALLEL_AWAIT_REBUILD(built_trigger, built)                                            \
    do                                                                                             \
    {                                                                                              \
        while( !self->par.cancelled && RS_ParallelTask_AskRebuild(&self->task, (built_trigger), (built)) ) \
            PT_YIELD(&self->pt);                                                                   \
    } while( 0 )

/**
 * (d) wait for a client cycle: not before cycle `due` on `clock` (compared
 * as a signed difference, so it holds across the wrap), parked on that
 * clock, which its owner advances in one place; or until the task is
 * cancelled. A suspension point.
 */
#define PT_PARALLEL_AWAIT_CYCLE(clock, due)                                                        \
    do                                                                                             \
    {                                                                                              \
        while( !self->par.cancelled && !ToriRS_CycleReached((clock)->now, (due)) )                 \
        {                                                                                          \
            ToriRS_TaskAskCycle(&self->task, TORIRS_TASK_WAIT_CYCLE, (clock), (due));              \
            PT_YIELD(&self->pt);                                                                   \
        }                                                                                          \
    } while( 0 )

#endif /* SRC_GAME_RS_PARALLEL_TASK_H */
