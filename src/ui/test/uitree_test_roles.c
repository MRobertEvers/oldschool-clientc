#include "test_harness.h"

#include "uitree_role.h"

#include <stdlib.h>

/*
 * Semantic roles: naming an element by what it IS, and finding it again after
 * the tree underneath has been rebuilt.
 *
 * The cases that matter are the ones a uid cannot survive. A CS2 rebuild --
 * CC_DELETEALL then CC_CREATE, which the chatbox does per message -- hands the
 * recycled uid straight back out, so anything that remembered one is now
 * pointing at a different component that looks entirely valid. A role
 * addressed as (parent, sub_id) has to come back with the NEW node, and a memo
 * that survived that rebuild would be the bug this exists to prevent.
 */

static struct UITreeRoleMatcher
matcher_slot(int slot, int member)
{
    struct UITreeRoleMatcher m;
    memset(&m, 0, sizeof(m));
    m.kind = UITREE_ROLE_MATCH_SLOT;
    m.slot = (int16_t)slot;
    m.member = member;
    return m;
}

static struct UITreeRoleMatcher
matcher_id(int uid)
{
    struct UITreeRoleMatcher m;
    memset(&m, 0, sizeof(m));
    m.kind = UITREE_ROLE_MATCH_ID;
    m.uid = uid;
    m.member = -1;
    return m;
}

static struct UITreeRoleMatcher
matcher_clientcode(int code)
{
    struct UITreeRoleMatcher m;
    memset(&m, 0, sizeof(m));
    m.kind = UITREE_ROLE_MATCH_CLIENTCODE;
    m.value = code;
    m.member = -1;
    return m;
}

static struct UITreeRoleMatcher
matcher_cc(int parent_uid, int sub_id)
{
    struct UITreeRoleMatcher m;
    memset(&m, 0, sizeof(m));
    m.kind = UITREE_ROLE_MATCH_CC;
    m.uid = parent_uid;
    m.value = sub_id;
    m.member = -1;
    return m;
}

static uint16_t
declare(struct UITreeRoleTable* table, char const* name, struct UITreeRoleMatcher m)
{
    uint16_t id = UITree_RoleIntern(table, name);
    TEST_ASSERT(id != 0, "intern returns a nonzero role id");
    TEST_ASSERT(UITree_RoleAddMatcher(table, id, &m), "matcher appended");
    return id;
}

/* A node carrying a clientCode, which no push helper sets. */
static int32_t
push_client_code(struct UITree* tree, int32_t parent, int component_id, int client_code)
{
    struct UITreeNodeSpec spec;
    struct UITreeBehavior behavior;
    memset(&spec, 0, sizeof(spec));
    memset(&behavior, 0, sizeof(behavior));
    behavior.client_code = client_code;
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = component_id;
    spec.width = 20;
    spec.height = 20;
    spec.has_position = 1;
    spec.behavior = &behavior;
    return UITree_Push(tree, parent, &spec);
}

/* A node stamped with an authored role, the way the revconfig bake does. */
static int32_t
push_authored(struct UITree* tree, int32_t parent, uint16_t role_id)
{
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = -1;
    spec.width = 30;
    spec.height = 30;
    spec.has_position = 1;
    spec.role_id = role_id;
    return UITree_Push(tree, parent, &spec);
}

static void
test_role_vocabulary(void)
{
    /* The slot names are the role spelling of enum UITreeFrameSlot; a rename on
     * one side and not the other is exactly the silent drift this guards. */
    TEST_ASSERT(
        UITree_RoleSlotFromName("viewport") == UITREE_FRAME_SLOT_VIEWPORT, "viewport name");
    TEST_ASSERT(UITree_RoleSlotFromName("minimap") == UITREE_FRAME_SLOT_MINIMAP, "minimap name");
    TEST_ASSERT(
        UITree_RoleSlotFromName("chat_buttons") == UITREE_FRAME_SLOT_CHAT_BUTTONS,
        "chat_buttons name");
    TEST_ASSERT(UITree_RoleSlotFromName("wibble") < 0, "an unknown slot name is refused");

    /* Chat filters have names; a sidebar tabno does not and is a number. */
    TEST_ASSERT(
        UITree_RoleSlotMemberFromName(UITREE_FRAME_SLOT_CHAT_BUTTONS, "report") == 3,
        "report is filter 3");
    TEST_ASSERT(
        UITree_RoleSlotMemberFromName(UITREE_FRAME_SLOT_CHAT_BUTTONS, "public") == 0,
        "public is filter 0");
    TEST_ASSERT(
        UITree_RoleSlotMemberFromName(UITREE_FRAME_SLOT_SIDEBAR, "10") == 10,
        "a sidebar member is its tabno");
    TEST_ASSERT(
        UITree_RoleSlotMemberFromName(UITREE_FRAME_SLOT_CHAT_BUTTONS, "wibble") < 0,
        "an unknown filter name is refused");
}

