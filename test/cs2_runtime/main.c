#include "vm/cs2vm.h"
#include "vm/cs2_opcode.h"
#include "vm/cs2_host_ui.h"
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

static int
test_runtime_branch(void)
{
    struct CS2VM* vm = cs2vm_new();
    TEST_ASSERT(vm != NULL, "cs2vm new");

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

    TEST_ASSERT(cs2vm_run(vm, &script, NULL, NULL) == CS2VM_OK, "branch script ok");
    cs2vm_free(vm);
    fprintf(stderr, "ok: cs2vm branch script completes\n");
    return 0;
}

static int
test_runtime_array_and_enum(void)
{
    struct CS2VM* vm = cs2vm_new();
    TEST_ASSERT(vm != NULL, "cs2vm new");

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
    static int operands[] = { 4, 0, 2, 42, 0, 2, 0, 0 };

    struct CS2_Script script = { 0 };
    script.op_count = 8;
    script.opcodes = opcodes;
    script.int_operands = operands;

    TEST_ASSERT(cs2vm_run(vm, &script, NULL, NULL) == CS2VM_OK, "array script ok");

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
    TEST_ASSERT(cs2vm_run(vm, &enum_script, NULL, NULL) == CS2VM_OK, "enum script ok");

    cs2vm_free(vm);
    fprintf(stderr, "ok: cs2vm array/enum script completes\n");
    return 0;
}

static struct CS2_Script s_callee_script;
static int s_gosub_result;

static void
test_host_set_varp(
    void* ud,
    int id,
    int value)
{
    (void)ud;
    if( id == 0 )
        s_gosub_result = value;
}

static struct CS2_Script*
test_host_resolve_script(
    void* ud,
    int script_id)
{
    (void)ud;
    if( script_id == 1 )
        return &s_callee_script;
    return NULL;
}

static int
test_gosub_return(void)
{
    static uint16_t callee_opcodes[] = { CS2_OP_PUSH_CONSTANT_INT, CS2_OP_RETURN };
    static int callee_operands[] = { 42, 0 };
    memset(&s_callee_script, 0, sizeof(s_callee_script));
    s_callee_script.script_id = 1;
    s_callee_script.op_count = 2;
    s_callee_script.opcodes = callee_opcodes;
    s_callee_script.int_operands = callee_operands;

    static uint16_t caller_opcodes[] = {
        CS2_OP_GOSUB_WITH_PARAMS,
        CS2_OP_POP_VAR,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_RETURN,
    };
    static int caller_operands[] = { 1, 0, 0, 0 };

    struct CS2_Script caller_script = { 0 };
    caller_script.op_count = 4;
    caller_script.opcodes = caller_opcodes;
    caller_script.int_operands = caller_operands;

    struct CS2Host host = {
        .set_varp = test_host_set_varp,
        .resolve_script = test_host_resolve_script,
    };

    struct CS2VM* vm = cs2vm_new();
    s_gosub_result = -1;
    TEST_ASSERT(cs2vm_run(vm, &caller_script, &host, NULL) == CS2VM_OK, "gosub script ok");
    TEST_ASSERT(s_gosub_result == 42, "gosub return value on caller stack");

    cs2vm_free(vm);
    fprintf(stderr, "ok: gosub/return resumes caller after subroutine\n");
    return 0;
}

static struct
{
    int inv_id;
    int slot;
    int obj_id;
    int count;
    int size;
} s_inv_fixture;

static int
test_inv_get_obj(
    void* ud,
    int inv_id,
    int slot)
{
    (void)ud;
    if( inv_id != s_inv_fixture.inv_id || slot != s_inv_fixture.slot )
        return 0;
    return s_inv_fixture.obj_id;
}

static int
test_inv_get_num(
    void* ud,
    int inv_id,
    int slot)
{
    (void)ud;
    if( inv_id != s_inv_fixture.inv_id || slot != s_inv_fixture.slot )
        return 0;
    return s_inv_fixture.count;
}

static int
test_inv_size(
    void* ud,
    int inv_id)
{
    (void)ud;
    if( inv_id != s_inv_fixture.inv_id )
        return 0;
    return s_inv_fixture.size;
}

