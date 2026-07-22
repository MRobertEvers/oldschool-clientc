#include "test_harness.h"

#include <stdlib.h>

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
        tree->components[rep].runtime_hooks.on_mouse_repeat.script_id = 5;
        UITree_TestResolve(tree);
        int id = UITree_FindHoveredComponentIdForRegion(
            tree, &host, tree->root_index, 210, 20, 0, 0, 400, 300);
        TEST_ASSERT(id == 14, "on_mouse_repeat-only component reports hovered");
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

    /* Regression: app-pushed decorative overlays are late root siblings, and
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

    (void)graphic;
    UITree_Free(tree);
}