static void
test_role_table(void)
{
    struct UITreeRoleTable table;
    uint16_t a, b;
    memset(&table, 0, sizeof(table));

    a = UITree_RoleIntern(&table, "report_button");
    b = UITree_RoleIntern(&table, "logout_screen");
    TEST_ASSERT(a != 0 && b != 0 && a != b, "distinct names intern distinctly");
    TEST_ASSERT(
        UITree_RoleIntern(&table, "report_button") == a, "the same name interns to the same id");
    TEST_ASSERT(UITree_RoleFind(&table, "report_button") == a, "find agrees with intern");
    TEST_ASSERT(UITree_RoleFind(&table, "never_declared") == 0, "an unknown name is role 0");
    TEST_ASSERT(strcmp(UITree_RoleName(&table, a), "report_button") == 0, "name round-trips");
    TEST_ASSERT(UITree_RoleName(&table, 0) == NULL, "role 0 has no name");

    /* A chain is capped, and the rung past the cap is refused rather than
     * quietly dropped -- the caller is the one that must report it. */
    for( int i = 0; i < UITREE_ROLE_MAX_MATCHERS; i++ )
    {
        struct UITreeRoleMatcher m = matcher_id(1000 + i);
        TEST_ASSERT(UITree_RoleAddMatcher(&table, a, &m), "rung fits");
    }
    {
        struct UITreeRoleMatcher m = matcher_id(9999);
        TEST_ASSERT(!UITree_RoleAddMatcher(&table, a, &m), "a chain past the cap refuses");
    }

    UITree_RoleTableFree(&table);
}

static void
test_role_resolution(void)
{
    struct UITreeRoleTable table;
    struct UITree* tree = UITree_New(4);
    int const top_uid = (161 << 16) | 10;
    int32_t top, logout, authored_node;
    uint16_t r_id, r_code, r_authored, r_absent, r_chain;

    memset(&table, 0, sizeof(table));
    TEST_ASSERT(tree != NULL, "UITree_New");

    top = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, top_uid, 0, 0, 200, 200);
    logout = push_client_code(tree, top, (161 << 16) | 42, 205);
    TEST_ASSERT(logout >= 0, "pushed a clientCode node");

    /* id(): the plainest binding. */
    r_id = declare(&table, "logout_by_id", matcher_id((161 << 16) | 42));
    TEST_ASSERT(UITree_RoleNode(tree, &table, r_id) == logout, "id() resolves");

    /* clientcode(): the cache's own tag. */
    r_code = declare(&table, "logout_by_code", matcher_clientcode(205));
    TEST_ASSERT(UITree_RoleNode(tree, &table, r_code) == logout, "clientcode() resolves");

    /* An undeclared role, and a declared one nothing satisfies, both answer -1
     * -- an ANSWER, not a fault. */
    TEST_ASSERT(UITree_RoleNodeByName(tree, &table, "never_declared") < 0, "unknown role is -1");
    r_absent = declare(&table, "not_here", matcher_id((999 << 16) | 1));
    TEST_ASSERT(UITree_RoleNode(tree, &table, r_absent) < 0, "an unmatched chain is -1");

    /* The chain falls through in order: rung 0 misses, rung 1 answers. */
    r_chain = UITree_RoleIntern(&table, "chained");
    {
        struct UITreeRoleMatcher miss = matcher_id((999 << 16) | 2);
        struct UITreeRoleMatcher hit = matcher_clientcode(205);
        UITree_RoleAddMatcher(&table, r_chain, &miss);
        UITree_RoleAddMatcher(&table, r_chain, &hit);
    }
    TEST_ASSERT(
        UITree_RoleNode(tree, &table, r_chain) == logout, "a chain falls through to the next rung");

    /* The authored channel, and its precedence: a node stamped at bake wins
     * over a chain that would have found something else. */
    r_authored = UITree_RoleIntern(&table, "authored");
    authored_node = push_authored(tree, top, r_authored);
    TEST_ASSERT(authored_node >= 0, "pushed an authored node");
    UITree_RoleMarkAuthored(&table, r_authored);
    {
        struct UITreeRoleMatcher m = matcher_id((161 << 16) | 42);
        UITree_RoleAddMatcher(&table, r_authored, &m);
    }
    TEST_ASSERT(
        UITree_RoleNode(tree, &table, r_authored) == authored_node,
        "the authored tag outranks the chain");

    UITree_RoleTableFree(&table);
    UITree_Free(tree);
}

