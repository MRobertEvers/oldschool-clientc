#ifndef SRC_APP_APP_PARALLEL_TASK_H
#define SRC_APP_APP_PARALLEL_TASK_H

/*
 * The client's half of a PARALLEL TASK (game/rs_parallel_task.h has the
 * shape): which queue it goes on, which index lists it, which clock it is
 * late by, and whether its scene is still live.
 *
 * THE QUEUE is the parallel queue that already exists, app->frame_runner. A
 * task is added there as an ordinary task, never render-blocking, so the
 * frame is not held for it; it runs whenever that queue is stepped: every
 * frame, until every task on it is done or parked (App_PumpAsync), inside a
 * client script's settle (app_settle_cs2_frame), and from the packet pump
 * while the packet queue's head is parked on its work (app_pump_net_packets).
 * A parallel task must be safe to resume at any of those points, as the
 * placeholder and loc-change tasks are.
 *
 * THE INDEX is app->pending (game/rs_pending.h). App_Logout cancels all of
 * it.
 *
 * A family's task, in its family's unit (app/app_tasks.h lists them):
 *
 *     static int
 *     Task_X_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
 *     {
 *         struct Task_X* self = (struct Task_X*)base;
 *         struct App* app = self->app;
 *
 *         PT_BEGIN(&self->pt);
 *         PT_PARALLEL_AWAIT_ASSET(app->assets, AssetKey_Make(ASSET_KIND_MODEL, self->model, 0));
 *         if( RS_ParallelTask_Cancelled(&self->par) )
 *             PT_EXIT(&self->pt);
 *         struct World* world = App_ParallelTaskWorld(app, &self->par, "x");
 *         if( !world || !RS_ParallelTask_AssetHere(&self->par) )
 *             PT_EXIT(&self->pt);
 *         ...apply, reading the model from the cache now...
 *         PT_END(&self->pt);
 *     }
 *
 *     static void
 *     Task_X_Free(struct ToriRS_Task* base)
 *     {
 *         struct Task_X* self = (struct Task_X*)base;
 *         App_ParallelTaskRelease(self->app, &self->par);
 *         ToriMem_Free(ToriMem_Process(), self);
 *     }
 *
 *     // the packet handler
 *     RS_Pending_CancelKey(app->pending, KIND_X, key);   // when newest wins
 *     App_ParallelTaskAdd(app, &task->task, &task->par, KIND_X, key, view);
 *
 * App layer only, included through app/app_internal.h.
 */

#include "game/rs_parallel_task.h"
#include "game/rs_pending.h"

#include <stdint.h>

struct App;
struct World;

/** Make app->pending (after app->assets), and name app->triggers. */
void
App_PendingInit(struct App* app);

/** After the queues are freed: every parallel task has unlisted itself, and
 *  every parked task has left the triggers it waited on. */
void
App_PendingFree(struct App* app);

/** The client cycle a parallel task arrives and is late by (the logic
 *  cycle). */
uint32_t
App_ParallelCycle(struct App const* app);

/**
 * Add a parallel task: list `par` in app->pending as `kind` for `key`, with
 * `view` (-1: none) and that view's scene serial (its World.load_seq now; 0
 * when the view has no world), arriving on this cycle, and add `task` to the
 * parallel queue as an ordinary task.
 */
void
App_ParallelTaskAdd(
    struct App* app,
    struct ToriRS_Task* task,
    struct RS_ParallelTask* par,
    int kind,
    struct RS_PendingKey key,
    int view);

/** The one call a parallel task's free makes (RS_ParallelTask_Release on the
 *  client's index and assets table). */
void
App_ParallelTaskRelease(
    struct App* app,
    struct RS_ParallelTask* par);

/**
 * Is my scene still live? The world of the task's view when it is the scene
 * the task was made in and it is fully built, NULL otherwise (the view died,
 * a rebuild replaced the scene, or a rebuild is in progress).
 * `owner` names the task in the log line a drop prints. The task has a view.
 */
struct World*
App_ParallelTaskWorld(
    struct App* app,
    struct RS_ParallelTask const* par,
    char const* owner);

