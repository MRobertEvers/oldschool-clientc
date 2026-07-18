#include "task_pack_assets_load.h"

#include "engine/cache_provider.h"
#include "engine/torirs_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_PackAssetsLoad
{
    struct ToriRS_Task task;
    struct pt pt;

    struct CacheProvider* provider;
    int iface_id;

    int* sprite_ids;
    int sprite_count;
    int sprite_cap;

    int* font_ids;
    int font_count;
    int font_cap;

    int* model_ids;
    int model_count;
    int model_cap;

    int* npc_ids;
    int npc_count;
    int npc_cap;

    int i;
};

static int
unique_id_add(
    int** ids,
    int* count,
    int* cap,
    int id)
{
    assert(ids && count && cap);
    if( id < 0 )
        return 0;
    for( int i = 0; i < *count; i++ )
    {
        if( (*ids)[i] == id )
            return 0;
    }
    if( *count >= *cap )
    {
        int n = *cap == 0 ? 16 : *cap * 2;
        int* grown = realloc(*ids, (size_t)n * sizeof(int));
        assert(grown);
        *ids = grown;
        *cap = n;
    }
    (*ids)[(*count)++] = id;
    return 1;
}

static void
collect_from_pack(
    struct Task_PackAssetsLoad* self,
    struct ToriRS_ComponentPack const* pack)
{
    assert(self && pack);
    for( int i = 0; i < pack->component_count; i++ )
    {
        struct ToriRS_Component const* c = &pack->components[i];
        if( c->graphic > 0 )
            unique_id_add(&self->sprite_ids, &self->sprite_count, &self->sprite_cap, c->graphic);
        if( c->graphic_active > 0 )
            unique_id_add(
                &self->sprite_ids, &self->sprite_count, &self->sprite_cap, c->graphic_active);
        for( int s = 0; s < TORIRS_INV_SLOT_MAX; s++ )
        {
            if( c->inv_slot_graphic_id[s] > 0 )
                unique_id_add(
                    &self->sprite_ids,
                    &self->sprite_count,
                    &self->sprite_cap,
                    c->inv_slot_graphic_id[s]);
        }
        if( c->font_id >= 0 )
            unique_id_add(&self->font_ids, &self->font_count, &self->font_cap, c->font_id);
        if( c->model_type == 1 && c->model_id >= 0 )
            unique_id_add(&self->model_ids, &self->model_count, &self->model_cap, c->model_id);
        else if( c->model_type == 2 && c->model_id >= 0 )
            unique_id_add(&self->npc_ids, &self->npc_count, &self->npc_cap, c->model_id);
    }
}

static int
Task_PackAssetsLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_PackAssetsLoad* self = (struct Task_PackAssetsLoad*)base;
    assert(self->provider);

    PT_BEGIN(&self->pt);

    {
        struct ToriRS_ComponentPack* pack =
            CacheProvider_ComponentPackGet(self->provider, self->iface_id);
        assert(pack && "PackAssetsLoad requires ComponentPack already loaded");
        collect_from_pack(self, pack);
    }

    for( self->i = 0; self->i < self->sprite_count; self->i++ )
        TASK_AWAITSELF_IF(CreateTask_SpriteLoad(self->provider, self->sprite_ids[self->i]));

    for( self->i = 0; self->i < self->font_count; self->i++ )
        TASK_AWAITSELF_IF(CreateTask_FontLoad(self->provider, self->font_ids[self->i]));

    for( self->i = 0; self->i < self->model_count; self->i++ )
        TASK_AWAITSELF_IF(CreateTask_ModelLoad(self->provider, self->model_ids[self->i]));

    for( self->i = 0; self->i < self->npc_count; self->i++ )
        TASK_AWAITSELF_IF(CreateTask_NpcLoad(self->provider, self->npc_ids[self->i]));

    PT_END(&self->pt);
}

static void
Task_PackAssetsLoad_Free(struct ToriRS_Task* base)
{
    struct Task_PackAssetsLoad* self = (struct Task_PackAssetsLoad*)base;
    free(self->sprite_ids);
    free(self->font_ids);
    free(self->model_ids);
    free(self->npc_ids);
    free(self);
}

static struct ToriRS_TaskVTable Task_PackAssetsLoad_VTable = {
    .run = Task_PackAssetsLoad_Run,
    .free = Task_PackAssetsLoad_Free,
};

struct ToriRS_Task*
CreateTask_PackAssetsLoad(
    struct CacheProvider* provider,
    int iface_id)
{
    assert(provider);
    if( iface_id <= 0 )
        return NULL;
    if( !CacheProvider_ComponentPackHas(provider, iface_id) )
        return NULL;

    struct Task_PackAssetsLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_PackAssetsLoad_VTable;
    strncpy(task->task.name, "PackAssetsLoad", sizeof(task->task.name) - 1);
    task->provider = provider;
    task->iface_id = iface_id;
    PT_INIT(&task->pt);
    return &task->task;
}
