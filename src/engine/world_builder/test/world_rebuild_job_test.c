/*
 * The background world rebuild's record, driven with made-up times, task
 * pointers and varps: no runner, no scene, no App.
 * `make -C src test-world-rebuild-job`.
 */
#include "engine/world_builder/world_rebuild_job.h"

#include <stdio.h>
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

enum
{
    DEADLINE_US = 600000,
    START_AT = 1000000,
};

/* Stand-ins for the gather task: only their addresses are kept. */
static struct ToriRS_Task* const GATHER = (struct ToriRS_Task*)(uintptr_t)0x1000;

static int const CHUNKS[] = { 50, 50, 50, 51, 51, 50 };

static void
configured(
    struct WorldRebuildJob* job,
    int background)
{
    memset(job, 0x5a, sizeof(*job));
    WorldRebuildJob_Reset(job);
    WorldRebuildJob_Configure(job, background, DEADLINE_US);
}

/* A job through its gather's start: pending, GATHER, its task counted. */
static void
gathering(
    struct WorldRebuildJob* job,
    int32_t const* zones,
    int tick_suspended)
{
    WorldRebuildJob_Begin(job, CHUNKS, 3, 400, 410, zones, START_AT);
    /* What ToriRS_TaskJob_Start records. */
    job->job.join = 1;
    job->job.must_finish_by_us = WorldRebuildJob_MustFinishBy(job, START_AT);
    job->job.promoted = 0;
    WorldRebuildJob_GatherStarted(job, GATHER, tick_suspended);
}

static void
test_a_reset_job_is_idle_and_blocking(void)
{
    struct WorldRebuildJob job;

    configured(&job, 0);
    WorldRebuildJob_Reset(&job);
    TEST_ASSERT(!WorldRebuildJob_Pending(&job), "a reset job is not pending");
    TEST_ASSERT(WorldRebuildJob_Phase(&job) == WORLD_REBUILD_JOB_IDLE, "a reset job is idle");
    TEST_ASSERT(!WorldRebuildJob_Background(&job), "a reset job is blocking");
    TEST_ASSERT(WorldRebuildJob_TasksDone(&job), "a reset job runs nothing");
    TEST_ASSERT(!WorldRebuildJob_Wanted(&job, 0, 1, 0, 0), "blocking mode never wants a job");
}

static void
test_the_wanted_rule(void)
{
    struct WorldRebuildJob job;

    configured(&job, 1);
    TEST_ASSERT(WorldRebuildJob_Background(&job), "configured background");
    TEST_ASSERT(WorldRebuildJob_Wanted(&job, 0, 1, 0, 0), "a loaded scene, a varp tick, nothing held");
    TEST_ASSERT(!WorldRebuildJob_Wanted(&job, 1, 1, 0, 0), "not for the editor");
    TEST_ASSERT(!WorldRebuildJob_Wanted(&job, 0, 0, 0, 0), "not onto no scene");
    TEST_ASSERT(!WorldRebuildJob_Wanted(&job, 0, 1, 1, 0), "not in a tick open on more than varps");
    TEST_ASSERT(!WorldRebuildJob_Wanted(&job, 0, 1, 0, 1), "not with a clientscript held");
    gathering(&job, NULL, 0);
    TEST_ASSERT(!WorldRebuildJob_Wanted(&job, 0, 1, 0, 0), "not while one is pending");

    configured(&job, 0);
    TEST_ASSERT(!WorldRebuildJob_Wanted(&job, 0, 1, 0, 0), "blocking mode wants nothing");
}

