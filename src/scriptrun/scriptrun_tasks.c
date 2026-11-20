/*
 * The headless client's parallel queue (scriptrun_tasks.h): a TaskRunner in
 * the parallel role, a pending index, an assets table whose loaders answer at
 * once, the cycle clock, and the bot's half of the loc tasks
 * (game/rs_loc_tasks.h).
 */
#include "scriptrun/scriptrun_tasks.h"

#include "asyncio.h"
#include "engine/asset_requests.h"
#include "game/rs_parallel_task.h"
#include "game/rs_pending.h"
#include "mem/torirs_mem.h"
#include "platform/platform_x_io.h"
#include "scriptrun/scriptrun_core.h"
#include "task_runner.h"
#include "task_trigger.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    /* Assets a test said the cache cannot serve. */
    SCRIPTRUN_TASKS_FAILED_MAX = 64,
    /* A runaway backstop on one pump, never reached by a real one. */
    SCRIPTRUN_TASKS_PUMP_LIMIT = 1 << 20,
};

struct ScriptrunTasks
{
    struct ScriptrunCore* core;
    struct TaskRunner runner;
    struct RS_Pending* pending;
    struct Assets* assets;
    struct ToriRS_Clock clock;
    /* Bumped when a view's scene is (re)built or the view goes. */
    uint32_t scene_serial[SCRIPTRUN_TASK_VIEWS];
    struct AssetKey failed[SCRIPTRUN_TASKS_FAILED_MAX]; /* variant unused */
    int failed_count;
};

/* ---- the assets: answered at once ---------------------------------------- */

static bool
tasks_failed(
    struct ScriptrunTasks const* tasks,
    struct AssetKey key)
{
    for( int i = 0; i < tasks->failed_count; i++ )
        if( tasks->failed[i].kind == key.kind && tasks->failed[i].id == key.id )
            return true;
    return false;
}

static bool
asset_resident(
    void* data,
    struct AssetKey key)
{
    return !tasks_failed(data, key);
}

/* Reached only for a key that is not resident: one the cache cannot serve. */
static enum AssetStart
asset_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    (void)assets;
    (void)data;
    (void)key;
    assert(tasks_failed(data, key));
    return ASSET_START_FAILED;
}

/* ---- the queue ------------------------------------------------------------ */

struct ScriptrunTasks*
ScriptrunTasks_New(struct ScriptrunCore* core)
{
    struct ScriptrunTasks* tasks;
    struct AssetLoader loader;

    assert(core);
    tasks = ToriMem_Calloc(ToriMem_ServerPlayers(), 1, sizeof(*tasks));
    assert(tasks);
    tasks->core = core;
    tasks->runner.queue = ToriRS_TaskQueue_New();
    assert(tasks->runner.queue);
    tasks->runner.io = ToriRS_IOBatch_New();
    assert(tasks->runner.io);
    /* A bot's tasks never read (their assets are answered at once): the
     * server's stand-in IO (scriptrun_stubs.c) aborts on a read. */
    tasks->runner.px = PlatformX_IO_New();
    assert(tasks->runner.px);
    tasks->runner.parallel = 1;
    tasks->runner.role = TASK_RUNNER_ROLE_PARALLEL;
    tasks->pending = RS_Pending_New();
    assert(tasks->pending);
    tasks->assets = Assets_New();
    assert(tasks->assets);
    loader.start = asset_start;
    loader.resident = asset_resident;
    loader.data = tasks;
    for( int kind = 0; kind < ASSET_KIND_COUNT; kind++ )
        Assets_SetLoader(tasks->assets, (enum AssetKind)kind, &loader);
    ToriRS_Clock_Init(&tasks->clock, "a bot's server tick cycle", 0);
    return tasks;
}

void
ScriptrunTasks_Free(struct ScriptrunTasks* tasks)
{
    if( !tasks )
        return;
    /* As the real client closes down: the table ignores the joins the freed
     * tasks drop, then the queue frees its tasks, each unlisting itself. */
    Assets_Close(tasks->assets);
    ToriRS_TaskQueue_Free(tasks->runner.queue);
    ToriRS_IOBatch_Free(tasks->runner.io);
    PlatformX_IO_Free(tasks->runner.px);
    RS_Pending_Free(tasks->pending);
    Assets_Free(tasks->assets);
    ToriRS_Clock_Retire(&tasks->clock);
    ToriMem_Free(ToriMem_ServerPlayers(), tasks);
}

