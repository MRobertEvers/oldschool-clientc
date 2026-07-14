#define HMAP_IMPLEMENTATION

#include "../src2/core/tapi/tapi_dat2.h"
#include "../src2/ioqueue/libtorirs_io.h"
#include "../src2/platforms/platform_x/cachelib_platform.h"
#include "../src2/platforms/platform_x_io_reactor.h"
#include "osrs/rscache/rscache.u.c"
#include <datastruct/hmap.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CacheDat2_ReferenceTable_Entry
{
    int table_id;
    void* data;
};

struct CacheDat2_Model_Entry
{
    int id;
    void* data;
};

struct CacheDat2_ObjectModel_Entry
{
    int object_id;
    void* data;
};

typedef void (*ReferenceTableLoadCallback)(
    void* user,
    struct RSCacheDat2Disk_ReferenceTable* reference_table);

struct CacheDat2
{
    struct HMap* reference_tables;
    struct HMap* models;
    struct HMap* object_models;
};

static struct LibToriRS_Task*
CacheDat2_ReferenceTable_Ensure(
    struct CacheDat2* cache,
    int table_id,
    void* user,
    ReferenceTableLoadCallback callback);

static void
CacheDat2_Model_Add(
    struct CacheDat2* cache,
    int id,
    struct RSCacheDat2A_Model* model)
{
    struct CacheDat2_Model_Entry* entry;

    entry = hmap_search(cache->models, &id, HMAP_INSERT);
    assert(entry && "Must have an entry");
    entry->id = id;
    entry->data = model;
}

static void
CacheDat2_ObjectModel_Add(
    struct CacheDat2* cache,
    int object_id,
    struct RSCacheDat2A_Model* model)
{
    struct CacheDat2_ObjectModel_Entry* entry;
    entry = hmap_search(cache->object_models, &object_id, HMAP_INSERT);
    assert(entry && "Must have an entry");
    entry->object_id = object_id;
    entry->data = model;
}

static void
CacheDat2_ReferenceTable_Add(
    struct CacheDat2* cache,
    int table_id,
    struct RSCacheDat2Disk_ReferenceTable* reference_table)
{
    struct CacheDat2_ReferenceTable_Entry* entry;
    entry = hmap_search(cache->reference_tables, &table_id, HMAP_INSERT);
    assert(entry && "Must have an entry");
    entry->table_id = table_id;
    entry->data = reference_table;
}

static struct RSCacheDat2Disk_ReferenceTable*
CacheDat2_ReferenceTable_Get(
    struct CacheDat2* cache,
    int table_id)
{
    struct CacheDat2_ReferenceTable_Entry* entry;
    entry = hmap_search(cache->reference_tables, &table_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return (struct RSCacheDat2Disk_ReferenceTable*)entry->data;
}

static struct RSCacheDat2A_Model*
CacheDat2_ObjectModel_Get(
    struct CacheDat2* cache,
    int object_id)
{
    struct CacheDat2_ObjectModel_Entry* entry;
    entry = hmap_search(cache->object_models, &object_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return (struct RSCacheDat2A_Model*)entry->data;
}

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

struct Task_AsyncCacheDat2_Model_Load
{
    struct LibToriRS_Task base;
    struct pt pt;
    int id;
    struct CacheDat2* cachedat2;
};

int
Task_AsyncCacheDat2_Model_Load_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_AsyncCacheDat2_Model_Load* task =
        LibToriRS_container_of(base, struct Task_AsyncCacheDat2_Model_Load, base);

    struct RSCacheDat2A_Model* model;

    PT_BEGIN(&task->pt);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchModel(ctx, task->id));
    PT_YIELD(&task->pt);
    model = TAPIDat2_DecodeModel(ctx, 0);

    CacheDat2_Model_Add(task->cachedat2, task->id, model);

    PT_END(&task->pt);
}

static void
Task_AsyncCacheDat2_Model_Load_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_async_cache_dat2_model_load_vtable = {
    .run_fn = Task_AsyncCacheDat2_Model_Load_Run,
    .free_fn = Task_AsyncCacheDat2_Model_Load_Free,
};

