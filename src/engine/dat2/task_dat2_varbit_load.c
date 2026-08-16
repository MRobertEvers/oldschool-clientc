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
 * Load every varbit type into the VarPManager, once, at boot.
 *
 * A varbit is a named bit range inside a varplayer — `basevar`, `startbit`, `endbit`
 * — and it is how the game packs quest stages, setting toggles and diary progress
 * into shared player variables. `VarPManager` has always known how to resolve one;
 * what was missing was anything to resolve *against*. Its only loader parsed the
 * dat1 `varbit.dat` blob and was called from tests alone, so `varbit_count` stayed 0
 * and `VarPManager_GetVarbit` returned 0 for every id — every CS2 script branching on
 * a varbit took the zero path, and no hook triggering on one ever fired.
 *
 * Unlike the other config loaders here this is whole-group and eager rather than
 * per-id and lazy. Two reasons: `VarPManager_SetVarbitTypes` takes the whole table at
 * once, and a varbit read happens deep inside script execution where there is nowhere
 * to yield to a load.
 */

struct Task_Dat2VarbitLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    struct VarPManager* varps;
    /* RS2 shards varbits across a whole table, so the load walks groups. Both
     * the walk state and the accumulator have to outlive a PT_YIELD. */
    struct RSCache_RecordAddress addr;
    struct RSCache_ReferenceTable* ref;
    int group_index;
    struct VarBitType* types;
    int type_count;
    int decoded;
};

/* Grow the id-indexed table so `id` fits, filling new slots with basevar -1
 * ("absent"; a zeroed slot would read as varp 0). */
static bool
varbit_types_reserve(
    struct Task_Dat2VarbitLoad* task,
    int id)
{
    if( id < task->type_count )
        return true;
    int want = id + 1;
    struct VarBitType* grown = realloc(task->types, (size_t)want * sizeof(*grown));
    assert(grown);
    for( int i = task->type_count; i < want; i++ )
    {
        memset(&grown[i], 0, sizeof(grown[i]));
        grown[i].basevar = -1;
    }
    task->types = grown;
    task->type_count = want;
    return true;
}

static void
varbit_decode_group(
    struct Task_Dat2VarbitLoad* task,
    struct RSCache_Dat2DiskArchive* archive,
    struct RSCache_FileList* filelist,
    int base_id)
{
    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = base_id + archive->file_ids[i];
        if( id < 0 || filelist->file_sizes[i] <= 0 )
            continue;
        if( !varbit_types_reserve(task, id) )
            return;

        struct RSCache_Dat2ConfigVarbit entry;
        memset(&entry, 0, sizeof(entry));
        RSCache_Dat2ConfigVarbitDecodeInplace(
            &entry, filelist->files[i], filelist->file_sizes[i]);

        if( entry._consumed != filelist->file_sizes[i] )
            fprintf(
                stderr,
                "varbit %d: decode consumed %d of %d bytes\n",
                id,
                entry._consumed,
                filelist->file_sizes[i]);

        task->types[id].basevar = entry.basevar;
        task->types[id].startbit = entry.startbit;
        task->types[id].endbit = entry.endbit;
        task->decoded++;

        RSCache_Dat2ConfigVarbitFreeInplace(&entry);
    }
}

