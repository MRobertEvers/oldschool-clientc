#include "../src/osrs/rscache/rscache.u.c"
#include "../src2/toriauxlib/toriauxlib.h"
#include "../src2/vm/cs2_opcode.h"
#include "bmp.h"
#include "osrs/rscache/dat2a/dat2a_clientscript.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_clientscript_convert.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore_types.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_model_sprite.h"
#include "games/ie_enum_lookup.h"
#include "vm/cs2_script.h"
#include <sys/stat.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INTERFACEX_DEBUG_OPS 0

#define CANVAS_W 1024
#define CANVAS_H 768
#define CANVAS_BG 0xFF202428

static inline int
uitree_mul_shift14(
    int a,
    int b)
{
    return (int)(((int64_t)a * (int64_t)b) >> 14);
}

struct UITreeNodeXLink
{
    int parent_tree_idx;
    int next_sibling_tree_idx;
    int first_child_tree_idx;
    int last_child_tree_idx;
};

struct UITreeXNode_RSLayer
{
    int scroll_width;
    int scroll_height;
};

struct UITreeXNode_RSGraphic
{
    int graphic_id;
};

struct UITreeXNode_RSObj
{
    int obj_id;
    int obj_count;
};

enum UITreeXNodeKind
{
    UITreeXNodeKind_Root,
    UITreeXNodeKind_RSLayer,
    UITreeXNodeKind_RSGraphic,
    UITreeXNodeKind_RSObj,
};

struct UITreeXNode
{
    int user_id;
    int idx;
    struct UITreeNodeXLink link;
    enum UITreeXNodeKind kind;
    int x;
    int y;
    int w;
    int h;
    int8_t x_mode;
    int8_t y_mode;
    int8_t w_mode;
    int8_t h_mode;
    int if3;
    int hidden;
    int tiling;
    int aspect_w;
    int aspect_h;
    int abs_x;
    int abs_y;
    int abs_w;
    int abs_h;
    int layout_resolved;
    int dynamic;
    int child_index;
    union
    {
        struct UITreeXNode_RSLayer rs_layer;
        struct UITreeXNode_RSGraphic rs_graphic;
        struct UITreeXNode_RSObj rs_obj;
    } u;
};

#define MAX_NODES 1024
struct UITreeX
{
    int node_count;
    struct UITreeXNode nodes[MAX_NODES];
};

static void
UITreeX_NodeInit(
    struct UITreeXNode* node,
    int idx)
{
    assert(node);
    memset(node, 0, sizeof(struct UITreeXNode));
    node->idx = idx;
    node->user_id = -1;
    node->link.parent_tree_idx = -1;
    node->link.next_sibling_tree_idx = -1;
    node->link.first_child_tree_idx = -1;
    node->link.last_child_tree_idx = -1;
}

struct UITreeX*
UITreeX_New(void)
{
    struct UITreeX* tree = calloc(1, sizeof(struct UITreeX));
    assert(tree);

    for( int i = 0; i < MAX_NODES; i++ )
        UITreeX_NodeInit(&tree->nodes[i], i);

    return tree;
}

struct UITreeXNode*
UITreeX_NodeEmplace(struct UITreeX* tree)
{
    assert(tree);
    assert(tree->node_count < MAX_NODES);
    struct UITreeXNode* node = &tree->nodes[tree->node_count++];
    UITreeX_NodeInit(node, tree->node_count - 1);
    return node;
}

struct UITreeXBuilder_ParentStack
{
    int parent_idx;
    int last_sibling_idx;
};

struct UITreeXBuilder
{
    struct UITreeXBuilder_ParentStack parent_stack[36];
    int parent_stack_top;

    struct UITreeX* tree;
};

void
UITreeXBuilder_ParentStackInitNode(struct UITreeXBuilder_ParentStack* parent_stack)
{
    assert(parent_stack);
    memset(parent_stack, 0, sizeof(struct UITreeXBuilder_ParentStack));
    parent_stack->parent_idx = -1;
    parent_stack->last_sibling_idx = -1;
}

void
UITreeXBuilder_Init(
    struct UITreeXBuilder* builder,
    struct UITreeX* tree)
{
    assert(builder);
    assert(tree);
    memset(builder, 0, sizeof(struct UITreeXBuilder));
    builder->tree = tree;
    builder->parent_stack_top = 0;

    for( int i = 0; i < 36; i++ )
        UITreeXBuilder_ParentStackInitNode(&builder->parent_stack[i]);
}

int
UITreeXBuilder_LinkPushSibling(
    struct UITreeXBuilder* builder,
    struct UITreeXNode* node)
{
    assert(builder);

    int parent_idx = builder->parent_stack[builder->parent_stack_top].parent_idx;
    if( parent_idx != -1 )
    {
        int first_child_idx = builder->tree->nodes[parent_idx].link.first_child_tree_idx;
        if( first_child_idx == -1 )
            builder->tree->nodes[parent_idx].link.first_child_tree_idx = node->idx;

        int last_child_idx = builder->tree->nodes[parent_idx].link.last_child_tree_idx;
        if( last_child_idx != -1 )
            builder->tree->nodes[last_child_idx].link.next_sibling_tree_idx = node->idx;

        builder->tree->nodes[parent_idx].link.last_child_tree_idx = node->idx;
        node->link.parent_tree_idx = parent_idx;
    }
    else
    {
        int last_sibling_idx = builder->parent_stack[builder->parent_stack_top].last_sibling_idx;
        if( last_sibling_idx != -1 )
            builder->tree->nodes[last_sibling_idx].link.next_sibling_tree_idx = node->idx;

        builder->parent_stack[builder->parent_stack_top].last_sibling_idx = node->idx;
    }

    return 0;
}

int
UITreeXBuilder_LinkPushParent(
    struct UITreeXBuilder* builder,
    struct UITreeXNode* node)
{
    assert(builder);
    assert(builder->parent_stack_top < 36);

    UITreeXBuilder_LinkPushSibling(builder, node);

    builder->parent_stack_top += 1;
    builder->parent_stack[builder->parent_stack_top].parent_idx = node->idx;

    return 0;
}

int
UITreeXBuilder_SetActiveParentByUserId(
    struct UITreeXBuilder* builder,
    int user_id)
{
    assert(builder);
    assert(builder->parent_stack_top < 36);

    if( user_id == -1 )
    {
        builder->parent_stack_top = 0;
        builder->parent_stack[builder->parent_stack_top].parent_idx = -1;
        builder->parent_stack[builder->parent_stack_top].last_sibling_idx = -1;
        return 0;
    }

    for( int i = 0; i < builder->tree->node_count; i++ )
    {
        if( builder->tree->nodes[i].user_id == user_id )
        {
            builder->parent_stack[builder->parent_stack_top].parent_idx = i;
            builder->parent_stack[builder->parent_stack_top].last_sibling_idx =
                builder->tree->nodes[i].link.last_child_tree_idx;
            return 0;
        }
    }

    return 0;
}

int
UITreeXBuilder_PushLayerWithParentUserId(
    struct UITreeXBuilder* builder,
    int user_id,
    int parent_user_id)
{
    assert(builder);
    assert(builder->parent_stack_top < 36);
    struct UITreeXNode* node = UITreeX_NodeEmplace(builder->tree);

    UITreeXBuilder_SetActiveParentByUserId(builder, parent_user_id);

    UITreeXBuilder_LinkPushSibling(builder, node);

    node->kind = UITreeXNodeKind_RSLayer;
    node->user_id = user_id;

    return node->idx;
}

#define UITREEXBUILDER_GRAPHIC_ID_NULL -1

int
UITreeXBuilder_PushGraphicWithParentUserId(
    struct UITreeXBuilder* builder,
    int user_id,
    int graphic_id,
    int parent_user_id)
{
    assert(builder);
    assert(builder->parent_stack_top < 36);
    struct UITreeXNode* node = UITreeX_NodeEmplace(builder->tree);

    UITreeXBuilder_SetActiveParentByUserId(builder, parent_user_id);

    UITreeXBuilder_LinkPushSibling(builder, node);

    node->kind = UITreeXNodeKind_RSGraphic;
    node->user_id = user_id;

    // node->u.rs_graphic.scene_id = scene_id;
    node->u.rs_graphic.graphic_id = graphic_id;

    return node->idx;
}

static char const*
UITreeX_NodeKindStr(enum UITreeXNodeKind kind)
{
    switch( kind )
    {
    case UITreeXNodeKind_Root:
        return "root";
    case UITreeXNodeKind_RSLayer:
        return "layer";
    case UITreeXNodeKind_RSGraphic:
        return "graphic";
    case UITreeXNodeKind_RSObj:
        return "obj";
    default:
        return "?";
    }
}

static void
UITreeX_UserIdFormat(
    char* buf,
    size_t buf_size,
    int user_id)
{
    if( !buf || buf_size == 0 )
        return;

    if( user_id < 0 )
    {
        snprintf(buf, buf_size, "-1");
        return;
    }

    snprintf(
        buf,
        buf_size,
        "0x%08x (%d<<16|%d)",
        (unsigned)user_id,
        (user_id >> 16) & 0xFFFF,
        user_id & 0xFFFF);
}

static void
UITreeX_PrintNode(
    struct UITreeX const* tree,
    int node_idx,
    int depth)
{
    assert(tree);
    assert(node_idx >= 0 && node_idx < tree->node_count);

    struct UITreeXNode const* node = &tree->nodes[node_idx];
    char user_id_buf[48];

    for( int i = 0; i < depth * 2; i++ )
        putchar(' ');

    UITreeX_UserIdFormat(user_id_buf, sizeof(user_id_buf), node->user_id);
    printf(
        "[%d] kind=%s user_id=%s %s",
        node_idx,
        UITreeX_NodeKindStr(node->kind),
        user_id_buf,
        node->dynamic ? "dynamic" : "static");

    if( node->kind == UITreeXNodeKind_RSGraphic )
    {
        printf(
            " graphic=%d abs=%d,%d %dx%d hidden=%d",
            node->u.rs_graphic.graphic_id,
            node->abs_x,
            node->abs_y,
            node->abs_w,
            node->abs_h,
            node->hidden);
    }
    else if( node->kind == UITreeXNodeKind_RSObj )
    {
        printf(
            " obj=%d count=%d abs=%d,%d %dx%d hidden=%d",
            node->u.rs_obj.obj_id,
            node->u.rs_obj.obj_count,
            node->abs_x,
            node->abs_y,
            node->abs_w,
            node->abs_h,
            node->hidden);
    }

    putchar('\n');

    for( int child = node->link.first_child_tree_idx; child != -1;
         child = tree->nodes[child].link.next_sibling_tree_idx )
        UITreeX_PrintNode(tree, child, depth + 1);
}

void
UITreeX_PrintNodes(struct UITreeX const* tree)
{
    if( !tree )
    {
        printf("UITreeX_PrintNodes: tree is NULL\n");
        return;
    }

    printf("uitreex: %d nodes\n", tree->node_count);

    for( int i = 0; i < tree->node_count; i++ )
    {
        if( tree->nodes[i].link.parent_tree_idx == -1 )
            UITreeX_PrintNode(tree, i, 0);
    }
}

/**
 * Magic number constants used in script args for runtime substitution
 * These are special values that get replaced with actual event data at execution time
 */
//  export const ScriptArgMagic = {
//     MOUSE_X: -2147483647, // Integer.MIN_VALUE + 1
//     MOUSE_Y: -2147483646, // Integer.MIN_VALUE + 2
//     WIDGET_ID: -2147483645, // Integer.MIN_VALUE + 3
//     OP_INDEX: -2147483644, // Integer.MIN_VALUE + 4
//     WIDGET_CHILD_INDEX: -2147483643, // Integer.MIN_VALUE + 5
//     DRAG_TARGET_ID: -2147483642, // Integer.MIN_VALUE + 6
//     DRAG_TARGET_CHILD_INDEX: -2147483641, // Integer.MIN_VALUE + 7
//     KEY_TYPED: -2147483640, // Integer.MIN_VALUE + 8
//     KEY_PRESSED: -2147483639, // Integer.MIN_VALUE + 9
//     OP_SUBINDEX: -2147483638, // Integer.MIN_VALUE + 10
// } as const;

#define CS2VM_SCRIPT_ARG_MOUSE_X -2147483647
#define CS2VM_SCRIPT_ARG_MOUSE_Y -2147483646
#define CS2VM_SCRIPT_ARG_WIDGET_ID -2147483645
#define CS2VM_SCRIPT_ARG_OP_INDEX -2147483644
#define CS2VM_SCRIPT_ARG_WIDGET_CHILD_INDEX -2147483643
#define CS2VM_SCRIPT_ARG_DRAG_TARGET_ID -2147483642
#define CS2VM_SCRIPT_ARG_DRAG_TARGET_CHILD_INDEX -2147483641
#define CS2VM_SCRIPT_ARG_KEY_TYPED -2147483640
#define CS2VM_SCRIPT_ARG_KEY_PRESSED -2147483639
#define CS2VM_SCRIPT_ARG_OP_SUBINDEX -2147483638

#define CS2VM_EXECNO_YIELD -2
#define CS2VM_EXECNO_ERROR -1
#define CS2VM_EXECNO_OK 0
#define CS2VM_EXECNO_DONE 1

#define CS2VM_STACK_MAX 1024

#define CACHE_PATH "/Users/matthewevers/Documents/git_repos/3draster/cache"

#define CS2VM_USER(vm) ((struct CS2VMX*)(vm))->user
#define CS2VM_FRAME(vm) &((struct CS2VMX*)(vm))->frames[((struct CS2VMX*)(vm))->frame_sp - 1]
#define CS2VM_MAX_LOCALS 1024
struct CS2VMX_Frame
{
    struct CS2_Script* script;
    int pc;
    int int_locals[CS2VM_MAX_LOCALS];
    char* str_locals[CS2VM_MAX_LOCALS];

    int return_pc;
    int return_frame;

    int has_return;
    int return_int_count;
    int return_ints[8];
};

#define CS2VM_MAX_FRAMES 32
#define CS2VM_MAX_CYCLES 1000000

struct CS2VMX;
struct CS2VM_HostRequest;
struct InterfaceX_VMHost;

static int
InterfaceX_EnumLookup(
    struct InterfaceX_VMHost* host,
    int input_type,
    int output_type,
    int enum_id,
    int key);

typedef int (*CS2VMX_HostExec_Fn)(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request);

struct CS2VMX
{
    void* user;
    CS2VMX_HostExec_Fn host_exec;
    struct CS2VMX_Frame frames[CS2VM_MAX_FRAMES];

