/*
 * THE WORKED EXAMPLE of a parallel task (game/rs_parallel_task.h): the shape
 * every family writes, on a parallel runner with a fake assets loader.
 *
 * Task_PaintTile is what a packet handler would add for "put model M on tile
 * T once M is here": it carries its own data (the model, a label, the view,
 * the scene serial and the arrival cycle in its header), and its run
 * function reads top to bottom -- await the model (and the next link of a
 * chain), exit if cancelled, wait its turn behind an earlier task for the
 * same tile, check the game still wants it (its scene, how late it is), and
 * apply. The game it applies to is a few counters here.
 *
 * Pins: it applies when the asset arrives and not before; a pending one is
 * found by kind and key while it lives and not after; a newer packet
 * cancelling it makes it exit at once, without waiting for the load it was
 * on; a cancelled scope makes every task in it exit; a scene that moved on
 * without a cancel and a task too late both exit at the body's check; a
 * failed load applies the "absent" policy; a chain awaits twice; an asset
 * already here applies on the first pass; two tasks for one key apply in
 * order even when the second's asset arrives first.
 *
 * Run: make -C src test-parallel-task
 */

#include "asyncio.h"
#include "engine/asset_requests.h"
#include "game/rs_parallel_task.h"
#include "game/rs_pending.h"
#include "mem/torirs_mem.h"
#include "task_runner.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* platform_x_io.h keeps this opaque; nothing here reads through it. */
struct PlatformX_IO
{
    int unused;
};

void
PlatformX_IO_Pump(struct PlatformX_IO* px)
{
    (void)px;
}

int
PlatformX_IO_Process(
    struct PlatformX_IO* px,
    struct ToriRS_IOBatch* io)
{
    (void)px;
    ToriRS_IOBatch_Reset(io);
    return 0;
}

/* ---- the game, the assets table and its fake loader ---------------------- */

#define LOG_MAX 32

struct Example
{
    struct TaskRunner runner;
    struct PlatformX_IO px;
    struct Assets* assets;
    struct RS_Pending* index;
    /* The scene serial of views 0 and 1, and the client's cycle. */
    uint32_t scene_serial[2];
    uint32_t now_cycle;
    /* The loader: models it was asked for, and models that are here. */
    int started[LOG_MAX];
    int started_count;
    int resident[LOG_MAX];
    int resident_count;
    /* What applied, and what exited and why, in order. */
    char log[LOG_MAX][32];
    int log_count;
};

static enum AssetStart
fake_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    struct Example* ex = data;
    (void)assets;
    assert(ex->started_count < LOG_MAX);
    ex->started[ex->started_count++] = key.id;
    return ASSET_START_LOADING;
}

static bool
fake_resident(
    void* data,
    struct AssetKey key)
{
    struct Example* ex = data;
    for( int i = 0; i < ex->resident_count; i++ )
        if( ex->resident[i] == key.id )
            return true;
    return false;
}

/* The read for model `id` came back. */
static void
arrive(
    struct Example* ex,
    int id,
    bool ok)
{
    if( ok )
        ex->resident[ex->resident_count++] = id;
    Assets_Finished(ex->assets, AssetKey_Make(ASSET_KIND_MODEL, id, 0), ok);
}

static void
note(
    struct Example* ex,
    char const* label,
    char const* what)
{
    assert(ex->log_count < LOG_MAX);
    snprintf(ex->log[ex->log_count++], sizeof(ex->log[0]), "%s:%s", label, what);
}

static bool
logged(
    struct Example const* ex,
    int at,
    char const* entry)
{
    return at < ex->log_count && strcmp(ex->log[at], entry) == 0;
}

/* Step the parallel queue until nothing more can progress, as the frame
 * does. */
static void
pump(struct Example* ex)
{
    for( int i = 0; i < 64; i++ )
        if( TaskRunner_Step(&ex->runner) != TASK_RUNNER_PROGRESSED )
            return;
    assert(0 && "the queue never settled");
}

static void
example_init(struct Example* ex)
{
    struct AssetLoader loader;

    memset(ex, 0, sizeof(*ex));
    ex->runner.queue = ToriRS_TaskQueue_New();
    ex->runner.io = ToriRS_IOBatch_New();
    ex->runner.px = &ex->px;
    ex->runner.parallel = 1;
    ex->assets = Assets_New();
    loader.start = fake_start;
    loader.resident = fake_resident;
    loader.data = ex;
    Assets_SetLoader(ex->assets, ASSET_KIND_MODEL, &loader);
    ex->index = RS_Pending_New();
    ex->scene_serial[0] = 5;
    ex->scene_serial[1] = 9;
}

