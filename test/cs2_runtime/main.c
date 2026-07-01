#include "vm/cs2vm.h"
#include "vm/cs2_opcode.h"

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
        CS2_OP_ENUM,
        CS2_OP_RETURN,
    };
    static int enum_operands[] = { 1, 99, 0, 0 };
    struct CS2_Script enum_script = { 0 };
    enum_script.op_count = 4;
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
    failures += test_step_limit();
    if( failures == 0 )
    {
        printf("All cs2vm tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
