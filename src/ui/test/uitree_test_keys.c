#include "test_harness.h"
#include "input/torirs_keymap.h"
#include "uitree_interact.h"

#include <stdlib.h>

/* Give a component an onKey hook. Only script_id is read by the collector. */
static void
set_on_key(
    struct UITree* tree,
    int32_t idx,
    int script_id)
{
    struct UITreeRuntimeHooks* hooks = UITree_HooksMut(&tree->components[idx]);
    memset(&hooks->on_key, 0, sizeof(hooks->on_key));
    hooks->on_key.script_id = script_id;
    UITree_SyncHookMembership(tree, idx);
}

static void
set_on_op(
    struct UITree* tree,
    int32_t idx,
    int script_id)
{
    struct UITreeRuntimeHooks* hooks = UITree_HooksMut(&tree->components[idx]);
    memset(&hooks->on_op, 0, sizeof(hooks->on_op));
    hooks->on_op.script_id = script_id;
}

static int
key_target_index(
    struct UIInteractOut const* out,
    int component_id)
{
    for( int i = 0; i < out->key_target_count; i++ )
        if( out->key_targets[i].component_id == component_id )
            return i;
    return -1;
}

/* Drive one frame of interaction with the given key events already queued. */
static void
run_frame(
    struct UIInteraction* interact,
    struct UITree* tree,
    struct UITreeHost const* host,
    struct LibToriRS_Input* input,
    struct UIInteractOut* out)
{
    LibToriRS_Input_End(input);
    UITree_InteractFrame(interact, tree, host, input, 1000, out);
}

/* Spot-check the hand-ported VK -> OSRS table against
 * xrsps-typescript src/client/InputManager.ts OSRS_KEY_MAP. This is the most
 * error-prone artifact in the keyboard path: a transcription slip here silently
 * mis-routes one key with no other symptom. */
static void
test_osrs_keymap(void)
{
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(65) == 48, "VK_A -> 48");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(90) == 64, "VK_Z -> 64");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(48) == 25, "digit 0 -> 25");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(49) == 16, "digit 1 -> 16");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(57) == 24, "digit 9 -> 24");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(8) == 85, "backspace -> 85");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(9) == 80, "tab -> 80");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(13) == 84, "enter -> 84");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(16) == 81, "shift -> 81");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(17) == 82, "ctrl -> 82");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(18) == 86, "alt -> 86");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(27) == 13, "escape -> 13");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(32) == 83, "space -> 83");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(112) == 1, "F1 -> 1");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(123) == 12, "F12 -> 12");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(103) == 230, "numpad 7 -> 230");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(108) == -1, "unmapped numpad gap -> -1");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(127) == 101, "delete -> 101");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(-1) == -1, "negative vk -> -1");
    TEST_ASSERT(LibToriRS_OsrsKeyFromVk(999) == -1, "out-of-range vk -> -1");
}

/* The per-frame queue is cleared by Begin() but held state must survive it. */
static void
test_input_queue_lifecycle(void)
{
    struct LibToriRS_Input storage;
    struct LibToriRS_Input* input = LibToriRS_Input_Init(&storage, 0);

    LibToriRS_Input_Begin(input, 0);
    LibToriRS_Input_PushKeyEvent(input, 84, 0, 0);
    LibToriRS_Input_SetOsrsKeyState(input, 84, 1, 1);
    TEST_ASSERT(input->key_event_count == 1, "event queued");
    TEST_ASSERT(input->osrs_key_held[84] == 1, "held recorded");
    TEST_ASSERT(input->osrs_key_pressed[84] == 1, "press edge recorded");

    LibToriRS_Input_Begin(input, 20);
    TEST_ASSERT(input->key_event_count == 0, "Begin clears the event queue");
    TEST_ASSERT(input->osrs_key_pressed[84] == 0, "Begin clears the press edge");
    TEST_ASSERT(input->osrs_key_held[84] == 1, "Begin preserves held state");

    /* Auto-repeat must not re-arm the press edge, or KEYPRESSED stops being one. */
    LibToriRS_Input_SetOsrsKeyState(input, 84, 1, 0);
    TEST_ASSERT(input->osrs_key_pressed[84] == 0, "auto-repeat does not set the press edge");

    LibToriRS_Input_ClearKeys(input);
    TEST_ASSERT(
        input->osrs_key_held[84] == 0 && input->key_event_count == 0,
        "ClearKeys drops held state and the queue (focus loss)");

    /* Overflow drops the tail without corrupting earlier entries. */
    LibToriRS_Input_Begin(input, 40);
    for( int i = 0; i < LIBTORIRS_KEY_EVENT_MAX + 8; i++ )
        LibToriRS_Input_PushKeyEvent(input, -1, 'a' + (i % 26), 0);
    TEST_ASSERT(
        input->key_event_count == LIBTORIRS_KEY_EVENT_MAX, "queue clamps at its cap");
    TEST_ASSERT(input->key_events[0].key_pressed == 'a', "earliest event survives overflow");
}