struct Task_AsyncCacheDat2_Model_Load*
Task_AsyncCacheDat2_Model_Load_New(
    int id,
    struct CacheDat2* cachedat2)
{
    struct Task_AsyncCacheDat2_Model_Load* task =
        malloc(sizeof(struct Task_AsyncCacheDat2_Model_Load));
    memset(task, 0, sizeof(struct Task_AsyncCacheDat2_Model_Load));
    task->base.vtable = &g_async_cache_dat2_model_load_vtable;
    task->id = id;
    task->cachedat2 = cachedat2;
    PT_INIT(&task->pt);
    return task;
}

struct Task_AsyncCacheDat2_ObjectModel_Load
{
    struct LibToriRS_Task base;
    struct pt pt;
    int object_id;

    struct CacheDat2* cachedat2;

    // Locals
    struct RSCacheDat2Disk_ReferenceTable* reference_table;
    struct RSCacheDat2A_ConfigObject* config_object;
    int model_id;
};

static void
onload_reference_table(
    void* user,
    struct RSCacheDat2Disk_ReferenceTable* reference_table)
{
    struct Task_AsyncCacheDat2_ObjectModel_Load* task;
    task = (struct Task_AsyncCacheDat2_ObjectModel_Load*)user;

    task->reference_table = reference_table;
}

int
Task_AsyncCacheDat2_ObjectModel_Load_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx);

static void
Task_AsyncCacheDat2_ObjectModel_Load_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_async_cache_dat2_object_model_load_vtable = {
    .run_fn = Task_AsyncCacheDat2_ObjectModel_Load_Run,
    .free_fn = Task_AsyncCacheDat2_ObjectModel_Load_Free,
};

struct Task_AsyncCacheDat2_ObjectModel_Load*
Task_AsyncCacheDat2_ObjectModel_Load_New(
    int object_id,
    struct CacheDat2* cachedat2)
{
    struct Task_AsyncCacheDat2_ObjectModel_Load* task =
        malloc(sizeof(struct Task_AsyncCacheDat2_ObjectModel_Load));
    memset(task, 0, sizeof(struct Task_AsyncCacheDat2_ObjectModel_Load));

    task->base.vtable = &g_async_cache_dat2_object_model_load_vtable;

    task->object_id = object_id;
    task->cachedat2 = cachedat2;
    PT_INIT(&task->pt);
    return task;
}

struct Task_AsyncCacheDat2_ReferenceTable_Ensure
{
    struct LibToriRS_Task base;
    struct pt pt;
    int table_id;
    struct CacheDat2* cachedat2;
    void* user;
    ReferenceTableLoadCallback callback;
};

void
Task_AsyncCacheDat2_ReferenceTable_Ensure_Free(struct LibToriRS_Task* base)
{
    free(base);
}

int
Task_AsyncCacheDat2_ReferenceTable_Ensure_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_AsyncCacheDat2_ReferenceTable_Ensure* task;
    struct RSCacheDat2Disk_ReferenceTable* reference_table;

    task = LibToriRS_container_of(base, struct Task_AsyncCacheDat2_ReferenceTable_Ensure, base);

    PT_BEGIN(&task->pt);

    reference_table = CacheDat2_ReferenceTable_Get(task->cachedat2, task->table_id);
    if( !reference_table )
    {
        IO_REQUEST(ctx, 0, TAPIDat2_FetchReferenceTable(ctx, task->table_id));
        PT_YIELD(&task->pt);
        reference_table = TAPIDat2_DecodeReferenceTable(ctx, 0, task->table_id);

        CacheDat2_ReferenceTable_Add(task->cachedat2, task->table_id, reference_table);
        LibToriRS_IOQueueClear(ctx->io);
    }

    task->callback(task->user, reference_table);

    PT_END(&task->pt);
}

static struct LibToriRS_TaskVTable g_async_cache_dat2_reference_table_ensure_vtable = {
    .run_fn = Task_AsyncCacheDat2_ReferenceTable_Ensure_Run,
    .free_fn = Task_AsyncCacheDat2_ReferenceTable_Ensure_Free,
};

