#include "asyncio.h"
#include "mem/torirs_mem.h"
#include "cache/rscache_io.h"
#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat1/dat1_tasks.h"
#include "engine/torirs_component_from_rscache.h"
#include "engine/torirs_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * A dat1 component pack: the media and interfaces jagfiles read once per
 * provider, the interfaces list decoded once, then the requested subtree
 * converted.
 *
 * The frame contract's split (app.h, THE FRAME CONTRACT): this task is on the
 * provider's asset queue (the parallel queue) and only READS. Decompressing a
 * jagfile and decoding the list -- 4.0 ms and 0.6-0.8 ms of processor time for
 * the interfaces jagfile of the dat1 test cache, every boot (lo-checks), one
 * step over the parallel queue's 4 ms compute check -- is handed, with the
 * archives, to ONE task on the provider's compute queue (the background
 * queue), Dat1ComponentPackDecode, and joined like any fan-out. The
 * conversion of the subtree (0.01-0.1 ms) and its sprite refs (0.0-0.12 ms)
 * stay here, after the join, so a pack whose list is already decoded lands in
 * the same pump as before; only the first pack of a provider waits on a
 * background slice.
 */
struct Task_Dat1ComponentPackLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    /* Dat1 "iface_id" is the root component id of the requested subtree. */
    int iface_id;
    /* Read and still compressed, until handed to the decode. */
    struct RSCache_Dat1DiskArchive* media_archive;
    struct RSCache_Dat1DiskArchive* interfaces_archive;
    /* The decode job, joined. */
    int decoding;
};

/* The CPU half, on the compute queue: decompress what was read, decode the
 * interfaces list, and keep both on the provider. It owns the archives it is
 * given; it stops between the pieces when its slice is spent. */
struct Task_Dat1ComponentPackDecode
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    int iface_id;
    struct RSCache_Dat1DiskArchive* media_archive;
    struct RSCache_Dat1DiskArchive* interfaces_archive;
};

static int
Task_Dat1ComponentPackDecode_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat1ComponentPackDecode* self = (struct Task_Dat1ComponentPackDecode*)task_base;

    PT_BEGIN(&self->pt);

    if( self->media_archive )
    {
        struct RSCache_FileListDat* media_jagfile = RSCache_FileListDatNewFromDecode(
            self->media_archive->data, self->media_archive->data_size);

        RSCache_Dat1DiskArchiveFree(self->media_archive);
        self->media_archive = NULL;
        if( !media_jagfile )
        {
            TORIRS_ERR("Failed to decode dat1 media jagfile for component pack %d\n", self->iface_id);
            PT_EXIT(&self->pt);
        }
        dat1_buildcache_set_media_2d_graphics_jagfile(self->bc, media_jagfile);
        TASK_YIELD_IF_SPENT(&self->task, &self->pt, io);
    }

    if( self->interfaces_archive )
    {
        struct RSCache_FileListDat* interfaces_jagfile = RSCache_FileListDatNewFromDecode(
            self->interfaces_archive->data, self->interfaces_archive->data_size);
        struct RSCache_Dat1ConfigComponentList* interfaces_list = NULL;
        int data_file_idx;

        RSCache_Dat1DiskArchiveFree(self->interfaces_archive);
        self->interfaces_archive = NULL;
        if( !interfaces_jagfile )
        {
            TORIRS_ERR("Failed to decode dat1 interfaces jagfile\n");
            PT_EXIT(&self->pt);
        }

        data_file_idx = RSCache_FileListDatFindFileByName(interfaces_jagfile, "data");
        if( data_file_idx == -1 )
        {
            TORIRS_LOG("dat1 interfaces jagfile has no \"data\" file\n");
            RSCache_FileListDatFree(interfaces_jagfile);
            PT_EXIT(&self->pt);
        }

        interfaces_list = RSCache_Dat1ConfigComponentListNewDecode(
            interfaces_jagfile->files[data_file_idx],
            interfaces_jagfile->file_sizes[data_file_idx]);
        RSCache_FileListDatFree(interfaces_jagfile);
        if( !interfaces_list )
        {
            TORIRS_ERR("Failed to decode dat1 interfaces list\n");
            PT_EXIT(&self->pt);
        }

        dat1_buildcache_set_interfaces_list(self->bc, interfaces_list);

        if( getenv("TORIRS_IF_DEBUG") )
        {
            int nulls = 0, first_null = -1, last_null = -1;
            for( int i = 0; i < interfaces_list->components_count; i++ )
            {
                if( !interfaces_list->components[i] )
                {
                    nulls++;
                    if( first_null < 0 )
                        first_null = i;
                    last_null = i;
                }
            }
            TORIRS_ERR("dat1 interfaces: count=%d nulls=%d first_null=%d last_null=%d\n",
                interfaces_list->components_count,
                nulls,
                first_null,
                last_null);
        }
    }

    PT_END(&self->pt);
}

