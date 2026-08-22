#include "engine/dat1/dat1_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/torirs_idk_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat1IdkLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    int idk_id;
};

static int
Task_Dat1IdkLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1IdkLoad* task = (struct Task_Dat1IdkLoad*)task_base;
    struct RSCache_Dat1ConfigIdk* rscache_idk = NULL;
    struct ToriRS_Idk* torirs_idk = NULL;

    PT_BEGIN(&task->pt);

    if( !dat1_buildcache_get_config_jagfile(task->bc) )
    {
        struct RSCache_FileListDat* config_jagfile = NULL;

        RSCache_IO_Dat1ConfigJagfileLoad(io, 0);
        PT_YIELD(&task->pt);

        config_jagfile = RSCache_IO_Dat1ConfigJagfileDecode(io, 0);
        if( !config_jagfile )
        {
            fprintf(stderr, "Failed to decode dat1 config jagfile for idk %d\n", task->idk_id);
            PT_EXIT(&task->pt);
        }

        dat1_buildcache_set_config_jagfile(task->bc, config_jagfile);
    }

    if( task->bc->idk_hmap->size == 0 )
        dat1_buildcache_idks_init_from_config_jagfile(task->bc);

    rscache_idk = dat1_buildcache_idk_get(task->bc, task->idk_id);
    if( !rscache_idk )
    {
        /*
         * "Past the end" is not "failed", and saying so matters here because
         * the design screen's kit scan FINDS the end by walking ids until one
         * is missing (player_appearance.c). That last, expected miss is not a
         * defect and must not read like one -- a boot that prints "Failed to
         * load dat1 idk 82" against a cache with exactly 82 kits sends the
         * next person looking for a broken record that does not exist.
         *
         * dat1 decodes the whole idk table in one go and the ids are
         * contiguous from 0, so the loaded count IS the table size and the two
         * cases are cleanly separable.
         */
        int const table_size = (int)task->bc->idk_hmap->size;
        if( task->idk_id >= table_size )
            fprintf(
                stderr,
                "dat1 idk %d: past the end of the table (%d kits)\n",
                task->idk_id,
                table_size);
        else
            fprintf(stderr, "Failed to load dat1 idk %d\n", task->idk_id);
        PT_EXIT(&task->pt);
    }

    torirs_idk = ToriRS_IdkFromRSCacheDat1(task->idk_id, rscache_idk);
    if( !torirs_idk )
    {
        fprintf(stderr, "Failed to convert dat1 idk %d\n", task->idk_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_IdkAdd(&task->bc->base, task->idk_id, torirs_idk);

    PT_END(&task->pt);
}

static void
Task_Dat1IdkLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1IdkLoad_VTable = {
    .run = Task_Dat1IdkLoad_Run,
    .free = Task_Dat1IdkLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat1IdkLoad(
    struct CacheProvider* provider,
    int idk_id)
{
    struct Dat1BuildCache* dat1_buildcache;
    struct Task_Dat1IdkLoad* task;

    assert(provider);

    dat1_buildcache = (struct Dat1BuildCache*)provider;
    if( CacheProvider_IdkHas(provider, idk_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1IdkLoad_VTable;
    strcpy(task->task.name, "Dat1IdkLoad");
    task->bc = dat1_buildcache;
    task->idk_id = idk_id;
    PT_INIT(&task->pt);
    return &task->task;
}
