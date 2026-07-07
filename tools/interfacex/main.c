#include "../src/osrs/rscache/rscache.u.c"
#include "../src2/toriauxlib/toriauxlib.h"
#include "../src2/vm/cs2_opcode.h"
#include "osrs/rscache/dat2a/dat2a_clientscript.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_clientscript_convert.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore_types.h"
#include "toridraw/toridraw_map.h"
#include "vm/cs2_script.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct UITreeNodeXLink
{
    int parent_tree_idx;
    int next_sibling_tree_idx;
    int first_child_tree_idx;
    int last_child_tree_idx;
};

struct UITreeXNode_RSLayer
{};

struct UITreeXNode_RSGraphic
{
    int scene_id;
    int graphic_id;
};

enum UITreeXNodeKind
{
    UITreeXNodeKind_Root,
    UITreeXNodeKind_RSLayer,
    UITreeXNodeKind_RSGraphic,
};

struct UITreeXNode
{
    int user_id;
    int idx;
    struct UITreeNodeXLink link;
    enum UITreeXNodeKind kind;
    union
    {
        struct UITreeXNode_RSLayer rs_layer;
        struct UITreeXNode_RSGraphic rs_graphic;
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
    printf("[%d] kind=%s user_id=%s", node_idx, UITreeX_NodeKindStr(node->kind), user_id_buf);

