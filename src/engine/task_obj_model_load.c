#include "task_obj_model_load.h"

#include "engine/cache_provider.h"
#include "engine/torirs_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_ObjModelLoad
{
    struct ToriRS_Task task;
    struct pt pt;

    struct CacheProvider* provider;
    int* obj_ids;
    int* counts;
    int n;
    int i;
    int count_obj_id;
    int model_id;
};

static int
obj_model_resolve_count_obj_id(
    struct CacheProvider* provider,
    int obj_id,
    int count)
{
    struct ToriRS_Objtype* obj;
    int i;
    int countobj_id = -1;

    assert(provider);
    if( obj_id <= 0 || count <= 1 )
        return -1;
    if( !CacheProvider_ObjtypeHas(provider, obj_id) )
        return -1;

    obj = CacheProvider_ObjtypeGet(provider, obj_id);
    assert(obj);
    for( i = 0; i < 10; i++ )
    {
        if( count >= obj->count_co[i] && obj->count_co[i] != 0 )
            countobj_id = obj->count_obj[i];
    }
    return countobj_id > 0 ? countobj_id : -1;
}

static int
obj_model_resolve_inventory_model_id(
    struct CacheProvider* provider,
    int obj_id,
    int count_obj_id)
{
    struct ToriRS_Objtype* obj;
    int resolved_id = count_obj_id > 0 ? count_obj_id : obj_id;

    assert(provider);
    if( resolved_id <= 0 || !CacheProvider_ObjtypeHas(provider, resolved_id) )
        return -1;

    obj = CacheProvider_ObjtypeGet(provider, resolved_id);
    assert(obj);
    if( obj->inventory_model_id <= 0 )
        return -1;
    return obj->inventory_model_id;
}

static int
obj_model_needs_work(
    struct CacheProvider* provider,
    int obj_id,
    int count)
{
    int count_obj_id;
    int model_id;

    assert(provider);
    if( obj_id <= 0 )
        return 0;
    if( !CacheProvider_ObjtypeHas(provider, obj_id) )
        return 1;

    count_obj_id = obj_model_resolve_count_obj_id(provider, obj_id, count);
    if( count_obj_id > 0 && !CacheProvider_ObjtypeHas(provider, count_obj_id) )
        return 1;

    model_id = obj_model_resolve_inventory_model_id(provider, obj_id, count_obj_id);
    if( model_id > 0 && !CacheProvider_ModelHas(provider, model_id) )
        return 1;
    return 0;
}

static int
obj_model_batch_needs_work(
    struct CacheProvider* provider,
    int const* obj_ids,
    int const* counts,
    int n)
{
    int i;
    assert(provider);
    assert(obj_ids);
    for( i = 0; i < n; i++ )
    {
        int count = counts ? counts[i] : 1;
        if( obj_model_needs_work(provider, obj_ids[i], count) )
            return 1;
    }
    return 0;
}

static int
Task_ObjModelLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_ObjModelLoad* self = (struct Task_ObjModelLoad*)base;
    assert(self->provider);
    assert(self->obj_ids);
    assert(self->n > 0);

    PT_BEGIN(&self->pt);

    for( self->i = 0; self->i < self->n; self->i++ )
    {
        int obj_id = self->obj_ids[self->i];
        int count = self->counts ? self->counts[self->i] : 1;

        if( obj_id <= 0 )
            continue;

        TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, obj_id));

        self->count_obj_id = obj_model_resolve_count_obj_id(self->provider, obj_id, count);
        if( self->count_obj_id > 0 )
            TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->count_obj_id));

        self->model_id =
            obj_model_resolve_inventory_model_id(self->provider, obj_id, self->count_obj_id);
        if( self->model_id > 0 )
            TASK_AWAITSELF_IF(CreateTask_ModelLoad(self->provider, self->model_id));
    }

    PT_END(&self->pt);
}

static void
Task_ObjModelLoad_Free(struct ToriRS_Task* base)
{
    struct Task_ObjModelLoad* self = (struct Task_ObjModelLoad*)base;
    free(self->obj_ids);
    free(self->counts);
    free(self);
}

static struct ToriRS_TaskVTable Task_ObjModelLoad_VTable = {
    .run = Task_ObjModelLoad_Run,
    .free = Task_ObjModelLoad_Free,
};

struct ToriRS_Task*
CreateTask_ObjModelLoad(
    struct CacheProvider* provider,
    int const* obj_ids,
    int const* counts,
    int n)
{
    struct Task_ObjModelLoad* task;

    assert(provider);
    if( !obj_ids || n <= 0 )
        return NULL;
    if( !obj_model_batch_needs_work(provider, obj_ids, counts, n) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ObjModelLoad_VTable;
    strncpy(task->task.name, "ObjModelLoad", sizeof(task->task.name) - 1);
    task->provider = provider;
    task->n = n;
    task->obj_ids = malloc((size_t)n * sizeof(int));
    assert(task->obj_ids);
    memcpy(task->obj_ids, obj_ids, (size_t)n * sizeof(int));
    if( counts )
    {
        task->counts = malloc((size_t)n * sizeof(int));
        assert(task->counts);
        memcpy(task->counts, counts, (size_t)n * sizeof(int));
    }
    task->count_obj_id = -1;
    task->model_id = -1;
    PT_INIT(&task->pt);
    return &task->task;
}
