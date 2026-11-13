/*
 * The background world rebuild: a server REBUILD whose gather runs on the
 * background queue while the old scene stays on screen.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 *
 * THE SHAPE. The reference client does a rebuild by holding the server's
 * packets behind one flag while its loader works, with the previous scene
 * still drawn in the ordinary in-game state, and then building and committing
 * in one cycle. This is that, in the task system's terms:
 *
 *   1. The REBUILD packet task does its cheap bookkeeping and calls
 *      App_WorldJobStart instead of awaiting the load. It ends; no packet task
 *      is left parked for the job's duration.
 *   2. The gather (CreateTask_WorldGather) runs on bg_runner, fanned out
 *      there, in whatever time it is given (App_RunBackground).
 *   3. The packet pump pops nothing while the job is pending -- everything
 *      behind the REBUILD addresses the scene being built. Frames, input,
 *      audio, interfaces and the client's own sends go on.
 *   4. When the gather has ended, the pump commits before its next pop: the
 *      build the blocking flow does, then the entity shift and
 *      App_WorldLoadFinish in the blocking flow's order (MAP_BUILD_COMPLETE
 *      included). The hold lifts.
 *
 * The blocking flow (task_gameproto_exec.c awaiting Task_WorldLoad) is the
 * default and is untouched by any of this; see App::world_rebuild_background.
 */

#include "app/app_internal.h"
#include "game/content_test.h"
#include "mem/torirs_mem.h"

extern uint64_t
PlatformWindow_TicksUs(void);

/* The slice App_RunOnce always gives the background queue, so every platform
 * loop moves it with no change of its own. */
#define APP_BACKGROUND_FLOOR_US 2000u

#define APP_WORLD_JOB_DEADLINE_MS_DEFAULT 600

/* The scene side a root rebuild builds (the packet task passes the same). */
#define APP_WORLD_JOB_SCENE_SIZE 104

static uint64_t
app_bg_clock(void* ud)
{
    (void)ud;
    return PlatformWindow_TicksUs();
}

void
app_bg_runner_init(struct App* app)
{
    assert(app);
    assert(app->runner.px);
    app->bg_runner.io = ToriRS_IOBatch_New();
    app->bg_runner.queue = ToriRS_TaskQueue_New();
    /* One IO backend, as for the packet FIFO. */
    app->bg_runner.px = app->runner.px;
    app->bg_runner.parallel = 1;
    TaskRunner_SetClock(&app->bg_runner, app_bg_clock, NULL);
    App_WorldRebuildModeInit(app);
}

void
app_bg_runner_free(struct App* app)
{
    assert(app);
    App_WorldJobCancel(app, "shutdown");
    ToriRS_TaskQueue_Free(app->bg_runner.queue);
    ToriRS_IOBatch_Free(app->bg_runner.io);
    app->bg_runner.queue = NULL;
    app->bg_runner.io = NULL;
}

void
App_WorldRebuildModeInit(struct App* app)
{
    char const* mode = torirs_env_world_rebuild();
    char const* deadline = torirs_env_world_job_deadline_ms();
    long deadline_ms = APP_WORLD_JOB_DEADLINE_MS_DEFAULT;

    assert(app);
    app->world_rebuild_background = 0;
    if( mode && strcmp(mode, "background") == 0 )
        app->world_rebuild_background = 1;
    else if( mode && mode[0] && strcmp(mode, "blocking") != 0 )
        TORIRS_LOG("world rebuild: TORIRS_WORLD_REBUILD=%s is not blocking|background; blocking\n", mode);
    /*
     * A content test keys every step on the tick a state is reached and waits
     * for it with frames. A hold measured in wall time moves every packet
     * behind a REBUILD by a different number of frames on every run, which is
     * exactly what such a run must not vary; it keeps the default.
     */
    if( app->world_rebuild_background && ContentTest_Enabled() )
    {
        TORIRS_LOG("world rebuild: background refused under TORIRS_CONTENT_TEST; blocking\n");
        app->world_rebuild_background = 0;
    }
    if( deadline && deadline[0] )
    {
        long const value = strtol(deadline, NULL, 0);
        if( value > 0 )
            deadline_ms = value;
    }
    app->world_job_deadline_us = (uint64_t)deadline_ms * 1000u;
    if( app->world_rebuild_background )
        TORIRS_LOG("world rebuild: background (job deadline %ld ms)\n", deadline_ms);
}

int
App_WorldJobWanted(struct App const* app)
{
    assert(app);
    if( !app->world_rebuild_background )
        return 0;
    /* The editor's scenes are its own (app_world_load_begin), and a scene it
     * shows must be the one it just asked for. */
    if( app->editor )
        return 0;
    /* A first load (login, a reconnect onto nothing) has no scene to keep on
     * screen; it takes the blocking path as it always has. */
    if( !app->world_active || !app->world || !app->world->load_complete )
        return 0;
    /*
     * The frame latch. A REBUILD is never the first packet of its server
     * tick: on every in-game REBUILD of three rev-242 recordings (29 of 29;
     * RSProx 20261022T202619, 20261022T191126, 20261028T103055) the tick was
     * already open (server_tick_open), opened by VARP_SMALL /
     * VARP_LARGE and nothing else of the UI kind -- the rest being
     * UNSET_MAP_FLAG, REFLECTION_CHECKER, PACKET_GROUP_START and
     * SET_ACTIVE_WORLD -- with no clientscript held. The tick's fence
     * (SERVER_TICK_END) is behind the REBUILD and is held with it, so the
     * latch left as it is would freeze the picture for the job's length.
     *
     * So the job suspends the latch while it holds, and re-arms it at the
     * commit for the rest of the tick (App_WorldJobStart / the commit). What
     * that publishes early is this tick's varps: the reference client, which
     * applies a VARP as it is read and keeps drawing while its loader works,
     * shows the same. What it must never publish early -- a held script, or
     * the interface and inventory packets a script reads -- refuses the
     * background path here: such a tick is not varps alone, and a held
     * script is held. Those REBUILDs are done the blocking way.
     */
    if( app->server_tick_open && app->server_tick_open_nonvar )
        return 0;
    if( RS_ClientScriptQueue_Count(&app->pending_clientscripts) > 0 )
        return 0;
    /* Unreachable while one is pending -- the pump pops nothing -- but a
     * second job would overwrite the first's record. */
    return !app->world_job.pending;
}

