/*
 * The per-owner owned-control budget.
 *
 * A plugin may hold 128 owned controls at once. The count used to be taken by
 * sweeping every component on every create; it is now kept as the controls
 * come and go, so these cases are really asking whether the two choke points
 * (the push that stamps an owner, and the reclaim that frees one) agree with
 * the truth after every way a control can disappear.
 */
#include "test_harness.h"

static int32_t owned_child(struct UITree* tree, int32_t parent, uint64_t owner, char const* key)
{
    return UITree_WidgetCreateGraphic(tree, UITree_RefAt(tree, parent), owner, key);
}

void test_owned_budget(void)
{
    struct UITree* tree = UITree_New(512);
    int32_t const root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 548 << 16, 0, 0, 765, 503);
    int32_t const host_a = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, (548 << 16) | 1, 0, 0, 100, 100);
    int32_t const host_b = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, (548 << 16) | 2, 0, 0, 100, 100);
    char key[32];
    int made = 0;

    printf("TEST: owned control budget\n");
    for( int i = 0; i < 128; i++ )
    {
        snprintf(key, sizeof(key), "k%d", i);
        if( owned_child(tree, host_a, 7, key) >= 0 ) made++;
    }
    TEST_ASSERT(made == 128, "an owner may hold its whole budget of controls");
    TEST_ASSERT(owned_child(tree, host_a, 7, "one-too-many") < 0, "the budget refuses the next control");

    /* A second owner has its own budget: the count is per owner, not per tree. */
    TEST_ASSERT(owned_child(tree, host_b, 9, "other") >= 0, "a second owner is not charged for the first");

    /* Removing one control makes room for exactly one more. */
    int32_t const doomed = UITree_WidgetCreateGraphic(tree, UITree_RefAt(tree, host_a), 7, "k0");
    TEST_ASSERT(doomed >= 0, "the key of an existing control resolves to it");
    TEST_ASSERT(UITree_WidgetRemove(tree, UITree_RefAt(tree, doomed), 7), "an owner may remove its control");
    TEST_ASSERT(owned_child(tree, host_a, 7, "after-remove") >= 0, "a removed control gives its slot back");
    TEST_ASSERT(owned_child(tree, host_a, 7, "still-full") < 0, "and only that one slot");

    /* Emptying a slot does NOT take the plugin's controls with it: that
     * exception is the whole reason the clear rebuilds the child list instead
     * of truncating it, and the budget has to agree with it. */
    UITree_Clear(tree);
    int32_t const root2 = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 548 << 16, 0, 0, 765, 503);
    int32_t const host_c = UITree_TestPushXy(tree, root2, UIELEM_RS_LAYER, (548 << 16) | 3, 0, 0, 100, 100);
    made = 0;
    for( int i = 0; i < 128; i++ )
    {
        snprintf(key, sizeof(key), "c%d", i);
        if( owned_child(tree, host_c, 7, key) >= 0 ) made++;
    }
    TEST_ASSERT(made == 128, "the budget is whole again after a tree clear");
    UITree_ClearChildren(tree, host_c);
    TEST_ASSERT(owned_child(tree, host_c, 7, "after-slot-cleared") < 0,
                "emptying a slot keeps the plugin's controls, so it frees no budget");
    TEST_ASSERT(UITree_WidgetCreateGraphic(tree, UITree_RefAt(tree, host_c), 7, "c0") >= 0,
                "and the controls themselves are still there, by key");

    /* Owner reset is the other bulk path: it removes every control of one
     * owner and leaves the other owner's alone. */
    for( int i = 0; i < 4; i++ )
    {
        snprintf(key, sizeof(key), "r%d", i);
        (void)owned_child(tree, host_c, 9, key);
    }
    UITree_WidgetResetOwner(tree, 7);
    made = 0;
    for( int i = 0; i < 128; i++ )
    {
        snprintf(key, sizeof(key), "n%d", i);
        if( owned_child(tree, host_c, 7, key) >= 0 ) made++;
    }
    TEST_ASSERT(made == 128, "an owner reset returns that owner's whole budget");
    TEST_ASSERT(owned_child(tree, host_c, 9, "second-owner-untouched") >= 0,
                "the other owner still has room, having never been reset");
    UITree_Free(tree);
}
