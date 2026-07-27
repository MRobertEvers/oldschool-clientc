#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_enum_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2EnumLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int enum_id;
    struct RSCache_RecordAddress addr;
};

static int
Task_Dat2EnumLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2EnumLoad* task = (struct Task_Dat2EnumLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct RSCache_Dat2ConfigEnum entry = { 0 };
    struct ToriRS_Enum* torirs = NULL;
    int found = 0;

    PT_BEGIN(&task->pt);

    /* OSRS keeps every enum as a file in config group 8; RS2 promotes them to
     * their own table, 256 per group (void's EnumDecoder: `id ushr 8`). Same
     * split as locs — see Task_Dat2LocLoad_Run. */
    task->addr = RSCache_RecordAddressFor(
        CacheProvider_Profile(&task->bc->base), RSCACHE_TYPE_ENUM);

    if( task->addr.group_shift == 0 )
    {
        RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_ENUM);
        PT_YIELD(&task->pt);
        archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_ENUM);
    }
    else
    {
        RSCache_IO_Dat2RecordGroupLoad(
            io, 0, task->addr.table, task->enum_id >> task->addr.group_shift);
        PT_YIELD(&task->pt);
        archive = RSCache_IO_Dat2RecordGroupDecode(io, 0, task->addr.table);
    }

    if( !archive )
    {
        fprintf(stderr, "Failed to decode dat2 enum group for enum %d\n", task->enum_id);
        PT_EXIT(&task->pt);
    }

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        fprintf(stderr, "Failed to filelist dat2 enum group for enum %d\n", task->enum_id);
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    /* Sharded file ids are group-relative, so match on the low bits. */
    for( int i = 0; i < filelist->file_count; i++ )
    {
        int want = task->addr.group_shift ? (task->enum_id & task->addr.file_mask) : task->enum_id;
        if( archive->file_ids[i] != want )
            continue;
        entry.id = task->enum_id;
        RSCache_Dat2ConfigEnumDecodeInplace(
            &entry, filelist->files[i], filelist->file_sizes[i]);
        found = 1;
        break;
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    if( !found )
    {
        fprintf(stderr, "Failed to find dat2 enum %d in config group\n", task->enum_id);
        PT_EXIT(&task->pt);
    }

    torirs = ToriRS_EnumFromRSCacheDat2(task->enum_id, &entry);
    RSCache_Dat2ConfigEnumFreeInplace(&entry);
    if( !torirs )
    {
        fprintf(stderr, "Failed to convert dat2 enum %d\n", task->enum_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_EnumAdd(&task->bc->base, task->enum_id, torirs);

    PT_END(&task->pt);
}

static void
Task_Dat2EnumLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2EnumLoad_VTable = {
    .run = Task_Dat2EnumLoad_Run,
    .free = Task_Dat2EnumLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2EnumLoad(
    struct CacheProvider* provider,
    int enum_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2EnumLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_EnumHas(provider, enum_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2EnumLoad_VTable;
    strcpy(task->task.name, "Dat2EnumLoad");
    task->bc = dat2_buildcache;
    task->enum_id = enum_id;
    PT_INIT(&task->pt);
    return &task->task;
}
