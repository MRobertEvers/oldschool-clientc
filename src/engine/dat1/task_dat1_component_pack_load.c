#include "asyncio.h"
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

struct Task_Dat1ComponentPackLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    /* Dat1 "iface_id" is the root component id of the requested subtree. */
    int iface_id;
};

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
                fprintf(
                    stderr,
                    "dat1 pack: sprite ref '%s' unresolved (com %d)\n",
                    component->sprite_ref,
                    component->id);
        }
        if( component->sprite_active_ref[0] != '\0' )
            component->graphic_active =
                dat1_buildcache_sprite_ref_acquire(dat1_buildcache, component->sprite_active_ref);
        for( int slot = 0; slot < TORIRS_INV_SLOT_MAX; slot++ )
        {
            if( component->inv_slot_sprite_ref[slot][0] == '\0' )
                continue;
            component->inv_slot_graphic_id[slot] = dat1_buildcache_sprite_ref_acquire(
                dat1_buildcache, component->inv_slot_sprite_ref[slot]);
        }
    }
}

static int
Task_Dat1ComponentPackLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1ComponentPackLoad* task = (struct Task_Dat1ComponentPackLoad*)task_base;
    struct ToriRS_ComponentPack* torirs_pack = NULL;

    PT_BEGIN(&task->pt);

    /* Media jagfile first: component GRAPHIC/INV sprite refs decode from it. */
    if( !dat1_buildcache_get_media_2d_graphics_jagfile(task->bc) )
    {
        struct RSCache_FileListDat* media_jagfile = NULL;

        RSCache_IO_Dat1JagfileLoad(io, 0, RSCACHE_DAT1_CONFIG_MEDIA_2D);
        PT_YIELD(&task->pt);

        media_jagfile = RSCache_IO_Dat1JagfileDecode(io, 0, RSCACHE_DAT1_CONFIG_MEDIA_2D);
        if( !media_jagfile )
        {
            fprintf(
                stderr,
                "Failed to decode dat1 media jagfile for component pack %d\n",
                task->iface_id);
            PT_EXIT(&task->pt);
        }

        dat1_buildcache_set_media_2d_graphics_jagfile(task->bc, media_jagfile);
    }

    if( !dat1_buildcache_get_interfaces_list(task->bc) )
    {
        struct RSCache_FileListDat* interfaces_jagfile = NULL;
        struct RSCache_Dat1ConfigComponentList* interfaces_list = NULL;
        int data_file_idx;

        RSCache_IO_Dat1JagfileLoad(io, 0, RSCACHE_DAT1_CONFIG_INTERFACES);
        PT_YIELD(&task->pt);

        interfaces_jagfile = RSCache_IO_Dat1JagfileDecode(io, 0, RSCACHE_DAT1_CONFIG_INTERFACES);
        if( !interfaces_jagfile )
        {
            fprintf(stderr, "Failed to decode dat1 interfaces jagfile\n");
            PT_EXIT(&task->pt);
        }

        data_file_idx = RSCache_FileListDatFindFileByName(interfaces_jagfile, "data");
        if( data_file_idx == -1 )
        {
            fprintf(stderr, "dat1 interfaces jagfile has no \"data\" file\n");
            RSCache_FileListDatFree(interfaces_jagfile);
            PT_EXIT(&task->pt);
        }

        interfaces_list = RSCache_Dat1ConfigComponentListNewDecode(
            interfaces_jagfile->files[data_file_idx],
            interfaces_jagfile->file_sizes[data_file_idx]);
        RSCache_FileListDatFree(interfaces_jagfile);
        if( !interfaces_list )
        {
            fprintf(stderr, "Failed to decode dat1 interfaces list\n");
            PT_EXIT(&task->pt);
        }

        dat1_buildcache_set_interfaces_list(task->bc, interfaces_list);

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
            fprintf(
                stderr,
                "dat1 interfaces: count=%d nulls=%d first_null=%d last_null=%d\n",
                interfaces_list->components_count,
                nulls,
                first_null,
                last_null);
        }
    }

    torirs_pack = ToriRS_ComponentPackFromRSCacheDat1(
        dat1_buildcache_get_interfaces_list(task->bc), task->iface_id);
    if( !torirs_pack )
    {
        struct RSCache_Dat1ConfigComponentList* dbg_list =
            dat1_buildcache_get_interfaces_list(task->bc);
        fprintf(
            stderr,
            "Failed to convert dat1 component pack %d (list count=%d entry=%s)\n",
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
    free(task_base);
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

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1ComponentPackLoad_VTable;
    strcpy(task->task.name, "Dat1ComponentPackLoad");
    task->bc = dat1_buildcache;
    task->iface_id = iface_id;
    PT_INIT(&task->pt);
    return &task->task;
}
