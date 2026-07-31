#include "test_harness.h"

/* The per-component runtime param table behind CS2's CC_SETCOMPONENTPARAM /
 * CC_GETCOMPONENTPARAM (OldSchool wire 1704/1703).
 *
 * The store is what makes the gameframe's widget tagging work: a script builds a
 * row, writes "kind 600, index 4" onto it, and later cc_finds the row and reads
 * the tags back to decide what a click meant. Three things it has to get right,
 * and all three are checked here: a write under an id already present replaces
 * rather than appends (or a re-tagged row would answer with its first value
 * forever), a miss is distinguishable from a stored 0 (the callers turn a miss
 * into the ParamType default, which is how their `= -1` guards read as "never
 * tagged"), and a reclaimed slot comes back empty — CC_DELETEALL recycles
 * component storage, so a stale table would hand the next widget the previous
 * one's identity.
 */
void
test_component_params(void)
{
    printf("TEST: component runtime params\n");

    struct UITree* tree = UITree_New(4);
    TEST_ASSERT(tree != NULL, "UITree_New");

    int const parent_id = (161 << 16) | 10;
    int const child_id = (161 << 16) | 11;
    int32_t parent = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, parent_id, 0, 0, 200, 200);
    int32_t child = UITree_TestPushXy(tree, parent, UIELEM_RS_RECT, child_id, 0, 0, 10, 10);
    TEST_ASSERT(parent >= 0 && child >= 0, "push nodes");

    int value = 12345;
    TEST_ASSERT(
        !UITree_ComponentParamGet(tree, child_id, 2365, &value),
        "unwritten param misses");
    TEST_ASSERT(value == 12345, "a miss leaves the out value alone");

    TEST_ASSERT(UITree_ApplyComponentParam(tree, child_id, 2365, 600, NULL), "write 2365");
    TEST_ASSERT(UITree_ApplyComponentParam(tree, child_id, 2370, 4, NULL), "write 2370");
    TEST_ASSERT(UITree_ApplyComponentParam(tree, child_id, 2349, 0, NULL), "write 2349 = 0");

    TEST_ASSERT(UITree_ComponentParamGet(tree, child_id, 2365, &value) && value == 600, "read 2365");
    TEST_ASSERT(UITree_ComponentParamGet(tree, child_id, 2370, &value) && value == 4, "read 2370");
    TEST_ASSERT(
        UITree_ComponentParamGet(tree, child_id, 2349, &value) && value == 0,
        "a stored 0 is a hit, not a miss");

    /* Replace, not append: the second write under 2365 is the one that answers. */
    TEST_ASSERT(UITree_ApplyComponentParam(tree, child_id, 2365, 605, NULL), "rewrite 2365");
    TEST_ASSERT(
        UITree_ComponentParamGet(tree, child_id, 2365, &value) && value == 605,
        "rewrite replaces");
    TEST_ASSERT(
        tree->components[child].params_count == 3,
        "rewrite does not grow the table");

    /* Past the initial capacity, since the table starts unallocated and grows. */
    for( int i = 0; i < 16; i++ )
        TEST_ASSERT(UITree_ApplyComponentParam(tree, child_id, 3000 + i, 100 + i, NULL), "write many");
    for( int i = 0; i < 16; i++ )
    {
        TEST_ASSERT(
            UITree_ComponentParamGet(tree, child_id, 3000 + i, &value) && value == 100 + i,
            "read many");
    }
    TEST_ASSERT(
        UITree_ComponentParamGet(tree, child_id, 2365, &value) && value == 605,
        "growth keeps earlier entries");

    /* String params share the table with int ones (the bank's row builder writes
     * both onto the same widget), and the int getter must not answer with a
     * string entry's unset `value`. */
    TEST_ASSERT(
        UITree_ApplyComponentParam(tree, child_id, 1017, 0, "Bank of Gielinor"),
        "write a string param");
    TEST_ASSERT(
        UITree_ComponentParamGetStr(tree, child_id, 1017) != NULL &&
            strcmp(UITree_ComponentParamGetStr(tree, child_id, 1017), "Bank of Gielinor") == 0,
        "read the string back");
    TEST_ASSERT(
        !UITree_ComponentParamGet(tree, child_id, 1017, &value),
        "the int getter treats a string entry as absent");
    TEST_ASSERT(
        UITree_ComponentParamGetStr(tree, child_id, 2365) == NULL,
        "the string getter treats an int entry as absent");
    TEST_ASSERT(
        UITree_ApplyComponentParam(tree, child_id, 1017, 0, "Grand Exchange"),
        "rewrite the string param");
    TEST_ASSERT(
        strcmp(UITree_ComponentParamGetStr(tree, child_id, 1017), "Grand Exchange") == 0,
        "the rewrite replaces the string");

    /* The parent is a different component: tags do not leak across nodes. */
    TEST_ASSERT(!UITree_ComponentParamGet(tree, parent_id, 2365, &value), "parent has no tags");

    /* A component that is not in the tree at all. */
    TEST_ASSERT(
        !UITree_ApplyComponentParam(tree, (999 << 16) | 1, 2365, 1, NULL),
        "write to a missing component fails");
    TEST_ASSERT(
        !UITree_ComponentParamGet(tree, (999 << 16) | 1, 2365, &value),
        "read from a missing component misses");

    /* CC_DELETEALL recycles the slot; whatever cc_create lands there next starts
     * clean. This is the pairing the gameframe actually runs — a panel rebuild is
     * a deleteall followed by a run of cc_creates that re-tag as they go. */
    int32_t dyn = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_TEXT, 0);
    TEST_ASSERT(dyn >= 0, "cc_create");
    int const dyn_id = tree->components[dyn].component_id;
    TEST_ASSERT(UITree_ApplyComponentParam(tree, dyn_id, 2365, 601, NULL), "tag the dynamic child");

    UITree_CcDeleteAll(tree, parent);
    int32_t reused = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_TEXT, 0);
    TEST_ASSERT(reused == dyn, "the reclaimed slot is the one handed back");
    TEST_ASSERT(
        tree->components[reused].params_count == 0 && tree->components[reused].params == NULL,
        "a reclaimed slot carries no params forward");
    TEST_ASSERT(
        !UITree_ComponentParamGet(tree, tree->components[reused].component_id, 2365, &value),
        "the recycled component reads as untagged");

    UITree_Free(tree);
}
