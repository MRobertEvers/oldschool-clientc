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
 *   2. The gather (CreateTask_WorldGather) runs on background_runner, fanned out
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
 * default and is untouched by any of this; see App_WorldRebuildModeInit.
 *
 * What the job records, its phases and its slice counts are
 * engine/world_builder/world_rebuild_job.h; the frame window around a
 * rebuild is engine/world_builder/rebuild_frame_stats.h.
 */

#include "app/app_internal.h"
#include "engine/world_builder/rebuild_frame_stats.h"
#include "engine/world_builder/rebuild_pace.h"
#include "engine/world_builder/world_rebuild_job.h"
#include "features/features.h"
#include "game/content_test.h"
#include "mem/torirs_mem.h"
#include "platform/platform_web_telemetry.h"

extern uint64_t
PlatformWindow_TicksUs(void);
extern uint64_t
PlatformWindow_ThreadCpuUs(void);

/* The slice App_RunOnce always gives the background queue, so every platform
 * loop moves it with no change of its own. */
#define APP_BACKGROUND_FLOOR_US 2000u

/* What the floor spends freeing a swapped-out scene's models per frame. */
#define APP_GRAVEYARD_SLICE_US 500u

#define APP_WORLD_JOB_DEADLINE_MS_DEFAULT 600

/* The scene side a root rebuild builds (the packet task passes the same). */
#define APP_WORLD_JOB_SCENE_SIZE 104

static uint64_t
app_runner_clock(void* ud)
{
    (void)ud;
    return PlatformWindow_TicksUs();
}

/* --- who is stepping now (the frame contract's second run-time check) -----
 *
 * The background queue is given time by its own pump and by nothing else:
 * never from inside the script settle, the logic ticks (the packet pump
 * among them), the parallel queue's pump, or a task of another queue. Those
 * sites say so here while they run (App_QueueSiteEnter/Leave), and the
 * background pass checks on entry. The one owner exception is explicit: the
 * world job finishing its own job at its deadline (App_WorldJobPoll), which
 * the packet pump asks for while it holds the packets behind it.
 */
static char const* g_app_queue_site;

char const*
App_QueueSiteEnter(char const* site)
{
    char const* previous = g_app_queue_site;

    assert(site);
    g_app_queue_site = site;
    return previous;
}

void
App_QueueSiteLeave(char const* previous)
{
    g_app_queue_site = previous;
}

/*
 * TORIRS_QUEUE_CONTRACT_TRACE: what one background job was given while it
 * was pending, reported when it commits -- the check that a rebuild that
 * holds the packet pump still lets frames through (frames, committed), and
 * that the job gets both its floor (every App_RunOnce) and the loop's slack.
 * Process-wide like the trace; a client has one world job at a time.
 */
static struct
{
    int frames;
    int committed;
    int floor_slices;
    int slack_slices;
    int in_floor;
} g_job_tally;

char const*
App_QueueSiteNow(void)
{
    return g_app_queue_site;
}

static void
app_background_entry_check(
    struct App* app,
    int owner)
{
    assert(app);
    TaskRunner_CheckBackgroundEntry(
        &app->packet_runner, &app->frame_runner, &app->background_runner, g_app_queue_site, owner);
}

/*
 * The parallel queue's "computes" threshold (TaskRunner::compute_step_us),
 * per lane, ONE table. A step's cost is read on PlatformWindow_ThreadCpuUs:
 * the stepping thread's processor time where the lane has that clock, so a
 * strict run on a loaded machine no longer aborts on a step that waited for
 * a core (the wall-time check named NpcBodyLand at 10.36 ms and MountVisuals
 * at 5.85 ms beside parallel builds, both clean on a quiet machine). The
 * number is a share of the 20 ms logic frame a parallel step may take before
 * it is CPU work on the IO queue -- a property of the frame, not of a
 * machine, so a slower lane that computes more per step is exactly what it
 * should name. TORIRS_QUEUE_CONTRACT_COMPUTE_MS overrides it, 0 turns it off.
 */
struct AppComputeLane
{
    char const* lane;
    uint32_t threshold_us;
    /* What the number rests on. */
    char const* evidence;
};

static struct AppComputeLane const k_app_compute_lanes[] = {
    { "macos", 4000u,
      "thread CPU time. lo-checks: cooks_assistant strict at 4 ms with all 16 cores spun by another "
      "process; the 96.bin replay windows at a 0.3 ms threshold named only AssetLoad:obj_model "
      "(0.56 ms) and Dat2LocLoad (0.91 ms) -- AppCS1Eval, 14.1 ms on the wall clock under load, "
      "never reached 0.3 ms of processor time" },
    { "linux", 4000u, "thread CPU time, the same clock as macos; not run (no toolchain here)" },
    { "android", 4000u,
      "thread CPU time, the same clock; never run on a device. A phone computes a step more slowly, "
      "which the check is there to name, not to excuse" },
    { "windows", 4000u,
      "QueryThreadCycleTime at the time-stamp counter's rate (wall time on XP, which lacks it); "
      "built only, never run" },
    { "web", 4000u,
      "wall time (one thread, performance.now(), 100 us steps). fv_web: 18 strict boots and 16 "
      "strict sessions on Soft3D, WebGL2 and WebGL1 at 4 ms, 0 compute lines once the music decode "
      "moved to the background queue (43a8f9ad9)" },
};

static struct AppComputeLane const*
app_compute_lane(void)
{
#if defined(TORIRS_PLATFORM_WEB)
    return &k_app_compute_lanes[4];
#elif defined(TORIRS_PLATFORM_ANDROID) || defined(__ANDROID__)
    return &k_app_compute_lanes[2];
#elif defined(_WIN32)
    return &k_app_compute_lanes[3];
#elif defined(__APPLE__)
    return &k_app_compute_lanes[0];
#else
    return &k_app_compute_lanes[1];
#endif
}

