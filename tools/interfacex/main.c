#include "../src/osrs/rscache/rscache.u.c"
#include "../src2/toriauxlib/toriauxlib.h"
#include "../src2/vm/cs2_opcode.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_clientscript_convert.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore_types.h"
#include "vm/cs2_script.h"

#include <assert.h>
#include <stdio.h>

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

#define CS2VM_EXECNO_YIELD -2
#define CS2VM_EXECNO_ERROR -1
#define CS2VM_EXECNO_OK 0
#define CS2VM_EXECNO_DONE 1

#define CS2VM_STACK_MAX 1024

#define CACHE_PATH "/Users/matthewevers/Documents/git_repos/3draster/cache"

#define CS2VM_USER(vm) ((struct CS2VMX*)(vm))->user
#define CS2VM_MAX_LOCALS 1024
struct CS2VMX_Frame
{
    struct CS2_Script* script;
    int pc;
    int int_locals[CS2VM_MAX_LOCALS];
    int int_stack_top;
    char* str_locals[CS2VM_MAX_LOCALS];
    int str_stack_top;

    int return_pc;
    int return_frame;

    int has_return;
    int return_int_count;
    int return_ints[8];
};

#define CS2VM_MAX_FRAMES 32
#define CS2VM_MAX_CYCLES 1000000

typedef int (*CS2VMX_HostExec_Fn)(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request);

struct CS2VMX
{
    void* user;
    CS2VMX_HostExec_Fn host_exec;
    struct CS2VMX_Frame frames[CS2VM_MAX_FRAMES];
    int frame_sp;
};

int
CS2VMX_Op_PushIntLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    (void)vm;
    assert(vm);
    assert(frame);
    frame->int_locals[frame->int_stack_top++] = operand;
    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_PushStrLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    char const* operand)
{
    (void)vm;
    assert(vm);
    assert(frame);
    frame->str_locals[frame->str_stack_top++] = operand;
    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_PopInt(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int* operand)
{
    (void)vm;
    assert(vm);
    assert(frame);
    if( frame->int_stack_top <= 0 )
        return CS2VM_EXECNO_ERROR;
    *operand = frame->int_locals[--frame->int_stack_top];
    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_PopStr(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    char** operand)
{
    (void)vm;
    assert(vm);
    assert(frame);
    if( frame->str_stack_top <= 0 )
        return CS2VM_EXECNO_ERROR;
    *operand = frame->str_locals[--frame->str_stack_top];
    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_GosubWithParams(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    struct CS2VM_HostRequest request;
    request.op = CS2_OP_GOSUB_WITH_PARAMS;
    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;
}

int
CS2VMX_Op_PopIntDiscard(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame)
{
    (void)vm;
    assert(vm);
    assert(frame->int_stack_top > 0);
    frame->int_stack_top -= 1;
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

enum CS2VM_HostRequestKind
{
    CS2VM_HOST_REQUEST_PUSHSCRIPT,
};

struct CS2VM_HostRequest_PushScript
{
    int script_id;
};

struct CS2VM_HostRequest
{
    enum CS2VM_HostRequestKind kind;

    union
    {
        struct CS2VM_HostRequest_PushScript push_script;
    } u;
};

int
CS2VMX_HostExec(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request)
{
    void* user = CS2VM_USER(vm);

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_PUSHSCRIPT:
    {
        struct CS2_Script* script =
            ToriAuxLibCache_GetScript(user, request->u.push_script.script_id);
    }
    default:
        return CS2VM_EXECNO_ERROR;
    }

    return CS2VM_EXECNO_ERROR;
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
    case CS2_OP_PUSH_INT_LOCAL:
        return CS2VMX_Op_PushIntLocal(vm, frame, operand);
    case CS2_OP_PUSH_STRING_LOCAL:
        return CS2VMX_Op_PushStrLocal(vm, frame, str_operand);
    case CS2_OP_POP_INT_LOCAL:
        return CS2VMX_Op_PopInt(vm, frame, &intpop_a);
    case CS2_OP_POP_STRING_LOCAL:
        return CS2VMX_Op_PopStr(vm, frame, &strpop_a);
    case CS2_OP_GOSUB_WITH_PARAMS:
        return CS2VMX_Op_GosubWithParams(vm, frame, operand);
    case CS2_OP_POP_INT_DISCARD:
        return CS2VMX_Op_PopIntDiscard(vm, frame);
    case CS2_OP_RETURN:
        return CS2VMX_Op_Return(vm, frame);
    default:
        fprintf(stderr, "unknown opcode: %d\n", opcode);
        return true;
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
        printf("opcode: %d\n", opcode);
        int operand = frame->script->int_operands[frame->pc];
        char const* str_operand_str = frame->script->string_operands[frame->pc];

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
    struct ToriAuxLib* toriauxlib = NULL;
    struct ToriAuxLibCore_Component* component_core = NULL;
    struct ToriAuxLibCore_Component** components = NULL;
    struct ToriAuxLibCore_ClientScript* client_script = NULL;
    struct UITreeX* tree = NULL;
    struct UITreeXBuilder* builder = NULL;
    struct RSCacheShared_FileList* file_list = NULL;

    cache = RSCacheDat2Disk_NewFromDirectory(CACHE_PATH);
    if( !cache )
    {
        fprintf(stderr, "failed to open cache: %s\n", CACHE_PATH);
        return 1;
    }

    struct CS2VMX vm;
    memset(&vm, 0, sizeof(vm));

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

    toriauxlib = ToriAuxLib_New(TORIAUXLIBCACHE_MODE_DAT2, NULL);
    if( !toriauxlib )
    {
        fprintf(stderr, "failed to create toriauxlib: %d\n", INTERFACE_ID);
        return 1;
    }

    // Layout loading loop
    tree = UITreeX_New();
    builder = calloc(1, sizeof(struct UITreeXBuilder));
    UITreeXBuilder_Init(builder, tree);

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

        archive =
            RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Clientscript, script_id);
        if( !archive )
        {
            fprintf(stderr, "failed to load archive: %d\n", script_id);
            return 1;
        }

        RSCacheDat2Disk_ArchiveInitMetadataFromTable(reference_table, archive);

        client_script = ToriAuxLibCache_ClientScriptNewFromDat2Archive2(
            archive, script_id, CLIENTSCRIPT_DECODE_TRAILER_LEGACY);
        if( !client_script )
        {
            fprintf(stderr, "failed to load client script: %d\n", script_id);
            return 1;
        }

        CS2VMX_PushCallScript(&vm, &client_script->script);

        int res;
        while( (res = CS2VMX_RunScript(&vm)) )
        {
            switch( res )
            {
            case CS2VM_EXECNO_DONE:
                break;
            case CS2VM_EXECNO_YIELD:
                break;
            case CS2VM_EXECNO_ERROR:
                fprintf(stderr, "error running client script: %d\n", script_id);
                return 1;
            }
        }
    }

    UITreeX_PrintNodes(tree);

    return 0;
}