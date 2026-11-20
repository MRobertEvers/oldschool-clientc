#include "task_obj_model_load.h"
#include "mem/torirs_mem.h"

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
    /* Read once per iteration and then read again after several awaits. A
     * protothread resumes by jumping to a `case __LINE__:` inside the loop
     * body (TASK_AWAITEX_IF), so a plain local would be re-entered
     * uninitialised -- these have to live in the task like `i` does. */
    int obj_id;
    int count;
    int count_obj_id;
    int render_obj_id;
    int model_id;
    int base_obj_id;
    int base_model_id;
    /** Loaders fanned out on the provider's frame queue and not yet ended. */
    int pending;
};

static int
obj_model_face_texture(
    struct CacheProvider* provider,
    int model_id,
    int face);

static int
obj_model_face_count(
    struct CacheProvider* provider,
    int model_id);

/*
 * Queue a load for every SD face texture of `model_id` the provider does not
 * hold, as siblings counted on `pending`. The textures of one model are
 * independent reads, so they go out together; the join is the caller's.
 */
static void
obj_model_fanout_textures(
    struct Task_ObjModelLoad* self,
    int model_id)
{
    int const faces = obj_model_face_count(self->provider, model_id);

    assert(self->provider->asset_queue);
    for( int f = 0; f < faces; f++ )
    {
        int const texture_id = obj_model_face_texture(self->provider, model_id, f);
        if( texture_id >= 0 && !CacheProvider_TextureHas(self->provider, texture_id) )
            ToriRS_TaskQueue_AddParallelPoolSubTask(
                self->provider->asset_queue,
                CreateTask_TextureLoad(self->provider, texture_id),
                &self->pending);
    }
}

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
    /* obj_id 0 is a real item; countobj_id > 0 below is the objtype's own
     * "no variant" (count_obj defaults to 0) and stays. */
    if( obj_id < 0 || count <= 1 )
        return -1;
    /* One lookup: CacheProvider_ObjtypeHas is ObjtypeGet != NULL. */
    obj = CacheProvider_ObjtypeGet(provider, obj_id);
    if( !obj )
        return -1;
    for( i = 0; i < 10; i++ )
    {
        if( count >= obj->count_co[i] && obj->count_co[i] != 0 )
            countobj_id = obj->count_obj[i];
    }
    return countobj_id > 0 ? countobj_id : -1;
}

/* The objtype whose model actually renders for a resolved (count-variant) obj:
 * itself, unless it is a bank note (model 0, cert_template set), in which case
 * the note template supplies the model (reference ObjType.genCert). Requires
 * the resolved obj resident; returns -1 if it is not yet loaded. */
static int
obj_model_render_obj_id(
    struct CacheProvider* provider,
    int resolved_id)
{
    struct ToriRS_Objtype* obj;

    if( resolved_id < 0 )
        return -1;
    obj = CacheProvider_ObjtypeGet(provider, resolved_id);
    if( !obj )
        return -1;
    if( obj->inventory_model_id <= 0 && obj->cert_template > 0 )
        return obj->cert_template;
    /* A bank placeholder has no model of its own either; unlike a note it has
     * no template paper to draw, so the linked item *is* the render obj — the
     * reference's item-sprite builder generates `placeholderId`'s sprite and
     * blits it with nothing under it. */
    if( obj->inventory_model_id <= 0 && obj->placeholder_template >= 0 &&
        obj->placeholder_link >= 0 )
        return obj->placeholder_link;
    return resolved_id;
}

/* The quantity a bank note's base item is drawn at (reference
 * getItemSprite(noteLink, 10, ...)); keep in step with the scene bridge. */
#define OBJ_MODEL_CERT_BASE_COUNT 10

/* Base item id a bank note draws on top of itself (reference genCert certlink /
 * getSprite(certlink, ...) overlay). -1 when the resolved obj is not a note or
 * is not yet resident. */
static int
obj_model_cert_link_id(
    struct CacheProvider* provider,
    int resolved_id)
{
    struct ToriRS_Objtype* obj;

    if( resolved_id < 0 )
        return -1;
    obj = CacheProvider_ObjtypeGet(provider, resolved_id);
    if( !obj )
        return -1;
    if( obj->inventory_model_id <= 0 && obj->cert_template > 0 && obj->cert_link >= 0 )
        return obj->cert_link;
    return -1;
}

