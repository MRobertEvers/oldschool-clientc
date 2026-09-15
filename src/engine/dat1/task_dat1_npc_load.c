#include "engine/dat1/dat1_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/torirs_npctype_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

struct Task_Dat1NpcLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    int npc_id;
};

static int
Task_Dat1NpcLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1NpcLoad* task = (struct Task_Dat1NpcLoad*)task_base;
    struct RSCache_Dat1ConfigNpc* rscache_npc = NULL;
    struct ToriRS_Npctype* torirs_npc = NULL;

    PT_BEGIN(&task->pt);

    if( !dat1_buildcache_get_config_jagfile(task->bc) )
    {
        struct RSCache_FileListDat* config_jagfile = NULL;

        RSCache_IO_Dat1ConfigJagfileLoad(io, 0);
        PT_YIELD(&task->pt);

        config_jagfile = RSCache_IO_Dat1ConfigJagfileDecode(io, 0);
        if( !config_jagfile )
        {
            TORIRS_ERR("Failed to decode dat1 config jagfile for npc %d\n", task->npc_id);
            PT_EXIT(&task->pt);
        }

        dat1_buildcache_set_config_jagfile(task->bc, config_jagfile);
    }

    rscache_npc = dat1_buildcache_npc_load_from_config_jagfile(task->bc, task->npc_id);
    if( !rscache_npc )
    {
        TORIRS_ERR("Failed to load dat1 npc %d\n", task->npc_id);
        PT_EXIT(&task->pt);
    }

    torirs_npc = ToriRS_NpctypeFromRSCacheDat1(task->npc_id, rscache_npc);
    if( !torirs_npc )
    {
        TORIRS_ERR("Failed to convert dat1 npc %d\n", task->npc_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_NpctypeAdd(&task->bc->base, task->npc_id, torirs_npc);

    PT_END(&task->pt);
}

static void
Task_Dat1NpcLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1NpcLoad_VTable = {
    .run = Task_Dat1NpcLoad_Run,
    .free = Task_Dat1NpcLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat1NpcLoad(
    struct CacheProvider* provider,
    int npc_id)
{
    struct Dat1BuildCache* dat1_buildcache;
    struct Task_Dat1NpcLoad* task;

    assert(provider);

    dat1_buildcache = (struct Dat1BuildCache*)provider;
    if( CacheProvider_NpctypeHas(provider, npc_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1NpcLoad_VTable;
    strcpy(task->task.name, "Dat1NpcLoad");
    task->bc = dat1_buildcache;
    task->npc_id = npc_id;
    PT_INIT(&task->pt);
    return &task->task;
}
