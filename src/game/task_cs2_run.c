#include "game/task_cs2_run.h"

#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_host.h"
#include "cs2vm2/cs2vm2_script.h"
#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "engine/uitree_from_component.h"
#include "game/rs_cs2_host.h"

#include <3rd/minipt.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TASK_CS2_RUN_INT_ARGS_MAX 64

struct Task_CS2Run
{
    struct ToriRS_Task task;
    struct pt pt;

    struct RS_CS2Host* host;
    struct CacheProvider* provider;
    struct CS2VM2 vm;

    int script_id;
    struct CS2VM2_Script* script; /* optional preloaded; else load by script_id */
    int active_component_id;
    int dot_component_id;
    int int_args[TASK_CS2_RUN_INT_ARGS_MAX];
    int int_arg_count;

    struct CS2VM_HostRequest pending;
    int await_id;
    int yield_obj_id;
    int yield_obj_count;
    int started;
};

static void
task_cs2_set_int_local(
    struct CS2VM2_Thread* thread,
    int local_idx,
    int argi,
    int active_component_id)
{
    switch( argi )
    {
    case CS2VM_SCRIPT_ARG_WIDGET_ID:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, active_component_id);
        break;
    case CS2VM_SCRIPT_ARG_MOUSE_X:
    case CS2VM_SCRIPT_ARG_MOUSE_Y:
    case CS2VM_SCRIPT_ARG_OP_INDEX:
    case CS2VM_SCRIPT_ARG_WIDGET_CHILD_INDEX:
    case CS2VM_SCRIPT_ARG_DRAG_TARGET_ID:
    case CS2VM_SCRIPT_ARG_DRAG_TARGET_CHILD_INDEX:
    case CS2VM_SCRIPT_ARG_KEY_TYPED:
    case CS2VM_SCRIPT_ARG_KEY_PRESSED:
    case CS2VM_SCRIPT_ARG_OP_SUBINDEX:
        /* Leave unset / zero — no live input binding in this task. */
        break;
    default:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, argi);
        break;
    }
}

