#include "engine/world_builder/world_rebuild_job.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <string.h>

void
WorldRebuildJob_Reset(struct WorldRebuildJob* job)
{
    assert(job);
    memset(job, 0, sizeof(*job));
}

void
WorldRebuildJob_Configure(
    struct WorldRebuildJob* job,
    int background,
    uint64_t deadline_us)
{
    assert(job);
    job->background = background;
    job->deadline_us = deadline_us;
}

int
WorldRebuildJob_Background(struct WorldRebuildJob const* job)
{
    assert(job);
    return job->background;
}

int
WorldRebuildJob_Wanted(
    struct WorldRebuildJob const* job,
    int editor,
    int scene_loaded,
    int tick_open_nonvar,
    int scripts_held)
{
    assert(job);
    if( !job->background )
        return 0;
    if( editor )
        return 0;
    if( !scene_loaded )
        return 0;
    if( tick_open_nonvar )
        return 0;
    if( scripts_held )
        return 0;
    /* Unreachable while one is pending -- the pump pops nothing -- but a
     * second job would overwrite the first's record. */
    return !job->pending;
}

int
WorldRebuildJob_Pending(struct WorldRebuildJob const* job)
{
    assert(job);
    return job->pending;
}

int
WorldRebuildJob_Phase(struct WorldRebuildJob const* job)
{
    assert(job);
    return job->phase;
}

int
WorldRebuildJob_TasksDone(struct WorldRebuildJob const* job)
{
    assert(job);
    return ToriRS_TaskJob_Done(&job->job);
}

void
WorldRebuildJob_Begin(
    struct WorldRebuildJob* job,
    int const* chunks_xz,
    int chunk_count,
    int zone_x,
    int zone_z,
    int32_t const* zones,
    uint64_t now_us)
{
    assert(job);
    assert(chunks_xz);
    assert(chunk_count >= 0);
    assert(chunk_count <= WORLD_REBUILD_JOB_MAX_CHUNKS);
    assert(!job->pending);
    assert(job->phase == WORLD_REBUILD_JOB_IDLE);
    assert(!job->stage_world);

    memset(&job->report, 0, sizeof(job->report));
    job->zone_x = zone_x;
    job->zone_z = zone_z;
    job->chunk_count = chunk_count;
    memcpy(job->chunks, chunks_xz, (size_t)chunk_count * 2 * sizeof(int));
    job->have_zones = zones != NULL;
    if( zones )
        memcpy(job->zones, zones, sizeof(job->zones));
    job->stalled = 0;
    job->t_begin_us = now_us;
    job->slices = 0;
    job->slice_us_sum = 0;
    job->slice_us_max = 0;
    job->build_slices = 0;
    job->build_slice_us_sum = 0;
    job->build_slice_us_max = 0;
    job->build_units = 0;
    job->prep_frames = 0;
    job->stage_crowded = 0;
    job->held_open = 0;
}

uint64_t
WorldRebuildJob_MustFinishBy(
    struct WorldRebuildJob const* job,
    uint64_t now_us)
{
    assert(job);
    return now_us + job->deadline_us;
}

void
WorldRebuildJob_GatherStarted(
    struct WorldRebuildJob* job,
    struct ToriRS_Task* gather,
    int tick_suspended)
{
    assert(job);
    assert(gather);
    assert(!job->pending);
    assert(job->phase == WORLD_REBUILD_JOB_IDLE);
    job->gather = gather;
    job->phase = WORLD_REBUILD_JOB_GATHER;
    job->pending = 1;
    job->tick_suspended = tick_suspended;
}

void
WorldRebuildJob_StageBegin(struct WorldRebuildJob* job)
{
    assert(job);
    assert(job->phase == WORLD_REBUILD_JOB_GATHER);
    assert(ToriRS_TaskJob_Done(&job->job));
    assert(!job->stage_world);
    assert(!job->stage_builder);
    job->gather = NULL;
    job->phase = WORLD_REBUILD_JOB_STAGE;
}