static void
Task_Dat1ComponentPackDecode_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat1ComponentPackDecode* self = (struct Task_Dat1ComponentPackDecode*)task_base;

    /* A queue freed whole before the job ran still owns the archives. */
    if( self->media_archive )
        RSCache_Dat1DiskArchiveFree(self->media_archive);
    if( self->interfaces_archive )
        RSCache_Dat1DiskArchiveFree(self->interfaces_archive);
    ToriMem_Free(ToriMem_Assets(), task_base);
}

static struct ToriRS_TaskVTable Task_Dat1ComponentPackDecode_VTable = {
    .run = Task_Dat1ComponentPackDecode_Run,
    .free = Task_Dat1ComponentPackDecode_Free,
};

static struct ToriRS_Task*
create_pack_decode(struct Task_Dat1ComponentPackLoad* load)
{
    struct Task_Dat1ComponentPackDecode* task;

    assert(load);
    task = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1ComponentPackDecode_VTable;
    strcpy(task->task.name, "Dat1ComponentPackDecode");
    task->bc = load->bc;
    task->iface_id = load->iface_id;
    task->media_archive = load->media_archive;
    task->interfaces_archive = load->interfaces_archive;
    load->media_archive = NULL;
    load->interfaces_archive = NULL;
    PT_INIT(&task->pt);
    return &task->task;
}

/* Resolve dat1 sprite name refs into provider sprite ids. Runs after conversion
 * (the converter stays IO-free); the media jagfile is resident by then so every
 * decode is synchronous. */
static void
dat1_pack_resolve_sprite_refs(
    struct Dat1BuildCache* dat1_buildcache,
    struct ToriRS_ComponentPack* pack)
{
    for( int i = 0; i < pack->component_count; i++ )
    {
        struct ToriRS_Component* component = &pack->components[i];

        if( component->sprite_ref[0] != '\0' )
        {
            component->graphic =
                dat1_buildcache_sprite_ref_acquire(dat1_buildcache, component->sprite_ref);
            if( component->graphic < 0 && getenv("TORIRS_IF_DEBUG") )
                TORIRS_LOG("dat1 pack: sprite ref '%s' unresolved (com %d)\n",
                    component->sprite_ref,
                    component->id);
        }
        if( component->sprite_active_ref[0] != '\0' )
            component->graphic_active =
                dat1_buildcache_sprite_ref_acquire(dat1_buildcache, component->sprite_active_ref);
        /* A component that is not an inventory has no slot columns at all. */
        for( int slot = 0; component->inv_slots && slot < TORIRS_INV_SLOT_MAX; slot++ )
        {
            if( component->inv_slots->sprite_ref[slot][0] == '\0' )
                continue;
            component->inv_slots->graphic_id[slot] = dat1_buildcache_sprite_ref_acquire(
                dat1_buildcache, component->inv_slots->sprite_ref[slot]);
        }
    }
}

