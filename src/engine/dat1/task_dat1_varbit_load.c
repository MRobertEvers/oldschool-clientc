#include "engine/dat1/dat1_tasks.h"

#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "varp/varp_manager.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Load every varbit type into the VarPManager, once, at boot — the dat1 half of what
 * CreateTask_Dat2VarbitLoad does.
 *
 * The two eras store the same records differently. dat2 splits config group 14 into one
 * file per varbit id; dat1 packs them all into a single `varbit.dat` inside the config
 * jagfile, as a u16 count followed by that many terminator-delimited records. The
 * per-record shape is identical, which is why one parser serves both:
 * VarPManager_LoadVarbitDat walks the blob, and rscache's cursor-form
 * RSCache_Dat2ConfigVarbitDecode is the same codec for a single record.
 *
 * Worth doing rather than a dat2-only fix: CS1's varbit opcode routes to the same
 * VarPManager (`CS1VM_HOST_REQUEST_VARBIT` -> `VarPManager_GetVarbit` in
 * rs_cs1_host.c), so without this every varbit an old-generation script reads is 0,
 * exactly as it was on dat2.
 */

struct Task_Dat1VarbitLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    struct VarPManager* varps;
};

static int
Task_Dat1VarbitLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1VarbitLoad* task = (struct Task_Dat1VarbitLoad*)task_base;
    struct RSCache_FileListDat* config_jagfile = NULL;
    int data_idx;

    PT_BEGIN(&task->pt);

    if( !dat1_buildcache_get_config_jagfile(task->bc) )
    {
        struct RSCache_FileListDat* loaded = NULL;

        RSCache_IO_Dat1ConfigJagfileLoad(io, 0);
        PT_YIELD(&task->pt);

        loaded = RSCache_IO_Dat1ConfigJagfileDecode(io, 0);
        if( !loaded )
        {
            fprintf(stderr, "varbit: failed to decode the dat1 config jagfile\n");
            PT_EXIT(&task->pt);
        }
        dat1_buildcache_set_config_jagfile(task->bc, loaded);
    }

    config_jagfile = dat1_buildcache_get_config_jagfile(task->bc);
    if( !config_jagfile )
        PT_EXIT(&task->pt);

    data_idx = RSCache_FileListDatFindFileByName(config_jagfile, "varbit.dat");
    if( data_idx < 0 )
    {
        /* Not every dat1 cache carries it. Leave the table empty rather than fail the
         * boot — varbits then read 0, which is the pre-existing behaviour. */
        fprintf(stderr, "varbit: varbit.dat absent from the config jagfile\n");
        PT_EXIT(&task->pt);
    }

    if( !VarPManager_LoadVarbitDat(
            task->varps,
            (const uint8_t*)config_jagfile->files[data_idx],
            (size_t)config_jagfile->file_sizes[data_idx]) )
    {
        fprintf(stderr, "varbit: failed to load varbit.dat\n");
        PT_EXIT(&task->pt);
    }

    printf("varbit load (dat1): %d types from varbit.dat\n", task->varps->varbit_count);

    PT_END(&task->pt);
}

static void
Task_Dat1VarbitLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1VarbitLoad_VTable = {
    .run = Task_Dat1VarbitLoad_Run,
    .free = Task_Dat1VarbitLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat1VarbitLoad(
    struct CacheProvider* provider,
    struct VarPManager* varps)
{
    assert(provider);
    assert(varps);

    struct Task_Dat1VarbitLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1VarbitLoad_VTable;
    strcpy(task->task.name, "Dat1VarbitLoad");
    task->bc = (struct Dat1BuildCache*)provider;
    task->varps = varps;
    PT_INIT(&task->pt);
    return &task->task;
}
