#include "test_harness.h"

#include "input/torirs_input.h"
#include "uitree_interact.h"

#include <stdlib.h>

void
test_click_event_coords(void)
{
    struct UITree* tree;
    struct TestHostState hs;
    struct UITreeHost host;
    struct UIInteraction interact;
    struct LibToriRS_Input storage;
    struct LibToriRS_Input* input;
    struct UIInteractOut out;
    int32_t track;
    int found = 0;

    printf("TEST: onClick event mouse is hook-component relative\n");
    tree = UITree_New(8);
    UITree_TestHostInit(&host, &hs);
    track = UITree_TestPushXy(tree, -1, UIELEM_RS_RECT, 700, 100, 40, 200, 20);
    tree->components[track].if3 = 1;
    UITree_HooksMut(&tree->components[track])->on_click.script_id = 9226;
    UITree_TestResolve(tree);

    UIInteraction_Init(&interact);
    input = LibToriRS_Input_Init(&storage, 0);
    LibToriRS_Input_Begin(input, 0);
    LibToriRS_Input_PushMouseMove(input, 160, 47);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 160, 47);
    LibToriRS_Input_End(input);
    UITree_InteractFrame(&interact, tree, &host, input, 0, &out);

    for( int i = 0; i < out.intent_count; i++ )
    {
        if( !out.intents[i].hook || out.intents[i].hook->script_id != 9226 )
            continue;
        found = 1;
        TEST_ASSERT(out.intents[i].is_click, "onClick intent is classified as a click");
        TEST_ASSERT(out.intents[i].has_event_mouse, "onClick carries event mouse coordinates");
        TEST_ASSERT(out.intents[i].event_mouse_x == 60, "event_mousex is relative to track");
        TEST_ASSERT(out.intents[i].event_mouse_y == 7, "event_mousey is relative to track");
    }
    TEST_ASSERT(found, "slider-like track dispatches its onClick hook");
    UITree_Free(tree);
}

/*
 * An overlay drawn over the tree in the same canvas owns the pointer: the
 * component under it is neither hovered, clicked, nor right-clicked.
 *
 * The client's regression is the plugin window rasterised into the game frame
 * (the buffer executor). Its clicks reached the chrome AND the game beneath
 * it, because "the chrome took this" cannot be read downstream off a consumed
 * flag the shell sets on every frame.
 */
void
test_pointer_owner_blocks_tree(void)
{
    struct UITree* tree;
    struct TestHostState hs;
    struct UITreeHost host;
    struct UIInteraction interact;
    struct LibToriRS_Input storage;
    struct LibToriRS_Input* input;
    struct UIInteractOut out;
    int32_t btn;
    int clicked = 0;

    printf("TEST: an owned pointer reaches no component\n");
    tree = UITree_New(8);
    UITree_TestHostInit(&host, &hs);
    btn = UITree_TestPushXy(tree, -1, UIELEM_RS_RECT, 700, 100, 40, 200, 20);
    tree->components[btn].if3 = 1;
    UITree_HooksMut(&tree->components[btn])->on_click.script_id = 9226;
    /* A hover hook as well, so "was it hovered" is a question with a yes: the
     * hover walk reports components the reference would offer options for. */
    UITree_HooksMut(&tree->components[btn])->on_mouse_over.script_id = 9227;
    UITree_TestResolve(tree);

    UIInteraction_Init(&interact);
    input = LibToriRS_Input_Init(&storage, 0);
    LibToriRS_Input_Begin(input, 0);
    LibToriRS_Input_PushMouseMove(input, 160, 47);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 160, 47);
    LibToriRS_Input_End(input);
    UITree_InteractFrameWithPointerOwner(&interact, tree, &host, input, 0, 0, 1, &out);

    for( int i = 0; i < out.intent_count; i++ )
        if( out.intents[i].hook && out.intents[i].hook->script_id == 9226 )
            clicked = 1;
    TEST_ASSERT(!clicked, "a press under the overlay runs no onClick");
    TEST_ASSERT(out.hover_com_id == -1, "nothing under the overlay is hovered");

    /* And no Choose Option menu: its rows would describe what the overlay is
     * drawn over. */
    LibToriRS_Input_Begin(input, 20);
    LibToriRS_Input_PushMouseMove(input, 160, 47);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_RIGHT, 160, 47);
    LibToriRS_Input_End(input);
    UITree_InteractFrameWithPointerOwner(&interact, tree, &host, input, 20, 0, 1, &out);
    TEST_ASSERT(!out.right_click, "a right press under the overlay asks for no menu");

    /* The same press with nobody owning the pointer is the control: this is a
     * gate, not a component that stopped working. */
    LibToriRS_Input_Begin(input, 40);
    LibToriRS_Input_PushMouseMove(input, 160, 47);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_RIGHT, 160, 47);
    LibToriRS_Input_End(input);
    UITree_InteractFrameWithPointerOwner(&interact, tree, &host, input, 40, 0, 0, &out);
    TEST_ASSERT(out.right_click, "the same press unowned does ask for one");
    TEST_ASSERT(out.hover_com_id == 700, "and the component hovers again");

    UITree_Free(tree);
}

