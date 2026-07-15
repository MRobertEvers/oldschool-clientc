#include "ioqueue/libtorirs_io.h"
#include "toriauxlib/cache/toriauxlibcache.h"

#include <assert.h>
#include <stdlib.h>

struct LibToriRS_Task*
Task_Dat2WorldRebuildCore_New(
    struct ToriAuxLibCache* c,
    const int* chunk_xs,
    const int* chunk_zs,
    int chunk_count);

struct Task_Dat2WorldRebuildChunkList
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* c;
    int chunk_count;
    int* chunks_x;
    int* chunks_z;
};

static int
Task_Dat2WorldRebuildChunkList_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2WorldRebuildChunkList* task =
        LibToriRS_container_of(base, struct Task_Dat2WorldRebuildChunkList, base);

    PT_BEGIN(&task->pt);

    TASK_AWAITEX(
        &task->pt,
        ctx,
        Task_Dat2WorldRebuildCore_New(task->c, task->chunks_x, task->chunks_z, task->chunk_count));

    PT_END(&task->pt);
}

static void
Task_Dat2WorldRebuildChunkList_Free(struct LibToriRS_Task* base)
{
    struct Task_Dat2WorldRebuildChunkList* task =
        LibToriRS_container_of(base, struct Task_Dat2WorldRebuildChunkList, base);

    free(task->chunks_x);
    free(task->chunks_z);
    free(task);
}

static struct LibToriRS_TaskVTable g_task_dat2_world_rebuild_chunk_list_vtable = {
    .run_fn = Task_Dat2WorldRebuildChunkList_Run,
    .free_fn = Task_Dat2WorldRebuildChunkList_Free,
};

struct LibToriRS_Task*
Task_Dat2WorldRebuildChunkList_New(
    struct ToriAuxLibCache* c,
    const int* chunks_x,
    const int* chunks_z,
    int count)
{
    struct Task_Dat2WorldRebuildChunkList* task;

    assert(c);
    assert(chunks_x);
    assert(chunks_z);
    assert(count > 0);

    task = calloc(1, sizeof(struct Task_Dat2WorldRebuildChunkList));
    if( !task )
        return NULL;

    task->chunks_x = malloc((size_t)count * sizeof(int));
    task->chunks_z = malloc((size_t)count * sizeof(int));
    if( !task->chunks_x || !task->chunks_z )
    {
        free(task->chunks_x);
        free(task->chunks_z);
        free(task);
        return NULL;
    }

    for( int i = 0; i < count; i++ )
    {
        task->chunks_x[i] = chunks_x[i];
        task->chunks_z[i] = chunks_z[i];
    }

    task->base.vtable = &g_task_dat2_world_rebuild_chunk_list_vtable;
    task->c = c;
    task->chunk_count = count;
    PT_INIT(&task->pt);
    return &task->base;
}