void
ScriptrunTasks_Pump(struct ScriptrunTasks* tasks)
{
    int steps;
    enum TaskRunnerStat stat;

    assert(tasks);
    if( !tasks->runner.queue->head )
        return;
    stat = TaskRunner_PumpToStandstill(&tasks->runner, SCRIPTRUN_TASKS_PUMP_LIMIT, &steps);
    assert(stat != TASK_RUNNER_PROGRESSED);
    (void)stat;
}

void
ScriptrunTasks_BeginTick(
    struct ScriptrunTasks* tasks,
    uint32_t cycle)
{
    assert(tasks);
    ToriRS_Clock_Advance(&tasks->clock, cycle);
    ScriptrunTasks_Pump(tasks);
}

void
ScriptrunTasks_AssertIdle(struct ScriptrunTasks const* tasks)
{
    assert(tasks);
    for( struct ToriRS_Task const* t = tasks->runner.queue->head; t; t = t->next )
    {
        char chain[512];

        if( ToriRS_TaskChainFormat(t, chain, sizeof(chain)) == TORIRS_CHAIN_CYCLE )
            continue;
        fprintf(stderr, "scriptrun: a task the bot can never answer is left at tick end: %s\n", chain);
        abort();
    }
}

void
ScriptrunTasks_SceneBuilt(
    struct ScriptrunTasks* tasks,
    int view)
{
    assert(tasks);
    assert(view >= 0);
    assert(view < SCRIPTRUN_TASK_VIEWS);
    tasks->scene_serial[view]++;
}

void
ScriptrunTasks_CancelAll(struct ScriptrunTasks* tasks)
{
    assert(tasks);
    (void)RS_Pending_Cancel(tasks->pending, RS_PendingScope_All());
    ScriptrunTasks_Pump(tasks);
}

void
ScriptrunTasks_FailAsset(
    struct ScriptrunTasks* tasks,
    int kind,
    int id)
{
    assert(tasks);
    assert(kind >= 0);
    assert(kind < ASSET_KIND_COUNT);
    assert(tasks->failed_count < SCRIPTRUN_TASKS_FAILED_MAX);
    tasks->failed[tasks->failed_count++] = AssetKey_Make((enum AssetKind)kind, id, 0);
}

/* ---- the loc tasks' host ----------------------------------------------------- */

static uint32_t
loc_cycle(void* ctx)
{
    return (uint32_t)((struct ScriptrunTasks*)ctx)->core->world->cycle;
}

static unsigned
loc_tick(void* ctx)
{
    return (unsigned)((struct ScriptrunTasks*)ctx)->core->tick;
}

static void
loc_add(
    void* ctx,
    struct ToriRS_Task* task,
    struct RS_ParallelTask* par,
    int kind,
    struct RS_PendingKey key,
    int view)
{
    struct ScriptrunTasks* tasks = ctx;

    assert(view >= 0);
    assert(view < SCRIPTRUN_TASK_VIEWS);
    RS_ParallelTask_Add(
        tasks->runner.queue, tasks->pending, task, par, kind, key, view,
        tasks->scene_serial[view], loc_cycle(ctx));
}

static void
loc_release(
    void* ctx,
    struct RS_ParallelTask* par)
{
    struct ScriptrunTasks* tasks = ctx;

    RS_ParallelTask_Release(tasks->pending, tasks->assets, par);
}

static uint32_t
loc_view_gen(
    void* ctx,
    int view)
{
    struct ScriptrunCore const* core = ((struct ScriptrunTasks*)ctx)->core;

    assert(view >= 0);
    assert(view < SCRIPTRUN_VIEWS);
    return view == 0 ? 0u : (uint32_t)core->views[view].wire_id;
}

/* The world a view's loc change records are kept in: the zone's root world,
 * or the deck's own (scriptrun_zone.h). */
static struct World*
loc_view_world(
    void* ctx,
    int view)
{
    struct ScriptrunCore* core = ((struct ScriptrunTasks*)ctx)->core;
    struct ScriptrunView const* v;

    if( view == 0 )
        return core->zone.world;
    v = &core->views[view];
    return ScriptrunZone_ViewWorld(
        &core->zone, view, v->base_x, v->base_z, v->size_x > v->size_z ? v->size_x : v->size_z);
}

