#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat2_buildcache.h"
#include "core/tapi/tapi_dat2.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/core/tasks/dat2/task_dat2_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2ConfigEntryLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* cache;
    int config_kind;
    int* ids;
    int id_count;
};

static const char*
task_dat2_config_kind_name(int config_kind)
{
    switch( config_kind )
    {
    case RSCacheDat2A_ConfigKind_Params:
        return "params";
    case RSCacheDat2A_ConfigKind_Enum:
        return "enum";
    case RSCacheDat2A_ConfigKind_Struct:
        return "struct";
    case RSCacheDat2A_ConfigKind_Object:
        return "object";
    default:
        return "unknown";
    }
}

static int
Task_Dat2ConfigEntryLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2ConfigEntryLoad* task =
        LibToriRS_container_of(base, struct Task_Dat2ConfigEntryLoad, base);
    struct Dat2BuildCache* bc = NULL;
    struct RSCacheDat2Disk_Archive* archive = NULL;

    if( task->cache && ToriAuxLibCache_Mode(task->cache) == TORIAUXLIBCACHE_MODE_DAT2 )
        bc = dat2(task->cache);

    PT_BEGIN(&task->pt);

    if( !bc || task->config_kind < 0 || task->id_count <= 0 || !task->ids )
    {
        fprintf(
            stderr,
            "task_dat2_config_entry_load: invalid task (kind=%d id_count=%d)\n",
            task->config_kind,
            task->id_count);
        PT_EXIT(&task->pt);
    }

    DAT2_ENSURE_CONFIGS_REFERENCE_TABLE(ctx, &task->pt, task->cache);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, task->config_kind));
    PT_YIELD(&task->pt);

    archive = TAPIDat2_DecodeConfigGroup(ctx, 0, task->config_kind);
    if( !archive )
    {
        fprintf(
            stderr,
            "task_dat2_config_entry_load: config %s archive decode failed (id_count=%d)\n",
            task_dat2_config_kind_name(task->config_kind),
            task->id_count);
        LibToriRS_IOQueueClear(ctx->io);
        PT_EXIT(&task->pt);
    }

    switch( task->config_kind )
    {
    case RSCacheDat2A_ConfigKind_Params:
        dat2_buildcache_params_init_from_archive_ids(bc, archive, task->ids, task->id_count);
        break;
    case RSCacheDat2A_ConfigKind_Enum:
        dat2_buildcache_enums_init_from_archive_ids(bc, archive, task->ids, task->id_count);
        break;
    case RSCacheDat2A_ConfigKind_Struct:
        dat2_buildcache_structs_init_from_archive_ids(bc, archive, task->ids, task->id_count);
        break;
    case RSCacheDat2A_ConfigKind_Object:
        dat2_buildcache_objects_init_from_archive(bc, archive, task->ids, task->id_count);
        break;
    default:
        fprintf(
            stderr, "task_dat2_config_entry_load: unhandled config kind %d\n", task->config_kind);
        break;
    }
    RSCacheDat2Disk_ArchiveFree(archive);

    LibToriRS_IOQueueClear(ctx->io);
    PT_END(&task->pt);
}

static void
Task_Dat2ConfigEntryLoad_Free(struct LibToriRS_Task* base)
{
    struct Task_Dat2ConfigEntryLoad* task =
        LibToriRS_container_of(base, struct Task_Dat2ConfigEntryLoad, base);
    free(task->ids);
    free(task);
}

static struct LibToriRS_TaskVTable g_task_dat2_config_entry_load_vtable = {
    .run_fn = Task_Dat2ConfigEntryLoad_Run,
    .free_fn = Task_Dat2ConfigEntryLoad_Free,
};

struct LibToriRS_Task*
Task_Dat2ConfigEntryLoad_New(
    struct ToriAuxLibCache* cache,
    int config_kind,
    const int* ids,
    int id_count)
{
    struct Task_Dat2ConfigEntryLoad* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;

    task->base.vtable = &g_task_dat2_config_entry_load_vtable;
    task->cache = cache;
    task->config_kind = config_kind;
    task->id_count = id_count;
    if( id_count > 0 && ids )
    {
        task->ids = malloc((size_t)id_count * sizeof(int));
        if( !task->ids )
        {
            free(task);
            return NULL;
        }
        memcpy(task->ids, ids, (size_t)id_count * sizeof(int));
    }
    PT_INIT(&task->pt);
    return &task->base;
}
