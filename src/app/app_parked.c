/*
 * The App's parked lookup (game/rs_parked.h): app->parked, the one
 * registration list of its kinds, and THE LANDING POINT.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_parked.h (included by app/app_internal.h), and nothing outside the
 * layer may include either.
 *
 * What a family parks, how it lands and what its policy is live in the
 * family's own unit (app_parked_<family>.c). This unit only makes the lookup,
 * calls each family's register function once, and lands arrivals at one
 * point in the frame:
 *
 *   - after the parallel queue has been pumped, so every load that could
 *     finish this frame has;
 *   - never inside a packet task (a packet parked mid-task on a read holds
 *     the landings back: a packet task never sees the world change under it)
 *     and never inside the script settle (app_frame.c calls this after its
 *     early-out for a settle still in progress);
 *   - in parked arrival order, at most APP_PARKED_LAND_BUDGET a frame;
 *   - and if a landing queued a client script, the settle runs once more
 *     here, before the frame can be published.
 */

#include "app/app_internal.h"
#include "game/rs_parked.h"

static uint32_t
app_parked_clock(void* data)
{
    return App_ParkedCycle((struct App const*)data);
}

uint32_t
App_ParkedCycle(struct App const* app)
{
    assert(app);
    return (uint32_t)FrameClockSerial_LogicCycle(&app->frame_clock.serial);
}

void
App_ParkedRegisterAll(struct App* app)
{
    assert(app);
    assert(app->parked);
    /* One line per family, each registering its own kinds from its own unit
     * and its own id range (game/rs_parked.h). */
    App_ParkedGroundRegister(app);
    App_ParkedEffectsRegister(app);
    App_ParkedLocsRegister(app);
    App_ParkedEntitiesRegister(app);
    App_ParkedIfAssetsRegister(app);
    App_ParkedMountsRegister(app);
    App_ParkedScriptsRegister(app);
}

void
App_ParkedInit(struct App* app)
{
    assert(app);
    assert(app->assets);
    assert(!app->parked);
    app->parked = Park_New(app->assets, app_parked_clock, app);
    App_ParkedRegisterAll(app);
}

void
App_ParkedFree(struct App* app)
{
    assert(app);
    Park_Free(app->parked);
    app->parked = NULL;
}

int
App_ParkedLandingPoint(struct App* app)
{
    int landed;
    enum TaskRunnerStat stat;

    assert(app);
    assert(app->parked);
    assert(app->assets);
    /* A packet task parked on a read is mid-application: nothing lands under
     * it. Its arrivals stay where they are (in the assets table's arrived
     * list, or ready in the lookup) until a frame where the queue is idle. */
    if( app->packet_runner.queue->head )
        return 1;
    (void)Assets_LandFor(app->assets, Park_AssetLandFn(), app->parked);
    landed = Park_Land(app->parked, APP_PARKED_LAND_BUDGET);
    if( landed == 0 )
        return 1;
    /* Past the budget: the next frame should come at once, not after the
     * frame cap, to land the rest. */
    if( Park_ReadyCount(app->parked) > 0 )
        BootPumpStats_SetPending(&app->pump_stats);
    if( !app->frame_runner.frame_settle_pending )
        return 1;
    /* A landing queued a client script: the settle runs once more, here,
     * before anything can publish the frame. */
    stat = app_settle_cs2_frame(app);
    if( stat != TASK_RUNNER_IDLE )
    {
        TaskRunner_SetHadWork(&app->frame_runner, 1);
        return 0;
    }
    UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    return 1;
}
