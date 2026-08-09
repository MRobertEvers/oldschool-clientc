#include "test_harness.h"

#include <stdlib.h>

static uint32_t
count_hook_blocks(struct UITree const* tree)
{
    uint32_t n = 0;
    for( uint32_t i = 0; i < tree->component_count; i++ )
        if( !tree->components[i].freed && tree->components[i].runtime_hooks )
            n++;
    return n;
}

static int
free_list_len(struct UITree const* tree)
{
    int n = 0;
    for( int32_t i = tree->free_head; i >= 0; i = tree->components[i].free_next )
        n++;
    return n;
}

static int
hooks_have_interaction(struct UITreeRuntimeHooks const* hooks)
{
    return hooks->on_op.script_id > 0 || hooks->on_click.script_id > 0 ||
           hooks->on_hold.script_id > 0 || hooks->on_click_repeat.script_id > 0 ||
           hooks->on_release.script_id > 0 || hooks->on_target_enter.script_id > 0 ||
           hooks->on_target_leave.script_id > 0 || hooks->on_drag.script_id > 0 ||
           hooks->on_drag_complete.script_id > 0;
}

/* Mirror RS_CS2Host_ClearHooksForInterfaceGroup's per-node policy: clear
 * reactive slots, keep click/op/drag, free the block only when nothing
 * interactive remains. */
static void
clear_reactive_hooks_at(struct UITree* tree, int32_t idx)
{
    struct UITreeRuntimeHooks* hooks = tree->components[idx].runtime_hooks;
    if( !hooks )
        return;
    memset(&hooks->on_timer, 0, sizeof(hooks->on_timer));
    memset(&hooks->on_key, 0, sizeof(hooks->on_key));
    memset(&hooks->on_var_transmit, 0, sizeof(hooks->on_var_transmit));
    memset(&hooks->on_inv_transmit, 0, sizeof(hooks->on_inv_transmit));
    memset(&hooks->on_misc_transmit, 0, sizeof(hooks->on_misc_transmit));
    memset(&hooks->on_friend_transmit, 0, sizeof(hooks->on_friend_transmit));
    memset(&hooks->on_dialog_abort, 0, sizeof(hooks->on_dialog_abort));
    memset(&hooks->on_resize, 0, sizeof(hooks->on_resize));
    memset(&hooks->on_sub_change, 0, sizeof(hooks->on_sub_change));
    if( !hooks_have_interaction(hooks) )
        UITree_FreeHooksAt(tree, idx);
    else
        UITree_SyncHookMembership(tree, idx);
}

static void
clear_reactive_hooks_for_group(struct UITree* tree, int group_id)
{
    struct UITreeNodeSet const* gset = UITree_GroupNodes(tree, group_id);
    int gi;
    if( !gset )
        return;
    for( gi = 0; gi < gset->count; gi++ )
    {
        int32_t idx = gset->slots[gi];
        int cid = tree->components[idx].component_id;
        if( cid < 0 || ((cid >> 16) & 0xffff) != group_id )
            continue;
        if( tree->components[idx].runtime_hooks )
            clear_reactive_hooks_at(tree, idx);
    }
}