static uint64_t
app_parallel_compute_us(void)
{
    char const* env = getenv("TORIRS_QUEUE_CONTRACT_COMPUTE_MS");

    if( env && env[0] )
        return (uint64_t)(atof(env) * 1000.0);
    return app_compute_lane()->threshold_us;
}

/* The paced catch-up (engine/world_builder/rebuild_pace.c) on the background
 * queue's clock and App_RunOnce's frame serial. */
void
App_PaceArm(struct App* app)
{
    assert(app);
    RebuildPace_Arm(
        &app->pace,
        TaskRunner_Now(&app->background_runner),
        FrameClockSerial_RunOnce(&app->frame_clock.serial));
}

uint64_t
App_PacedDeadline(struct App* app)
{
    uint64_t deadline;
    int ended;

    assert(app);
    if( !RebuildPace_Pacing(&app->pace) )
        return 0;
    deadline = RebuildPace_Deadline(
        &app->pace,
        TaskRunner_Now(&app->background_runner),
        FrameClockSerial_RunOnce(&app->frame_clock.serial),
        &ended);
    if( ended && WorldBuilder_TimingOn() )
        TORIRS_REPORT(
            "rebuild_pace: ended pop_stops=%d settle_stops=%d\n",
            app->pace.stat_pop_stops,
            app->pace.stat_settle_stops);
    return deadline;
}

void
App_PaceNoteStop(struct App* app)
{
    assert(app);
    if( !RebuildPace_Pacing(&app->pace) )
        return;
    RebuildPace_NoteStop(&app->pace, TaskRunner_Now(&app->background_runner));
}

int
App_PaceHoldsPop(struct App* app)
{
    uint64_t deadline;

    assert(app);
    deadline = App_PacedDeadline(app);
    if( deadline == 0 )
        return 0;
    if( !RebuildPace_HoldsPop(
            &app->pace,
            deadline,
            TaskRunner_Now(&app->background_runner),
            FrameClockSerial_RunOnce(&app->frame_clock.serial)) )
        return 0;
    App_PaceNoteStop(app);
    return 1;
}

void
app_background_runner_init(struct App* app)
{
    assert(app);
    assert(app->frame_runner.px);
    app->background_runner.io = ToriRS_IOBatch_New();
    app->background_runner.queue = ToriRS_TaskQueue_New();
    /* One IO backend, as for the packet queue. */
    app->background_runner.px = app->frame_runner.px;
    app->background_runner.parallel = 1;
    /* Which queue is which: what each may return, and what ends its pump
     * (task_runner.h; the frame contract is in app.h). */
    app->packet_runner.role = TASK_RUNNER_ROLE_MAIN;
    app->frame_runner.role = TASK_RUNNER_ROLE_PARALLEL;
    app->background_runner.role = TASK_RUNNER_ROLE_BACKGROUND;
    app->frame_runner.compute_step_us = app_parallel_compute_us();
    /* What a parallel step costs is read on the stepping thread's processor
     * clock (the lane table above). */
    TaskRunner_SetCostClock(&app->frame_runner, PlatformWindow_ThreadCpuUs);
    RebuildPace_Reset(&app->pace);
    WorldRebuildJob_Reset(&app->world_job);
    RebuildFrameStats_Reset(&app->rebuild_frames);
    /* TORIRS_REBUILD_TIMING: marks on every frame, so the frame a rebuild
     * opens in can say where all of it went (rebuild_marks_first). */
    if( WorldBuilder_TimingOn() )
        RebuildFrameStats_Arm(&app->rebuild_frames);
    TaskRunner_SetClock(&app->background_runner, app_runner_clock, NULL);
    /* The packet queue and the frame queue read the same clock: a paced
     * catch-up gives their passes a deadline too (App_PacedDeadline). */
    TaskRunner_SetClock(&app->frame_runner, app_runner_clock, NULL);
    TaskRunner_SetClock(&app->packet_runner, app_runner_clock, NULL);
    {
        char const* slow = torirs_env_task_slow_ms();
        uint64_t const slow_us = (slow && slow[0]) ? (uint64_t)atoi(slow) * 1000u : 0;

        app->background_runner.slow_task_us = slow_us;
        app->frame_runner.slow_task_us = slow_us;
        app->packet_runner.slow_task_us = slow_us;
    }
    App_WorldRebuildModeInit(app);
}

void
app_background_runner_free(struct App* app)
{
    assert(app);
    App_WorldJobCancel(app, "shutdown");
    app_world_local_load_free(app);
    ToriRS_TaskQueue_Free(app->background_runner.queue);
    ToriRS_IOBatch_Free(app->background_runner.io);
    app->background_runner.queue = NULL;
    app->background_runner.io = NULL;
}