static int
Task_Dat2VarbitLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2VarbitLoad* task = (struct Task_Dat2VarbitLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct VarBitType* types = NULL;
    int type_count = 0;
    int decoded = 0;

    PT_BEGIN(&task->pt);

    /* OSRS packs every varbit into config group 14; RS2 gives them their own
     * table, 1024 per group (void's VarBitDecoder: `id ushr 10`). The sharded
     * form needs every group, not just one, so it walks the reference table. */
    task->addr = RSCache_RecordAddressFor(
        CacheProvider_Profile(&task->bc->base), RSCACHE_TYPE_VARBIT);

    if( task->addr.group_shift != 0 )
    {
        RSCache_IO_Dat2ReferenceTableLoad(io, 0, task->addr.table);
        PT_YIELD(&task->pt);
        task->ref = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
        if( !task->ref )
        {
            fprintf(stderr, "varbit: table %d absent; varbits will read 0\n", task->addr.table);
            PT_EXIT(&task->pt);
        }

        for( task->group_index = 0; task->group_index < task->ref->id_count; task->group_index++ )
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
                varbit_decode_group(
                    task,
                    archive,
                    filelist,
                    task->ref->ids[task->group_index] << task->addr.group_shift);
            RSCache_FileListFree(filelist);
            RSCache_Dat2DiskArchiveFree(archive);
        }

        RSCache_ReferenceTableFree(task->ref);
        task->ref = NULL;

        if( !VarPManager_SetVarbitTypes(task->varps, task->types, task->type_count) )
            fprintf(stderr, "varbit: failed to install %d types\n", task->type_count);
        else
            printf("varbit load: %d types (%d records, ids 0..%d)\n", task->type_count,
                   task->decoded, task->type_count - 1);
        free(task->types);
        task->types = NULL;
        PT_EXIT(&task->pt);
    }

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_VARBIT);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_VARBIT);
    if( !archive )
    {
        /* Not every cache carries the group. Leave the table empty rather than fail
         * the boot: that is the pre-existing behaviour, just now on purpose. */
        fprintf(stderr, "varbit: config group absent; varbits will read 0\n");
        PT_EXIT(&task->pt);
    }

    filelist =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        fprintf(stderr, "varbit: failed to split the config group\n");
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    /*
     * The table is indexed by varbit id, and ids are neither dense nor ordered — the
     * group's files carry their own ids — so size it by the largest id present and
     * leave the holes zeroed. A zeroed entry has basevar 0, which GetVarbit treats as
     * varp 0 rather than "absent"; that is why the loop below writes basevar -1 into
     * every slot first.
     */
    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( archive->file_ids[i] >= type_count )
            type_count = archive->file_ids[i] + 1;
    }

    if( type_count > 0 )
    {
        types = calloc((size_t)type_count, sizeof(*types));
        assert(types);
        for( int i = 0; i < type_count; i++ )
            types[i].basevar = -1;
    }

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];
        if( id < 0 || id >= type_count || filelist->file_sizes[i] <= 0 )
            continue;

        struct RSCache_Dat2ConfigVarbit entry;
        memset(&entry, 0, sizeof(entry));
        RSCache_Dat2ConfigVarbitDecodeInplace(
            &entry, filelist->files[i], filelist->file_sizes[i]);

        if( entry._consumed != filelist->file_sizes[i] )
            fprintf(
                stderr,
                "varbit %d: decode consumed %d of %d bytes\n",
                id,
                entry._consumed,
                filelist->file_sizes[i]);

        types[id].basevar = entry.basevar;
        types[id].startbit = entry.startbit;
        types[id].endbit = entry.endbit;
        decoded++;

        RSCache_Dat2ConfigVarbitFreeInplace(&entry);
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    if( !VarPManager_SetVarbitTypes(task->varps, types, type_count) )
        fprintf(stderr, "varbit: failed to install %d types\n", type_count);
    else
        printf("varbit load: %d types (%d records, ids 0..%d)\n", type_count, decoded,
               type_count - 1);

    free(types);

    PT_END(&task->pt);
}

static void
Task_Dat2VarbitLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2VarbitLoad_VTable = {
    .run = Task_Dat2VarbitLoad_Run,
    .free = Task_Dat2VarbitLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2VarbitLoad(
    struct CacheProvider* provider,
    struct VarPManager* varps)
{
    assert(provider);
    assert(varps);

    struct Task_Dat2VarbitLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2VarbitLoad_VTable;
    strcpy(task->task.name, "Dat2VarbitLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->varps = varps;
    PT_INIT(&task->pt);
    return &task->task;
}