void
test_key_dispatch(void)
{
    printf("TEST: keyboard broadcast / op-key bindings\n");

    test_osrs_keymap();
    test_input_queue_lifecycle();

    struct UITree* tree = UITree_New(32);
    struct TestHostState hstate;
    struct UITreeHost host;
    struct UIInteraction interact;
    struct LibToriRS_Input input_storage;
    struct LibToriRS_Input* input;
    struct UIInteractOut out;

    UITree_TestHostInit(&host, &hstate);
    UIInteraction_Init(&interact);

    int32_t layer = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 10, 0, 0, 400, 300);
    /* Two handlers at disjoint positions: neither is under the cursor. */
    int32_t left = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, 11, 0, 0, 40, 40);
    int32_t right = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, 12, 300, 200, 40, 40);
    /* Zero-size: still collected, unlike the wheel hit-test collector. */
    int32_t empty = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, 13, 10, 10, 0, 0);
    /* Under a hidden parent: pruned. */
    int32_t hidden_parent = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 20, 0, 0, 100, 100);
    int32_t hidden_child = UITree_TestPushXy(tree, hidden_parent, UIELEM_RS_RECT, 21, 0, 0, 10, 10);
    /* No onKey hook at all: never collected. */
    int32_t plain = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, 14, 50, 50, 10, 10);

    set_on_key(tree, left, 100);
    set_on_key(tree, right, 101);
    set_on_key(tree, empty, 102);
    set_on_key(tree, hidden_child, 103);
    (void)plain;
    tree->components[hidden_parent].behavior.hide = 1;

    UITree_TestResolve(tree);

    /* --- broadcast, not a hit test ------------------------------------- */
    input = LibToriRS_Input_Init(&input_storage, 0);
    LibToriRS_Input_Begin(input, 0);
    LibToriRS_Input_PushMouseMove(input, 200, 150); /* over neither handler */
    LibToriRS_Input_PushKeyEvent(input, 84, 0, 0);  /* Enter */
    run_frame(&interact, tree, &host, input, &out);

    TEST_ASSERT(out.key_event_count == 1, "one key event forwarded");
    TEST_ASSERT(key_target_index(&out, 11) >= 0, "left handler collected though not hovered");
    TEST_ASSERT(key_target_index(&out, 12) >= 0, "right handler collected though not hovered");
    TEST_ASSERT(key_target_index(&out, 13) >= 0, "zero-size handler still collected");
    TEST_ASSERT(key_target_index(&out, 21) < 0, "handler under hidden parent pruned");
    TEST_ASSERT(key_target_index(&out, 14) < 0, "component without onKey not collected");
    TEST_ASSERT(out.key_target_count == 3, "exactly the three visible handlers");
    TEST_ASSERT(out.intent_count == 0, "a key-only frame pushes no click/hover intents");

    /* Event context is the mouse relative to each component's drawn origin. */
    {
        int li = key_target_index(&out, 11);
        int ri = key_target_index(&out, 12);
        TEST_ASSERT(
            out.key_targets[li].node_index == left &&
                out.key_targets[li].node_incarnation == tree->components[left].incarnation,
            "key target retains the exact collected node incarnation");
        TEST_ASSERT(
            out.key_targets[ri].node_index == right &&
                out.key_targets[ri].node_incarnation == tree->components[right].incarnation,
            "each broadcast target carries its own exact occupant");
        TEST_ASSERT(
            out.key_mouse_x - out.key_targets[li].abs_x == 200, "left rel x = mouse - abs");
        TEST_ASSERT(
            out.key_mouse_x - out.key_targets[ri].abs_x == 200 - 300, "right rel x = mouse - abs");
    }

    /* --- hiding the node itself also prunes it -------------------------- */
    tree->components[left].behavior.hide = 1;
    LibToriRS_Input_Begin(input, 20);
    LibToriRS_Input_PushKeyEvent(input, 84, 0, 0);
    run_frame(&interact, tree, &host, input, &out);
    TEST_ASSERT(key_target_index(&out, 11) < 0, "self-hidden handler pruned");
    tree->components[left].behavior.hide = 0;

    /* --- empty queue is a no-op ----------------------------------------- */
    LibToriRS_Input_Begin(input, 40);
    run_frame(&interact, tree, &host, input, &out);
    TEST_ASSERT(out.key_target_count == 0, "no key events -> no targets collected");
    TEST_ASSERT(out.key_event_count == 0, "no key events -> none forwarded");

    /* --- ordering and fan-out preserved --------------------------------- */
    LibToriRS_Input_Begin(input, 60);
    LibToriRS_Input_PushKeyEvent(input, -1, 'a', 0);
    LibToriRS_Input_PushKeyEvent(input, -1, 'b', 0);
    LibToriRS_Input_PushKeyEvent(input, 85, 0, 0);
    run_frame(&interact, tree, &host, input, &out);
    TEST_ASSERT(out.key_event_count == 3, "all three events forwarded");
    TEST_ASSERT(
        out.key_events[0].key_pressed == 'a' && out.key_events[1].key_pressed == 'b',
        "character events keep arrival order");
    TEST_ASSERT(
        out.key_events[0].key_typed == -1,
        "character event carries no key code (see LibToriRS_KeyEvent)");
    TEST_ASSERT(
        out.key_events[2].key_typed == 85 && out.key_events[2].key_pressed == 0,
        "key-code event carries no character");

    /* --- key burst must not evict click/hover intents -------------------- */
    {
        struct UITree* big = UITree_New(UI_KEY_TARGET_MAX + 16);
        struct UIInteraction big_interact;
        struct UIInteractOut big_out;
        UIInteraction_Init(&big_interact);
        for( int i = 0; i < UI_KEY_TARGET_MAX + 8; i++ )
        {
            int32_t idx = UITree_TestPushXy(big, -1, UIELEM_RS_RECT, 1000 + i, 0, 0, 5, 5);
            set_on_key(big, idx, 200 + i);
        }
        UITree_TestResolve(big);

        LibToriRS_Input_Begin(input, 80);
        LibToriRS_Input_PushKeyEvent(input, 84, 0, 0);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&big_interact, big, &host, input, 1000, &big_out);
        TEST_ASSERT(
            big_out.key_target_count == UI_KEY_TARGET_MAX,
            "key targets clamp at UI_KEY_TARGET_MAX rather than overflowing");
        UITree_Free(big);
    }

    /* --- op-key bindings ------------------------------------------------- */
    {
        int const chars[UITREE_OPKEY_PAIR_MAX] = { -1, -1, -1, -1, -1 };
        int const codes[UITREE_OPKEY_PAIR_MAX] = { 1, -1, -1, -1, -1 }; /* F1 */

        set_on_op(tree, right, 555);
        /* Clearing is a successful operation, so this reports true; what makes
         * it a clear rather than a bind is that the slot ends up unbound. */
        TEST_ASSERT(
            UITree_ApplyOpKey(tree, 12, 3, chars, codes, 1),
            "negative first keychar is accepted");
        TEST_ASSERT(
            !UITree_OpKeys(&tree->components[right])->slots[2].bound &&
                !UITree_OpKeys(&tree->components[right])->has_bindings,
            "negative first keychar clears the slot rather than binding it");

        {
            int const bind_chars[UITREE_OPKEY_PAIR_MAX] = { 0, 0, 0, 0, 0 };
            int const bind_codes[UITREE_OPKEY_PAIR_MAX] = { 1, 0, 0, 0, 0 };
            TEST_ASSERT(
                UITree_ApplyOpKey(tree, 12, 3, bind_chars, bind_codes, 1), "bind F1 to op 3");
        }
        TEST_ASSERT(UITree_OpKeys(&tree->components[right])->has_bindings, "has_bindings set after bind");
        TEST_ASSERT(
            UITree_ApplyOpKey(tree, 12, 0, NULL, NULL, 1) == false, "op_index 0 is out of range");
        TEST_ASSERT(
            UITree_ApplyOpKey(tree, 12, UITREE_OPKEY_SLOTS + 1, NULL, NULL, 1) == false,
            "op_index past the last slot is out of range");

        /* F1 (OSRS code 1) fires on_op with the bound op index. */
        LibToriRS_Input_Begin(input, 100);
        LibToriRS_Input_PushKeyEvent(input, 1, 0, 0);
        run_frame(&interact, tree, &host, input, &out);
        TEST_ASSERT(out.intent_count == 1, "matching op key pushes exactly one intent");
        TEST_ASSERT(out.intents[0].component_id == 12, "intent targets the bound component");
        TEST_ASSERT(out.intents[0].op_index == 3, "intent reports the bound op index");
        TEST_ASSERT(
            out.intents[0].hook == &UITree_Hooks(&tree->components[right])->on_op,
            "op key dispatches through on_op");

        /* A different key does not fire it. */
        LibToriRS_Input_Begin(input, 120);
        LibToriRS_Input_PushKeyEvent(input, 2, 0, 0); /* F2 */
        run_frame(&interact, tree, &host, input, &out);
        TEST_ASSERT(out.intent_count == 0, "unbound key does not fire the op");

        /* ignore_held suppresses OS auto-repeat but not a fresh press. */
        TEST_ASSERT(UITree_ApplyOpKeyIgnoreHeld(tree, 12, 3), "set ignore-held");
        LibToriRS_Input_Begin(input, 140);
        LibToriRS_Input_PushKeyEvent(input, 1, 0, 1); /* repeat */
        run_frame(&interact, tree, &host, input, &out);
        TEST_ASSERT(out.intent_count == 0, "ignore_held suppresses auto-repeat");

        LibToriRS_Input_Begin(input, 160);
        LibToriRS_Input_PushKeyEvent(input, 1, 0, 0); /* fresh press */
        run_frame(&interact, tree, &host, input, &out);
        TEST_ASSERT(out.intent_count == 1, "ignore_held still allows a fresh press");

        /* Rate config round-trips. */
        TEST_ASSERT(UITree_ApplyOpKeyRate(tree, 12, 3, 5, 1), "set op key rate");
        TEST_ASSERT(
            UITree_OpKeys(&tree->components[right])->slots[2].rate == 5 &&
                UITree_OpKeys(&tree->components[right])->slots[2].rate_enabled == 1,
            "rate and enabled stored");
    }

    /* --- CcCopy must clone op keys -------------------------------------- */
    {
        struct UITree* copy_tree = UITree_New(16);
        int32_t parent = UITree_TestPushXy(copy_tree, -1, UIELEM_RS_LAYER, 30, 0, 0, 100, 100);
        struct UITreeNodeSpec spec;
        int const bind_chars[UITREE_OPKEY_PAIR_MAX] = { 'q', 0, 0, 0, 0 };
        int const bind_codes[UITREE_OPKEY_PAIR_MAX] = { 7, 0, 0, 0, 0 };
        int32_t src_idx;

        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_RECT;
        spec.component_id = 31;
        spec.width = 10;
        spec.height = 10;
        src_idx = UITree_Push(copy_tree, parent, &spec);
        TEST_ASSERT(src_idx >= 0, "template child pushed");
        TEST_ASSERT(
            UITree_ApplyOpKey(copy_tree, 31, 2, bind_chars, bind_codes, 1),
            "bind op key on the template");

        {
            /* Mirror what CC_COPY does through the tree API. */
            struct UITreeComponent const src = copy_tree->components[src_idx];
            struct UITreeComponent dst;
            memset(&dst, 0, sizeof(dst));
            dst.op_keys = src.op_keys;
            TEST_ASSERT(
                UITree_OpKeys(&dst)->has_bindings && UITree_OpKeys(&dst)->slots[1].bound &&
                    UITree_OpKeys(&dst)->slots[1].key_chars[0] == 'q' &&
                    UITree_OpKeys(&dst)->slots[1].key_codes[0] == 7,
                "op keys survive a component copy");
        }
        UITree_Free(copy_tree);
    }

    UITree_Free(tree);
}

