#include "asyncio.h"
#include "cache/rscache_io.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/torirs_component_from_rscache.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2ComponentPackLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int iface_id;
};

static int
Task_Dat2ComponentPackLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2ComponentPackLoad* task = (struct Task_Dat2ComponentPackLoad*)task_base;
    struct RSCache_Dat2ComponentPack* rscache_pack = NULL;
    struct ToriRS_ComponentPack* torirs_pack = NULL;
    struct RSCache_ReferenceTable* table = NULL;

    PT_BEGIN(&task->pt);

    /* The interfaces table version is the cache revision the if1/if3 field
     * layout is keyed on (see RSCACHE_DAT2_COMPONENT_INDEX_REVISION_237). */
    if( !dat2_buildcache_reference_table_has(task->bc, RSCACHE_DAT2_DISK_TABLE_INTERFACES) )
    {
        RSCache_IO_Dat2ReferenceTableLoad(io, 0, RSCACHE_DAT2_DISK_TABLE_INTERFACES);
        PT_YIELD(&task->pt);

        table = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
        if( !table )
        {
            fprintf(stderr, "Failed to load interfaces reference table\n");
            PT_EXIT(&task->pt);
        }
        dat2_buildcache_reference_table_add(task->bc, RSCACHE_DAT2_DISK_TABLE_INTERFACES, table);
    }

    RSCache_IO_Dat2ComponentPackLoad(io, 0, task->iface_id);
    PT_YIELD(&task->pt);

    table = dat2_buildcache_reference_table_get(task->bc, RSCACHE_DAT2_DISK_TABLE_INTERFACES);
    rscache_pack = RSCache_IO_Dat2ComponentPackDecode(
        io, 0, table ? table->version : RSCACHE_DAT2_COMPONENT_INDEX_REVISION_UNKNOWN);
    if( !rscache_pack )
    {
        fprintf(stderr, "Failed to decode dat2 component pack %d\n", task->iface_id);
        PT_EXIT(&task->pt);
    }

    dat2_buildcache_componentpack_add(task->bc, task->iface_id, rscache_pack);

    torirs_pack = ToriRS_ComponentPackFromRSCacheDat2(rscache_pack);
    if( !torirs_pack )
    {
        fprintf(stderr, "Failed to convert dat2 component pack %d\n", task->iface_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_ComponentPackAdd(&task->bc->base, task->iface_id, torirs_pack);

    PT_END(&task->pt);
}

static void
Task_Dat2ComponentPackLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2ComponentPackLoad_VTable = {
    .run = Task_Dat2ComponentPackLoad_Run,
    .free = Task_Dat2ComponentPackLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2ComponentPackLoad(
    struct CacheProvider* provider,
    int iface_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2ComponentPackLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_ComponentPackHas(provider, iface_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2ComponentPackLoad_VTable;
    strcpy(task->task.name, "Dat2ComponentPackLoad");
    task->bc = dat2_buildcache;
    task->iface_id = iface_id;
    PT_INIT(&task->pt);
    return &task->task;
}
