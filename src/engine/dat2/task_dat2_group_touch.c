#include "engine/dat2/dat2_tasks.h"

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * One group, fetched and dropped. See CreateTask_Dat2GroupTouch.
 *
 * Why a task at all, rather than the preload task queueing the items itself:
 * the queue's contract is that a task's items are complete when it resumes,
 * and a fan-out of thousands has to be waited on as a set. Siblings joined
 * with PT_TASK_JOIN are how every other fan-out in this tree does that
 * (task_pack_assets_load.c, app_world_spawn.c), so this is the same shape
 * with the smallest possible body.
 */
struct Task_Dat2GroupTouch
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int table_id;
    int group_id;
};

static int
Task_Dat2GroupTouch_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat2GroupTouch* task = (struct Task_Dat2GroupTouch*)task_base;
    struct RSCache_Dat2DiskArchive* archive;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2RecordGroupLoad(io, 0, task->table_id, task->group_id);
    PT_YIELD(&task->pt);

    /* A group the cache does not hold decodes to NULL, which is not an error
     * here: the reference table listed it and the server has no reply for
     * "no such group", so the producer answered with nothing. Nothing to keep
     * either way. */
    archive = RSCache_IO_Dat2RecordGroupDecode(io, 0, task->table_id);
    if( archive )
        RSCache_Dat2DiskArchiveFree(archive);

    PT_END(&task->pt);
}

static void
Task_Dat2GroupTouch_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2GroupTouch_VTable = {
    .run = Task_Dat2GroupTouch_Run,
    .free = Task_Dat2GroupTouch_Free,
};

struct ToriRS_Task*
CreateTask_Dat2GroupTouch(
    struct CacheProvider* provider,
    int table_id,
    int group_id)
{
    struct Task_Dat2GroupTouch* task;

    assert(provider);
    assert(table_id >= 0);
    assert(group_id >= 0);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2GroupTouch_VTable;
    strcpy(task->task.name, "Dat2GroupTouch");
    task->bc = (struct Dat2BuildCache*)provider;
    task->table_id = table_id;
    task->group_id = group_id;
    PT_INIT(&task->pt);
    return &task->task;
}