struct Task_AsyncCacheDat2_ReferenceTable_Ensure*
Task_AsyncCacheDat2_ReferenceTable_Ensure_New(
    int table_id,
    struct CacheDat2* cachedat2,
    void* user,
    ReferenceTableLoadCallback callback)
{
    struct Task_AsyncCacheDat2_ReferenceTable_Ensure* task =
        malloc(sizeof(struct Task_AsyncCacheDat2_ReferenceTable_Ensure));
    memset(task, 0, sizeof(struct Task_AsyncCacheDat2_ReferenceTable_Ensure));
    task->base.vtable = &g_async_cache_dat2_reference_table_ensure_vtable;

    task->table_id = table_id;
    task->cachedat2 = cachedat2;
    task->user = user;
    task->callback = callback;
    PT_INIT(&task->pt);
    return task;
}

static struct LibToriRS_Task*
CacheDat2_ReferenceTable_Ensure(
    struct CacheDat2* cache,
    int table_id,
    void* user,
    ReferenceTableLoadCallback callback)
{
    return &Task_AsyncCacheDat2_ReferenceTable_Ensure_New(table_id, cache, user, callback)->base;
}

int
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
        CacheDat2_ReferenceTable_Ensure(
            task->cachedat2, RSCacheDat2Disk_Table_Configs, task, onload_reference_table));

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
CacheDat2_Init(struct CacheDat2* cache)
{
    int capacity = 1024;
    struct HashConfig config;

    memset(cache, 0, sizeof(struct CacheDat2));

    void* models_buffer = malloc(capacity);
    config = (struct HashConfig){
        .key_size = sizeof(int),
        .entry_size = sizeof(struct CacheDat2_Model_Entry),
        .buffer = models_buffer,
        .buffer_size = capacity,
    };
    cache->models = hmap_new(&config, 0);

    void* object_models_buffer = malloc(capacity);
    config = (struct HashConfig){
        .key_size = sizeof(int),
        .entry_size = sizeof(struct CacheDat2_ObjectModel_Entry),
        .buffer = object_models_buffer,
        .buffer_size = capacity,
    };
    cache->object_models = hmap_new(&config, 0);

    void* reference_tables_buffer = malloc(capacity);
    config = (struct HashConfig){
        .key_size = sizeof(int),
        .entry_size = sizeof(struct CacheDat2_ReferenceTable_Entry),
        .buffer = reference_tables_buffer,
        .buffer_size = capacity,
    };
    cache->reference_tables = hmap_new(&config, 0);
}

int
main(void)
{
    struct LibToriRS_IOQueue queue = { 0 };

    const char* cache_dir = "/Users/matthewevers/Documents/git_repos/3draster/cache";
    const int cache_mode = CACHE_MODE_DAT2;

    struct RSCacheDat2DiskLib* cachedisk = NULL;
    cachedisk = cachelib_new(cache_mode);
    if( !cachedisk )
    {
        fprintf(stderr, "cachelib_new failed\n");
        return 1;
    }
    if( cachelib_platform_init(cachedisk, cache_dir) != 1 )
    {
        fprintf(stderr, "cachelib_platform_init failed for: %s\n", cache_dir);
        cachelib_free(cachedisk);
        return 1;
    }

    struct LibToriPlatformX_IOReactor* reactor = LibToriPlatformX_IOReactorNew(cachedisk);
    assert(reactor && "Must have a reactor");

    struct LibToriRS_TaskRunner runner = { 0 };
    LibToriRS_TaskRunner_Init(&runner, &queue);

    struct CacheDat2 cache = { 0 };
    CacheDat2_Init(&cache);

    const int test_object_id = 1333;
    struct Task_AsyncCacheDat2_ObjectModel_Load* task =
        Task_AsyncCacheDat2_ObjectModel_Load_New(test_object_id, &cache);

    LibToriRS_TaskRunner_Add(&runner, &task->base);

    while( LibToriRS_TaskRunner_Run(&runner) )
        LibToriPlatformX_IOReactorProcess(reactor, &queue);

    struct RSCacheDat2A_Model* object_model = CacheDat2_ObjectModel_Get(&cache, test_object_id);
    if( !object_model )
    {
        fprintf(stderr, "async_cache: object model load failed for object_id=%d\n", test_object_id);
        LibToriPlatformX_IOReactorFree(reactor);
        return 1;
    }

    fprintf(
        stderr,
        "async_cache: loaded object model object_id=%d vertices=%d faces=%d\n",
        test_object_id,
        object_model->vertex_count,
        object_model->face_count);

    LibToriPlatformX_IOReactorFree(reactor);

    return 0;
}