static void
example_free(struct Example* ex)
{
    TEST_ASSERT(ex->runner.queue->head == NULL, "every task ended");
    TEST_ASSERT(RS_Pending_Listed(ex->index) == 0, "and unlisted itself");
    ToriRS_TaskQueue_Free(ex->runner.queue);
    ToriRS_IOBatch_Free(ex->runner.io);
    RS_Pending_Free(ex->index);
    Assets_Free(ex->assets);
}

/* ---- THE TASK ------------------------------------------------------------ */

enum
{
    KIND_PAINT_TILE = RS_PENDING_KINDS_TEST_FIRST,
};

struct Task_PaintTile
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct Example* ex;
    /* Its own data, copied from the packet. */
    int model_id;
    /* A chain: the model the first one names, -1 for none. */
    int next_model_id;
    /* Dropped when it would apply more than this many cycles after its
     * packet; 0 for no limit. */
    uint32_t late_limit;
    char label[8];
};

static int
Task_PaintTile_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_PaintTile* self = (struct Task_PaintTile*)base;
    struct Example* ex = self->ex;
    (void)io;

    PT_BEGIN(&self->pt);
    /* 1. Wait for what it needs. */
    PT_PARALLEL_AWAIT_ASSET(ex->assets, AssetKey_Make(ASSET_KIND_MODEL, self->model_id, 0));
    if( self->next_model_id >= 0 && RS_ParallelTask_AssetHere(&self->par) )
        PT_PARALLEL_AWAIT_ASSET(
            ex->assets, AssetKey_Make(ASSET_KIND_MODEL, self->next_model_id, 0));
    /* 2. Cancelled or superseded while it waited. */
    if( RS_ParallelTask_Cancelled(&self->par) )
    {
        note(ex, self->label, "cancelled");
        PT_EXIT(&self->pt);
    }
    /* An earlier task for this tile applies first. */
    PT_PARALLEL_AWAIT_TURN(ex->index);
    if( RS_ParallelTask_Cancelled(&self->par) )
    {
        note(ex, self->label, "cancelled");
        PT_EXIT(&self->pt);
    }
    /* 3. Does the game still want it? */
    if( ex->scene_serial[self->par.view] != self->par.scene_serial )
    {
        note(ex, self->label, "scene");
        PT_EXIT(&self->pt);
    }
    if( self->late_limit && RS_ParallelTask_LateCycles(&self->par, ex->now_cycle) > self->late_limit )
    {
        note(ex, self->label, "late");
        PT_EXIT(&self->pt);
    }
    /* 4. Apply, reading the asset from the cache now. The policy for a
     * model that failed: the tile draws nothing. */
    if( !RS_ParallelTask_AssetHere(&self->par) )
    {
        note(ex, self->label, "absent");
        PT_EXIT(&self->pt);
    }
    assert(fake_resident(ex, AssetKey_Make(ASSET_KIND_MODEL, self->model_id, 0)));
    note(ex, self->label, "applied");
    PT_END(&self->pt);
}

static void
Task_PaintTile_Free(struct ToriRS_Task* base)
{
    struct Task_PaintTile* self = (struct Task_PaintTile*)base;
    RS_ParallelTask_Release(self->ex->index, self->ex->assets, &self->par);
    free(self);
}

static struct ToriRS_TaskVTable Task_PaintTile_VTable = {
    .run = Task_PaintTile_Run,
    .free = Task_PaintTile_Free,
};

static struct RS_PendingKey
tile_key(
    int view,
    int x)
{
    return RS_PendingKey_View(view, x, 0, 0, 0, 1);
}

/* What the packet handler does: make the task with its own data and add it.
 * `supersede`: a newer packet for the tile replaces what is pending there. */