void
test_hover_input(void)
{
    printf("TEST: hover / hit-test / input\n");

    struct UITree* tree = UITree_New(32);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    /* Layer (pass-through) containing decorative graphic + clickable button + overlapping later sibling */
    int32_t layer = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 10, 0, 0, 400, 300);

    struct UITreeNodeSpec gfx;
    memset(&gfx, 0, sizeof(gfx));
    gfx.type = UIELEM_RS_GRAPHIC;
    gfx.component_id = 11;
    gfx.x = 10;
    gfx.y = 10;
    gfx.width = 100;
    gfx.height = 80;
    gfx.u.rs_graphic.scene_id = 1;
    /* no button_type / ops → decorative pass-through */
    int32_t graphic = UITree_Push(tree, layer, &gfx);

    struct UITreeNodeSpec btn;
    memset(&btn, 0, sizeof(btn));
    btn.type = UIELEM_RS_RECT;
    btn.component_id = 12;
    btn.x = 10;
    btn.y = 10;
    btn.width = 100;
    btn.height = 80;
    btn.u.rs_rect.color = 0x00FF00;
    btn.u.rs_rect.filled = 1;
    int32_t button = UITree_Push(tree, layer, &btn);
    tree->components[button].behavior.button_type = 1;
    tree->components[button].behavior.over_color = 0xFFFFFF;

    /* Later overlapping sibling (wins paint / hit) */
    int32_t top = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, 13, 50, 50, 40, 40);
    tree->components[top].behavior.button_type = 1;
    tree->components[top].behavior.over_color = 0xEEEEEE;

    UITree_TestResolve(tree);

    /* Geometric hit: deepest / last sibling wins */
    int32_t hit_btn_area = UITree_HitTest(tree, 20, 20);
    TEST_ASSERT(hit_btn_area == button || hit_btn_area == graphic, "hit in overlap graphic/button");
    /* Interactive: skip pass-through layer+decorative graphic → button */
    int32_t ihit = UITree_HitTestInteractive(tree, &host, 20, 20);
    TEST_ASSERT(ihit == button, "interactive hits button not layer/graphic");

    int32_t hit_top = UITree_HitTest(tree, 60, 60);
    TEST_ASSERT(hit_top == top, "later sibling wins overlap");

    int32_t miss = UITree_HitTestInteractive(tree, &host, 350, 250);
    TEST_ASSERT(miss < 0, "interactive miss outside interactive nodes");

    /* hide visibility */
    TEST_ASSERT(UITree_ComponentVisibleById(&tree->components[button], -1), "non-hide visible with -1");
    tree->components[button].behavior.hide = 1;
    TEST_ASSERT(!UITree_ComponentVisibleById(&tree->components[button], -1), "hide invisible with -1");
    TEST_ASSERT(UITree_ComponentVisibleById(&tree->components[button], 12), "hide visible when id matches");
    tree->components[button].behavior.hide = 0;

    /* Scroll-aware hit (canonical component scroll offset) */
    {
        struct UITree* st = UITree_New(8);
        int32_t L = UITree_TestPushXy(st, -1, UIELEM_RS_LAYER, 50, 0, 0, 100, 50);
        st->components[L].u.rs_layer.scroll_height = 200;
        int32_t child = UITree_TestPushXy(st, L, UIELEM_RS_RECT, 51, 0, 100, 40, 30);
        st->components[child].behavior.button_type = 1;
        UITree_TestResolve(st);

        /* Child at content y=100; viewport 50 tall — without scroll, point (10,20) is in layer but not child */
        int32_t before = UITree_HitTestInteractive(st, &host, 10, 20);
        TEST_ASSERT(before != child, "child not hit before scroll");

        st->components[L].scroll_y = 100;
        int32_t after = UITree_HitTestInteractive(st, &host, 10, 20);
        TEST_ASSERT(after == child, "child hit after scroll_y");
        UITree_Free(st);
    }

    /* Region hover: outside → -1; inside deepest id; over_layer_id redirect */
    {
        int outside = UITree_FindHoveredComponentIdForRegion(
            tree, &host, tree->root_index, 900, 900, 0, 0, 400, 300);
        TEST_ASSERT(outside < 0, "hover outside region");

        int inside = UITree_FindHoveredComponentIdForRegion(
            tree, &host, tree->root_index, 60, 60, 0, 0, 400, 300);
        TEST_ASSERT(inside == 13, "hover deepest component_id is top rect");

        tree->components[top].behavior.over_layer_id = 99;
        int redirected = UITree_FindHoveredComponentIdForRegion(
            tree, &host, tree->root_index, 60, 60, 0, 0, 400, 300);
        TEST_ASSERT(redirected == 99, "over_layer_id redirect");
        tree->components[top].behavior.over_layer_id = -1;
    }

    /* A component whose only hover script is on_mouse_repeat must still be
     * reported hovered so the dispatcher can fire it. */
    {
        int32_t rep = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, 14, 200, 10, 30, 30);
        UITree_HooksMut(&tree->components[rep])->on_mouse_repeat.script_id = 5;
        UITree_TestResolve(tree);
        int id = UITree_FindHoveredComponentIdForRegion(
            tree, &host, tree->root_index, 210, 20, 0, 0, 400, 300);
        TEST_ASSERT(id == 14, "on_mouse_repeat-only component reports hovered");
    }

    /* Regression: IF_SETHIDE on a non-layer must prune hover (match hit-test).
     *
     * Magic spellbook jewellery-enchant: CS2 hides main-book type=5 icons but
     * leaves on_mouse_repeat attached. Hover used to only skip hidden layers,
     * so a later hidden Lumbridge sibling overwrote the visible enchant icon
     * (last-match-wins) and script2622 still drew its tooltip. */
    {
        struct UITree* st = UITree_New(8);
        int32_t L = UITree_TestPushXy(st, -1, UIELEM_RS_LAYER, 300, 0, 0, 100, 100);
        int32_t enchant = UITree_TestPushXy(st, L, UIELEM_RS_RECT, 301, 10, 10, 24, 24);
        UITree_HooksMut(&st->components[enchant])->on_mouse_repeat.script_id = 2622;
        int32_t lumbridge = UITree_TestPushXy(st, L, UIELEM_RS_RECT, 302, 10, 10, 24, 24);
        UITree_HooksMut(&st->components[lumbridge])->on_mouse_repeat.script_id = 2622;
        st->components[lumbridge].behavior.hide = 1;
        UITree_TestResolve(st);

        int id = UITree_FindHoveredComponentIdForRegion(
            st, &host, -1, 20, 20, 0, 0, 400, 300);
        TEST_ASSERT(id == 301, "hidden later sibling with on_mouse_repeat does not win hover");

        st->components[enchant].behavior.hide = 1;
        id = UITree_FindHoveredComponentIdForRegion(
            st, &host, -1, 20, 20, 0, 0, 400, 300);
        TEST_ASSERT(id < 0, "lone hidden hooked child reports no hover");
        UITree_Free(st);
    }

    /* Out-of-region deep branch must not steal hover id */
    {
        int32_t far = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 80, 500, 0, 200, 200);
        int32_t deep = far;
        for( int i = 0; i < 5; i++ )
            deep = UITree_TestPushXy(tree, deep, UIELEM_RS_RECT, 81 + i, 0, 0, 10, 10);
        UITree_TestResolve(tree);

        int id = UITree_FindHoveredComponentIdForRegion(
            tree, &host, layer, 20, 20, 0, 0, 400, 300);
        TEST_ASSERT(id == 12, "region hover stays on in-region button");
        TEST_ASSERT(id != 81 + 4, "far deep leaf not reported");
    }

    /* Regression: the hover walk MUST be given the host.
     *
     * Without it the sidebar-tab gate cannot run, so the walk descends into
     * every tab's subtree — and because it is last-match-wins, a component
     * from a tab that is not even on screen overrides the visible one. Live
     * symptom: stats-tab cells had a hover box a few pixels tall because rows
     * from another tab kept winning. */
    {
        struct UITree* st = UITree_New(16);
        int32_t vis_tab = UITree_TestPushXy(st, -1, UIELEM_BUILTIN_SIDEBAR, 200, 0, 0, 200, 200);
        st->components[vis_tab].u.sidebar.tabno = 1;
        int32_t vis_cell = UITree_TestPushXy(st, vis_tab, UIELEM_RS_LAYER, 201, 0, 0, 64, 32);
        st->components[vis_cell].behavior.over_layer_id = 900;

        /* Same screen box, but parked on a tab that is not selected, and
         * pushed later so it wins any last-match-wins tie. */
        int32_t hidden_tab = UITree_TestPushXy(st, -1, UIELEM_BUILTIN_SIDEBAR, 210, 0, 0, 200, 200);
        st->components[hidden_tab].u.sidebar.tabno = 2;
        int32_t hidden_cell =
            UITree_TestPushXy(st, hidden_tab, UIELEM_RS_LAYER, 211, 0, 0, 64, 32);
        st->components[hidden_cell].behavior.over_layer_id = 911;
        UITree_TestResolve(st);

        hs.selected_tab = 1;
        int with_host = UITree_FindHoveredComponentIdForRegion(
            st, &host, -1, 30, 16, 0, 0, 400, 300);
        TEST_ASSERT(with_host == 900, "hover respects the selected sidebar tab");

        /* The whole 64x32 cell resolves to the same overlayer id — the actual
         * user-visible property (a stat box tooltip covers the box). */
        TEST_ASSERT(
            UITree_FindHoveredComponentIdForRegion(st, &host, -1, 1, 1, 0, 0, 400, 300) == 900 &&
                UITree_FindHoveredComponentIdForRegion(st, &host, -1, 63, 31, 0, 0, 400, 300) ==
                    900,
            "overlayer hover covers the whole cell");

        int no_host =
            UITree_FindHoveredComponentIdForRegion(st, NULL, -1, 30, 16, 0, 0, 400, 300);
        TEST_ASSERT(no_host == 911, "without a host the unselected tab leaks through");
        UITree_Free(st);
    }

    /* Regression: an INACTIVE sidebar tab must not block the active tab's hits.
     *
     * Sidebar tabs are fully-overlapping siblings. On the CS2/dat2 gameframe,
     * script-driven tab containers carry no_click_through. Before the fix the
     * hit-test/collect walks gated the tab visibility only on recursion, AFTER
     * recording the no_click_through barrier/blocks — so an inactive tab pushed
     * later than the active one (over the same screen box) discarded the active
     * tab's already-collected inventory hit. Live symptom: after a tab switch,
     * inventory items stopped being hoverable/clickable and right-click showed
     * no options. The gate now runs FIRST (mirroring the emit walk). */
    {
        struct UITree* st = UITree_New(16);

        /* Active tab (tabno 1) with a collectable inventory item. */
        int32_t inv_tab = UITree_TestPushXy(st, -1, UIELEM_BUILTIN_SIDEBAR, 400, 0, 0, 200, 200);
        st->components[inv_tab].u.sidebar.tabno = 1;
        int32_t item = UITree_TestPushXy(st, inv_tab, UIELEM_RS_RECT, 401, 0, 0, 64, 32);
        st->components[item].behavior.button_type = 1;
        st->components[item].behavior.over_color = 0xFFFFFF;

        /* Inactive tab (tabno 2), same screen box, pushed LATER (wins ties) and
         * carrying no_click_through — the blocker that used to eat the item. */
        int32_t blk_tab = UITree_TestPushXy(st, -1, UIELEM_BUILTIN_SIDEBAR, 410, 0, 0, 200, 200);
        st->components[blk_tab].u.sidebar.tabno = 2;
        st->components[blk_tab].no_click_through = 1;
        int32_t blk_child = UITree_TestPushXy(st, blk_tab, UIELEM_RS_RECT, 411, 0, 0, 200, 200);
        st->components[blk_child].no_click_through = 1;
        UITree_TestResolve(st);

        hs.selected_tab = 1;

        TEST_ASSERT(
            UITree_HitTestInteractive(st, &host, 20, 16) == item,
            "inactive no_click_through tab does not block active tab's click");

        int32_t hits[8];
        int n = UITree_CollectNodesAt(st, &host, 20, 16, hits, 8);
        int found = 0;
        for( int i = 0; i < n; i++ )
            if( hits[i] == item )
                found = 1;
        TEST_ASSERT(found, "active tab's item still collected for the right-click menu");
        UITree_Free(st);
    }

    /* Regression: configured decorative overlays are late root siblings, and
     * UITree_HitTestInteractive lets a later root win — so a non-passthrough
     * one shadows the entire interface, not just the world. */
    {
        struct UITree* st = UITree_New(8);
        int32_t panel = UITree_TestPushXy(st, -1, UIELEM_RS_RECT, 300, 0, 0, 100, 100);
        st->components[panel].behavior.button_type = 1;
        /* Pushed after the panel, unsized — exactly how app.c pushes them. */
        int32_t overlay = UITree_TestPushXy(st, -1, UIELEM_BUILTIN_ENTITY_OVERLAY, 301, 0, 0, 0, 0);
        (void)overlay;
        UITree_TestResolve(st);

        TEST_ASSERT(
            UITree_HitTestInteractive(st, &host, 50, 50) == panel,
            "entity overlay never eats clicks");
        UITree_Free(st);
    }

    /* Hover routing commit edge */
    {
        struct UIHoverRouting routing;
        UITree_HoverRoutingReset(&routing);
        UITree_HoverRoutingBeginFrame(&routing);
        routing.over_main_com_id = 12;
        TEST_ASSERT(UITree_HoverRoutingCommitFrame(&routing), "commit true on change");
        UITree_HoverRoutingBeginFrame(&routing);
        routing.over_main_com_id = 12;
        TEST_ASSERT(!UITree_HoverRoutingCommitFrame(&routing), "commit false when unchanged");
        struct UITreeHoverIds ids = UITree_HoverRoutingToIds(&routing);
        TEST_ASSERT(ids.main_com_id == 12, "to_ids main");
    }

    /* Input update click only on press+release same node */
    {
        struct UIInputState st = { .hovered = -1, .pressed = -1 };
        struct UIInputResult r = UITree_InputUpdate(
            &st, tree, &host,
            (struct UIInputEvent){ .kind = UI_INPUT_MOVE, .x = 20, .y = 20 });
        TEST_ASSERT(r.hovered == button || r.hovered == graphic || r.hovered == layer, "move hover");
        r = UITree_InputUpdate(
            &st, tree, &host,
            (struct UIInputEvent){ .kind = UI_INPUT_DOWN, .x = 20, .y = 20 });
        int32_t pressed = st.pressed;
        TEST_ASSERT(pressed >= 0, "down presses");
        r = UITree_InputUpdate(
            &st, tree, &host,
            (struct UIInputEvent){ .kind = UI_INPUT_UP, .x = 20, .y = 20 });
        TEST_ASSERT(r.clicked == pressed, "click when same node");
        r = UITree_InputUpdate(
            &st, tree, &host,
            (struct UIInputEvent){ .kind = UI_INPUT_DOWN, .x = 20, .y = 20 });
        r = UITree_InputUpdate(
            &st, tree, &host,
            (struct UIInputEvent){ .kind = UI_INPUT_UP, .x = 350, .y = 250 });
        TEST_ASSERT(r.clicked < 0, "no click when release elsewhere");
    }

    /* Every positive-size container clips its children — hit/hover must match
     * the drawn (clipped) pixels, including for non-scrollable layers. */
    {
        struct UITree* ct = UITree_New(8);
        /* Non-scrollable layer 100x40 at (100,100); child rect overhangs both
         * edges (rel x=-20, w=200 → abs 80..280 vs layer 100..200). */
        int32_t lay = UITree_TestPushXy(ct, -1, UIELEM_RS_LAYER, 60, 100, 100, 100, 40);
        int32_t wide = UITree_TestPushXy(ct, lay, UIELEM_RS_RECT, 61, -20, 10, 200, 20);
        ct->components[wide].behavior.button_type = 1;
        ct->components[wide].behavior.over_color = 0xFFFFFF;
        UITree_TestResolve(ct);

        int32_t in_both = UITree_HitTestInteractive(ct, &host, 150, 115);
        TEST_ASSERT(in_both == wide, "overhang child hit inside layer bounds");
        int32_t in_overhang = UITree_HitTestInteractive(ct, &host, 230, 115);
        TEST_ASSERT(in_overhang != wide, "overhang clipped: no hit past layer edge");

        int hov_in = UITree_FindHoveredComponentIdForRegion(
            ct, &host, ct->root_index, 150, 115, 0, 0, 400, 300);
        TEST_ASSERT(hov_in == 61, "overhang child hovers inside layer bounds");
        int hov_out = UITree_FindHoveredComponentIdForRegion(
            ct, &host, ct->root_index, 230, 115, 0, 0, 400, 300);
        TEST_ASSERT(hov_out != 61, "overhang clipped: no hover past layer edge");
        UITree_Free(ct);
    }

    /* Non-layer containers (inv grid) clip children with the same predicate as
     * the emit walk. */
    {
        struct UITree* ct = UITree_New(8);
        int32_t grid = UITree_TestPushXy(ct, -1, UIELEM_RS_INV, 70, 50, 50, 60, 60);
        int32_t spill = UITree_TestPushXy(ct, grid, UIELEM_RS_RECT, 71, 40, 10, 60, 20);
        ct->components[spill].behavior.button_type = 1;
        UITree_TestResolve(ct);

        TEST_ASSERT(
            UITree_HitTestInteractive(ct, &host, 100, 65) == spill,
            "inv-grid child hit inside grid bounds");
        TEST_ASSERT(
            UITree_HitTestInteractive(ct, &host, 130, 65) != spill,
            "inv-grid clips child hit past grid edge");
        UITree_Free(ct);
    }

    /* An RS_INV grid carries cols/rows in its layout width/height (a live
     * backpack is 4x7), so its node box is a few pixels while its 32px slots
     * span far past it. CollectNodesAt (the right-click menu's hit source) must
     * still collect the grid when a slot — not the node box — is clicked, or
     * the whole inventory is invisible to the minimenu. */
    {
        struct UITree* ct = UITree_New(8);
        int32_t grid = UITree_TestPushXy(ct, -1, UIELEM_RS_INV, 70, 70, 50, 4, 7);
        ct->components[grid].u.rs_inv.cols = 4;
        ct->components[grid].u.rs_inv.rows = 7;
        ct->components[grid].u.rs_inv.margin_x = 0;
        ct->components[grid].u.rs_inv.margin_y = 0;
        UITree_TestResolve(ct);

        int32_t hits[8];
        /* (85,65): inside slot 0's 32x32 rect (70..102, 50..82) but past the
         * 4x7 node box (70..74, 50..57). */
        int n_in = UITree_CollectNodesAt(ct, &host, 85, 65, hits, 8);
        int found = 0;
        for( int i = 0; i < n_in; i++ )
            if( hits[i] == grid )
                found = 1;
        TEST_ASSERT(found, "inv grid collected when a slot (not the node box) is clicked");

        /* Well past every slot: not collected. */
        int n_out = UITree_CollectNodesAt(ct, &host, 300, 300, hits, 8);
        int found_out = 0;
        for( int i = 0; i < n_out; i++ )
            if( hits[i] == grid )
                found_out = 1;
        TEST_ASSERT(!found_out, "inv grid not collected past its slots");
        UITree_Free(ct);
    }

    /* Clip rect must sit at SCREEN coords: a clipping layer inside a scrolled
     * ancestor keeps its hitbox where it is drawn (emit places the clip at
     * x - scroll_off). */
    {
        struct UITree* ct = UITree_New(8);
        int32_t outer = UITree_TestPushXy(ct, -1, UIELEM_RS_LAYER, 90, 0, 0, 100, 100);
        ct->components[outer].u.rs_layer.scroll_height = 300;
        /* Inner layer at content y=120 draws at y=20 once scrolled by 100. */
        int32_t inner = UITree_TestPushXy(ct, outer, UIELEM_RS_LAYER, 91, 0, 120, 80, 40);
        int32_t item = UITree_TestPushXy(ct, inner, UIELEM_RS_RECT, 92, -10, 0, 120, 40);
        ct->components[item].behavior.button_type = 1;
        UITree_TestResolve(ct);
        ct->components[outer].scroll_y = 100;

        TEST_ASSERT(
            UITree_HitTestInteractive(ct, &host, 40, 30) == item,
            "scrolled inner-layer child hit at drawn position");
        TEST_ASSERT(
            UITree_HitTestInteractive(ct, &host, 90, 30) != item,
            "scrolled inner-layer clip still cuts overhang");
        UITree_Free(ct);
    }

    /* IF_OPENSUB type is load-bearing for world input: a modal owns its panel
     * even when actionless, while an overlay remains transparent unless its
     * own cache record raises noClickThrough. */
    {
        struct UITree* mt = UITree_New(8);
        int const container_uid = (500 << 16) | 0;
        int const overlay_uid = (600 << 16) | 0;
        int32_t container =
            UITree_TestPushXy(mt, -1, UIELEM_RS_LAYER, container_uid, 0, 0, 300, 200);
        (void)UITree_TestPushXy(mt, container, UIELEM_RS_LAYER, overlay_uid, 20, 30, 100, 80);
        (void)UITree_InterfaceParentSet(mt, container_uid, 600, 0);
        UITree_TestResolve(mt);

        TEST_ASSERT(
            UITree_HitTestInteractive(mt, &host, 40, 50) < 0,
            "actionless modal has no interactive click target");
        TEST_ASSERT(
            UITree_PointBlocksWorld(mt, &host, 40, 50),
            "actionless modal blocks clicks to the world");
        TEST_ASSERT(
            UITree_PointBlocksWorld(mt, &host, 150, 50),
            "blank mount-host space outside modal root still blocks world input");

        (void)UITree_InterfaceParentSet(mt, container_uid, 600, 1);
        TEST_ASSERT(
            !UITree_PointBlocksWorld(mt, &host, 40, 50),
            "actionless overlay remains transparent to world clicks");
        TEST_ASSERT(
            !UITree_PointBlocksWorld(mt, &host, 150, 50),
            "blank host space for an overlay remains transparent");
        UITree_Free(mt);
    }

    /* Hover events retain an exact node incarnation. A cycle rebuild may put
     * the same component id back into the same slot while the pointer never
     * moves; the replacement gets a fresh over, never its own leave on behalf
     * of the reclaimed occupant. */
    {
        struct UITree* ht = UITree_New(8);
        struct UIInteraction interact;
        struct LibToriRS_Input storage;
        struct LibToriRS_Input* input;
        struct UIInteractOut out;
        int const parent_id = (700 << 16) | 0;
        int32_t parent =
            UITree_TestPushXy(ht, -1, UIELEM_RS_LAYER, parent_id, 0, 0, 200, 200);
        int32_t old = UITree_CcCreate(ht, parent, parent_id, UIELEM_RS_RECT, 0);
        uint32_t old_incarnation;
        int saw_old_over = 0;
        int saw_new_over = 0;
        int saw_replacement_leave = 0;

        printf("TEST: hover identity survives same-id/same-slot recycle\n");
        ht->components[old].position.x = 10;
        ht->components[old].position.y = 10;
        ht->components[old].position.width = 100;
        ht->components[old].position.height = 100;
        UITree_HooksMut(&ht->components[old])->on_mouse_over.script_id = 851;
        UITree_HooksMut(&ht->components[old])->on_mouse_leave.script_id = 852;
        UITree_TestResolve(ht);
        old_incarnation = ht->components[old].incarnation;
        UIInteraction_Init(&interact);
        input = LibToriRS_Input_Init(&storage, 0);

        LibToriRS_Input_Begin(input, 0);
        LibToriRS_Input_PushMouseMove(input, 20, 20);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, ht, &host, input, 0, &out);
        for( int i = 0; i < out.intent_count; i++ )
            if( out.intents[i].hook && out.intents[i].hook->script_id == 851 )
                saw_old_over = 1;
        TEST_ASSERT(saw_old_over, "initial occupant receives mouse-over");

        UITree_CcDeleteAll(ht, parent);
        {
            int32_t replacement = UITree_CcCreate(
                ht, parent, parent_id, UIELEM_RS_RECT, 0);
            TEST_ASSERT(replacement == old, "hover fixture reuses the same array slot");
            TEST_ASSERT(
                ht->components[replacement].incarnation != old_incarnation,
                "replacement has a fresh incarnation");
            ht->components[replacement].position.x = 10;
            ht->components[replacement].position.y = 10;
            ht->components[replacement].position.width = 100;
            ht->components[replacement].position.height = 100;
            UITree_HooksMut(&ht->components[replacement])->on_mouse_over.script_id = 861;
            UITree_HooksMut(&ht->components[replacement])->on_mouse_leave.script_id = 862;
        }
        UITree_TestResolve(ht);
        interact.client_cycle++;
        LibToriRS_Input_Begin(input, 20);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, ht, &host, input, 20, &out);
        for( int i = 0; i < out.intent_count; i++ )
        {
            if( out.intents[i].hook && out.intents[i].hook->script_id == 861 )
                saw_new_over = 1;
            if( out.intents[i].hook && out.intents[i].hook->script_id == 862 )
                saw_replacement_leave = 1;
        }
        TEST_ASSERT(saw_new_over, "same-id replacement receives a fresh mouse-over");
        TEST_ASSERT(
            !saw_replacement_leave,
            "replacement never inherits reclaimed occupant's mouse-leave");
        UITree_Free(ht);
    }

    (void)graphic;
    UITree_Free(tree);
}