static void
test_role_dynamic_rebuild(void)
{
    struct UITreeRoleTable table;
    struct UITree* tree = UITree_New(4);
    int const parent_uid = (162 << 16) | 3;
    int32_t parent, first, second;
    uint16_t role;

    memset(&table, 0, sizeof(table));
    TEST_ASSERT(tree != NULL, "UITree_New");

    parent = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, parent_uid, 0, 0, 100, 100);

    role = declare(&table, "xp_counter", matcher_cc(parent_uid, 4));

    /* Nothing built yet: the parent is there and the child is not, which is a
     * state (the script has not run) and not a fault. */
    TEST_ASSERT(UITree_RoleNode(tree, &table, role) < 0, "cc() with no child yet is -1");

    first = UITree_CcCreate(tree, parent, parent_uid, UIELEM_RS_TEXT, 4);
    TEST_ASSERT(first >= 0, "cc_create sub 4");
    TEST_ASSERT(UITree_RoleNode(tree, &table, role) == first, "cc() finds the dynamic child");
    /* Asked twice: the memo has to return the same answer, not a different one. */
    TEST_ASSERT(UITree_RoleNode(tree, &table, role) == first, "cc() memo is stable");

    /*
     * The case the whole design turns on. A delete-all and rebuild recycles the
     * uid, so a role that remembered one would now be pointing at whatever was
     * built next. Resolving again has to produce the node that is there NOW.
     */
    UITree_CcDeleteAll(tree, parent);
    TEST_ASSERT(UITree_RoleNode(tree, &table, role) < 0, "the role goes with the deleted child");

    /* Rebuild with a decoy at a different sub id first, so a stale memo or a
     * "first dynamic child" shortcut would answer with the wrong node. */
    UITree_CcCreate(tree, parent, parent_uid, UIELEM_RS_RECT, 1);
    second = UITree_CcCreate(tree, parent, parent_uid, UIELEM_RS_TEXT, 4);
    TEST_ASSERT(second >= 0, "cc_create sub 4 again");
    TEST_ASSERT(
        UITree_RoleNode(tree, &table, role) == second,
        "cc() re-resolves to the rebuilt child, not the recycled uid");

    UITree_RoleTableFree(&table);
    UITree_Free(tree);
}

static void
test_role_slot_delegation(void)
{
    struct UITreeRoleTable table;
    struct UITree* tree = UITree_New(4);
    int32_t world;
    uint16_t r_view, r_report;

    memset(&table, 0, sizeof(table));
    TEST_ASSERT(tree != NULL, "UITree_New");

    world = UITree_TestPushXy(tree, -1, UIELEM_BUILTIN_WORLD, -1, 4, 4, 512, 334);
    TEST_ASSERT(world >= 0, "pushed a world node");

    /* A region role and the layout system must never be able to disagree about
     * which node they mean, so the role goes through the layout's own lookup. */
    r_view = declare(&table, "viewport", matcher_slot(UITREE_FRAME_SLOT_VIEWPORT, -1));
    TEST_ASSERT(
        UITree_RoleNode(tree, &table, r_view) == UITree_FrameSlotNode(tree, UITREE_FRAME_SLOT_VIEWPORT),
        "a slot() role answers exactly what the frame slot answers");

    /* A member this frame does not have is -1, not the region it belongs to. */
    r_report = declare(&table, "report_button", matcher_slot(UITREE_FRAME_SLOT_CHAT_BUTTONS, 3));
    TEST_ASSERT(
        UITree_RoleNode(tree, &table, r_report) < 0,
        "a member role on a frame with no chat buttons is -1");

    UITree_RoleTableFree(&table);
    UITree_Free(tree);
}

void
test_roles(void)
{
    printf("TEST: semantic roles\n");

    test_role_vocabulary();
    test_role_table();
    test_role_resolution();
    test_role_dynamic_rebuild();
    test_role_slot_delegation();
}