static struct Task_PaintTile*
packet_paint_tile(
    struct Example* ex,
    char const* label,
    int view,
    int x,
    int model_id,
    bool supersede)
{
    struct Task_PaintTile* t = calloc(1, sizeof(*t));

    assert(t);
    t->task.vtable = &Task_PaintTile_VTable;
    strcpy(t->task.name, "PaintTile");
    PT_INIT(&t->pt);
    t->ex = ex;
    t->model_id = model_id;
    t->next_model_id = -1;
    snprintf(t->label, sizeof(t->label), "%s", label);
    if( supersede )
        (void)RS_Pending_CancelKey(ex->index, KIND_PAINT_TILE, tile_key(view, x));
    RS_ParallelTask_Add(
        ex->runner.queue,
        ex->index,
        &t->task,
        &t->par,
        KIND_PAINT_TILE,
        tile_key(view, x),
        view,
        ex->scene_serial[view],
        ex->now_cycle);
    return t;
}

/* ---- the cases ----------------------------------------------------------- */

static void
test_applies_when_the_asset_arrives(void)
{
    struct Example ex;
    struct Task_PaintTile* t;

    example_init(&ex);
    t = packet_paint_tile(&ex, "A", 0, 1, 100, false);
    pump(&ex);
    TEST_ASSERT(ex.log_count == 0, "nothing applies before the model is here");
    TEST_ASSERT(ex.started_count == 1 && ex.started[0] == 100, "one load started");
    TEST_ASSERT(RS_Pending_Find(ex.index, KIND_PAINT_TILE, tile_key(0, 1)) == &t->par,
        "a later packet finds it pending by kind and key");
    pump(&ex);
    TEST_ASSERT(ex.log_count == 0, "and pumping again does nothing");
    arrive(&ex, 100, true);
    pump(&ex);
    TEST_ASSERT(logged(&ex, 0, "A:applied") && ex.log_count == 1, "applied once it arrived");
    TEST_ASSERT(RS_Pending_Find(ex.index, KIND_PAINT_TILE, tile_key(0, 1)) == NULL,
        "and is no longer pending");
    example_free(&ex);
}

static void
test_already_here_applies_at_once(void)
{
    struct Example ex;

    example_init(&ex);
    ex.resident[ex.resident_count++] = 100;
    packet_paint_tile(&ex, "A", 0, 1, 100, false);
    pump(&ex);
    TEST_ASSERT(logged(&ex, 0, "A:applied"), "a resident model applies on the first pass");
    TEST_ASSERT(ex.started_count == 0, "with no load");
    example_free(&ex);
}

static void
test_a_newer_one_cancels_it(void)
{
    struct Example ex;

    example_init(&ex);
    packet_paint_tile(&ex, "A", 0, 1, 100, true);
    pump(&ex);
    /* The next packet for the same tile names another model. */
    packet_paint_tile(&ex, "B", 0, 1, 101, true);
    pump(&ex);
    TEST_ASSERT(logged(&ex, 0, "A:cancelled") && ex.log_count == 1,
        "the older exits at once, without waiting for its load");
    TEST_ASSERT(Assets_InFlight(ex.assets, AssetKey_Make(ASSET_KIND_MODEL, 100, 0)),
        "its load cannot be stopped mid-read");
    arrive(&ex, 100, true);
    pump(&ex);
    TEST_ASSERT(ex.log_count == 1, "and its arrival applies nothing");
    arrive(&ex, 101, true);
    pump(&ex);
    TEST_ASSERT(logged(&ex, 1, "B:applied") && ex.log_count == 2, "the newer applies");
    example_free(&ex);
}

static void
test_a_cancelled_scope(void)
{
    struct Example ex;

    example_init(&ex);
    packet_paint_tile(&ex, "C", 0, 1, 100, false);
    packet_paint_tile(&ex, "D", 0, 2, 100, false);
    packet_paint_tile(&ex, "E", 1, 1, 100, false);
    pump(&ex);
    /* A rebuild of view 0's scene. */
    TEST_ASSERT(RS_Pending_Cancel(ex.index, RS_PendingScope_Scene(0, 5)) == 2,
        "the rebuild cancels the two tasks of that scene");
    pump(&ex);
    TEST_ASSERT(logged(&ex, 0, "C:cancelled") && logged(&ex, 1, "D:cancelled") && ex.log_count == 2,
        "both exit");
    arrive(&ex, 100, true);
    pump(&ex);
    TEST_ASSERT(logged(&ex, 2, "E:applied") && ex.log_count == 3, "the other view's applies");
    example_free(&ex);
}