static void
test_begin_records_the_zone_and_clears_the_last_job(void)
{
    struct WorldRebuildJob job;
    int32_t zones[WORLD_REBUILD_JOB_ZONES];

    configured(&job, 1);
    for( int i = 0; i < WORLD_REBUILD_JOB_ZONES; i++ )
        zones[i] = i * 7;
    job.slices = 9;
    job.slice_us_sum = 9;
    job.slice_us_max = 9;
    job.build_slices = 9;
    job.build_slice_us_sum = 9;
    job.build_slice_us_max = 9;
    job.build_units = 9;
    job.prep_frames = 9;
    job.stage_crowded = 1;
    job.held_open = 1;
    job.stalled = 1;
    job.report.done = 1;
    job.report.locs = 9;

    WorldRebuildJob_Begin(&job, CHUNKS, 3, 400, 410, zones, START_AT);
    TEST_ASSERT(job.zone_x == 400 && job.zone_z == 410, "the centre zone");
    TEST_ASSERT(job.chunk_count == 3, "the square count");
    TEST_ASSERT(memcmp(job.chunks, CHUNKS, sizeof(CHUNKS)) == 0, "the squares");
    TEST_ASSERT(job.have_zones, "an instance grid was given");
    TEST_ASSERT(memcmp(job.zones, zones, sizeof(zones)) == 0, "the grid is copied");
    TEST_ASSERT(job.t_begin_us == START_AT, "the start time");
    TEST_ASSERT(job.slices == 0 && job.slice_us_sum == 0 && job.slice_us_max == 0, "slices cleared");
    TEST_ASSERT(
        job.build_slices == 0 && job.build_slice_us_sum == 0 && job.build_slice_us_max == 0,
        "build slices cleared");
    TEST_ASSERT(job.build_units == 0 && job.prep_frames == 0, "units and prep frames cleared");
    TEST_ASSERT(!job.stage_crowded && !job.held_open && !job.stalled, "flags cleared");
    TEST_ASSERT(!job.report.done && job.report.locs == 0, "the report cleared");
    TEST_ASSERT(!WorldRebuildJob_Pending(&job), "not pending before its gather starts");

    zones[0] = -1;
    TEST_ASSERT(job.zones[0] == 0, "the caller's grid is not kept by reference");

    configured(&job, 1);
    job.zones[5] = 1234;
    WorldRebuildJob_Begin(&job, CHUNKS, 2, -1, -1, NULL, START_AT);
    TEST_ASSERT(!job.have_zones, "no grid");
    TEST_ASSERT(job.zones[5] == 1234, "no grid leaves the old one alone");
    TEST_ASSERT(job.zone_x == -1, "a square list load");
}

static void
test_the_phases(void)
{
    struct WorldRebuildJob job;

    configured(&job, 1);
    gathering(&job, NULL, 1);
    TEST_ASSERT(WorldRebuildJob_Pending(&job), "pending once its gather starts");
    TEST_ASSERT(WorldRebuildJob_Phase(&job) == WORLD_REBUILD_JOB_GATHER, "gathering");
    TEST_ASSERT(job.gather == GATHER, "the gather is kept");
    TEST_ASSERT(job.tick_suspended == 1, "the suspended tick is kept");
    TEST_ASSERT(!WorldRebuildJob_TasksDone(&job), "the gather is running");
    TEST_ASSERT(
        job.job.must_finish_by_us == START_AT + DEADLINE_US, "the deadline counts from the start");

    job.job.join = 0;
    TEST_ASSERT(WorldRebuildJob_TasksDone(&job), "the gather ended");
    WorldRebuildJob_StageBegin(&job);
    TEST_ASSERT(WorldRebuildJob_Phase(&job) == WORLD_REBUILD_JOB_STAGE, "staging");
    TEST_ASSERT(job.gather == NULL, "the gather is gone");
    TEST_ASSERT(WorldRebuildJob_Pending(&job), "still pending while it stages");

    TEST_ASSERT(WorldRebuildJob_Committed(&job, START_AT + 5000) == 1, "the commit resumes the tick");
    TEST_ASSERT(!WorldRebuildJob_Pending(&job), "committed: not pending");
    TEST_ASSERT(WorldRebuildJob_Phase(&job) == WORLD_REBUILD_JOB_IDLE, "committed: idle");
    TEST_ASSERT(job.tick_suspended == 0, "committed: the tick is no longer suspended");

    configured(&job, 1);
    gathering(&job, NULL, 0);
    job.job.join = 0;
    WorldRebuildJob_StageBegin(&job);
    TEST_ASSERT(WorldRebuildJob_Committed(&job, START_AT) == 0, "no tick to resume");
}