    if( node->kind == UITreeXNodeKind_RSGraphic )
    {
        printf(
            " graphic_id=%d scene_id=%d",
            node->u.rs_graphic.graphic_id,
            node->u.rs_graphic.scene_id);
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

typedef int (*CS2VMX_HostExec_Fn)(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request);

struct CS2VMX
{
    void* user;
    CS2VMX_HostExec_Fn host_exec;
    struct CS2VMX_Frame frames[CS2VM_MAX_FRAMES];

    int ints_stack[100];
    int ints_stack_top;
    char* strs_stack[100];
    int strs_stack_top;

    int frame_sp;

    int active_component_id;
    int dot_component_id;
};

enum CS2VM_HostRequestKind
{
    CS2VM_HOST_REQUEST_PUSHSCRIPT,
    CS2VM_HOST_REQUEST_CC_DELETEALL,
    CS2VM_HOST_REQUEST_CC_CREATE,
    CS2VM_HOST_REQUEST_CC_SETPOSITION,
    CS2VM_HOST_REQUEST_CC_SETSIZE,
    CS2VM_HOST_REQUEST_CC_SETGRAPHIC,
    CS2VM_HOST_REQUEST_CC_SETTILING,
    CS2VM_HOST_REQUEST_IF_GETWIDTH,
    CS2VM_HOST_REQUEST_IF_GETHEIGHT,
};

struct CS2VM_HostRequest_PushScript
{
    int script_id;
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
};

struct CS2VM_HostRequest_IF_GetWidth
{
    int component_id;
};

struct CS2VM_HostRequest_IF_GetHeight
{
    int component_id;
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

struct CS2VM_HostRequest
{
    enum CS2VM_HostRequestKind kind;

    union
    {
        struct CS2VM_HostRequest_PushScript push_script;
        struct CS2VM_HostRequest_CC_DeleteAll cc_delete_all;
        struct CS2VM_HostRequest_CC_Create cc_create;
        struct CS2VM_HostRequest_CC_SetPosition cc_set_position;
        struct CS2VM_HostRequest_CC_SetSize cc_set_size;
        struct CS2VM_HostRequest_CC_SetGraphic cc_set_graphic;
        struct CS2VM_HostRequest_CC_SetTiling cc_set_tiling;
        struct CS2VM_HostRequest_IF_GetWidth if_get_width;
        struct CS2VM_HostRequest_IF_GetHeight if_get_height;
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
CS2VMX_PushStr(
    struct CS2VMX* vm,
    char* value)
{
    assert(vm);

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
        // if( CS2VMX_PopStrFrameLocal(vm, callee, i) != CS2VM_EXECNO_OK )
        //     return CS2VM_EXECNO_ERROR;
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

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_DELETEALL;
    request.u.cc_delete_all.component_id = vm->active_component_id;

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

    int is_nested, type, child_index, parent_id;

    if( CS2VMX_PopInt(vm, &is_nested) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &child_index) != CS2VM_EXECNO_OK )
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
}

int
CS2VMX_Op_IF_GetWidth(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETWIDTH;
    request.u.if_get_width.component_id = operand;

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
    (void)vm;
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETHEIGHT;
    request.u.if_get_height.component_id = operand;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
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

    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, intpop_a * intpop_b);
}

int
CS2VMX_Op_Div(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    (void)vm;
    assert(vm);
    assert(frame);

    int intpop_a, intpop_b;

    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, intpop_a / intpop_b);
}

int
CS2VMX_Op_Mod(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    (void)vm;
    assert(vm);
    assert(frame);
    int intpop_a, intpop_b;

    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, intpop_a % intpop_b);
}

int
CS2VMX_Op_Pow(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    (void)vm;
    assert(vm);
    assert(frame);
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
CS2VMX_Op_Return(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame)
{
    (void)vm;
    assert(vm);
    assert(frame);
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
    case CS2_OP_RETURN:
        return CS2VMX_Op_Return(vm, frame);
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
    case CS2_OP_IF_GETWIDTH:
        return CS2VMX_Op_IF_GetWidth(vm, frame, operand);
    case CS2_OP_IF_GETHEIGHT:
        return CS2VMX_Op_IF_GetHeight(vm, frame, operand);
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
    default:
        fprintf(stderr, "unknown opcode: %d\n", opcode);
        return true;
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

    switch( component->type )
    {
    case TORIAUXLIBCORE_COMPONENT_LAYER:
        return UITreeXBuilder_PushLayerWithParentUserId(builder, component->id, layer);
    case TORIAUXLIBCORE_COMPONENT_GRAPHIC:
        return UITreeXBuilder_PushGraphicWithParentUserId(
            builder, component->id, component->graphic, layer);
    default:
        fprintf(stderr, "unknown component type: %d\n", component->type);
        return 0;
    }

    return 0;
}

struct MapEntry_ClientScript
{
    int id;
    struct ToriAuxLibCore_ClientScript* script;
};

struct InterfaceX_VMHost
{
    struct ToriDraw_Map* scripts;

    unsigned char scripts_buf[4096];
    struct RSCacheDat2Disk* disk;
    struct RSCacheDat2Disk_ReferenceTable* clientscript_table;

    struct UITreeXBuilder* builder;
};

#define HOST_SCRIPT_MAP_CAP 64

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
InterfaceX_VMHost_Exec_CC_DeleteAll(struct InterfaceX_VMHost* host)
{
    assert(host);
    assert(host->builder);

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
#define SCREEN_WIDTH 1024
    CS2VMX_PushInt(vm, SCREEN_WIDTH);

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

#define SCREEN_HEIGHT 768
    CS2VMX_PushInt(vm, SCREEN_HEIGHT);

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

    int parent_id = request.parent_id;
    int type = request.component_type;
    int child_index = request.child_index;
    int is_nested = request.is_nested;

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

    int component_id = request.component_id;
    int x = request.x;
    int y = request.y;

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
}

int
InterfaceX_VMHost_Exec_CC_SetGraphic(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetGraphic request)
{
    assert(host);
    assert(host->builder);
}

int
InterfaceX_VMHost_Exec_CC_SetTiling(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetTiling request)
{
    assert(host);
    assert(host->builder);

    int component_id = request.component_id;
    int tiling = request.tiling;

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
    case CS2VM_HOST_REQUEST_CC_DELETEALL:
        return InterfaceX_VMHost_Exec_CC_DeleteAll(vmhost);
    case CS2VM_HOST_REQUEST_IF_GETWIDTH:
        return InterfaceX_VMHost_Exec_IF_GetWidth(vmhost, vm, request->u.if_get_width.component_id);
    case CS2VM_HOST_REQUEST_IF_GETHEIGHT:
        return InterfaceX_VMHost_Exec_IF_GetHeight(
            vmhost, vm, request->u.if_get_height.component_id);
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
    default:
        return CS2VM_EXECNO_ERROR;
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

    components = calloc(file_list->file_count, sizeof(struct ToriAuxLibCore_Component));
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

        client_script = InterfaceX_VMHost_ResolveScript(&vmhost, script_id);

        CS2VMX_PushCallScript(&vm, &client_script->script);
        for( int j = 1; j < component_core->on_load.argc; j++ )
        {
            int argi = component_core->on_load.argv[j];
            switch( argi )
            {
            case CS2VM_SCRIPT_ARG_WIDGET_ID:
                CS2VMX_SetIntCurrentFrameLocal(&vm, j - 1, component_core->id);
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
                CS2VMX_SetIntCurrentFrameLocal(&vm, j - 1, argi);
                break;
            }
        }

        CS2VMX_SetActiveAndDotComponentId(&vm, component_core->id);

        int res;
        while( (res = CS2VMX_RunScript(&vm)) )
        {
            switch( res )
            {
            case CS2VM_EXECNO_DONE:
                goto done;
            case CS2VM_EXECNO_YIELD:
                break;
            case CS2VM_EXECNO_ERROR:
                fprintf(stderr, "error running client script: %d\n", script_id);
                return 1;
            }
        }
    done:
    }

    UITreeX_PrintNodes(tree);

    return 0;
}