int
App_WorldJobPending(struct App const* app)
{
    assert(app);
    return app->world_job.pending;
}

int
App_WorldSettled(struct App const* app)
{
    assert(app);
    return !app->world_load_inflight && !app->world_job.pending;
}

/* Where a job stands (AppWorldJob::phase). */
enum
{
    APP_WORLD_JOB_IDLE = 0,
    APP_WORLD_JOB_GATHER = 1,
    APP_WORLD_JOB_STAGE = 2,
};

/*
 * The staging scene's pool pair: the last view id the scene can mint pools
 * for, which no world view can be (WORLDVIEW_MAX is far below it). Its static
 * pool is the scene's deferred pool while the build runs.
 */
#define APP_WORLD_STAGE_VIEW (TORIDRAW_SCENE_POOL_VIEW_MAX - 1)
_Static_assert(APP_WORLD_STAGE_VIEW >= WORLDVIEW_MAX, "the staging pools belong to no view");

/*
 * Free element slots the staging build must have before each of its units.
 *
 * Two scenes are resident while it runs, in one TORIDRAW_SCENE_MAX_ELEMENTS
 * id space. A unit allocates at most one terrain level of the scene (the mesh
 * stage, (size-2)^2) or one slice of a square's locs (768 locs, two elements
 * at most each); the rest covers the entities the live scene spawns between
 * two units. A build that would run out does not: it stops, and the commit
 * builds in place as a scene too dense for two would have to
 * (AppWorldJob::stage_crowded).
 */
#define APP_WORLD_STAGE_HEADROOM (APP_WORLD_JOB_SCENE_SIZE * APP_WORLD_JOB_SCENE_SIZE + 4096)

/* TORIRS_WORLD_JOB_STAGE_HEADROOM=<slots> overrides it: the headless way to
 * make a scene "too dense for two" and run the in-place fallback. */
static int
app_world_stage_headroom(void)
{
    static long headroom = -1;

    if( headroom < 0 )
    {
        char const* value = getenv("TORIRS_WORLD_JOB_STAGE_HEADROOM");
        headroom = value && value[0] ? strtol(value, NULL, 0) : APP_WORLD_STAGE_HEADROOM;
    }
    return (int)headroom;
}

static int
app_world_stage_crowded(struct App* app)
{
    assert(app);
    return TORIDRAW_SCENE_MAX_ELEMENTS - ToriDraw_SceneElementLiveCount(app->scene) <
           app_world_stage_headroom();
}

/* The REBUILD's varps, for the staging build (AppWorldJob::stage_varps). */
static void
app_world_job_varps_take(struct App* app)
{
    struct AppWorldJob* job = &app->world_job;

    assert(!job->stage_varps_taken);
    /* The type tables are the live manager's and outlive the job; only the
     * values are the job's. Nothing writes through the copy: the builder
     * reads varbits and varps and nothing else (VarPManager_ResolveTransform),
     * so the long arm, the server values and the callbacks stay empty. */
    job->stage_varps = app->varps;
    job->stage_varps.var = NULL;
    job->stage_varps.var_serv = NULL;
    job->stage_varps.var_long = NULL;
    job->stage_varps.var_long_serv = NULL;
    job->stage_varps.var_long_set = NULL;
    job->stage_varps.var_long_count = 0;
    job->stage_varps.change_fn = NULL;
    job->stage_varps.change_userdata = NULL;
    job->stage_varps.server_update_fn = NULL;
    job->stage_varps.server_update_userdata = NULL;
    if( app->varps.varp_count > 0 )
    {
        size_t const bytes = (size_t)app->varps.varp_count * sizeof(int);
        job->stage_varps.var = ToriMem_Alloc(ToriMem_Scene(), bytes);
        assert(job->stage_varps.var);
        memcpy(job->stage_varps.var, app->varps.var, bytes);
    }
    job->stage_varps_taken = 1;
}

static void
app_world_job_varps_drop(struct App* app)
{
    struct AppWorldJob* job = &app->world_job;

    if( !job->stage_varps_taken )
        return;
    ToriMem_Free(ToriMem_Scene(), job->stage_varps.var);
    memset(&job->stage_varps, 0, sizeof(job->stage_varps));
    job->stage_varps_taken = 0;
}

/*
 * Every varp that moved between the REBUILD and the commit -- a client-side
 * write; the server's are held behind the REBUILD -- re-resolves what it
 * drives in the scene just swapped in, through the live remorph path: the
 * same thing a change landing after a build in place does. The remorph it
 * ran on the OLD scene when the varp moved was dropped with that scene (a
 * loc change is keyed to the load_seq it was queued against). Returns how
 * many moved; `out_replaced` how many placements they queued a re-place for.
 */
static int
app_world_job_varps_reresolve(
    struct App* app,
    int* out_replaced)
{
    struct VarPManager const* staged = &app->world_job.stage_varps;
    int const count =
        app->varps.varp_count > staged->varp_count ? app->varps.varp_count : staged->varp_count;
    int moved = 0;

    assert(app->world_job.stage_varps_taken);
    assert(out_replaced);
    *out_replaced = 0;
    for( int id = 0; id < count; id++ )
    {
        if( VarPManager_GetVarp(staged, id) == VarPManager_GetVarp(&app->varps, id) )
            continue;
        moved++;
        *out_replaced += app_varp_refresh_root_loc_transforms(app, id);
    }
    return moved;
}

/*
 * TORIRS_WORLD_JOB_VARP_POKE=<varp>|auto: the forced case for the above. At
 * the first slice boundary of a staging build past its scenery, write <varp>
 * client-side (value + 1), as a clientscript would, so the commit has a varp
 * to re-resolve. `auto` takes the varp behind the first multiloc the staging
 * scene placed. Once per process. Diagnostics.
 */
