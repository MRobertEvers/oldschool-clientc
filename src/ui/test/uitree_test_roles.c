#include "test_harness.h"

#include "uitree_role.h"
#include "uitree_scroll.h"

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

static int
role_emit_node_index(struct UITreeEmitBuffer const* emit, int32_t node)
{
    for( int i = 0; i < emit->count; i++ )
        if( emit->cmds[i].node_index == node )
            return i;
    return -1;
}

static int
role_emit_overlay_index(
    struct UITreeEmitBuffer const* emit,
    struct UITreeEntityOverlay const* item)
{
    for( int i = 0; i < emit->count; i++ )
        if( emit->cmds[i].kind == UITREE_EMIT_ENTITY_OVERLAY &&
            emit->cmds[i].entity_overlays == item )
            return i;
    return -1;
}

static void
test_role_replacement_overlay(void)
{
    struct UITree* tree = UITree_New(8);
    struct UITreeHost host;
    struct TestHostState state;
    struct UITreeEmitBuffer emit;
    struct UITreeEntityOverlay item;
    struct UITreeRoleOverlayGroup group;
    int32_t root, target, child_a, child_b, sibling;
    uint32_t target_incarnation;
    int overlay_at, child_a_at, child_b_at, sibling_at;
    int cx, cy, cw, ch;

    TEST_ASSERT(tree != NULL, "replacement overlay tree");
    UITree_TestHostInit(&host, &state);
    UITree_EmitBufferInit(&emit);
    root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, (500 << 16), 5, 7, 200, 140);
    target = UITree_TestPushXy(
        tree, root, UIELEM_RS_LAYER, (500 << 16) | 1, 10, 11, 90, 70);
    child_a = UITree_TestPushXy(
        tree, target, UIELEM_RS_RECT, (500 << 16) | 2, 14, 15, 20, 20);
    child_b = UITree_TestPushXy(
        tree, target, UIELEM_RS_RECT, (500 << 16) | 3, 40, 15, 20, 20);
    sibling = UITree_TestPushXy(
        tree, root, UIELEM_RS_RECT, (500 << 16) | 4, 120, 11, 30, 30);
    TEST_ASSERT(
        root >= 0 && target >= 0 && child_a >= 0 && child_b >= 0 && sibling >= 0,
        "replacement overlay fixture builds");
    tree->components[child_a].behavior.button_type = 1;
    tree->components[child_b].behavior.button_type = 1;
    UITree_TestResolve(tree);

    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_RECT;
    item.x = 12;
    item.y = 13;
    item.w = 75;
    item.h = 40;
    item.color = 0xff00ff00u;
    memset(&group, 0, sizeof(group));
    target_incarnation = tree->components[target].incarnation;
    group.node_index = target;
    group.node_incarnation = target_incarnation;
    group.replace = 1;
    group.items = &item;
    group.item_count = 1;
    state.role_overlay_groups = &group;
    state.role_overlay_group_count = 1;
    state.role_anchor_seen = 1;

    TEST_ASSERT(
        UITree_SetReplacementHidden(tree, target, target_incarnation, 1),
        "exact target incarnation accepts replacement suppression");
    /* A claim may become effective in the canvas prepass while the native
     * widget was already an armed drag source. Its deferred pass must not leak
     * a native ghost or duplicate the replacement overlay. */
    UITree_SetComponentDragActive(tree, target, 1);
    tree->components[target].drag_visual_x = 70;
    tree->components[target].drag_visual_y = 60;
    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);
    overlay_at = role_emit_overlay_index(&emit, &item);
    sibling_at = role_emit_node_index(&emit, sibling);
    TEST_ASSERT(role_emit_node_index(&emit, target) < 0, "replacement target does not paint");
    TEST_ASSERT(role_emit_node_index(&emit, child_a) < 0, "replacement prunes first descendant");
    TEST_ASSERT(role_emit_node_index(&emit, child_b) < 0, "replacement prunes full subtree");
    TEST_ASSERT(overlay_at >= 0 && overlay_at < sibling_at,
                "replacement paints at the target tombstone before its next sibling");
    TEST_ASSERT(
        UITree_NodePaintsAfterRoleBoundary(
            tree, &host, sibling, target, target_incarnation, true),
        "a later native sibling paints above a replacement tombstone");
    {
        int overlay_count = 0;
        for( int i = 0; i < emit.count; i++ )
            if( emit.cmds[i].entity_overlays == &item )
                overlay_count++;
        TEST_ASSERT(overlay_count == 1,
                    "replacement bypasses deferred drag without a duplicate tombstone");
    }
    TEST_ASSERT(emit.cmds[overlay_at].node_index < 0,
                "replacement paint does not inherit native interactive identity");
    UITree_LayoutGetBounds(&tree->components[root].position, &cx, &cy, &cw, &ch);
    TEST_ASSERT(
        emit.cmds[overlay_at].clip.x == cx && emit.cmds[overlay_at].clip.y == cy &&
            emit.cmds[overlay_at].clip.w == cw && emit.cmds[overlay_at].clip.h == ch,
        "replacement overlay uses the semantic target's parent clip");
    TEST_ASSERT(
        state.role_clip_updates == 1 && state.role_clip_node == target &&
            state.role_clip_incarnation == target_incarnation &&
            state.role_clip.clip_x == cx && state.role_clip.clip_y == cy &&
            state.role_clip.clip_w == cw && state.role_clip.clip_h == ch,
        "anchored hit regions receive the identical parent clip");
    UITree_LayoutGetBounds(&tree->components[child_a].position, &cx, &cy, &cw, &ch);
    TEST_ASSERT(
        UITree_HitTestInteractive(tree, &host, cx + 1, cy + 1) < 0,
        "replacement subtree contributes no native hit target");
    TEST_ASSERT(
        UITree_NodeOrAncestorDisplayHidden(tree, child_a),
        "replacement composes into display-hidden ancestry");
    TEST_ASSERT(
        !UITree_ComponentOrAncestorHidden(tree, tree->components[child_a].component_id),
        "cache/script activity remains live behind replacement suppression");
    UITree_SetComponentDragActive(tree, target, 0);

    /* Release restores native paint/input. The same explicit anchor without a
     * replacement is additive and lands after the ENTIRE multi-child subtree. */
    TEST_ASSERT(
        UITree_SetReplacementHidden(tree, target, target_incarnation, 0),
        "release reveals the exact target incarnation");
    group.replace = 0;
    state.role_clip_updates = 0;
    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);
    child_a_at = role_emit_node_index(&emit, child_a);
    child_b_at = role_emit_node_index(&emit, child_b);
    overlay_at = role_emit_overlay_index(&emit, &item);
    sibling_at = role_emit_node_index(&emit, sibling);
    TEST_ASSERT(
        child_a_at >= 0 && child_b_at > child_a_at && overlay_at > child_b_at &&
            overlay_at < sibling_at,
        "additive anchor follows every descendant and precedes the next sibling");
    TEST_ASSERT(
        !UITree_NodePaintsAfterRoleBoundary(
            tree, &host, child_b, target, target_incarnation, false),
        "an additive anchor paints above its own final descendant");
    TEST_ASSERT(
        UITree_NodePaintsAfterRoleBoundary(
            tree, &host, sibling, target, target_incarnation, false),
        "a later native sibling paints above an additive anchor");
    UITree_LayoutGetBounds(&tree->components[child_a].position, &cx, &cy, &cw, &ch);
    TEST_ASSERT(
        UITree_HitTestInteractive(tree, &host, cx + 1, cy + 1) == child_a,
        "release restores native hit testing");

    tree->components[target].behavior.hide = 1;
    UITree_MarkNodeVisibilityDirty(tree, target);
    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);
    TEST_ASSERT(role_emit_overlay_index(&emit, &item) < 0,
                "an explicitly hidden target drops additive anchor art");
    tree->components[target].behavior.hide = 0;
    UITree_MarkNodeVisibilityDirty(tree, target);

    TEST_ASSERT(
        UITree_ApplySize(tree, tree->components[target].component_id, 0, 0),
        "target layer collapses");
    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);
    TEST_ASSERT(role_emit_overlay_index(&emit, &item) < 0,
                "a collapsed clipping target drops its anchor");
    TEST_ASSERT(
        UITree_ApplySize(tree, tree->components[target].component_id, 90, 70),
        "target layer expands again");

    TEST_ASSERT(
        !UITree_SetReplacementHidden(tree, target, target_incarnation + 1, 1) &&
            !tree->components[target].replacement_hidden,
        "a stale incarnation cannot suppress a live recycled slot");
    group.node_incarnation = target_incarnation + 1;
    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);
    TEST_ASSERT(role_emit_overlay_index(&emit, &item) < 0,
                "anchor art for a stale incarnation is dropped safely");
    group.node_index = -1;
    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);
    TEST_ASSERT(role_emit_overlay_index(&emit, &item) < 0,
                "an absent target never falls back to the global canvas");

    TEST_ASSERT(emit.volatile_unrefreshable,
                "role anchors force a full retained walk while declared");
    UITree_EmitBufferFree(&emit);
    UITree_Free(tree);
}