static int
loc_cache_level(
    void* ctx,
    int view,
    int x,
    int z,
    int zone_level)
{
    /* A deck's bridges are not modelled: its walked plane is its cache
     * level. */
    if( view != 0 )
        return zone_level;
    return ScriptrunCore_LocCacheLevel(((struct ScriptrunTasks*)ctx)->core, x, z, zone_level);
}

static struct ToriRS_Location*
loc_location(
    void* ctx,
    int loc_id)
{
    (void)ctx;
    return ScriptrunCore_LocConfig(loc_id);
}

static struct VarPManager const*
loc_varps(void* ctx)
{
    return &((struct ScriptrunTasks*)ctx)->core->model.varps;
}

static struct World*
loc_world(
    void* ctx,
    struct RS_ParallelTask const* par,
    char const* owner)
{
    struct ScriptrunTasks* tasks = ctx;
    struct ScriptrunCore const* core = tasks->core;

    (void)owner; /* a drop is not said here */
    if( tasks->scene_serial[par->view] != par->scene_serial )
        return NULL;
    if( par->view != 0 &&
        (!core->views[par->view].live || (uint32_t)core->views[par->view].wire_id != par->key.gen) )
        return NULL;
    return loc_view_world(ctx, par->view);
}

void
ScriptrunTasks_LocRecord(
    struct ScriptrunCore const* core,
    struct ScriptrunZone* zone,
    struct World* world,
    int view,
    struct RS_LocChange const* c,
    int end_time)
{
    int const layer = World_LocShapeToLayer(c->shape);
    int const origin_x = view == 0 ? world->_base_tile_x : core->views[view].base_x;
    int const origin_z = view == 0 ? world->_base_tile_z : core->views[view].base_z;
    int old_loc, old_shape = -1, old_angle = 0;

    old_loc = ScriptrunCore_MapLocAt(
        core, origin_x + c->x, origin_z + c->z, c->level, layer, &old_shape, &old_angle);
    if( view == 0 )
        zone->loc_changes_seen++;
    World_LocChangePushOps(
        world, c->level, layer, c->x, c->z, old_loc, old_angle, old_shape, c->loc_id, c->angle,
        c->shape, (int)core->world->cycle, end_time, c->op_flags & 0x1f, c->ops);
}

/* Place into the bot's loc table and collision map (ScriptrunCore_LocPlace),
 * then keep the record. */
static void
loc_place(
    void* ctx,
    int view,
    struct World* world,
    struct RS_LocChange const* c,
    int end_time,
    int carried,
    int carried_seq)
{
    struct ScriptrunCore* core = ((struct ScriptrunTasks*)ctx)->core;
    int const origin_x = view == 0 ? world->_base_tile_x : core->views[view].base_x;
    int const origin_z = view == 0 ? world->_base_tile_z : core->views[view].base_z;

    (void)carried;
    (void)carried_seq;
    /* The record first: its old loc is the map's, read before the place. */
    ScriptrunTasks_LocRecord(core, &core->zone, world, view, c, end_time);
    ScriptrunCore_LocPlace(
        core, view, origin_x + c->x, origin_z + c->z, c->level, c->shape, c->angle, c->loc_id,
        c->op_flags & 0x1f, c->ops);
}

static struct RS_LocHostOps const k_loc_ops = {
    .cycle = loc_cycle,
    .tick = loc_tick,
    .add = loc_add,
    .release = loc_release,
    .changed = NULL,
    .view_gen = loc_view_gen,
    .view_world = loc_view_world,
    .cache_level = loc_cache_level,
    .location = loc_location,
    .varps = loc_varps,
    /* Every view's scene is built at its packet: nothing waits for one. */
    .built_trigger = NULL,
    .scene_building = NULL,
    .world = loc_world,
    .place = loc_place,
    .applied = NULL,
};

struct RS_LocHost
ScriptrunTasks_LocHost(struct ScriptrunTasks* tasks)
{
    struct RS_LocHost host;

    assert(tasks);
    host.ops = &k_loc_ops;
    host.ctx = tasks;
    host.pending = tasks->pending;
    host.assets = tasks->assets;
    host.queue = tasks->runner.queue;
    host.clock = &tasks->clock;
    return host;
}