void
App_WorldRebuildModeInit(struct App* app)
{
    char const* mode = torirs_env_world_rebuild();
    char const* deadline = torirs_env_world_job_deadline_ms();
    long deadline_ms = APP_WORLD_JOB_DEADLINE_MS_DEFAULT;
    int model;
    int background;

    assert(app);
    /*
     * What the world says: its era table (background), or its manifest's
     * `[features:boot] world_rebuild` on top of that (app.c). This runs once
     * when the queues are made and again when the features are bound; before
     * that there is no world to rebuild, and it reads blocking.
     * TORIRS_WORLD_REBUILD overrides the world's statement for one launch.
     */
    model = ToriRS_Features_Resolved(&app->features) ? app->features.world_rebuild_model
                                                     : TORIRS_WORLD_REBUILD_BLOCKING;
    if( mode && mode[0] )
    {
        int const from_env = ToriRS_Features_WorldRebuildModelByName(mode);

        if( from_env < 0 )
            TORIRS_LOG("world rebuild: TORIRS_WORLD_REBUILD=%s is not blocking|background; ignored\n", mode);
        else
            model = from_env;
    }
    background = model == TORIRS_WORLD_REBUILD_BACKGROUND;
    /*
     * A content test keys every step on the tick a state is reached and waits
     * for it with frames. A hold measured in wall time moves every packet
     * behind a REBUILD by a different number of frames on every run, which is
     * exactly what such a run must not vary; it blocks whatever the world or
     * the launch says.
     */
    if( background && ContentTest_Enabled() && !ContentTest_BackgroundRebuild() )
        background = 0;
    if( deadline && deadline[0] )
    {
        long const value = strtol(deadline, NULL, 0);
        if( value > 0 )
            deadline_ms = value;
    }
    WorldRebuildJob_Configure(&app->world_job, background, (uint64_t)deadline_ms * 1000u);
    {
        char const* budget = torirs_env_pace_budget_us();
        char const* window = torirs_env_pace_window_ms();

        uint64_t budget_us = REBUILD_PACE_BUDGET_US_DEFAULT;
        uint64_t window_us = (uint64_t)REBUILD_PACE_WINDOW_MS_DEFAULT * 1000u;

        if( budget && budget[0] )
            budget_us = (uint64_t)strtoul(budget, NULL, 0);
        if( window && window[0] )
            window_us = (uint64_t)strtoul(window, NULL, 0) * 1000u;
        RebuildPace_Configure(&app->pace, budget_us, window_us);
    }
    if( WorldRebuildJob_Background(&app->world_job) )
        TORIRS_LOG("world rebuild: background (job deadline %ld ms)\n", deadline_ms);
}

int
App_WorldJobWanted(struct App const* app)
{
    assert(app);
    /*
     * The editor's scenes are its own (app_world_load_begin), and a scene it
     * shows must be the one it just asked for.
     *
     * A first load (login, a reconnect onto nothing) has no scene to keep on
     * screen; it takes the blocking path as it always has.
     *
     * The frame latch. A REBUILD is never the first packet of its server
     * tick: on every in-game REBUILD of three rev-242 recordings (29 of 29;
     * RSProx 20261022T202619, 20261022T191126, 20261028T103055) the tick was
     * already open (RS_ServerTick_IsOpen), opened by VARP_SMALL /
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
    return WorldRebuildJob_Wanted(
        &app->world_job,
        App_MapEditorOpen(app),
        WorldLoadState_Active(&app->world_load) && app->world && app->world->load_complete,
        RS_ServerTick_IsOpen(&app->server_tick) && RS_ServerTick_OpenNonVar(&app->server_tick),
        RS_ClientScriptQueue_Count(&app->pending_clientscripts) > 0);
}

int
App_WorldJobPending(struct App const* app)
{
    assert(app);
    return WorldRebuildJob_Pending(&app->world_job);
}

int
App_WorldSettled(struct App const* app)
{
    struct SettleGateInput in;

    assert(app);
    app_settle_gate_input(app, &in);
    return SettleGate_WorldSettled(&in);
}

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
 * (WorldRebuildJob::stage_crowded).
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
    struct WorldRebuildJob* job = &app->world_job;
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
 * The staging build, a unit at a time, on background_runner: WorldBuilder_BuildStep
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

/* The scene's staged-batch step: adds sent per unit, between slice checks. A
 * unit measured ~0.05 ms (each add is an event; its bake is the renderer's). */
#define APP_WORLD_STAGE_ADDS_PER_UNIT 512

/* WorldBuilder_BuildStep's `spent`, asked after every unit but a build's last. */
static int
app_world_stage_spent(void* userdata)
{
    struct AppWorldStageSlice* s = (struct AppWorldStageSlice*)userdata;

    WorldRebuildJob_NoteBuildUnit(&s->app->world_job);
    return ToriRS_SliceSpent(s->slice) || app_world_stage_crowded(s->app);
}

static int
Task_AppWorldStage_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_AppWorldStage* self = (struct Task_AppWorldStage*)task_base;
    struct WorldRebuildJob* job = &self->app->world_job;

    PT_BEGIN(&self->pt);

    for( ;; )
    {
        {
            struct AppWorldStageSlice slice = { self->app, &io->slice };

            /* Ahead of the unit, not only after it: a build that begins
             * already short of room never touches the scene. */
            if( app_world_stage_crowded(self->app) )
            {
                WorldRebuildJob_MarkCrowded(job);
                break;
            }
            self->done = WorldBuilder_BuildStep(job->stage_builder, app_world_stage_spent, &slice);
        }
        if( self->done )
        {
            /* The build's last unit, which asks no `spent`. */
            WorldRebuildJob_NoteBuildUnit(job);
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
            WorldRebuildJob_MarkCrowded(job);
            break;
        }
        /* BuildStep stopped for the slice (it stops for nothing else). */
        assert(ToriRS_SliceSpent(&io->slice));
        PT_TASK_YIELD_IF_SPENT();
    }

    /*
     * The renderer's half of the staging: a renderer that can hold a second
     * world batch (GL3, WebGL2, GLES3) is sent the new scene's adds now, a few
     * hundred a slice, and bakes them in its own per-frame slice before the
     * swap (ToriDraw_SceneStageBatchBegin; the design is in torirs_render.h
     * at TORIRSRC_BATCH3D_STAGE_BEGIN). Every other renderer is sent the
     * whole batch at the swap, as before.
     */
    if( self->done && !job->stage_crowded &&
        ToriDraw_SceneBatchStagingSupported(self->app->scene) )
    {
        ToriDraw_SceneStageBatchBegin(self->app->scene, job->stage_builder->static_pool);
        /* Open: a live renderer switch forgets the stage
         * (ToriDraw_SceneStageBatchForget) and the adoption sends a batch. */
        while( ToriDraw_SceneStageBatchOpen(self->app->scene) &&
               !ToriDraw_SceneStageBatchStep(self->app->scene, APP_WORLD_STAGE_ADDS_PER_UNIT) )
            PT_TASK_YIELD_IF_SPENT();
        /* Then a frame at a time until the renderer has it baked and
         * uploaded -- its bake is paid in its own slice at the end of each
         * frame, not here. A promoted job (past its deadline) stops waiting:
         * the swap then takes what is ready and the rest bakes as drawn. */
        while( ToriDraw_SceneStageBatchOpen(self->app->scene) &&
               !ToriDraw_SceneStageBatchPrepared(self->app->scene) && !job->job.promoted )
        {
            WorldRebuildJob_NotePrepFrame(job);
            self->task.spent = 1;
            PT_YIELD(&self->pt);
        }
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
    struct WorldRebuildJob* job = &app->world_job;

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
    WorldRebuildJob_DropVarps(job);
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
    struct WorldRebuildJob* job = &app->world_job;
    struct Task_AppWorldStage* task;
    int estimate;

    WorldRebuildJob_StageBegin(job);

    /* The new scene will be about the size of the one it replaces. */
    estimate = ToriDraw_ScenePoolCount(app->scene, TORIDRAW_SCENE_POOL_STATIC);
    ToriPlatformWeb_Telemetry(
        "world_job_stage",
        "{\"zone_x\":%d,\"zone_z\":%d,\"live_elements\":%d,\"estimate\":%d,\"crowded\":%d}",
        job->zone_x,
        job->zone_z,
        ToriDraw_SceneElementLiveCount(app->scene),
        estimate,
        ToriDraw_SceneElementLiveCount(app->scene) + estimate + app_world_stage_headroom() >
                TORIDRAW_SCENE_MAX_ELEMENTS
            ? 1
            : 0);
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
        WorldRebuildJob_MarkCrowded(job);
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
        ToriRS_TaskJob_Start(&job->job, &app->background_runner, &task->task, job->job.must_finish_by_us);
        job->job.promoted = promoted;
    }
}