/* Inventory model id of a resident objtype, or -1. */
static int
obj_model_objtype_model_id(
    struct CacheProvider* provider,
    int obj_id)
{
    struct ToriRS_Objtype* obj;

    if( obj_id < 0 )
        return -1;
    obj = CacheProvider_ObjtypeGet(provider, obj_id);
    if( !obj )
        return -1;
    return obj->inventory_model_id > 0 ? obj->inventory_model_id : -1;
}

static int
obj_model_resolve_inventory_model_id(
    struct CacheProvider* provider,
    int obj_id,
    int count_obj_id)
{
    struct ToriRS_Objtype* obj;
    int resolved_id = count_obj_id > 0 ? count_obj_id : obj_id;
    int render_id;

    assert(provider);
    render_id = obj_model_render_obj_id(provider, resolved_id);
    if( render_id < 0 )
        return -1;

    obj = CacheProvider_ObjtypeGet(provider, render_id);
    if( !obj )
        return -1;
    if( obj->inventory_model_id <= 0 )
        return -1;
    return obj->inventory_model_id;
}

/* Texture id for face `face` of the model, or -1 if none/out of range. */
static int
obj_model_face_texture(
    struct CacheProvider* provider,
    int model_id,
    int face)
{
    struct ToriRS_Model* model;

    if( model_id <= 0 )
        return -1;
    model = CacheProvider_ModelGet(provider, model_id);
    if( !model || !model->face_textures || face < 0 || face >= model->face_count )
        return -1;
    return (int)model->face_textures[face];
}

static int
obj_model_face_count(
    struct CacheProvider* provider,
    int model_id)
{
    struct ToriRS_Model* model;

    if( model_id <= 0 )
        return 0;
    model = CacheProvider_ModelGet(provider, model_id);
    if( !model || !model->face_textures )
        return 0;
    return model->face_count;
}

/* Whether a resident model names a texture that is not resident. A run of
 * faces on one texture asks about that texture once. */
static int
obj_model_textures_missing_in(
    struct CacheProvider* provider,
    struct ToriRS_Model const* model)
{
    int last = -1;

    if( !model->face_textures )
        return 0;
    for( int f = 0; f < model->face_count; f++ )
    {
        int const texture_id = (int)model->face_textures[f];
        if( texture_id < 0 || texture_id == last )
            continue;
        if( !CacheProvider_TextureHas(provider, texture_id) )
            return 1;
        last = texture_id;
    }
    return 0;
}

/* The same for a model id: nothing missing when the model itself is not
 * resident or names none (the caller asks about the model separately). */
static int
obj_model_textures_missing(
    struct CacheProvider* provider,
    int model_id)
{
    struct ToriRS_Model* model;

    if( model_id <= 0 )
        return 0;
    model = CacheProvider_ModelGet(provider, model_id);
    if( !model )
        return 0;
    return obj_model_textures_missing_in(provider, model);
}

int
ObjModelLoad_RenderObjId(
    struct CacheProvider* provider,
    int obj_id,
    int count)
{
    int count_obj_id;

    assert(provider);
    if( obj_id < 0 )
        return obj_id;
    count_obj_id = obj_model_resolve_count_obj_id(provider, obj_id, count);
    return count_obj_id > 0 ? count_obj_id : obj_id;
}

/* The three rules ObjModelLoad_NeedsWork applies, read off an objtype that is
 * already in hand. The id-taking helpers above look the objtype up each time
 * they are asked, which for one plain item was the same lookup six times, and
 * cc_setobject asks 4,230 times when the bank opens. Each must agree with its
 * id-taking twin: obj_model_resolve_count_obj_id, obj_model_render_obj_id,
 * obj_model_cert_link_id. */
static int
obj_model_count_variant_of(
    struct ToriRS_Objtype const* obj,
    int count)
{
    int countobj_id = -1;

    if( count <= 1 )
        return -1;
    for( int i = 0; i < 10; i++ )
    {
        if( count >= obj->count_co[i] && obj->count_co[i] != 0 )
            countobj_id = obj->count_obj[i];
    }
    return countobj_id > 0 ? countobj_id : -1;
}