void
test_open_close_steady(void)
{
    struct UITree* tree = UITree_New(0);
    struct UITreeNodeSpec root_spec;
    struct UITreeNodeSpec panel_spec;
    struct UITreeNodeSpec leaf_spec;
    int32_t root;
    int32_t panel;
    uint32_t baseline_count;
    uint32_t baseline_hooks;
    int cycle;

    printf("TEST: open/close steady-state\n");

    memset(&root_spec, 0, sizeof(root_spec));
    root_spec.type = UIELEM_RS_LAYER;
    root_spec.component_id = (161 << 16) | 0;
    root_spec.width = 765;
    root_spec.height = 503;
    root = UITree_Push(tree, -1, &root_spec);
    TEST_ASSERT(root >= 0, "root push");

    memset(&panel_spec, 0, sizeof(panel_spec));
    panel_spec.type = UIELEM_RS_LAYER;
    panel_spec.component_id = (12 << 16) | 0;
    panel_spec.width = 400;
    panel_spec.height = 300;
    panel = UITree_Push(tree, root, &panel_spec);
    TEST_ASSERT(panel >= 0, "panel push");

    /* Mix of models (anim index) and hooked leaves (hook-block reclaim). */
    for( int i = 0; i < 40; i++ )
    {
        int32_t idx;
        memset(&leaf_spec, 0, sizeof(leaf_spec));
        leaf_spec.type = (i % 4 == 0) ? UIELEM_RS_MODEL : UIELEM_RS_RECT;
        leaf_spec.component_id = (12 << 16) | (i + 1);
        leaf_spec.width = 16;
        leaf_spec.height = 16;
        idx = UITree_Push(tree, panel, &leaf_spec);
        TEST_ASSERT(idx >= 0, "leaf push");
        if( i % 2 == 0 )
        {
            struct UITreeRuntimeHooks* hooks = UITree_HooksMut(&tree->components[idx]);
            hooks->on_timer.script_id = 1000 + i;
            hooks->on_click.script_id = 2000 + i;
            UITree_SyncHookMembership(tree, idx);
        }
    }

    {
        TEST_ASSERT(tree->models.count == 10, "model live set counts only RS_MODEL nodes");
        TEST_ASSERT(tree->timer_hooks.count == 20, "timer live set from hooked leaves");
        TEST_ASSERT(
            tree->models.count < (int)tree->component_count,
            "model set smaller than component_count");
        TEST_ASSERT(UITree_GroupPresent(tree, 12), "group 12 present");
        TEST_ASSERT(
            UITree_GroupNodes(tree, 12)->count == 41,
            "group 12 has panel + 40 leaves");
    }

    baseline_count = tree->component_count;
    baseline_hooks = count_hook_blocks(tree);
    TEST_ASSERT(baseline_hooks == 20, "hook blocks allocated");

    /* Open/close cycle: clear reactive hooks on "close"; interaction (on_click)
     * keeps the block. Remount re-arms timers. */
    for( cycle = 0; cycle < 8; cycle++ )
    {
        clear_reactive_hooks_for_group(tree, 12);
        TEST_ASSERT(
            count_hook_blocks(tree) == baseline_hooks,
            "close keeps interaction hook blocks");
        TEST_ASSERT(tree->timer_hooks.count == 0, "close drops timer live set");

        for( uint32_t i = 0; i < tree->component_count; i++ )
        {
            struct UITreeComponent* c = &tree->components[i];
            if( c->freed || ((c->component_id >> 16) & 0xffff) != 12 )
                continue;
            if( (c->component_id & 0xffff) == 0 )
                continue;
            if( ((c->component_id & 0xffff) - 1) % 2 != 0 )
                continue;
            {
                struct UITreeRuntimeHooks* hooks = UITree_HooksMut(c);
                hooks->on_timer.script_id = 1000;
                hooks->on_click.script_id = 2000;
                UITree_SyncHookMembership(tree, (int32_t)i);
            }
        }
        TEST_ASSERT(
            count_hook_blocks(tree) == baseline_hooks,
            "remount restores hook-block count");
        TEST_ASSERT(
            tree->timer_hooks.count == (int)baseline_hooks,
            "remount restores timer live set");
        TEST_ASSERT(
            tree->component_count == baseline_count,
            "hide-reuse keeps component_count flat across open/close");
    }

    /* Purely reactive leaf: close must free the block. */
    {
        int32_t idx;
        struct UITreeRuntimeHooks* hooks;
        memset(&leaf_spec, 0, sizeof(leaf_spec));
        leaf_spec.type = UIELEM_RS_RECT;
        leaf_spec.component_id = (12 << 16) | 99;
        leaf_spec.width = 8;
        leaf_spec.height = 8;
        idx = UITree_Push(tree, panel, &leaf_spec);
        TEST_ASSERT(idx >= 0, "reactive-only leaf");
        hooks = UITree_HooksMut(&tree->components[idx]);
        hooks->on_timer.script_id = 42;
        UITree_SyncHookMembership(tree, idx);
        clear_reactive_hooks_for_group(tree, 12);
        TEST_ASSERT(
            tree->components[idx].runtime_hooks == NULL,
            "reactive-only block freed on close");
    }

    /* CC_DELETEALL + CC_CREATE rebuild: free-list recycles, count stays flat. */
    {
        uint32_t after_rebuild;
        uint32_t before_dyn;
        int free_after_delete;
        for( int i = 0; i < 40; i++ )
        {
            int32_t idx = UITree_CcCreate(tree, panel, (12 << 16) | 0, 3, i);
            TEST_ASSERT(idx >= 0, "seed dynamic children");
        }
        before_dyn = tree->component_count;
        UITree_CcDeleteAll(tree, panel);
        free_after_delete = free_list_len(tree);
        TEST_ASSERT(free_after_delete > 0, "deleteall feeds free list");
        for( int i = 0; i < 40; i++ )
        {
            int32_t idx = UITree_CcCreate(tree, panel, (12 << 16) | 0, 3, i);
            TEST_ASSERT(idx >= 0, "rebuild dynamic children");
        }
        after_rebuild = tree->component_count;
        TEST_ASSERT(
            after_rebuild == before_dyn,
            "rebuild reuses free-list slots (component_count flat)");
    }

    UITree_Free(tree);
}

