#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_location_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2LocLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int loc_id;
    struct RSCache_RecordAddress addr;
};

static int
Task_Dat2LocLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2LocLoad* task = (struct Task_Dat2LocLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_Dat2ConfigLoc* rscache_loc = NULL;
    struct ToriRS_Location* torirs_loc = NULL;

    PT_BEGIN(&task->pt);

    /*
     * Where the loc records live is an era question, so the profile answers it rather than
     * this task assuming the OSRS layout.
     *
     * OSRS keeps every loc as a file in config group 6, so one load covers all of them.
     * RS2 (643) promotes locs to table 16 and shards them 256 per group, so a load covers
     * only the requested id's group — which is why the whole-group memo below is keyed on
     * having *this* id rather than on having loaded anything at all.
     */
    task->addr = RSCache_RecordAddressFor(
        CacheProvider_Profile(&task->bc->base), RSCACHE_TYPE_LOC);

    /* The whole group decodes on first touch; later tasks skip the archive
     * load entirely instead of re-decompressing it per id. */
    if( !dat2_buildcache_loc_get(task->bc, task->loc_id) )
    {
        if( task->addr.group_shift == 0 )
        {
            RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_LOCS);
            PT_YIELD(&task->pt);
            archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_LOCS);
        }
        else
        {
            RSCache_IO_Dat2RecordGroupLoad(
                io, 0, task->addr.table, task->loc_id >> task->addr.group_shift);
            PT_YIELD(&task->pt);
            archive = RSCache_IO_Dat2RecordGroupDecode(io, 0, task->addr.table);
        }

        if( !archive )
        {
            fprintf(stderr, "Failed to decode dat2 loc group for loc %d\n", task->loc_id);
            PT_EXIT(&task->pt);
        }

        /*
         * Sharded groups number their files 0..255 within the group, so the ids the
         * archive reports are group-relative. Pass the group's base id so the records land
         * under their global loc id.
         */
        int base_id = task->addr.group_shift
                          ? ((task->loc_id >> task->addr.group_shift) << task->addr.group_shift)
                          : 0;
        dat2_buildcache_locs_init_from_archive_based(task->bc, archive, NULL, 0, base_id);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    rscache_loc = dat2_buildcache_loc_get(task->bc, task->loc_id);
    if( !rscache_loc )
    {
        fprintf(stderr, "Failed to load dat2 loc %d\n", task->loc_id);
        PT_EXIT(&task->pt);
    }

    torirs_loc = ToriRS_LocationFromRSCacheDat2(task->loc_id, rscache_loc);
    if( !torirs_loc )
    {
        fprintf(stderr, "Failed to convert dat2 loc %d\n", task->loc_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_LocationAdd(&task->bc->base, task->loc_id, torirs_loc);

    PT_END(&task->pt);
}

static void
Task_Dat2LocLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2LocLoad_VTable = {
    .run = Task_Dat2LocLoad_Run,
    .free = Task_Dat2LocLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2LocLoad(
    struct CacheProvider* provider,
    int loc_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2LocLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_LocationHas(provider, loc_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2LocLoad_VTable;
    strcpy(task->task.name, "Dat2LocLoad");
    task->bc = dat2_buildcache;
    task->loc_id = loc_id;
    PT_INIT(&task->pt);
    return &task->task;
}
