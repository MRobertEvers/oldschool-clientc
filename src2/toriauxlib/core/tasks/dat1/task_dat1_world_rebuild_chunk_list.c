#include "ioqueue/libtorirs_io.h"
#include "toriauxlib/cache/toriauxlibcache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TASK_WORLD_MAX_CHUNKS 64

struct Task_Dat1WorldRebuildChunkList
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* c;
    int chunk_count;
    int chunks_x[TASK_WORLD_MAX_CHUNKS];
    int chunks_z[TASK_WORLD_MAX_CHUNKS];
};

struct LibToriRS_Task*
Task_Dat1WorldRebuildCore_New(
    struct ToriAuxLibCache* c,
    const int* chunks_x,
    const int* chunks_z,
    int chunk_count);

static int
Task_Dat1WorldRebuildChunkList_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat1WorldRebuildChunkList* task =
        LibToriRS_container_of(base, struct Task_Dat1WorldRebuildChunkList, base);

    PT_BEGIN(&task->pt);

    TASK_AWAITEX(
        &task->pt,
        ctx,
        Task_Dat1WorldRebuildCore_New(
            task->c, task->chunks_x, task->chunks_z, task->chunk_count));

    PT_END(&task->pt);
}

static void
Task_Dat1WorldRebuildChunkList_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat1_world_rebuild_chunk_list_vtable = {
    .run_fn = Task_Dat1WorldRebuildChunkList_Run,
    .free_fn = Task_Dat1WorldRebuildChunkList_Free,
};

struct LibToriRS_Task*
Task_Dat1WorldRebuildChunkList_New(
    struct ToriAuxLibCache* c,
    const int* chunks_x,
    const int* chunks_z,
    int count)
{
    struct Task_Dat1WorldRebuildChunkList* task =
        calloc(1, sizeof(struct Task_Dat1WorldRebuildChunkList));
    if( !task )
        return NULL;
    assert(count <= TASK_WORLD_MAX_CHUNKS);
    task->base.vtable = &g_task_dat1_world_rebuild_chunk_list_vtable;
    task->c = c;
    task->chunk_count = count;
    for( int i = 0; i < count; i++ )
    {
        task->chunks_x[i] = chunks_x[i];
        task->chunks_z[i] = chunks_z[i];
    }
    PT_INIT(&task->pt);
    return &task->base;
}
