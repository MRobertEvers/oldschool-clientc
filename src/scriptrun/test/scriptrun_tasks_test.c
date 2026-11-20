/*
 * The headless client's parallel queue (scriptrun/scriptrun_tasks.h) running
 * the loc tasks both clients share (game/rs_loc_tasks.h), against a bot whose
 * loc table is a recording stand-in: a loc change applies in the pump after
 * its packet; a LOC_MERGE hides at start+1 and restores at end+1 counted on
 * the bot's cycle (the real client's test-loc-tasks, "a merge fires in its own
 * view"); a rebuild drops a pending merge, a zone reset cancels it, a later
 * change for its key replaces it; a loc whose config the cache cannot serve is
 * dropped; nothing is left at a tick's end but a wait for a cycle; freeing the
 * bot frees a merge still waiting. Asserts on.
 */
#include "engine/torirs_types.h"
#include "game/rs_loc_tasks.h"
#include "mem/torirs_mem.h"
#include "scriptrun/scriptrun_core.h"
#include "scriptrun/scriptrun_tasks.h"
#include "scriptrun/scriptrun_zone.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                      \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

enum
{
    BASE_X = 3200,
    BASE_Z = 3200,
    SHAPE_WALL = 0,
    LAYER_WALL = 0,
    MAP_LOC = 2001,
    NO_CONFIG_LOC = 4321,
    PLACED_MAX = 64,
};

/* ---- the bot's loc table, recorded ----------------------------------------- */

struct Placed
{
    int view, x, z, level, shape, loc_id;
};

static struct Placed s_placed[PLACED_MAX];
static int s_placed_count;

void
ScriptrunCore_LocPlace(
    struct ScriptrunCore* core,
    int view,
    int abs_x,
    int abs_z,
    int level,
    int shape,
    int angle,
    int loc_id,
    int op_flags,
    const char ops[5][32])
{
    (void)core;
    (void)angle;
    (void)op_flags;
    (void)ops;
    assert(s_placed_count < PLACED_MAX);
    s_placed[s_placed_count++] = (struct Placed){ view, abs_x, abs_z, level, shape, loc_id };
}

/* The map put MAP_LOC, a wall, on every tile. */
int
ScriptrunCore_MapLocAt(
    struct ScriptrunCore const* core,
    int abs_x,
    int abs_z,
    int level,
    int layer,
    int* out_shape,
    int* out_angle)
{
    (void)core;
    (void)abs_x;
    (void)abs_z;
    (void)level;
    if( layer != LAYER_WALL )
        return -1;
    *out_shape = SHAPE_WALL;
    *out_angle = 0;
    return MAP_LOC;
}

int
ScriptrunCore_LocCacheLevel(
    struct ScriptrunCore const* core,
    int sx,
    int sz,
    int zone_level)
{
    (void)core;
    (void)sx;
    (void)sz;
    return zone_level;
}

/* Every loc is a plain one (no models, no chain) but NO_CONFIG_LOC, which the
 * cache cannot serve. */
static struct ToriRS_Location s_loc;

struct ToriRS_Location*
ScriptrunCore_LocConfig(int loc_id)
{
    if( loc_id < 0 || loc_id == NO_CONFIG_LOC )
        return NULL;
    s_loc.id = loc_id;
    s_loc.seq_id = -1;
    return &s_loc;
}

/* ---- the bot -------------------------------------------------------------- */

static struct ScriptrunCore* s_core;

static void
bot_new(void)
{
    s_core = ToriMem_Calloc(ToriMem_ServerPlayers(), 1, sizeof(*s_core));
    assert(s_core);
    s_core->world = World_New();
    assert(s_core->world);
    VarPManager_Init(&s_core->model.varps);
    ScriptrunZone_Init(&s_core->zone, (struct RS_EventSink){ 0 });
    ScriptrunZone_Rebuild(&s_core->zone, BASE_X, BASE_Z, 104);
    s_core->tasks = ScriptrunTasks_New(s_core);
    s_placed_count = 0;
}