    int ints_stack[CS2VM_STACK_MAX];
    int ints_stack_top;
    char* strs_stack[CS2VM_STACK_MAX];
    int strs_stack_top;

    int frame_sp;

    int active_component_id;
    int dot_component_id;
};

enum CS2VM_HostRequestKind
{
    CS2VM_HOST_REQUEST_PUSHSCRIPT,

    CS2VM_HOST_REQUEST_INVS_GET_SIZE,
    CS2VM_HOST_REQUEST_INVS_GET_OBJ,
    CS2VM_HOST_REQUEST_INVS_GET_TOTAL,
    CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR,
    // CC Child component
    CS2VM_HOST_REQUEST_CC_DELETEALL,
    CS2VM_HOST_REQUEST_CC_CREATE,
    CS2VM_HOST_REQUEST_CC_SETPOSITION,
    CS2VM_HOST_REQUEST_CC_SETSIZE,
    CS2VM_HOST_REQUEST_CC_SETGRAPHIC,
    CS2VM_HOST_REQUEST_CC_SETTILING,
    CS2VM_HOST_REQUEST_CC_SETOBJECT,
    // IF Interfaces
    CS2VM_HOST_REQUEST_IF_GETWIDTH,
    CS2VM_HOST_REQUEST_IF_GETHEIGHT,
    CS2VM_HOST_REQUEST_IF_SETHIDE,
    CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT,
    CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT,
    CS2VM_HOST_REQUEST_IF_SETONOP,
    CS2VM_HOST_REQUEST_IF_SETOP,
    CS2VM_HOST_REQUEST_IF_CLEAROPS,
    CS2VM_HOST_REQUEST_IF_SETOBJECT,
    // OC Object config
    CS2VM_HOST_REQUEST_OC_PARAM,
};

struct CS2VM_HostRequest_PushScript
{
    int script_id;
};

struct CS2VM_HostRequest_InvSize
{
    int inv_id;
};

struct CS2VM_HostRequest_InvGetObj
{
    int inv_id;
    int slot;
};

struct CS2VM_HostRequest_InvTotal
{
    int inv_id;
    int item_id;
};

struct CS2VM_HostRequest_VarsReadVarp
{
    int varp_id;
};

struct CS2VM_HostRequest_CC_DeleteAll
{
    int component_id;
};

struct CS2VM_HostRequest_CC_Create
{
    int parent_id;
    int component_type;
    int child_index;
    int is_nested;
    int dot_operand;
};

struct CS2VM_HostRequest_IF_GetWidth
{
    int component_id;
};

struct CS2VM_HostRequest_IF_GetHeight
{
    int component_id;
};

struct CS2VM_HostRequest_IF_SetHide
{
    int component_id;
    bool hidden;
};

struct CS2VM_HostRequest_IF_SetOnVarTransmit
{
    int component_id;
    int script_id;
    char* signature;
    int* trigger_ids;
    int trigger_count;
};

struct CS2VM_HostRequest_IF_SetOnInvTransmit
{
    int component_id;
    int script_id;
    char* signature;
    int* trigger_ids;
    int trigger_count;
    int int_args[16];
    int int_arg_count;
};

struct CS2VM_HostRequest_IF_SetOnOp
{
    int component_id;
    int script_id;
    char* signature;
    int* trigger_ids;
    int trigger_count;
};

struct CS2VM_HostRequest_IF_ClearOps
{
    int component_id;
};

struct CS2VM_HostRequest_IF_SetOp
{
    int component_id;
    int index;
    char* text;
};

struct CS2VM_HostRequest_CC_SetPosition
{
    int component_id;
    int x;
    int y;
    int xmode;
    int ymode;
};

struct CS2VM_HostRequest_CC_SetSize
{
    int component_id;
    int width;
    int height;
    int wmode;
    int hmode;
};

struct CS2VM_HostRequest_CC_SetGraphic
{
    int component_id;
    int graphic_id;
};

struct CS2VM_HostRequest_CC_SetTiling
{
    int component_id;
    int tiling;
};

struct CS2VM_HostRequest_CC_SetObject
{
    int component_id;
    int obj_id;
    int count;
};

struct CS2VM_HostRequest_IF_SetObject
{
    int component_id;
    int obj_id;
    int count;
};

struct CS2VM_HostRequest_OC_Param
{
    int param_id;
    int item_id;
};

struct CS2VM_HostRequest
{
    enum CS2VM_HostRequestKind kind;

    union
    {
        struct CS2VM_HostRequest_PushScript push_script;
        struct CS2VM_HostRequest_InvSize invs_get_size;
        struct CS2VM_HostRequest_InvGetObj invs_get_obj;
        struct CS2VM_HostRequest_InvTotal invs_get_total;
        struct CS2VM_HostRequest_VarsReadVarp vars_read_varp;
        struct CS2VM_HostRequest_CC_DeleteAll cc_delete_all;
        struct CS2VM_HostRequest_CC_Create cc_create;
        struct CS2VM_HostRequest_CC_SetPosition cc_set_position;
        struct CS2VM_HostRequest_CC_SetSize cc_set_size;
        struct CS2VM_HostRequest_CC_SetGraphic cc_set_graphic;
        struct CS2VM_HostRequest_CC_SetTiling cc_set_tiling;
        struct CS2VM_HostRequest_CC_SetObject cc_set_object;
        struct CS2VM_HostRequest_OC_Param oc_param;
        struct CS2VM_HostRequest_IF_GetWidth if_get_width;
        struct CS2VM_HostRequest_IF_GetHeight if_get_height;
        struct CS2VM_HostRequest_IF_SetHide if_set_hide;
        struct CS2VM_HostRequest_IF_SetOnVarTransmit if_set_on_var_transmit;
        struct CS2VM_HostRequest_IF_SetOnInvTransmit if_set_on_inv_transmit;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_op;
        struct CS2VM_HostRequest_IF_SetOp if_set_op;
        struct CS2VM_HostRequest_IF_ClearOps if_clear_ops;
        struct CS2VM_HostRequest_IF_SetObject if_set_object;
    } u;
};

void
CS2VMX_BindHost(
    struct CS2VMX* vm,
    void* user,
    CS2VMX_HostExec_Fn host_exec)
{
    assert(vm);
    assert(host_exec);
    vm->user = user;
    vm->host_exec = host_exec;
}

static inline int
CS2VMX_DotOrActiveComponentId(
    struct CS2VMX* vm,
    int operand)
{
    assert(vm);
    return operand == 1 ? vm->dot_component_id : vm->active_component_id;
}