static void
test_the_held_window(void)
{
    struct WorldRebuildJob job;
    uint64_t t0 = 7;

    configured(&job, 1);
    TEST_ASSERT(!WorldRebuildJob_HeldClose(&job, &t0), "nothing held before a commit");
    TEST_ASSERT(t0 == 7, "nothing written");
    gathering(&job, NULL, 0);
    job.job.join = 0;
    WorldRebuildJob_StageBegin(&job);
    (void)WorldRebuildJob_Committed(&job, 123456);
    TEST_ASSERT(WorldRebuildJob_HeldClose(&job, &t0), "the commit opened the held window");
    TEST_ASSERT(t0 == 123456, "timed from the commit");
    TEST_ASSERT(!WorldRebuildJob_HeldClose(&job, &t0), "closed once");

    gathering(&job, NULL, 0);
    TEST_ASSERT(!WorldRebuildJob_HeldClose(&job, &t0), "a new job forgets the last held window");
}

static void
test_the_deadline_promotion_and_stall(void)
{
    struct WorldRebuildJob job;

    configured(&job, 1);
    gathering(&job, NULL, 0);
    TEST_ASSERT(!WorldRebuildJob_Overdue(&job, START_AT + DEADLINE_US - 1), "before the deadline");
    TEST_ASSERT(WorldRebuildJob_Overdue(&job, START_AT + DEADLINE_US), "at the deadline");
    TEST_ASSERT(WorldRebuildJob_Promote(&job) == 1, "promoted the first time");
    TEST_ASSERT(job.job.promoted == 1, "the task job records it");
    TEST_ASSERT(WorldRebuildJob_Promote(&job) == 0, "promoted once");
    TEST_ASSERT(WorldRebuildJob_Stall(&job) == 1, "stalled the first time");
    TEST_ASSERT(WorldRebuildJob_Stall(&job) == 0, "stalled once");

    job.job.join = 0;
    WorldRebuildJob_StageBegin(&job);
    (void)WorldRebuildJob_Committed(&job, START_AT);
    TEST_ASSERT(job.stalled == 0, "the commit clears the stall");
}

static void
test_the_slice_counts(void)
{
    struct WorldRebuildJob job;

    configured(&job, 1);
    gathering(&job, NULL, 0);
    WorldRebuildJob_NoteSlice(&job, 300, 0);
    WorldRebuildJob_NoteSlice(&job, 900, 0);
    WorldRebuildJob_NoteSlice(&job, 500, 1);
    WorldRebuildJob_NoteSlice(&job, 200, 1);
    TEST_ASSERT(job.slices == 4, "every slice is counted");
    TEST_ASSERT(job.slice_us_sum == 1900, "and summed");
    TEST_ASSERT(job.slice_us_max == 900, "and the longest kept");
    TEST_ASSERT(job.build_slices == 2, "the build's own slices are counted");
    TEST_ASSERT(job.build_slice_us_sum == 700, "and summed");
    TEST_ASSERT(job.build_slice_us_max == 500, "and the longest kept");

    WorldRebuildJob_NoteBuildUnit(&job);
    WorldRebuildJob_NoteBuildUnit(&job);
    WorldRebuildJob_NotePrepFrame(&job);
    TEST_ASSERT(job.build_units == 2, "build units");
    TEST_ASSERT(job.prep_frames == 1, "prep frames");
    TEST_ASSERT(!job.stage_crowded, "not crowded");
    WorldRebuildJob_MarkCrowded(&job);
    TEST_ASSERT(job.stage_crowded, "crowded");
}