int
WorldRebuildJob_Overdue(
    struct WorldRebuildJob const* job,
    uint64_t now_us)
{
    assert(job);
    return now_us >= job->job.must_finish_by_us;
}

int
WorldRebuildJob_Promote(struct WorldRebuildJob* job)
{
    assert(job);
    if( job->job.promoted )
        return 0;
    job->job.promoted = 1;
    return 1;
}

int
WorldRebuildJob_Stall(struct WorldRebuildJob* job)
{
    assert(job);
    if( job->stalled )
        return 0;
    job->stalled = 1;
    return 1;
}

void
WorldRebuildJob_NoteSlice(
    struct WorldRebuildJob* job,
    uint64_t us,
    int building)
{
    assert(job);
    job->slices++;
    job->slice_us_sum += us;
    if( us > job->slice_us_max )
        job->slice_us_max = us;
    if( building )
    {
        job->build_slices++;
        job->build_slice_us_sum += us;
        if( us > job->build_slice_us_max )
            job->build_slice_us_max = us;
    }
}

void
WorldRebuildJob_NoteBuildUnit(struct WorldRebuildJob* job)
{
    assert(job);
    job->build_units++;
}

void
WorldRebuildJob_NotePrepFrame(struct WorldRebuildJob* job)
{
    assert(job);
    job->prep_frames++;
}

void
WorldRebuildJob_MarkCrowded(struct WorldRebuildJob* job)
{
    assert(job);
    job->stage_crowded = 1;
}

void
WorldRebuildJob_TakeVarps(
    struct WorldRebuildJob* job,
    struct VarPManager const* live)
{
    assert(job);
    assert(live);
    assert(!job->stage_varps_taken);
    /* The type tables are the live manager's and outlive the job; only the
     * values are the job's. Nothing writes through the copy: the builder
     * reads varbits and varps and nothing else (VarPManager_ResolveTransform),
     * so the long arm, the server values and the callbacks stay empty. */
    job->stage_varps = *live;
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
    if( live->varp_count > 0 )
    {
        size_t const bytes = (size_t)live->varp_count * sizeof(int);
        job->stage_varps.var = ToriMem_Alloc(ToriMem_Scene(), bytes);
        assert(job->stage_varps.var);
        memcpy(job->stage_varps.var, live->var, bytes);
    }
    job->stage_varps_taken = 1;
}

void
WorldRebuildJob_DropVarps(struct WorldRebuildJob* job)
{
    assert(job);
    if( !job->stage_varps_taken )
        return;
    ToriMem_Free(ToriMem_Scene(), job->stage_varps.var);
    memset(&job->stage_varps, 0, sizeof(job->stage_varps));
    job->stage_varps_taken = 0;
}

int
WorldRebuildJob_Committed(
    struct WorldRebuildJob* job,
    uint64_t now_us)
{
    int resume;

    assert(job);
    assert(job->pending);
    assert(job->phase == WORLD_REBUILD_JOB_STAGE);
    job->pending = 0;
    job->stalled = 0;
    job->phase = WORLD_REBUILD_JOB_IDLE;
    resume = job->tick_suspended;
    job->tick_suspended = 0;
    job->held_open = 1;
    job->held_t0_us = now_us;
    return resume;
}

int
WorldRebuildJob_HeldClose(
    struct WorldRebuildJob* job,
    uint64_t* out_t0_us)
{
    assert(job);
    assert(out_t0_us);
    if( !job->held_open )
        return 0;
    job->held_open = 0;
    *out_t0_us = job->held_t0_us;
    return 1;
}

int
WorldRebuildJob_Cancelled(struct WorldRebuildJob* job)
{
    int stalled;

    assert(job);
    assert(job->pending);
    assert(ToriRS_TaskJob_Done(&job->job));
    job->gather = NULL;
    job->pending = 0;
    job->phase = WORLD_REBUILD_JOB_IDLE;
    /* The tick it suspended ends with the session (or is superseded by the
     * load replacing this scene); nothing of it is left to latch. */
    job->tick_suspended = 0;
    stalled = job->stalled;
    job->stalled = 0;
    return stalled;
}
