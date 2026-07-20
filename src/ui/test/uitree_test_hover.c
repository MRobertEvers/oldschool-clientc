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
    int32_t ihit = UITree_HitTestInteractive(tree, &host, NULL, 20, 20);
    TEST_ASSERT(ihit == button, "interactive hits button not layer/graphic");

    int32_t hit_top = UITree_HitTest(tree, 60, 60);
    TEST_ASSERT(hit_top == top, "later sibling wins overlap");

    int32_t miss = UITree_HitTestInteractive(tree, &host, NULL, 350, 250);
    TEST_ASSERT(miss < 0, "interactive miss outside interactive nodes");

    /* hide visibility */
    TEST_ASSERT(UITree_ComponentVisibleById(&tree->components[button], -1), "non-hide visible with -1");
    tree->components[button].behavior.hide = 1;
    TEST_ASSERT(!UITree_ComponentVisibleById(&tree->components[button], -1), "hide invisible with -1");
    TEST_ASSERT(UITree_ComponentVisibleById(&tree->components[button], 12), "hide visible when id matches");
    tree->components[button].behavior.hide = 0;

    /* Scroll-aware hit */
    {
        struct UITree* st = UITree_New(8);
        int32_t L = UITree_TestPushXy(st, -1, UIELEM_RS_LAYER, 50, 0, 0, 100, 50);
        st->components[L].u.rs_layer.scroll_height = 200;
        int32_t child = UITree_TestPushXy(st, L, UIELEM_RS_RECT, 51, 0, 100, 40, 30);
        st->components[child].behavior.button_type = 1;
        UITree_TestResolve(st);

        int* sx = calloc((size_t)UITREE_SCROLL_MAX, sizeof(int));
        int* sy = calloc((size_t)UITREE_SCROLL_MAX, sizeof(int));
        TEST_ASSERT(sx && sy, "scroll alloc hover");
        struct UITreeScrollState scroll = { .scroll_x = sx, .scroll_y = sy };

        /* Child at content y=100; viewport 50 tall — without scroll, point (10,20) is in layer but not child */
        int32_t before = UITree_HitTestInteractive(st, &host, &scroll, 10, 20);
        TEST_ASSERT(before != child, "child not hit before scroll");

        UITree_ScrollSetPos(&scroll, 50, 0, 100);
        int32_t after = UITree_HitTestInteractive(st, &host, &scroll, 10, 20);
        TEST_ASSERT(after == child, "child hit after scroll_y");
        free(sx);
        free(sy);
        UITree_Free(st);
    }

    /* Region hover: outside → -1; inside deepest id; over_layer_id redirect */
    {
        int outside = UITree_FindHoveredComponentIdForRegion(
            tree, &host, NULL, tree->root_index, 900, 900, 0, 0, 400, 300);
        TEST_ASSERT(outside < 0, "hover outside region");

        int inside = UITree_FindHoveredComponentIdForRegion(
            tree, &host, NULL, tree->root_index, 60, 60, 0, 0, 400, 300);
        TEST_ASSERT(inside == 13, "hover deepest component_id is top rect");

        tree->components[top].behavior.over_layer_id = 99;
        int redirected = UITree_FindHoveredComponentIdForRegion(
            tree, &host, NULL, tree->root_index, 60, 60, 0, 0, 400, 300);
        TEST_ASSERT(redirected == 99, "over_layer_id redirect");
        tree->components[top].behavior.over_layer_id = -1;
    }

    /* Out-of-region deep branch must not steal hover id */
    {
        int32_t far = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 80, 500, 0, 200, 200);
        int32_t deep = far;
        for( int i = 0; i < 5; i++ )
            deep = UITree_TestPushXy(tree, deep, UIELEM_RS_RECT, 81 + i, 0, 0, 10, 10);
        UITree_TestResolve(tree);

        int id = UITree_FindHoveredComponentIdForRegion(
            tree, &host, NULL, layer, 20, 20, 0, 0, 400, 300);
        TEST_ASSERT(id == 12, "region hover stays on in-region button");
        TEST_ASSERT(id != 81 + 4, "far deep leaf not reported");
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
            &st, tree, &host, NULL,
            (struct UIInputEvent){ .kind = UI_INPUT_MOVE, .x = 20, .y = 20 });
        TEST_ASSERT(r.hovered == button || r.hovered == graphic || r.hovered == layer, "move hover");
        r = UITree_InputUpdate(
            &st, tree, &host, NULL,
            (struct UIInputEvent){ .kind = UI_INPUT_DOWN, .x = 20, .y = 20 });
        int32_t pressed = st.pressed;
        TEST_ASSERT(pressed >= 0, "down presses");
        r = UITree_InputUpdate(
            &st, tree, &host, NULL,
            (struct UIInputEvent){ .kind = UI_INPUT_UP, .x = 20, .y = 20 });
        TEST_ASSERT(r.clicked == pressed, "click when same node");
        r = UITree_InputUpdate(
            &st, tree, &host, NULL,
            (struct UIInputEvent){ .kind = UI_INPUT_DOWN, .x = 20, .y = 20 });
        r = UITree_InputUpdate(
            &st, tree, &host, NULL,
            (struct UIInputEvent){ .kind = UI_INPUT_UP, .x = 350, .y = 250 });
        TEST_ASSERT(r.clicked < 0, "no click when release elsewhere");
    }

    (void)graphic;
    UITree_Free(tree);
}
