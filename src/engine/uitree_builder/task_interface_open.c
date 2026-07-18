#include "task_interface_open.h"
#include "task_pack_assets_load.h"

#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "engine/uitree_from_component.h"
#include "game/rs_cs2_host.h"
#include "game/task_cs2_run.h"
#include "inv/inv_manager.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INTERFACE_OPEN_ONLOAD_ARGV_MAX 16
#define INTERFACE_OPEN_ONLOAD_MAX 256
#define INTERFACE_OPEN_SEED_OBJ_MAX 32

struct InterfaceOpenOnLoad
{
    int component_id;
    int script_id;
    int argc;
    int argv[INTERFACE_OPEN_ONLOAD_ARGV_MAX];
};

struct Task_InterfaceOpen
{
    struct ToriRS_Task task;
    struct pt pt;

    struct CacheProvider* provider;
    struct UITree* tree;
    struct RS_CS2Host* host;
    struct InvManager* invs;
    int interface_id;
    struct InterfaceOpenStats* stats;

    struct InterfaceOpenOnLoad onloads[INTERFACE_OPEN_ONLOAD_MAX];
    int onload_count;

    int seed_obj_ids[INTERFACE_OPEN_SEED_OBJ_MAX];
    int seed_obj_count;

    int i;
    int script_id;
};

static int
open_resolve_sprite(
    void* ud,
    int graphic_id)
{
    struct Task_InterfaceOpen* self = (struct Task_InterfaceOpen*)ud;
    assert(self && self->provider);
    if( graphic_id <= 0 )
        return -1;
    if( !CacheProvider_SpriteHas(self->provider, graphic_id) )
    {
        fprintf(stderr, "InterfaceOpen: RS sprite %d not loaded\n", graphic_id);
        return -1;
    }
    return graphic_id;
}

static int
open_resolve_font(
    void* ud,
    int font_id)
{
    (void)ud;
    if( font_id < 0 )
        return -1;
    return font_id;
}

static void
collect_onloads(
    struct Task_InterfaceOpen* self,
    struct ToriRS_ComponentPack const* pack)
{
    assert(self && pack);
    self->onload_count = 0;
    for( int i = 0; i < pack->component_count; i++ )
    {
        struct ToriRS_Component const* src = &pack->components[i];
        if( src->on_load.argc <= 0 )
            continue;
        int script_id = src->on_load.argv[0];
        if( script_id <= 0 )
            continue;
        assert(self->onload_count < INTERFACE_OPEN_ONLOAD_MAX);
        struct InterfaceOpenOnLoad* hook = &self->onloads[self->onload_count++];
        hook->component_id = src->id;
        hook->script_id = script_id;
        hook->argc = src->on_load.argc;
        if( hook->argc > INTERFACE_OPEN_ONLOAD_ARGV_MAX )
            hook->argc = INTERFACE_OPEN_ONLOAD_ARGV_MAX;
        memcpy(hook->argv, src->on_load.argv, (size_t)hook->argc * sizeof(int));
    }
}

static void
collect_seed_objs(struct Task_InterfaceOpen* self)
{
    assert(self && self->invs);
    self->seed_obj_count = 0;

    static int const containers[] = {
        INV_MANAGER_CONTAINER_WORN,
        INV_MANAGER_CONTAINER_BACKPACK,
    };
    for( size_t ci = 0; ci < sizeof(containers) / sizeof(containers[0]); ci++ )
    {
        struct InvContainer const* c = InvManager_FindContainer(self->invs, containers[ci]);
        if( !c || !c->slots )
            continue;
        for( int s = 0; s < c->slot_count; s++ )
        {
            int oid = c->slots[s].obj_id;
            if( oid <= INV_MANAGER_EMPTY_OBJ_ID )
                continue;
            int found = 0;
            for( int j = 0; j < self->seed_obj_count; j++ )
            {
                if( self->seed_obj_ids[j] == oid )
                {
                    found = 1;
                    break;
                }
            }
            if( found )
                continue;
            assert(self->seed_obj_count < INTERFACE_OPEN_SEED_OBJ_MAX);
            self->seed_obj_ids[self->seed_obj_count++] = oid;
        }
    }
}