static void
app_world_job_varp_poke(struct App* app)
{
    static int poked;
    char const* value = getenv("TORIRS_WORLD_JOB_VARP_POKE");
    struct AppWorldJob* job = &app->world_job;
    int varp = -1;
    /* What is added to the varp: a varbit's lowest bit, so the write moves
     * the varbit the multiloc reads. */
    int step = 1;

    if( poked || !value || !value[0] || !job->stage_builder ||
        job->stage_builder->build.stage <= WORLD_BUILD_STAGE_SCENERY )
        return;
    if( strcmp(value, "auto") != 0 )
        varp = (int)strtol(value, NULL, 0);
    else
    {
        struct World_EntityPool const* pool = &job->stage_world->entities.scenery;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL && varp < 0;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Scenery const* sc = World_EntityPoolGet((struct World_EntityPool*)pool, i);
            struct ToriRS_Location* loc = CacheProvider_LocationGet(app->provider, sc->loc_id);
            if( !loc || loc->transform_count <= 0 )
                continue;
            if( loc->transform_varp >= 0 )
                varp = loc->transform_varp;
            else if( loc->transform_varbit >= 0 && loc->transform_varbit < app->varps.varbit_count )
            {
                varp = VarPManager_VarbitBaseVar(&app->varps, loc->transform_varbit);
                step = 1 << app->varps.varbit_types[loc->transform_varbit].startbit;
            }
            if( varp >= 0 )
                TORIRS_REPORT(
                    "world_job: VARP_POKE auto: loc %d at %d,%d,l%d (varbit %d varp %d)\n",
                    sc->loc_id,
                    sc->grid_position.x,
                    sc->grid_position.z,
                    sc->grid_position.level,
                    loc->transform_varbit,
                    loc->transform_varp);
        }
        if( varp < 0 )
            return;
    }
    poked = 1;
    TORIRS_REPORT(
        "world_job: TORIRS_WORLD_JOB_VARP_POKE varp %d %d -> %d mid-build\n",
        varp,
        VarPManager_GetVarp(&app->varps, varp),
        VarPManager_GetVarp(&app->varps, varp) ^ step);
    VarPManager_SetVarpOptimistic(&app->varps, varp, VarPManager_GetVarp(&app->varps, varp) ^ step);
}

/* --- the stage task --------------------------------------------------- */

/*
 * The staging build, a unit at a time, on bg_runner: WorldBuilder_BuildStep
 * until the runner's slice is over, then yield to the next slice. On a pass
 * with no limit (a promoted job, TaskRunner_RunUntil(0)) the slice is never
 * over and the build runs to its end in one resume.
 *
 * It writes only what the job owns -- the staging World, its builder, the
 * staging pools, the varp snapshot -- and reads the provider (held resident)
 * and the scene's shared stores. What it hands back is a flag: the commit is
 * the main thread's, polled (App_WorldJobPoll).
 */
struct Task_AppWorldStage
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int done;
};

struct AppWorldStageSlice
{
    struct App* app;
    struct ToriRS_Slice* slice;
};

/* WorldBuilder_BuildStep's `spent`, asked after every unit but a build's last. */
static int
app_world_stage_spent(void* userdata)
{
    struct AppWorldStageSlice* s = (struct AppWorldStageSlice*)userdata;

    s->app->world_job.build_units++;
    return ToriRS_SliceSpent(s->slice) || app_world_stage_crowded(s->app);
}

static int
Task_AppWorldStage_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_AppWorldStage* self = (struct Task_AppWorldStage*)task_base;
    struct AppWorldJob* job = &self->app->world_job;

    PT_BEGIN(&self->pt);

    for( ;; )
    {
        {
            struct AppWorldStageSlice slice = { self->app, &io->slice };

            /* Ahead of the unit, not only after it: a build that begins
             * already short of room never touches the scene. */
            if( app_world_stage_crowded(self->app) )
            {
                job->stage_crowded = 1;
                break;
            }
            self->done = WorldBuilder_BuildStep(job->stage_builder, app_world_stage_spent, &slice);
        }
        if( self->done )
        {
            /* The build's last unit, which asks no `spent`. */
            job->build_units++;
            break;
        }
        app_world_job_varp_poke(self->app);
        if( app_world_stage_crowded(self->app) )
        {
            TORIRS_LOG(
                "world_job: zone %d,%d staging stopped at %d units: %d elements live; built in place\n",
                job->zone_x,
                job->zone_z,
                job->build_units,
                ToriDraw_SceneElementLiveCount(self->app->scene));
            job->stage_crowded = 1;
            break;
        }
        /* BuildStep stopped for the slice (it stops for nothing else). */
        assert(ToriRS_SliceSpent(&io->slice));
        PT_TASK_YIELD_IF_SPENT();
    }

    PT_END(&self->pt);
}

static void
Task_AppWorldStage_Free(struct ToriRS_Task* task_base)
{
    struct Task_AppWorldStage* self = (struct Task_AppWorldStage*)task_base;

    /* The staging scene is the job's, freed by the commit or the cancel. */
    if( self->app->world_job.stage_task == task_base )
        self->app->world_job.stage_task = NULL;
    ToriMem_Free(ToriMem_Scene(), self);
}

static struct ToriRS_TaskVTable Task_AppWorldStage_VTable = {
    .run = Task_AppWorldStage_Run,
    .free = Task_AppWorldStage_Free,
};

/*
 * Free the staging scene and everything that kept it: its elements, its world
 * and builder, the deferral, the provider hold, the varp snapshot. After a
 * commit the staging world holds the OLD scene (WorldBuilder_HotSwap) and
 * its pools are empty; after a cancel it holds the half-built new one.
 */
static void
app_world_job_stage_free(struct App* app)
{
    struct AppWorldJob* job = &app->world_job;

    assert(!job->stage_task);
    if( job->stage_builder )
    {
        if( WorldBuilder_BuildActive(job->stage_builder) )
            WorldBuilder_BuildAbort(job->stage_builder);
        ToriDraw_SceneClearPool(app->scene, job->stage_builder->static_pool);
        ToriDraw_SceneClearPool(app->scene, job->stage_builder->dynamic_pool);
        ToriDraw_SceneSetDeferredPool(app->scene, -1);
        WorldBuilder_Free(job->stage_builder);
        job->stage_builder = NULL;
    }
    if( job->stage_world )
    {
        World_Free(job->stage_world);
        job->stage_world = NULL;
    }
    if( job->trim_held )
    {
        CacheProvider_TrimRelease(app->provider);
        job->trim_held = 0;
    }
    app_world_job_varps_drop(app);
}

