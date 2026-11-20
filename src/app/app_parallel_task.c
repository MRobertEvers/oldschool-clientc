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
    ToriRS_Clock_Init(&app->triggers.cycle, "the logic cycle", App_ParallelCycle(app));
    ToriRS_Trigger_Init(&app->triggers.boot_built, "the boot's tree");
    for( int i = 0; i < WORLDVIEW_MAX; i++ )
        ToriRS_Trigger_Init(&app->triggers.view_built[i], i == WORLDVIEW_ROOT ? "the root view's scene" : "a view's scene");
}

void
App_PendingFree(struct App* app)
{
    assert(app);
    RS_Pending_Free(app->pending);
    app->pending = NULL;
    /* The queues are gone, and every task parked on these left them as it
     * was freed: retiring wakes nothing. */
    ToriRS_Clock_Retire(&app->triggers.cycle);
    ToriRS_Trigger_Retire(&app->triggers.boot_built);
    for( int i = 0; i < WORLDVIEW_MAX; i++ )
    {
        ToriRS_Trigger_Retire(&app->triggers.view_built[i]);
        ToriRS_TriggerOwner_End(&app->triggers.view_awaits[i], 0);
    }
}

struct ToriRS_Clock*
App_CycleClock(struct App* app)
{
    assert(app);
    return &app->triggers.cycle;
}

void
App_CycleClockTick(struct App* app)
{
    assert(app);
    ToriRS_Clock_Advance(&app->triggers.cycle, App_ParallelCycle(app));
}

struct ToriRS_Trigger*
App_BootBuilt(struct App* app)
{
    assert(app);
    return &app->triggers.boot_built;
}

struct ToriRS_Trigger*
App_ViewBuiltTrigger(
    struct App* app,
    int view)
{
    assert(app);
    if( view < 0 || !WorldviewRegistry_IsLive(&app->worldviews, view) )
        return NULL;
    return &app->triggers.view_built[view];
}

void
App_ViewAwaitsScene(
    struct App* app,
    int view)
{
    struct ToriRS_TriggerOwner* awaits;

    assert(app);
    assert(view > WORLDVIEW_ROOT);
    assert(view < WORLDVIEW_MAX);
    awaits = &app->triggers.view_awaits[view];
    /* The view stands as the owner of its own first scene: it ends when the
     * view is released (App_ViewReleased) or a rebuild of it takes the
     * trigger over (App_ViewRebuildBegin). */
    ToriRS_TriggerOwner_Init(awaits, TORIRS_OWNER_EXTERNAL, "a spawned view awaiting its first REBUILD_WORLDENTITY");
    ToriRS_Trigger_Prime(&app->triggers.view_built[view], awaits);
}

void
App_ViewReleased(
    struct App* app,
    int view)
{
    assert(app);
    assert(view >= 0);
    assert(view < WORLDVIEW_MAX);
    ToriRS_Trigger_Retire(&app->triggers.view_built[view]);
    ToriRS_TriggerOwner_End(&app->triggers.view_awaits[view], 0);
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

uint32_t
App_ParallelCyclesFor(uint32_t ms)
{
    uint32_t const cycles = (ms + APP_PARALLEL_CYCLE_MS - 1u) / APP_PARALLEL_CYCLE_MS;

    return cycles > 0 ? cycles : 1u;
}

struct RS_PendingKey
App_ViewRebuildKey(
    struct App* app,
    int view)
{
    struct RS_PendingKey none = { 0 };

    assert(app);
    if( view < 0 || !WorldviewRegistry_IsLive(&app->worldviews, view) )
        return none;
    return RS_PendingKey_View(
        view, 0, 0, 0, 0, (uint32_t)WorldviewRegistry_Get(&app->worldviews, view)->wire_id);
}

void
App_ViewRebuildBegin(
    struct App* app,
    struct RS_ParallelTask* rebuild,
    int view,
    struct ToriRS_TriggerOwner* owner)
{
    uint32_t scene_serial = 0;
    struct World const* world;

    assert(app);
    assert(app->pending);
    assert(rebuild);
    assert(!rebuild->listed);
    assert(WorldviewRegistry_IsLive(&app->worldviews, view));
    world = WorldviewRegistry_Get(&app->worldviews, view)->world;
    if( world )
        scene_serial = world->load_seq;
    RS_Pending_List(
        app->pending,
        rebuild,
        RS_PENDING_KIND_REBUILD,
        App_ViewRebuildKey(app, view),
        view,
        scene_serial,
        App_ParallelCycle(app));
    /* What a task waiting out this view's rebuild parks on, from here to the
     * commit. A prime the view held for its first scene, or an older
     * rebuild's, is abandoned by this one (its waiters wait again). */
    ToriRS_Trigger_Prime(&app->triggers.view_built[view], owner);
}

void
App_ViewRebuildEnd(
    struct App* app,
    struct RS_ParallelTask* rebuild,
    int built)
{
    int view;

    assert(app);
    assert(app->pending);
    assert(rebuild);
    /* Every end of a rebuild comes here, the ones that never began included
     * (a packet task freed before its gather, a job cancelled twice). */
    if( !rebuild->listed )
        return;
    view = rebuild->view;
    RS_Pending_Unlist(app->pending, rebuild);
    /* Built: the waiters' scene is here. Not built: the owner's end abandons
     * the trigger (task_free, ToriRS_TaskJob_End), which is what the
     * waiters need -- they find the view as it is and go on or exit. */
    if( built && view >= 0 && view < WORLDVIEW_MAX )
        ToriRS_Trigger_FireIfPrimed(&app->triggers.view_built[view]);
}

/*
 * The world a deferred apply belongs to, or NULL when the scene it was aimed
 * at is gone (App_ParallelTaskWorld is this on a task's header; the effect
 * spawns in app_world_spawn.c call it directly). Three reasons, all guards
 * rather than asserts -- a task cannot be told the world moved under it:
 *
 *   - the VIEW died (a boat despawned);
 *   - a REBUILD replaced the world, and the same tile coordinates now name a
 *     different place (the reference drops its pending spot anims here too);
 *   - a rebuild is IN PROGRESS, and this queue is the one pumping it, so
 *     applying would put an element in a scene being reset.
 *
 * `owner` and `kind` only name the dropped task in the log.
 */
struct World*
app_deferred_land_world(
    struct App* app,
    int view,
    unsigned world_load_seq,
    char const* owner,
    int kind)
{
    struct Worldview* wv;

    assert(app);
    assert(owner);
    if( !WorldviewRegistry_IsLive(&app->worldviews, view) )
        return NULL;
    wv = WorldviewRegistry_Get(&app->worldviews, view);
    if( !wv->world )
        return NULL;
    if( wv->world->load_seq != world_load_seq )
    {
        TORIRS_LOG("%s: kind=%d dropped, scene rebuilt while its assets loaded\n", owner, kind);
        return NULL;
    }
    if( !wv->world->load_complete )
    {
        TORIRS_LOG("%s: kind=%d dropped, scene mid-rebuild\n", owner, kind);
        return NULL;
    }
    return wv->world;
}