static void
test_role_boundary_deferred_drag(void)
{
    struct UITree* tree = UITree_New(4);
    struct UITreeHost host;
    struct TestHostState state;
    int32_t root, dragged, anchor;

    TEST_ASSERT(tree != NULL, "role drag-order tree");
    UITree_TestHostInit(&host, &state);
    root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, (501 << 16), 0, 0, 200, 140);
    dragged = UITree_TestPushXy(
        tree, root, UIELEM_RS_RECT, (501 << 16) | 1, 10, 10, 40, 40);
    anchor = UITree_TestPushXy(
        tree, root, UIELEM_RS_RECT, (501 << 16) | 2, 10, 10, 40, 40);
    TEST_ASSERT(root >= 0 && dragged >= 0 && anchor >= 0,
                "role drag-order fixture builds");
    UITree_TestResolve(tree);

    TEST_ASSERT(
        !UITree_NodePaintsAfterRoleBoundary(
            tree,
            &host,
            dragged,
            anchor,
            tree->components[anchor].incarnation,
            false),
        "an earlier ordinary sibling paints below the additive anchor");
    tree->components[dragged].drag_behavior = 0;
    UITree_SetComponentDragActive(tree, dragged, 1);
    TEST_ASSERT(
        UITree_NodePaintsAfterRoleBoundary(
            tree,
            &host,
            dragged,
            anchor,
            tree->components[anchor].incarnation,
            false),
        "the deferred drag pass paints an earlier sibling above the anchor");

    UITree_Free(tree);
}

