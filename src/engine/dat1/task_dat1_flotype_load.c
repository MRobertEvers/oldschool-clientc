#include "engine/dat1/dat1_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/torirs_flotype_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Dat1 floor types.
 *
 * A dat1 cache has ONE floor table ("flo.dat" in the config jagfile), which
 * map tiles index from both underlay_id and overlay_id; dat2 later split it
 * into separate underlay and overlay config groups, which is why the provider
 * has two caches. The whole table decodes in one pass (entries are
 * variable-length and only sequentially addressable), so a single task
 * populates both caches for every id at once — later per-id requests find
 * their entry already present and never create a task.
 */

struct Task_Dat1FlotypeLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    int flo_id;
};

static void
task_dat1_flotype_decode_all(struct Task_Dat1FlotypeLoad* task)
{
    struct RSCache_FileListDat* config_jagfile = dat1_buildcache_get_config_jagfile(task->bc);
    struct RSCache_Buffer buffer;
    int data_file_idx;
    int count;

    assert(config_jagfile);

    data_file_idx = RSCache_FileListDatFindFileByName(config_jagfile, "flo.dat");
    if( data_file_idx < 0 )
    {
        fprintf(stderr, "dat1 flo: flo.dat missing from the config jagfile\n");
        return;
    }

    RSCache_BufferInit(
        &buffer,
        (uint8_t*)config_jagfile->files[data_file_idx],
        (uint32_t)config_jagfile->file_sizes[data_file_idx]);

    count = RSCache_BufferG2(&buffer);
    for( int flo_id = 0; flo_id < count; flo_id++ )
    {
        struct RSCache_Dat2ConfigOverlay decoded;
        int consumed;

        memset(&decoded, 0, sizeof(decoded));
        /* The dat2 overlay decoder covers the dat1 opcodes too (3 and 6 are
         * dat-only); it returns how many bytes this entry used. */
        consumed = RSCache_Dat2ConfigOverlayDecodeInplace(
            &decoded,
            (char*)buffer.data + buffer.position,
            (int)(buffer.size - buffer.position));
        buffer.position += (uint32_t)consumed;
        decoded._id = flo_id;

        /* One decoded entry, two owners: each provider cache frees what it
         * holds, so they cannot share an allocation. */
        if( !CacheProvider_FlotypeHas(&task->bc->base, flo_id) )
            CacheProvider_FlotypeAdd(
                &task->bc->base, flo_id, ToriRS_FlotypeFromRSCacheOverlay(flo_id, &decoded));
        if( !CacheProvider_UnderlayHas(&task->bc->base, flo_id) )
            CacheProvider_UnderlayAdd(
                &task->bc->base, flo_id, ToriRS_FlotypeFromRSCacheOverlay(flo_id, &decoded));

        RSCache_Dat2ConfigOverlayFreeInplace(&decoded);
    }
}

static int
Task_Dat1FlotypeLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1FlotypeLoad* task = (struct Task_Dat1FlotypeLoad*)task_base;

    PT_BEGIN(&task->pt);

    if( !dat1_buildcache_get_config_jagfile(task->bc) )
    {
        struct RSCache_FileListDat* config_jagfile = NULL;

        RSCache_IO_Dat1ConfigJagfileLoad(io, 0);
        PT_YIELD(&task->pt);

        config_jagfile = RSCache_IO_Dat1ConfigJagfileDecode(io, 0);
        if( !config_jagfile )
        {
            fprintf(stderr, "Failed to decode dat1 config jagfile for flo %d\n", task->flo_id);
            PT_EXIT(&task->pt);
        }

        dat1_buildcache_set_config_jagfile(task->bc, config_jagfile);
    }

    task_dat1_flotype_decode_all(task);

    PT_END(&task->pt);
}

static void
Task_Dat1FlotypeLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1FlotypeLoad_VTable = {
    .run = Task_Dat1FlotypeLoad_Run,
    .free = Task_Dat1FlotypeLoad_Free,
};

static struct ToriRS_Task*
task_dat1_flotype_load_new(
    struct CacheProvider* provider,
    int flo_id)
{
    struct Task_Dat1FlotypeLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1FlotypeLoad_VTable;
    strncpy(task->task.name, "Dat1FlotypeLoad", sizeof(task->task.name) - 1);
    task->bc = (struct Dat1BuildCache*)provider;
    task->flo_id = flo_id;
    PT_INIT(&task->pt);
    return &task->task;
}

struct ToriRS_Task*
CreateTask_Dat1FlotypeLoad(
    struct CacheProvider* provider,
    int flo_id)
{
    assert(provider);

    if( CacheProvider_FlotypeHas(provider, flo_id) )
        return NULL;
    return task_dat1_flotype_load_new(provider, flo_id);
}

struct ToriRS_Task*
CreateTask_Dat1UnderlayLoad(
    struct CacheProvider* provider,
    int underlay_id)
{
    assert(provider);

    if( CacheProvider_UnderlayHas(provider, underlay_id) )
        return NULL;
    return task_dat1_flotype_load_new(provider, underlay_id);
}