static int
task_cs2_group_id_from_request(struct CS2VM_HostRequest const* request)
{
    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_CC_CREATE:
        return (request->u.cc_create.parent_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_CC_FIND:
        return (request->u.cc_find.parent_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_IF_FIND:
        return (request->u.if_find.component_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND:
        return (request->u.cc_children_find.parent_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
        return (request->u.if_children_find.uid >> 16) & 0xffff;
    default:
        return -1;
    }
}

static int
task_cs2_obj_inventory_model_id(
    struct CacheProvider* provider,
    int obj_id,
    int count)
{
    struct ToriRS_Objtype* obj;
    int i;
    int countobj_id = -1;

    if( !provider || obj_id < 0 )
        return -1;

    obj = CacheProvider_ObjtypeGet(provider, obj_id);
    if( !obj )
        return -1;

    if( count > 1 )
    {
        for( i = 0; i < 10; i++ )
        {
            if( count >= obj->count_co[i] && obj->count_co[i] != 0 )
                countobj_id = obj->count_obj[i];
        }
        if( countobj_id >= 0 )
        {
            obj = CacheProvider_ObjtypeGet(provider, countobj_id);
            if( !obj )
                return -1;
        }
    }

    if( obj->inventory_model_id <= 0 )
        return -1;
    return obj->inventory_model_id;
}

static int
Task_CS2Run_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_CS2Run* self = (struct Task_CS2Run*)task;
    struct CS2VM2_Thread* thread = NULL;
    enum CS2VM2_ThreadStatus status;
    struct CS2VM2_ThreadError err;
    int j;

    PT_BEGIN(&self->pt);

    assert(self->host);
    assert(self->host->provider);
    self->provider = self->host->provider;

    if( !self->script && self->script_id <= 0 )
        PT_EXIT(&self->pt);

    if( !self->script )
    {
        if( !CacheProvider_ClientScriptHas(self->provider, self->script_id) )
        {
            self->await_id = self->script_id;
            TASK_AWAITSELF_IF(CreateTask_ClientScriptLoad(self->provider, self->await_id));
        }
        self->script = CacheProvider_ClientScriptGet(self->provider, self->script_id);
        if( !self->script )
        {
            fprintf(stderr, "Task_CS2Run: failed to resolve script %d\n", self->script_id);
            PT_EXIT(&self->pt);
        }
    }

    if( !self->started )
    {
        CS2VM2_Init(&self->vm);
        CS2VM2_BindHost(&self->vm, self->host, RS_CS2Host_Exec);
        thread = CS2VM2_ThreadMain(&self->vm);
        CS2VM2_ThreadSetCanvas(
            thread,
            self->host->viewport_w > 0 ? self->host->viewport_w : 765,
            self->host->viewport_h > 0 ? self->host->viewport_h : 503);
        CS2VM2_ThreadStart(thread, self->script);

        for( j = 0; j < self->int_arg_count; j++ )
            task_cs2_set_int_local(
                thread, j, self->int_args[j], self->active_component_id);

        CS2VM2_SetActiveAndDotComponentId(thread, self->active_component_id);
        if( self->dot_component_id != self->active_component_id )
            thread->dot_component_id = self->dot_component_id;

        self->started = 1;
    }

    for( ;; )
    {
        thread = CS2VM2_ThreadMain(&self->vm);
        status = CS2VM2_ThreadRun(thread, &err);
        if( status == CS2VM2_THREAD_DONE || status == 0 )
            break;
        if( status == CS2VM2_THREAD_ERROR )
        {
            fprintf(
                stderr,
                "Task_CS2Run: script %d failed at opcode %d pc %d "
                "(invoked as script %d for component 0x%x)\n",
                thread->last_error_script_id,
                thread->last_error_opcode,
                thread->last_error_pc,
                self->script_id,
                (unsigned)self->active_component_id);
            CS2VM2_ResetRuntime(thread);
            break;
        }
        if( status != CS2VM2_THREAD_YIELDED )
            break;
        if( !self->host->has_pending )
        {
            fprintf(stderr, "Task_CS2Run: yield without pending host request\n");
            CS2VM2_ResetRuntime(thread);
            break;
        }

        self->pending = self->host->pending;
        self->host->has_pending = false;

        /* if/else only — protothreads cannot nest switch with PT_YIELD. */
        if( self->pending.kind == CS2VM_HOST_REQUEST_PUSHSCRIPT )
        {
            self->await_id = self->pending.u.push_script.script_id;
            TASK_AWAITSELF_IF(CreateTask_ClientScriptLoad(self->provider, self->await_id));
        }
        else if( self->pending.kind == CS2VM_HOST_REQUEST_ENUM_LOOKUP )
        {
            self->await_id = self->pending.u.enum_lookup.enum_id;
            TASK_AWAITSELF_IF(CreateTask_EnumLoad(self->provider, self->await_id));
        }
        else if( self->pending.kind == CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT )
        {
            self->await_id = self->pending.u.enum_get_output_count.enum_id;
            TASK_AWAITSELF_IF(CreateTask_EnumLoad(self->provider, self->await_id));
        }
        else if( self->pending.kind == CS2VM_HOST_REQUEST_STRUCT_PARAM )
        {
            self->await_id = self->pending.u.struct_param.struct_id;
            TASK_AWAITSELF_IF(CreateTask_StructLoad(self->provider, self->await_id));
        }
        else if( self->pending.kind == CS2VM_HOST_REQUEST_OC_PARAM )
        {
            self->await_id = self->pending.u.oc_param.item_id;
            TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->await_id));
        }
        else if( self->pending.kind == CS2VM_HOST_REQUEST_OC_NAME )
        {
            self->await_id = self->pending.u.oc_name.item_id;
            TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->await_id));
        }
        else if( self->pending.kind == CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER )
        {
            self->await_id = self->pending.u.oc_unplaceholder.item_id;
            TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->await_id));
        }
        else if( self->pending.kind == CS2VM_HOST_REQUEST_OC_INT_PARAM )
        {
            self->await_id = self->pending.u.oc_int_param.item_id;
            TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->await_id));
        }
        else if(
            self->pending.kind == CS2VM_HOST_REQUEST_CC_CREATE ||
            self->pending.kind == CS2VM_HOST_REQUEST_CC_FIND ||
            self->pending.kind == CS2VM_HOST_REQUEST_IF_FIND ||
            self->pending.kind == CS2VM_HOST_REQUEST_CC_CHILDREN_FIND ||
            self->pending.kind == CS2VM_HOST_REQUEST_IF_CHILDREN_FIND )
        {
            self->await_id = task_cs2_group_id_from_request(&self->pending);
            if( self->await_id > 0 )
            {
                TASK_AWAITSELF_IF(CreateTask_ComponentPackLoad(self->provider, self->await_id));
                /* Bake loaded pack into the tree so the host retry can find nodes.
                 * Identity resolvers: cache sprite/font ids stored as scene_id until a
                 * scene layer exists. */
                if( self->host->tree &&
                    CacheProvider_ComponentPackHas(self->provider, self->await_id) )
                {
                    struct ToriRS_ComponentPack* pack =
                        CacheProvider_ComponentPackGet(self->provider, self->await_id);
                    if( pack )
                    {
                        (void)UITree_BuildFromComponentPack(
                            self->host->tree, pack, NULL, NULL, NULL);
                    }
                }
            }
        }
        else if( self->pending.kind == CS2VM_HOST_REQUEST_WIDGET_SET_MODEL )
        {
            self->await_id = self->pending.u.widget_set_model.model_id;
            if( self->await_id >= 0 )
                TASK_AWAITSELF_IF(CreateTask_ModelLoad(self->provider, self->await_id));
        }
        else if( self->pending.kind == CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND )
        {
            if( self->pending.u.widget_set_model_kind.model_kind == CS2VM_MODEL_KIND_PLAIN )
            {
                self->await_id = self->pending.u.widget_set_model_kind.model_id;
                if( self->await_id >= 0 )
                    TASK_AWAITSELF_IF(CreateTask_ModelLoad(self->provider, self->await_id));
            }
            else if(
                self->pending.u.widget_set_model_kind.model_kind == CS2VM_MODEL_KIND_NPC_HEAD )
            {
                self->await_id = self->pending.u.widget_set_model_kind.model_id;
                if( self->await_id >= 0 )
                    TASK_AWAITSELF_IF(CreateTask_NpcLoad(self->provider, self->await_id));
            }
            /* Player model kinds: soft-skip for now. */
        }
        else if(
            self->pending.kind == CS2VM_HOST_REQUEST_CC_SETOBJECT ||
            self->pending.kind == CS2VM_HOST_REQUEST_IF_SETOBJECT )
        {
            if( self->pending.kind == CS2VM_HOST_REQUEST_IF_SETOBJECT )
            {
                self->yield_obj_id = self->pending.u.if_set_object.obj_id;
                self->yield_obj_count = self->pending.u.if_set_object.count;
            }
            else
            {
                self->yield_obj_id = self->pending.u.cc_set_object.obj_id;
                self->yield_obj_count = self->pending.u.cc_set_object.count;
            }

            if( self->yield_obj_id > 0 )
            {
                self->await_id = self->yield_obj_id;
                TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->await_id));
                self->await_id = task_cs2_obj_inventory_model_id(
                    self->provider, self->yield_obj_id, self->yield_obj_count);
                if( self->await_id > 0 )
                    TASK_AWAITSELF_IF(CreateTask_ModelLoad(self->provider, self->await_id));
            }
        }
        else if(
            self->pending.kind == CS2VM_HOST_REQUEST_CC_SETGRAPHIC ||
            self->pending.kind == CS2VM_HOST_REQUEST_IF_SETGRAPHIC )
        {
            self->await_id = self->pending.u.cc_set_graphic.graphic_id;
            /* Reject obviously invalid ids (e.g. widget-local placeholders). */
            if( self->await_id >= 0 && self->await_id < 1000000 )
                TASK_AWAITSELF_IF(CreateTask_SpriteLoad(self->provider, self->await_id));
        }
        else if(
            self->pending.kind == CS2VM_HOST_REQUEST_PARAHEIGHT ||
            self->pending.kind == CS2VM_HOST_REQUEST_PARAWIDTH ||
            self->pending.kind == CS2VM_HOST_REQUEST_CC_SETTEXTFONT )
        {
            if( self->pending.kind == CS2VM_HOST_REQUEST_CC_SETTEXTFONT )
                self->await_id = self->pending.u.cc_set_text_font.font_id;
            else
                self->await_id = self->pending.u.para_height.font_id;
            if( self->await_id >= 0 )
                TASK_AWAITSELF_IF(CreateTask_FontLoad(self->provider, self->await_id));
        }
        else
        {
            fprintf(
                stderr,
                "Task_CS2Run: unhandled yield kind %d (script %d)\n",
                (int)self->pending.kind,
                self->script_id);
            CS2VM2_ResetRuntime(CS2VM2_ThreadMain(&self->vm));
            PT_EXIT(&self->pt);
        }
        /* Re-enter ThreadRun; CS2VM2 restores the opcode site so the host succeeds. */
    }

    PT_END(&self->pt);
    return 0;
}