static void
test_role_boundary_input_covers(void)
{
    struct UITree* tree = UITree_New(8);
    struct UITreeHost host;
    struct TestHostState state;
    int32_t root, under_blocker, anchor, over_blocker;
    uint32_t anchor_incarnation;

    TEST_ASSERT(tree != NULL, "role input-cover tree");
    UITree_TestHostInit(&host, &state);
    root = UITree_TestPushXy(
        tree, -1, UIELEM_RS_LAYER, (502 << 16), 0, 0, 200, 140);
    under_blocker = UITree_TestPushXy(
        tree, root, UIELEM_RS_LAYER, (502 << 16) | 1, 10, 10, 60, 60);
    anchor = UITree_TestPushXy(
        tree, root, UIELEM_RS_RECT, (502 << 16) | 2, 10, 10, 60, 60);
    over_blocker = UITree_TestPushXy(
        tree, root, UIELEM_RS_LAYER, (502 << 16) | 3, 10, 10, 60, 60);
    TEST_ASSERT(
        root >= 0 && under_blocker >= 0 && anchor >= 0 && over_blocker >= 0,
        "role input-cover fixture builds");
    tree->components[under_blocker].no_click_through = 1;
    tree->components[over_blocker].no_click_through = 1;
    UITree_TestResolve(tree);
    anchor_incarnation = tree->components[anchor].incarnation;

    TEST_ASSERT(
        UITree_HitTestInteractive(tree, &host, 20, 20) < 0,
        "blank noClickThrough cover has no ordinary interactive hit");
    TEST_ASSERT(
        UITree_PointInputCoverPaintsAfterRoleBoundary(
            tree, &host, 20, 20, anchor, anchor_incarnation, false),
        "blank native cover after an additive role boundary occludes it");
    TEST_ASSERT(
        UITree_PointHasNativeInputCover(tree, &host, 20, 20),
        "FRAME input fence sees a blank native cover");
    TEST_ASSERT(
        UITree_SetReplacementHidden(tree, anchor, anchor_incarnation, 1) &&
            UITree_PointInputCoverPaintsAfterRoleBoundary(
                tree, &host, 20, 20, anchor, anchor_incarnation, true),
        "blank native cover also occludes a replacement tombstone");
    TEST_ASSERT(
        UITree_SetReplacementHidden(tree, anchor, anchor_incarnation, 0),
        "replacement cover fixture reveals its anchor");

    tree->components[over_blocker].behavior.hide = 1;
    UITree_MarkNodeVisibilityDirty(tree, over_blocker);
    TEST_ASSERT(
        !UITree_PointInputCoverPaintsAfterRoleBoundary(
            tree, &host, 20, 20, anchor, anchor_incarnation, false),
        "a blocker painted below the role boundary does not occlude it");
    tree->components[over_blocker].behavior.hide = 0;
    tree->components[over_blocker].no_click_through = 0;
    tree->components[over_blocker].behavior.click_mask = 1;
    UITree_MarkNodeVisibilityDirty(tree, over_blocker);
    TEST_ASSERT(
        UITree_PointInputCoverPaintsAfterRoleBoundary(
            tree, &host, 20, 20, anchor, anchor_incarnation, false),
        "an interactive native sibling painted later occludes the role region");
    TEST_ASSERT(
        !UITree_PointInputCoverPaintsAfterRoleBoundary(
            tree, &host, 100, 100, anchor, anchor_incarnation, false),
        "native covers outside the queried plugin pixel do not occlude it");
    UITree_Free(tree);

    /*
     * An open inventory is native cover, so a FRAME plugin region laid over the
     * panel does not take the pointer from the items in it.
     *
     * The grid is the one target that reaches neither half of the ordinary
     * predicate: a TYPE_INV node carries cols/rows in its layout box -- 4x7 --
     * so the point below is inside slot 0's 32x32 rect and far outside the node,
     * and the type is pass-through besides. The fence answered "nothing native
     * here" over every open inventory, and mobile-gameframe's drawer blocker
     * (a FRAME region over its own panel, there to stop taps reaching the world
     * behind a floating frame) won the point over every item: the menu build
     * drops the game's rows wholesale under a region, so an inventory item
     * offered Cancel and nothing else.
     */
    {
        struct UITree* inv = UITree_New(6);
        int32_t grid;

        TEST_ASSERT(inv != NULL, "role inv-cover tree");
        grid = UITree_TestPushXy(inv, -1, UIELEM_RS_INV, (504 << 16), 70, 50, 4, 7);
        TEST_ASSERT(grid >= 0, "role inv-cover fixture builds");
        inv->components[grid].u.rs_inv.cols = 4;
        inv->components[grid].u.rs_inv.rows = 7;
        inv->components[grid].u.rs_inv.margin_x = 0;
        inv->components[grid].u.rs_inv.margin_y = 0;
        UITree_TestResolve(inv);

        /* (85,65) is inside slot 0's rect (70..102, 50..82) and past the node
         * box (70..74, 50..57). */
        TEST_ASSERT(
            UITree_PointHasNativeInputCover(inv, &host, 85, 65),
            "FRAME input fence sees an inventory slot as native cover");
        TEST_ASSERT(
            !UITree_PointHasNativeInputCover(inv, &host, 300, 300),
            "and sees none past the grid's last slot");
        UITree_Free(inv);
    }

    /* Type-0 InterfaceParent barriers sit after ordinary host children and
     * before mounted roots even when that modal root has no interactive node. */
    {
        struct UITree* modal = UITree_New(6);
        int const host_uid = (503 << 16);
        int32_t host_node, ordinary_anchor;

        TEST_ASSERT(modal != NULL, "role modal-cover tree");
        host_node = UITree_TestPushXy(
            modal, -1, UIELEM_RS_LAYER, host_uid, 0, 0, 200, 140);
        ordinary_anchor = UITree_TestPushXy(
            modal, host_node, UIELEM_RS_RECT, host_uid | 1, 10, 10, 60, 60);
        (void)UITree_TestPushXy(
            modal, host_node, UIELEM_RS_LAYER, (603 << 16), 20, 20, 40, 40);
        (void)UITree_InterfaceParentSet(modal, host_uid, 603, 0);
        UITree_TestResolve(modal);
        TEST_ASSERT(
            UITree_PointInputCoverPaintsAfterRoleBoundary(
                modal,
                &host,
                100,
                60,
                ordinary_anchor,
                modal->components[ordinary_anchor].incarnation,
                false),
            "blank type-0 modal boundary occludes an earlier local overlay");

        (void)UITree_InterfaceParentSet(modal, host_uid, 603, 1);
        TEST_ASSERT(
            !UITree_PointInputCoverPaintsAfterRoleBoundary(
                modal,
                &host,
                100,
                60,
                ordinary_anchor,
                modal->components[ordinary_anchor].incarnation,
                false),
            "non-modal overlay mount does not invent an input cover");
        UITree_Free(modal);
    }
}