/* Compass-shaped case: gameframe dynamic child keeps on_op when a sibling
 * pack (bank) is cleared — the live failure was menu ops without on_op. */
void
test_clear_hooks_preserves_sibling_on_op(void)
{
    struct UITree* tree = UITree_New(0);
    struct UITreeNodeSpec spec;
    int32_t root;
    int32_t compass_parent;
    int32_t compass;
    int32_t panel;
    int32_t leaf;
    struct UITreeRuntimeHooks* hooks;

    printf("TEST: clear hooks preserves sibling on_op\n");

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = (161 << 16) | 0;
    root = UITree_Push(tree, -1, &spec);
    TEST_ASSERT(root >= 0, "gameframe root");

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = (161 << 16) | 31;
    compass_parent = UITree_Push(tree, root, &spec);
    TEST_ASSERT(compass_parent >= 0, "compassclick");

    /* script7560 creates sub 0 then sub 1; Look North lives on the dot (sub 1
     * → packed 161:0x8001). */
    TEST_ASSERT(
        UITree_CcCreate(tree, compass_parent, (161 << 16) | 31, 4, 0) >= 0,
        "compass active child");
    compass = UITree_CcCreate(tree, compass_parent, (161 << 16) | 31, 4, 1);
    TEST_ASSERT(compass >= 0, "compass dot child");
    TEST_ASSERT(
        tree->components[compass].component_id == ((161 << 16) | 0x8001),
        "compass id is 161:0x8001");
    hooks = UITree_HooksMut(&tree->components[compass]);
    hooks->on_op.script_id = 1050;
    strncpy(tree->components[compass].menu_options.ops[0], "Look North",
            UITREE_MENU_OPTION_LEN - 1);
    UITree_SyncHookMembership(tree, compass);

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = (12 << 16) | 0;
    panel = UITree_Push(tree, root, &spec);
    TEST_ASSERT(panel >= 0, "bank root");

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_RECT;
    spec.component_id = (12 << 16) | 1;
    leaf = UITree_Push(tree, panel, &spec);
    TEST_ASSERT(leaf >= 0, "bank leaf");
    hooks = UITree_HooksMut(&tree->components[leaf]);
    hooks->on_timer.script_id = 99;
    hooks->on_click.script_id = 88;
    UITree_SyncHookMembership(tree, leaf);

    clear_reactive_hooks_for_group(tree, 12);

    TEST_ASSERT(
        tree->components[compass].runtime_hooks != NULL &&
            tree->components[compass].runtime_hooks->on_op.script_id == 1050,
        "compass on_op survives sibling pack clear");
    TEST_ASSERT(
        tree->components[compass].menu_options.ops[0][0] != '\0',
        "compass Look North op text still present");
    TEST_ASSERT(
        tree->components[leaf].runtime_hooks != NULL &&
            tree->components[leaf].runtime_hooks->on_click.script_id == 88,
        "bank on_click kept");
    TEST_ASSERT(
        tree->components[leaf].runtime_hooks->on_timer.script_id == 0,
        "bank on_timer cleared");
    TEST_ASSERT(tree->timer_hooks.count == 0, "timer live set empty after clear");

    UITree_Free(tree);
}

