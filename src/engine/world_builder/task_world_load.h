#ifndef TASK_WORLD_LOAD_H
#define TASK_WORLD_LOAD_H

struct CacheProvider;
struct WorldBuilder;
struct ToriRS_Task;

/**
 * Composite load: map terrain + scenery for a chunk list, then every config
 * referenced by them (underlays, overlays, overlay textures, locs, loc
 * models), then a synchronous WorldBuilder_RebuildChunklist + load-complete.
 * All IO flows through the task system; already-cached assets are skipped.
 * chunks_xz is (mapx,mapz) pairs, copied into the task.
 *
 * on_done (may be NULL) is invoked once, synchronously, at the tail of the load
 * — after the rebuild and load-complete, before the task frees. It is how a
 * caller runs "the load landed" work without polling: the fire-and-forget path
 * supplies it; a caller that instead awaits this task (TASK_AWAITSELF_IF) can
 * leave it NULL and run its own tail after the await returns.
 */
struct ToriRS_Task*
CreateTask_WorldLoad(
    struct CacheProvider* provider,
    struct WorldBuilder* builder,
    const int* chunks_xz,
    int chunk_count,
    void (*on_done)(void*),
    void* on_done_ud);

#endif