/* Move the job on as far as its ended tasks allow: a gather that has ended
 * begins the staging build. 1: everything the job runs has ended. */
static int
app_world_job_advance(struct App* app)
{
    struct WorldRebuildJob* job = &app->world_job;

    if( !WorldRebuildJob_TasksDone(job) )
        return 0;
    if( WorldRebuildJob_Phase(job) == WORLD_REBUILD_JOB_GATHER )
    {
        app_world_job_stage_begin(app);
        return WorldRebuildJob_TasksDone(job);
    }
    return 1;
}

/*
 * THE ONE PLACE A WORLD JOB ENDS: `committed` (its scene is the live one) or
 * cancelled. It is unlisted as the root view's rebuild; committed, the view's
 * built trigger fires; and the job ends as an owner, which abandons whatever
 * it primed and did not fire.
 */
static void
app_world_job_end(
    struct App* app,
    int committed)
{
    struct WorldRebuildJob* job = &app->world_job;

    App_ViewRebuildEnd(app, &job->rebuild, committed);
    ToriRS_TaskJob_End(&job->job, committed);
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
    struct WorldRebuildJob* job;
    struct Worldview* root;
    struct ToriRS_Task* gather;
    uint64_t now;

    assert(app);
    assert(chunks_xz);
    job = &app->world_job;
    _Static_assert(
        sizeof(job->zones) == PKT_MAP_REBUILD_ZONES * sizeof(job->zones[0]),
        "the job keeps the packet's grid");

    root = WorldviewRegistry_Get(&app->worldviews, WORLDVIEW_ROOT);
    now = TaskRunner_Now(&app->background_runner);

    WorldRebuildJob_Begin(job, chunks_xz, chunk_count, zone_x, zone_z, zones, now);
    /* Listed as the rebuild of the root view until its commit or cancel, and
     * the owner of the root view's built trigger for as long (the job's end,
     * app_world_job_end, is the one place it ends). */
    App_ViewRebuildBegin(
        app, &job->rebuild, WORLDVIEW_ROOT, ToriRS_TaskJobOwner(&job->job, "the background world rebuild"));
    /* The varps the scene is built against are the REBUILD's, as a build in
     * place at this packet would read them. */
    WorldRebuildJob_TakeVarps(job, &app->varps);

    gather = CreateTask_WorldGather(
        app->provider,
        root->builder,
        app->background_runner.queue,
        job->chunks,
        job->chunk_count,
        job->zone_x,
        job->zone_z,
        APP_WORLD_JOB_SCENE_SIZE,
        job->have_zones ? job->zones : NULL,
        &job->report);
    ToriRS_TaskJob_Start(&job->job, &app->background_runner, gather, WorldRebuildJob_MustFinishBy(job, now));
    /* The tick this REBUILD belongs to is open on varps alone (the only open
     * tick App_WorldJobWanted lets through); its fence is held behind us.
     * Suspend the latch for the hold, or nothing would be published until the
     * commit; the commit re-arms it for the rest of the tick. */
    WorldRebuildJob_GatherStarted(job, gather, RS_ServerTick_Suspend(&app->server_tick));
    if( WorldBuilder_TimingOn() )
        TORIRS_REPORT(
            "world_job: start zone=%d,%d squares=%d instance=%d cycle=%llu\n",
            zone_x,
            zone_z,
            chunk_count,
            job->have_zones,
            (unsigned long long)FrameClockSerial_LogicCycle(&app->frame_clock.serial));
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
    struct WorldRebuildJob* job = &app->world_job;
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
    WorldLoadState_BeginRepeat(&app->world_load);
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
    struct WorldRebuildJob* job = &app->world_job;
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
    uint64_t t_finished;
    uint64_t t_varps;
    uint64_t t_done;

    assert(WorldRebuildJob_TasksDone(job));
    assert(WorldRebuildJob_Phase(job) == WORLD_REBUILD_JOB_STAGE);
    assert(!job->stage_task);
    job->gather = NULL;
    root = WorldviewRegistry_Get(&app->worldviews, WORLDVIEW_ROOT);
    swapped = !job->stage_crowded;
    ToriPlatformWeb_Telemetry(
        "world_job_commit",
        "{\"zone_x\":%d,\"zone_z\":%d,\"swapped\":%d}",
        job->zone_x,
        job->zone_z,
        swapped ? 1 : 0);
    assert(!swapped || (job->stage_builder && !WorldBuilder_BuildActive(job->stage_builder)));

    /* The plugins' copy of the grid goes with the scene it describes, so a
     * frame of the old scene never shows it beside the new grid. */
    RS_RebuildRegion_KeepInstance(
        &app->rebuild_region, job->have_zones ? job->zones : NULL, job->zone_x, job->zone_z);

    /* The entity-shift source, as the packet task captures it. Nothing can
     * have moved the base since the packet: the pump has popped nothing. */
    had_world = WorldLoadState_Active(&app->world_load) && app->world && root->world->load_complete;
    prev_base_x = had_world ? root->world->_base_tile_x : 0;
    prev_base_z = had_world ? root->world->_base_tile_z : 0;

    /* The C2 drain rule (World_ResetSceneAlloc asserts the queue is empty,
     * and so does the swap). The old scene went on simulating while the job
     * ran, and what it removed since the packet is drained here. */
    App_WorldDrainEntityRemovedFor(app, root->world);

    t_commit = TaskRunner_Now(&app->background_runner);
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
    t_swapped = TaskRunner_Now(&app->background_runner);

    if( had_world && root->world->load_complete )
        App_WorldRebuildShift(
            app,
            root->world->_base_tile_x - prev_base_x,
            root->world->_base_tile_z - prev_base_z);
    t_shifted = TaskRunner_Now(&app->background_runner);
    RebuildFrameStats_SetCommit(
        &app->rebuild_frames,
        (double)(t_swapped - t_commit) / 1000.0,
        (double)(t_swapped - t_commit) / 1000.0,
        (double)(t_shifted - t_swapped) / 1000.0);
    /* Lands the world load, unblanking what a stalled promotion blanked. */
    App_WorldLoadFinish(app);
    t_finished = TaskRunner_Now(&app->background_runner);
    /* After the finish, where a varp written after a build in place would
     * land: its re-place runs behind the standing loc changes'. */
    if( swapped )
        varps_moved = app_world_job_varps_reresolve(app, &varps_replaced);
    t_varps = TaskRunner_Now(&app->background_runner);
    /* The old scene (the swap left it on the staging side) and everything
     * that kept the job's own: one scene resident from here on. */
    app_world_job_stage_free(app);
    t_done = TaskRunner_Now(&app->background_runner);
    if( WorldBuilder_TimingOn() )
        TORIRS_REPORT("rebuild_timing: commit finish load_finish=%.2fms varps=%.2fms stage_free=%.2fms\n",
            (double)(t_finished - t_shifted) / 1000.0, (double)(t_varps - t_finished) / 1000.0,
            (double)(t_done - t_varps) / 1000.0);
    RebuildFrameStats_SetTail(
        &app->rebuild_frames, (double)(t_done - t_swapped) / 1000.0, (double)(t_done - t_shifted) / 1000.0);
    /* The job is idle, and the held packets popped behind this are timed to
     * the pump's return. The rest of the REBUILD's tick is about to be
     * popped: latch it as the blocking flow would have, from here to its
     * SERVER_TICK_END. Counted from now, so the lost-fence backstop measures
     * this stretch, not the hold. */
    if( TaskRunner_ContractTrace() )
        fprintf(
            stderr,
            "queue_contract: trace world_job commit zone=%d,%d frames=%d committed=%d slices floor=%d slack=%d "
            "(the job's own finish: promoted=%d)\n",
            job->zone_x,
            job->zone_z,
            g_job_tally.frames,
            g_job_tally.committed,
            g_job_tally.floor_slices,
            g_job_tally.slack_slices,
            job->job.promoted);
    App_ParallelTraceGatherCommitted(app);
    g_job_tally.frames = 0;
    g_job_tally.committed = 0;
    g_job_tally.floor_slices = 0;
    g_job_tally.slack_slices = 0;
    if( WorldRebuildJob_Committed(job, t_done) )
        RS_ServerTick_Resume(
            &app->server_tick, FrameClockSerial_LogicCycle(&app->frame_clock.serial));

    if( WorldBuilder_TimingOn() )
        TORIRS_REPORT(
            "world_job: commit zone=%d,%d wall=%.1fms gather=%.1fms slices=%d slice_sum=%.1fms "
            "slice_max=%.2fms promoted=%d mode=%s build_slices=%d build_units=%d "
            "build_slice_max=%.2fms build_slice_sum=%.1fms swap=%.2fms shift=%.2fms finish=%.2fms "
            "varps_moved=%d replaced=%d trims_held=%d resumes=%d chunks=%d locs=%d models=%d textures=%d seqs=%d "
            "prep_frames=%d staged=%d\n",
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
            job->report.seqs,
            job->prep_frames,
            ToriDraw_SceneBatchStagingSupported(app->scene));
    /* The rebuild of the root view has finished, its finish included. */
    app_world_job_end(app, 1);
    app_world_job_repeat_requested(app);
}

