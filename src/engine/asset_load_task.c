#include "engine/asset_load_task.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct ToriRS_Task*
asset_load_take_child(struct AssetLoadTask* self)
{
    struct ToriRS_Task* child = self->child;
    self->child = NULL;
    return child;
}

static int
Task_AssetLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct AssetLoadTask* self = (struct AssetLoadTask*)base;

    PT_BEGIN(&self->pt);
    PT_TASK_AWAITSELF(asset_load_take_child(self));
    self->ended = 1;
    Assets_Finished(self->assets, self->key, self->resident(self->data, self->key));
    PT_END(&self->pt);
}

static void
Task_AssetLoad_Free(struct ToriRS_Task* base)
{
    struct AssetLoadTask* self = (struct AssetLoadTask*)base;

    /* Freed mid-read: the entry must not stay "loading" for the rest of the
     * session, which is what a flag left set did. (At shutdown the table is
     * closed and ignores it.) */
    if( !self->ended )
        Assets_Abandoned(self->assets, self->key);
    /* A child it was awaiting inline is freed by task_free (inline_child). */
    if( self->child )
        task_free(self->child);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_AssetLoad_VTable = {
    .run = Task_AssetLoad_Run,
    .free = Task_AssetLoad_Free,
};

struct AssetLoadTask*
AssetLoadTask_New(
    struct Assets* assets,
    struct AssetKey key,
    bool (*resident)(
        void* data,
        struct AssetKey key),
    void* data)
{
    struct AssetLoadTask* task;

    assert(assets);
    assert(resident);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AssetLoad_VTable;
    snprintf(task->task.name, sizeof(task->task.name), "AssetLoad:%s",
        Assets_Policy((enum AssetKind)key.kind)->name);
    task->assets = assets;
    task->key = key;
    task->resident = resident;
    task->data = data;
    task->args[0] = key.id;
    task->args[1] = key.variant;
    PT_INIT(&task->pt);
    return task;
}

enum AssetStart
AssetLoadTask_Start(
    struct AssetLoadTask* task,
    struct ToriRS_Task* child,
    struct ToriRS_TaskQueue* queue)
{
    assert(task);
    assert(queue);
    if( !child )
    {
        bool const here = task->resident(task->data, task->key);
        ToriMem_Free(ToriMem_Process(), task);
        return here ? ASSET_START_HERE : ASSET_START_FAILED;
    }
    task->child = child;
    ToriRS_TaskQueue_Add(queue, &task->task);
    return ASSET_START_LOADING;
}