static void
bot_free(void)
{
    ScriptrunTasks_Free(s_core->tasks);
    ScriptrunZone_Free(&s_core->zone);
    World_Free(s_core->world);
    ToriMem_Free(ToriMem_ServerPlayers(), s_core);
    s_core = NULL;
}

/* Server tick `tick` begins: the cycle clock reads tick * 30 and the queue is
 * pumped (ScriptrunCore_BeginTick's share). */
static void
tick(int t)
{
    s_core->tick = t;
    s_core->world->cycle = (uint64_t)t * SCRIPTRUN_CYCLES_PER_TICK;
    ScriptrunTasks_BeginTick(s_core->tasks, (uint32_t)s_core->world->cycle);
}

/* The tick's SERVER_TICK_END: pumped, then nothing but a cycle wait left. */
static void
tick_end(void)
{
    ScriptrunTasks_Pump(s_core->tasks);
    ScriptrunTasks_AssertIdle(s_core->tasks);
}

static struct World_LocChange const*
record_at(
    int x,
    int z,
    int level,
    int layer)
{
    struct World const* world = s_core->zone.world;
    struct World_LocChange const* found = NULL;

    for( int i = 0; i < world->loc_change_count; i++ )
    {
        struct World_LocChange const* rec = &world->loc_changes[i];
        if( rec->x == x && rec->z == z && rec->level == level && rec->layer == layer )
            found = rec;
    }
    return found;
}

static void
change(
    int x,
    int z,
    int loc_id)
{
    static const char none[5][32] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };
    struct RS_LocHost const host = ScriptrunTasks_LocHost(s_core->tasks);
    struct RS_LocChange c;

    RS_LocChange_Fill(&c, x, z, 0, loc_id, SHAPE_WALL, 0, 0x1f, none);
    (void)RS_LocChange_Add(&host, 0, &c, NULL, (unsigned)s_core->tick, 1);
}

static void
merge(
    int x,
    int z,
    int start,
    int end)
{
    struct RS_LocHost const host = ScriptrunTasks_LocHost(s_core->tasks);

    RS_LocMerge_Add(&host, 0, x, z, 0, SHAPE_WALL, start, end);
}

/* ---- the tests ------------------------------------------------------------- */

static void
test_a_change_applies_in_the_pump_after_its_packet(void)
{
    struct World_LocChange const* rec;

    printf("scriptrun tasks: a loc change applies in the pump after its packet\n");
    bot_new();
    tick(10);
    change(20, 21, 1010);
    CHECK(s_placed_count == 0, "added, not yet run");
    ScriptrunTasks_Pump(s_core->tasks);
    CHECK(s_placed_count == 1 && s_placed[0].loc_id == 1010 && s_placed[0].x == BASE_X + 20 &&
              s_placed[0].z == BASE_Z + 21 && s_placed[0].view == 0,
        "placed at its absolute tile");
    rec = record_at(20, 21, 0, LAYER_WALL);
    CHECK(rec && rec->new_type == 1010 && rec->old_type == MAP_LOC && rec->end_time < 0,
        "its record keeps the map's loc as the old one");
    CHECK(RS_Pending_Listed(ScriptrunTasks_LocHost(s_core->tasks).pending) == 0, "the task ended");
    tick_end();
    bot_free();
}

static void
test_a_merge_hides_at_start_and_restores_at_end(void)
{
    struct World_LocChange const* rec;

    printf("scriptrun tasks: a LOC_MERGE hides at start+1 and restores at end+1\n");
    bot_new();
    tick(10);
    /* Hide due at cycle 303, restore at 366. */
    merge(5, 6, 2, 65);
    ScriptrunTasks_Pump(s_core->tasks);
    CHECK(s_placed_count == 0, "not before its start");
    tick_end();
    tick(11); /* cycle 330 */
    CHECK(s_placed_count == 1 && s_placed[0].loc_id == -1 && s_placed[0].x == BASE_X + 5,
        "the loc is hidden at the first tick past start+1");
    rec = record_at(5, 6, 0, LAYER_WALL);
    CHECK(rec && rec->new_type == -1 && rec->old_type == MAP_LOC, "the hide's record");
    CHECK(rec && rec->end_time == 366 - 330, "a merge's record, which a rebuild drops");
    tick_end();
    tick(12); /* cycle 360 */
    CHECK(s_placed_count == 1, "still hidden before end+1");
    tick_end();
    tick(13); /* cycle 390 */
    CHECK(s_placed_count == 2 && s_placed[1].loc_id == MAP_LOC, "restored: the map's loc is back");
    rec = record_at(5, 6, 0, LAYER_WALL);
    CHECK(rec && rec->new_type == MAP_LOC && rec->end_time < 0, "a standing record again");
    CHECK(RS_Pending_Listed(ScriptrunTasks_LocHost(s_core->tasks).pending) == 0, "the merge and its restore ended");
    tick_end();
    bot_free();
}