static int
obj_model_render_id_of(
    struct ToriRS_Objtype const* obj,
    int obj_id)
{
    if( obj->inventory_model_id <= 0 && obj->cert_template > 0 )
        return obj->cert_template;
    if( obj->inventory_model_id <= 0 && obj->placeholder_template >= 0 &&
        obj->placeholder_link >= 0 )
        return obj->placeholder_link;
    return obj_id;
}

int
ObjModelLoad_NeedsWork(
    struct CacheProvider* provider,
    int obj_id,
    int count)
{
    struct ToriRS_Objtype* obj;
    struct ToriRS_Objtype* shown;
    struct ToriRS_Objtype* render;
    struct ToriRS_Model* model;
    int count_obj_id;
    int shown_id;
    int render_obj_id;
    int model_id;

    assert(provider);
    if( obj_id < 0 ) /* obj 0 (Dwarf remains) is a real item */
        return 0;
    obj = CacheProvider_ObjtypeGet(provider, obj_id);
    if( !obj )
        return CacheProvider_ObjtypeAbsent(provider, obj_id) ? 0 : 1;

    /* The objtype the cell shows: the stack's count variant when it has one. */
    count_obj_id = obj_model_count_variant_of(obj, count);
    shown = obj;
    shown_id = obj_id;
    if( count_obj_id > 0 )
    {
        shown = CacheProvider_ObjtypeGet(provider, count_obj_id);
        if( !shown )
            return 1;
        shown_id = count_obj_id;
    }

    /* A bank note's model lives on its cert template objtype, which must be
     * resident before the model (and the icon) can resolve. */
    render_obj_id = obj_model_render_id_of(shown, shown_id);
    render = shown;
    if( render_obj_id != shown_id )
    {
        render = render_obj_id >= 0 ? CacheProvider_ObjtypeGet(provider, render_obj_id) : NULL;
        if( render_obj_id >= 0 && !render )
            return 1;
    }

    /* A bank note also composites its base item (cert_link) icon on top, so the
     * base objtype, its model, and that model's textures must all load too. */
    if( shown->inventory_model_id <= 0 && shown->cert_template > 0 && shown->cert_link >= 0 )
    {
        int base_obj_id = shown->cert_link;
        int base_model_id;

        if( !CacheProvider_ObjtypeHas(provider, base_obj_id) )
            return 1;
        if( obj_model_resolve_count_obj_id(provider, base_obj_id, OBJ_MODEL_CERT_BASE_COUNT) > 0 )
        {
            base_obj_id =
                obj_model_resolve_count_obj_id(provider, base_obj_id, OBJ_MODEL_CERT_BASE_COUNT);
            if( !CacheProvider_ObjtypeHas(provider, base_obj_id) )
                return 1;
        }
        base_model_id = obj_model_objtype_model_id(provider, base_obj_id);
        if( base_model_id > 0 && !CacheProvider_ModelHas(provider, base_model_id) )
            return 1;
        if( obj_model_textures_missing(provider, base_model_id) )
            return 1;
    }

    /* The inventory model of the objtype that is drawn, and its textures:
     * the icon raster reads them, and they load with the model batch. */
    model_id = render && render->inventory_model_id > 0 ? render->inventory_model_id : -1;
    if( model_id <= 0 )
        return 0;
    model = CacheProvider_ModelGet(provider, model_id);
    if( !model )
        return 1;
    return obj_model_textures_missing_in(provider, model);
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
        if( ObjModelLoad_NeedsWork(provider, obj_ids[i], count) )
            return 1;
    }
    return 0;
}