/*
 * The gather has ended: begin the staging build, on the job's own world and
 * builder, as the job's next root task. Left unstarted (stage_crowded) when
 * the scene's element space cannot hold a second scene: the commit builds in
 * place.
 */
static void
app_world_job_stage_begin(struct App* app)
{
    struct AppWorldJob* job = &app->world_job;
    struct Task_AppWorldStage* task;
    int estimate;

    assert(job->phase == APP_WORLD_JOB_GATHER);
    assert(ToriRS_TaskJob_Done(&job->job));
    assert(!job->stage_world);
    assert(!job->stage_builder);
    job->gather = NULL;
    job->phase = APP_WORLD_JOB_STAGE;

    /* The new scene will be about the size of the one it replaces. */
    estimate = ToriDraw_ScenePoolCount(app->scene, TORIDRAW_SCENE_POOL_STATIC);
    if( ToriDraw_SceneElementLiveCount(app->scene) + estimate + app_world_stage_headroom() >
        TORIDRAW_SCENE_MAX_ELEMENTS )
    {
        TORIRS_LOG(
            "world_job: zone %d,%d built in place: %d elements live, ~%d more for a staging scene, "
            "%d free a unit needs, of %d\n",
            job->zone_x,
            job->zone_z,
            ToriDraw_SceneElementLiveCount(app->scene),
            estimate,
            app_world_stage_headroom(),
            TORIDRAW_SCENE_MAX_ELEMENTS);
        job->stage_crowded = 1;
        return;
    }

    job->stage_world = World_New();
    World_SetScene(job->stage_world, app->scene);
    job->stage_builder = WorldBuilder_New(job->stage_world, app->provider, app->scene, &job->stage_varps);
    WorldBuilder_SetSceneView(job->stage_builder, APP_WORLD_STAGE_VIEW);
    ToriDraw_SceneSetDeferredPool(app->scene, job->stage_builder->static_pool);
    /* From here to the commit every unit re-reads what the gather made
     * resident; nothing may trim it in between. */
    CacheProvider_TrimHold(app->provider);
    job->trim_held = 1;

    if( job->have_zones )
        WorldBuilder_BuildBeginInstance(
            job->stage_builder, job->zone_x, job->zone_z, APP_WORLD_JOB_SCENE_SIZE, job->zones);
    else if( job->zone_x >= 0 )
        WorldBuilder_BuildBeginCenterzone(
            job->stage_builder, job->zone_x, job->zone_z, APP_WORLD_JOB_SCENE_SIZE);
    else
        WorldBuilder_BuildBeginChunklist(job->stage_builder, job->chunks, job->chunk_count);

    task = ToriMem_Calloc(ToriMem_Scene(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppWorldStage_VTable;
    strcpy(task->task.name, "WorldStage");
    task->app = app;
    PT_INIT(&task->pt);
    job->stage_task = &task->task;
    {
        int const promoted = job->job.promoted;
        ToriRS_TaskJob_Start(&job->job, &app->bg_runner, &task->task, job->job.must_finish_by_us);
        job->job.promoted = promoted;
    }
}

/* Move the job on as far as its ended tasks allow: a gather that has ended
 * begins the staging build. 1: everything the job runs has ended. */
static int
app_world_job_advance(struct App* app)
{
    struct AppWorldJob* job = &app->world_job;

    if( !ToriRS_TaskJob_Done(&job->job) )
        return 0;
    if( job->phase == APP_WORLD_JOB_GATHER )
    {
        app_world_job_stage_begin(app);
        return ToriRS_TaskJob_Done(&job->job);
    }
    return 1;
}

void
App_WorldJobStart(
    struct App* app,
    int const* chunks_xz,
    int chunk_count,
    int zone_x,
    int zone_z,
    int32_t const* zones)
{
    struct AppWorldJob* job;
    struct Worldview* root;
    uint64_t now;

    assert(app);
    assert(chunks_xz);
    assert(chunk_count >= 0);
    assert(chunk_count <= (int)(sizeof(app->world_job.chunks) / sizeof(app->world_job.chunks[0]) / 2));
    job = &app->world_job;
    assert(!job->pending);
    assert(job->phase == APP_WORLD_JOB_IDLE);
    assert(!job->stage_world);
    _Static_assert(
        sizeof(job->zones) == PKT_MAP_REBUILD_ZONES * sizeof(job->zones[0]),
        "the job keeps the packet's grid");

    root = WorldviewRegistry_Get(&app->worldviews, WORLDVIEW_ROOT);
    now = TaskRunner_Now(&app->bg_runner);

    memset(&job->report, 0, sizeof(job->report));
    job->zone_x = zone_x;
    job->zone_z = zone_z;
    job->chunk_count = chunk_count;
    memcpy(job->chunks, chunks_xz, (size_t)chunk_count * 2 * sizeof(int));
    job->have_zones = zones != NULL;
    if( zones )
        memcpy(job->zones, zones, sizeof(job->zones));
    job->stalled = 0;
    job->t_begin_us = now;
    job->slices = 0;
    job->slice_us_sum = 0;
    job->slice_us_max = 0;
    job->build_slices = 0;
    job->build_slice_us_sum = 0;
    job->build_slice_us_max = 0;
    job->build_units = 0;
    job->stage_crowded = 0;
    job->held_open = 0;
    /* The varps the scene is built against are the REBUILD's, as a build in
     * place at this packet would read them. */
    app_world_job_varps_take(app);

    job->gather = CreateTask_WorldGather(
        app->provider,
        root->builder,
        app->bg_runner.queue,
        job->chunks,
        job->chunk_count,
        job->zone_x,
        job->zone_z,
        APP_WORLD_JOB_SCENE_SIZE,
        job->have_zones ? job->zones : NULL,
        &job->report);
    ToriRS_TaskJob_Start(&job->job, &app->bg_runner, job->gather, now + app->world_job_deadline_us);
    job->phase = APP_WORLD_JOB_GATHER;
    job->pending = 1;
    /* The tick this REBUILD belongs to is open on varps alone (the only open
     * tick App_WorldJobWanted lets through); its fence is held behind us.
     * Suspend the latch for the hold, or nothing would be published until the
     * commit; the commit re-arms it for the rest of the tick. */
    job->tick_suspended = app->server_tick_open;
    app->server_tick_open = 0;
    if( WorldBuilder_TimingOn() )
        TORIRS_REPORT(
            "world_job: start zone=%d,%d squares=%d instance=%d cycle=%llu\n",
            zone_x,
            zone_z,
            chunk_count,
            job->have_zones,
            (unsigned long long)app->logic_cycle);
}

/*
 * TORIRS_WORLD_JOB_REPEAT=<n>: right after each of the first n commits, start
 * the same rebuild again as a fresh job -- a second REBUILD landing the frame
 * after a commit, which the recordings never put closer than 16 ticks. It is
 * acknowledged (MAP_BUILD_COMPLETE) like a server one; a replay ignores the
 * ack. Diagnostics.
 */
static void
app_world_job_repeat_requested(struct App* app)
{
    static long remaining = -2;
    static int repeating;
    struct AppWorldJob* job = &app->world_job;
    int chunks[sizeof(job->chunks) / sizeof(job->chunks[0])];
    int32_t zones[sizeof(job->zones) / sizeof(job->zones[0])];

    if( remaining == -2 )
    {
        char const* value = getenv("TORIRS_WORLD_JOB_REPEAT");
        remaining = value && value[0] ? strtol(value, NULL, 0) : 0;
    }
    /* A repeat's own commit does not repeat again. */
    if( repeating )
    {
        repeating = 0;
        return;
    }
    if( remaining <= 0 )
        return;
    remaining--;
    repeating = 1;
    memcpy(chunks, job->chunks, sizeof(chunks));
    memcpy(zones, job->zones, sizeof(zones));
    app->world_load_server_driven = 1;
    app_rebuild_frames_open(app, 1);
    TORIRS_REPORT("world_job: TORIRS_WORLD_JOB_REPEAT zone=%d,%d again, right after its commit\n",
        job->zone_x, job->zone_z);
    App_WorldJobStart(app, chunks, job->chunk_count, job->zone_x, job->zone_z, job->have_zones ? zones : NULL);
}

/*
 * Put the new scene in and do everything the blocking flow does after its
 * await, in the same order (task_gameproto_exec.c's REBUILD branch): the
 * staging scene swapped in whole (WorldBuilder_HotSwap) -- or, when it could
 * not be staged, the build in place -- then the entity shift and
 * App_WorldLoadFinish.
 */
static void
app_world_job_commit(struct App* app)
{
    struct AppWorldJob* job = &app->world_job;
    struct Worldview* root;
    int had_world;
    int prev_base_x;
    int prev_base_z;
    int swapped;
    int varps_moved = 0;
    int varps_replaced = 0;
    uint64_t t_commit;
    uint64_t t_swapped;
    uint64_t t_shifted;
    uint64_t t_done;

    assert(ToriRS_TaskJob_Done(&job->job));
    assert(job->phase == APP_WORLD_JOB_STAGE);
    assert(!job->stage_task);
    job->gather = NULL;
    root = WorldviewRegistry_Get(&app->worldviews, WORLDVIEW_ROOT);
    swapped = !job->stage_crowded;
    assert(!swapped || (job->stage_builder && !WorldBuilder_BuildActive(job->stage_builder)));

    /* The plugins' copy of the grid goes with the scene it describes, so a
     * frame of the old scene never shows it beside the new grid. */
    app->rebuild_instance_active = job->have_zones;
    app->rebuild_instance_zone_x = job->zone_x;
    app->rebuild_instance_zone_z = job->zone_z;
    if( job->have_zones )
        memcpy(app->rebuild_instance_zones, job->zones, sizeof(app->rebuild_instance_zones));

    /* The entity-shift source, as the packet task captures it. Nothing can
     * have moved the base since the packet: the pump has popped nothing. */
    had_world = app->world_active && app->world && root->world->load_complete;
    prev_base_x = had_world ? root->world->_base_tile_x : 0;
    prev_base_z = had_world ? root->world->_base_tile_z : 0;

    /* The C2 drain rule (World_ResetSceneAlloc asserts the queue is empty,
     * and so does the swap). The old scene went on simulating while the job
     * ran, and what it removed since the packet is drained here. */
    App_WorldDrainEntityRemovedFor(app, root->world);

    t_commit = TaskRunner_Now(&app->bg_runner);
    if( swapped )
    {
        WorldBuilder_HotSwap(root->builder, job->stage_builder);
    }
    else
    {
        /* Built in place, as phase B did: against the live varps, now. */
        if( job->stage_builder )
            app_world_job_stage_free(app);
        WorldLoad_Build(
            root->builder,
            job->chunks,
            job->chunk_count,
            job->zone_x,
            job->zone_z,
            APP_WORLD_JOB_SCENE_SIZE,
            job->have_zones ? job->zones : NULL);
    }
    t_swapped = TaskRunner_Now(&app->bg_runner);

    if( had_world && root->world->load_complete )
        App_WorldRebuildShift(
            app,
            root->world->_base_tile_x - prev_base_x,
            root->world->_base_tile_z - prev_base_z);
    t_shifted = TaskRunner_Now(&app->bg_runner);
    app->rebuild_frames.build_ms = (double)(t_swapped - t_commit) / 1000.0;
    app->rebuild_frames.swap_ms = (double)(t_swapped - t_commit) / 1000.0;
    app->rebuild_frames.shift_ms = (double)(t_shifted - t_swapped) / 1000.0;
    /* Clears world_load_inflight, which a stalled promotion set. */
    App_WorldLoadFinish(app);
    /* After the finish, where a varp written after a build in place would
     * land: its re-place runs behind the standing loc changes'. */
    if( swapped )
        varps_moved = app_world_job_varps_reresolve(app, &varps_replaced);
    /* The old scene (the swap left it on the staging side) and everything
     * that kept the job's own: one scene resident from here on. */
    app_world_job_stage_free(app);
    t_done = TaskRunner_Now(&app->bg_runner);
    app->rebuild_frames.tail_ms = (double)(t_done - t_swapped) / 1000.0;
    app->rebuild_frames.finish_ms = (double)(t_done - t_shifted) / 1000.0;
    job->pending = 0;
    job->stalled = 0;
    job->phase = APP_WORLD_JOB_IDLE;
    /* The rest of the REBUILD's tick is about to be popped: latch it as the
     * blocking flow would have, from here to its SERVER_TICK_END. Counted
     * from now, so the lost-fence backstop measures this stretch, not the
     * hold. */
    if( job->tick_suspended )
    {
        job->tick_suspended = 0;
        app->server_tick_open = 1;
        app->server_tick_open_cycle = app->logic_cycle;
    }
    /* The held packets popped behind this, to the pump's return. */
    job->held_open = 1;
    job->held_t0_us = t_done;

    if( WorldBuilder_TimingOn() )
        TORIRS_REPORT(
            "world_job: commit zone=%d,%d wall=%.1fms gather=%.1fms slices=%d slice_sum=%.1fms "
            "slice_max=%.2fms promoted=%d mode=%s build_slices=%d build_units=%d "
            "build_slice_max=%.2fms build_slice_sum=%.1fms swap=%.2fms shift=%.2fms finish=%.2fms "
            "varps_moved=%d replaced=%d trims_held=%d resumes=%d chunks=%d locs=%d models=%d textures=%d seqs=%d\n",
            job->zone_x,
            job->zone_z,
            (double)(t_done - job->t_begin_us) / 1000.0,
            job->report.done ? job->report.t_assets_ms - job->report.t_start_ms : -1.0,
            job->slices,
            (double)job->slice_us_sum / 1000.0,
            (double)job->slice_us_max / 1000.0,
            job->job.promoted,
            swapped ? "swap" : "in_place",
            job->build_slices,
            job->build_units,
            (double)job->build_slice_us_max / 1000.0,
            (double)job->build_slice_us_sum / 1000.0,
            app->rebuild_frames.swap_ms,
            app->rebuild_frames.shift_ms,
            app->rebuild_frames.finish_ms,
            varps_moved,
            varps_replaced,
            app->provider->trims_held,
            job->report.resumes,
            job->report.chunk_count,
            job->report.locs,
            job->report.models,
            job->report.textures,
            job->report.seqs);
    app_world_job_repeat_requested(app);
}

void
app_world_job_held_note(struct App* app)
{
    struct AppWorldJob* job;

    assert(app);
    job = &app->world_job;
    if( !job->held_open )
        return;
    job->held_open = 0;
    app->rebuild_frames.held_ms = (double)(TaskRunner_Now(&app->bg_runner) - job->held_t0_us) / 1000.0;
}

int
App_WorldJobPoll(struct App* app)
{
    struct AppWorldJob* job;

    assert(app);
    job = &app->world_job;
    if( !job->pending )
        return 1;
    if( !app_world_job_advance(app) )
    {
        uint64_t const now = TaskRunner_Now(&app->bg_runner);
        uint64_t t_after;

        if( now < job->job.must_finish_by_us )
            return 0;
        /*
         * Past its deadline: stop waiting for slices and finish it here, in
         * this frame, with no limit -- the rest of the gather and the whole
         * staging build. On the desktop every read is answered as it is asked
         * for, so this runs to the end.
         */
        if( !job->job.promoted )
        {
            job->job.promoted = 1;
            TORIRS_LOG(
                "world_job: zone %d,%d not %s after %llu ms; finishing it now\n",
                job->zone_x,
                job->zone_z,
                job->phase == APP_WORLD_JOB_GATHER ? "gathered" : "built",
                (unsigned long long)((now - job->t_begin_us) / 1000u));
        }
        (void)TaskRunner_RunUntil(&app->bg_runner, 0);
        if( app_world_job_advance(app) == 0 && job->phase == APP_WORLD_JOB_STAGE )
            (void)TaskRunner_RunUntil(&app->bg_runner, 0);
        t_after = TaskRunner_Now(&app->bg_runner);
        job->slices++;
        job->slice_us_sum += t_after - now;
        if( t_after - now > job->slice_us_max )
            job->slice_us_max = t_after - now;
        if( !app_world_job_advance(app) )
        {
            /*
             * Waiting on reads only the platform can answer (JS5, the
             * browser): spinning here would answer nothing. Say what a
             * blocking rebuild says while it waits -- the viewport's
             * "Loading - please wait." -- and come back next pump.
             */
            if( !job->stalled )
            {
                job->stalled = 1;
                app->world_load_inflight = 1;
                app->need_redraw = 1;
            }
            return 0;
        }
    }
    app_world_job_commit(app);
    return 1;
}

void
App_WorldJobCancel(
    struct App* app,
    char const* why)
{
    struct AppWorldJob* job;

    assert(app);
    assert(why);
    job = &app->world_job;
    if( !job->pending )
        return;
    if( !ToriRS_TaskJob_Done(&job->job) )
    {
        if( job->phase == APP_WORLD_JOB_GATHER )
        {
            assert(job->gather);
            TaskWorldLoad_Cancel(app->bg_runner.queue, job->gather);
        }
        else
        {
            /* The stage task owns no read; the queue may unlink it at any
             * yield, which ends the job it is counted on. */
            assert(job->stage_task);
            ToriRS_TaskQueue_Remove(app->bg_runner.queue, job->stage_task);
            assert(!job->stage_task);
        }
    }
    assert(ToriRS_TaskJob_Done(&job->job));
    if( job->phase == APP_WORLD_JOB_STAGE && WorldBuilder_TimingOn() && job->stage_builder )
        TORIRS_REPORT(
            "world_job: cancel mid-build zone=%d,%d units=%d staged_elements=%d live_elements=%d\n",
            job->zone_x,
            job->zone_z,
            job->stage_builder->build.units,
            ToriDraw_ScenePoolCount(app->scene, job->stage_builder->static_pool),
            ToriDraw_SceneElementLiveCount(app->scene));
    /* The half-built scene goes, abort and elements and all. */
    app_world_job_stage_free(app);
    if( WorldBuilder_TimingOn() )
        TORIRS_REPORT(
            "world_job: cancelled live_elements=%d\n", ToriDraw_SceneElementLiveCount(app->scene));
    job->gather = NULL;
    job->pending = 0;
    job->phase = APP_WORLD_JOB_IDLE;
    /* The tick it suspended ends with the session (or is superseded by the
     * load replacing this scene); nothing of it is left to latch. */
    job->tick_suspended = 0;
    if( job->stalled )
    {
        job->stalled = 0;
        app->world_load_inflight = 0;
    }
    /* Nothing was built, so nothing is acknowledged; and the zone the job was
     * for was never the scene's, so a REBUILD naming it again must not be
     * skipped as the same zone. */
    app->world_load_server_driven = 0;
    app->rebuild_zone_x = -1;
    app->rebuild_zone_z = -1;
    app->rebuild_frames.open = 0;
    app->need_redraw = 1;
    TORIRS_LOG("world_job: zone %d,%d cancelled (%s)\n", job->zone_x, job->zone_z, why);
}

/*
 * TORIRS_WORLD_JOB_DROP_AT=<n>: sever the session after the first slice of the
 * n-th background rebuild, while it is still out -- the headless way to put a
 * teardown in the middle of a job, which a wall-clock drop (TORIRS_NET_DROP_MS)
 * only hits by luck. The session ends by the route a lost link takes
 * (app_net_lost), so the job is cancelled by the same call every real teardown
 * makes. Fires once. TORIRS_WORLD_JOB_DROP_PHASE=build waits for the staging
 * build to be under way (at least TORIRS_WORLD_JOB_DROP_UNITS units, default
 * 10) instead of the first slice of the gather.
 */
static void
app_world_job_drop_requested(struct App* app)
{
    static long drop_at = -2;
    static int drop_in_build;
    static long drop_units;
    static long jobs_seen;
    static uint64_t last_begin_us;

    assert(app);
    if( drop_at == -2 )
    {
        char const* value = getenv("TORIRS_WORLD_JOB_DROP_AT");
        char const* phase = getenv("TORIRS_WORLD_JOB_DROP_PHASE");
        char const* units = getenv("TORIRS_WORLD_JOB_DROP_UNITS");
        drop_at = value && value[0] ? strtol(value, NULL, 0) : -1;
        drop_in_build = phase && strcmp(phase, "build") == 0;
        drop_units = units && units[0] ? strtol(units, NULL, 0) : 10;
    }
    if( drop_at <= 0 )
        return;
    if( app->world_job.t_begin_us != last_begin_us )
    {
        last_begin_us = app->world_job.t_begin_us;
        jobs_seen++;
    }
    if( jobs_seen != drop_at || ToriRS_TaskJob_Done(&app->world_job.job) )
        return;
    if( drop_in_build &&
        (app->world_job.phase != APP_WORLD_JOB_STAGE || app->world_job.build_units < drop_units) )
        return;
    drop_at = -1;
    {
        int queued = 0;
        for( struct ToriRS_Task const* task = app->bg_runner.queue->head; task; task = task->next )
            queued++;
        TORIRS_REPORT(
            "world_job: TORIRS_WORLD_JOB_DROP_AT severing the session mid-%s (job %ld, %d "
            "slice(s), %d build unit(s), %d task(s) queued)\n",
            app->world_job.phase == APP_WORLD_JOB_STAGE ? "build" : "gather",
            jobs_seen,
            app->world_job.slices,
            app->world_job.build_units,
            queued);
    }
    app_net_lost(app, "TORIRS_WORLD_JOB_DROP_AT");
}

void
App_RunBackground(
    struct App* app,
    uint64_t deadline_us)
{
    struct AppWorldJob* job;
    uint64_t t0;
    uint64_t t1;
    int building;

    assert(app);
    /* The common case, every frame: nothing queued, nothing read. */
    if( !app->bg_runner.queue->head )
        return;
    t0 = TaskRunner_Now(&app->bg_runner);
    if( deadline_us <= t0 )
        return;
    job = &app->world_job;
    building = job->pending && job->phase == APP_WORLD_JOB_STAGE;
    (void)TaskRunner_RunUntil(&app->bg_runner, deadline_us);
    /* A gather that ended in this slice hands the rest of it to the build. */
    if( job->pending && job->phase == APP_WORLD_JOB_GATHER && !app_world_job_advance(app) &&
        job->phase == APP_WORLD_JOB_STAGE && TaskRunner_Now(&app->bg_runner) < deadline_us )
    {
        building = 1;
        (void)TaskRunner_RunUntil(&app->bg_runner, deadline_us);
    }
    t1 = TaskRunner_Now(&app->bg_runner);
    if( job->pending )
    {
        job->slices++;
        job->slice_us_sum += t1 - t0;
        if( t1 - t0 > job->slice_us_max )
            job->slice_us_max = t1 - t0;
        if( building )
        {
            job->build_slices++;
            job->build_slice_us_sum += t1 - t0;
            if( t1 - t0 > job->build_slice_us_max )
                job->build_slice_us_max = t1 - t0;
        }
        app_world_job_drop_requested(app);
    }
}

void
app_run_background_floor(struct App* app)
{
    assert(app);
    if( !app->bg_runner.queue->head )
        return;
    App_RunBackground(app, PlatformWindow_TicksUs() + APP_BACKGROUND_FLOOR_US);
}

/* --- TORIRS_WORLD_DIGEST: what a rebuild left, comparable across modes ----- */

static uint32_t
app_digest_mix(
    uint32_t hash,
    int value)
{
    /* FNV-1a over the int's bytes: order-sensitive, which is the point --
     * the same packets in the same order fill the pools in the same order. */
    uint32_t v = (uint32_t)value;
    for( int i = 0; i < 4; i++ )
    {
        hash ^= (v >> (i * 8)) & 0xffu;
        hash *= 16777619u;
    }
    return hash;
}

/*
 * One line naming the root world's state: its base, and a count and hash per
 * entity kind over what a rebuild or a packet decides (ids, tiles, shapes).
 * Not what the frame clock decides (draw positions, animation frames), so two
 * runs that applied the same packets at different frames agree once those
 * packets have all been applied. Diagnostics; TORIRS_WORLD_DIGEST=1 (every
 * finished load) and TORIRS_WORLD_DIGEST_EVERY=<server ticks> (the packet
 * pump, as a tick's fence has been applied: keyed by tick, not by frame, since
 * a background rebuild's hold moves every later packet by some frames).
 */
void
app_world_digest_report(
    struct App* app,
    char const* why)
{
    struct World* world;
    struct World_EntityPool const* pool;
    uint32_t h_scenery = 2166136261u;
    uint32_t h_npc = 2166136261u;
    uint32_t h_player = 2166136261u;
    uint32_t h_obj = 2166136261u;
    int n_scenery = 0;
    int n_npc = 0;
    int n_player = 0;
    int n_obj = 0;

    assert(app);
    assert(why);
    world = app->world;
    if( !world || !world->load_complete )
        return;

    pool = &world->entities.scenery;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery const* e = World_EntityPoolGet((struct World_EntityPool*)pool, i);
        if( !e )
            continue;
        n_scenery++;
        h_scenery = app_digest_mix(h_scenery, e->loc_id);
        h_scenery = app_digest_mix(h_scenery, e->grid_position.x);
        h_scenery = app_digest_mix(h_scenery, e->grid_position.z);
        h_scenery = app_digest_mix(h_scenery, e->grid_position.level);
        h_scenery = app_digest_mix(h_scenery, e->shape);
        h_scenery = app_digest_mix(h_scenery, e->angle);
    }
    pool = &world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC const* e = World_EntityPoolGet((struct World_EntityPool*)pool, i);
        if( !e )
            continue;
        n_npc++;
        h_npc = app_digest_mix(h_npc, e->server_slot);
        h_npc = app_digest_mix(h_npc, e->grid_position.x);
        h_npc = app_digest_mix(h_npc, e->grid_position.z);
        h_npc = app_digest_mix(h_npc, e->grid_position.level);
    }
    pool = &world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player const* e = World_EntityPoolGet((struct World_EntityPool*)pool, i);
        if( !e )
            continue;
        n_player++;
        h_player = app_digest_mix(h_player, e->grid_position.x);
        h_player = app_digest_mix(h_player, e->grid_position.z);
        h_player = app_digest_mix(h_player, e->grid_position.level);
    }
    pool = &world->entities.obj_stack;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack const* e = World_EntityPoolGet((struct World_EntityPool*)pool, i);
        if( !e )
            continue;
        n_obj++;
        h_obj = app_digest_mix(h_obj, e->obj_id);
        h_obj = app_digest_mix(h_obj, e->count);
        h_obj = app_digest_mix(h_obj, e->grid_position.x);
        h_obj = app_digest_mix(h_obj, e->grid_position.z);
    }
    TORIRS_REPORT(
        "world_digest: %s server_tick=%u cycle=%llu base=%d,%d scenery=%d/%08x npc=%d/%08x "
        "player=%d/%08x obj=%d/%08x\n",
        why,
        app->server_tick_serial,
        (unsigned long long)app->logic_cycle,
        world->_base_tile_x,
        world->_base_tile_z,
        n_scenery,
        h_scenery,
        n_npc,
        h_npc,
        n_player,
        h_player,
        n_obj,
        h_obj);
}