static int
Task_Dat1ComponentPackLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat1ComponentPackLoad* task = (struct Task_Dat1ComponentPackLoad*)task_base;
    struct ToriRS_ComponentPack* torirs_pack = NULL;

    PT_BEGIN(&task->pt);

    /* Media jagfile first: component GRAPHIC/INV sprite refs decode from it. */
    if( !dat1_buildcache_get_media_2d_graphics_jagfile(task->bc) )
    {
        RSCache_IO_Dat1JagfileLoad(io, 0, RSCACHE_DAT1_CONFIG_MEDIA_2D);
        PT_YIELD(&task->pt);

        task->media_archive = RSCache_IO_Dat1JagfileTake(io, 0, RSCACHE_DAT1_CONFIG_MEDIA_2D);
        if( !task->media_archive )
        {
            TORIRS_ERR("Failed to read dat1 media jagfile for component pack %d\n",
                task->iface_id);
            PT_EXIT(&task->pt);
        }
    }

    if( !dat1_buildcache_get_interfaces_list(task->bc) )
    {
        RSCache_IO_Dat1JagfileLoad(io, 0, RSCACHE_DAT1_CONFIG_INTERFACES);
        PT_YIELD(&task->pt);

        task->interfaces_archive = RSCache_IO_Dat1JagfileTake(io, 0, RSCACHE_DAT1_CONFIG_INTERFACES);
        if( !task->interfaces_archive )
        {
            TORIRS_ERR("Failed to read dat1 interfaces jagfile\n");
            PT_EXIT(&task->pt);
        }
    }

    /* The decoding is the compute queue's; this task waits for it as for
     * any fan-out, then converts on what it left on the provider. */
    if( task->media_archive || task->interfaces_archive )
    {
        assert(task->bc->base.compute_queue);
        ToriRS_TaskQueue_AddParallelPoolSubTask(
            task->bc->base.compute_queue, create_pack_decode(task), &task->decoding);
        TASK_AWAIT_JOIN(&task->task, &task->pt, task->decoding);
    }
    if( !dat1_buildcache_get_media_2d_graphics_jagfile(task->bc) ||
        !dat1_buildcache_get_interfaces_list(task->bc) )
        PT_EXIT(&task->pt);

    torirs_pack = ToriRS_ComponentPackFromRSCacheDat1(
        dat1_buildcache_get_interfaces_list(task->bc), task->iface_id);
    if( !torirs_pack )
    {
        struct RSCache_Dat1ConfigComponentList* dbg_list =
            dat1_buildcache_get_interfaces_list(task->bc);
        TORIRS_ERR("Failed to convert dat1 component pack %d (list count=%d entry=%s)\n",
            task->iface_id,
            dbg_list ? dbg_list->components_count : -1,
            dbg_list && task->iface_id >= 0 && task->iface_id < dbg_list->components_count &&
                    dbg_list->components[task->iface_id]
                ? "present"
                : "null");
        PT_EXIT(&task->pt);
    }

    dat1_pack_resolve_sprite_refs(task->bc, torirs_pack);

    CacheProvider_ComponentPackAdd(&task->bc->base, task->iface_id, torirs_pack);

    PT_END(&task->pt);
}

static void
Task_Dat1ComponentPackLoad_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat1ComponentPackLoad* task = (struct Task_Dat1ComponentPackLoad*)task_base;

    /* Read but never handed on (the task was freed in between). */
    if( task->media_archive )
        RSCache_Dat1DiskArchiveFree(task->media_archive);
    if( task->interfaces_archive )
        RSCache_Dat1DiskArchiveFree(task->interfaces_archive);
    ToriMem_Free(ToriMem_Assets(), task_base);
}

static struct ToriRS_TaskVTable Task_Dat1ComponentPackLoad_VTable = {
    .run = Task_Dat1ComponentPackLoad_Run,
    .free = Task_Dat1ComponentPackLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat1ComponentPackLoad(
    struct CacheProvider* provider,
    int iface_id)
{
    struct Dat1BuildCache* dat1_buildcache;
    struct Task_Dat1ComponentPackLoad* task;

    assert(provider);

    dat1_buildcache = (struct Dat1BuildCache*)provider;
    if( CacheProvider_ComponentPackHas(provider, iface_id) )
        return NULL;

    task = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1ComponentPackLoad_VTable;
    strcpy(task->task.name, "Dat1ComponentPackLoad");
    task->bc = dat1_buildcache;
    task->iface_id = iface_id;
    PT_INIT(&task->pt);
    return &task->task;
}
