#include "../../../src2/core/tapi/tapi_dat2.h"
#include "../../../src2/ioqueue/libtorirs_io.h"
#include "../async_cache_tasks.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/shared/shared_file_list.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_AsyncCacheDat2_ObjectModel_Load
{
    struct LibToriRS_Task base;
    struct pt pt;
    int object_id;

    struct CacheDat2* cachedat2;

    struct RSCacheDat2Disk_ReferenceTable* reference_table;
    struct RSCacheDat2A_ConfigObject* config_object;
    int model_id;
};

static void
object_model_recolor(
    struct RSCacheDat2A_Model* model,
    struct RSCacheDat2A_ConfigObject* config_object)
{
    assert(model && config_object);

    for( int i = 0; i < config_object->recolor_count; i++ )
    {
        int color_src = config_object->recolors_from[i];
        int color_dst = config_object->recolors_to[i];
        for( int f = 0; f < model->face_count; f++ )
        {
            if( model->face_colors[f] == (uint16_t)color_src )
                model->face_colors[f] = (uint16_t)color_dst;
        }
    }
}

static struct RSCacheDat2A_ConfigObject*
object_model_decode_config(
    struct RSCacheDat2Disk_ReferenceTable* reference_table,
    struct RSCacheDat2Disk_Archive* archive,
    int object_id)
{
    struct RSCacheDat2Disk_ArchiveReference* archive_ref;
    struct RSCacheShared_FileList* filelist;
    struct RSCacheDat2A_ConfigObject* config_object;

    assert(reference_table && archive && "Reference table and archive must be valid");
    archive_ref = &reference_table->archives[RSCacheDat2A_ConfigKind_Object];
    filelist = RSCacheShared_FileListNewFromCacheArchive(archive);
    config_object = NULL;
    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( archive_ref->children.files[i].id != object_id )
            continue;

        assert(i == object_id);

        config_object = calloc(1, sizeof(struct RSCacheDat2A_ConfigObject));
        assert(config_object);

        RSCacheDat2A_ConfigObjectDecodeInplace(
            config_object, filelist->files[i], filelist->file_sizes[i]);
        config_object->_id = object_id;
        break;
    }

    RSCacheShared_FileListFree(filelist);
    return config_object;
}

static void
onload_reference_table(
    void* user,
    struct RSCacheDat2Disk_ReferenceTable* reference_table)
{
    struct Task_AsyncCacheDat2_ObjectModel_Load* task;
    task = (struct Task_AsyncCacheDat2_ObjectModel_Load*)user;

    task->reference_table = reference_table;
}

static int
Task_AsyncCacheDat2_ObjectModel_Load_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_AsyncCacheDat2_ObjectModel_Load* task;
    struct RSCacheDat2A_Model* model;
    struct RSCacheDat2Disk_Archive* archive;

    task = LibToriRS_container_of(base, struct Task_AsyncCacheDat2_ObjectModel_Load, base);

    PT_BEGIN(&task->pt);

    TASK_AWAITEX(
        &task->pt,
        ctx,
        Task_AsyncCacheDat2_ReferenceTable_Ensure_New(
            RSCacheDat2Disk_Table_Configs, task->cachedat2, task, onload_reference_table));

    IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Object));
    PT_YIELD(&task->pt);

    archive = TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Object);
    assert(archive && "DecodeConfigGroup");

    task->config_object =
        object_model_decode_config(task->reference_table, archive, task->object_id);
    assert(task->config_object && "DecodeConfigObject");
    RSCacheDat2Disk_ArchiveFree(archive);
    archive = NULL;

    task->model_id = task->config_object->inventory_model_id;
    assert(task->model_id);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchModel(ctx, task->model_id));
    PT_YIELD(&task->pt);

    model = TAPIDat2_DecodeModel(ctx, 0);
    assert(model && "Model");

    object_model_recolor(model, task->config_object);

    RSCacheDat2A_ConfigObjectFree(task->config_object);
    task->config_object = NULL;

    CacheDat2_ObjectModel_Add(task->cachedat2, task->object_id, model);

    PT_END(&task->pt);
}

static void
Task_AsyncCacheDat2_ObjectModel_Load_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_vtable = {
    .run_fn = Task_AsyncCacheDat2_ObjectModel_Load_Run,
    .free_fn = Task_AsyncCacheDat2_ObjectModel_Load_Free,
};

struct LibToriRS_Task*
Task_AsyncCacheDat2_ObjectModel_Load_New(
    int object_id,
    struct CacheDat2* cachedat2)
{
    struct Task_AsyncCacheDat2_ObjectModel_Load* task =
        malloc(sizeof(struct Task_AsyncCacheDat2_ObjectModel_Load));
    memset(task, 0, sizeof(struct Task_AsyncCacheDat2_ObjectModel_Load));

    task->base.vtable = &g_vtable;

    task->object_id = object_id;
    task->cachedat2 = cachedat2;
    PT_INIT(&task->pt);
    return &task->base;
}
