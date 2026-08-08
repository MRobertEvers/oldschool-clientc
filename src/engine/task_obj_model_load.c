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
    int render_obj_id;
    int model_id;
    int tex_f;
    int base_obj_id;
    int base_model_id;
    int base_tex_f;
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

    if( resolved_id <= 0 || !CacheProvider_ObjtypeHas(provider, resolved_id) )
        return -1;
    obj = CacheProvider_ObjtypeGet(provider, resolved_id);
    assert(obj);
    if( obj->inventory_model_id <= 0 && obj->cert_template > 0 )
        return obj->cert_template;
    /* A bank placeholder has no model of its own either; unlike a note it has
     * no template paper to draw, so the linked item *is* the render obj — the
     * reference's item-sprite builder generates `placeholderId`'s sprite and
     * blits it with nothing under it. */
    if( obj->inventory_model_id <= 0 && obj->placeholder_template >= 0 &&
        obj->placeholder_link > 0 )
        return obj->placeholder_link;
    return resolved_id;
}

/* Base item id a bank note draws on top of itself (reference genCert certlink /
 * getSprite(certlink, ...) overlay). -1 when the resolved obj is not a note or
 * is not yet resident. */
static int
obj_model_cert_link_id(
    struct CacheProvider* provider,
    int resolved_id)
{
    struct ToriRS_Objtype* obj;

    if( resolved_id <= 0 || !CacheProvider_ObjtypeHas(provider, resolved_id) )
        return -1;
    obj = CacheProvider_ObjtypeGet(provider, resolved_id);
    assert(obj);
    if( obj->inventory_model_id <= 0 && obj->cert_template > 0 && obj->cert_link > 0 )
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

    if( obj_id <= 0 || !CacheProvider_ObjtypeHas(provider, obj_id) )
        return -1;
    obj = CacheProvider_ObjtypeGet(provider, obj_id);
    assert(obj);
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
    if( render_id <= 0 || !CacheProvider_ObjtypeHas(provider, render_id) )
        return -1;

    obj = CacheProvider_ObjtypeGet(provider, render_id);
    assert(obj);
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

    if( model_id <= 0 || !CacheProvider_ModelHas(provider, model_id) )
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

    if( model_id <= 0 || !CacheProvider_ModelHas(provider, model_id) )
        return 0;
    model = CacheProvider_ModelGet(provider, model_id);
    if( !model || !model->face_textures )
        return 0;
    return model->face_count;
}

int
ObjModelLoad_NeedsWork(
    struct CacheProvider* provider,
    int obj_id,
    int count)
{
    int count_obj_id;
    int render_obj_id;
    int model_id;
    int face_count;
    int f;

    assert(provider);
    if( obj_id <= 0 )
        return 0;
    if( !CacheProvider_ObjtypeHas(provider, obj_id) )
        return 1;

    count_obj_id = obj_model_resolve_count_obj_id(provider, obj_id, count);
    if( count_obj_id > 0 && !CacheProvider_ObjtypeHas(provider, count_obj_id) )
        return 1;

    /* A bank note's model lives on its cert template objtype, which must be
     * resident before the model (and the icon) can resolve. */
    render_obj_id = obj_model_render_obj_id(provider, count_obj_id > 0 ? count_obj_id : obj_id);
    if( render_obj_id > 0 && !CacheProvider_ObjtypeHas(provider, render_obj_id) )
        return 1;

    /* A bank note also composites its base item (cert_link) icon on top, so the
     * base objtype, its model, and that model's textures must all load too. */
    {
        int base_obj_id = obj_model_cert_link_id(
            provider, count_obj_id > 0 ? count_obj_id : obj_id);
        if( base_obj_id > 0 )
        {
            int base_model_id;
            int base_faces;

            if( !CacheProvider_ObjtypeHas(provider, base_obj_id) )
                return 1;
            base_model_id = obj_model_objtype_model_id(provider, base_obj_id);
            if( base_model_id > 0 && !CacheProvider_ModelHas(provider, base_model_id) )
                return 1;
            base_faces = obj_model_face_count(provider, base_model_id);
            for( f = 0; f < base_faces; f++ )
            {
                int texture_id = obj_model_face_texture(provider, base_model_id, f);
                if( texture_id >= 0 && !CacheProvider_TextureHas(provider, texture_id) )
                    return 1;
            }
        }
    }

    model_id = obj_model_resolve_inventory_model_id(provider, obj_id, count_obj_id);
    if( model_id > 0 && !CacheProvider_ModelHas(provider, model_id) )
        return 1;

    /* Icon raster reads face textures; they load with the model batch. */
    face_count = obj_model_face_count(provider, model_id);
    for( f = 0; f < face_count; f++ )
    {
        int texture_id = obj_model_face_texture(provider, model_id, f);
        if( texture_id >= 0 && !CacheProvider_TextureHas(provider, texture_id) )
            return 1;
    }
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
        if( ObjModelLoad_NeedsWork(provider, obj_ids[i], count) )
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

        /* Bank note: pull in the cert template objtype whose model it borrows
         * (base obj/count obj are resident now, so render-id resolves). */
        self->render_obj_id = obj_model_render_obj_id(
            self->provider, self->count_obj_id > 0 ? self->count_obj_id : obj_id);
        if( self->render_obj_id > 0 && self->render_obj_id != obj_id &&
            self->render_obj_id != self->count_obj_id )
            TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->render_obj_id));

        /* Bank note: also load the base item (cert_link) objtype + its model +
         * textures — the icon composites the base's icon over the note paper. */
        self->base_obj_id = obj_model_cert_link_id(
            self->provider, self->count_obj_id > 0 ? self->count_obj_id : obj_id);
        if( self->base_obj_id > 0 )
        {
            TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->base_obj_id));
            self->base_model_id =
                obj_model_objtype_model_id(self->provider, self->base_obj_id);
            if( self->base_model_id > 0 )
                TASK_AWAITSELF_IF(CreateTask_ModelLoad(self->provider, self->base_model_id));
            for( self->base_tex_f = 0;
                 self->base_tex_f < obj_model_face_count(self->provider, self->base_model_id);
                 self->base_tex_f++ )
            {
                int base_texture_id = obj_model_face_texture(
                    self->provider, self->base_model_id, self->base_tex_f);
                if( base_texture_id >= 0 &&
                    !CacheProvider_TextureHas(self->provider, base_texture_id) )
                    TASK_AWAITSELF_IF(CreateTask_TextureLoad(self->provider, base_texture_id));
            }
        }

        self->model_id =
            obj_model_resolve_inventory_model_id(self->provider, obj_id, self->count_obj_id);
        if( self->model_id > 0 )
            TASK_AWAITSELF_IF(CreateTask_ModelLoad(self->provider, self->model_id));

        /* One attempt per face: a texture whose load fails stays missing and
         * the icon raster skips those faces instead of looping here. */
        for( self->tex_f = 0;
             self->tex_f < obj_model_face_count(self->provider, self->model_id);
             self->tex_f++ )
        {
            int texture_id =
                obj_model_face_texture(self->provider, self->model_id, self->tex_f);
            if( texture_id >= 0 && !CacheProvider_TextureHas(self->provider, texture_id) )
                TASK_AWAITSELF_IF(CreateTask_TextureLoad(self->provider, texture_id));
        }
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
    task->base_obj_id = -1;
    task->base_model_id = -1;
    PT_INIT(&task->pt);
    return &task->task;
}
