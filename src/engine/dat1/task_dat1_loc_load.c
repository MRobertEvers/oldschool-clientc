#include "engine/dat1/dat1_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/torirs_location_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Dat1 loc configs. "loc.dat" holds variable-length entries and "loc.idx" the
 * per-id offsets into it, so one id decodes without touching the rest (unlike
 * flo.dat, which is only sequentially addressable). The payload itself is the
 * same opcode stream dat2 uses, minus the later-era fields — hence the shared
 * decoder under RSCACHE_CONFIG_LOC_DECODE_DAT.
 */

struct Task_Dat1LocLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    int loc_id;
};

static int
Task_Dat1LocLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1LocLoad* task = (struct Task_Dat1LocLoad*)task_base;
    struct RSCache_FileListDatIndexed* loc_index = NULL;
    struct RSCache_Dat2ConfigLoc decoded;
    struct ToriRS_Location* torirs_loc = NULL;
    int offset;

    PT_BEGIN(&task->pt);

    if( !dat1_buildcache_get_config_jagfile(task->bc) )
    {
        struct RSCache_FileListDat* config_jagfile = NULL;

        RSCache_IO_Dat1ConfigJagfileLoad(io, 0);
        PT_YIELD(&task->pt);

        config_jagfile = RSCache_IO_Dat1ConfigJagfileDecode(io, 0);
        if( !config_jagfile )
        {
            fprintf(stderr, "Failed to decode dat1 config jagfile for loc %d\n", task->loc_id);
            PT_EXIT(&task->pt);
        }

        dat1_buildcache_set_config_jagfile(task->bc, config_jagfile);
    }

    loc_index = dat1_buildcache_get_loc_index(task->bc);
    if( !loc_index )
    {
        fprintf(stderr, "dat1 loc: loc.dat/loc.idx missing from the config jagfile\n");
        PT_EXIT(&task->pt);
    }
    if( task->loc_id < 0 || task->loc_id >= loc_index->offset_count )
    {
        fprintf(
            stderr,
            "dat1 loc %d out of range (%d entries)\n",
            task->loc_id,
            loc_index->offset_count);
        PT_EXIT(&task->pt);
    }

    offset = loc_index->offsets[task->loc_id];
    memset(&decoded, 0, sizeof(decoded));
    /* Flags from the cache profile rather than a constant spelled out here, so the
     * one place that knows the era decides. For a dat1 profile this is exactly
     * RSCACHE_CONFIG_LOC_DECODE_DAT, which is what this used to pass literally —
     * the OSRS-era gates cannot fire on a dat1 cache, since 254 and 220 are
     * numbered in different lineages. */
    RSCache_Dat2ConfigLocDecodeInplace(
        &decoded,
        loc_index->data + offset,
        loc_index->data_size - offset,
        RSCache_Dat2ConfigLocFlags(CacheProvider_Profile(&task->bc->base)));
    decoded._id = task->loc_id;

    torirs_loc = ToriRS_LocationFromRSCacheDat2(task->loc_id, &decoded);
    RSCache_Dat2ConfigLocFreeInplace(&decoded);
    if( !torirs_loc )
    {
        fprintf(stderr, "Failed to convert dat1 loc %d\n", task->loc_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_LocationAdd(&task->bc->base, task->loc_id, torirs_loc);

    PT_END(&task->pt);
}

static void
Task_Dat1LocLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1LocLoad_VTable = {
    .run = Task_Dat1LocLoad_Run,
    .free = Task_Dat1LocLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat1LocLoad(
    struct CacheProvider* provider,
    int loc_id)
{
    struct Task_Dat1LocLoad* task;

    assert(provider);

    if( CacheProvider_LocationHas(provider, loc_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1LocLoad_VTable;
    strncpy(task->task.name, "Dat1LocLoad", sizeof(task->task.name) - 1);
    task->bc = (struct Dat1BuildCache*)provider;
    task->loc_id = loc_id;
    PT_INIT(&task->pt);
    return &task->task;
}
