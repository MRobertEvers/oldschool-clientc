#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_npctype_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2NpcLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int npc_id;
};

static int
Task_Dat2NpcLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2NpcLoad* task = (struct Task_Dat2NpcLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_Dat2ConfigNpc* rscache_npc = NULL;
    struct ToriRS_Npctype* torirs_npc = NULL;
    int wanted_id = task->npc_id;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_NPC);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_NPC);
    if( !archive )
    {
        fprintf(stderr, "Failed to decode dat2 npc config group for npc %d\n", task->npc_id);
        PT_EXIT(&task->pt);
    }

    dat2_buildcache_npctypes_init_from_archive(task->bc, archive, &wanted_id, 1);
    RSCache_Dat2DiskArchiveFree(archive);

    rscache_npc = dat2_buildcache_npctype_get(task->bc, task->npc_id);
    if( !rscache_npc )
    {
        fprintf(stderr, "Failed to load dat2 npc %d\n", task->npc_id);
        PT_EXIT(&task->pt);
    }

    torirs_npc = ToriRS_NpctypeFromRSCacheDat2(task->npc_id, rscache_npc);
    if( !torirs_npc )
    {
        fprintf(stderr, "Failed to convert dat2 npc %d\n", task->npc_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_NpctypeAdd(&task->bc->base, task->npc_id, torirs_npc);

    PT_END(&task->pt);
}

static void
Task_Dat2NpcLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2NpcLoad_VTable = {
    .run = Task_Dat2NpcLoad_Run,
    .free = Task_Dat2NpcLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2NpcLoad(
    struct CacheProvider* provider,
    int npc_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2NpcLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_NpctypeHas(provider, npc_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2NpcLoad_VTable;
    strcpy(task->task.name, "Dat2NpcLoad");
    task->bc = dat2_buildcache;
    task->npc_id = npc_id;
    PT_INIT(&task->pt);
    return &task->task;
}
