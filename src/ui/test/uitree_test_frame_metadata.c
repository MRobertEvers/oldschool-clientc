#include "test_harness.h"
#include "engine/torirs_types.h"

void test_frame_authored_metadata(void)
{
    struct ToriRS_Component components[2] = { 0 };
    struct ToriRS_ComponentPack pack = { components, 2 };
    struct ToriRS_ScriptHook load = { .argc = 3, .argv = { 901, -2147483645, 8765 } };
    int control = -1, value = -1;
    printf("TEST: frame authored metadata\n");
    /* Neither the root nor the enum exists in the old hardcoded table. */
    components[1].id = (777 << 16) | 42;
    components[1].hooks[TORIRS_COMPONENT_HOOK_LOAD] = &load;
    TEST_ASSERT(ToriRS_ComponentPackLoadInt(&pack, 901, 2, &control, &value) &&
                control == components[1].id && value == 8765,
                "the authored onload supplies an unfamiliar root's enum");
    load.argv[2] = 9876;
    TEST_ASSERT(ToriRS_ComponentPackLoadInt(&pack, 901, 2, &control, &value) && value == 9876,
                "a changed authored enum is read without a root table update");
    value = 9876;
    load.str_mask = UINT64_C(1) << 2;
    TEST_ASSERT(!ToriRS_ComponentPackLoadInt(&pack, 901, 2, &control, &value) && value == 9876,
                "a string argument cannot masquerade as an enum id");
    load.str_mask = 0;
    components[0].hooks[TORIRS_COMPONENT_HOOK_LOAD] = &load;
    TEST_ASSERT(!ToriRS_ComponentPackLoadInt(&pack, 901, 2, &control, &value),
                "ambiguous control declarations are rejected");
    components[0].hooks[TORIRS_COMPONENT_HOOK_LOAD] = NULL;
    load.argc = 2;
    TEST_ASSERT(!ToriRS_ComponentPackLoadInt(&pack, 901, 2, &control, &value),
                "a truncated declaration cannot supply the enum");
}
