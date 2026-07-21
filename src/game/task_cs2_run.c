#include "game/task_cs2_run.h"

#include "cs2vm2/cs2_opcode_meta.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_host.h"
#include "cs2vm2/cs2vm2_script.h"
#include "engine/cache_provider.h"
#include "engine/task_obj_model_load.h"
#include "engine/torirs_types.h"
#include "engine/uitree_builder/task_pack_assets_load.h"
#include "engine/uitree_from_component.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_cs2_host.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"
#include <3rd/minipt.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UITREE_CLICK_DEBUG
#define UITREE_CLICK_DEBUG 0
#endif

#define TASK_CS2_RUN_INT_ARGS_MAX 64
#define TASK_CS2_RUN_STR_ARGS_MAX 4
#define TASK_CS2_RUN_STR_ARG_LEN 80

enum TaskCS2YieldPlan
{
    TASK_CS2_YIELD_NONE = 0,
    TASK_CS2_YIELD_SCRIPT,
    TASK_CS2_YIELD_ENUM,
    TASK_CS2_YIELD_STRUCT,
    TASK_CS2_YIELD_OBJ,
    TASK_CS2_YIELD_COMPONENT,
    TASK_CS2_YIELD_MODEL,
    TASK_CS2_YIELD_NPC,
    TASK_CS2_YIELD_SETOBJECT,
    TASK_CS2_YIELD_SPRITE,
    TASK_CS2_YIELD_FONT,
    TASK_CS2_YIELD_ABORT,
};

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
    /* Bit i set = arg position i is a string; strings fill str_args[] in
     * position order. Positions past the pool cap degrade to "". */
    uint64_t str_mask;
    int str_arg_count;
    char str_args[TASK_CS2_RUN_STR_ARGS_MAX][TASK_CS2_RUN_STR_ARG_LEN];

    struct CS2VM_HostRequest pending;
    enum TaskCS2YieldPlan yield_plan;
    int await_id;
    /* Second config a request needs alongside await_id (the ParamType behind a
     * struct/obj param lookup), or -1. One yield loads both. */
    int await_id2;
    int yield_obj_id;
    int yield_obj_count;
    int started;
};

static int
task_cs2_resolve_sprite(
    void* ud,
    int graphic_id)
{
    struct UITreeSceneBridge* bridge = (struct UITreeSceneBridge*)ud;
    assert(bridge);
    if( graphic_id <= 0 )
        return -1;
    return UITreeSceneBridge_EnsureSprite(bridge, graphic_id);
}

static int
task_cs2_resolve_font(
    void* ud,
    int font_id)
{
    struct UITreeSceneBridge* bridge = (struct UITreeSceneBridge*)ud;
    assert(bridge);
    if( font_id < 0 )
        return -1;
    return UITreeSceneBridge_EnsureFont(bridge, font_id);
}

