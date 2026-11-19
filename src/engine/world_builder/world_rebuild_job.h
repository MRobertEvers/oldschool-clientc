#ifndef SRC_ENGINE_WORLD_BUILDER_WORLD_REBUILD_JOB_H
#define SRC_ENGINE_WORLD_BUILDER_WORLD_REBUILD_JOB_H

/*
 * The background world rebuild: whether a REBUILD is done in the background
 * at all, what the job in progress is building, which phase it is in, and how
 * its slices have gone.
 *
 * In background mode a REBUILD onto a scene that is already loaded becomes a
 * job instead of a load awaited inline. The job records the zone it is for --
 * the square list, the centre zone, the instance grid when there is one --
 * and runs in two phases: GATHER, while the gather task reads what the new
 * scene needs, and STAGE, while a World and WorldBuilder of the job's own
 * build it beside the scene still drawn. The commit or a cancel ends it and
 * the job is idle again. Only one job is ever pending.
 *
 * A REBUILD is wanted in the background only when the mode says so, the
 * editor is not the one asking, there is a loaded scene to keep on screen, the
 * server tick it belongs to is not open on anything but varps, no clientscript
 * is held, and no job is pending already. A job past its deadline (the
 * configured one, counted from its start) is promoted: finished there and
 * then.
 *
 * Every slice the job is given is counted, summed and its longest kept, and
 * the staging build's slices again on their own, for the timing report at the
 * commit. The staged varps are the REBUILD's values, copied when the job
 * starts so one scene is built against one varp state; the copy shares the
 * live manager's type tables and owns only its values.
 *
 * Nothing here reads a clock, runs a task, touches a scene or prints. The
 * caller reads the environment for the mode and the deadline, passes the time,
 * answers the wanted rule's questions about the editor, the scene, the server
 * tick and the held scripts, creates and starts the gather and stage tasks on
 * its runner, builds and swaps the scene, cancels the tasks a cancelled job
 * still has queued, and prints what it wants to say about all of it.
 */

#include "engine/world_builder/task_world_load.h"
#include "task_runner.h"
#include "varp/varp_manager.h"

#include <stdint.h>

struct ToriRS_Task;
struct World;
struct WorldBuilder;

/* Where a job stands (WorldRebuildJob::phase). */
enum
{
    WORLD_REBUILD_JOB_IDLE = 0,
    /* `job` counts the gather. */
    WORLD_REBUILD_JOB_GATHER = 1,
    /* `job` counts the staging build. */
    WORLD_REBUILD_JOB_STAGE = 2,
};

/* The square list a job keeps: (mapx, mapz) pairs. */
#define WORLD_REBUILD_JOB_MAX_CHUNKS 64
/* The instance grid a job keeps: PKT_MAP_REBUILD_ZONES descriptors. */
#define WORLD_REBUILD_JOB_ZONES (4 * 13 * 13)

struct WorldRebuildJob
{
    /** Is a REBUILD done in the background at all? */
    int background;
    /** A job still gathering past this long after its start is promoted. */
    uint64_t deadline_us;

    int pending;
    struct ToriRS_TaskJob job;
    /** The gather, while it has not ended (job.join > 0). */
    struct ToriRS_Task* gather;
    struct WorldLoadGatherReport report;
    int zone_x;
    int zone_z;
    int chunk_count;
    int chunks[WORLD_REBUILD_JOB_MAX_CHUNKS * 2];
    int have_zones;
    int32_t zones[WORLD_REBUILD_JOB_ZONES];
    /** The scene base before the rebuild: the entity-shift source. */
    int had_world;
    int prev_base_x;
    int prev_base_z;
    /** A promoted job waiting on a read only the platform can answer. */
    int stalled;
    /** The REBUILD's server tick was open and its latch is suspended for the
     *  hold; the commit re-arms it. */
    int tick_suspended;
    /* What the job cost, reported at the commit. */
    uint64_t t_begin_us;
    int slices;
    uint64_t slice_us_sum;
    uint64_t slice_us_max;

    /** WORLD_REBUILD_JOB_IDLE, _GATHER or _STAGE. */
    int phase;
    /**
     * The staging scene (phase STAGE, until the commit or a cancel): a World
     * and a WorldBuilder of the job's own, built a unit at a time beside the
     * scene still drawn, then swapped in whole. NULL outside a staging build.
     */
    struct World* stage_world;
    struct WorldBuilder* stage_builder;
    /** The stage task, while it is queued. */
    struct ToriRS_Task* stage_task;
    /** The varps the staging build resolves multilocs against: the values at
     *  the job's start. Owns only `var` (stage_varps_taken). */
    struct VarPManager stage_varps;
    int stage_varps_taken;
    /** The provider's derived caches are held from the gather's end to the
     *  build's. */
    int trim_held;
    /** The scene's element space could not hold the staging scene beside the
     *  live one: the commit builds in place instead. */
    int stage_crowded;
    /* The staging build's own slices. */
    int build_slices;
    uint64_t build_slice_us_sum;
    uint64_t build_slice_us_max;
    int build_units;
    /* Frames the job waited for the renderer's staged batch. */
    int prep_frames;
    /* The commit frame's packet work after the commit itself (the held
     * packets): open from the commit, at `held_t0_us`, to the pump's return. */
    int held_open;
    uint64_t held_t0_us;
};