static int
seed_obj_inventory_model_id(
    struct CacheProvider* provider,
    int obj_id)
{
    struct ToriRS_Objtype* obj;
    if( !provider || obj_id <= 0 )
        return -1;
    obj = CacheProvider_ObjtypeGet(provider, obj_id);
    if( !obj || obj->inventory_model_id <= 0 )
        return -1;
    return obj->inventory_model_id;
}

static int
Task_InterfaceOpen_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_InterfaceOpen* self = (struct Task_InterfaceOpen*)base;
    assert(self->provider);
    assert(self->tree);
    assert(self->host);
    assert(self->invs);
    assert(self->interface_id > 0);

    PT_BEGIN(&self->pt);

    /* 1. Load interface pack. */
    TASK_AWAITSELF_IF(CreateTask_ComponentPackLoad(self->provider, self->interface_id));
    assert(CacheProvider_ComponentPackHas(self->provider, self->interface_id));

    /* 2. Prefetch pack sprites / fonts / models. */
    TASK_AWAITSELF_IF(CreateTask_PackAssetsLoad(self->provider, self->interface_id));

    /* 3. Load seeded inv objs + inventory models. */
    collect_seed_objs(self);
    for( self->i = 0; self->i < self->seed_obj_count; self->i++ )
    {
        int obj_id = self->seed_obj_ids[self->i];
        int model_id;
        TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, obj_id));
        model_id = seed_obj_inventory_model_id(self->provider, obj_id);
        if( model_id > 0 )
            TASK_AWAITSELF_IF(CreateTask_ModelLoad(self->provider, model_id));
    }

    /* 4. Bake pack into UITree + collect on_load hooks. */
    {
        struct ToriRS_ComponentPack* pack =
            CacheProvider_ComponentPackGet(self->provider, self->interface_id);
        assert(pack);
        collect_onloads(self, pack);
        (void)UITree_BuildFromComponentPack(
            self->tree, pack, open_resolve_sprite, open_resolve_font, self);
    }

    /* 5. Layout. */
    UITree_LayoutResolve(
        self->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    /* 6. Run IF3 on_load hooks. */
    for( self->i = 0; self->i < self->onload_count; self->i++ )
    {
        struct InterfaceOpenOnLoad const* hook = &self->onloads[self->i];
        int const* args;
        int arg_count;

        self->script_id = hook->script_id;
        if( self->script_id <= 0 )
            continue;

        if( hook->argc > 1 )
        {
            args = hook->argv + 1;
            arg_count = hook->argc - 1;
        }
        else
        {
            args = NULL;
            arg_count = 0;
        }

        TASK_AWAITSELF_IF(CreateTask_CS2Run(
            self->host,
            self->script_id,
            hook->component_id,
            hook->component_id,
            args,
            arg_count));
    }

    /* 7. Inv + var transmit hooks registered during on_load. */
    TASK_AWAITSELF_IF(CreateTask_CS2InvTransmitDispatch(self->host, -1));
    TASK_AWAITSELF_IF(CreateTask_CS2VarTransmitDispatch(self->host, -1));

    if( self->stats )
    {
        struct ToriRS_ComponentPack* pack =
            CacheProvider_ComponentPackGet(self->provider, self->interface_id);
        self->stats->interface_id = self->interface_id;
        self->stats->pack_component_count = pack ? pack->component_count : 0;
        self->stats->onload_count = self->onload_count;
        self->stats->tree_component_count = self->tree->component_count;
    }

    PT_END(&self->pt);
}

static void
Task_InterfaceOpen_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_InterfaceOpen_VTable = {
    .run = Task_InterfaceOpen_Run,
    .free = Task_InterfaceOpen_Free,
};

struct ToriRS_Task*
CreateTask_InterfaceOpen(
    struct CacheProvider* provider,
    struct UITree* tree,
    struct RS_CS2Host* host,
    struct InvManager* invs,
    int interface_id,
    struct InterfaceOpenStats* stats)
{
    assert(provider);
    assert(tree);
    assert(host);
    assert(invs);
    assert(interface_id > 0);

    struct Task_InterfaceOpen* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_InterfaceOpen_VTable;
    strncpy(task->task.name, "InterfaceOpen", sizeof(task->task.name) - 1);
    task->provider = provider;
    task->tree = tree;
    task->host = host;
    task->invs = invs;
    task->interface_id = interface_id;
    task->stats = stats;
    PT_INIT(&task->pt);
    return &task->task;
}