static void
test_role_drawn_bounds_follow_scroll_and_drag(void)
{
    struct UITree* tree = UITree_New(4);
    int32_t root;
    int32_t dragged;
    int32_t child;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    TEST_ASSERT(tree != NULL, "UITree_New");
    root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 700 << 16, 10, 20, 100, 80);
    dragged = UITree_TestPushXy(
        tree, root, UIELEM_RS_RECT, (700 << 16) | 1, 5, 50, 30, 20);
    child = UITree_TestPushXy(
        tree, dragged, UIELEM_RS_RECT, (700 << 16) | 2, 7, 9, 10, 8);
    TEST_ASSERT(root >= 0 && dragged >= 0 && child >= 0, "drawn-bounds fixture");
    tree->components[root].u.rs_layer.scroll_height = 200;
    tree->components[root].scroll_y = 30;
    UITree_TestResolve(tree);

    TEST_ASSERT(
        UITree_NodeDrawnBounds(tree, child, &x, &y, &w, &h) &&
            x == 22 && y == 49 && w == 10 && h == 8,
        "role geometry subtracts ancestor scroll like native paint");

    tree->components[dragged].drag_behavior = 0;
    tree->components[dragged].drag_visual_x = 80;
    tree->components[dragged].drag_visual_y = 90;
    UITree_SetComponentDragActive(tree, dragged, 1);
    TEST_ASSERT(
        UITree_NodeDrawnBounds(tree, dragged, &x, &y, &w, &h) &&
            x == 80 && y == 90 && w == 30 && h == 20,
        "an anchored drag source reports its visual box");
    TEST_ASSERT(
        UITree_NodeDrawnBounds(tree, child, &x, &y, &w, &h) &&
            x == 87 && y == 99 && w == 10 && h == 8,
        "ancestor drag translation carries through anchored descendants");

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
    test_role_replacement_overlay();
    test_role_boundary_deferred_drag();
    test_role_boundary_input_covers();
    test_role_drawn_bounds_follow_scroll_and_drag();
}