void
app_world_job_held_note(struct App* app)
{
    uint64_t held_t0_us;

    assert(app);
    if( !WorldRebuildJob_HeldClose(&app->world_job, &held_t0_us) )
        return;
    RebuildFrameStats_SetHeld(
        &app->rebuild_frames, (double)(TaskRunner_Now(&app->background_runner) - held_t0_us) / 1000.0);
}

int
App_WorldJobPoll(struct App* app)
{
    struct WorldRebuildJob* job;

    assert(app);
    job = &app->world_job;
    if( !WorldRebuildJob_Pending(job) )
        return 1;
    if( !app_world_job_advance(app) )
    {
        uint64_t const now = TaskRunner_Now(&app->background_runner);
        uint64_t t_after;

        if( !WorldRebuildJob_Overdue(job, now) )
            return 0;
        /*
         * Past its deadline: stop waiting for slices and finish it here, in
         * this frame, with no limit -- the rest of the gather and the whole
         * staging build. On the desktop every read is answered as it is asked
         * for, so this runs to the end.
         */
        if( WorldRebuildJob_Promote(job) )
            TORIRS_LOG(
                "world_job: zone %d,%d not %s after %llu ms; finishing it now\n",
                job->zone_x,
                job->zone_z,
                WorldRebuildJob_Phase(job) == WORLD_REBUILD_JOB_GATHER ? "gathered" : "built",
                (unsigned long long)((now - job->t_begin_us) / 1000u));
        /* The owner finishing its own job: the one place the background
         * queue is stepped from inside the packet pump, and it says so. */
        app_background_entry_check(app, 1);
        (void)TaskRunner_RunUntil(&app->background_runner, 0);
        if( app_world_job_advance(app) == 0 && WorldRebuildJob_Phase(job) == WORLD_REBUILD_JOB_STAGE )
            (void)TaskRunner_RunUntil(&app->background_runner, 0);
        t_after = TaskRunner_Now(&app->background_runner);
        WorldRebuildJob_NoteSlice(job, t_after - now, 0);
        if( !app_world_job_advance(app) )
        {
            /*
             * Waiting on reads only the platform can answer (JS5, the
             * browser): spinning here would answer nothing. Say what a
             * blocking rebuild says while it waits -- the viewport's
             * "Loading - please wait." -- and come back next pump.
             */
            if( WorldRebuildJob_Stall(job) )
            {
                WorldLoadState_Stall(&app->world_load);
                RedrawRequest_Raise(&app->redraw_request);
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
    struct WorldRebuildJob* job;

    assert(app);
    assert(why);
    job = &app->world_job;
    if( !WorldRebuildJob_Pending(job) )
        return;
    ToriPlatformWeb_Telemetry(
        "world_job_cancel",
        "{\"why\":\"%s\",\"phase\":%d,\"zone_x\":%d,\"zone_z\":%d}",
        why,
        (int)job->phase,
        job->zone_x,
        job->zone_z);
    if( !WorldRebuildJob_TasksDone(job) )
    {
        if( WorldRebuildJob_Phase(job) == WORLD_REBUILD_JOB_GATHER )
        {
            assert(job->gather);
            TaskWorldLoad_Cancel(app->background_runner.queue, job->gather);
        }
        else
        {
            /* The stage task owns no read; the queue may unlink it at any
             * yield, which ends the job it is counted on. */
            assert(job->stage_task);
            ToriRS_TaskQueue_Remove(app->background_runner.queue, job->stage_task);
            assert(!job->stage_task);
        }
    }
    assert(WorldRebuildJob_TasksDone(job));
    if( WorldRebuildJob_Phase(job) == WORLD_REBUILD_JOB_STAGE && WorldBuilder_TimingOn() && job->stage_builder )
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
    /* Idle, with no tick suspended: the tick ends with the session (or is
     * superseded by the load replacing this scene); nothing of it is left to
     * latch. Nothing was built, so nothing is acknowledged; and the zone the
     * job was for was never the scene's, so a REBUILD naming it again must not
     * be skipped as the same zone. */
    WorldLoadState_Cancel(&app->world_load, WorldRebuildJob_Cancelled(job));
    app_world_job_end(app, 0);
    RS_RebuildRegion_ForgetCentre(&app->rebuild_region);
    RebuildFrameStats_Drop(&app->rebuild_frames);
    RedrawRequest_Raise(&app->redraw_request);
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
    if( jobs_seen != drop_at || WorldRebuildJob_TasksDone(&app->world_job) )
        return;
    if( drop_in_build &&
        (WorldRebuildJob_Phase(&app->world_job) != WORLD_REBUILD_JOB_STAGE || app->world_job.build_units < drop_units) )
        return;
    drop_at = -1;
    {
        int queued = 0;
        for( struct ToriRS_Task const* task = app->background_runner.queue->head; task; task = task->next )
            queued++;
        TORIRS_REPORT(
            "world_job: TORIRS_WORLD_JOB_DROP_AT severing the session mid-%s (job %ld, %d "
            "slice(s), %d build unit(s), %d task(s) queued)\n",
            WorldRebuildJob_Phase(&app->world_job) == WORLD_REBUILD_JOB_STAGE ? "build" : "gather",
            jobs_seen,
            app->world_job.slices,
            app->world_job.build_units,
            queued);
    }
    app_net_lost(app, "TORIRS_WORLD_JOB_DROP_AT");
}


void
app_world_job_contract_frame(
    struct App* app,
    int committed)
{
    assert(app);
    if( !WorldRebuildJob_Pending(&app->world_job) )
        return;
    g_job_tally.frames++;
    if( committed )
        g_job_tally.committed++;
}

void
App_RunBackground(
    struct App* app,
    uint64_t deadline_us)
{
    struct WorldRebuildJob* job;
    uint64_t t0;
    uint64_t t1;
    int building;

    assert(app);
    /* The common case, every frame: nothing queued, nothing read. */
    if( !app->background_runner.queue->head )
        return;
    app_background_entry_check(app, 0);
    t0 = TaskRunner_Now(&app->background_runner);
    /* A slice already over still gets its pump: every task on the queue is
     * stepped once whatever the clock says (the pump guarantee), so the
     * background queue moves on a frame that missed its deadline. */
    if( deadline_us <= t0 )
        deadline_us = t0 + 1;
    job = &app->world_job;
    building = WorldRebuildJob_Pending(job) && WorldRebuildJob_Phase(job) == WORLD_REBUILD_JOB_STAGE;
    (void)TaskRunner_RunUntil(&app->background_runner, deadline_us);
    /* A gather that ended in this slice hands the rest of it to the build. */
    if( WorldRebuildJob_Pending(job) && WorldRebuildJob_Phase(job) == WORLD_REBUILD_JOB_GATHER &&
        !app_world_job_advance(app) && WorldRebuildJob_Phase(job) == WORLD_REBUILD_JOB_STAGE &&
        TaskRunner_Now(&app->background_runner) < deadline_us )
    {
        building = 1;
        (void)TaskRunner_RunUntil(&app->background_runner, deadline_us);
    }
    t1 = TaskRunner_Now(&app->background_runner);
    if( WorldRebuildJob_Pending(job) )
    {
        if( g_job_tally.in_floor )
            g_job_tally.floor_slices++;
        else
            g_job_tally.slack_slices++;
        WorldRebuildJob_NoteSlice(job, t1 - t0, building);
        app_world_job_drop_requested(app);
    }
}

/* TORIRS_QUEUE_CONTRACT_TRACE: the longest single step the background queue
 * has run, each time it grows -- one step is the unit by which a background
 * pump can overrun its slice, since every task gets one whatever the clock
 * says. */
static void
app_background_step_report(struct App* app)
{
    static uint64_t reported_us;

    assert(app);
    if( !TaskRunner_ContractTrace() || app->background_runner.longest_step_us <= reported_us )
        return;
    reported_us = app->background_runner.longest_step_us;
    fprintf(
        stderr,
        "queue_contract: trace background longest step %.2f ms (%s)\n",
        (double)reported_us / 1000.0,
        app->background_runner.longest_step_name[0] ? app->background_runner.longest_step_name : "(unnamed)");
}

void
app_run_background_floor(struct App* app)
{
    assert(app);
    /* The models a swap buried (WorldBuilder_HotSwap): freed in chunks while
     * the floor's slice lasts. A chunk of 512 measured ~0.07 ms. */
    if( app->scene && app->scene->graveyard_count > 0 )
    {
        uint64_t const until = PlatformWindow_TicksUs() + APP_GRAVEYARD_SLICE_US;
        while( ToriDraw_SceneGraveyardDrain(app->scene, 512) > 0 && PlatformWindow_TicksUs() < until )
            ;
    }
    if( !app->background_runner.queue->head )
        return;
    app_contract_note_background_floor();
    g_job_tally.in_floor = 1;
    App_RunBackground(app, PlatformWindow_TicksUs() + APP_BACKGROUND_FLOOR_US);
    g_job_tally.in_floor = 0;
    app_background_step_report(app);
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
        RS_ServerTick_Serial(&app->server_tick),
        (unsigned long long)FrameClockSerial_LogicCycle(&app->frame_clock.serial),
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
    if( every > 0 && RS_ServerTick_Serial(&app->server_tick) % (unsigned)every == 0 )
        app_world_digest_report(app, "tick");
}

/* --- TORIRS_REBUILD_TIMING's frame window --------------------------------- */

/* The frames after the window: what the renderer's tail cost them. */
static void
app_rebuild_frames_after_report(struct App* app)
{
    struct RebuildFrameStats const* stats = &app->rebuild_frames;
    char series[REBUILD_FRAME_STATS_AFTER * 8];
    int over8;
    int over16;
    unsigned max_tenths;

    RebuildFrameStats_CountOver(stats->after, stats->after_frames, &over8, &over16, &max_tenths);
    RebuildFrameStats_FormatSeries(stats->after, stats->after_frames, series, sizeof(series));
    TORIRS_REPORT(
        "rebuild_after: mode=%s zone=%d,%d frames=%d max=%u.%ums over8.3=%d over16.7=%d series=%s\n",
        stats->background ? "background" : "blocking",
        RS_RebuildRegion_CentreX(&app->rebuild_region),
        RS_RebuildRegion_CentreZ(&app->rebuild_region),
        stats->after_frames,
        max_tenths / 10u,
        max_tenths % 10u,
        over8,
        over16,
        series);
}

void
app_rebuild_frames_open(
    struct App* app,
    int background)
{
    assert(app);
    if( !WorldBuilder_TimingOn() )
        return;
    if( RebuildFrameStats_AfterPending(&app->rebuild_frames) )
        app_rebuild_frames_after_report(app);
    RebuildFrameStats_Open(&app->rebuild_frames, background);
}

void
app_rebuild_mark(
    struct App* app,
    char const* name)
{
    assert(app);
    assert(name);
    /* Kept through the tail too (rebuild_marks_after). */
    if( !RebuildFrameStats_WantsMark(&app->rebuild_frames) )
        return;
    RebuildFrameStats_Mark(&app->rebuild_frames, name, PlatformWindow_TicksUs());
}

void
app_rebuild_frames_finished(struct App* app)
{
    assert(app);
    RebuildFrameStats_Finished(&app->rebuild_frames);
}

void
app_rebuild_frames_note(
    struct App* app,
    uint64_t frame_us)
{
    struct RebuildFrameStats const* stats;
    int due;

    assert(app);
    stats = &app->rebuild_frames;
    due = RebuildFrameStats_Note(&app->rebuild_frames, frame_us);
    /* The frame the rebuild started in, whole: what the platform loop did
     * before App_RunOnce (the net poll, and with it an in-process server's
     * tick), App_RunOnce mark by mark, and what the loop did after it (the
     * draw, audio) up to this note. */
    if( (due & REBUILD_FRAME_STATS_FIRST) && stats->marks > 0 )
    {
        char line[REBUILD_FRAME_STATS_MARKS * 40];
        uint64_t const now_us = PlatformWindow_TicksUs();
        uint64_t const start_us = now_us >= frame_us ? now_us - frame_us : 0;
        uint64_t const first_us = stats->mark_us[0];
        uint64_t const last_us = stats->mark_us[stats->marks - 1];

        RebuildFrameStats_FormatMarks(stats, 0.3, line, sizeof(line));
        TORIRS_REPORT(
            "rebuild_marks_first: mode=%s zone=%d,%d %.1fms: before RunOnce=%.2f RunOnce=%.2f after RunOnce=%.2f | "
            "RunOnce:%s\n",
            stats->background ? "background" : "blocking",
            RS_RebuildRegion_CentreX(&app->rebuild_region),
            RS_RebuildRegion_CentreZ(&app->rebuild_region),
            (double)frame_us / 1000.0,
            first_us > start_us ? (double)(first_us - start_us) / 1000.0 : 0.0,
            (double)(last_us - first_us) / 1000.0,
            now_us > last_us ? (double)(now_us - last_us) / 1000.0 : 0.0,
            line);
    }
    /* A slow frame in the tail says where its RunOnce went, as the commit
     * frame does (rebuild_marks). */
    if( due & REBUILD_FRAME_STATS_AFTER_SLOW )
    {
        char line[REBUILD_FRAME_STATS_MARKS * 40];

        RebuildFrameStats_FormatMarks(stats, 0.3, line, sizeof(line));
        TORIRS_REPORT("rebuild_marks_after: zone=%d,%d +%d %.1fms RunOnce:%s\n",
            RS_RebuildRegion_CentreX(&app->rebuild_region),
            RS_RebuildRegion_CentreZ(&app->rebuild_region), stats->after_frames,
            (double)frame_us / 1000.0, line);
    }
    if( due & REBUILD_FRAME_STATS_AFTER_DONE )
        app_rebuild_frames_after_report(app);
    if( !(due & REBUILD_FRAME_STATS_CLOSED) )
        return;
    /* The frame that finished the rebuild is the last one counted: for the
     * blocking flow that is usually the only one; for a job it is the commit. */
    TORIRS_REPORT(
        "rebuild_frames: mode=%s zone=%d,%d frames=%d max=%.2fms last=%.2fms sum=%.2fms "
        "hist(<2,<5,<10,<20,<50,>=50ms)=%d,%d,%d,%d,%d,%d build=%.2fms tail=%.2fms "
        "swap=%.2fms shift=%.2fms finish=%.2fms held=%.2fms\n",
        stats->background ? "background" : "blocking",
        RS_RebuildRegion_CentreX(&app->rebuild_region),
        RS_RebuildRegion_CentreZ(&app->rebuild_region),
        stats->frames,
        (double)stats->max_us / 1000.0,
        (double)stats->last_us / 1000.0,
        (double)stats->sum_us / 1000.0,
        stats->hist[0],
        stats->hist[1],
        stats->hist[2],
        stats->hist[3],
        stats->hist[4],
        stats->hist[5],
        stats->build_ms,
        stats->tail_ms,
        stats->swap_ms,
        stats->shift_ms,
        stats->finish_ms,
        stats->held_ms);
    {
        char series[REBUILD_FRAME_STATS_SERIES * 8];
        int over8;
        int over16;
        unsigned max_tenths;
        int const n = RebuildFrameStats_SeriesCount(stats);

        RebuildFrameStats_CountOver(stats->series, n, &over8, &over16, &max_tenths);
        RebuildFrameStats_FormatSeries(stats->series, n, series, sizeof(series));
        TORIRS_REPORT("rebuild_series: mode=%s zone=%d,%d frames=%d over8.3=%d over16.7=%d series=%s\n",
            stats->background ? "background" : "blocking",
            RS_RebuildRegion_CentreX(&app->rebuild_region),
            RS_RebuildRegion_CentreZ(&app->rebuild_region),
            stats->frames,
            over8,
            over16,
            series);
    }
    if( stats->marks > 1 )
    {
        char line[REBUILD_FRAME_STATS_MARKS * 40];

        RebuildFrameStats_FormatMarks(stats, 0.0, line, sizeof(line));
        TORIRS_REPORT("rebuild_marks: zone=%d,%d last frame's RunOnce:%s\n",
            RS_RebuildRegion_CentreX(&app->rebuild_region),
            RS_RebuildRegion_CentreZ(&app->rebuild_region), line);
    }
}
