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
 * frame, until nothing more can progress (App_PumpAsync), inside a client
 * script's settle (app_settle_cs2_frame), and from the packet pump while the
 * packet queue is blocked on it (app_pump_net_packets). A parallel task must
 * be safe to resume at any of those points, as the placeholder and loc-change
 * tasks are.
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

/** Make app->pending (after app->assets). */
void
App_PendingInit(struct App* app);

/** After the queues are freed: every parallel task has unlisted itself. */
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

/* ---- the named waits, the client's half (rs_parallel_task.h has them) ----- */

/* A client cycle is 20 ms (the logic tick, 50 a second). */
#define APP_PARALLEL_CYCLE_MS 20u

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

/** Is a rebuild of `view` listed (under way)? */
bool
App_ViewRebuilding(
    struct App* app,
    int view);

/**
 * (c) the rebuild of this task's view: wait until none is listed and `built`
 * holds (the view has a built scene, or is gone), or the task is cancelled.
 * PT_PARALLEL_AWAIT_REBUILD on the client's index and the task's view.
 */
#define PT_APP_AWAIT_VIEW_REBUILT(app, built)                                                      \
    PT_PARALLEL_AWAIT_REBUILD((app)->pending, App_ViewRebuildKey((app), self->par.view), (built))

#endif /* SRC_APP_APP_PARALLEL_TASK_H */
