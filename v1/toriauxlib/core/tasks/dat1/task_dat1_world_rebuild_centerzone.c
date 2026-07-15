#include "ioqueue/libtorirs_io.h"
#include "toriauxlib/cache/toriauxlibcache.h"

#include <stdlib.h>

struct Task_Dat1WorldRebuildNormalCenterzone
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* c;
    int zonex;
    int zonez;
};

struct LibToriRS_Task*
Task_Dat1WorldRebuildCore_NewForCenterzone(
    struct ToriAuxLibCache* c,
    int zonex,
    int zonez);

static int
Task_Dat1WorldRebuildNormalCenterzone_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat1WorldRebuildNormalCenterzone* task =
        LibToriRS_container_of(base, struct Task_Dat1WorldRebuildNormalCenterzone, base);

    PT_BEGIN(&task->pt);

    TASK_AWAITEX(
        &task->pt,
        ctx,
        Task_Dat1WorldRebuildCore_NewForCenterzone(task->c, task->zonex, task->zonez));

    PT_END(&task->pt);
}

static void
Task_Dat1WorldRebuildNormalCenterzone_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat1_world_rebuild_centerzone_vtable = {
    .run_fn = Task_Dat1WorldRebuildNormalCenterzone_Run,
    .free_fn = Task_Dat1WorldRebuildNormalCenterzone_Free,
};

struct LibToriRS_Task*
Task_Dat1WorldRebuildNormalCenterzone_New(
    struct ToriAuxLibCache* c,
    int zonex,
    int zonez)
{
    struct Task_Dat1WorldRebuildNormalCenterzone* task =
        calloc(1, sizeof(struct Task_Dat1WorldRebuildNormalCenterzone));
    if( !task )
        return NULL;
    task->base.vtable = &g_task_dat1_world_rebuild_centerzone_vtable;
    task->c = c;
    task->zonex = zonex;
    task->zonez = zonez;
    PT_INIT(&task->pt);
    return &task->base;
}
