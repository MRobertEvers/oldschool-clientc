#include "vm/cs2vmx.h"
#include "vm/cs2_opcode.h"
#include "ui/uitree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) \
    do \
    { \
        if( !(cond) ) \
        { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            return 1; \
        } \
    } while( 0 )

struct TestHost
{
    struct UITree* tree;
    struct CS2_Script* resolve_script;
    int resolve_script_id;

    int inv_id;
    int inv_slot;
    int inv_obj_id;
    int inv_count;
    int inv_size;
};

static int
test_run(
    struct CS2VMX* vm,
    struct CS2_Script* script)
{
    CS2VMX_ResetRuntime(vm);
    if( CS2VMX_PushCallScript(vm, script) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VMX_RunScript(vm);
}

static int
test_host_exec(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request)
{
    struct TestHost* host = CS2VM_USER(vm);
    if( !host || !request )
        return CS2VM_EXECNO_ERROR;

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_PUSHSCRIPT:
        if( host->resolve_script && request->u.push_script.script_id == host->resolve_script_id )
            return CS2VMX_PushCallScript(vm, host->resolve_script);
        return CS2VM_EXECNO_ERROR;

    case CS2VM_HOST_REQUEST_ENUM_LOOKUP:
        return CS2VMX_PushInt(vm, request->u.enum_lookup.key);

    case CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT:
        return CS2VMX_PushInt(vm, 0);

    case CS2VM_HOST_REQUEST_STRUCT_PARAM:
        return CS2VMX_PushInt(vm, 0);

    case CS2VM_HOST_REQUEST_INVS_GET_OBJ:
        if( request->u.invs_get_obj.inv_id == host->inv_id &&
            request->u.invs_get_obj.slot == host->inv_slot )
            return CS2VMX_PushInt(vm, host->inv_obj_id);
        return CS2VMX_PushInt(vm, -1);

    case CS2VM_HOST_REQUEST_INVS_GET_NUM:
        if( request->u.invs_get_num.inv_id == host->inv_id &&
            request->u.invs_get_num.slot == host->inv_slot )
            return CS2VMX_PushInt(vm, host->inv_count);
        return CS2VMX_PushInt(vm, 0);

    case CS2VM_HOST_REQUEST_INVS_GET_SIZE:
        if( request->u.invs_get_size.inv_id == host->inv_id )
            return CS2VMX_PushInt(vm, host->inv_size);
        return CS2VMX_PushInt(vm, 0);

    case CS2VM_HOST_REQUEST_INVS_GET_TOTAL:
        return CS2VMX_PushInt(vm, 0);

    case CS2VM_HOST_REQUEST_IF_FIND:
        if( host->tree &&
            uitree_find_by_component_id(host->tree, request->u.if_find.component_id) >= 0 )
        {
            CS2VMX_SetTargetComponentId(
                vm, request->u.if_find.dot_operand, request->u.if_find.component_id);
            return CS2VMX_PushInt(vm, 1);
        }
        return CS2VMX_PushInt(vm, 0);

    case CS2VM_HOST_REQUEST_CC_CREATE:
    {
        if( !host->tree )
            return CS2VM_EXECNO_OK;
        int32_t parent_idx =
            uitree_find_by_component_id(host->tree, request->u.cc_create.parent_id);
        if( parent_idx < 0 )
            return CS2VM_EXECNO_OK;
        int32_t child_idx = uitree_cc_create(
            host->tree,
            parent_idx,
            request->u.cc_create.parent_id,
            request->u.cc_create.component_type,
            request->u.cc_create.child_index);
        if( child_idx < 0 )
            return CS2VM_EXECNO_ERROR;
        CS2VMX_SetTargetComponentId(
            vm, request->u.cc_create.dot_operand, host->tree->components[child_idx].component_id);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_CC_SETOBJECT:
    case CS2VM_HOST_REQUEST_IF_SETOBJECT:
    {
        if( !host->tree )
            return CS2VM_EXECNO_OK;
        int cid = request->kind == CS2VM_HOST_REQUEST_CC_SETOBJECT
                      ? request->u.cc_set_object.component_id
                      : request->u.if_set_object.component_id;
        int obj = request->kind == CS2VM_HOST_REQUEST_CC_SETOBJECT
                      ? request->u.cc_set_object.obj_id
                      : request->u.if_set_object.obj_id;
        int count = request->kind == CS2VM_HOST_REQUEST_CC_SETOBJECT
                        ? request->u.cc_set_object.count
                        : request->u.if_set_object.count;
        (void)uitree_apply_object(host->tree, cid, obj, count, -1, 0);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_IF_SETHIDE:
        if( host->tree )
            (void)uitree_apply_hide(
                host->tree,
                request->u.if_set_hide.component_id,
                request->u.if_set_hide.hidden ? 1 : 0);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_WIDGET_SET_INT:
    {
        if( !host->tree )
            return CS2VM_EXECNO_OK;
        int32_t idx =
            uitree_find_by_component_id(host->tree, request->u.widget_set_int.component_id);
        if( idx < 0 )
            return CS2VM_EXECNO_OK;
        struct StaticUIComponent* node = &host->tree->components[idx];
        if( request->u.widget_set_int.field == CS2VM_WIDGET_INT_DRAG_DEAD_ZONE )
            node->drag_dead_zone = (uint8_t)request->u.widget_set_int.value;
        else if( request->u.widget_set_int.field == CS2VM_WIDGET_INT_DRAG_DEAD_TIME )
            node->drag_dead_time = (uint8_t)request->u.widget_set_int.value;
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR:
    case CS2VM_HOST_REQUEST_VARS_READ_VARBIT:
    case CS2VM_HOST_REQUEST_VARS_READ_VARC_INT:
        return CS2VMX_PushInt(vm, 0);

    case CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING:
        return CS2VMX_PushStr(vm, (char*)"");

    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_INT:
    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_STRING:
    case CS2VM_HOST_REQUEST_CC_SETON_DISCARD:
    case CS2VM_HOST_REQUEST_IF_SETON_DISCARD:
        return CS2VM_EXECNO_OK;

    default:
        fprintf(stderr, "test_host_exec: unhandled kind %d\n", (int)request->kind);
        return CS2VM_EXECNO_ERROR;
    }
}

static void
test_bind(
    struct CS2VMX* vm,
    struct TestHost* host)
{
    memset(vm, 0, sizeof(*vm));
    memset(host, 0, sizeof(*host));
    CS2VMX_BindHost(vm, host, test_host_exec);
}

static int
test_runtime_branch(void)
{
    struct CS2VMX vm;
    struct TestHost host;
    test_bind(&vm, &host);

    static uint16_t opcodes[] = {
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_BRANCH_EQUALS,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_RETURN,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_RETURN,
    };
    static int operands[] = { 1, 1, 2, 99, 0, 5, 0 };

    struct CS2_Script script = { 0 };
    script.op_count = 7;
    script.opcodes = opcodes;
    script.int_operands = operands;

    TEST_ASSERT(test_run(&vm, &script) == CS2VM_EXECNO_DONE, "branch script ok");
    fprintf(stderr, "ok: cs2vmx branch script completes\n");
    return 0;
}

static int
test_runtime_array_and_enum(void)
{
    struct CS2VMX vm;
    struct TestHost host;
    test_bind(&vm, &host);

    static uint16_t opcodes[] = {
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_DEFINE_ARRAY,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_ARRAY_INT,
        CS2_OP_RETURN,
    };
    /* POP_ARRAY_INT pops index then value → push value, then index. */
    static int operands[] = { 4, 0, 42, 2, 0, 2, 0, 0 };

    struct CS2_Script script = { 0 };
    script.op_count = 8;
    script.opcodes = opcodes;
    script.int_operands = operands;

    TEST_ASSERT(test_run(&vm, &script) == CS2VM_EXECNO_DONE, "array script ok");
    TEST_ASSERT(vm.ints_stack_top == 1 && vm.ints_stack[0] == 42, "array push value");

    static uint16_t enum_ops[] = {
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_ENUM,
        CS2_OP_RETURN,
    };
    static int enum_operands[] = { 0, 0, 99, 1, 0, 0 };
    struct CS2_Script enum_script = { 0 };
    enum_script.op_count = 6;
    enum_script.opcodes = enum_ops;
    enum_script.int_operands = enum_operands;
    TEST_ASSERT(test_run(&vm, &enum_script) == CS2VM_EXECNO_DONE, "enum script ok");
    TEST_ASSERT(vm.ints_stack_top == 1 && vm.ints_stack[0] == 1, "enum lookup echoes key");

    fprintf(stderr, "ok: cs2vmx array/enum script completes\n");
    return 0;
}

static int
test_gosub_return(void)
{
    static uint16_t callee_opcodes[] = { CS2_OP_PUSH_CONSTANT_INT, CS2_OP_RETURN };
    static int callee_operands[] = { 42, 0 };
    struct CS2_Script callee = { 0 };
    callee.script_id = 1;
    callee.op_count = 2;
    callee.opcodes = callee_opcodes;
    callee.int_operands = callee_operands;

    static uint16_t caller_opcodes[] = {
        CS2_OP_GOSUB_WITH_PARAMS,
        CS2_OP_RETURN,
    };
    static int caller_operands[] = { 1, 0 };
    struct CS2_Script caller = { 0 };
    caller.op_count = 2;
    caller.opcodes = caller_opcodes;
    caller.int_operands = caller_operands;

    struct CS2VMX vm;
    struct TestHost host;
    test_bind(&vm, &host);
    host.resolve_script = &callee;
    host.resolve_script_id = 1;

    TEST_ASSERT(test_run(&vm, &caller) == CS2VM_EXECNO_DONE, "gosub script ok");
    TEST_ASSERT(vm.ints_stack_top == 1 && vm.ints_stack[0] == 42, "gosub return on stack");

    fprintf(stderr, "ok: gosub/return resumes caller after subroutine\n");
    return 0;
}

static int
test_host_inventory_opcodes(void)
{
    enum
    {
        k_inv_id = 94,
        k_slot = 0,
        k_obj_id = 1153,
    };

    struct CS2VMX vm;
    struct TestHost host;
    test_bind(&vm, &host);
    host.inv_id = k_inv_id;
    host.inv_slot = k_slot;
    host.inv_obj_id = k_obj_id;
    host.inv_count = 1;
    host.inv_size = 28;

    static uint16_t opcodes[] = {
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_INV_GETOBJ,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_INV_SIZE,
        CS2_OP_RETURN,
    };
    static int operands[] = { k_inv_id, k_slot, 0, k_inv_id, 0, 0 };

    struct CS2_Script script = { 0 };
    script.op_count = 6;
    script.opcodes = opcodes;
    script.int_operands = operands;

    TEST_ASSERT(test_run(&vm, &script) == CS2VM_EXECNO_DONE, "inventory host script ok");
    TEST_ASSERT(vm.ints_stack_top == 2, "inv stack depth");
    TEST_ASSERT(vm.ints_stack[0] == k_obj_id, "inv_getobj value");
    TEST_ASSERT(vm.ints_stack[1] == 28, "inv_size value");

    fprintf(stderr, "ok: cs2 host inventory opcodes push fixture values\n");
    return 0;
}

static int
test_host_drag_dead_zone_opcodes(void)
{
    enum
    {
        k_component = 0x01830010,
        k_zone = 12,
        k_time = 3,
    };

    struct UITree* tree = uitree_new(4);
    TEST_ASSERT(tree != NULL, "uitree new");

    struct UINodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_GRAPHIC;
    spec.component_id = k_component;
    spec.width = 32;
    spec.height = 32;
    int32_t idx = uitree_push(tree, -1, &spec);
    TEST_ASSERT(idx >= 0, "component pushed");

    struct CS2VMX vm;
    struct TestHost host;
    test_bind(&vm, &host);
    host.tree = tree;

    /* IF_SETDRAGDEAD*: stack is [value, component] with component on top. */
    static uint16_t opcodes[] = {
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_IF_SETDRAGDEADZONE,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_IF_SETDRAGDEADTIME,
        CS2_OP_RETURN,
    };
    static int operands[] = { k_zone, k_component, 0, k_time, k_component, 0, 0 };

    struct CS2_Script script = { 0 };
    script.op_count = 7;
    script.opcodes = opcodes;
    script.int_operands = operands;

    TEST_ASSERT(test_run(&vm, &script) == CS2VM_EXECNO_DONE, "drag dead host script ok");
    TEST_ASSERT(tree->components[idx].drag_dead_zone == k_zone, "drag dead zone stored");
    TEST_ASSERT(tree->components[idx].drag_dead_time == k_time, "drag dead time stored");

    uitree_free(tree);
    fprintf(stderr, "ok: if_setdragdeadzone/time host opcodes store uitree fields\n");
    return 0;
}

static int
test_enum_getoutputcount_host_opcode(void)
{
    struct CS2VMX vm;
    struct TestHost host;
    test_bind(&vm, &host);

    static uint16_t opcodes[] = {
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_ENUM_GETOUTPUTCOUNT,
        CS2_OP_RETURN,
    };
    static int operands[] = { 99, 0, 0 };

    struct CS2_Script script = { 0 };
    script.op_count = 3;
    script.opcodes = opcodes;
    script.int_operands = operands;

    TEST_ASSERT(test_run(&vm, &script) == CS2VM_EXECNO_DONE, "enum_getoutputcount host script ok");
    TEST_ASSERT(vm.ints_stack_top == 1 && vm.ints_stack[0] == 0, "enum count fallback");
    fprintf(stderr, "ok: enum_getoutputcount pops enum id and pushes count\n");
    return 0;
}

static int
test_struct_param_host_opcode(void)
{
    struct CS2VMX vm;
    struct TestHost host;
    test_bind(&vm, &host);

    static uint16_t opcodes[] = {
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_STRUCT_PARAM,
        CS2_OP_RETURN,
    };
    static int operands[] = { 123, 456, 0, 0 };

    struct CS2_Script script = { 0 };
    script.op_count = 4;
    script.opcodes = opcodes;
    script.int_operands = operands;

    TEST_ASSERT(test_run(&vm, &script) == CS2VM_EXECNO_DONE, "struct_param host script ok");
    TEST_ASSERT(vm.ints_stack_top == 1 && vm.ints_stack[0] == 0, "struct param fallback");
    fprintf(stderr, "ok: struct_param pops struct id and pushes fallback int\n");
    return 0;
}

static int
test_hook_event_component_cc_sethide(void)
{
    enum
    {
        k_component = 0x000c0005,
        k_prev_active = 0x7abcdef,
    };

    struct UITree* tree = uitree_new(4);
    TEST_ASSERT(tree != NULL, "uitree new");

    struct UINodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = k_component;
    spec.width = 100;
    spec.height = 100;
    int32_t idx = uitree_push(tree, -1, &spec);
    TEST_ASSERT(idx >= 0, "layer pushed");
    TEST_ASSERT(tree->components[idx].behavior.hide == 0, "starts visible");

    struct CS2VMX vm;
    struct TestHost host;
    test_bind(&vm, &host);
    host.tree = tree;

    static uint16_t opcodes[] = {
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_CC_SETHIDE,
        CS2_OP_RETURN,
    };
    static int operands[] = { 1, 0, 0 };

    struct CS2_Script script = { 0 };
    script.op_count = 3;
    script.opcodes = opcodes;
    script.int_operands = operands;

    CS2VMX_ResetRuntime(&vm);
    vm.active_component_id = k_prev_active;
    vm.dot_component_id = k_prev_active;
    TEST_ASSERT(CS2VMX_PushCallScript(&vm, &script) == CS2VM_EXECNO_OK, "push script");
    CS2VMX_SetActiveAndDotComponentId(&vm, k_component);
    TEST_ASSERT(CS2VMX_RunScript(&vm) == CS2VM_EXECNO_DONE, "hook event cc_sethide script ok");
    TEST_ASSERT(tree->components[idx].behavior.hide == 1, "component hidden");

    /* Restore previous active/dot after hook (caller responsibility). */
    CS2VMX_SetActiveAndDotComponentId(&vm, k_prev_active);
    TEST_ASSERT(vm.active_component_id == k_prev_active, "active_component restored");
    TEST_ASSERT(vm.dot_component_id == k_prev_active, "dot_component restored");

    uitree_free(tree);
    fprintf(stderr, "ok: hook event_component_id enables CC_SETHIDE without IF_FIND\n");
    return 0;
}

static int
test_step_limit(void)
{
    static uint16_t loop_opcodes[] = {
        CS2_OP_BRANCH,
        CS2_OP_RETURN,
    };
    static int loop_operands[] = { -1, 0 };

    struct CS2_Script script = { 0 };
    script.op_count = 2;
    script.opcodes = loop_opcodes;
    script.int_operands = loop_operands;

    struct CS2VMX vm;
    struct TestHost host;
    test_bind(&vm, &host);

    int rc = test_run(&vm, &script);
    TEST_ASSERT(rc == CS2VM_EXECNO_ERROR, "infinite branch hits cycle limit");
    fprintf(stderr, "ok: cycle limit catches infinite branch loop\n");
    return 0;
}

int
main(void)
{
    int failures = test_runtime_branch();
    failures += test_runtime_array_and_enum();
    failures += test_gosub_return();
    failures += test_host_inventory_opcodes();
    failures += test_host_drag_dead_zone_opcodes();
    failures += test_enum_getoutputcount_host_opcode();
    failures += test_struct_param_host_opcode();
    failures += test_hook_event_component_cc_sethide();
    failures += test_step_limit();
    if( failures == 0 )
    {
        printf("All cs2vmx tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
