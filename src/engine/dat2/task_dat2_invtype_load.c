#include "asyncio.h"
#include "cache/rscache_io.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_group_await.h"
#include "engine/dat2/dat2_tasks.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * Load one inventory type capacity lazily.
 *
 * INV_SIZE reads a type definition, not live container contents. The opcode
 * cannot perform IO itself, so its task-runner seam awaits this loader on the
 * first lookup and retries against CacheProvider_InvtypeGet afterwards.
 *
 * Config group 5 is small and immutable. Dat2GroupCache keeps its split form,
 * so later ids avoid both disk IO and the per-member allocation pass. Missing
 * ids are deliberately cached with size 0: absence is an answer, and without
 * that negative cache a script asking for an unknown type would yield forever.
 */

struct Task_Dat2InvtypeLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int inv_id;
    struct RSCache_RecordAddress addr;
    /* Borrowed from the buildcache's split-group LRU; never freed here. */
    struct Dat2Group const* group;
    int group_table;
    int group_id;
};

static int
Task_Dat2InvtypeLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2InvtypeLoad* task = (struct Task_Dat2InvtypeLoad*)task_base;
    struct RSCache_Dat2ConfigInv entry = { 0 };
    int file_id;
    int pos;
    int size = 0;

    PT_BEGIN(&task->pt);

    task->addr = RSCache_RecordAddressFor(CacheProvider_Profile(&task->bc->base), RSCACHE_TYPE_INV);
    task->group_table = task->addr.group_shift ? task->addr.table : RSCACHE_DAT2_TABLE_CONFIGS;
    task->group_id =
        task->addr.group_shift ? (task->inv_id >> task->addr.group_shift) : task->addr.group;
    if( task->group_id < 0 )
        task->group_id = RSCACHE_DAT2_CONFIG_KIND_INV;

    DAT2_GROUP_AWAIT(
        &task->pt, io, 0, task->bc->group_cache, task->group_table, task->group_id, task->group);

    if( task->group )
    {
        file_id = task->addr.group_shift ? (task->inv_id & task->addr.file_mask) : task->inv_id;
        pos = Dat2Group_IndexOf(task->group, file_id);
        if( pos >= 0 && task->group->filelist->file_sizes[pos] > 0 )
        {
            entry.id = task->inv_id;
            RSCache_Dat2ConfigInvDecodeInplace(
                &entry, task->group->filelist->files[pos], task->group->filelist->file_sizes[pos]);
            if( entry._consumed != task->group->filelist->file_sizes[pos] )
                TORIRS_LOG("invtype %d: decode consumed %d of %d bytes\n",
                    task->inv_id,
                    entry._consumed,
                    task->group->filelist->file_sizes[pos]);
            size = entry.size;
            RSCache_Dat2ConfigInvFreeInplace(&entry);
        }
    }
    else
    {
        TORIRS_ERR("Failed to decode dat2 inventory type group for inv %d\n", task->inv_id);
    }

    CacheProvider_InvtypeAdd(&task->bc->base, task->inv_id, size);

    PT_END(&task->pt);
}

static void
Task_Dat2InvtypeLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2InvtypeLoad_VTable = {
    .run = Task_Dat2InvtypeLoad_Run,
    .free = Task_Dat2InvtypeLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2InvtypeLoad(
    struct CacheProvider* provider,
    int inv_id)
{
    struct Task_Dat2InvtypeLoad* task;
    int cached_size;

    assert(provider);

    if( inv_id < 0 || CacheProvider_InvtypeGet(provider, inv_id, &cached_size) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2InvtypeLoad_VTable;
    strcpy(task->task.name, "Dat2InvtypeLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->inv_id = inv_id;
    PT_INIT(&task->pt);
    return &task->task;
}