static void
test_the_scene_moved_on(void)
{
    struct Example ex;

    example_init(&ex);
    packet_paint_tile(&ex, "F", 0, 1, 100, false);
    pump(&ex);
    /* A rebuild finished with nobody cancelling: the body's own check. */
    ex.scene_serial[0] = 6;
    arrive(&ex, 100, true);
    pump(&ex);
    TEST_ASSERT(logged(&ex, 0, "F:scene") && ex.log_count == 1, "exits: not its scene any more");
    example_free(&ex);
}

static void
test_too_late(void)
{
    struct Example ex;
    struct Task_PaintTile* g;
    struct Task_PaintTile* h;

    example_init(&ex);
    ex.now_cycle = 1000;
    g = packet_paint_tile(&ex, "G", 0, 1, 100, false);
    g->late_limit = 50;
    h = packet_paint_tile(&ex, "H", 0, 2, 101, false);
    h->late_limit = 50;
    pump(&ex);
    ex.now_cycle = 1030;
    arrive(&ex, 101, true);
    pump(&ex);
    TEST_ASSERT(logged(&ex, 0, "H:applied"), "30 cycles late is within its limit");
    ex.now_cycle = 1080;
    arrive(&ex, 100, true);
    pump(&ex);
    TEST_ASSERT(logged(&ex, 1, "G:late") && ex.log_count == 2, "80 cycles late is dropped");
    example_free(&ex);
}

static void
test_a_failed_load(void)
{
    struct Example ex;

    example_init(&ex);
    packet_paint_tile(&ex, "K", 0, 1, 100, false);
    pump(&ex);
    arrive(&ex, 100, false);
    pump(&ex);
    TEST_ASSERT(logged(&ex, 0, "K:absent") && ex.log_count == 1,
        "the task learns the load failed and applies its policy");
    example_free(&ex);
}

static void
test_a_chain(void)
{
    struct Example ex;
    struct Task_PaintTile* t;

    example_init(&ex);
    t = packet_paint_tile(&ex, "L", 0, 1, 100, false);
    t->next_model_id = 200;
    pump(&ex);
    arrive(&ex, 100, true);
    pump(&ex);
    TEST_ASSERT(ex.log_count == 0 && ex.started_count == 2 && ex.started[1] == 200,
        "the first link names the second, which is awaited next");
    arrive(&ex, 200, true);
    pump(&ex);
    TEST_ASSERT(logged(&ex, 0, "L:applied"), "applied after the last link");
    example_free(&ex);
}

static void
test_two_for_one_key_apply_in_order(void)
{
    struct Example ex;

    example_init(&ex);
    /* A loc and then its replacement, both to be applied: no supersede. */
    packet_paint_tile(&ex, "I", 0, 1, 100, false);
    packet_paint_tile(&ex, "J", 0, 1, 101, false);
    pump(&ex);
    arrive(&ex, 101, true);
    pump(&ex);
    TEST_ASSERT(ex.log_count == 0, "the second's model came first, but it waits its turn");
    arrive(&ex, 100, true);
    pump(&ex);
    TEST_ASSERT(logged(&ex, 0, "I:applied") && logged(&ex, 1, "J:applied") && ex.log_count == 2,
        "the first applies, then the second, in the same pump");

    /* A cancelled earlier one does not hold the later: M was made in the old
     * scene, N for the same tile in the rebuilt one, and the rebuild cancels
     * the old scene's tasks. */
    packet_paint_tile(&ex, "M", 0, 2, 300, false);
    ex.scene_serial[0] = 6;
    packet_paint_tile(&ex, "N", 0, 2, 301, false);
    pump(&ex);
    TEST_ASSERT(RS_Pending_Cancel(ex.index, RS_PendingScope_Scene(0, 5)) == 1, "M only");
    arrive(&ex, 301, true);
    pump(&ex);
    TEST_ASSERT(logged(&ex, 2, "M:cancelled") && logged(&ex, 3, "N:applied") && ex.log_count == 4,
        "with the earlier cancelled, the later applies without it");
    arrive(&ex, 300, true);
    pump(&ex);
    example_free(&ex);
}

int
main(void)
{
    ToriMem_Init();
    test_applies_when_the_asset_arrives();
    test_already_here_applies_at_once();
    test_a_newer_one_cancels_it();
    test_a_cancelled_scope();
    test_the_scene_moved_on();
    test_too_late();
    test_a_failed_load();
    test_a_chain();
    test_two_for_one_key_apply_in_order();
    if( g_failures )
    {
        printf("rs_parallel_task_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("rs_parallel_task_test: all passed\n");
    ToriMem_Shutdown();
    return 0;
}