/*
 * A press and its release in ONE frame still click.
 *
 * The touch layer pushes move, down and up together (a finger has no hover to
 * watch), and a mouse clicked faster than a frame does the same. A component
 * with on_op fires on the press edge and tells the release to stay quiet, so
 * the bridge, returning only the release's result, said "clicked nothing" --
 * and the mobile chrome's every tab and orb was dead to a tap while a long
 * press (a right click) still worked. The second tap guards the latch that
 * swallows a press-edge click's release: armed for a release still to come,
 * it swallowed the NEXT tap instead.
 */
void
test_same_frame_press_release_clicks(void)
{
    struct UITree* tree = UITree_New(8);
    struct TestHostState hstate;
    struct UITreeHost host;
    struct UIInteraction interact;
    struct LibToriRS_Input input_storage;
    struct LibToriRS_Input* input;
    struct UIInteractOut out;
    int tap;

    printf("TEST: a press and its release in one frame click (a finger's tap)\n");

    UITree_TestHostInit(&host, &hstate);
    UIInteraction_Init(&interact);
    {
        int32_t layer = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 10, 0, 0, 400, 300);
        int32_t button = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, 11, 100, 100, 40, 40);
        set_on_op(tree, button, 200);
        tree->components[button].behavior.click_mask = 1u; /* op1 */
    }
    UITree_TestResolve(tree);
    input = LibToriRS_Input_Init(&input_storage, 0);

    for( tap = 0; tap < 3; tap++ )
    {
        LibToriRS_Input_Begin(input, (uint64_t)(1000 + tap * 700));
        LibToriRS_Input_PushMouseMove(input, 120, 120);
        LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 120, 120);
        LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, 120, 120);
        run_frame(&interact, tree, &host, input, &out);
        TEST_ASSERT(out.clicked_com_id == 11, "every tap clicks the button under the finger");
        TEST_ASSERT(!interact.swallow_left_click,
            "no release is owed after a tap, so nothing is latched to swallow the next one");
        /* The finger lifted: the frame after a tap has no pointer and no
         * buttons, like the touch layer's mouse-leave. */
        LibToriRS_Input_Begin(input, (uint64_t)(1000 + tap * 700 + 100));
        LibToriRS_Input_PushMouseLeave(input);
        run_frame(&interact, tree, &host, input, &out);
        TEST_ASSERT(out.clicked_com_id < 0, "the idle frame after a tap clicks nothing");
    }

    UITree_Free(tree);
}

