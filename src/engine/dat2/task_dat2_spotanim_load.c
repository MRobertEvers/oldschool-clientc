#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_spotanimtype_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2SpotanimLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int spotanim_id;
};

/* Decode a single spotanim id straight out of the config group archive and hand
 * the converted type to the provider. Unlike the npc/obj path there is no dat2
 * buildcache spotanim store: the provider caches the ToriRS type and
 * CacheProvider_SpotanimtypeHas gates re-loads, so the raw config is transient.
 * The group archive itself is LRU-cached in the IO layer. */
static int
Task_Dat2SpotanimLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2SpotanimLoad* task = (struct Task_Dat2SpotanimLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct RSCache_Dat2ConfigSpotanim* rscache_spotanim = NULL;
    struct ToriRS_Spotanimtype* torirs_spotanim = NULL;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_SPOTANIM);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_SPOTANIM);
    if( !archive || !archive->file_ids )
    {
        fprintf(stderr, "Failed to decode dat2 spotanim config group for %d\n", task->spotanim_id);
        PT_EXIT(&task->pt);
    }

    filelist = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( filelist )
    {
        for( int i = 0; i < filelist->file_count; i++ )
        {
            if( archive->file_ids[i] != task->spotanim_id )
                continue;
            rscache_spotanim = RSCache_Dat2ConfigSpotanimNewDecode(
                archive->revision, filelist->files[i], filelist->file_sizes[i]);
            break;
        }
    }

    if( rscache_spotanim )
    {
        torirs_spotanim =
            ToriRS_SpotanimtypeFromRSCacheDat2(task->spotanim_id, rscache_spotanim);
        RSCache_Dat2ConfigSpotanimFree(rscache_spotanim);
    }

    if( filelist )
        RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    if( !torirs_spotanim )
    {
        fprintf(stderr, "Failed to load dat2 spotanim %d\n", task->spotanim_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_SpotanimtypeAdd(&task->bc->base, task->spotanim_id, torirs_spotanim);

    PT_END(&task->pt);
}

static void
Task_Dat2SpotanimLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2SpotanimLoad_VTable = {
    .run = Task_Dat2SpotanimLoad_Run,
    .free = Task_Dat2SpotanimLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2SpotanimLoad(
    struct CacheProvider* provider,
    int spotanim_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2SpotanimLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_SpotanimtypeHas(provider, spotanim_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2SpotanimLoad_VTable;
    strcpy(task->task.name, "Dat2SpotanimLoad");
    task->bc = dat2_buildcache;
    task->spotanim_id = spotanim_id;
    PT_INIT(&task->pt);
    return &task->task;
}
