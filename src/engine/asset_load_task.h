#ifndef SRC_ENGINE_ASSET_LOAD_TASK_H
#define SRC_ENGINE_ASSET_LOAD_TASK_H

/*
 * The task a loader of the assets table puts on the parallel queue: it runs
 * the cache's own load task (the child) to its end and reports the end to the
 * table, landed when `resident` says the asset is here.
 *
 * For a loader's `start` (engine/asset_requests.h):
 *
 *     struct AssetLoadTask* task = AssetLoadTask_New(assets, key, resident, data);
 *     return AssetLoadTask_Start(task, CreateTask_SoundLoad(provider, key.id), queue);
 *
 * `args` is storage the child may read by pointer for as long as it runs (an
 * obj model load reads its obj and count arrays that way). A task freed
 * before its child ended reports itself abandoned. (Its `land_mask`, kinds
 * whose callback waiters landed at a load's end, went with the last of those
 * waiters: whoever waits now is a task joined on the table.)
 */

#include "asyncio.h"
#include "engine/asset_requests.h"

struct AssetLoadTask
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Assets* assets;
    struct AssetKey key;
    bool (*resident)(
        void* data,
        struct AssetKey key);
    void* data;
    /* Handed to the await on the first run. */
    struct ToriRS_Task* child;
    int args[2];
    /* The end has been reported. */
    int ended;
};

struct AssetLoadTask*
AssetLoadTask_New(
    struct Assets* assets,
    struct AssetKey key,
    bool (*resident)(
        void* data,
        struct AssetKey key),
    void* data);

/**
 * Queue `task` on `queue` to run `child`. A NULL child (the cache makes no
 * task for a record it holds) frees `task` and answers HERE when `resident`
 * says so, FAILED otherwise.
 */
enum AssetStart
AssetLoadTask_Start(
    struct AssetLoadTask* task,
    struct ToriRS_Task* child,
    struct ToriRS_TaskQueue* queue);

#endif /* SRC_ENGINE_ASSET_LOAD_TASK_H */