/** No job, nothing counted, blocking mode, no deadline. */
void
WorldRebuildJob_Reset(struct WorldRebuildJob* job);

/** The mode (background or blocking) and the deadline a job is given. */
void
WorldRebuildJob_Configure(
    struct WorldRebuildJob* job,
    int background,
    uint64_t deadline_us);

int
WorldRebuildJob_Background(struct WorldRebuildJob const* job);

/**
 * Should the REBUILD about to be applied be a background job?
 *
 * `editor`: the editor is running (its scenes are its own). `scene_loaded`:
 * there is a loaded scene to keep on screen. `tick_open_nonvar`: the
 * REBUILD's server tick is open on something that is not a varp.
 * `scripts_held`: a clientscript is held.
 */
int
WorldRebuildJob_Wanted(
    struct WorldRebuildJob const* job,
    int editor,
    int scene_loaded,
    int tick_open_nonvar,
    int scripts_held);

int
WorldRebuildJob_Pending(struct WorldRebuildJob const* job);

/** WORLD_REBUILD_JOB_IDLE, _GATHER or _STAGE. */
int
WorldRebuildJob_Phase(struct WorldRebuildJob const* job);

/** Has everything the job's current phase runs ended? */
int
WorldRebuildJob_TasksDone(struct WorldRebuildJob const* job);

/**
 * Record a new job, started at `now_us`, for a zone: the square list
 * (`chunk_count` pairs), the centre zone, and the instance grid when `zones`
 * is not NULL. Everything the last job counted is cleared. The job is not
 * pending until its gather is started (WorldRebuildJob_GatherStarted).
 */
void
WorldRebuildJob_Begin(
    struct WorldRebuildJob* job,
    int const* chunks_xz,
    int chunk_count,
    int zone_x,
    int zone_z,
    int32_t const* zones,
    uint64_t now_us);

/** The deadline for a job started at `now_us`. */
uint64_t
WorldRebuildJob_MustFinishBy(
    struct WorldRebuildJob const* job,
    uint64_t now_us);

/**
 * The gather `gather` was started on `job`: the job is pending, in phase
 * GATHER. `tick_suspended`: the latch of the REBUILD's server tick was
 * suspended for the hold.
 */
void
WorldRebuildJob_GatherStarted(
    struct WorldRebuildJob* job,
    struct ToriRS_Task* gather,
    int tick_suspended);

/** The gather has ended: the job is in phase STAGE, its gather gone. */
void
WorldRebuildJob_StageBegin(struct WorldRebuildJob* job);

/** Is `now_us` past the job's deadline? */
int
WorldRebuildJob_Overdue(
    struct WorldRebuildJob const* job,
    uint64_t now_us);

/** Promote the job. Returns 1 the first time, 0 when it already was. */
int
WorldRebuildJob_Promote(struct WorldRebuildJob* job);

/** The promoted job is waiting on a read. Returns 1 the first time. */
int
WorldRebuildJob_Stall(struct WorldRebuildJob* job);

/** One slice of `us` the job was given; `building`: in the staging build. */
void
WorldRebuildJob_NoteSlice(
    struct WorldRebuildJob* job,
    uint64_t us,
    int building);

/** One unit of the staging build ran. */
void
WorldRebuildJob_NoteBuildUnit(struct WorldRebuildJob* job);

/** One frame waited for the renderer's staged batch. */
void
WorldRebuildJob_NotePrepFrame(struct WorldRebuildJob* job);

/** The staging scene cannot be held beside the live one. */
void
WorldRebuildJob_MarkCrowded(struct WorldRebuildJob* job);

/**
 * Copy `live`'s values for the staging build. The copy shares `live`'s type
 * tables, which must outlive it, and has no long arm, server values or
 * callbacks.
 */
void
WorldRebuildJob_TakeVarps(
    struct WorldRebuildJob* job,
    struct VarPManager const* live);

/** Free the staged values, when there are any. */
void
WorldRebuildJob_DropVarps(struct WorldRebuildJob* job);

/**
 * The job was committed at `now_us`: it is idle, and the held packets'
 * window is open from `now_us`. Returns 1 when the server tick's latch was
 * suspended and must be resumed now.
 */
int
WorldRebuildJob_Committed(
    struct WorldRebuildJob* job,
    uint64_t now_us);

/**
 * The pump has returned: when the held window is open, close it, set
 * `*out_t0_us` to its start and return 1; otherwise 0.
 */
int
WorldRebuildJob_HeldClose(
    struct WorldRebuildJob* job,
    uint64_t* out_t0_us);

/**
 * The pending job was cancelled, its tasks already gone: it is idle, with no
 * gather and no suspended tick. Returns 1 when it had stalled (the caller
 * clears what the stall set).
 */
int
WorldRebuildJob_Cancelled(struct WorldRebuildJob* job);

#endif
