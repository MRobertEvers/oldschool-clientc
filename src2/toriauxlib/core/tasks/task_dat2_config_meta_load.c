#include "task_dat2_config_meta_load.h"

#include "buildcache/dat2_buildcache.h"
#include "core/tapi/tapi_dat2.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/core/tasks/task_dat2_io.h"

#include <stdlib.h>

struct Task_Dat2ConfigMetaLoad*
Task_Dat2ConfigMetaLoad_New(struct ToriAuxLibCache* cache)
{
    struct Task_Dat2ConfigMetaLoad* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->cache = cache;
    return task;
}

void
Task_Dat2ConfigMetaLoad_Free(struct Task_Dat2ConfigMetaLoad* task)
{
    free(task);
}

int
Task_Dat2ConfigMetaLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2ConfigMetaLoad* task = task_state;
    struct Dat2BuildCache* bc = NULL;
    struct RSCacheDat2Disk_Archive* params_archive = NULL;
    struct RSCacheDat2Disk_Archive* enum_archive = NULL;
    struct RSCacheDat2Disk_Archive* struct_archive = NULL;

    PT_BEGIN(&task->thread);

    if( !task->cache || ToriAuxLibCache_Mode(task->cache) != TORIAUXLIBCACHE_MODE_DAT2 )
        PT_EXIT(&task->thread);

    bc = dat2(task->cache);
    if( !bc )
        PT_EXIT(&task->thread);

    DAT2_ENSURE_CONFIGS_REFERENCE_TABLE(ctx, &task->thread, task->cache);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Params));
    IO_REQUEST(ctx, 1, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Enum));
    IO_REQUEST(ctx, 2, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Struct));
    PT_YIELD(&task->thread);

    params_archive = TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Params);
    enum_archive = TAPIDat2_DecodeConfigGroup(ctx, 1, RSCacheDat2A_ConfigKind_Enum);
    struct_archive = TAPIDat2_DecodeConfigGroup(ctx, 2, RSCacheDat2A_ConfigKind_Struct);

    if( params_archive )
        dat2_buildcache_params_init_from_archive(bc, params_archive);
    if( enum_archive )
        dat2_buildcache_enums_init_from_archive(bc, enum_archive);
    if( struct_archive )
        dat2_buildcache_structs_init_from_archive(bc, struct_archive);

    RSCacheDat2Disk_ArchiveFree(params_archive);
    RSCacheDat2Disk_ArchiveFree(enum_archive);
    RSCacheDat2Disk_ArchiveFree(struct_archive);
    LibToriRS_IOQueueClear(ctx->io);

    PT_END(&task->thread);
}
