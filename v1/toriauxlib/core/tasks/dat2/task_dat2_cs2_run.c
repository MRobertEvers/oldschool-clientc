#include "toriauxlib/core/tasks/dat2/task_dat2_cs2_run.h"

#include "buildcache/dat2_buildcache.h"
#include "games/runescape.h"
#include "games/runescape_cs2_host.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/core/tasks/toriauxlib_tasks.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "ui/uitree_build.h"
#include "vm/cs2vmx.h"
#include "vm/cs2vmx_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct Task_Dat2CS2Run
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct GameRunescape* game;
    struct CS2VMX* vm;
    struct GameRunescapeCS2Host* host;
    int script_id;
    int component_id;
    int int_args[64];
    int int_arg_count;
    char* str_args[8];
    int str_arg_count;
    struct CS2VM_HostRequest pending;
    int await_id;
    int yield_obj_id;
    int yield_obj_count;
};

static struct ToriAuxLibCache*
task_cs2_cache(struct Task_Dat2CS2Run* task)
{
    assert(task);
    assert(task->game);
    assert(task->game->td);
    return ToriAuxLibTD_C(task->game->td);
}

static void
task_cs2_set_int_local(
    struct Task_Dat2CS2Run* task,
    int local_idx,
    int argi)
{
    switch( argi )
    {
    case CS2VM_SCRIPT_ARG_WIDGET_ID:
        CS2VMX_SetIntCurrentFrameLocal(task->vm, local_idx, task->component_id);
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
        /* Leave unset / zero for now — no live input binding in this task. */
        break;
    default:
        CS2VMX_SetIntCurrentFrameLocal(task->vm, local_idx, argi);
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
    struct Dat2BuildCache* bc,
    int obj_id,
    int count)
{
    struct RSCacheDat2A_ConfigObject* obj;

    if( !bc || obj_id < 0 )
        return -1;

    obj = dat2_buildcache_object_get(bc, obj_id);
    if( obj && count > 1 )
    {
        int countobj_id = -1;
        int i;
        for( i = 0; i < 10; i++ )
        {
            if( count >= obj->count_co[i] && obj->count_co[i] != 0 )
                countobj_id = obj->count_obj[i];
        }
        if( countobj_id >= 0 )
            obj = dat2_buildcache_object_get(bc, countobj_id);
    }

    if( !obj || obj->inventory_model_id <= 0 )
        return -1;
    return obj->inventory_model_id;
}

static int
Task_Dat2CS2Run_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2CS2Run* task = LibToriRS_container_of(base, struct Task_Dat2CS2Run, base);
    struct ToriAuxLibCache* cache = task_cs2_cache(task);
    struct ToriAuxLibCore* core = task->game ? task->game->core : NULL;
    struct Dat2BuildCache* bc = cache ? dat2(cache) : NULL;
    struct ToriAuxLibCore_ClientScript* cs = NULL;
    struct CS2VMX_Frame* frame = NULL;
    int res = 0;
    int j;

    PT_BEGIN(&task->pt);

    assert(task->game);
    assert(task->vm);
    assert(task->host);
    assert(cache);
    assert(core);
    if( task->script_id <= 0 )
        PT_EXIT(&task->pt);

    if( !ToriAuxLibCore_ClientScriptGet(core, task->script_id) )
    {
        task->await_id = task->script_id;
        TASK_AWAITEX(
            &task->pt, ctx, Task_Dat2ClientScriptLoad_New(cache, &task->await_id, 1));
        if( !ToriAuxLibCore_ClientScriptGet(core, task->script_id) )
        {
            fprintf(stderr, "Task_Dat2CS2Run: failed to resolve script %d\n", task->script_id);
            PT_EXIT(&task->pt);
        }
    }

    CS2VMX_ResetRuntime(task->vm);
    cs = ToriAuxLibCore_ClientScriptGet(core, task->script_id);
    if( !cs )
        PT_EXIT(&task->pt);
    CS2VMX_PushCallScript(task->vm, &cs->script);

    frame = CS2VM_FRAME(task->vm);
    for( j = 0; j < task->str_arg_count; j++ )
    {
        char const* src = task->str_args[j] ? task->str_args[j] : "";
        frame->str_locals[j] = strdup(src);
        if( !frame->str_locals[j] )
            PT_EXIT(&task->pt);
    }
    for( j = 0; j < task->int_arg_count; j++ )
        task_cs2_set_int_local(task, j, task->int_args[j]);

    CS2VMX_SetActiveAndDotComponentId(task->vm, task->component_id);

    for( ;; )
    {
        res = CS2VMX_RunScript(task->vm);
        if( res == CS2VM_EXECNO_DONE || res == 0 )
            break;
        if( res == CS2VM_EXECNO_ERROR )
        {
            fprintf(
                stderr,
                "Task_Dat2CS2Run: script %d failed at opcode %d pc %d "
                "(invoked as script %d for component 0x%x)\n",
                task->vm->last_error_script_id,
                task->vm->last_error_opcode,
                task->vm->last_error_pc,
                task->script_id,
                (unsigned)task->component_id);
            CS2VMX_ResetRuntime(task->vm);
            break;
        }
        if( res != CS2VM_EXECNO_YIELD )
            break;
        if( !task->host->has_pending )
        {
            fprintf(stderr, "Task_Dat2CS2Run: yield without pending host request\n");
            CS2VMX_ResetRuntime(task->vm);
            break;
        }

        task->pending = task->host->pending;
        task->host->has_pending = false;

        /* if/else only — protothreads cannot nest switch with PT_YIELD. */
        if( task->pending.kind == CS2VM_HOST_REQUEST_PUSHSCRIPT )
        {
            task->await_id = task->pending.u.push_script.script_id;
            if( !ToriAuxLibCore_ClientScriptGet(core, task->await_id) )
            {
                TASK_AWAITEX(
                    &task->pt,
                    ctx,
                    Task_Dat2ClientScriptLoad_New(cache, &task->await_id, 1));
            }
        }
        else if( task->pending.kind == CS2VM_HOST_REQUEST_ENUM_LOOKUP )
        {
            task->await_id = task->pending.u.enum_lookup.enum_id;
            TASK_AWAITEX(
                &task->pt,
                ctx,
                Task_Dat2ConfigEntryLoad_New(
                    cache, RSCacheDat2A_ConfigKind_Enum, &task->await_id, 1));
        }
        else if( task->pending.kind == CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT )
        {
            task->await_id = task->pending.u.enum_get_output_count.enum_id;
            TASK_AWAITEX(
                &task->pt,
                ctx,
                Task_Dat2ConfigEntryLoad_New(
                    cache, RSCacheDat2A_ConfigKind_Enum, &task->await_id, 1));
        }
        else if( task->pending.kind == CS2VM_HOST_REQUEST_STRUCT_PARAM )
        {
            task->await_id = task->pending.u.struct_param.struct_id;
            TASK_AWAITEX(
                &task->pt,
                ctx,
                Task_Dat2ConfigEntryLoad_New(
                    cache, RSCacheDat2A_ConfigKind_Struct, &task->await_id, 1));
            bc = dat2(cache);
            if( bc && !dat2_buildcache_param_get(bc, task->pending.u.struct_param.param_id) )
            {
                task->await_id = task->pending.u.struct_param.param_id;
                TASK_AWAITEX(
                    &task->pt,
                    ctx,
                    Task_Dat2ConfigEntryLoad_New(
                        cache, RSCacheDat2A_ConfigKind_Params, &task->await_id, 1));
            }
        }
        else if( task->pending.kind == CS2VM_HOST_REQUEST_OC_PARAM )
        {
            task->await_id = task->pending.u.oc_param.item_id;
            TASK_AWAITEX(
                &task->pt,
                ctx,
                Task_Dat2ConfigEntryLoad_New(
                    cache, RSCacheDat2A_ConfigKind_Object, &task->await_id, 1));
            bc = dat2(cache);
            if( bc && !dat2_buildcache_param_get(bc, task->pending.u.oc_param.param_id) )
            {
                task->await_id = task->pending.u.oc_param.param_id;
                TASK_AWAITEX(
                    &task->pt,
                    ctx,
                    Task_Dat2ConfigEntryLoad_New(
                        cache, RSCacheDat2A_ConfigKind_Params, &task->await_id, 1));
            }
        }
        else if( task->pending.kind == CS2VM_HOST_REQUEST_OC_NAME )
        {
            task->await_id = task->pending.u.oc_name.item_id;
            TASK_AWAITEX(
                &task->pt,
                ctx,
                Task_Dat2ConfigEntryLoad_New(
                    cache, RSCacheDat2A_ConfigKind_Object, &task->await_id, 1));
        }
        else if( task->pending.kind == CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER )
        {
            task->await_id = task->pending.u.oc_unplaceholder.item_id;
            TASK_AWAITEX(
                &task->pt,
                ctx,
                Task_Dat2ConfigEntryLoad_New(
                    cache, RSCacheDat2A_ConfigKind_Object, &task->await_id, 1));
        }
        else if( task->pending.kind == CS2VM_HOST_REQUEST_OC_INT_PARAM )
        {
            task->await_id = task->pending.u.oc_int_param.item_id;
            TASK_AWAITEX(
                &task->pt,
                ctx,
                Task_Dat2ConfigEntryLoad_New(
                    cache, RSCacheDat2A_ConfigKind_Object, &task->await_id, 1));
        }
        else if(
            task->pending.kind == CS2VM_HOST_REQUEST_CC_CREATE ||
            task->pending.kind == CS2VM_HOST_REQUEST_CC_FIND ||
            task->pending.kind == CS2VM_HOST_REQUEST_IF_FIND ||
            task->pending.kind == CS2VM_HOST_REQUEST_CC_CHILDREN_FIND ||
            task->pending.kind == CS2VM_HOST_REQUEST_IF_CHILDREN_FIND )
        {
            task->await_id = task_cs2_group_id_from_request(&task->pending);
            if( task->await_id > 0 )
            {
                TASK_AWAITEX(
                    &task->pt,
                    ctx,
                    Task_Dat2ComponentLoad_New(
                        cache, task->game->scene, NULL, task->await_id, NULL));
                if( task->await_id > 0 && task->game->ui_tree && task->game->core )
                {
                    (void)uitree_build_interface_group(
                        task->game->ui_tree, task->game->core, task->await_id, NULL, NULL);
                }
            }
        }
        else if( task->pending.kind == CS2VM_HOST_REQUEST_WIDGET_SET_MODEL )
        {
            task->await_id = task->pending.u.widget_set_model.model_id;
            if( task->await_id >= 0 && !ToriAuxLibCore_ModelHas(core, task->await_id) )
            {
                TASK_AWAITEX(
                    &task->pt, ctx, Task_Dat2ModelLoad_New(cache, task->await_id));
            }
        }
        else if( task->pending.kind == CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND )
        {
            if( task->pending.u.widget_set_model_kind.model_kind == CS2VM_MODEL_KIND_PLAIN )
            {
                task->await_id = task->pending.u.widget_set_model_kind.model_id;
                if( task->await_id >= 0 && !ToriAuxLibCore_ModelHas(core, task->await_id) )
                {
                    TASK_AWAITEX(
                        &task->pt, ctx, Task_Dat2ModelLoad_New(cache, task->await_id));
                }
            }
            else if(
                task->pending.u.widget_set_model_kind.model_kind == CS2VM_MODEL_KIND_NPC_HEAD )
            {
                task->await_id = task->pending.u.widget_set_model_kind.model_id;
                if( task->await_id >= 0 )
                {
                    TASK_AWAITEX(
                        &task->pt, ctx, Task_Dat2NpcAdd_New(cache, task->await_id));
                }
            }
            /* Player model kinds: soft-skip for now. */
        }
        else if(
            task->pending.kind == CS2VM_HOST_REQUEST_CC_SETOBJECT ||
            task->pending.kind == CS2VM_HOST_REQUEST_IF_SETOBJECT )
        {
            if( task->pending.kind == CS2VM_HOST_REQUEST_IF_SETOBJECT )
            {
                task->yield_obj_id = task->pending.u.if_set_object.obj_id;
                task->yield_obj_count = task->pending.u.if_set_object.count;
            }
            else
            {
                task->yield_obj_id = task->pending.u.cc_set_object.obj_id;
                task->yield_obj_count = task->pending.u.cc_set_object.count;
            }

            task->await_id = task->yield_obj_id;
            if( task->yield_obj_id > 0 )
            {
                bc = dat2(cache);
                if( !bc || !dat2_buildcache_object_get(bc, task->yield_obj_id) )
                {
                    TASK_AWAITEX(
                        &task->pt,
                        ctx,
                        Task_Dat2ConfigEntryLoad_New(
                            cache, RSCacheDat2A_ConfigKind_Object, &task->await_id, 1));
                }
                bc = dat2(cache);
                task->await_id = task_cs2_obj_inventory_model_id(
                    bc, task->yield_obj_id, task->yield_obj_count);
                if( task->await_id > 0 &&
                    ( !bc || !dat2_buildcache_model_get(bc, task->await_id) ) &&
                    !ToriAuxLibCore_ModelHas(core, task->await_id) )
                {
                    TASK_AWAITEX(
                        &task->pt, ctx, Task_Dat2ModelLoad_New(cache, task->await_id));
                }
            }
        }
        else if(
            task->pending.kind == CS2VM_HOST_REQUEST_CC_SETGRAPHIC ||
            task->pending.kind == CS2VM_HOST_REQUEST_IF_SETGRAPHIC ||
            task->pending.kind == CS2VM_HOST_REQUEST_PARAHEIGHT ||
            task->pending.kind == CS2VM_HOST_REQUEST_PARAWIDTH ||
            task->pending.kind == CS2VM_HOST_REQUEST_CC_SETTEXTFONT )
        {
            /* Soft-skip: sprites/fonts are usually preloaded. Retrying without load
             * would infinite-yield, so stop the script run. */
            fprintf(
                stderr,
                "Task_Dat2CS2Run: soft-skip yield kind %d (sprite/font); ending script %d\n",
                (int)task->pending.kind,
                task->script_id);
            CS2VMX_ResetRuntime(task->vm);
            PT_EXIT(&task->pt);
        }
        else
        {
            fprintf(
                stderr,
                "Task_Dat2CS2Run: unhandled yield kind %d (script %d)\n",
                (int)task->pending.kind,
                task->script_id);
            CS2VMX_ResetRuntime(task->vm);
            PT_EXIT(&task->pt);
        }
    }

    PT_END(&task->pt);
}