static void
Task_CS2Run_Free(struct ToriRS_Task* task)
{
    struct Task_CS2Run* self = (struct Task_CS2Run*)task;
    assert(self);
    CS2VM2_Free(&self->vm);
    free(self);
}

static struct ToriRS_TaskVTable Task_CS2Run_VTable = {
    .run = Task_CS2Run_Run,
    .free = Task_CS2Run_Free,
};

static struct ToriRS_Task*
task_cs2_run_new(
    struct RS_CS2Host* host,
    int script_id,
    struct CS2VM2_Script* script,
    int active_component_id,
    int dot_component_id,
    int const* int_args,
    int int_arg_count)
{
    struct Task_CS2Run* self;

    assert(host);
    assert(host->provider);

    self = calloc(1, sizeof(*self));
    assert(self);

    self->task.vtable = &Task_CS2Run_VTable;
    strcpy(self->task.name, "CS2Run");
    self->host = host;
    self->provider = host->provider;
    self->script_id = script_id;
    self->script = script;
    self->active_component_id = active_component_id;
    self->dot_component_id = dot_component_id >= 0 ? dot_component_id : active_component_id;

    if( int_arg_count > TASK_CS2_RUN_INT_ARGS_MAX )
        int_arg_count = TASK_CS2_RUN_INT_ARGS_MAX;
    if( int_arg_count < 0 )
        int_arg_count = 0;
    self->int_arg_count = int_arg_count;
    if( int_arg_count > 0 && int_args )
        memcpy(self->int_args, int_args, (size_t)int_arg_count * sizeof(int));

    PT_INIT(&self->pt);
    return &self->task;
}