static int
test_host_inventory_cc_obj(void)
{
    enum
    {
        k_parent_component = 0x0183000f,
        k_inv_id = 94,
        k_slot = 0,
        k_obj_id = 1153,
        k_sub_id = 1,
    };

    s_inv_fixture.inv_id = k_inv_id;
    s_inv_fixture.slot = k_slot;
    s_inv_fixture.obj_id = k_obj_id;
    s_inv_fixture.count = 1;
    s_inv_fixture.size = 1;

    struct UITree* tree = uitree_new(8);
    TEST_ASSERT(tree != NULL, "uitree new");

    struct UINodeSpec parent_spec;
    memset(&parent_spec, 0, sizeof(parent_spec));
    parent_spec.type = UIELEM_RS_LAYER;
    parent_spec.component_id = k_parent_component;
    parent_spec.width = 190;
    parent_spec.height = 261;
    int32_t parent_idx = uitree_push(tree, -1, &parent_spec);
    TEST_ASSERT(parent_idx >= 0, "parent layer pushed");

    struct CS2HostUIInitArgs host_args = {
        .tree = tree,
        .inv_get_obj = test_inv_get_obj,
        .inv_get_num = test_inv_get_num,
        .inv_size = test_inv_size,
    };

    struct CS2Host host;
    cs2_host_ui_init(&host, &host_args);

    static uint16_t opcodes[] = {
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_IF_FIND,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_INV_GETOBJ,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_CC_CREATE,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_CC_SETOBJECT,
        CS2_OP_RETURN,
    };
    static int operands[] = {
        k_parent_component,
        0,
        k_inv_id,
        k_slot,
        0,
        k_sub_id,
        1000,
        1,
        0,
        0,
    };

    struct CS2_Script script = { 0 };
    script.op_count = 10;
    script.opcodes = opcodes;
    script.int_operands = operands;

    struct CS2VM* vm = cs2vm_new();
    TEST_ASSERT(cs2vm_run(vm, &script, &host, NULL) == CS2VM_OK, "inventory host script ok");

    int found = 0;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].type != UIELEM_CC_OBJ )
            continue;
        if( tree->components[i].u.cc_obj.obj_id != k_obj_id )
            continue;
        if( tree->components[i].dynamic != 1 )
            continue;
        found = 1;
        break;
    }
    TEST_ASSERT(found, "cc_create+cc_setobject produced dynamic UIELEM_CC_OBJ");

    cs2vm_free(vm);
    uitree_free(tree);
    fprintf(stderr, "ok: cs2 host inventory opcodes create cc_obj node\n");
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

    struct CS2HostUIInitArgs host_args = { .tree = tree };
    struct CS2Host host;
    cs2_host_ui_init(&host, &host_args);

    static uint16_t opcodes[] = {
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_IF_SETDRAGDEADZONE,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_IF_SETDRAGDEADTIME,
        CS2_OP_RETURN,
    };
    static int operands[] = { 0, 0, 0, 0, 0, 0, 0 };

    struct CS2_Script script = { 0 };
    script.op_count = 7;
    script.opcodes = opcodes;
    script.int_operands = operands;

    struct CS2VM* vm = cs2vm_new();
    operands[0] = k_component;
    operands[1] = k_zone;
    operands[3] = k_component;
    operands[4] = k_time;
    TEST_ASSERT(cs2vm_run(vm, &script, &host, NULL) == CS2VM_OK, "drag dead host script ok");
    TEST_ASSERT(tree->components[idx].drag_dead_zone == k_zone, "drag dead zone stored");
    TEST_ASSERT(tree->components[idx].drag_dead_time == k_time, "drag dead time stored");

    cs2vm_free(vm);
    uitree_free(tree);
    fprintf(stderr, "ok: cc_setdragdeadzone/time host opcodes store uitree fields\n");
    return 0;
}

static int
test_enum_getoutputcount_host_opcode(void)
{
    struct CS2Host host;
    cs2_host_ui_init(&host, NULL);

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

    struct CS2VM* vm = cs2vm_new();
    TEST_ASSERT(cs2vm_run(vm, &script, &host, NULL) == CS2VM_OK, "enum_getoutputcount host script ok");
    cs2vm_free(vm);
    fprintf(stderr, "ok: enum_getoutputcount pops enum id and pushes count\n");
    return 0;
}

static int
test_struct_param_host_opcode(void)
{
    struct CS2Host host;
    cs2_host_ui_init(&host, NULL);

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

    struct CS2VM* vm = cs2vm_new();
    TEST_ASSERT(cs2vm_run(vm, &script, &host, NULL) == CS2VM_OK, "struct_param host script ok");
    cs2vm_free(vm);
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

    struct CS2HostUIInitArgs host_args = { .tree = tree };
    struct CS2Host host;
    cs2_host_ui_init(&host, &host_args);

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

    struct CS2VM* vm = cs2vm_new();
    TEST_ASSERT(vm != NULL, "cs2vm new");
    cs2vm_set_active_component(vm, k_prev_active);
    cs2vm_set_dot_component(vm, k_prev_active);

    struct CS2_RunArgs args = {
        .event_component_id = k_component,
    };
    TEST_ASSERT(
        cs2vm_run(vm, &script, &host, &args) == CS2VM_OK,
        "hook event cc_sethide script ok");
    TEST_ASSERT(tree->components[idx].behavior.hide == 1, "component hidden");
    TEST_ASSERT(
        cs2vm_get_active_component(vm) == k_prev_active,
        "active_component restored after run");
    TEST_ASSERT(
        cs2vm_get_dot_component(vm) == k_prev_active,
        "dot_component restored after run");

    cs2vm_free(vm);
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

    struct CS2VM* vm = cs2vm_new();
    int rc = cs2vm_run(vm, &script, NULL, NULL);
    TEST_ASSERT(rc == CS2VM_ERR_STEP_LIMIT, "infinite branch hits step limit");
    cs2vm_free(vm);
    fprintf(stderr, "ok: step limit catches infinite branch loop\n");
    return 0;
}

int
main(void)
{
    int failures = test_runtime_branch();
    failures += test_runtime_array_and_enum();
    failures += test_gosub_return();
    failures += test_host_inventory_cc_obj();
    failures += test_host_drag_dead_zone_opcodes();
    failures += test_enum_getoutputcount_host_opcode();
    failures += test_struct_param_host_opcode();
    failures += test_hook_event_component_cc_sethide();
    failures += test_step_limit();
    if( failures == 0 )
    {
        printf("All cs2vm tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