static void
Task_Dat2CS2Run_Free(struct LibToriRS_Task* base)
{
    struct Task_Dat2CS2Run* task = LibToriRS_container_of(base, struct Task_Dat2CS2Run, base);
    int i;

    if( !task )
        return;
    for( i = 0; i < task->str_arg_count; i++ )
        free(task->str_args[i]);
    free(task);
}

static struct LibToriRS_TaskVTable g_task_dat2_cs2_run_vtable = {
    .run_fn = Task_Dat2CS2Run_Run,
    .free_fn = Task_Dat2CS2Run_Free,
};

struct LibToriRS_Task*
Task_Dat2CS2Run_New(
    struct GameRunescape* game,
    struct CS2VMX* vm,
    struct GameRunescapeCS2Host* host,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count,
    char const* const* str_args,
    int str_arg_count)
{
    struct Task_Dat2CS2Run* task;
    int i;

    task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;

    task->base.vtable = &g_task_dat2_cs2_run_vtable;
    task->game = game;
    task->vm = vm;
    task->host = host;
    task->script_id = script_id;
    task->component_id = component_id;

    if( int_arg_count > 64 )
        int_arg_count = 64;
    if( int_arg_count < 0 )
        int_arg_count = 0;
    task->int_arg_count = int_arg_count;
    if( int_arg_count > 0 && int_args )
        memcpy(task->int_args, int_args, (size_t)int_arg_count * sizeof(int));

    if( str_arg_count > 8 )
        str_arg_count = 8;
    if( str_arg_count < 0 )
        str_arg_count = 0;
    task->str_arg_count = str_arg_count;
    for( i = 0; i < str_arg_count; i++ )
    {
        char const* src = str_args && str_args[i] ? str_args[i] : "";
        task->str_args[i] = strdup(src);
        if( !task->str_args[i] )
        {
            while( --i >= 0 )
                free(task->str_args[i]);
            free(task);
            return NULL;
        }
    }

    PT_INIT(&task->pt);
    return &task->base;
}
