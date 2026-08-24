#include "test_harness.h"
#include "uitree_interact.h"

/*
 * Real-client configuration regression tests: component ids are packed
 * (group<<16)|file uids and NO UITreeScrollState is available — hover and
 * hit-testing must follow the canonical UITreeComponent.scroll_x/scroll_y
 * offsets that emit and the scrollbar math already use. The shipping client
 * always ran this configuration while the old tests exercised the dead
 * UITreeScrollState arrays with small ids.
 */

#define TG 550
#define TUID(file) ((TG << 16) | (file))

void
test_scroll_hit(void)
{
    printf("TEST: scroll-aware hit/hover (real ids, canonical offsets)\n");

    struct UITree* tree = UITree_New(16);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    /* Scrollable layer: viewport 100x100 at origin, content 400 tall. */
    int32_t layer = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, TUID(3), 0, 0, 100, 100);
    tree->components[layer].u.rs_layer.scroll_height = 400;

    /* Interactive + hoverable button at content y=250 (below the viewport
     * until the layer scrolls down). */
    int32_t button = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, TUID(7), 10, 250, 60, 30);
    tree->components[button].behavior.button_type = 1;
    tree->components[button].behavior.over_color = 0xFFFFFF;

    UITree_TestResolve(tree);

    /* Unscrolled: the button's content position is outside the viewport. */
    TEST_ASSERT(
        UITree_HitTestInteractive(tree, &host, 20, 60) < 0,
        "button not hit before scroll");

    /* Scroll down 200: button now drawn at y = 250 - 200 = 50..80. */
    tree->components[layer].scroll_y = 200;

    int32_t hit = UITree_HitTestInteractive(tree, &host, 20, 60);
    TEST_ASSERT(hit == button, "button hit at drawn position after scroll");

    /* The unscrolled position is outside the layer clip — must miss. */
    TEST_ASSERT(
        UITree_HitTestInteractive(tree, &host, 20, 260) < 0,
        "unscrolled position does not hit after scroll");

    /* Hover follows the drawn position too. */
    int hover = UITree_FindHoveredComponentIdForRegion(
        tree, &host, tree->root_index, 20, 60, 0, 0, 400, 300);
    TEST_ASSERT(hover == TUID(7), "hover reports button at drawn position");

    int hover_unscrolled = UITree_FindHoveredComponentIdForRegion(
        tree, &host, tree->root_index, 20, 260, 0, 0, 400, 300);
    TEST_ASSERT(hover_unscrolled < 0, "no hover at unscrolled position");

    /* A child scrolled fully out of the viewport must not be hit: place a
     * second button near the top of the content space. */
    int32_t above = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, TUID(8), 10, 10, 60, 30);
    tree->components[above].behavior.button_type = 1;
    UITree_TestResolve(tree);
    tree->components[layer].scroll_y = 200; /* re-assert after resolve */
    /* Drawn y would be 10 - 200 = -190: off-screen above the clip. */
    int32_t above_hit = UITree_HitTestInteractive(tree, &host, 20, 20);
    TEST_ASSERT(above_hit != above, "child scrolled out of viewport not hit");

    /*
     * Scrollbar classification must stay consistent with the drawn grip math
     * (torirs_frame.c vertical_scrollbar_grip): vh=100, track=vh-32=68,
     * grip=track*vh/content=68*100/400=17, range=300,
     * grip_y=(68-17)*200/300=34 → grip pixels ly+16+34=50..67.
     */
    {
        struct UITreeScrollbarHitInfo sb;
        struct UITreeScrollbarHitInfo stale_down;
        int const before_hidden = tree->components[layer].scroll_y;
        TEST_ASSERT(
            UITree_FindScrollbarAt(tree, &host, 108, 55, &sb) &&
                sb.kind == UITREE_SCROLLBAR_V_GRIP,
            "grip classified at drawn grip position");
        TEST_ASSERT(
            UITree_FindScrollbarAt(tree, &host, 108, 30, &sb) &&
                sb.kind == UITREE_SCROLLBAR_V_TRACK,
            "track above grip");
        TEST_ASSERT(
            UITree_FindScrollbarAt(tree, &host, 108, 8, &sb) &&
                sb.kind == UITREE_SCROLLBAR_V_UP,
            "up arrow");
        TEST_ASSERT(
            UITree_FindScrollbarAt(tree, &host, 108, 90, &stale_down) &&
                stale_down.kind == UITREE_SCROLLBAR_V_DOWN,
            "down arrow");

        /* A plugin frame hides replaced widgets as an effective display:none.
         * IF1 scrollbars are synthetic hit regions rather than child nodes, so
         * they must honor that gate explicitly. Also reject a hit latched while
         * visible: a frame can replace the layer during an active arrow/grip
         * hold, before the interaction code consumes its saved hit record. */
        tree->components[layer].frame_hidden = 1;
        TEST_ASSERT(
            !UITree_FindScrollbarAt(tree, &host, 108, 90, &sb),
            "a frame-hidden IF1 layer exposes no synthetic scrollbar hitbox");
        TEST_ASSERT(
            !UITree_ScrollbarHandle(
                tree, &stale_down, 108, 90,
                UITREE_SCROLLBAR_ACTION_ARROW_STEP, 0),
            "a stale scrollbar hit cannot mutate a layer hidden by the frame");
        TEST_ASSERT(
            tree->components[layer].scroll_y == before_hidden,
            "hidden IF1 scrollbar leaves its native scroll offset unchanged");

        /* A rebuild can reuse the same id and array index. The saved hit still
         * belongs to the former occupant and must not scroll the replacement. */
        tree->components[layer].frame_hidden = 0;
        tree->components[layer].incarnation++;
        TEST_ASSERT(
            !UITree_ScrollbarHandle(
                tree, &stale_down, 108, 90,
                UITREE_SCROLLBAR_ACTION_ARROW_STEP, 0),
            "a stale scrollbar capture cannot transfer to a recycled layer slot");
        TEST_ASSERT(
            tree->components[layer].scroll_y == before_hidden,
            "recycled layer keeps its native scroll offset");

        /* The release edge is no longer `held`. If the layer disappears on
         * exactly that frame, retain an explicit cancellation result for the
         * app's out-of-tree plugin hit regions instead of treating the mouse
         * up as a fresh click on the newly exposed replacement. */
        {
            struct UIInteraction interact;
            struct UIInteractOut out;
            struct LibToriRS_Input input_storage;
            struct LibToriRS_Input* input = LibToriRS_Input_Init(&input_storage, 0);

            UIInteraction_Init(&interact);
            LibToriRS_Input_Begin(input, 0);
            LibToriRS_Input_PushMouseMove(input, 108, 8);
            LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 108, 8);
            LibToriRS_Input_End(input);
            UITree_InteractFrame(&interact, tree, &host, input, 0, &out);
            TEST_ASSERT(interact.sb_arrow_held, "visible scrollbar owns its press");

            tree->components[layer].frame_hidden = 1;
            LibToriRS_Input_Begin(input, 20);
            LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, 108, 8);
            LibToriRS_Input_End(input);
            UITree_InteractFrame(&interact, tree, &host, input, 20, &out);
            TEST_ASSERT(
                out.cancelled_pointer_click,
                "hidden scrollbar release is fenced from plugin regions");
            TEST_ASSERT(!out.left_click_miss, "hidden scrollbar release cannot reach world");
        }
    }

    UITree_Free(tree);
}