/** How late am I: client cycles since the task's packet arrived. */
uint32_t
App_ParallelTaskLate(
    struct App const* app,
    struct RS_ParallelTask const* par);

/* ---- the waits, the client's half (rs_parallel_task.h has them) ----------- */

/* A client cycle is 20 ms (the logic tick, 50 a second). */
#define APP_PARALLEL_CYCLE_MS 20u

/** The logic cycle's clock: what PT_APP_AWAIT_CYCLE parks on. */
struct ToriRS_Clock*
App_CycleClock(struct App* app);

/** The logic tick began (app_logic_tick, the one place): advance the clock to
 *  App_ParallelCycle, waking every task due by now. */
void
App_CycleClockTick(struct App* app);

/** (d) not before logic cycle `due`. */
#define PT_APP_AWAIT_CYCLE(app, due) PT_PARALLEL_AWAIT_CYCLE(App_CycleClock(app), (due))

/** The cycles a pause of `ms` takes, at least one: what a wait that was
 *  stated in milliseconds (an assets back-off) waits as a client cycle. */
uint32_t
App_ParallelCyclesFor(uint32_t ms);

/**
 * The key a rebuild of `view` is listed under in app->pending
 * (RS_PENDING_KIND_REBUILD): the view and its generation. Shape
 * RS_PENDING_KEY_NONE -- which names no rebuild -- for a view that is not
 * live.
 */
struct RS_PendingKey
App_ViewRebuildKey(
    struct App* app,
    int view);

/** View `view`'s built trigger (struct AppTriggers), or NULL for a view that
 *  is not live. Primed while a rebuild of it is under way, or while it awaits
 *  its first scene. */
struct ToriRS_Trigger*
App_ViewBuiltTrigger(
    struct App* app,
    int view);

/** A view was spawned whose first scene the server has yet to send
 *  (App_WevSpawn): it primes its own built trigger until a rebuild of it
 *  begins or it is released. */
void
App_ViewAwaitsScene(
    struct App* app,
    int view);

/** View `view` is being released (App_WevDespawn): its built trigger's
 *  storage is about to serve another view, so it is retired (its waiters
 *  wake, and find the view gone). */
void
App_ViewReleased(
    struct App* app,
    int view);

/** The boot's built trigger (struct AppTriggers): primed while the client
 *  phase is BOOTING. */
struct ToriRS_Trigger*
App_BootBuilt(struct App* app);

/**
 * (c) the rebuild of this task's view: parked on the view's built trigger
 * while it is primed or `built` (the view has a built scene, or is gone) does
 * not hold; ends at once on a cancel. PT_PARALLEL_AWAIT_REBUILD on the
 * task's view.
 */
#define PT_APP_AWAIT_VIEW_REBUILT(app, built)                                                      \
    do                                                                                             \
    {                                                                                              \
        self->par.rebuild_parked = 0;                                                              \
        while( !self->par.cancelled &&                                                             \
               RS_ParallelTask_AskRebuild(&self->task, App_ViewBuiltTrigger((app), self->par.view), (built)) ) \
        {                                                                                          \
            if( !self->par.rebuild_parked )                                                        \
                App_ParallelTaskRebuildTrace((app), &self->par, "parks on");                       \
            self->par.rebuild_parked = 1;                                                          \
            PT_YIELD(&self->pt);                                                                   \
        }                                                                                          \
        if( self->par.rebuild_parked )                                                             \
            App_ParallelTaskRebuildTrace((app), &self->par, "woke from");                          \
    } while( 0 )

/*
 * TORIRS_QUEUE_CONTRACT_TRACE: one line when a parallel task parks on its
 * view's rebuild (`what` "parks on") and one when it is resumed past it
 * ("woke from"): the task, how the wait ended, and the scene serial it was
 * made in against the one the view holds now -- the evidence that a task
 * reaching its apply mid-rebuild waits for the commit and then re-checks
 * against the NEW scene. Silent otherwise.
 */
void
App_ParallelTaskRebuildTrace(
    struct App* app,
    struct RS_ParallelTask const* par,
    char const* what);

#endif /* SRC_APP_APP_PARALLEL_TASK_H */
