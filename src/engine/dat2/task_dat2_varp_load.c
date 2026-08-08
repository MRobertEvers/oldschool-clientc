#include "engine/dat2/dat2_tasks.h"

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "varp/varp_manager.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Load every varplayer type into the VarPManager, once, at boot.
 *
 * The twin of CreateTask_Dat2VarbitLoad, and it exists for the same reason: the
 * manager knew how to answer `VarPManager_GetClientcode` and had nothing to
 * answer from. Its only varplayer loader parsed the dat1 `varp.dat` blob and
 * was called from tests alone, so on a dat2 boot `varp_types` stayed NULL and
 * every clientcode read 0 — which is "this varp drives no built-in behaviour".
 *
 * A varplayer's `clientcode` is the whole point of the record for a client: it
 * ties a server-synced variable to behaviour baked into the client rather than
 * to a script. Code 4 is the sound-effect volume slider; 18 and 22 are the
 * Controls panel's two Attack-options dropdowns (rs_attack_option.h). None of
 * those could fire on an OldSchool cache before this loaded.
 *
 * Whole-group and eager for the same two reasons the varbit loader is:
 * VarPManager_SetVarpTypes takes the whole table at once, and the read happens
 * on the packet path where there is nowhere to yield to a load.
 *
 * Note the install ORDER at the call site — this runs before the varbit load.
 * SetVarpTypes reallocates (and therefore zeroes) the var value arrays;
 * SetVarbitTypes does not touch them. Installing varps second would be correct
 * only as long as nothing had written a varp yet, which is a property of the
 * boot sequence rather than of this file.
 */

struct Task_Dat2VarpLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    struct VarPManager* varps;
    /* RS2 shards varplayers across a whole table, so the load walks groups.
     * Both the walk state and the accumulator outlive a PT_YIELD. */
    struct RSCache_RecordAddress addr;
    struct RSCache_ReferenceTable* ref;
    int group_index;
    struct VarPType* types;
    int type_count;
    int decoded;
};

/* Grow the id-indexed table so `id` fits. A zeroed slot reads as clientcode 0,
 * which is exactly right for "this id names no varplayer record". */
static bool
varp_types_reserve(
    struct Task_Dat2VarpLoad* task,
    int id)
{
    if( id < task->type_count )
        return true;
    int want = id + 1;
    struct VarPType* grown = realloc(task->types, (size_t)want * sizeof(*grown));
    if( !grown )
        return false;
    for( int i = task->type_count; i < want; i++ )
        memset(&grown[i], 0, sizeof(grown[i]));
    task->types = grown;
    task->type_count = want;
    return true;
}

static void
varp_decode_one(
    struct Task_Dat2VarpLoad* task,
    int id,
    char const* data,
    int size)
{
    struct RSCache_Dat2ConfigVarplayer entry;

    if( id < 0 || size <= 0 )
        return;
    if( !varp_types_reserve(task, id) )
        return;

    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigVarplayerDecodeInplace(&entry, (uint8_t const*)data, size);

    if( entry._consumed != size )
        fprintf(
            stderr, "varp %d: decode consumed %d of %d bytes\n", id, entry._consumed, size);

    task->types[id].clientcode = entry.clientcode;
    task->decoded++;
}

static void
varp_decode_group(
    struct Task_Dat2VarpLoad* task,
    struct RSCache_Dat2DiskArchive* archive,
    struct RSCache_FileList* filelist,
    int base_id)
{
    for( int i = 0; i < filelist->file_count; i++ )
        varp_decode_one(
            task,
            base_id + archive->file_ids[i],
            filelist->files[i],
            filelist->file_sizes[i]);
}

static void
varp_install(struct Task_Dat2VarpLoad* task)
{
    if( !VarPManager_SetVarpTypes(task->varps, task->types, task->type_count) )
        fprintf(stderr, "varp: failed to install %d types\n", task->type_count);
    else
        printf(
            "varp load: %d types (%d records, ids 0..%d)\n",
            task->type_count,
            task->decoded,
            task->type_count - 1);
    free(task->types);
    task->types = NULL;
    task->type_count = 0;
}

static int
Task_Dat2VarpLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2VarpLoad* task = (struct Task_Dat2VarpLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;

    PT_BEGIN(&task->pt);

    /* OSRS packs every varplayer into config group 16; RS2 gives them their own
     * table. Same split as the varbit loader, and for the same reason. */
    task->addr = RSCache_RecordAddressFor(
        CacheProvider_Profile(&task->bc->base), RSCACHE_TYPE_VARPLAYER);

    if( task->addr.group_shift != 0 )
    {
        RSCache_IO_Dat2ReferenceTableLoad(io, 0, task->addr.table);
        PT_YIELD(&task->pt);
        task->ref = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
        if( !task->ref )
        {
            fprintf(
                stderr,
                "varp: table %d absent; every clientcode will read 0\n",
                task->addr.table);
            PT_EXIT(&task->pt);
        }

        for( task->group_index = 0; task->group_index < task->ref->id_count;
             task->group_index++ )
        {
            RSCache_IO_Dat2RecordGroupLoad(
                io, 0, task->addr.table, task->ref->ids[task->group_index]);
            PT_YIELD(&task->pt);
            archive = RSCache_IO_Dat2RecordGroupDecode(io, 0, task->addr.table);
            if( !archive )
                continue;
            filelist = RSCache_FileListNewFromDecode(
                archive->data, archive->data_size, archive->file_count);
            if( filelist && archive->file_ids )
                varp_decode_group(
                    task,
                    archive,
                    filelist,
                    task->ref->ids[task->group_index] << task->addr.group_shift);
            RSCache_FileListFree(filelist);
            RSCache_Dat2DiskArchiveFree(archive);
        }

        RSCache_ReferenceTableFree(task->ref);
        task->ref = NULL;
        varp_install(task);
        PT_EXIT(&task->pt);
    }

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_VARPLAYER);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_VARPLAYER);
    if( !archive )
    {
        /* Not every cache carries the group. Leaving the table NULL keeps the
         * manager in untyped mode, which is the pre-existing behaviour: varps
         * still store and transmit, they just drive no client behaviour. */
        fprintf(stderr, "varp: config group absent; every clientcode will read 0\n");
        PT_EXIT(&task->pt);
    }

    filelist =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        fprintf(stderr, "varp: failed to split the config group\n");
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    varp_decode_group(task, archive, filelist, 0);

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    varp_install(task);

    PT_END(&task->pt);
}

static void
Task_Dat2VarpLoad_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2VarpLoad* task = (struct Task_Dat2VarpLoad*)task_base;
    free(task->types);
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2VarpLoad_VTable = {
    .run = Task_Dat2VarpLoad_Run,
    .free = Task_Dat2VarpLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2VarpLoad(
    struct CacheProvider* provider,
    struct VarPManager* varps)
{
    assert(provider);
    assert(varps);

    struct Task_Dat2VarpLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2VarpLoad_VTable;
    strcpy(task->task.name, "Dat2VarpLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->varps = varps;
    PT_INIT(&task->pt);
    return &task->task;
}