static void
test_a_rebuild_drops_a_pending_merge(void)
{
    printf("scriptrun tasks: a rebuild drops a pending merge\n");
    bot_new();
    tick(10);
    merge(5, 6, 2, 65);
    tick_end();
    ScriptrunTasks_SceneBuilt(s_core->tasks, 0);
    tick(11);
    tick(13);
    CHECK(s_placed_count == 0, "the old scene's merge never hides or restores");
    CHECK(RS_Pending_Listed(ScriptrunTasks_LocHost(s_core->tasks).pending) == 0, "and ended");
    bot_free();
}

static void
test_a_zone_reset_and_a_newer_change_cancel_a_merge(void)
{
    struct RS_LocHost host;

    printf("scriptrun tasks: a zone reset, or a newer change for its key, cancels a merge\n");
    bot_new();
    host = ScriptrunTasks_LocHost(s_core->tasks);
    tick(10);
    merge(17, 18, 2, 65);
    merge(25, 18, 2, 65);
    merge(40, 40, 2, 65);
    CHECK(RS_LocTasks_CancelZone(&host, 0, 16, 16, 0) == 1, "the merge in the zone is cancelled");
    change(40, 40, 1012);
    ScriptrunTasks_Pump(s_core->tasks);
    CHECK(s_placed_count == 1 && s_placed[0].loc_id == 1012, "the newer change applies at once");
    tick(11);
    CHECK(s_placed_count == 2 && s_placed[1].x == BASE_X + 25 && s_placed[1].loc_id == -1,
        "only the other zone's merge hides");
    tick_end();
    bot_free();
}

static void
test_a_loc_the_cache_cannot_serve_is_dropped(void)
{
    printf("scriptrun tasks: a loc whose config the cache cannot serve is dropped\n");
    bot_new();
    ScriptrunTasks_FailAsset(s_core->tasks, ASSET_KIND_CONFIG, NO_CONFIG_LOC);
    tick(10);
    change(30, 30, NO_CONFIG_LOC);
    ScriptrunTasks_Pump(s_core->tasks);
    CHECK(s_placed_count == 0 && record_at(30, 30, 0, LAYER_WALL) == NULL,
        "nothing placed, nothing recorded (the real client's rule)");
    CHECK(RS_Pending_Listed(ScriptrunTasks_LocHost(s_core->tasks).pending) == 0, "the task ended");
    tick_end();
    bot_free();
}

static void
test_freeing_the_bot_frees_a_waiting_merge(void)
{
    printf("scriptrun tasks: freeing the bot frees a merge still waiting\n");
    bot_new();
    tick(10);
    merge(5, 6, 200, 400);
    tick_end();
    CHECK(RS_Pending_Listed(ScriptrunTasks_LocHost(s_core->tasks).pending) == 1, "the merge waits on the cycle");
    bot_free();
}

int
main(void)
{
    test_a_change_applies_in_the_pump_after_its_packet();
    test_a_merge_hides_at_start_and_restores_at_end();
    test_a_rebuild_drops_a_pending_merge();
    test_a_zone_reset_and_a_newer_change_cancel_a_merge();
    test_a_loc_the_cache_cannot_serve_is_dropped();
    test_freeing_the_bot_frees_a_waiting_merge();
    printf("scriptrun_tasks_test: all passed\n");
    return 0;
}
