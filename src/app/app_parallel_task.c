/*
 * The client's half of a parallel task (app/app_parallel_task.h): the pending
 * index on struct App, adding a task to the parallel queue, its release, and
 * the checks its body makes.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

void
App_PendingInit(struct App* app)
{
    assert(app);
    assert(app->assets);
    assert(!app->pending);
    app->pending = RS_Pending_New();
}

void
App_PendingFree(struct App* app)
{
    assert(app);
    RS_Pending_Free(app->pending);
    app->pending = NULL;
}

uint32_t
App_ParallelCycle(struct App const* app)
{
    assert(app);
    return (uint32_t)FrameClockSerial_LogicCycle(&app->frame_clock.serial);
}

void
App_ParallelTaskAdd(
    struct App* app,
    struct ToriRS_Task* task,
    struct RS_ParallelTask* par,
    int kind,
    struct RS_PendingKey key,
    int view)
{
    uint32_t scene_serial = 0;

    assert(app);
    assert(app->pending);
    if( view >= 0 && WorldviewRegistry_IsLive(&app->worldviews, view) )
    {
        struct World const* world = WorldviewRegistry_Get(&app->worldviews, view)->world;
        if( world )
            scene_serial = world->load_seq;
    }
    RS_ParallelTask_Add(
        app->frame_runner.queue,
        app->pending,
        task,
        par,
        kind,
        key,
        view,
        scene_serial,
        App_ParallelCycle(app));
}

void
App_ParallelTaskRelease(
    struct App* app,
    struct RS_ParallelTask* par)
{
    assert(app);
    RS_ParallelTask_Release(app->pending, app->assets, par);
}

struct World*
App_ParallelTaskWorld(
    struct App* app,
    struct RS_ParallelTask const* par,
    char const* owner)
{
    assert(app);
    assert(par);
    assert(par->view >= 0);
    return app_deferred_land_world(app, par->view, par->scene_serial, owner, par->kind);
}

uint32_t
App_ParallelTaskLate(
    struct App const* app,
    struct RS_ParallelTask const* par)
{
    return RS_ParallelTask_LateCycles(par, App_ParallelCycle(app));
}