static int
Task_ObjModelLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_ObjModelLoad* self = (struct Task_ObjModelLoad*)base;
    assert(self->provider);
    assert(self->obj_ids);
    assert(self->n > 0);

    PT_BEGIN(&self->pt);

    /*
     * Several objs: one chain each, ALL AT ONCE.
     *
     * The records of ONE obj are a dependency chain (obj -> count obj -> model
     * -> textures) and have to be read in order; the chains of different objs
     * are independent, and this task used to run them nose to tail -- an
     * inventory of twenty-eight items was twenty-eight chains of round trips
     * on a streamed cache. So each obj gets a single-obj instance of this task
     * as a sibling on the frame queue, and this one joins.
     */
    if( self->n > 1 )
    {
        for( int k = 0; k < self->n; k++ )
        {
            if( self->obj_ids[k] < 0 )
                continue;
            ToriRS_TaskQueue_AddParallelPoolSubTask(
                self->provider->asset_queue,
                CreateTask_ObjModelLoad(
                    self->provider, &self->obj_ids[k], self->counts ? &self->counts[k] : NULL, 1),
                &self->pending);
        }
        PT_TASK_JOIN(pending);
        PT_EXIT(&self->pt);
    }

    for( self->i = 0; self->i < self->n; self->i++ )
    {
        self->obj_id = self->obj_ids[self->i];
        self->count = self->counts ? self->counts[self->i] : 1;

        if( self->obj_id < 0 )
            continue;

        PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->obj_id));

        self->count_obj_id =
            obj_model_resolve_count_obj_id(self->provider, self->obj_id, self->count);
        if( self->count_obj_id > 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->count_obj_id));

        /* Bank note: pull in the cert template objtype whose model it borrows
         * (base obj/count obj are resident now, so render-id resolves). */
        self->render_obj_id = obj_model_render_obj_id(
            self->provider, self->count_obj_id > 0 ? self->count_obj_id : self->obj_id);
        if( self->render_obj_id >= 0 && self->render_obj_id != self->obj_id &&
            self->render_obj_id != self->count_obj_id )
            PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->render_obj_id));

        /* Bank note: also load the base item (cert_link) objtype + its model +
         * textures — the icon composites the base's icon over the note paper. */
        self->base_obj_id = obj_model_cert_link_id(
            self->provider, self->count_obj_id > 0 ? self->count_obj_id : self->obj_id);
        if( self->base_obj_id >= 0 )
        {
            PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->base_obj_id));
            /* The base is drawn at quantity 10 (reference
             * getItemSprite(noteLink, 10, ...)): its stack variant, if any. */
            if( obj_model_resolve_count_obj_id(
                    self->provider, self->base_obj_id, OBJ_MODEL_CERT_BASE_COUNT) > 0 )
            {
                self->base_obj_id = obj_model_resolve_count_obj_id(
                    self->provider, self->base_obj_id, OBJ_MODEL_CERT_BASE_COUNT);
                PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->base_obj_id));
            }
            self->base_model_id =
                obj_model_objtype_model_id(self->provider, self->base_obj_id);
            if( self->base_model_id > 0 )
                PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(self->provider, self->base_model_id));
            obj_model_fanout_textures(self, self->base_model_id);
            PT_TASK_JOIN(pending);
        }

        self->model_id = obj_model_resolve_inventory_model_id(
            self->provider, self->obj_id, self->count_obj_id);
        if( self->model_id > 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(self->provider, self->model_id));

        /* One attempt per face: a texture whose load fails stays missing and
         * the icon raster skips those faces instead of looping here. The
         * textures of one model are independent, so they go out together and
         * are joined. */
        obj_model_fanout_textures(self, self->model_id);
        PT_TASK_JOIN(pending);
    }

    PT_END(&self->pt);
}

static void
Task_ObjModelLoad_Free(struct ToriRS_Task* base)
{
    struct Task_ObjModelLoad* self = (struct Task_ObjModelLoad*)base;
    /* Both came from the arena made at creation (obj_ids is never NULL), so
     * this never makes a new one during teardown. */
    ToriMem_Free(ToriRS_TaskMem(base), self->obj_ids);
    if( self->counts )
        ToriMem_Free(ToriRS_TaskMem(base), self->counts);
    ToriMem_Free(ToriMem_Assets(), self);
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
    if( n <= 0 )
        return NULL;
    assert(obj_ids);
    if( !obj_model_batch_needs_work(provider, obj_ids, counts, n) )
        return NULL;

    task = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*task));
    task->task.vtable = &Task_ObjModelLoad_VTable;
    strncpy(task->task.name, "ObjModelLoad", sizeof(task->task.name) - 1);
    task->provider = provider;
    task->n = n;
    /* The batch's own copy dies with the task: its arena. */
    task->obj_ids = ToriMem_Memdup(ToriRS_TaskMem(&task->task), obj_ids, (size_t)n * sizeof(int));
    if( counts )
        task->counts = ToriMem_Memdup(ToriRS_TaskMem(&task->task), counts, (size_t)n * sizeof(int));
    task->count_obj_id = -1;
    task->model_id = -1;
    task->base_obj_id = -1;
    task->base_model_id = -1;
    PT_INIT(&task->pt);
    return &task->task;
}
