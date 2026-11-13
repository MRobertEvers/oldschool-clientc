#ifndef TASK_WORLD_LOAD_H
#define TASK_WORLD_LOAD_H

#include <stdint.h>

struct CacheProvider;
struct ToriRS_TaskQueue;
struct WorldBuilder;
struct ToriRS_Task;

/**
 * Composite load: map terrain + scenery for a chunk list, then every config
 * referenced by them (underlays, overlays, overlay textures, locs, loc
 * models), then a synchronous rebuild + load-complete.
 * All IO flows through the task system; already-cached assets are skipped.
 * chunks_xz is (mapx,mapz) pairs, copied into the task — used for the asset
 * prefetch regardless of rebuild shape.
 *
 * zone_center_x/z (>= 0): rebuild via WorldBuilder_RebuildCenterzone — the
 * REBUILD_NORMAL path. zone_center_x < 0: rebuild via
 * WorldBuilder_RebuildChunklist (offline / hotkey loads that name an explicit
 * square list), which derives its own scene size from the square bounds.
 *
 * scene_size: side of the square scene the centerzone/instance rebuilds
 * allocate, in tiles (a multiple of 8; the scene is based at
 * (zone_center - scene_size/16) * 8). 104 is the classic root scene; a boat
 * view passes its own 8..104. Ignored by the chunklist path.
 *
 * zones (non-NULL): rebuild via WorldBuilder_RebuildInstance instead — the
 * REBUILD_REGION / REBUILD_WORLDENTITY path. PKT_MAP_REBUILD_ZONES
 * descriptors, copied into the task.
 * chunks_xz then names the *source* squares the descriptors read from, since the
 * destination squares are unused map and have nothing to prefetch.
 *
 * on_done (may be NULL) is invoked once, synchronously, at the tail of the load
 * — after the rebuild and load-complete, before the task frees. It is how a
 * caller runs "the load landed" work without polling: the fire-and-forget path
 * supplies it; a caller that instead awaits this task (PT_TASK_AWAITSELF_IF) can
 * leave it NULL and run its own tail after the await returns.
 */
struct ToriRS_Task*
CreateTask_WorldLoad(
    struct CacheProvider* provider,
    struct WorldBuilder* builder,
    struct ToriRS_TaskQueue* queue,
    const int* chunks_xz,
    int chunk_count,
    int zone_center_x,
    int zone_center_z,
    int scene_size,
    const int32_t* zones,
    void (*on_done)(void*),
    void* on_done_ud);

/**
 * What a gather found and how long it took (TORIRS_REBUILD_TIMING), written
 * once, by the gather, as it ends. Times are WorldBuilder_TimingNowMs; zero
 * when timing is off.
 */
struct WorldLoadGatherReport
{
    int done;
    double t_start_ms;
    double t_assets_ms;
    int resumes;
    int chunk_count;
    int locs;
    int models;
    int textures;
    int seqs;
};

/**
 * The load's first half alone: every asset CreateTask_WorldLoad would read,
 * fanned out on `queue`, and no build. The task ends when they have all ended,
 * having touched nothing a frame reads (the scene's sequence registry only
 * gains entries), so it can run while the previous scene is still being
 * drawn. The arguments mean what they mean to CreateTask_WorldLoad; the build
 * they describe is WorldLoad_Build's, run by whoever owns the result.
 *
 * `report` (may be NULL) must outlive the task, or be detached with it
 * (TaskWorldLoad_Cancel).
 */
struct ToriRS_Task*
CreateTask_WorldGather(
    struct CacheProvider* provider,
    struct WorldBuilder* builder,
    struct ToriRS_TaskQueue* queue,
    const int* chunks_xz,
    int chunk_count,
    int zone_center_x,
    int zone_center_z,
    int scene_size,
    const int32_t* zones,
    struct WorldLoadGatherReport* report);

/**
 * The load's second half: the synchronous build from resident assets, plus
 * load-complete. Exactly the build CreateTask_WorldLoad ends with, for the
 * same arguments.
 */
void
WorldLoad_Build(
    struct WorldBuilder* builder,
    const int* chunks_xz,
    int chunk_count,
    int zone_center_x,
    int zone_center_z,
    int scene_size,
    const int32_t* zones);

/**
 * Take a gather that has not ended off `queue` and free it. The loads it
 * fanned out stay queued and run to their own end, counted by nothing; what
 * they land is ordinary cache residency. Its report is not written.
 */
void
TaskWorldLoad_Cancel(
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_Task* task);

#endif