void
app_world_digest_loaded(struct App* app)
{
    static int on = -1;

    assert(app);
    if( on < 0 )
        on = getenv("TORIRS_WORLD_DIGEST") != NULL;
    if( on )
        app_world_digest_report(app, "loaded");
}

void
app_world_digest_tick(struct App* app)
{
    static long every = -1;

    assert(app);
    if( every < 0 )
    {
        char const* value = getenv("TORIRS_WORLD_DIGEST_EVERY");
        every = value && value[0] ? strtol(value, NULL, 0) : 0;
    }
    if( every > 0 && app->server_tick_serial % (unsigned)every == 0 )
        app_world_digest_report(app, "tick");
}

/* --- TORIRS_REBUILD_TIMING's frame window --------------------------------- */

void
app_rebuild_frames_open(
    struct App* app,
    int background)
{
    assert(app);
    if( !WorldBuilder_TimingOn() )
        return;
    memset(&app->rebuild_frames, 0, sizeof(app->rebuild_frames));
    app->rebuild_frames.open = 1;
    app->rebuild_frames.background = background;
}

void
app_rebuild_frames_finished(struct App* app)
{
    assert(app);
    if( app->rebuild_frames.open )
        app->rebuild_frames.closing = 1;
}

void
app_rebuild_frames_note(
    struct App* app,
    uint64_t frame_us)
{
    static uint64_t const edges_us[5] = { 2000, 5000, 10000, 20000, 50000 };
    int bucket = 5;

    assert(app);
    if( !app->rebuild_frames.open )
        return;
    for( int i = 0; i < 5; i++ )
        if( frame_us < edges_us[i] )
        {
            bucket = i;
            break;
        }
    app->rebuild_frames.hist[bucket]++;
    app->rebuild_frames.frames++;
    app->rebuild_frames.sum_us += frame_us;
    app->rebuild_frames.last_us = frame_us;
    if( frame_us > app->rebuild_frames.max_us )
        app->rebuild_frames.max_us = frame_us;
    if( !app->rebuild_frames.closing )
        return;
    /* The frame that finished the rebuild is the last one counted: for the
     * blocking flow that is usually the only one; for a job it is the commit. */
    TORIRS_REPORT(
        "rebuild_frames: mode=%s zone=%d,%d frames=%d max=%.2fms last=%.2fms sum=%.2fms "
        "hist(<2,<5,<10,<20,<50,>=50ms)=%d,%d,%d,%d,%d,%d build=%.2fms tail=%.2fms "
        "swap=%.2fms shift=%.2fms finish=%.2fms held=%.2fms\n",
        app->rebuild_frames.background ? "background" : "blocking",
        app->rebuild_zone_x,
        app->rebuild_zone_z,
        app->rebuild_frames.frames,
        (double)app->rebuild_frames.max_us / 1000.0,
        (double)app->rebuild_frames.last_us / 1000.0,
        (double)app->rebuild_frames.sum_us / 1000.0,
        app->rebuild_frames.hist[0],
        app->rebuild_frames.hist[1],
        app->rebuild_frames.hist[2],
        app->rebuild_frames.hist[3],
        app->rebuild_frames.hist[4],
        app->rebuild_frames.hist[5],
        app->rebuild_frames.build_ms,
        app->rebuild_frames.tail_ms,
        app->rebuild_frames.swap_ms,
        app->rebuild_frames.shift_ms,
        app->rebuild_frames.finish_ms,
        app->rebuild_frames.held_ms);
    app->rebuild_frames.open = 0;
}