struct ToriRS_Task*
CreateTask_CS2Run(
    struct RS_CS2Host* host,
    int script_id,
    int active_component_id,
    int dot_component_id,
    int const* int_args,
    int int_arg_count)
{
    return task_cs2_run_new(
        host, script_id, NULL, active_component_id, dot_component_id, int_args, int_arg_count);
}

struct ToriRS_Task*
CreateTask_CS2RunScript(
    struct RS_CS2Host* host,
    struct CS2VM2_Script* script,
    int active_component_id,
    int dot_component_id,
    int const* int_args,
    int int_arg_count)
{
    assert(script);
    return task_cs2_run_new(
        host,
        script->script_id,
        script,
        active_component_id,
        dot_component_id,
        int_args,
        int_arg_count);
}

/* =========================================================================
 * Inv-transmit dispatch
 * ========================================================================= */

struct Task_CS2InvTransmitDispatch
{
    struct ToriRS_Task task;
    struct pt pt;

    struct RS_CS2Host* host;
    int container_id;
    int hook_index;
};

static int
hook_matches_container(
    struct RS_CS2InvTransmitHook const* hook,
    int container_id)
{
    int i;
    assert(hook);
    if( container_id < 0 )
        return 1;
    if( hook->trigger_count <= 0 )
        return 1;
    for( i = 0; i < hook->trigger_count; i++ )
    {
        if( hook->trigger_ids[i] == container_id )
            return 1;
    }
    return 0;
}

static int
Task_CS2InvTransmitDispatch_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_CS2InvTransmitDispatch* self = (struct Task_CS2InvTransmitDispatch*)task;
    struct RS_CS2InvTransmitHook const* hook;

    PT_BEGIN(&self->pt);

    assert(self->host);

    for( self->hook_index = 0; self->hook_index < self->host->inv_transmit_hook_count;
         self->hook_index++ )
    {
        hook = &self->host->inv_transmit_hooks[self->hook_index];
        if( !hook_matches_container(hook, self->container_id) )
            continue;
        if( hook->script_id <= 0 )
            continue;

        TASK_AWAITSELF(CreateTask_CS2Run(
            self->host,
            hook->script_id,
            hook->component_id,
            hook->component_id,
            hook->int_args,
            hook->int_arg_count));
    }

    PT_END(&self->pt);
    return 0;
}

static void
Task_CS2InvTransmitDispatch_Free(struct ToriRS_Task* task)
{
    struct Task_CS2InvTransmitDispatch* self = (struct Task_CS2InvTransmitDispatch*)task;
    assert(self);
    free(self);
}

static struct ToriRS_TaskVTable Task_CS2InvTransmitDispatch_VTable = {
    .run = Task_CS2InvTransmitDispatch_Run,
    .free = Task_CS2InvTransmitDispatch_Free,
};

struct ToriRS_Task*
CreateTask_CS2InvTransmitDispatch(
    struct RS_CS2Host* host,
    int container_id)
{
    struct Task_CS2InvTransmitDispatch* self;

    assert(host);

    self = calloc(1, sizeof(*self));
    assert(self);
    self->task.vtable = &Task_CS2InvTransmitDispatch_VTable;
    strcpy(self->task.name, "CS2InvTransmitDispatch");
    self->host = host;
    self->container_id = container_id;
    PT_INIT(&self->pt);
    return &self->task;
}