static int
count_live_with_component_id(
    struct UITree const* tree,
    int component_id)
{
    int n = 0;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].freed )
            continue;
        if( tree->components[i].component_id == component_id )
            n++;
    }
    return n;
}

/* chatmodal close/replace must reclaim dialogue packs so remount cannot
 * keep a hidden copy whose string shadows FindByComponentId / ApplyText. */
void
test_chatmodal_reclaim_no_shadow_text(void)
{
    struct UITree* tree = UITree_New(0);
    struct UITreeNodeSpec spec;
    int32_t slot;
    int32_t root_a;
    int32_t text_a;
    int32_t root_b;
    int const group_a = 231;
    int const group_b = 217;
    int const text_cid = (group_a << 16) | 4;

    printf("TEST: chatmodal reclaim — no shadowed dialogue text\n");

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = UITREE_CHATBOX_CHATMODAL_UID;
    spec.width = 479;
    spec.height = 96;
    slot = UITree_Push(tree, -1, &spec);
    TEST_ASSERT(slot >= 0, "chatmodal slot");

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = (group_a << 16) | 0;
    spec.width = 479;
    spec.height = 96;
    root_a = UITree_Push(tree, slot, &spec);
    TEST_ASSERT(root_a >= 0, "dialogue A root");

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_TEXT;
    spec.component_id = text_cid;
    spec.width = 100;
    spec.height = 20;
    spec.u.rs_text.font_id = 1;
    spec.u.rs_text.text = "old-name";
    text_a = UITree_Push(tree, root_a, &spec);
    TEST_ASSERT(text_a >= 0, "dialogue A name text");
    TEST_ASSERT(UITree_ApplyText(tree, text_cid, "stuck-name"), "set stuck text");
    TEST_ASSERT(
        tree->components[text_a].u.rs_text.text &&
            strcmp(tree->components[text_a].u.rs_text.text, "stuck-name") == 0,
        "stuck text applied");
    TEST_ASSERT(count_live_with_component_id(tree, text_cid) == 1, "one live A name");

    /* Replace A with B under chatmodal — reclaim A (not hide). */
    UITree_ReclaimInterfaceGroup(tree, group_a);
    TEST_ASSERT(!UITree_GroupPresent(tree, group_a), "group A gone after reclaim");
    TEST_ASSERT(count_live_with_component_id(tree, text_cid) == 0, "no live A name");
    TEST_ASSERT(UITree_FindByComponentId(tree, text_cid) < 0, "FindById misses A");

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = (group_b << 16) | 0;
    spec.width = 479;
    spec.height = 96;
    root_b = UITree_Push(tree, slot, &spec);
    TEST_ASSERT(root_b >= 0, "dialogue B root");
    TEST_ASSERT(UITree_GroupPresent(tree, group_b), "group B present");

    /* Remount A: fresh bake must start empty until ApplyText. */
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = (group_a << 16) | 0;
    spec.width = 479;
    spec.height = 96;
    root_a = UITree_Push(tree, slot, &spec);
    TEST_ASSERT(root_a >= 0, "dialogue A remount root");

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_TEXT;
    spec.component_id = text_cid;
    spec.width = 100;
    spec.height = 20;
    spec.u.rs_text.font_id = 1;
    spec.u.rs_text.text = "";
    text_a = UITree_Push(tree, root_a, &spec);
    TEST_ASSERT(text_a >= 0, "dialogue A remount text");
    TEST_ASSERT(count_live_with_component_id(tree, text_cid) == 1, "single live A name");
    TEST_ASSERT(
        UITree_FindByComponentId(tree, text_cid) == text_a,
        "FindById hits remounted node");
    {
        char const* t = tree->components[text_a].u.rs_text.text;
        TEST_ASSERT(!t || t[0] == '\0', "remount text empty until ApplyText");
    }
    TEST_ASSERT(UITree_ApplyText(tree, text_cid, "fresh-name"), "apply fresh");
    TEST_ASSERT(
        tree->components[text_a].u.rs_text.text &&
            strcmp(tree->components[text_a].u.rs_text.text, "fresh-name") == 0,
        "ApplyText lands on remounted node");
    TEST_ASSERT(count_live_with_component_id(tree, text_cid) == 1, "still one live A name");

    UITree_Free(tree);
}