void
test_wheel_stops_at_interface(void)
{
    struct UITree* tree = UITree_New(4);
    struct TestHostState hs;
    struct UITreeHost host;
    struct LibToriRS_Input input_storage;
    struct LibToriRS_Input* input;
    struct UIInteraction interact;
    struct UIInteractOut out;
    int32_t pane;
    struct UITreeRuntimeHooks* hooks;

    printf("TEST: interface wheel stops propagation to world gestures\n");
    UITree_TestHostInit(&host, &hs);

    pane = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, TUID(20), 10, 10, 100, 100);
    hooks = UITree_HooksMut(&tree->components[pane]);
    hooks->on_scroll_wheel.script_id = 1234;
    UITree_SyncHookMembership(tree, pane);
    UITree_TestResolve(tree);

    input = LibToriRS_Input_Init(&input_storage, 0);
    UIInteraction_Init(&interact);
    LibToriRS_Input_PushMouseMove(input, 20, 20);
    LibToriRS_Input_PushMouseWheel(input, -1);
    LibToriRS_Input_End(input);
    UITree_InteractFrame(&interact, tree, &host, input, 0, &out);

    TEST_ASSERT(out.intent_count == 1, "onScroll hook receives wheel");
    TEST_ASSERT(
        out.intents[0].component_id == TUID(20),
        "wheel dispatch targets interface under pointer");
    TEST_ASSERT(out.wheel_consumed, "handled interface wheel cannot reach world gesture");

    UITree_Free(tree);

    /* Mounted native pane under an outer scroller: its screen Y includes the
     * outer scroll, but excludes the mount host's own scroll. A flat abs-box
     * wheel scan instead picks the outer layer at this point. */
    {
        int const outer_uid = (560 << 16) | 0;
        int const host_uid = (560 << 16) | 1;
        int const mounted_root_uid = (561 << 16) | 0;
        int const pane_uid = (561 << 16) | 1;
        struct UITree* mounted = UITree_New(8);
        int32_t outer =
            UITree_TestPushXy(mounted, -1, UIELEM_RS_LAYER, outer_uid, 0, 0, 300, 160);
        mounted->components[outer].u.rs_layer.scroll_height = 300;
        int32_t mount_host =
            UITree_TestPushXy(mounted, outer, UIELEM_RS_LAYER, host_uid, 20, 110, 140, 100);
        mounted->components[mount_host].u.rs_layer.scroll_height = 180;
        int32_t mounted_root = UITree_TestPushXy(
            mounted, -1, UIELEM_RS_LAYER, mounted_root_uid, 0, 0, 100, 80);
        int32_t native_pane =
            UITree_TestPushXy(mounted, mounted_root, UIELEM_RS_LAYER, pane_uid, 10, 10, 60, 50);
        mounted->components[native_pane].u.rs_layer.scroll_height = 140;
        (void)UITree_InterfaceParentSet(mounted, host_uid, 561, 0);
        UITree_Reparent(mounted, mounted_root, mount_host);
        UITree_TestResolve(mounted);
        mounted->components[outer].scroll_y = 40;
        mounted->components[mount_host].scroll_y = 29;

        input = LibToriRS_Input_Init(&input_storage, 0);
        UIInteraction_Init(&interact);
        /* pane abs=(30,120), drawn=(30,80): outer -40 applies, host -29 does not. */
        LibToriRS_Input_PushMouseMove(input, 40, 90);
        LibToriRS_Input_PushMouseWheel(input, -1);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, mounted, &host, input, 0, &out);

        TEST_ASSERT(
            mounted->components[native_pane].scroll_y == UITREE_SCROLLBAR_WHEEL_STEP,
            "native wheel targets mounted pane at drawn outer-scrolled position");
        TEST_ASSERT(
            mounted->components[outer].scroll_y == 40,
            "mounted pane wins over outer scroller at the same wheel point");
        TEST_ASSERT(out.wheel_consumed, "mounted native pane consumes world wheel gesture");
        UITree_Free(mounted);
    }
}