static void
task_cs2_set_int_local(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int local_idx,
    int argi,
    int active_component_id)
{
    assert(host);
    switch( argi )
    {
    case CS2VM_SCRIPT_ARG_WIDGET_ID:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, active_component_id);
        break;
    case CS2VM_SCRIPT_ARG_OP_INDEX:
        /* Primary left-click op index for on_op handlers. */
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, 1);
        break;
    case CS2VM_SCRIPT_ARG_MOUSE_X:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, host->event_mouse_x);
        break;
    case CS2VM_SCRIPT_ARG_MOUSE_Y:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, host->event_mouse_y);
        break;
    case CS2VM_SCRIPT_ARG_WIDGET_CHILD_INDEX:
    {
        int32_t idx = UITree_FindByComponentId(host->tree, active_component_id);
        int child = -1;
        if( idx >= 0 && host->tree->components[idx].dynamic )
            child = host->tree->components[idx].dynamic_child_index;
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, child);
        break;
    }
    case CS2VM_SCRIPT_ARG_DRAG_TARGET_ID:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, host->event_drag_target_id);
        break;
    case CS2VM_SCRIPT_ARG_DRAG_TARGET_CHILD_INDEX:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, host->event_drag_target_child_index);
        break;
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
    case CS2VM_HOST_REQUEST_CC_COPY:
        return (request->u.cc_copy.parent_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_CC_FIND:
        return (request->u.cc_find.parent_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_IF_FIND:
        return (request->u.if_find.component_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND:
        return (request->u.cc_children_find.parent_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
        return (request->u.if_children_find.uid >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_WIDGET_SET_INT:
        return (request->u.widget_set_int.component_id >> 16) & 0xffff;
    default:
        return -1;
    }
}

/** Parent component id that triggered a missing-group yield, or -1. */
static int
task_cs2_mount_parent_id_from_request(struct CS2VM_HostRequest const* request)
{
    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_CC_CREATE:
        return request->u.cc_create.parent_id;
    case CS2VM_HOST_REQUEST_CC_COPY:
        return request->u.cc_copy.parent_id;
    case CS2VM_HOST_REQUEST_CC_FIND:
        return request->u.cc_find.parent_id;
    case CS2VM_HOST_REQUEST_IF_FIND:
        return request->u.if_find.component_id;
    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND:
        return request->u.cc_children_find.parent_id;
    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
        return request->u.if_children_find.uid;
    case CS2VM_HOST_REQUEST_WIDGET_SET_INT:
        return request->u.widget_set_int.component_id;
    default:
        return -1;
    }
}

static void
task_cs2_bake_pack(struct Task_CS2Run* self)
{
    struct ToriRS_ComponentPack* pack;
    struct UITree* tree;
    int (*resolve_sprite)(void*, int) = NULL;
    int (*resolve_font)(void*, int) = NULL;
    void* resolve_ud = NULL;
    int pack_root_id;
    int32_t pack_root_idx;
    int mount_parent_id;
    int mount_group;

    assert(self);
    assert(self->host);
    assert(self->host->tree);
    assert(self->await_id > 0);

    tree = self->host->tree;

    if( !CacheProvider_ComponentPackHas(self->provider, self->await_id) )
    {
        fprintf(
            stderr,
            "Task_CS2Run: component pack %d missing after load (script %d)\n",
            self->await_id,
            self->script_id);
        return;
    }

    pack = CacheProvider_ComponentPackGet(self->provider, self->await_id);
    assert(pack);

    if( self->host->bridge )
    {
        resolve_ud = self->host->bridge;
        resolve_sprite = task_cs2_resolve_sprite;
        resolve_font = task_cs2_resolve_font;
    }
    (void)UITree_BuildFromComponentPack(
        tree, pack, resolve_sprite, resolve_font, resolve_ud);

    /* If the yield parent lives on another already-baked group, mount this pack
     * root under it (sub-interface style). Same-group parents (e.g. CC_CREATE
     * under 728:6 while loading 728) are already linked inside the pack. */
    pack_root_id = (self->await_id << 16) | 0;
    pack_root_idx = UITree_FindByComponentId(tree, pack_root_id);
    mount_parent_id = task_cs2_mount_parent_id_from_request(&self->pending);
    mount_group = (mount_parent_id >> 16) & 0xffff;
    if( pack_root_idx >= 0 && mount_group > 0 && mount_group != self->await_id )
    {
        int32_t mount_idx = UITree_FindByComponentId(tree, mount_parent_id);
        if( mount_idx >= 0 )
            UITree_Reparent(tree, pack_root_idx, mount_idx);
    }

    UITree_LayoutResolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    if( self->host->bridge )
    {
        uint32_t mi;
        for( mi = 0; mi < tree->component_count; mi++ )
        {
            struct UITreeComponent* c = &tree->components[mi];
            if( c->type != UIELEM_RS_MODEL )
                continue;
            if( c->u.rs_model.gamecache_model_id >= 0 )
            {
                int sid = UITreeSceneBridge_EnsureModel(
                    self->host->bridge, c->u.rs_model.gamecache_model_id);
                if( sid >= 0 )
                    c->u.rs_model.gamecache_model_id = sid;
            }
        }
    }
}

static void
task_cs2_plan_pushscript(struct Task_CS2Run* self)
{
    self->await_id = self->pending.u.push_script.script_id;
    assert(self->await_id > 0);
    self->yield_plan = TASK_CS2_YIELD_SCRIPT;
}

static void
task_cs2_plan_enum(struct Task_CS2Run* self)
{
    if( self->pending.kind == CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT )
        self->await_id = self->pending.u.enum_get_output_count.enum_id;
    else
        self->await_id = self->pending.u.enum_lookup.enum_id;
    assert(self->await_id >= 0);
    self->yield_plan = TASK_CS2_YIELD_ENUM;
}

/* A struct param answer needs the struct *and* the ParamType behind it; either
 * may be the missing one, so the single yield loads both. struct -1 ("no
 * struct", what a missed enum lookup pushes) is not loadable — the ParamType
 * default answers it. */
static void
task_cs2_plan_struct(struct Task_CS2Run* self)
{
    self->await_id = self->pending.u.struct_param.struct_id;
    self->await_id2 = self->pending.u.struct_param.param_id;
    assert(self->await_id >= 0 || self->await_id2 >= 0);
    self->yield_plan = TASK_CS2_YIELD_STRUCT;
}

static void
task_cs2_plan_obj(struct Task_CS2Run* self)
{
    switch( self->pending.kind )
    {
    case CS2VM_HOST_REQUEST_OC_PARAM:
        /* Same pairing as task_cs2_plan_struct: objtype + ParamType. */
        self->await_id = self->pending.u.oc_param.item_id;
        self->await_id2 = self->pending.u.oc_param.param_id;
        break;
    case CS2VM_HOST_REQUEST_OC_NAME:
        self->await_id = self->pending.u.oc_name.item_id;
        break;
    case CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER:
        self->await_id = self->pending.u.oc_unplaceholder.item_id;
        break;
    case CS2VM_HOST_REQUEST_OC_INT_PARAM:
        self->await_id = self->pending.u.oc_int_param.item_id;
        break;
    default:
        assert(0 && "task_cs2_plan_obj: unexpected kind");
        self->yield_plan = TASK_CS2_YIELD_ABORT;
        return;
    }
    /* item -1 (empty slot) with a missing ParamType is a valid wait. */
    assert(self->await_id >= 0 || self->await_id2 >= 0);
    self->yield_plan = TASK_CS2_YIELD_OBJ;
}

static void
task_cs2_plan_component(struct Task_CS2Run* self)
{
    self->await_id = task_cs2_group_id_from_request(&self->pending);
    assert(self->await_id > 0 && "component yield must carry a valid group id");
    self->yield_plan = TASK_CS2_YIELD_COMPONENT;
}

static void
task_cs2_plan_widget_set_model(struct Task_CS2Run* self)
{
    self->await_id = self->pending.u.widget_set_model.model_id;
    if( self->await_id < 0 )
    {
        self->yield_plan = TASK_CS2_YIELD_NONE;
        return;
    }
    self->yield_plan = TASK_CS2_YIELD_MODEL;
}

static void
task_cs2_plan_widget_set_model_kind(struct Task_CS2Run* self)
{
    int model_id = self->pending.u.widget_set_model_kind.model_id;
    enum CS2VM_ModelKind kind = self->pending.u.widget_set_model_kind.model_kind;

    if( kind == CS2VM_MODEL_KIND_PLAIN )
    {
        if( model_id < 0 )
        {
            self->yield_plan = TASK_CS2_YIELD_NONE;
            return;
        }
        self->await_id = model_id;
        self->yield_plan = TASK_CS2_YIELD_MODEL;
        return;
    }
    if( kind == CS2VM_MODEL_KIND_NPC_HEAD )
    {
        if( model_id < 0 )
        {
            self->yield_plan = TASK_CS2_YIELD_NONE;
            return;
        }
        self->await_id = model_id;
        self->yield_plan = TASK_CS2_YIELD_NPC;
        return;
    }
    if( kind == CS2VM_MODEL_KIND_PLAYER_HEAD || kind == CS2VM_MODEL_KIND_PLAYER_SELF ||
        kind == CS2VM_MODEL_KIND_PLAYER_CHATHEAD )
    {
        /* No appearance compositor yet — do not abort; let the script continue
         * (e.g. IF_SETTEXT for equipment bonuses). clientCode 328 emit handles preview. */
        fprintf(
            stderr,
            "Task_CS2Run: player model kind %d no-op (script %d)\n",
            (int)kind,
            self->script_id);
        self->yield_plan = TASK_CS2_YIELD_NONE;
        return;
    }
    fprintf(
        stderr, "Task_CS2Run: unhandled model kind %d (script %d)\n", (int)kind, self->script_id);
    self->yield_plan = TASK_CS2_YIELD_ABORT;
}

static void
task_cs2_plan_setobject(struct Task_CS2Run* self)
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

    /* obj_id <= 0 clears the slot — no load. */
    if( self->yield_obj_id <= 0 )
    {
        self->yield_plan = TASK_CS2_YIELD_NONE;
        return;
    }
    self->await_id = self->yield_obj_id;
    self->yield_plan = TASK_CS2_YIELD_SETOBJECT;
}

static void
task_cs2_plan_setgraphic(struct Task_CS2Run* self)
{
    self->await_id = self->pending.u.cc_set_graphic.graphic_id;
    if( self->await_id < 0 )
    {
        self->yield_plan = TASK_CS2_YIELD_NONE;
        return;
    }
    self->yield_plan = TASK_CS2_YIELD_SPRITE;
}

static void
task_cs2_plan_font(struct Task_CS2Run* self)
{
    if( self->pending.kind == CS2VM_HOST_REQUEST_CC_SETTEXTFONT )
        self->await_id = self->pending.u.cc_set_text_font.font_id;
    else
        self->await_id = self->pending.u.para_height.font_id;
    if( self->await_id < 0 )
    {
        self->yield_plan = TASK_CS2_YIELD_NONE;
        return;
    }
    self->yield_plan = TASK_CS2_YIELD_FONT;
}

static void
task_cs2_plan_yield(struct Task_CS2Run* self)
{
    assert(self);

    self->yield_plan = TASK_CS2_YIELD_NONE;
    self->await_id = -1;
    self->await_id2 = -1;

    switch( self->pending.kind )
    {
    case CS2VM_HOST_REQUEST_PUSHSCRIPT:
        task_cs2_plan_pushscript(self);
        break;

    case CS2VM_HOST_REQUEST_ENUM_LOOKUP:
    case CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT:
        task_cs2_plan_enum(self);
        break;

    case CS2VM_HOST_REQUEST_STRUCT_PARAM:
        task_cs2_plan_struct(self);
        break;

    case CS2VM_HOST_REQUEST_OC_PARAM:
    case CS2VM_HOST_REQUEST_OC_NAME:
    case CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER:
    case CS2VM_HOST_REQUEST_OC_INT_PARAM:
        task_cs2_plan_obj(self);
        break;

    case CS2VM_HOST_REQUEST_CC_CREATE:
    case CS2VM_HOST_REQUEST_CC_FIND:
    case CS2VM_HOST_REQUEST_IF_FIND:
    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND:
    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
    /* Set-ops yield only when their target's group isn't baked yet (e.g.
     * search targeting the chatbox group); load + mount it like a find. */
    case CS2VM_HOST_REQUEST_WIDGET_SET_INT:
        task_cs2_plan_component(self);
        break;

    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL:
        task_cs2_plan_widget_set_model(self);
        break;

    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND:
        task_cs2_plan_widget_set_model_kind(self);
        break;

    case CS2VM_HOST_REQUEST_CC_SETOBJECT:
    case CS2VM_HOST_REQUEST_IF_SETOBJECT:
        task_cs2_plan_setobject(self);
        break;

    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC:
    case CS2VM_HOST_REQUEST_IF_SETGRAPHIC:
        task_cs2_plan_setgraphic(self);
        break;

    case CS2VM_HOST_REQUEST_PARAHEIGHT:
    case CS2VM_HOST_REQUEST_PARAWIDTH:
    case CS2VM_HOST_REQUEST_CC_SETTEXTFONT:
        task_cs2_plan_font(self);
        break;

    /* Sync-only: host never yields these for cache loads. Leave YIELD_NONE. */
    case CS2VM_HOST_REQUEST_INVS_GET_SIZE:
    case CS2VM_HOST_REQUEST_INVS_GET_OBJ:
    case CS2VM_HOST_REQUEST_INVS_GET_NUM:
    case CS2VM_HOST_REQUEST_INVS_GET_TOTAL:
    case CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR:
    case CS2VM_HOST_REQUEST_VARS_READ_VARBIT:
    case CS2VM_HOST_REQUEST_VARS_READ_VARC_INT:
    case CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING:
    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_INT:
    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_STRING:
    case CS2VM_HOST_REQUEST_CC_DELETEALL:
    case CS2VM_HOST_REQUEST_CC_SETPOSITION:
    case CS2VM_HOST_REQUEST_CC_SETSIZE:
    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC2:
    case CS2VM_HOST_REQUEST_CC_SETTILING:
    case CS2VM_HOST_REQUEST_CC_SETOUTLINE:
    case CS2VM_HOST_REQUEST_CC_SETGRAPHICSHADOW:
    case CS2VM_HOST_REQUEST_CC_SETCOLOUR:
    case CS2VM_HOST_REQUEST_CC_SETFILL:
    case CS2VM_HOST_REQUEST_CC_SETTRANS:
    case CS2VM_HOST_REQUEST_CC_SETNOCLICKTHROUGH:
    case CS2VM_HOST_REQUEST_CC_SETTEXT:
    case CS2VM_HOST_REQUEST_CC_SETTEXTALIGN:
    case CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW:
    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLE:
    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLEBEHAVIOR:
    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADZONE:
    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADTIME:
    case CS2VM_HOST_REQUEST_CC_SETOP:
    case CS2VM_HOST_REQUEST_CC_GETID:
    case CS2VM_HOST_REQUEST_CC_GETX:
    case CS2VM_HOST_REQUEST_CC_GETY:
    case CS2VM_HOST_REQUEST_CC_GETWIDTH:
    case CS2VM_HOST_REQUEST_CC_GETHEIGHT:
    case CS2VM_HOST_REQUEST_CC_GETHIDE:
    case CS2VM_HOST_REQUEST_CC_GETTEXT:
    case CS2VM_HOST_REQUEST_CC_GETTRANS:
    case CS2VM_HOST_REQUEST_CC_SETONCLICK:
    case CS2VM_HOST_REQUEST_CC_SETONHOLD:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE:
    case CS2VM_HOST_REQUEST_CC_SETONDRAG:
    case CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL:
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
    case CS2VM_HOST_REQUEST_CC_SETONOP:
    case CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_CC_SETONTIMER:
    case CS2VM_HOST_REQUEST_CC_SETONVARTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONINVTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETON_DISCARD:
    case CS2VM_HOST_REQUEST_CC_SETSCROLLPOS:
    case CS2VM_HOST_REQUEST_CC_SETSCROLLSIZE:
    case CS2VM_HOST_REQUEST_CC_FINDROOT:
    case CS2VM_HOST_REQUEST_CC_RESOLVE_PARENT:
    case CS2VM_HOST_REQUEST_IF_GETWIDTH:
    case CS2VM_HOST_REQUEST_IF_GETHEIGHT:
    case CS2VM_HOST_REQUEST_IF_GETY:
    case CS2VM_HOST_REQUEST_IF_GETLAYER:
    case CS2VM_HOST_REQUEST_IF_GETTOP:
    case CS2VM_HOST_REQUEST_IF_GETSCROLLX:
    case CS2VM_HOST_REQUEST_IF_GETSCROLLY:
    case CS2VM_HOST_REQUEST_IF_GETSCROLLHEIGHT:
    case CS2VM_HOST_REQUEST_IF_GETHIDE:
    case CS2VM_HOST_REQUEST_IF_GETX:
    case CS2VM_HOST_REQUEST_IF_GETTEXT:
    case CS2VM_HOST_REQUEST_IF_GETSCROLLWIDTH:
    case CS2VM_HOST_REQUEST_IF_SETHIDE:
    case CS2VM_HOST_REQUEST_IF_SETPOSITION:
    case CS2VM_HOST_REQUEST_IF_SETSIZE:
    case CS2VM_HOST_REQUEST_IF_SETSCROLLPOS:
    case CS2VM_HOST_REQUEST_IF_SETSCROLLSIZE:
    case CS2VM_HOST_REQUEST_IF_SETTEXT:
    case CS2VM_HOST_REQUEST_IF_SETOUTLINE:
    case CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT:
    case CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT:
    case CS2VM_HOST_REQUEST_IF_SETONOP:
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER:
    case CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE:
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_IF_SETONTIMER:
    case CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL:
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
    case CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT:
    case CS2VM_HOST_REQUEST_IF_SETON_DISCARD:
    case CS2VM_HOST_REQUEST_IF_SETOP:
    case CS2VM_HOST_REQUEST_IF_SETOPBASE:
    case CS2VM_HOST_REQUEST_IF_SETOPSUBMENU:
    case CS2VM_HOST_REQUEST_IF_SETTARGETPRIORITY:
    case CS2VM_HOST_REQUEST_IF_CLEAROPS:
    case CS2VM_HOST_REQUEST_WIDGET_SET_INT2:
    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_ANGLE:
    case CS2VM_HOST_REQUEST_WIDGET_SET_ARC:
    case CS2VM_HOST_REQUEST_WIDGET_INPUT_INT:
    case CS2VM_HOST_REQUEST_CLIENTCLOCK:
        break;

    default:
        fprintf(
            stderr,
            "Task_CS2Run: unhandled yield kind %d (script %d)\n",
            (int)self->pending.kind,
            self->script_id);
        self->yield_plan = TASK_CS2_YIELD_ABORT;
        break;
    }
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
    {
        fprintf(stderr, "Task_CS2Run: no script to run\n");
        PT_EXIT(&self->pt);
    }

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

        {
            /* Mixed positional args: ints fill int locals in order, strings
             * fill string locals in order (OSRS ScriptEvent semantics). */
            int int_i = 0;
            int str_i = 0;
            for( j = 0; j < self->int_arg_count; j++ )
            {
                if( j < 64 && (self->str_mask & ((uint64_t)1 << j)) )
                {
                    char const* s =
                        str_i < self->str_arg_count ? self->str_args[str_i] : "";
                    (void)CS2VM2_SetStringCurrentFrameLocal(thread, str_i, s);
                    str_i++;
                }
                else
                {
                    task_cs2_set_int_local(
                        self->host,
                        thread,
                        int_i,
                        self->int_args[j],
                        self->active_component_id);
                    int_i++;
                }
            }
        }

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
            /* Bytecode window around the failing pc — unknown/mis-stubbed
             * opcodes only surface as underflows a few ops later. */
            {
                struct CS2VM2_Script* dbg =
                    CacheProvider_ClientScriptGet(self->provider, thread->last_error_script_id);
                if( dbg )
                {
                    int pci;
                    fprintf(
                        stderr,
                        "  script %d: int_args=%d str_args=%d int_locals=%d str_locals=%d "
                        "ops=%d\n",
                        dbg->script_id,
                        dbg->int_argument_count,
                        dbg->string_argument_count,
                        dbg->local_int_count,
                        dbg->local_string_count,
                        dbg->op_count);
                    pci = thread->last_error_pc - 24;
                    if( pci < 0 )
                        pci = 0;
                    for( ; pci < dbg->op_count && pci <= thread->last_error_pc + 2; pci++ )
                        fprintf(
                            stderr,
                            "  pc=%d op=%d %s operand=%d str=%s\n",
                            pci,
                            dbg->opcodes[pci],
                            CS2_OpCode_String(dbg->opcodes[pci]),
                            dbg->int_operands ? dbg->int_operands[pci] : 0,
                            (dbg->string_operands && dbg->string_operands[pci])
                                ? dbg->string_operands[pci]
                                : "(null)");
                }
            }
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

        /* Flat switch (no PT) then linear awaits — protothreads cannot nest switch. */
        task_cs2_plan_yield(self);

        if( self->yield_plan == TASK_CS2_YIELD_ABORT )
        {
            CS2VM2_ResetRuntime(CS2VM2_ThreadMain(&self->vm));
            PT_EXIT(&self->pt);
        }
        else if( self->yield_plan == TASK_CS2_YIELD_SCRIPT )
        {
            TASK_AWAITSELF_IF(CreateTask_ClientScriptLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_ENUM )
        {
            TASK_AWAITSELF_IF(CreateTask_EnumLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_STRUCT )
        {
            if( self->await_id >= 0 )
                TASK_AWAITSELF_IF(CreateTask_StructLoad(self->provider, self->await_id));
            if( self->await_id2 >= 0 )
                TASK_AWAITSELF_IF(CreateTask_ParamLoad(self->provider, self->await_id2));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_OBJ )
        {
            if( self->await_id >= 0 )
                TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->await_id));
            if( self->await_id2 >= 0 )
                TASK_AWAITSELF_IF(CreateTask_ParamLoad(self->provider, self->await_id2));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_COMPONENT )
        {
            assert(self->host->tree);
            TASK_AWAITSELF_IF(CreateTask_ComponentPackLoad(self->provider, self->await_id));
            TASK_AWAITSELF_IF(CreateTask_PackAssetsLoad(self->provider, self->await_id));
            task_cs2_bake_pack(self);
            if( !CacheProvider_ComponentPackHas(self->provider, self->await_id) )
            {
                CS2VM2_ResetRuntime(CS2VM2_ThreadMain(&self->vm));
                PT_EXIT(&self->pt);
            }
        }
        else if( self->yield_plan == TASK_CS2_YIELD_MODEL )
        {
            TASK_AWAITSELF_IF(CreateTask_ModelLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_NPC )
        {
            TASK_AWAITSELF_IF(CreateTask_NpcLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_SETOBJECT )
        {
            int ids[1];
            int counts[1];
            ids[0] = self->yield_obj_id;
            counts[0] = self->yield_obj_count;
            TASK_AWAITSELF_IF(CreateTask_ObjModelLoad(self->provider, ids, counts, 1));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_SPRITE )
        {
            TASK_AWAITSELF_IF(CreateTask_SpriteLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_FONT )
        {
            TASK_AWAITSELF_IF(CreateTask_FontLoad(self->provider, self->await_id));
        }
        /* TASK_CS2_YIELD_NONE: expected no-op (clear ids). Re-enter ThreadRun. */
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
    int int_arg_count,
    uint64_t str_mask,
    char const* const* str_args,
    int str_arg_count)
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

    self->str_mask = str_mask;
    if( str_arg_count > TASK_CS2_RUN_STR_ARGS_MAX )
        str_arg_count = TASK_CS2_RUN_STR_ARGS_MAX;
    if( str_arg_count < 0 || !str_args )
        str_arg_count = 0;
    self->str_arg_count = str_arg_count;
    for( int i = 0; i < str_arg_count; i++ )
    {
        strncpy(self->str_args[i], str_args[i] ? str_args[i] : "", TASK_CS2_RUN_STR_ARG_LEN - 1);
        self->str_args[i][TASK_CS2_RUN_STR_ARG_LEN - 1] = '\0';
    }

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
        host,
        script_id,
        NULL,
        active_component_id,
        dot_component_id,
        int_args,
        int_arg_count,
        0,
        NULL,
        0);
}

struct ToriRS_Task*
CreateTask_CS2RunMixed(
    struct RS_CS2Host* host,
    int script_id,
    int active_component_id,
    int dot_component_id,
    int const* args,
    int arg_count,
    uint64_t str_mask,
    char const* const* str_args,
    int str_arg_count)
{
    return task_cs2_run_new(
        host,
        script_id,
        NULL,
        active_component_id,
        dot_component_id,
        args,
        arg_count,
        str_mask,
        str_args,
        str_arg_count);
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
        int_arg_count,
        0,
        NULL,
        0);
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
    struct RS_CS2InvTransmitHook* hook;

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
        /* Dead hook (component reclaimed): mark seen so it never fires — a missing
         * component reads as "not hidden" below. */
        if( UITree_FindByComponentId(self->host->tree, hook->component_id) < 0 )
        {
            hook->last_seen_serial = self->host->inv_change_serial;
            continue;
        }
        /* Hidden: skip WITHOUT advancing so the hook fires once when unhidden
         * (TS parity: hidden nodes are skipped before counter sync). */
        if( UITree_ComponentOrAncestorHidden(self->host->tree, hook->component_id) )
            continue;
        /* Already fired for the current inv state — the per-hook serial gate that
         * makes widgets-loaded re-traversals free (TS lastChangedInvCount). */
        if( hook->last_seen_serial >= self->host->inv_change_serial )
            continue;
        hook->last_seen_serial = self->host->inv_change_serial;

#if UITREE_CLICK_DEBUG
        fprintf(
            stderr,
            "uitree_click: InvTransmitDispatch script_id=%d component_id=%d argc=%d "
            "container_filter=%d\n",
            hook->script_id,
            hook->component_id,
            hook->int_arg_count,
            self->container_id);
#endif

        {
            char const* str_ptrs[CS2VM_SETON_STR_ARG_MAX];
            int si;
            for( si = 0; si < CS2VM_SETON_STR_ARG_MAX; si++ )
                str_ptrs[si] = hook->str_args[si];
            /* CreateTask copies the strings immediately, so the locals need
             * not survive the protothread yield. */
            TASK_AWAITSELF(CreateTask_CS2RunMixed(
                self->host,
                hook->script_id,
                hook->component_id,
                hook->component_id,
                hook->int_args,
                hook->int_arg_count,
                hook->str_arg_mask,
                str_ptrs,
                hook->str_arg_count));
        }
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

/* =========================================================================
 * Var-transmit dispatch
 * ========================================================================= */

struct Task_CS2VarTransmitDispatch
{
    struct ToriRS_Task task;
    struct pt pt;

    struct RS_CS2Host* host;
    int var_id;
    int hook_index;
};

static int
hook_matches_var(
    struct RS_CS2VarTransmitHook const* hook,
    int var_id)
{
    int i;
    assert(hook);
    if( var_id < 0 )
        return 1;
    if( hook->trigger_count <= 0 )
        return 1;
    for( i = 0; i < hook->trigger_count; i++ )
    {
        if( hook->trigger_ids[i] == var_id )
            return 1;
    }
    return 0;
}

static int
Task_CS2VarTransmitDispatch_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_CS2VarTransmitDispatch* self = (struct Task_CS2VarTransmitDispatch*)task;
    struct RS_CS2VarTransmitHook* hook;

    PT_BEGIN(&self->pt);

    assert(self->host);

    for( self->hook_index = 0; self->hook_index < self->host->var_transmit_hook_count;
         self->hook_index++ )
    {
        hook = &self->host->var_transmit_hooks[self->hook_index];
        if( !hook_matches_var(hook, self->var_id) )
            continue;
        if( hook->script_id <= 0 )
            continue;
        /* Dead hook (component reclaimed): mark seen so it never fires — a missing
         * component reads as "not hidden" below. */
        if( UITree_FindByComponentId(self->host->tree, hook->component_id) < 0 )
        {
            hook->last_seen_serial = self->host->var_change_serial;
            continue;
        }
        /* Hidden: skip WITHOUT advancing so the hook fires once when unhidden. */
        if( UITree_ComponentOrAncestorHidden(self->host->tree, hook->component_id) )
            continue;
        /* Already fired for the current var state (TS lastChangedVarpCount gate). */
        if( hook->last_seen_serial >= self->host->var_change_serial )
            continue;
        hook->last_seen_serial = self->host->var_change_serial;

        {
            char const* str_ptrs[CS2VM_SETON_STR_ARG_MAX];
            int si;
            for( si = 0; si < CS2VM_SETON_STR_ARG_MAX; si++ )
                str_ptrs[si] = hook->str_args[si];
            /* CreateTask copies the strings immediately, so the locals need
             * not survive the protothread yield. */
            TASK_AWAITSELF(CreateTask_CS2RunMixed(
                self->host,
                hook->script_id,
                hook->component_id,
                hook->component_id,
                hook->int_args,
                hook->int_arg_count,
                hook->str_arg_mask,
                str_ptrs,
                hook->str_arg_count));
        }
    }

    PT_END(&self->pt);
    return 0;
}

static void
Task_CS2VarTransmitDispatch_Free(struct ToriRS_Task* task)
{
    struct Task_CS2VarTransmitDispatch* self = (struct Task_CS2VarTransmitDispatch*)task;
    assert(self);
    free(self);
}

static struct ToriRS_TaskVTable Task_CS2VarTransmitDispatch_VTable = {
    .run = Task_CS2VarTransmitDispatch_Run,
    .free = Task_CS2VarTransmitDispatch_Free,
};

struct ToriRS_Task*
CreateTask_CS2VarTransmitDispatch(
    struct RS_CS2Host* host,
    int var_id)
{
    struct Task_CS2VarTransmitDispatch* self;

    assert(host);

    self = calloc(1, sizeof(*self));
    assert(self);
    self->task.vtable = &Task_CS2VarTransmitDispatch_VTable;
    strcpy(self->task.name, "CS2VarTransmitDispatch");
    self->host = host;
    self->var_id = var_id;
    PT_INIT(&self->pt);
    return &self->task;
}