static inline int
CS2VMX_JumpRelative(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    frame->pc += operand;

    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_PopFrame(struct CS2VMX* vm)
{
    assert(vm);
    vm->frame_sp--;
    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_PopInt(
    struct CS2VMX* vm,
    int* operand)
{
    assert(vm);
    if( vm->ints_stack_top <= 0 )
        return CS2VM_EXECNO_ERROR;
    *operand = vm->ints_stack[--vm->ints_stack_top];
    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_PopIntFrameLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    if( vm->ints_stack_top <= 0 )
        return CS2VM_EXECNO_ERROR;

    frame->int_locals[operand] = vm->ints_stack[--vm->ints_stack_top];

    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_PushInt(
    struct CS2VMX* vm,
    int value)
{
    assert(vm);

    if( vm->ints_stack_top >= CS2VM_STACK_MAX )
        return CS2VM_EXECNO_ERROR;

    vm->ints_stack[vm->ints_stack_top++] = value;

    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_SetIntFrameLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int idx,
    int value)
{
    assert(vm);
    assert(frame);
    frame->int_locals[idx] = value;
    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_SetIntCurrentFrameLocal(
    struct CS2VMX* vm,
    int idx,
    int value)
{
    assert(vm);
    return CS2VMX_SetIntFrameLocal(vm, CS2VM_FRAME(vm), idx, value);
}

static inline int
CS2VMX_PushIntFrameLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int idx)
{
    assert(vm);
    assert(frame);

    return CS2VMX_PushInt(vm, frame->int_locals[idx]);
}

static inline int
CS2VMX_PushIntCurrentFrameLocal(
    struct CS2VMX* vm,
    int idx)
{
    assert(vm);

    struct CS2VMX_Frame* frame = CS2VM_FRAME(vm);
    return CS2VMX_PushIntFrameLocal(vm, frame, idx);
}

static inline int
CS2VMX_PopStr(
    struct CS2VMX* vm,
    char** value)
{
    assert(vm);
    if( vm->strs_stack_top <= 0 )
        return CS2VM_EXECNO_ERROR;
    *value = vm->strs_stack[--vm->strs_stack_top];
    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_PushStr(
    struct CS2VMX* vm,
    char* value)
{
    assert(vm);

    if( vm->strs_stack_top >= CS2VM_STACK_MAX )
        return CS2VM_EXECNO_ERROR;

    vm->strs_stack[vm->strs_stack_top++] = value;

    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_PushStrFrameLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int idx)
{
    assert(vm);
    assert(frame);

    return CS2VMX_PushStr(vm, frame->str_locals[idx]);
}

int
CS2VMX_PushCallScript(
    struct CS2VMX* vm,
    struct CS2_Script* script);

int
CS2VMX_Op_PushVar(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR;
    request.u.vars_read_varp.varp_id = operand;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_PushConstantString(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    char* value)
{
    assert(vm);
    return CS2VMX_PushStr(vm, value);
}

int
CS2VMX_Op_PushConstantInt(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int value)
{
    assert(vm);
    return CS2VMX_PushInt(vm, value);
}

int
CS2VMX_Op_PushIntLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    return CS2VMX_PushIntFrameLocal(vm, frame, operand);
}

int
CS2VMX_Op_PushStrLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int idx)
{
    assert(vm);
    assert(frame);
    return CS2VMX_PushStrFrameLocal(vm, frame, idx);
}

int
CS2VMX_Op_PopIntLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    return CS2VMX_PopIntFrameLocal(vm, frame, operand);
}

int
CS2VMX_Op_GosubWithParams(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    assert(vm->host_exec);

    if( vm->frame_sp >= CS2VM_MAX_FRAMES )
        return CS2VM_EXECNO_ERROR;

    struct CS2VMX_Frame* caller = frame;
    caller->return_pc = caller->pc;
    caller->return_frame = vm->frame_sp - 1;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_PUSHSCRIPT;
    request.u.push_script.script_id = operand;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    struct CS2VMX_Frame* callee = &vm->frames[vm->frame_sp - 1];
    int argc = callee->script->int_argument_count + callee->script->string_argument_count;
    int str_args = callee->script->string_argument_count;
    int int_args = callee->script->int_argument_count;

    for( int i = str_args - 1; i >= 0; i-- )
    {
        char* value = NULL;
        if( CS2VMX_PopStr(vm, &value) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        callee->str_locals[i] = value;
    }
    for( int i = int_args - 1; i >= 0; i-- )
    {
        if( CS2VMX_PopIntFrameLocal(vm, callee, i) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_DeleteAll(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame)
{
    assert(vm);
    assert(frame);

    int component_id;

    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_DELETEALL;
    request.u.cc_delete_all.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

// CC_CREATE: When intOp=1 (dot variant .cc_create), sets dotWidget instead of activeWidget
//
// PARITY: Argument count depends on client revision, NOT stack contents.
// - Older revisions (< 200): 3 args [parentUid, type, childIndex] (aka Kronos)
// - Modern revisions (>= 200): 4 args [parentUid, type, childIndex, isNested]
int
CS2VMX_Op_CC_Create(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int is_nested, child_index, type, parent_id;

    if( CS2VMX_PopInt(vm, &is_nested) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &child_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &parent_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_CREATE;
    request.u.cc_create.parent_id = parent_id;
    request.u.cc_create.component_type = type;
    request.u.cc_create.child_index = child_index;
    request.u.cc_create.is_nested = is_nested;
    request.u.cc_create.dot_operand = operand;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

// === Position and Size ===
// 4 args read as array
int
CS2VMX_Op_CC_SetPosition(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int x, y, xmode, ymode;

    if( CS2VMX_PopInt(vm, &ymode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &xmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETPOSITION;
    request.u.cc_set_position.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_position.x = x;
    request.u.cc_set_position.y = y;
    request.u.cc_set_position.xmode = xmode;
    request.u.cc_set_position.ymode = ymode;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetSize(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int w, h, wmode, hmode;

    if( CS2VMX_PopInt(vm, &hmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &wmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &h) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &w) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETSIZE;
    request.u.cc_set_size.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_size.width = w;
    request.u.cc_set_size.height = h;
    request.u.cc_set_size.wmode = wmode;
    request.u.cc_set_size.hmode = hmode;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetGraphic(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int graphic_id;

    if( CS2VMX_PopInt(vm, &graphic_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHIC;
    request.u.cc_set_graphic.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_graphic.graphic_id = graphic_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetTiling(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int tiling;
    if( CS2VMX_PopInt(vm, &tiling) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTILING;
    request.u.cc_set_tiling.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_tiling.tiling = tiling;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetObject(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int count, obj_id;

    if( CS2VMX_PopInt(vm, &count) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &obj_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETOBJECT;
    request.u.cc_set_object.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_object.obj_id = obj_id;
    request.u.cc_set_object.count = count;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetObject(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, count, obj_id;

    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &count) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &obj_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOBJECT;
    request.u.if_set_object.component_id = component_id;
    request.u.if_set_object.obj_id = obj_id;
    request.u.if_set_object.count = count;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetHide(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int hide;
    if( CS2VMX_PopInt(vm, &hide) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETHIDE;
    request.u.if_set_hide.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.if_set_hide.hidden = hide != 0;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_GetWidth(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETWIDTH;
    request.u.if_get_width.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_GetHeight(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETHEIGHT;
    request.u.if_get_height.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetHide(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int component_id, hide;

    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &hide) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    bool hidden = hide != 0;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETHIDE;
    request.u.if_set_hide.component_id = component_id;
    request.u.if_set_hide.hidden = hidden;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

/**
 * Set event handler by widget UID (IF_SETON* opcodes)
 *
 * OSRS stack layout for IF_SETON* trigger hooks (bottom to top):
 * - int stack: [scriptId, intArgs..., widgetUid]  <- UID is at TOP
 * - string stack: [stringArgs..., signature]
 *
 * The widget UID is pushed LAST (so it's at the top), then the signature.
 * We pop: UID first, then signature, then args (reverse order), then scriptId.

  // Parse trigger args (pops signature, args, scriptId, and transmit triggers if 'Y' suffix)
        const parsed = this.parseTriggerArgs();

 */
int
CS2VMX_Op_IF_SetOnVarTransmit(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int widget_uid, script_id, trigger_count;
    int* trigger_ids = NULL;

    char* signature = NULL;

    if( CS2VMX_PopInt(vm, &widget_uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( CS2VMX_PopStr(vm, &signature) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int signature_len = signature ? (int)strlen(signature) : 0;
    int signature_parse_len = signature_len;
    if( signature_len > 0 && signature[signature_len - 1] == 'Y' )
    {
        if( CS2VMX_PopInt(vm, &trigger_count) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( trigger_count > 0 )
        {
            trigger_ids = calloc((size_t)trigger_count, sizeof(int));
            if( !trigger_ids )
                return CS2VM_EXECNO_ERROR;
            for( int i = trigger_count - 1; i >= 0; i-- )
            {
                if( CS2VMX_PopInt(vm, &trigger_ids[i]) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
        signature_parse_len = signature_len - 1;
    }

    if( signature && signature_parse_len > 0 )
    {
        for( int i = signature_parse_len - 1; i >= 0; i-- )
        {
            char c = signature[i];
            if( c == 's' || c == 'W' || c == 'X' )
            {
                char* v = NULL;
                if( CS2VMX_PopStr(vm, &v) != CS2VM_EXECNO_OK )
                    return CS2VM_EXECNO_ERROR;
                // argsArray[i] = v;
                // objectArgs.unshift(v);
            }
            else
            {
                int v = 0;
                if( CS2VMX_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                    return CS2VM_EXECNO_ERROR;
                // argsArray[i] = v;
                // intArgs.unshift(v);
            }
        }
    }

    if( CS2VMX_PopInt(vm, &script_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( script_id == -1 )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_OK;
    }

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT;
    request.u.if_set_on_var_transmit.component_id = widget_uid;
    request.u.if_set_on_var_transmit.script_id = script_id;
    request.u.if_set_on_var_transmit.signature = signature;
    request.u.if_set_on_var_transmit.trigger_ids = trigger_ids;
    request.u.if_set_on_var_transmit.trigger_count = trigger_count;

    int result = vm->host_exec(vm, &request);
    free(trigger_ids);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetOnInvTransmit(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int widget_uid, script_id, trigger_count = 0;
    int* trigger_ids = NULL;
    char* signature = NULL;

    if( CS2VMX_PopInt(vm, &widget_uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopStr(vm, &signature) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int signature_len = signature ? (int)strlen(signature) : 0;
    int signature_parse_len = signature_len;
    if( signature_len > 0 && signature[signature_len - 1] == 'Y' )
    {
        if( CS2VMX_PopInt(vm, &trigger_count) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( trigger_count > 0 )
        {
            trigger_ids = calloc((size_t)trigger_count, sizeof(int));
            if( !trigger_ids )
                return CS2VM_EXECNO_ERROR;
            for( int i = trigger_count - 1; i >= 0; i-- )
            {
                if( CS2VMX_PopInt(vm, &trigger_ids[i]) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
        signature_parse_len = signature_len - 1;
    }

    int int_args[16] = { 0 };
    int int_arg_count = 0;

    if( signature && signature_parse_len > 0 )
    {
        for( int i = signature_parse_len - 1; i >= 0; i-- )
        {
            char c = signature[i];
            if( c == 's' || c == 'W' || c == 'X' )
            {
                char* v = NULL;
                if( CS2VMX_PopStr(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
            else
            {
                int v = 0;
                if( CS2VMX_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
                if( i < (int)(sizeof(int_args) / sizeof(int_args[0])) )
                {
                    int_args[i] = v;
                    if( i + 1 > int_arg_count )
                        int_arg_count = i + 1;
                }
            }
        }
    }

    if( CS2VMX_PopInt(vm, &script_id) != CS2VM_EXECNO_OK )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_ERROR;
    }

    if( script_id == -1 )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_OK;
    }

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT;
    request.u.if_set_on_inv_transmit.component_id = widget_uid;
    request.u.if_set_on_inv_transmit.script_id = script_id;
    request.u.if_set_on_inv_transmit.signature = signature;
    request.u.if_set_on_inv_transmit.trigger_ids = trigger_ids;
    request.u.if_set_on_inv_transmit.trigger_count = trigger_count;
    memcpy(
        request.u.if_set_on_inv_transmit.int_args,
        int_args,
        sizeof(request.u.if_set_on_inv_transmit.int_args));
    request.u.if_set_on_inv_transmit.int_arg_count = int_arg_count;

    int result = vm->host_exec(vm, &request);
    free(trigger_ids);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetOnOp(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int widget_uid, script_id, trigger_count = 0;
    int* trigger_ids = NULL;
    char* signature = NULL;

    if( CS2VMX_PopInt(vm, &widget_uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( CS2VMX_PopStr(vm, &signature) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int signature_len = signature ? (int)strlen(signature) : 0;
    int signature_parse_len = signature_len;
    if( signature_len > 0 && signature[signature_len - 1] == 'Y' )
    {
        if( CS2VMX_PopInt(vm, &trigger_count) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( trigger_count > 0 )
        {
            trigger_ids = calloc((size_t)trigger_count, sizeof(int));
            if( !trigger_ids )
                return CS2VM_EXECNO_ERROR;
            for( int i = trigger_count - 1; i >= 0; i-- )
            {
                if( CS2VMX_PopInt(vm, &trigger_ids[i]) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
        signature_parse_len = signature_len - 1;
    }

    if( signature && signature_parse_len > 0 )
    {
        for( int i = signature_parse_len - 1; i >= 0; i-- )
        {
            char c = signature[i];
            if( c == 's' || c == 'W' || c == 'X' )
            {
                char* v = NULL;
                if( CS2VMX_PopStr(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
            else
            {
                int v = 0;
                if( CS2VMX_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
    }

    if( CS2VMX_PopInt(vm, &script_id) != CS2VM_EXECNO_OK )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_ERROR;
    }

    if( script_id == -1 )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_OK;
    }

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETONOP;
    request.u.if_set_on_op.component_id = widget_uid;
    request.u.if_set_on_op.script_id = script_id;
    request.u.if_set_on_op.signature = signature;
    request.u.if_set_on_op.trigger_ids = trigger_ids;
    request.u.if_set_on_op.trigger_count = trigger_count;

    int result = vm->host_exec(vm, &request);
    free(trigger_ids);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetOp(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int widget, index;
    char* text;

    if( CS2VMX_PopInt(vm, &widget) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( CS2VMX_PopInt(vm, &index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( CS2VMX_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOP;
    request.u.if_set_op.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.if_set_op.index = index;
    request.u.if_set_op.text = text;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_ClearOps(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_CLEAROPS;
    request.u.if_clear_ops.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_Add(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    int intpop_a, intpop_b;

    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, intpop_a + intpop_b);
}

int
CS2VMX_Op_Sub(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    int intpop_a, intpop_b;

    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, intpop_a - intpop_b);
}

int
CS2VMX_Op_Mul(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    (void)vm;
    assert(vm);
    assert(frame);
    int intpop_a, intpop_b;

    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, intpop_a * intpop_b);
}

int
CS2VMX_Op_Div(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int intpop_b, intpop_a;

    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_b == 0 )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, intpop_a / intpop_b);
}

int
CS2VMX_Op_Mod(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;
    int intpop_b, intpop_a;

    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_b == 0 )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, intpop_a % intpop_b);
}

int
CS2VMX_Op_Pow(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int intpop_b, intpop_a;

    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, (int)pow((double)intpop_a, (double)intpop_b));
}

int
CS2VMX_Op_PopIntDiscard(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame)
{
    assert(vm);
    int intpop;
    if( CS2VMX_PopInt(vm, &intpop) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_Enum(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int key, enum_id, output_type, input_type;

    if( CS2VMX_PopInt(vm, &key) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &enum_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &output_type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &input_type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)CS2VM_USER(vm);
    int value = InterfaceX_EnumLookup(host, input_type, output_type, enum_id, key);

    return CS2VMX_PushInt(vm, value);
}

int
CS2VMX_Op_IsMapMembers(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // Hardcode yes

    return CS2VMX_PushInt(vm, 1);
}

// IF_ICMPGT
int
CS2VMX_Op_BranchGreaterThan(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    // const b = ctx.intStack[--ctx.intStackSize];
    // const a = ctx.intStack[--ctx.intStackSize];
    // if( a > b )
    //     return { jump: intOp };

    int intpop_b, intpop_a;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a > intpop_b )
        return CS2VMX_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_BranchLessThan(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    // const b = ctx.intStack[--ctx.intStackSize];
    // const a = ctx.intStack[--ctx.intStackSize];
    // if( a < b )
    //     return { jump: intOp };

    int intpop_b, intpop_a;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a < intpop_b )
        return CS2VMX_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_BranchLessThanOrEquals(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int intpop_b, intpop_a;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a <= intpop_b )
        return CS2VMX_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_BranchGreaterThanOrEquals(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int intpop_b, intpop_a;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a >= intpop_b )
        return CS2VMX_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_BranchEquals(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const b = ctx.intStack[--ctx.intStackSize];
    // const a = ctx.intStack[--ctx.intStackSize];
    // if( a === b )
    //     return { jump: intOp };

    int intpop_b, intpop_a;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a == intpop_b )
        return CS2VMX_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_BranchNotEquals(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const cond = ctx.intStack[--ctx.intStackSize];
    // if( cond === 0 )
    //     return { jump: intOp };

    int intpop_b, intpop_a;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a != intpop_b )
        return CS2VMX_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_Branch(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    return CS2VMX_JumpRelative(vm, frame, operand);
}

int
CS2VMX_Op_TestBit(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const bit = ctx.intStack[--ctx.intStackSize];
    // const value = ctx.intStack[--ctx.intStackSize];
    // ctx.pushInt((value & (1 << bit)) !== 0 ? 1 : 0);

    int intpop_bit, intpop_value;
    if( CS2VMX_PopInt(vm, &intpop_bit) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, (intpop_value & (1 << intpop_bit)) != 0 ? 1 : 0);
}

// OC is object config
int
CS2VMX_Op_OC_Param(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // handlers.set(Opcodes.OC_PARAM, (ctx) => {
    //     const paramId = ctx.intStack[--ctx.intStackSize];
    //     const itemId = ctx.intStack[--ctx.intStackSize];
    //     const param = ctx.paramTypeLoader?.load(paramId);
    //     const obj = ctx.objTypeLoader?.load(itemId);
    //     if (param && obj && obj.params) {
    //         const val = obj.params.get(paramId);
    //         if (param.isString()) {
    //             ctx.pushString(typeof val === "string" ? val : param.defaultString || "");
    //         } else {
    //             ctx.pushInt(typeof val === "number" ? val : param.defaultInt || 0);
    //         }
    //     } else {
    //         if (param?.isString()) {
    //             ctx.pushString(param.defaultString || "");
    //         } else {
    //             ctx.pushInt(param?.defaultInt ?? 0);
    //         }
    //     }

    int param_id, item_id;
    if( CS2VMX_PopInt(vm, &param_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_PARAM;
    request.u.oc_param.param_id = param_id;
    request.u.oc_param.item_id = item_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_Return(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame)
{
    (void)vm;
    assert(vm);
    assert(frame);
    return CS2VMX_PopFrame(vm);
}

int
CS2VMX_Op_InvSize(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int inv_id;

    if( CS2VMX_PopInt(vm, &inv_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_INVS_GET_SIZE;
    request.u.invs_get_size.inv_id = inv_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_InvGetObj(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const invId = ctx.intStack[--ctx.intStackSize];
    // const slot = ctx.intStack[--ctx.intStackSize];
    // const obj = ctx.invs.get(invId)?.get(slot);
    // ctx.pushInt(obj ?? -1);

    int inv_id, slot;
    if( CS2VMX_PopInt(vm, &slot) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &inv_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_INVS_GET_OBJ;
    request.u.invs_get_obj.inv_id = inv_id;
    request.u.invs_get_obj.slot = slot;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_InvTotal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int inv_id, item_id;
    if( CS2VMX_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &inv_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_INVS_GET_TOTAL;
    request.u.invs_get_total.inv_id = inv_id;
    request.u.invs_get_total.item_id = item_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_RunOp(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int opcode,
    int operand,
    char const* str_operand)
{
    assert(vm);
    assert(frame);
    int intpop_a;
    int intpop_b;
    char* strpop_a;
    char* strpop_b;

    switch( opcode )
    {
    case CS2_OP_PUSH_VAR:
        return CS2VMX_Op_PushVar(vm, frame, operand);
    case CS2_OP_PUSH_CONSTANT_STRING:
        return CS2VMX_Op_PushConstantString(vm, frame, str_operand);
    case CS2_OP_PUSH_CONSTANT_INT:
        return CS2VMX_Op_PushConstantInt(vm, frame, operand);
    case CS2_OP_PUSH_INT_LOCAL:
        return CS2VMX_Op_PushIntLocal(vm, frame, operand);
    case CS2_OP_POP_INT_LOCAL:
        return CS2VMX_Op_PopIntLocal(vm, frame, operand);
    case CS2_OP_GOSUB_WITH_PARAMS:
        return CS2VMX_Op_GosubWithParams(vm, frame, operand);
    case CS2_OP_POP_INT_DISCARD:
        return CS2VMX_Op_PopIntDiscard(vm, frame);
    case CS2_OP_ENUM:
        return CS2VMX_Op_Enum(vm, frame, operand);
    case CS2_OP_MAP_MEMBERS:
        return CS2VMX_Op_IsMapMembers(vm, frame, operand);
    case CS2_OP_INV_SIZE:
        return CS2VMX_Op_InvSize(vm, frame, operand);
    case CS2_OP_INV_GETOBJ:
        return CS2VMX_Op_InvGetObj(vm, frame, operand);
    case CS2_OP_INV_TOTAL:
        return CS2VMX_Op_InvTotal(vm, frame, operand);
    case CS2_OP_CC_DELETEALL:
        return CS2VMX_Op_CC_DeleteAll(vm, frame);
    case CS2_OP_CC_CREATE:
        return CS2VMX_Op_CC_Create(vm, frame, operand);
    case CS2_OP_CC_SETPOSITION:
        return CS2VMX_Op_CC_SetPosition(vm, frame, operand);
    case CS2_OP_CC_SETSIZE:
        return CS2VMX_Op_CC_SetSize(vm, frame, operand);
    case CS2_OP_CC_SETGRAPHIC:
        return CS2VMX_Op_CC_SetGraphic(vm, frame, operand);
    case CS2_OP_CC_SETTILING:
        return CS2VMX_Op_CC_SetTiling(vm, frame, operand);
    case CS2_OP_CC_SETOBJECT:
    case CS2_OP_CC_SETOBJECT_ALWAYS_NUM:
    case CS2_OP_CC_SETOBJECT_NONUM:
        return CS2VMX_Op_CC_SetObject(vm, frame, operand);
    case CS2_OP_CC_SETHIDE:
        return CS2VMX_Op_CC_SetHide(vm, frame, operand);
    case CS2_OP_IF_GETWIDTH:
        return CS2VMX_Op_IF_GetWidth(vm, frame, operand);
    case CS2_OP_IF_GETHEIGHT:
        return CS2VMX_Op_IF_GetHeight(vm, frame, operand);
    case CS2_OP_IF_SETHIDE:
        return CS2VMX_Op_IF_SetHide(vm, frame, operand);
    case CS2_OP_IF_SETONVARTRANSMIT:
        return CS2VMX_Op_IF_SetOnVarTransmit(vm, frame, operand);
    case CS2_OP_IF_SETONINVTRANSMIT:
        return CS2VMX_Op_IF_SetOnInvTransmit(vm, frame, operand);
    case CS2_OP_IF_SETONOP:
        return CS2VMX_Op_IF_SetOnOp(vm, frame, operand);
    case CS2_OP_IF_SETOP:
        return CS2VMX_Op_IF_SetOp(vm, frame, operand);
    case CS2_OP_IF_CLEAROPS:
        return CS2VMX_Op_IF_ClearOps(vm, frame, operand);
    case CS2_OP_IF_SETOBJECT:
    case CS2_OP_IF_SETOBJECT_ALWAYS_NUM:
    case CS2_OP_IF_SETOBJECT_NONUM:
        return CS2VMX_Op_IF_SetObject(vm, frame, operand);
    case CS2_OP_BRANCH_LESS_THAN:
        return CS2VMX_Op_BranchLessThan(vm, frame, operand);
    case CS2_OP_BRANCH_GREATER_THAN:
        return CS2VMX_Op_BranchGreaterThan(vm, frame, operand);
    case CS2_OP_BRANCH_LESS_THAN_OR_EQUALS:
        return CS2VMX_Op_BranchLessThanOrEquals(vm, frame, operand);
    case CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS:
        return CS2VMX_Op_BranchGreaterThanOrEquals(vm, frame, operand);
    case CS2_OP_BRANCH_EQUALS:
        return CS2VMX_Op_BranchEquals(vm, frame, operand);
    case CS2_OP_BRANCH_NOT_EQUALS:
        return CS2VMX_Op_BranchNotEquals(vm, frame, operand);
    case CS2_OP_BRANCH:
        return CS2VMX_Op_Branch(vm, frame, operand);
    case CS2_OP_RETURN:
        return CS2VMX_Op_Return(vm, frame);
    case CS2_OP_ADD:
        return CS2VMX_Op_Add(vm, frame, operand);
    case CS2_OP_SUB:
        return CS2VMX_Op_Sub(vm, frame, operand);
    case CS2_OP_MULTIPLY:
        return CS2VMX_Op_Mul(vm, frame, operand);
    case CS2_OP_DIV:
        return CS2VMX_Op_Div(vm, frame, operand);
    case CS2_OP_MOD:
        return CS2VMX_Op_Mod(vm, frame, operand);
    case CS2_OP_POW:
        return CS2VMX_Op_Pow(vm, frame, operand);
    case CS2_OP_TESTBIT:
        return CS2VMX_Op_TestBit(vm, frame, operand);
    case CS2_OP_OC_PARAM:
        return CS2VMX_Op_OC_Param(vm, frame, operand);
    default:
        fprintf(stderr, "unknown opcode: %d (pc=%d)\n", opcode, frame->pc - 1);
        return CS2VM_EXECNO_OK;
    }
}

/* Fills *int_args / *str_args with the stack values this opcode pops.
 * Returns 0 for a fixed count, 1 when the count is variable (e.g. GOSUB). */
static int
CS2VMX_OpArgCounts(
    int opcode,
    int* int_args,
    int* str_args)
{
    *int_args = 0;
    *str_args = 0;
    switch( opcode )
    {
    case CS2_OP_SUB:
    case CS2_OP_MULTIPLY:
    case CS2_OP_DIV:
    case CS2_OP_MOD:
    case CS2_OP_POW:
        *int_args = 2;
        return 0;
    case CS2_OP_CC_CREATE:
        *int_args = 4;
        return 0;
    case CS2_OP_POP_INT_LOCAL:
    case CS2_OP_POP_INT_DISCARD:
        *int_args = 1;
        return 0;
    case CS2_OP_GOSUB_WITH_PARAMS:
        return 1;
    default:
        return 0;
    }
}

static void
CS2VMX_DebugPrintOpCode(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int opcode,
    int operand,
    char const* str_operand)
{
#if INTERFACEX_DEBUG_OPS
    printf("pc=%d %s (op %d)", frame->pc, CS2_OpCode_String(opcode), opcode);

    switch( cs2_opcode_operand_kind(opcode) )
    {
    case CS2_OPERAND_STRING:
        printf(" operand.str=\"%s\"", str_operand ? str_operand : "(null)");
        break;
    case CS2_OPERAND_INT8:
    case CS2_OPERAND_INT32:
        printf(" operand.int=%d", operand);
        break;
    case CS2_OPERAND_NONE:
    default:
        break;
    }
    printf("\n");

    if( opcode == CS2_OP_PUSH_INT_LOCAL || opcode == CS2_OP_POP_INT_LOCAL )
        printf("    int_local[%d] = %d\n", operand, frame->int_locals[operand]);

    int int_args = 0;
    int str_args = 0;
    if( CS2VMX_OpArgCounts(opcode, &int_args, &str_args) != 0 )
    {
        printf("    args: variable (callee signature)\n");
        return;
    }

    for( int i = 0; i < int_args; i++ )
    {
        int depth = vm->ints_stack_top - 1 - i;
        if( depth >= 0 )
            printf("    int arg[%d] (top-%d) = %d\n", i, i, vm->ints_stack[depth]);
        else
            printf("    int arg[%d] = <stack underflow>\n", i);
    }
    for( int i = 0; i < str_args; i++ )
    {
        int depth = vm->strs_stack_top - 1 - i;
        if( depth >= 0 )
        {
            printf(
                "    str arg[%d] (top-%d) = \"%s\"\n",
                i,
                i,
                vm->strs_stack[depth] ? vm->strs_stack[depth] : "(null)");
        }
        else
            printf("    str arg[%d] = <stack underflow>\n", i);
    }
#endif
}

int
CS2VMX_PushCallScript(
    struct CS2VMX* vm,
    struct CS2_Script* script)
{
    assert(vm);
    assert(vm->frame_sp < CS2VM_MAX_FRAMES);
    assert(script);

    memset(&vm->frames[vm->frame_sp], 0, sizeof(struct CS2VMX_Frame));
    vm->frames[vm->frame_sp].script = script;
    vm->frame_sp += 1;
    return CS2VM_EXECNO_OK;
}

// Called for onLoad, onOp, onClick, onVarTransmit
// Format: [scriptId, ...args]

// [0] = script ID (not a script local)
// [1..] = int/string args → $int0, $int1, … and $obj0, …
// Many onLoad listeners are just [scriptId] (count=1) → no args, param locals stay 0.
int
CS2VMX_SetActiveAndDotComponentId(
    struct CS2VMX* vm,
    int component_id)
{
    assert(vm);
    vm->active_component_id = component_id;
    vm->dot_component_id = component_id;
    return CS2VM_EXECNO_OK;
}

int
CS2VMX_RunScript(struct CS2VMX* vm)
{
    assert(vm);
    assert(vm->frame_sp > 0);

    int result;
    int cycles = 0;
    while( cycles++ < CS2VM_MAX_CYCLES )
    {
        if( vm->frame_sp <= 0 )
            return CS2VM_EXECNO_DONE;

        struct CS2VMX_Frame* frame = &vm->frames[vm->frame_sp - 1];
        if( frame->pc >= frame->script->op_count )
            return CS2VM_EXECNO_DONE;

        int opcode = frame->script->opcodes[frame->pc];
        int operand = frame->script->int_operands[frame->pc];
        char const* str_operand_str = NULL;
        if( frame->script->string_operands )
            str_operand_str = frame->script->string_operands[frame->pc];

        CS2VMX_DebugPrintOpCode(vm, frame, opcode, operand, str_operand_str);

        frame->pc += 1;

        result = CS2VMX_RunOp(vm, frame, opcode, operand, str_operand_str);

        switch( result )
        {
        case CS2VM_EXECNO_OK:
            break;
        default:
            return result;
        }
    }
    return CS2VM_EXECNO_ERROR;
}

struct MapEntry_ClientScript
{
    int id;
    struct ToriAuxLibCore_ClientScript* script;
};

#define INTERFACEX_INV_TRANSMIT_HOOK_MAX 32
#define INTERFACEX_INV_TRANSMIT_INT_ARG_MAX 16
#define INTERFACEX_INV_TRANSMIT_TRIGGER_MAX 8

#define INTERFACEX_INV_CONTAINER_MAX 8
#define INTERFACEX_INV_SLOT_MAX 2048
#define INTERFACEX_OBJ_ICON_CACHE_MAX 128
#define INTERFACEX_INV_CONTAINER_WORN 94
#define INTERFACEX_INV_CONTAINER_BACKPACK 93

struct InterfaceX_InvSlot
{
    int obj_id;
    int count;
};

struct InterfaceX_InvContainer
{
    int inv_id;
    int size;
    struct InterfaceX_InvSlot slots[INTERFACEX_INV_SLOT_MAX];
};

struct InterfaceX_ObjIconCacheEntry
{
    int obj_id;
    int* pixels;
    struct InterfaceX_ObjIconCacheEntry* next;
};

struct InterfaceX_InvTransmitHook
{
    int component_id;
    int script_id;
    int int_args[INTERFACEX_INV_TRANSMIT_INT_ARG_MAX];
    int int_arg_count;
    int trigger_ids[INTERFACEX_INV_TRANSMIT_TRIGGER_MAX];
    int trigger_count;
};

struct InterfaceX_ParamType
{
    int id;
    bool is_string;
    int default_int;
    char* default_string;
};

struct InterfaceX_ObjTypeParam
{
    int key;
    bool is_string;
    int int_value;
    char* str_value;
};

struct InterfaceX_ObjType
{
    int id;
    struct InterfaceX_ObjTypeParam* params;
    int param_count;
};

struct InterfaceX_VMHost
{
    struct ToriDraw_Map* scripts;

    unsigned char scripts_buf[4096];
    struct RSCacheDat2Disk* disk;
    struct RSCacheDat2Disk_ReferenceTable* clientscript_table;
    struct ToriDraw_Scene* scene;

    struct UITreeXBuilder* builder;
    struct UITreeX* tree;
    int interface_id;
    uint16_t next_dynamic_uid;

    struct InterfaceX_InvTransmitHook inv_transmit_hooks[INTERFACEX_INV_TRANSMIT_HOOK_MAX];
    int inv_transmit_hook_count;

    struct InterfaceX_InvContainer inv_containers[INTERFACEX_INV_CONTAINER_MAX];
    int inv_container_count;

    struct InterfaceX_ObjIconCacheEntry* obj_icon_cache;
};

struct ToriAuxLibCore_ClientScript*
InterfaceX_VMHost_ResolveScript(
    struct InterfaceX_VMHost* host,
    int script_id);

static void
CS2VMX_ResetRuntime(struct CS2VMX* vm);

static int
InterfaceX_RunClientScript(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count);

static struct InterfaceX_InvContainer*
InterfaceX_InvContainerGet(
    struct InterfaceX_VMHost* host,
    int inv_id,
    bool create);

static int const*
InterfaceX_ObjIconGet(
    struct InterfaceX_VMHost* host,
    int obj_id);

static int
InterfaceX_VMHost_Exec_CC_SetObjectOnNode(
    struct InterfaceX_VMHost* host,
    int component_id,
    int obj_id,
    int count);

static int
InterfaceX_EquipmentSlotForFile(int file_index);

static struct RSCacheDat2A_ConfigObject*
InterfaceX_LoadObjConfig(
    struct RSCacheDat2Disk* disk,
    int item_id);

static bool
InterfaceX_ConfigArchiveReady(
    struct RSCacheDat2Disk* disk,
    int config_kind);

static bool
InterfaceX_ConfigArchiveFindFile(
    struct RSCacheDat2Disk* disk,
    int config_kind,
    struct RSCacheShared_FileList* fl,
    int file_id,
    uint8_t const** out_data,
    int* out_len);

static void
InterfaceX_InvStoreSeedDefaults(struct InterfaceX_VMHost* host);

static int
InterfaceX_EnumLookup(
    struct InterfaceX_VMHost* host,
    int input_type,
    int output_type,
    int enum_id,
    int key)
{
    if( !host || !host->disk )
        return -1;
    return ie_enum_lookup(host->disk, input_type, output_type, enum_id, key);
}

static int
UITreeX_FindByUserId(
    struct UITreeX const* tree,
    int user_id)
{
    if( !tree || user_id < 0 )
        return -1;

    for( int i = 0; i < tree->node_count; i++ )
    {
        if( tree->nodes[i].user_id == user_id )
            return i;
    }
    return -1;
}

static void
UITreeX_InvalidateLayout(struct UITreeX* tree)
{
    assert(tree);
    for( int i = 0; i < tree->node_count; i++ )
        tree->nodes[i].layout_resolved = 0;
}

static int
UITreeX_DimFromParentMode(
    int8_t mode,
    int orig,
    int parent_dim)
{
    switch( mode )
    {
    case 0:
        return orig;
    case 1:
        return parent_dim - orig;
    case 2:
        return uitree_mul_shift14(parent_dim, orig);
    default:
        return orig;
    }
}

static int
UITreeX_AxisFromPositionMode(
    int8_t mode,
    int base,
    int parent_origin,
    int parent_dim,
    int self_dim)
{
    switch( mode )
    {
    case 0:
        return parent_origin + base;
    case 1:
        return parent_origin + ((parent_dim - self_dim) >> 1) + base;
    case 2:
        return parent_origin + parent_dim - base - self_dim;
    case 3:
        return parent_origin + uitree_mul_shift14(parent_dim, base);
    case 4:
        return parent_origin + ((parent_dim - self_dim) >> 1) +
               uitree_mul_shift14(parent_dim, base);
    case 5:
        return parent_origin + parent_dim - uitree_mul_shift14(parent_dim, base) - self_dim;
    default:
        return parent_origin + base;
    }
}

static void
UITreeX_ComputeSize(
    struct UITreeXNode* node,
    int parent_w,
    int parent_h,
    int* out_w,
    int* out_h)
{
    int aspect_w = node->aspect_w > 0 ? node->aspect_w : 1;
    int aspect_h = node->aspect_h > 0 ? node->aspect_h : 1;
    int w = UITreeX_DimFromParentMode(node->w_mode, node->w, parent_w);
    int h = UITreeX_DimFromParentMode(node->h_mode, node->h, parent_h);

    if( node->w_mode == 4 )
        w = aspect_w * h / aspect_h;
    if( node->h_mode == 4 )
        h = aspect_h * w / aspect_w;

    if( w < 0 )
        w = 0;
    if( h < 0 )
        h = 0;
    *out_w = w;
    *out_h = h;
}

static void
UITreeX_LayoutNode(
    struct UITreeX* tree,
    int node_idx,
    int parent_x,
    int parent_y,
    int parent_w,
    int parent_h,
    int is_root)
{
    assert(tree);
    assert(node_idx >= 0 && node_idx < tree->node_count);

    struct UITreeXNode* node = &tree->nodes[node_idx];
    int w = 0;
    int h = 0;

    if( node->if3 )
    {
        UITreeX_ComputeSize(node, parent_w, parent_h, &w, &h);
        node->abs_x = UITreeX_AxisFromPositionMode(node->x_mode, node->x, parent_x, parent_w, w);
        node->abs_y = UITreeX_AxisFromPositionMode(node->y_mode, node->y, parent_y, parent_h, h);
    }
    else
    {
        w = node->w;
        h = node->h;
        node->abs_x = parent_x + node->x;
        node->abs_y = parent_y + node->y;
    }

    if( is_root && w == 0 && h == 0 )
    {
        w = parent_w;
        h = parent_h;
    }

    node->abs_w = w;
    node->abs_h = h;
    node->layout_resolved = 1;

    int child_pw = w;
    int child_ph = h;
    if( node->kind == UITreeXNodeKind_RSLayer )
    {
        if( node->u.rs_layer.scroll_width > 0 )
            child_pw = node->u.rs_layer.scroll_width;
        if( node->u.rs_layer.scroll_height > 0 )
            child_ph = node->u.rs_layer.scroll_height;
    }

    for( int child = node->link.first_child_tree_idx; child != -1;
         child = tree->nodes[child].link.next_sibling_tree_idx )
        UITreeX_LayoutNode(tree, child, node->abs_x, node->abs_y, child_pw, child_ph, 0);
}

static void
UITreeX_LayoutResolve(
    struct UITreeX* tree,
    int root_w,
    int root_h)
{
    assert(tree);

    UITreeX_InvalidateLayout(tree);

    for( int i = 0; i < tree->node_count; i++ )
    {
        if( tree->nodes[i].link.parent_tree_idx == -1 )
            UITreeX_LayoutNode(tree, i, 0, 0, root_w, root_h, 1);
    }
}

static void
CS2VMX_InvalidateComponentIfGone(
    struct CS2VMX* vm,
    struct UITreeX* tree,
    int* component_id)
{
    assert(vm);
    assert(tree);
    assert(component_id);

    if( *component_id < 0 )
        return;

    if( UITreeX_FindByUserId(tree, *component_id) < 0 )
        *component_id = -1;
}

static int
UITreeX_GetLayoutWidth(
    struct UITreeX* tree,
    int user_id)
{
    assert(tree);

    UITreeX_LayoutResolve(tree, CANVAS_W, CANVAS_H);

    int idx = UITreeX_FindByUserId(tree, user_id);
    if( idx < 0 )
        return CANVAS_W;

    struct UITreeXNode* node = &tree->nodes[idx];
    if( node->layout_resolved && node->abs_w > 0 )
        return node->abs_w;
    return node->w > 0 ? node->w : CANVAS_W;
}

static int
UITreeX_GetLayoutHeight(
    struct UITreeX* tree,
    int user_id)
{
    assert(tree);

    UITreeX_LayoutResolve(tree, CANVAS_W, CANVAS_H);

    int idx = UITreeX_FindByUserId(tree, user_id);
    if( idx < 0 )
        return CANVAS_H;

    struct UITreeXNode* node = &tree->nodes[idx];
    if( node->layout_resolved && node->abs_h > 0 )
        return node->abs_h;
    return node->h > 0 ? node->h : CANVAS_H;
}

static struct UITreeXNode*
UITreeX_NodeByComponentId(
    struct InterfaceX_VMHost* host,
    int component_id)
{
    if( !host || !host->tree || component_id < 0 )
        return NULL;

    int idx = UITreeX_FindByUserId(host->tree, component_id);
    if( idx < 0 )
        return NULL;
    return &host->tree->nodes[idx];
}

static int
InterfaceX_VMHost_AllocateDynamicUid(struct InterfaceX_VMHost* host)
{
    assert(host);

    uint16_t next = host->next_dynamic_uid;
    if( next < 0x8000u )
        next = 0x8000u;

    for( int i = 0; i < 0x8000; i++ )
    {
        uint16_t child_id = next;
        int uid = (host->interface_id << 16) | (int)child_id;
        next = (uint16_t)((child_id + 1u) & 0xffffu);
        if( next < 0x8000u )
            next = 0x8000u;
        if( UITreeX_FindByUserId(host->tree, uid) < 0 )
        {
            host->next_dynamic_uid = next;
            return uid;
        }
    }
    return -1;
}

static void
UITreeX_UnlinkChild(
    struct UITreeX* tree,
    int parent_idx,
    int child_idx)
{
    assert(tree);
    assert(parent_idx >= 0 && parent_idx < tree->node_count);
    assert(child_idx >= 0 && child_idx < tree->node_count);

    struct UITreeXNode* parent = &tree->nodes[parent_idx];
    struct UITreeXNode* child = &tree->nodes[child_idx];
    int prev = -1;

    for( int cur = parent->link.first_child_tree_idx; cur != -1;
         cur = tree->nodes[cur].link.next_sibling_tree_idx )
    {
        if( cur == child_idx )
        {
            if( prev == -1 )
                parent->link.first_child_tree_idx = child->link.next_sibling_tree_idx;
            else
                tree->nodes[prev].link.next_sibling_tree_idx = child->link.next_sibling_tree_idx;

            if( parent->link.last_child_tree_idx == child_idx )
                parent->link.last_child_tree_idx = prev;

            child->link.parent_tree_idx = -1;
            child->link.next_sibling_tree_idx = -1;
            return;
        }
        prev = cur;
    }
}

static int
UITreeX_FindDynamicChild(
    struct UITreeX* tree,
    int parent_idx,
    int child_index)
{
    if( !tree || parent_idx < 0 )
        return -1;

    for( int child = tree->nodes[parent_idx].link.first_child_tree_idx; child != -1;
         child = tree->nodes[child].link.next_sibling_tree_idx )
    {
        if( tree->nodes[child].dynamic && tree->nodes[child].child_index == child_index )
            return child;
    }
    return -1;
}

static void
UITreeX_DeleteDynamicChildren(
    struct UITreeX* tree,
    int parent_user_id)
{
    int parent_idx = UITreeX_FindByUserId(tree, parent_user_id);
    if( parent_idx < 0 )
        return;

    for( ;; )
    {
        int victim = -1;
        for( int child = tree->nodes[parent_idx].link.first_child_tree_idx; child != -1;
             child = tree->nodes[child].link.next_sibling_tree_idx )
        {
            if( tree->nodes[child].dynamic )
            {
                victim = child;
                break;
            }
        }
        if( victim < 0 )
            break;
        UITreeX_UnlinkChild(tree, parent_idx, victim);
        tree->nodes[victim].user_id = -1;
    }
}

static void
UITreeX_ApplyComponentGeometry(
    struct UITreeXNode* node,
    struct ToriAuxLibCore_Component const* component)
{
    assert(node);
    assert(component);

    node->if3 = component->if3 ? 1 : 0;
    node->hidden = component->hide ? 1 : 0;
    node->tiling = component->tiled ? 1 : 0;
    node->aspect_w = component->aspect_w > 0 ? component->aspect_w : 1;
    node->aspect_h = component->aspect_h > 0 ? component->aspect_h : 1;

    if( node->kind == UITreeXNodeKind_RSLayer )
    {
        node->u.rs_layer.scroll_width = component->scroll_width;
        node->u.rs_layer.scroll_height = component->scroll_height;
    }

    if( node->if3 )
    {
        node->x = component->base_x;
        node->y = component->base_y;
        node->w = component->base_width;
        node->h = component->base_height;
        node->x_mode = component->x_mode;
        node->y_mode = component->y_mode;
        node->w_mode = component->width_mode;
        node->h_mode = component->height_mode;
    }
    else
    {
        node->x = component->base_x;
        node->y = component->base_y;
        node->w = component->width;
        node->h = component->height;
        node->x_mode = 0;
        node->y_mode = 0;
        node->w_mode = 0;
        node->h_mode = 0;
    }
}

static void
blit_rgba_pixel(
    int* dest,
    int dstride,
    int sx,
    int sy,
    int p)
{
    if( sx < 0 || sy < 0 || sx >= CANVAS_W || sy >= CANVAS_H )
        return;

    int a = (p >> 24) & 0xFF;
    if( a == 0 )
        return;

    if( a == 255 )
    {
        dest[sy * dstride + sx] = (p & 0x00FFFFFF) | 0xFF000000;
        return;
    }

    int d = dest[sy * dstride + sx];
    int dr = (d >> 16) & 0xFF;
    int dg = (d >> 8) & 0xFF;
    int db = d & 0xFF;
    int sr = (p >> 16) & 0xFF;
    int sg = (p >> 8) & 0xFF;
    int sb = p & 0xFF;
    int rr = (sr * a + dr * (255 - a)) / 255;
    int rg = (sg * a + dg * (255 - a)) / 255;
    int rb = (sb * a + db * (255 - a)) / 255;
    dest[sy * dstride + sx] = 0xFF000000 | (rr << 16) | (rg << 8) | rb;
}

static void
blit_rgba_sprite(
    int* dest,
    int dstride,
    int dx,
    int dy,
    int const* spr,
    int sw,
    int sh)
{
    for( int y = 0; y < sh; y++ )
    {
        int sy = dy + y;
        for( int x = 0; x < sw; x++ )
            blit_rgba_pixel(dest, dstride, dx + x, sy, spr[y * sw + x]);
    }
}

static void
blit_rgba_sprite_tiled(
    int* dest,
    int dstride,
    int rect_x,
    int rect_y,
    int rect_w,
    int rect_h,
    int const* spr,
    int sw,
    int sh,
    int origin_x,
    int origin_y)
{
    if( sw <= 0 || sh <= 0 || rect_w <= 0 || rect_h <= 0 )
        return;

    int x0 = rect_x < 0 ? 0 : rect_x;
    int y0 = rect_y < 0 ? 0 : rect_y;
    int x1 = rect_x + rect_w;
    int y1 = rect_y + rect_h;
    if( x1 > CANVAS_W )
        x1 = CANVAS_W;
    if( y1 > CANVAS_H )
        y1 = CANVAS_H;

    for( int y = y0; y < y1; y++ )
    {
        int sy = y - origin_y;
        sy = ((sy % sh) + sh) % sh;
        for( int x = x0; x < x1; x++ )
        {
            int sx = x - origin_x;
            sx = ((sx % sw) + sw) % sw;
            blit_rgba_pixel(dest, dstride, x, y, spr[sy * sw + sx]);
        }
    }
}

static void
InterfaceX_BlitGraphic(
    struct RSCacheDat2Disk* cache,
    int* pixels,
    int graphic_id,
    int dx,
    int dy,
    int lw,
    int lh,
    int tiling)
{
    if( !cache || graphic_id < 0 )
        return;

    struct RSCacheDat2A_SpritePack* pack = RSCacheDat2A_SpritePackNewFromCache(cache, graphic_id);
    if( !pack || pack->count <= 0 || !pack->palette )
    {
        if( pack )
            RSCacheDat2A_SpritePackFree(pack);
        return;
    }

    int* spr_px = RSCacheDat2A_SpriteGetPixels(&pack->sprites[0], pack->palette, 0);
    int sw = pack->sprites[0].width;
    int sh = pack->sprites[0].height;
    int ox = pack->sprites[0].offset_x;
    int oy = pack->sprites[0].offset_y;
    RSCacheDat2A_SpritePackFree(pack);
    if( !spr_px )
        return;

    int blit_x = dx + ox;
    int blit_y = dy + oy;
    if( tiling )
        blit_rgba_sprite_tiled(pixels, CANVAS_W, dx, dy, lw, lh, spr_px, sw, sh, dx, dy);
    else
        blit_rgba_sprite(pixels, CANVAS_W, blit_x, blit_y, spr_px, sw, sh);
    free(spr_px);
}

static void
UITreeX_RenderNode(
    struct InterfaceX_VMHost* host,
    struct RSCacheDat2Disk* cache,
    struct UITreeX const* tree,
    int node_idx,
    int* pixels)
{
    assert(tree);
    assert(node_idx >= 0 && node_idx < tree->node_count);

    struct UITreeXNode const* node = &tree->nodes[node_idx];
    if( node->hidden )
        goto render_children;

    if( node->kind == UITreeXNodeKind_RSGraphic && node->u.rs_graphic.graphic_id >= 0 )
    {
        InterfaceX_BlitGraphic(
            cache,
            pixels,
            node->u.rs_graphic.graphic_id,
            node->abs_x,
            node->abs_y,
            node->abs_w > 0 ? node->abs_w : CANVAS_W,
            node->abs_h > 0 ? node->abs_h : CANVAS_H,
            node->tiling);
    }
    else if( node->kind == UITreeXNodeKind_RSObj && node->u.rs_obj.obj_id > 0 && host )
    {
        int const* icon = InterfaceX_ObjIconGet(host, node->u.rs_obj.obj_id);
        if( icon )
        {
            int icon_size = 32;
            int bw = node->abs_w > 0 ? node->abs_w : icon_size;
            int bh = node->abs_h > 0 ? node->abs_h : icon_size;
            if( icon_size > bw )
                icon_size = bw;
            if( icon_size > bh )
                icon_size = bh;
            if( icon_size < 8 )
                icon_size = 8;
            int ix = node->abs_x + (bw - icon_size) / 2;
            int iy = node->abs_y + (bh - icon_size) / 2;
            blit_rgba_sprite(pixels, CANVAS_W, ix, iy, icon, icon_size, icon_size);
        }
    }

render_children:
    for( int child = node->link.first_child_tree_idx; child != -1;
         child = tree->nodes[child].link.next_sibling_tree_idx )
        UITreeX_RenderNode(host, cache, tree, child, pixels);
}

static void
UITreeX_Render(
    struct InterfaceX_VMHost* host,
    struct RSCacheDat2Disk* cache,
    struct UITreeX* tree,
    int* pixels)
{
    assert(cache);
    assert(tree);
    assert(pixels);

    UITreeX_LayoutResolve(tree, CANVAS_W, CANVAS_H);

    for( int i = 0; i < tree->node_count; i++ )
    {
        if( tree->nodes[i].link.parent_tree_idx == -1 )
            UITreeX_RenderNode(host, cache, tree, i, pixels);
    }
}

static RSCacheDat2A_Component*
component_decode_from_bytes(
    int packed_id,
    char* data,
    int size)
{
    if( !data || size <= 0 )
        return NULL;

    RSCacheDat2A_Component* comp = calloc(1, sizeof(RSCacheDat2A_Component));
    if( !comp )
        return NULL;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, size);
    RSCacheDat2A_ComponentInit(comp);
    comp->id = packed_id;
    if( (unsigned char)data[0] == (unsigned char)255 )
        RSCacheDat2A_ComponentDecodeIf3(comp, &buf);
    else
        RSCacheDat2A_ComponentDecodeIf1(comp, &buf);
    return comp;
}

static int
process_component(
    struct UITreeXBuilder* builder,
    struct ToriAuxLibCore_Component* component)
{
    assert(builder);
    assert(component);

    int layer = component->parent_id;
    int idx = 0;

    switch( component->type )
    {
    case TORIAUXLIBCORE_COMPONENT_LAYER:
        idx = UITreeXBuilder_PushLayerWithParentUserId(builder, component->id, layer);
        break;
    case TORIAUXLIBCORE_COMPONENT_GRAPHIC:
        idx = UITreeXBuilder_PushGraphicWithParentUserId(
            builder, component->id, component->graphic, layer);
        break;
    default:
        idx = UITreeXBuilder_PushLayerWithParentUserId(builder, component->id, layer);
        break;
    }

    if( idx >= 0 && idx < builder->tree->node_count )
        UITreeX_ApplyComponentGeometry(&builder->tree->nodes[idx], component);

    return idx;
}

#define HOST_SCRIPT_MAP_CAP 64

static struct InterfaceX_InvContainer*
InterfaceX_InvContainerGet(
    struct InterfaceX_VMHost* host,
    int inv_id,
    bool create)
{
    assert(host);
    assert(inv_id >= 0);

    for( int i = 0; i < host->inv_container_count; i++ )
    {
        if( host->inv_containers[i].inv_id == inv_id )
            return &host->inv_containers[i];
    }

    if( !create || host->inv_container_count >= INTERFACEX_INV_CONTAINER_MAX )
        return NULL;

    struct InterfaceX_InvContainer* container =
        &host->inv_containers[host->inv_container_count++];
    memset(container, 0, sizeof(*container));
    container->inv_id = inv_id;
    if( inv_id == INTERFACEX_INV_CONTAINER_WORN )
        container->size = 14;
    else if( inv_id == INTERFACEX_INV_CONTAINER_BACKPACK )
        container->size = 28;
    else
        container->size = 28;
    return container;
}

static void
InterfaceX_InvContainerSetSlot(
    struct InterfaceX_InvContainer* container,
    int slot,
    int obj_id,
    int count)
{
    if( !container || slot < 0 || slot >= container->size || slot >= INTERFACEX_INV_SLOT_MAX )
        return;
    container->slots[slot].obj_id = obj_id > 0 ? obj_id : -1;
    container->slots[slot].count = obj_id > 0 ? (count > 0 ? count : 1) : 0;
}

static int
InterfaceX_EquipmentSlotForFile(int file_index)
{
    static int const k_slot_files[] = { 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25 };
    for( int i = 0; i < (int)(sizeof(k_slot_files) / sizeof(k_slot_files[0])); i++ )
    {
        if( k_slot_files[i] == file_index )
            return i;
    }
    return -1;
}

static struct RSCacheDat2A_ConfigObject*
InterfaceX_LoadObjConfig(
    struct RSCacheDat2Disk* disk,
    int item_id)
{
    if( !disk || item_id <= 0 )
        return NULL;

    if( !InterfaceX_ConfigArchiveReady(disk, RSCacheDat2A_ConfigKind_Object) )
        return NULL;

    struct RSCacheDat2Disk_Archive* arch = RSCacheDat2Disk_ArchiveNewLoad(
        disk, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Object);
    if( !arch )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadata(disk, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl )
    {
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    uint8_t const* data = NULL;
    int data_len = 0;
    if( !InterfaceX_ConfigArchiveFindFile(
            disk, RSCacheDat2A_ConfigKind_Object, fl, item_id, &data, &data_len) )
    {
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    struct RSCacheDat2A_ConfigObject* decoded = calloc(1, sizeof(struct RSCacheDat2A_ConfigObject));
    if( !decoded )
    {
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    RSCacheDat2A_ConfigObjectDecodeInplace(decoded, (char*)data, data_len);
    decoded->_id = item_id;

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return decoded;
}

static void
InterfaceX_InvStoreSeedDefaults(struct InterfaceX_VMHost* host)
{
    if( !host )
        return;

    struct InterfaceX_InvContainer* worn =
        InterfaceX_InvContainerGet(host, INTERFACEX_INV_CONTAINER_WORN, true);
    struct InterfaceX_InvContainer* backpack =
        InterfaceX_InvContainerGet(host, INTERFACEX_INV_CONTAINER_BACKPACK, true);
    if( !worn || !backpack )
        return;

    for( int slot = 0; slot < worn->size; slot++ )
        InterfaceX_InvContainerSetSlot(worn, slot, -1, 0);
    for( int slot = 0; slot < backpack->size; slot++ )
        InterfaceX_InvContainerSetSlot(backpack, slot, -1, 0);

    struct
    {
        int file_index;
        int obj_id;
    } const k_worn_items[] = {
        { 15, 1153 },
        { 18, 1333 },
        { 19, 1115 },
        { 21, 1189 },
        { 23, 1067 },
    };

    for( int i = 0; i < (int)(sizeof(k_worn_items) / sizeof(k_worn_items[0])); i++ )
    {
        int slot = InterfaceX_EquipmentSlotForFile(k_worn_items[i].file_index);
        if( slot >= 0 )
            InterfaceX_InvContainerSetSlot(worn, slot, k_worn_items[i].obj_id, 1);
    }
}

static int*
InterfaceX_RasterizeObjIcon(
    struct InterfaceX_VMHost* host,
    struct RSCacheDat2A_ConfigObject* obj)
{
    if( !host || !host->disk || !host->scene || !obj || obj->inventory_model_id <= 0 )
        return NULL;

    struct RSCacheDat2A_Model* raw =
        RSCacheDat2A_ModelNewFromCache(host->disk, obj->inventory_model_id);
    if( !raw )
        return NULL;

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(raw);
    RSCacheDat2A_ModelFree(raw);
    if( !copy )
        return NULL;

    if( copy->face_colors && obj->recolor_count > 0 )
    {
        for( int i = 0; i < obj->recolor_count; i++ )
        {
            int color_src = obj->recolors_from[i];
            int color_dst = obj->recolors_to[i];
            for( int f = 0; f < copy->face_count; f++ )
            {
                if( copy->face_colors[f] == (uint16_t)color_src )
                    copy->face_colors[f] = (uint16_t)color_dst;
            }
        }
    }

    struct ToriDraw_Model* td_model = ToriDraw_ModelNewFromCacheModel(copy);
    RSCacheDat2A_ModelFree(copy);
    if( !td_model )
        return NULL;

    ToriDraw_ModelSetBoundsCylinder(td_model);

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td_model,
    };
    ToriDraw_LightModelDefault(hnd, obj->contrast, obj->ambient);

    int zoom = obj->zoom2d;
    if( zoom == 0 )
        zoom = 2000;

    struct ToriDraw_Sprite* spr = ToriDraw_SpriteNewFromModelRaster(
        host->scene, hnd, zoom, obj->xan2d, obj->yan2d, 32, 32, true);
    if( !spr || !spr->pixels_argb )
    {
        ToriDraw_SceneFrameEnd(host->scene);
        ToriDraw_SpriteFree(spr);
        return NULL;
    }

    int* pixels = malloc(32 * 32 * sizeof(int));
    if( !pixels )
    {
        ToriDraw_SceneFrameEnd(host->scene);
        ToriDraw_SpriteFree(spr);
        return NULL;
    }
    memcpy(pixels, spr->pixels_argb, 32 * 32 * sizeof(int));
    ToriDraw_SpriteFree(spr);
    ToriDraw_SceneFrameEnd(host->scene);
    return pixels;
}

static int const*
InterfaceX_ObjIconGet(
    struct InterfaceX_VMHost* host,
    int obj_id)
{
    if( !host || obj_id <= 0 )
        return NULL;

    for( struct InterfaceX_ObjIconCacheEntry* it = host->obj_icon_cache; it; it = it->next )
    {
        if( it->obj_id == obj_id )
            return it->pixels;
    }

    struct RSCacheDat2A_ConfigObject* obj = InterfaceX_LoadObjConfig(host->disk, obj_id);
    if( !obj )
        return NULL;

    int* pixels = InterfaceX_RasterizeObjIcon(host, obj);
    RSCacheDat2A_ConfigObjectFree(obj);
    if( !pixels )
        return NULL;

    struct InterfaceX_ObjIconCacheEntry* entry =
        calloc(1, sizeof(struct InterfaceX_ObjIconCacheEntry));
    if( !entry )
    {
        free(pixels);
        return NULL;
    }
    entry->obj_id = obj_id;
    entry->pixels = pixels;
    entry->next = host->obj_icon_cache;
    host->obj_icon_cache = entry;
    return entry->pixels;
}

static int
InterfaceX_VMHost_Exec_CC_SetObjectOnNode(
    struct InterfaceX_VMHost* host,
    int component_id,
    int obj_id,
    int count)
{
    assert(host);

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    if( obj_id <= 0 )
    {
        node->u.rs_obj.obj_id = 0;
        node->u.rs_obj.obj_count = 0;
        return CS2VM_EXECNO_OK;
    }

    if( node->kind != UITreeXNodeKind_RSObj && node->kind != UITreeXNodeKind_RSGraphic )
        node->kind = UITreeXNodeKind_RSObj;
    node->u.rs_obj.obj_id = obj_id;
    node->u.rs_obj.obj_count = count > 0 ? count : 1;
    (void)InterfaceX_ObjIconGet(host, obj_id);
    return CS2VM_EXECNO_OK;
}

static bool
InterfaceX_RuntimeHookOwnsComponent(
    struct InterfaceX_VMHost const* host,
    int component_id)
{
    if( !host )
        return false;
    for( int i = 0; i < host->inv_transmit_hook_count; i++ )
    {
        if( host->inv_transmit_hooks[i].component_id == component_id )
            return true;
    }
    return false;
}

static void
InterfaceX_SetHookIntLocal(
    struct CS2VMX* vm,
    int component_id,
    int local_idx,
    int argi)
{
    switch( argi )
    {
    case CS2VM_SCRIPT_ARG_WIDGET_ID:
        CS2VMX_SetIntCurrentFrameLocal(vm, local_idx, component_id);
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
        break;
    default:
        CS2VMX_SetIntCurrentFrameLocal(vm, local_idx, argi);
        break;
    }
}

static int
InterfaceX_RunClientScript(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count)
{
    assert(host);
    assert(vm);

    if( script_id <= 0 )
        return CS2VM_EXECNO_OK;

    struct ToriAuxLibCore_ClientScript* client_script =
        InterfaceX_VMHost_ResolveScript(host, script_id);
    if( !client_script )
    {
        fprintf(stderr, "failed to resolve script: %d\n", script_id);
        return CS2VM_EXECNO_ERROR;
    }

    CS2VMX_ResetRuntime(vm);
    CS2VMX_PushCallScript(vm, &client_script->script);
    for( int j = 0; j < int_arg_count; j++ )
        InterfaceX_SetHookIntLocal(vm, component_id, j, int_args[j]);

    CS2VMX_SetActiveAndDotComponentId(vm, component_id);

    int res;
    while( (res = CS2VMX_RunScript(vm)) )
    {
        switch( res )
        {
        case CS2VM_EXECNO_DONE:
            return CS2VM_EXECNO_OK;
        case CS2VM_EXECNO_YIELD:
            break;
        case CS2VM_EXECNO_ERROR:
            fprintf(stderr, "error running script %d (continuing)\n", script_id);
            CS2VMX_ResetRuntime(vm);
            return CS2VM_EXECNO_ERROR;
        }
    }
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Init(struct InterfaceX_VMHost* host)
{
    assert(host);

    int entry_size = (int)sizeof(struct MapEntry_ClientScript);
    int buffer_size = ToriDraw_MapBufferSizeFor(entry_size, HOST_SCRIPT_MAP_CAP);
    if( buffer_size > (int)sizeof(host->scripts_buf) )
        return false;

    struct ToriDraw_MapConfig config = {
        .buffer = host->scripts_buf,
        .buffer_size = (size_t)buffer_size,
        .key_size = sizeof(int),
        .entry_size = (size_t)entry_size,
        .capacity = HOST_SCRIPT_MAP_CAP,
    };

    host->scripts = ToriDraw_MapNew(&config, 0);
    return true;
}

struct ToriAuxLibCore_ClientScript*
InterfaceX_VMHost_ResolveScript(
    struct InterfaceX_VMHost* host,
    int script_id)
{
    assert(host);
    assert(script_id >= 0);

    struct MapEntry_ClientScript* entry =
        ToriDraw_MapSearch(host->scripts, &script_id, TORIDRAW_MAP_FIND);

    struct RSCacheDat2Disk_Archive* archive =
        RSCacheDat2Disk_ArchiveNewLoad(host->disk, RSCacheDat2Disk_Table_Clientscript, script_id);
    if( !archive )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadataFromTable(host->clientscript_table, archive);

    struct ToriAuxLibCore_ClientScript* loaded = ToriAuxLibCache_ClientScriptNewFromDat2Archive2(
        archive, script_id, CLIENTSCRIPT_DECODE_TRAILER_LEGACY);
    assert(loaded);

    entry = ToriDraw_MapSearch(host->scripts, &script_id, TORIDRAW_MAP_INSERT);
    assert(entry);

    entry->id = script_id;
    entry->script = loaded;

    return loaded;
}

int
InterfaceX_VMHost_Exec_PushScript(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int script_id)
{
    assert(host);
    assert(host->builder);
    struct ToriAuxLibCore_ClientScript* cs;

    cs = InterfaceX_VMHost_ResolveScript(host, script_id);
    assert(cs);

    return CS2VMX_PushCallScript(vm, &cs->script);
}

int
InterfaceX_VMHost_Exec_InvSize(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int inv_id)
{
    assert(host);
    assert(host->builder);

    struct InterfaceX_InvContainer* container = InterfaceX_InvContainerGet(host, inv_id, false);
    int size = container ? container->size : 0;
    if( size <= 0 )
    {
        if( inv_id == INTERFACEX_INV_CONTAINER_WORN )
            size = 14;
        else if( inv_id == INTERFACEX_INV_CONTAINER_BACKPACK )
            size = 28;
    }

    CS2VMX_PushInt(vm, size);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_InvGetObj(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_InvGetObj request)
{
    assert(host);
    assert(host->builder);

    struct InterfaceX_InvContainer* container =
        InterfaceX_InvContainerGet(host, request.inv_id, false);
    int obj_id = -1;
    if( container && request.slot >= 0 && request.slot < container->size )
        obj_id = container->slots[request.slot].obj_id;

    CS2VMX_PushInt(vm, obj_id);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_InvTotal(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_InvTotal request)
{
    assert(host);
    assert(host->builder);

    struct InterfaceX_InvContainer* container =
        InterfaceX_InvContainerGet(host, request.inv_id, false);
    int total = 0;
    if( container && request.item_id > 0 )
    {
        for( int slot = 0; slot < container->size; slot++ )
        {
            if( container->slots[slot].obj_id == request.item_id )
                total += container->slots[slot].count > 0 ? container->slots[slot].count : 1;
        }
    }

    CS2VMX_PushInt(vm, total);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_VarsReadVarp(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_VarsReadVarp request)
{
    assert(host);
    assert(host->builder);

    int varp_id = request.varp_id;

#define DUMMY_VARP_VALUE 123
    CS2VMX_PushInt(vm, DUMMY_VARP_VALUE);

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_GetWidth(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(host->builder);

    CS2VMX_PushInt(vm, UITreeX_GetLayoutWidth(host->tree, component_id));
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_GetHeight(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(host->builder);

    CS2VMX_PushInt(vm, UITreeX_GetLayoutHeight(host->tree, component_id));
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetHide(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetHide request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( node )
    {
        node->hidden = request.hidden ? 1 : 0;
        UITreeX_InvalidateLayout(host->tree);
    }

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOnInvTransmit(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOnInvTransmit request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request.signature;

    if( host->inv_transmit_hook_count >= INTERFACEX_INV_TRANSMIT_HOOK_MAX )
        return CS2VM_EXECNO_OK;

    struct InterfaceX_InvTransmitHook* hook =
        &host->inv_transmit_hooks[host->inv_transmit_hook_count++];

    hook->component_id = request.component_id;
    hook->script_id = request.script_id;
    hook->int_arg_count = request.int_arg_count;
    if( hook->int_arg_count > INTERFACEX_INV_TRANSMIT_INT_ARG_MAX )
        hook->int_arg_count = INTERFACEX_INV_TRANSMIT_INT_ARG_MAX;
    memcpy(hook->int_args, request.int_args, sizeof(hook->int_args));

    hook->trigger_count = request.trigger_count;
    if( hook->trigger_count > INTERFACEX_INV_TRANSMIT_TRIGGER_MAX )
        hook->trigger_count = INTERFACEX_INV_TRANSMIT_TRIGGER_MAX;
    if( request.trigger_ids && hook->trigger_count > 0 )
    {
        memcpy(
            hook->trigger_ids,
            request.trigger_ids,
            (size_t)hook->trigger_count * sizeof(hook->trigger_ids[0]));
    }
    else
    {
        hook->trigger_count = 0;
    }

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOnVarTransmit(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOnVarTransmit request)
{
    assert(host);
    assert(host->builder);

    int component_id = request.component_id;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOnOp(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOp(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOp request)
{
    assert(host);
    assert(host->builder);

    int component_id = request.component_id;
    int index = request.index;
    char* text = request.text;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_ClearOps(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_ClearOps request)
{
    assert(host);
    assert(host->builder);

    int component_id = request.component_id;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_DeleteAll(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(host->builder);
    assert(vm);

    UITreeX_DeleteDynamicChildren(host->tree, component_id);
    CS2VMX_InvalidateComponentIfGone(vm, host->tree, &vm->active_component_id);
    CS2VMX_InvalidateComponentIfGone(vm, host->tree, &vm->dot_component_id);
    UITreeX_InvalidateLayout(host->tree);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_Create(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_Create request)
{
    assert(host);
    assert(host->builder);
    assert(host->tree);

    int parent_id = request.parent_id;
    int type = request.component_type;
    int child_index = request.child_index;
    (void)request.is_nested;

    int parent_idx = UITreeX_FindByUserId(host->tree, parent_id);
    if( parent_idx < 0 )
        return CS2VM_EXECNO_ERROR;

    int existing = UITreeX_FindDynamicChild(host->tree, parent_idx, child_index);
    if( existing >= 0 )
        UITreeX_UnlinkChild(host->tree, parent_idx, existing);

    struct UITreeXNode* node = UITreeX_NodeEmplace(host->tree);
    node->user_id = InterfaceX_VMHost_AllocateDynamicUid(host);
    if( node->user_id < 0 )
        return CS2VM_EXECNO_ERROR;

    node->dynamic = 1;
    node->child_index = child_index;
    node->if3 = 1;
    node->aspect_w = 1;
    node->aspect_h = 1;

    switch( type )
    {
    case 5:
        node->kind = UITreeXNodeKind_RSGraphic;
        node->u.rs_graphic.graphic_id = -1;
        break;
    case 0:
        node->kind = UITreeXNodeKind_RSLayer;
        break;
    default:
        node->kind = UITreeXNodeKind_RSObj;
        node->u.rs_obj.obj_id = 0;
        node->u.rs_obj.obj_count = 0;
        break;
    }

    node->link.parent_tree_idx = parent_idx;
    int last = host->tree->nodes[parent_idx].link.last_child_tree_idx;
    if( last == -1 )
        host->tree->nodes[parent_idx].link.first_child_tree_idx = node->idx;
    else
        host->tree->nodes[last].link.next_sibling_tree_idx = node->idx;
    host->tree->nodes[parent_idx].link.last_child_tree_idx = node->idx;

    if( request.dot_operand == 1 )
        vm->dot_component_id = node->user_id;
    else
        vm->active_component_id = node->user_id;

#if INTERFACEX_DEBUG_OPS
    fprintf(
        stderr,
        "CC_CREATE: child=0x%x parent=0x%x type=%d sub=%d active=0x%x dot=0x%x\n",
        (unsigned)node->user_id,
        (unsigned)parent_id,
        type,
        child_index,
        (unsigned)vm->active_component_id,
        (unsigned)vm->dot_component_id);
#endif

    UITreeX_InvalidateLayout(host->tree);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetPosition(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetPosition request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
#if INTERFACEX_DEBUG_OPS
    fprintf(
        stderr,
        "CC_SETPOSITION: component=0x%x x=%d y=%d xm=%d ym=%d %s\n",
        (unsigned)request.component_id,
        request.x,
        request.y,
        request.xmode,
        request.ymode,
        node ? "ok" : "MISS (active/dot stale?)");
#endif
    if( !node )
        return CS2VM_EXECNO_OK;

    node->x = request.x;
    node->y = request.y;
    node->x_mode = (int8_t)request.xmode;
    node->y_mode = (int8_t)request.ymode;
    node->if3 = 1;
    UITreeX_InvalidateLayout(host->tree);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetSize(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetSize request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    node->w = request.width;
    node->h = request.height;
    node->w_mode = (int8_t)request.wmode;
    node->h_mode = (int8_t)request.hmode;
    node->if3 = 1;
    UITreeX_InvalidateLayout(host->tree);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetGraphic(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetGraphic request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    if( node->kind != UITreeXNodeKind_RSGraphic )
        node->kind = UITreeXNodeKind_RSGraphic;

    node->u.rs_graphic.graphic_id = request.graphic_id;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetObject(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetObject request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    return InterfaceX_VMHost_Exec_CC_SetObjectOnNode(
        host, request.component_id, request.obj_id, request.count);
}

int
InterfaceX_VMHost_Exec_IF_SetObject(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetObject request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    return InterfaceX_VMHost_Exec_CC_SetObjectOnNode(
        host, request.component_id, request.obj_id, request.count);
}

int
InterfaceX_VMHost_Exec_CC_SetTiling(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetTiling request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( node )
        node->tiling = request.tiling != 0;

    return CS2VM_EXECNO_OK;
}

static bool
InterfaceX_ConfigArchiveReady(
    struct RSCacheDat2Disk* disk,
    int config_kind)
{
    if( !disk || config_kind < 0 )
        return false;

    struct RSCacheDat2Disk_ReferenceTable* table = disk->tables[RSCacheDat2Disk_Table_Configs];
    if( !table || config_kind >= table->archive_count )
        return false;

    return true;
}

static bool
InterfaceX_ConfigArchiveFindFile(
    struct RSCacheDat2Disk* disk,
    int config_kind,
    struct RSCacheShared_FileList* fl,
    int file_id,
    uint8_t const** out_data,
    int* out_len)
{
    if( !disk || !fl || file_id < 0 )
        return false;

    struct RSCacheDat2Disk_ReferenceTable* table = disk->tables[RSCacheDat2Disk_Table_Configs];
    struct RSCacheDat2Disk_ArchiveReference* ref = NULL;
    if( table && table->archives )
        ref = &table->archives[config_kind];

    for( int i = 0; i < fl->file_count; i++ )
    {
        int id = (ref && i < ref->children.count) ? ref->children.files[i].id : i;
        if( id != file_id )
            continue;
        if( out_data )
            *out_data = (uint8_t const*)fl->files[i];
        if( out_len )
            *out_len = fl->file_sizes[i];
        return true;
    }
    return false;
}

static char
InterfaceX_ParamTypeIdToChar(int id)
{
    if( id == 36 )
        return 's';
    return 'i';
}

static void
InterfaceX_DecodeParamType(
    uint8_t const* data,
    int len,
    struct InterfaceX_ParamType* out)
{
    if( !out || !data || len <= 0 || (len == 1 && data[0] == 0) )
        return;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, (uint32_t)len);

    for( ;; )
    {
        int opcode = g1(&buf);
        if( opcode == 0 )
            break;
        switch( opcode )
        {
        case 1:
            out->is_string = g1(&buf) == 's';
            break;
        case 8:
            out->is_string = InterfaceX_ParamTypeIdToChar(g1(&buf)) == 's';
            break;
        case 2:
            out->default_int = g4(&buf);
            break;
        case 5:
            free(out->default_string);
            out->default_string = gcstring(&buf);
            break;
        default:
            break;
        }
    }
}

static bool
InterfaceX_LoadParamType(
    struct RSCacheDat2Disk* disk,
    int param_id,
    struct InterfaceX_ParamType* out)
{
    assert(out);
    memset(out, 0, sizeof(*out));
    out->id = param_id;

    if( !disk || param_id < 0 )
        return false;

    if( !InterfaceX_ConfigArchiveReady(disk, RSCacheDat2A_ConfigKind_Params) )
        return false;

    struct RSCacheDat2Disk_Archive* arch = RSCacheDat2Disk_ArchiveNewLoad(
        disk, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Params);
    if( !arch )
        return false;

    RSCacheDat2Disk_ArchiveInitMetadata(disk, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl )
    {
        RSCacheDat2Disk_ArchiveFree(arch);
        return false;
    }

    uint8_t const* data = NULL;
    int data_len = 0;
    bool found = InterfaceX_ConfigArchiveFindFile(
        disk, RSCacheDat2A_ConfigKind_Params, fl, param_id, &data, &data_len);

    if( found )
        InterfaceX_DecodeParamType(data, data_len, out);

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return found;
}

static struct InterfaceX_ObjType*
InterfaceX_LoadObjType(
    struct RSCacheDat2Disk* disk,
    int item_id)
{
    if( !disk || item_id <= 0 )
        return NULL;

    if( !InterfaceX_ConfigArchiveReady(disk, RSCacheDat2A_ConfigKind_Object) )
        return NULL;

    struct RSCacheDat2Disk_Archive* arch = RSCacheDat2Disk_ArchiveNewLoad(
        disk, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Object);
    if( !arch )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadata(disk, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl )
    {
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    uint8_t const* data = NULL;
    int data_len = 0;
    if( !InterfaceX_ConfigArchiveFindFile(
            disk, RSCacheDat2A_ConfigKind_Object, fl, item_id, &data, &data_len) )
    {
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    struct RSCacheDat2A_ConfigObject* decoded = calloc(1, sizeof(struct RSCacheDat2A_ConfigObject));
    if( !decoded )
    {
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    RSCacheDat2A_ConfigObjectDecodeInplace(decoded, (char*)data, data_len);

    struct InterfaceX_ObjType* obj = calloc(1, sizeof(struct InterfaceX_ObjType));
    if( !obj )
    {
        RSCacheDat2A_ConfigObjectFree(decoded);
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    obj->id = item_id;
    obj->param_count = decoded->params.count;
    if( obj->param_count > 0 )
    {
        obj->params = calloc((size_t)obj->param_count, sizeof(struct InterfaceX_ObjTypeParam));
        if( !obj->params )
        {
            RSCacheDat2A_ConfigObjectFree(decoded);
            free(obj);
            RSCacheShared_FileListFree(fl);
            RSCacheDat2Disk_ArchiveFree(arch);
            return NULL;
        }

        for( int i = 0; i < obj->param_count; i++ )
        {
            struct InterfaceX_ObjTypeParam* dst = &obj->params[i];
            dst->key = decoded->params.keys[i];
            dst->is_string = decoded->params.is_string[i];
            if( dst->is_string )
            {
                char const* src =
                    decoded->params.values[i] ? (char const*)decoded->params.values[i] : "";
                dst->str_value = src[0] != '\0' ? strdup(src) : NULL;
            }
            else
            {
                dst->int_value = decoded->params.values[i] ? *(int*)decoded->params.values[i] : 0;
            }
        }
    }

    RSCacheDat2A_ConfigObjectFree(decoded);
    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return obj;
}

static struct InterfaceX_ObjTypeParam*
InterfaceX_ObjTypeParamGet(
    struct InterfaceX_ObjType const* obj,
    int param_id)
{
    if( !obj || !obj->params || obj->param_count <= 0 )
        return NULL;

    for( int i = 0; i < obj->param_count; i++ )
    {
        if( obj->params[i].key == param_id )
            return &obj->params[i];
    }
    return NULL;
}

int
InterfaceX_VMHost_Exec_OC_Param(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_OC_Param request)
{
    assert(host);
    assert(host->builder);
    assert(vm);

    struct InterfaceX_ParamType param;
    bool have_param = false;
    struct InterfaceX_ObjType* obj = NULL;
    struct InterfaceX_ObjTypeParam* val = NULL;

    if( host->disk )
    {
        have_param = InterfaceX_LoadParamType(host->disk, request.param_id, &param);
        obj = InterfaceX_LoadObjType(host->disk, request.item_id);
        val = obj ? InterfaceX_ObjTypeParamGet(obj, request.param_id) : NULL;
    }

    if( have_param && obj && obj->params && obj->param_count > 0 )
    {
        if( param.is_string )
        {
            char const* pushed = "";
            if( val && val->is_string && val->str_value )
                pushed = val->str_value;
            else if( param.default_string )
                pushed = param.default_string;
            CS2VMX_PushStr(vm, (char*)pushed);
        }
        else
        {
            int pushed = param.default_int;
            if( val && !val->is_string )
                pushed = val->int_value;
            CS2VMX_PushInt(vm, pushed);
        }
    }
    else
    {
        if( have_param && param.is_string )
        {
            CS2VMX_PushStr(vm, param.default_string ? param.default_string : (char*)"");
        }
        else
        {
            CS2VMX_PushInt(vm, have_param ? param.default_int : 0);
        }
    }

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request)
{
    struct InterfaceX_VMHost* vmhost = (struct InterfaceX_VMHost*)CS2VM_USER(vm);

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_PUSHSCRIPT:
        return InterfaceX_VMHost_Exec_PushScript(vmhost, vm, request->u.push_script.script_id);
    case CS2VM_HOST_REQUEST_INVS_GET_SIZE:
        return InterfaceX_VMHost_Exec_InvSize(vmhost, vm, request->u.invs_get_size.inv_id);
    case CS2VM_HOST_REQUEST_INVS_GET_OBJ:
        return InterfaceX_VMHost_Exec_InvGetObj(vmhost, vm, request->u.invs_get_obj);
    case CS2VM_HOST_REQUEST_INVS_GET_TOTAL:
        return InterfaceX_VMHost_Exec_InvTotal(vmhost, vm, request->u.invs_get_total);
    case CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR:
        return InterfaceX_VMHost_Exec_VarsReadVarp(vmhost, vm, request->u.vars_read_varp);
    case CS2VM_HOST_REQUEST_IF_GETWIDTH:
        return InterfaceX_VMHost_Exec_IF_GetWidth(vmhost, vm, request->u.if_get_width.component_id);
    case CS2VM_HOST_REQUEST_IF_GETHEIGHT:
        return InterfaceX_VMHost_Exec_IF_GetHeight(
            vmhost, vm, request->u.if_get_height.component_id);
    case CS2VM_HOST_REQUEST_IF_SETHIDE:
        return InterfaceX_VMHost_Exec_IF_SetHide(vmhost, vm, request->u.if_set_hide);
    case CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT:
        return InterfaceX_VMHost_Exec_IF_SetOnVarTransmit(
            vmhost, vm, request->u.if_set_on_var_transmit);
    case CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT:
        return InterfaceX_VMHost_Exec_IF_SetOnInvTransmit(
            vmhost, vm, request->u.if_set_on_inv_transmit);
    case CS2VM_HOST_REQUEST_IF_SETONOP:
        return InterfaceX_VMHost_Exec_IF_SetOnOp(vmhost, vm, request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETOP:
        return InterfaceX_VMHost_Exec_IF_SetOp(vmhost, vm, request->u.if_set_op);
    case CS2VM_HOST_REQUEST_IF_CLEAROPS:
        return InterfaceX_VMHost_Exec_IF_ClearOps(vmhost, vm, request->u.if_clear_ops);
    case CS2VM_HOST_REQUEST_IF_SETOBJECT:
        return InterfaceX_VMHost_Exec_IF_SetObject(vmhost, vm, request->u.if_set_object);
    case CS2VM_HOST_REQUEST_CC_DELETEALL:
        return InterfaceX_VMHost_Exec_CC_DeleteAll(
            vmhost, vm, request->u.cc_delete_all.component_id);
    case CS2VM_HOST_REQUEST_CC_CREATE:
        return InterfaceX_VMHost_Exec_CC_Create(vmhost, vm, request->u.cc_create);
    case CS2VM_HOST_REQUEST_CC_SETPOSITION:
        return InterfaceX_VMHost_Exec_CC_SetPosition(vmhost, vm, request->u.cc_set_position);
    case CS2VM_HOST_REQUEST_CC_SETSIZE:
        return InterfaceX_VMHost_Exec_CC_SetSize(vmhost, vm, request->u.cc_set_size);
    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC:
        return InterfaceX_VMHost_Exec_CC_SetGraphic(vmhost, vm, request->u.cc_set_graphic);
    case CS2VM_HOST_REQUEST_CC_SETTILING:
        return InterfaceX_VMHost_Exec_CC_SetTiling(vmhost, vm, request->u.cc_set_tiling);
    case CS2VM_HOST_REQUEST_CC_SETOBJECT:
        return InterfaceX_VMHost_Exec_CC_SetObject(vmhost, vm, request->u.cc_set_object);
    case CS2VM_HOST_REQUEST_OC_PARAM:
        return InterfaceX_VMHost_Exec_OC_Param(vmhost, vm, request->u.oc_param);
    default:
        printf("VMHost: unknown request kind: %d\n", request->kind);
        return CS2VM_EXECNO_ERROR;
    }
}

static void
CS2VMX_ResetRuntime(struct CS2VMX* vm)
{
    assert(vm);
    vm->ints_stack_top = 0;
    vm->strs_stack_top = 0;
    vm->frame_sp = 0;
}

static void
InterfaceX_VMHost_FireInvTransmitHooks(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm)
{
    assert(host);
    assert(vm);

    for( int i = 0; i < host->inv_transmit_hook_count; i++ )
    {
        struct InterfaceX_InvTransmitHook const* hook = &host->inv_transmit_hooks[i];
        if( hook->script_id <= 0 )
            continue;

        printf(
            "running onInvTransmit script %d for component %d (args:",
            hook->script_id,
            hook->component_id);
        for( int j = 0; j < hook->int_arg_count; j++ )
            printf(" %d", hook->int_args[j]);
        printf(")\n");

        (void)InterfaceX_RunClientScript(
            host,
            vm,
            hook->script_id,
            hook->component_id,
            hook->int_args,
            hook->int_arg_count);
    }
}

static void
InterfaceX_VMHost_FireCacheInvTransmitHooks(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct ToriAuxLibCore_Component** components,
    int component_count)
{
    assert(host);
    assert(vm);
    if( !components || component_count <= 0 )
        return;

    for( int i = 0; i < component_count; i++ )
    {
        struct ToriAuxLibCore_Component* comp = components[i];
        if( !comp )
            continue;
        if( InterfaceX_RuntimeHookOwnsComponent(host, comp->id) )
            continue;
        if( comp->on_inv_transmit.argc <= 0 )
            continue;

        int script_id = comp->on_inv_transmit.argv[0];
        int args[INTERFACEX_INV_TRANSMIT_INT_ARG_MAX];
        int arg_count = comp->on_inv_transmit.argc - 1;
        if( arg_count > INTERFACEX_INV_TRANSMIT_INT_ARG_MAX )
            arg_count = INTERFACEX_INV_TRANSMIT_INT_ARG_MAX;
        for( int j = 0; j < arg_count; j++ )
            args[j] = comp->on_inv_transmit.argv[j + 1];

        printf(
            "running cache onInvTransmit script %d for component %d\n",
            script_id,
            comp->id);
        (void)InterfaceX_RunClientScript(host, vm, script_id, comp->id, args, arg_count);
    }
}

// dat2 disk
//   └─ Interfaces index, archive N
//        └─ RSCacheDat2Disk_Archive (compressed blob + file_count metadata)
//             └─ RSCacheShared_FileListNewFromCacheArchive
//                  └─ files[0..count-1]  (one encoded widget each)
//                       └─ component_decode_from_bytes((N<<16)|i, ...)
//                            ├─ byte[0]==255 → DecodeIf3
//                            └─ else         → DecodeIf1
//                                 └─ RSCacheDat2A_Component in InterfaceArchive
//                                      └─ (optional) ToriAuxLibCache_SubmitComponentsFromDat2
int
main(
    int argc,
    char** argv)
{
    struct RSCacheDat2Disk* cache = NULL;
    struct RSCacheDat2Disk_Archive* archive = NULL;
    struct RSCacheDat2Disk_ReferenceTable* reference_table = NULL;
    RSCacheDat2A_Component* component = NULL;
    struct RSCacheShared_RSBuffer buffer;
    struct ToriAuxLibCore_Component* component_core = NULL;
    struct ToriAuxLibCore_Component** components = NULL;
    struct ToriAuxLibCore_ClientScript* client_script = NULL;
    struct UITreeX* tree = NULL;
    struct UITreeXBuilder* builder = NULL;
    struct RSCacheShared_FileList* file_list = NULL;
    struct InterfaceX_VMHost vmhost;

    cache = RSCacheDat2Disk_NewFromDirectory(CACHE_PATH);
    if( !cache )
    {
        fprintf(stderr, "failed to open cache: %s\n", CACHE_PATH);
        return 1;
    }

    struct CS2VMX vm;
    memset(&vm, 0, sizeof(vm));
    vm.active_component_id = -1;
    vm.dot_component_id = -1;

    memset(&vmhost, 0, sizeof(vmhost));
    vmhost.disk = cache;
    vmhost.clientscript_table = cache->tables[RSCacheDat2Disk_Table_Clientscript];

    if( !InterfaceX_VMHost_Init(&vmhost) )
    {
        fprintf(stderr, "failed to init host script cache\n");
        return 1;
    }

    CS2VMX_BindHost(&vm, &vmhost, InterfaceX_VMHost_Exec);

#define BANK_INTERFACE 12
#define EQUIPMENT_INTERFACE 387
#define INTERFACE_ID EQUIPMENT_INTERFACE

    reference_table = cache->tables[RSCacheDat2Disk_Table_Interfaces];
    if( !reference_table )
    {
        fprintf(stderr, "failed to load reference table: %d\n", INTERFACE_ID);
        return 1;
    }

    archive = RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Interfaces, INTERFACE_ID);
    if( !archive )
    {
        fprintf(stderr, "failed to load archive: %d\n", INTERFACE_ID);
        return 1;
    }

    RSCacheDat2Disk_ArchiveInitMetadataFromTable(reference_table, archive);

    file_list = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !file_list )
    {
        fprintf(stderr, "failed to create file list: %d\n", INTERFACE_ID);
        return 1;
    }

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0);
    if( !scene )
    {
        fprintf(stderr, "failed to create scene: %d\n", INTERFACE_ID);
        return 1;
    }

    // Layout loading loop
    tree = UITreeX_New();
    builder = calloc(1, sizeof(struct UITreeXBuilder));
    UITreeXBuilder_Init(builder, tree);

    vmhost.builder = builder;
    vmhost.tree = tree;
    vmhost.interface_id = INTERFACE_ID;
    vmhost.scene = scene;
    InterfaceX_InvStoreSeedDefaults(&vmhost);

    components = calloc(file_list->file_count, sizeof(struct ToriAuxLibCore_Component*));
    for( int i = 0; i < file_list->file_count; i++ )
    {
        int packed_id = (INTERFACE_ID << 16) | (i & 0xFFFF);
        component =
            component_decode_from_bytes(packed_id, file_list->files[i], file_list->file_sizes[i]);
        if( !component )
        {
            fprintf(stderr, "failed to decode component: %d\n", INTERFACE_ID);
            return 1;
        }

        component_core = ToriAuxLibCache_ComponentNewFromCacheDat2Component(component);
        if( !component_core )
        {
            fprintf(stderr, "failed to create component core: %d\n", INTERFACE_ID);
            return 1;
        }

        components[i] = component_core;
    }

    for( int i = 0; i < file_list->file_count; i++ )
    {
        component_core = components[i];
        process_component(builder, component_core);
    }

    reference_table = cache->tables[RSCacheDat2Disk_Table_Clientscript];
    for( int i = 0; i < file_list->file_count; i++ )
    {
        component_core = components[i];

        int script_id = component_core->on_load.argv[0];
        if( script_id <= 0 )
            continue;

        int args[TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX];
        int arg_count = component_core->on_load.argc - 1;
        if( arg_count > TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX )
            arg_count = TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX;
        for( int j = 0; j < arg_count; j++ )
            args[j] = component_core->on_load.argv[j + 1];

        (void)InterfaceX_RunClientScript(
            &vmhost, &vm, script_id, component_core->id, args, arg_count);
    }

    InterfaceX_VMHost_FireInvTransmitHooks(&vmhost, &vm);
    InterfaceX_VMHost_FireCacheInvTransmitHooks(
        &vmhost, &vm, components, file_list->file_count);

    UITreeX_LayoutResolve(tree, CANVAS_W, CANVAS_H);
    UITreeX_PrintNodes(tree);

    int* pixels = calloc((size_t)CANVAS_W * (size_t)CANVAS_H, sizeof(int));
    if( !pixels )
    {
        fprintf(stderr, "failed to allocate canvas\n");
        return 1;
    }

    for( int i = 0; i < CANVAS_W * CANVAS_H; i++ )
        pixels[i] = CANVAS_BG;

    UITreeX_Render(&vmhost, cache, tree, pixels);

    char const* out_path = "./interfacex_387-3.bmp";
    bmp_write_file(out_path, pixels, CANVAS_W, CANVAS_H);
    printf("wrote %s (%dx%d)\n", out_path, CANVAS_W, CANVAS_H);
    free(pixels);

    return 0;
}