static void
test_a_cancel(void)
{
    struct WorldRebuildJob job;

    configured(&job, 1);
    gathering(&job, NULL, 1);
    job.job.join = 0;
    TEST_ASSERT(WorldRebuildJob_Cancelled(&job) == 0, "it had not stalled");
    TEST_ASSERT(!WorldRebuildJob_Pending(&job), "cancelled: not pending");
    TEST_ASSERT(WorldRebuildJob_Phase(&job) == WORLD_REBUILD_JOB_IDLE, "cancelled: idle");
    TEST_ASSERT(job.gather == NULL, "cancelled: no gather");
    TEST_ASSERT(job.tick_suspended == 0, "cancelled: the tick is not resumed later");

    gathering(&job, NULL, 0);
    TEST_ASSERT(WorldRebuildJob_Stall(&job), "stalls");
    job.job.join = 0;
    WorldRebuildJob_StageBegin(&job);
    TEST_ASSERT(WorldRebuildJob_Cancelled(&job) == 1, "a stalled job says so");
    TEST_ASSERT(job.stalled == 0, "and the stall is cleared");
}

static void
test_the_staged_varps(void)
{
    struct WorldRebuildJob job;
    struct VarPManager live;
    struct VarPType types[3];
    int values[3] = { 4, 5, 6 };
    int serv[3] = { 1, 1, 1 };

    configured(&job, 1);
    memset(&live, 0, sizeof(live));
    memset(types, 0, sizeof(types));
    live.varp_types = types;
    live.varp_type_count = 3;
    live.varp_count = 3;
    live.var = values;
    live.var_serv = serv;
    live.var_long_count = 2;
    live.change_userdata = &live;
    live.server_update_userdata = &live;

    WorldRebuildJob_DropVarps(&job);
    TEST_ASSERT(!job.stage_varps_taken, "dropping nothing is nothing");

    WorldRebuildJob_TakeVarps(&job, &live);
    TEST_ASSERT(job.stage_varps_taken, "taken");
    TEST_ASSERT(job.stage_varps.varp_types == types, "the type tables are shared");
    TEST_ASSERT(job.stage_varps.varp_count == 3, "the count is the live one");
    TEST_ASSERT(job.stage_varps.var != values, "the values are the job's own");
    TEST_ASSERT(memcmp(job.stage_varps.var, values, sizeof(values)) == 0, "copied");
    TEST_ASSERT(job.stage_varps.var_serv == NULL, "no server values");
    TEST_ASSERT(job.stage_varps.var_long_count == 0, "no long arm");
    TEST_ASSERT(job.stage_varps.change_userdata == NULL, "no change callback");
    TEST_ASSERT(job.stage_varps.server_update_userdata == NULL, "no server callback");
    values[1] = 99;
    TEST_ASSERT(job.stage_varps.var[1] == 5, "a later write does not reach the copy");

    WorldRebuildJob_DropVarps(&job);
    TEST_ASSERT(!job.stage_varps_taken, "dropped");
    TEST_ASSERT(job.stage_varps.var == NULL, "the values are freed");

    live.varp_count = 0;
    WorldRebuildJob_TakeVarps(&job, &live);
    TEST_ASSERT(job.stage_varps_taken, "taken with no values");
    TEST_ASSERT(job.stage_varps.var == NULL, "nothing to copy");
    WorldRebuildJob_DropVarps(&job);
}

int
main(void)
{
    test_a_reset_job_is_idle_and_blocking();
    test_the_wanted_rule();
    test_begin_records_the_zone_and_clears_the_last_job();
    test_the_phases();
    test_the_held_window();
    test_the_deadline_promotion_and_stall();
    test_the_slice_counts();
    test_a_cancel();
    test_the_staged_varps();
    if( g_failures )
    {
        printf("world_rebuild_job_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("world_rebuild_job_test: ok\n");
    return 0;
}
