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
 */
struct ToriRS_Task*
CreateTask_WorldLoad(
    struct CacheProvider* provider,
    struct WorldBuilder* builder,
    const int* chunks_xz,
    int chunk_count);

#endif
