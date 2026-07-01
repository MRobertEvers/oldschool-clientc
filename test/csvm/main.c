#include "vm/cs1vm.h"
#include "vm/cs1vm_level.h"
#include "vm/cs1vm_opcode.h"

#include <stdio.h>
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

static int g_varp[8];
static int g_varbit[8];

static int
test_get_varp(
    void* ud,
    int id)
{
    (void)ud;
    if( id < 0 || id >= 8 )
        return 0;
    return g_varp[id];
}

static int
test_get_varbit(
    void* ud,
    int id)
{
    (void)ud;
    if( id < 0 || id >= 8 )
        return 0;
    return g_varbit[id];
}

static int
test_get_stat_base_level(
    void* ud,
    int skill)
{
    (void)ud;
    return skill + 1;
}

static int
test_get_stat_xp_remaining(
    void* ud,
    int skill)
{
    (void)ud;
    return cs1vm_level_experience_for_base_level(skill + 1);
}

static int
test_get_inv_count(
    void* ud,
    int iface_id,
    int obj_id)
{
    (void)ud;
    (void)iface_id;
    if( obj_id == 100 )
        return 3;
    return 0;
}

static int
test_inv_contains(
    void* ud,
    int iface_id,
    int obj_id)
{
    (void)ud;
    (void)iface_id;
    return obj_id == 200 ? CS1VM_INV_CONTAINS_PRESENT : 0;
}

static int
test_varp_and_bit(void)
{
    struct CS1VM* vm = cs1vm_new();
    TEST_ASSERT(vm != NULL, "cs1vm_new");

    g_varp[5] = 0x0C;
    struct CS1Host host = {
        .get_varp = test_get_varp,
        .get_varbit = test_get_varbit,
    };

    int script[] = { CS1VM_OP_TEST_BIT, 5, 2, CS1VM_OP_END };
    int value = cs1vm_eval(vm, script, &host);
    TEST_ASSERT(value == 1, "testbit set");

    cs1vm_free(vm);
    return 0;
}

static int
test_arithmetic(void)
{
    struct CS1VM* vm = cs1vm_new();
    TEST_ASSERT(vm != NULL, "cs1vm_new");

    struct CS1Host host = { 0 };
    int script[] = { CS1VM_OP_PUSH_CONSTANT, 10, CS1VM_OP_SUBTRACT, CS1VM_OP_PUSH_CONSTANT, 3, CS1VM_OP_END };
    int value = cs1vm_eval(vm, script, &host);
    TEST_ASSERT(value == 7, "subtract");

    cs1vm_free(vm);
    return 0;
}

static int
test_inv_opcodes(void)
{
    struct CS1VM* vm = cs1vm_new();
    TEST_ASSERT(vm != NULL, "cs1vm_new");

    struct CS1Host host = {
        .get_inv_count = test_get_inv_count,
        .inv_contains = test_inv_contains,
    };

    int count_script[] = { CS1VM_OP_INV_COUNT, 0, 99, CS1VM_OP_END };
    TEST_ASSERT(cs1vm_eval(vm, count_script, &host) == 3, "inv_count obj+1");

    int contains_script[] = { CS1VM_OP_INV_CONTAINS, 0, 199, CS1VM_OP_END };
    TEST_ASSERT(cs1vm_eval(vm, contains_script, &host) == CS1VM_INV_CONTAINS_PRESENT, "inv_contains");

    cs1vm_free(vm);
    return 0;
}

static int
test_stat_xp_remaining(void)
{
    struct CS1VM* vm = cs1vm_new();
    TEST_ASSERT(vm != NULL, "cs1vm_new");

    struct CS1Host host = {
        .get_stat_base_level = test_get_stat_base_level,
        .get_stat_xp_remaining = test_get_stat_xp_remaining,
    };

    int script[] = { CS1VM_OP_STAT_XP_REMAINING, 0, CS1VM_OP_END };
    int value = cs1vm_eval(vm, script, &host);
    TEST_ASSERT(value == cs1vm_level_experience_for_base_level(1), "xp remaining");

    cs1vm_free(vm);
    return 0;
}

static int
test_script_length(void)
{
    int script[] = { CS1VM_OP_INV_COUNT, 1, 2, CS1VM_OP_PUSH_CONSTANT, 5, CS1VM_OP_END };
    TEST_ASSERT(cs1vm_script_length(script) == 6, "script length");
    return 0;
}

int
main(void)
{
    int failures = 0;
    failures += test_varp_and_bit();
    failures += test_arithmetic();
    failures += test_inv_opcodes();
    failures += test_stat_xp_remaining();
    failures += test_script_length();

    if( failures == 0 )
    {
        printf("All cs1vm tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