/*
 * A held finger inside a scrollable layer scrolls it (UIInteraction::
 * touch_scroll): the layer moves by the finger's travel, the row under the
 * finger is never pressed, and the lift is not a click. A tap in the same
 * layer still clicks its row, and a mouse (touch_scroll off) still presses.
 */
void
test_touch_swipe_scrolls_layer(void)
{
    struct UITree* tree = UITree_New(8);
    struct TestHostState hstate;
    struct UITreeHost host;
    struct UIInteraction interact;
    struct LibToriRS_Input input_storage;
    struct LibToriRS_Input* input;
    struct UIInteractOut out;
    int32_t list;
    int32_t row;

    printf("TEST: a held finger inside a scrollable layer scrolls it\n");

    UITree_TestHostInit(&host, &hstate);
    UIInteraction_Init(&interact);
    interact.touch_scroll = 1;
    list = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 10, 0, 0, 200, 100);
    row = UITree_TestPushXy(tree, list, UIELEM_RS_RECT, 11, 0, 20, 200, 20);
    set_on_op(tree, row, 200);
    tree->components[row].behavior.click_mask = 1u;
    UITree_TestResolve(tree);
    TEST_ASSERT(UITree_SetScrollSizeAt(tree, list, 200, 400), "the list is taller than its box");
    /* The cache hangs the scrollbar's own handler on the CONTENT layer
     * (scrollbar_vertical_31: `if_setonscrollwheel(..., $content)`). */
    UITree_HooksMut(&tree->components[list])->on_scroll_wheel.script_id = 77;
    UITree_SyncHookMembership(tree, list);
    TEST_ASSERT(UITree_ScrollLayerNeedsVertical(&tree->components[list]), "and so needs scrolling");
    input = LibToriRS_Input_Init(&input_storage, 0);

    /* The finger lands on the row and is held (the touch layer's drag press). */
    LibToriRS_Input_Begin(input, 1000);
    LibToriRS_Input_PushMouseMove(input, 100, 30);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 100, 30);
    run_frame(&interact, tree, &host, input, &out);
    TEST_ASSERT(out.clicked_com_id < 0, "the row under a held finger is not pressed");
    TEST_ASSERT(interact.ts_layer == list, "the list owns the gesture");

    /* It travels 50 px up the screen: the list scrolls down by 50. */
    LibToriRS_Input_Begin(input, 1050);
    LibToriRS_Input_PushMouseMove(input, 100, -20);
    run_frame(&interact, tree, &host, input, &out);
    TEST_ASSERT(tree->components[list].scroll_y == 50, "the layer scrolled by the finger's travel");
    TEST_ASSERT(out.clicked_com_id < 0, "still no click");
    /* And the interface is told, so a script-drawn scrollbar's dragger can
     * follow: the list's own onScrollWheel handler, with a zero step. */
    {
        int found = 0;
        for( int i = 0; i < out.intent_count; i++ )
        {
            if( out.intents[i].component_id != 10 || !out.intents[i].hook )
                continue;
            if( out.intents[i].hook->script_id != 77 )
                continue;
            found = 1;
            TEST_ASSERT(out.intents[i].has_event_mouse, "the notify carries an event mouse");
            TEST_ASSERT(out.intents[i].event_mouse_y == 0, "and a zero wheel step");
        }
        TEST_ASSERT(found, "the swipe notifies the list's scroll handler");
    }

    /* Past the end: clamped. */
    LibToriRS_Input_Begin(input, 1100);
    LibToriRS_Input_PushMouseMove(input, 100, -900);
    run_frame(&interact, tree, &host, input, &out);
    TEST_ASSERT(tree->components[list].scroll_y == 300, "the scroll is clamped to the content");

    /* The lift is the gesture's, not a click. */
    LibToriRS_Input_Begin(input, 1150);
    LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, 100, -900);
    run_frame(&interact, tree, &host, input, &out);
    TEST_ASSERT(out.clicked_com_id < 0, "lifting after a swipe clicks nothing");
    TEST_ASSERT(interact.ts_layer < 0, "the gesture is over");

    /* A tap on the row (press and release together) still clicks it. */
    LibToriRS_Input_Begin(input, 1300);
    LibToriRS_Input_PushMouseLeave(input);
    run_frame(&interact, tree, &host, input, &out);
    TEST_ASSERT(UITree_SetScrollPosAt(tree, list, 0, 0), "back to the top");
    LibToriRS_Input_Begin(input, 1400);
    LibToriRS_Input_PushMouseMove(input, 100, 30);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 100, 30);
    LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, 100, 30);
    run_frame(&interact, tree, &host, input, &out);
    TEST_ASSERT(out.clicked_com_id == 11, "a tap in a scrollable list still clicks its row");

    /* A mouse: no swipe, the press reaches the row. */
    interact.touch_scroll = 0;
    LibToriRS_Input_Begin(input, 1500);
    LibToriRS_Input_PushMouseLeave(input);
    run_frame(&interact, tree, &host, input, &out);
    LibToriRS_Input_Begin(input, 1600);
    LibToriRS_Input_PushMouseMove(input, 100, 30);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 100, 30);
    run_frame(&interact, tree, &host, input, &out);
    TEST_ASSERT(out.clicked_com_id == 11, "a mouse press still presses the row");

    UITree_Free(tree);
}

