#include "ioqueue/libtorirs_io.h"
#include "toriauxlib/cache/toriauxlibcache.h"

#include <assert.h>
#include <stdlib.h>

#define TASK_DAT2_WORLD_SCENE_WIDTH 104
#define TASK_DAT2_WORLD_MAX_CHUNKS 64

struct LibToriRS_Task*
Task_Dat2WorldRebuildCore_New(
    struct ToriAuxLibCache* c,
    const int* chunk_xs,
    const int* chunk_zs,
    int chunk_count);

struct Task_Dat2WorldRebuildCenterzone
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* c;
    int zonex;
    int zonez;
};

static int
task_dat2_world_compute_centerzone_chunks(
    int zonex,
    int zonez,
    int* chunks_x,
    int* chunks_z)
{
    int zone_padding = TASK_DAT2_WORLD_SCENE_WIDTH / (2 * 8);
    int zone_sw_x = zonex - zone_padding;
    int zone_sw_z = zonez - zone_padding;
    int zone_ne_x = zonex + zone_padding;
    int zone_ne_z = zonez + zone_padding;

    int map_sw_x = zone_sw_x / 8;
    int map_sw_z = zone_sw_z / 8;
    int map_ne_x = zone_ne_x / 8;
    int map_ne_z = zone_ne_z / 8;

    int count = 0;
    for( int x = map_sw_x; x <= map_ne_x; x++ )
    {
        for( int z = map_sw_z; z <= map_ne_z; z++ )
        {
            assert(count < TASK_DAT2_WORLD_MAX_CHUNKS);
            chunks_x[count] = x;
            chunks_z[count] = z;
            count++;
        }
    }
    return count;
}

static int
Task_Dat2WorldRebuildCenterzone_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2WorldRebuildCenterzone* task =
        LibToriRS_container_of(base, struct Task_Dat2WorldRebuildCenterzone, base);
    int chunks_x[TASK_DAT2_WORLD_MAX_CHUNKS];
    int chunks_z[TASK_DAT2_WORLD_MAX_CHUNKS];
    int chunk_count;

    PT_BEGIN(&task->pt);

    chunk_count = task_dat2_world_compute_centerzone_chunks(
        task->zonex, task->zonez, chunks_x, chunks_z);

    TASK_AWAITEX(
        &task->pt,
        ctx,
        Task_Dat2WorldRebuildCore_New(task->c, chunks_x, chunks_z, chunk_count));

    PT_END(&task->pt);
}

static void
Task_Dat2WorldRebuildCenterzone_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat2_world_rebuild_centerzone_vtable = {
    .run_fn = Task_Dat2WorldRebuildCenterzone_Run,
    .free_fn = Task_Dat2WorldRebuildCenterzone_Free,
};

struct LibToriRS_Task*
Task_Dat2WorldRebuildCenterzone_New(
    struct ToriAuxLibCache* c,
    int zonex,
    int zonez)
{
    struct Task_Dat2WorldRebuildCenterzone* task =
        calloc(1, sizeof(struct Task_Dat2WorldRebuildCenterzone));
    if( !task )
        return NULL;

    task->base.vtable = &g_task_dat2_world_rebuild_centerzone_vtable;
    task->c = c;
    task->zonex = zonex;
    task->zonez = zonez;
    PT_INIT(&task->pt);
    return &task->base;
}
