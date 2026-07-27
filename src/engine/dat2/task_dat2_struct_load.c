#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_struct_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2StructLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int struct_id;
    int group;
};

static int
Task_Dat2StructLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2StructLoad* task = (struct Task_Dat2StructLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct RSCache_Dat2ConfigStruct entry = { 0 };
    struct ToriRS_Struct* torirs = NULL;
    int found = 0;

    PT_BEGIN(&task->pt);

    /* Which config group holds structs is era-dependent — OldSchool 34, RS2 26 —
     * so the profile answers it (RSCache_RecordAddressFor). */
    task->group = RSCache_RecordAddressFor(
                      CacheProvider_Profile(&task->bc->base), RSCACHE_TYPE_STRUCT)
                      .group;
    if( task->group < 0 )
        task->group = RSCACHE_DAT2_CONFIG_KIND_STRUCT;

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, task->group);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, task->group);
    if( !archive )
    {
        fprintf(
            stderr, "Failed to decode dat2 struct config group for struct %d\n", task->struct_id);
        PT_EXIT(&task->pt);
    }

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        fprintf(stderr, "Failed to filelist dat2 struct group for struct %d\n", task->struct_id);
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( archive->file_ids[i] != task->struct_id )
            continue;
        entry.id = task->struct_id;
        RSCache_Dat2ConfigStructDecodeInplace(
            &entry, filelist->files[i], filelist->file_sizes[i]);
        found = 1;
        break;
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    if( !found )
    {
        fprintf(stderr, "Failed to find dat2 struct %d in config group\n", task->struct_id);
        PT_EXIT(&task->pt);
    }

    torirs = ToriRS_StructFromRSCacheDat2(task->struct_id, &entry);
    if( !torirs )
    {
        RSCache_Dat2ConfigStructFreeInplace(&entry);
        fprintf(stderr, "Failed to convert dat2 struct %d\n", task->struct_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_StructAdd(&task->bc->base, task->struct_id, torirs);

    PT_END(&task->pt);
}

static void
Task_Dat2StructLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2StructLoad_VTable = {
    .run = Task_Dat2StructLoad_Run,
    .free = Task_Dat2StructLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2StructLoad(
    struct CacheProvider* provider,
    int struct_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2StructLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_StructHas(provider, struct_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2StructLoad_VTable;
    strcpy(task->task.name, "Dat2StructLoad");
    task->bc = dat2_buildcache;
    task->struct_id = struct_id;
    PT_INIT(&task->pt);
    return &task->task;
}