/*
 * Feedback overlays never take a click.
 *
 * The touch marker ("inkwell") is a 64x64 late root sibling parked at the
 * canvas origin, and UITree_HitTestInteractive lets a later root's hit beat an
 * earlier one -- so while it was missing from the pass-through switch it won
 * every hit test it covered. On the mobile gameframe that corner holds the
 * logout, chat and keyboard stones: a TAP on them resolved to the marker and
 * ran nothing, while a long press still worked (the minimenu is built from
 * components carrying ops and never sees this node).
 */
void
test_feedback_overlay_never_takes_a_click(void)
{
    struct UITree* tree = UITree_New(8);
    struct TestHostState hstate;
    struct UITreeHost host;
    struct UIInteraction interact;
    struct LibToriRS_Input input_storage;
    struct LibToriRS_Input* input;
    struct UIInteractOut out;
    int32_t button;

    printf("TEST: a feedback overlay over a button does not take its click\n");

    UITree_TestHostInit(&host, &hstate);
    UIInteraction_Init(&interact);
    {
        int32_t panel = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 10, 0, 0, 200, 200);
        button = UITree_TestPushXy(tree, panel, UIELEM_RS_RECT, 11, 10, 10, 40, 40);
        set_on_op(tree, button, 200);
        tree->components[button].behavior.click_mask = 1u;
        /* The marker: its own root, pushed last, over the button. */
        (void)UITree_TestPushXy(tree, -1, UIELEM_BUILTIN_INKWELL, -1, 0, 0, 64, 64);
    }
    UITree_TestResolve(tree);

    /* The predicate itself, because the walk above asks the HOST whether the
     * marker is showing and this harness always answers no -- so only this
     * assertion actually fails when the marker is interactive again. */
    {
        int32_t marker = -1;
        for( uint32_t i = 0; i < tree->component_count; i++ )
            if( tree->components[i].type == UIELEM_BUILTIN_INKWELL )
                marker = (int32_t)i;
        TEST_ASSERT(marker >= 0, "the marker is in the tree");
        TEST_ASSERT(
            UITree_ComponentIsPassThrough(&tree->components[marker], &host),
            "the marker is pass-through, so a hit test walks past it");
    }
    TEST_ASSERT(
        UITree_HitTestInteractive(tree, &host, 20, 20) == button,
        "the hit under the marker is the button beneath it");

    input = LibToriRS_Input_Init(&input_storage, 0);
    LibToriRS_Input_Begin(input, 1000);
    LibToriRS_Input_PushMouseMove(input, 20, 20);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 20, 20);
    LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, 20, 20);
    run_frame(&interact, tree, &host, input, &out);
    TEST_ASSERT(out.clicked_com_id == 11, "and the tap runs the button, not the marker");

    UITree_Free(tree);
}
