#include "ui/ui_input.h"
#include "ui/ui_behavior.h"
#include "ui/uitree.h"
#include "vm/csvm.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/varp_varbit_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) \
    do \
    { \
        if( !(cond) ) \
        { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            return 1; \
        } \
    } while( 0 )

static int
test_get_varp_cb(
    void* ud,
    int id)
{
    struct VarPVarBitManager* mgr = ud;
    return varp_varbit_get_varp(mgr, id);
}

static int
test_hit_test_basic(void)
{
    struct UITree* tree = uitree_new(8);
    TEST_ASSERT(tree != NULL, "tree alloc");

    struct UINodeSpec parent_spec = { 0 };
    parent_spec.type = UIELEM_RS_RECT;
    parent_spec.component_id = 1;
    parent_spec.x = 0;
    parent_spec.y = 0;
    parent_spec.width = 100;
    parent_spec.height = 100;
    parent_spec.u.rs_rect.color = 0xFF0000;
    parent_spec.u.rs_rect.filled = 1;
    int32_t parent = uitree_push(tree, -1, &parent_spec);

    struct UINodeSpec child_spec = { 0 };
    child_spec.type = UIELEM_RS_RECT;
    child_spec.component_id = 2;
    child_spec.x = 10;
    child_spec.y = 10;
    child_spec.width = 40;
    child_spec.height = 40;
    child_spec.u.rs_rect.color = 0x00FF00;
    child_spec.u.rs_rect.filled = 1;
    int32_t child = uitree_push(tree, parent, &child_spec);

    TEST_ASSERT(parent == 0, "parent index");
    TEST_ASSERT(child == 1, "child index");
    TEST_ASSERT(uitree_hit_test(tree, -1, -1) == -1, "outside");
    TEST_ASSERT(uitree_hit_test(tree, 5, 5) == parent, "parent only");
    TEST_ASSERT(uitree_hit_test(tree, 20, 20) == child, "child over parent");
    TEST_ASSERT(uitree_hit_test(tree, 90, 90) == parent, "parent edge not child");

    uitree_free(tree);
    return 0;
}

static int
test_input_hover_and_click(void)
{
    struct UITree* tree = uitree_new(4);
    struct UINodeSpec rect0 = { 0 };
    rect0.type = UIELEM_RS_RECT;
    rect0.component_id = 1;
    rect0.x = 0;
    rect0.y = 0;
    rect0.width = 50;
    rect0.height = 50;
    rect0.u.rs_rect.filled = 1;
    uitree_push(tree, -1, &rect0);

    struct UINodeSpec rect1 = { 0 };
    rect1.type = UIELEM_RS_RECT;
    rect1.component_id = 2;
    rect1.x = 60;
    rect1.y = 0;
    rect1.width = 50;
    rect1.height = 50;
    rect1.u.rs_rect.filled = 1;
    uitree_push(tree, -1, &rect1);

    struct UIInputState state = { .hovered = -1, .pressed = -1 };
    struct UIInputResult result;

    result = uitree_input_update(
        &state, tree, (struct UIInputEvent){ UI_INPUT_MOVE, 10, 10, 0 });
    TEST_ASSERT(result.hovered == 0, "hover node 0");
    TEST_ASSERT(result.hover_changed, "hover changed on first move");

    result = uitree_input_update(
        &state, tree, (struct UIInputEvent){ UI_INPUT_MOVE, 70, 10, 0 });
    TEST_ASSERT(result.hovered == 1, "hover node 1");
    TEST_ASSERT(result.hover_changed, "hover changed to node 1");

    result = uitree_input_update(
        &state, tree, (struct UIInputEvent){ UI_INPUT_DOWN, 70, 10, 0 });
    TEST_ASSERT(state.pressed == 1, "pressed node 1");

    result = uitree_input_update(
        &state, tree, (struct UIInputEvent){ UI_INPUT_UP, 70, 10, 0 });
    TEST_ASSERT(result.clicked == 1, "click node 1");

    result = uitree_input_update(
        &state, tree, (struct UIInputEvent){ UI_INPUT_DOWN, 10, 10, 0 });
    result = uitree_input_update(
        &state, tree, (struct UIInputEvent){ UI_INPUT_UP, 70, 10, 0 });
    TEST_ASSERT(result.clicked == -1, "no click when release elsewhere");

    uitree_free(tree);
    return 0;
}

static int
test_behavior_visibility_and_color(void)
{
    struct UITree* tree = uitree_new(2);
    struct UINodeSpec hidden_spec = { 0 };
    hidden_spec.type = UIELEM_RS_RECT;
    hidden_spec.component_id = 1;
    hidden_spec.x = 0;
    hidden_spec.y = 0;
    hidden_spec.width = 20;
    hidden_spec.height = 20;
    hidden_spec.u.rs_rect.color = 0x111111;
    hidden_spec.u.rs_rect.filled = 1;
    int32_t hidden = uitree_push(tree, -1, &hidden_spec);

    struct UINodeSpec button_spec = { 0 };
    button_spec.type = UIELEM_RS_RECT;
    button_spec.component_id = 2;
    button_spec.x = 30;
    button_spec.y = 0;
    button_spec.width = 20;
    button_spec.height = 20;
    button_spec.u.rs_rect.color = 0x222222;
    button_spec.u.rs_rect.filled = 1;
    int32_t button = uitree_push(tree, -1, &button_spec);

    tree->components[hidden].behavior.hide = 1;
    tree->components[button].behavior.over_color = 0xAAAAAA;
    tree->components[button].behavior.active_color = 0xBBBBBB;
    tree->components[button].behavior.active_over_color = 0xCCCCCC;

    TEST_ASSERT(
        !uitree_component_visible(&tree->components[hidden], hidden, -1), "hidden invisible");
    TEST_ASSERT(
        uitree_component_visible(&tree->components[hidden], hidden, hidden), "hidden visible on hover");

    struct UITreeBehaviorHost host = { 0 };
    int color = uitree_component_rect_color(
        &tree->components[button], button, button, &host, 0x222222);
    TEST_ASSERT(color == 0xAAAAAA, "hover color");

    int active_script[] = { 20, 1, 0 };
    int script_lengths[] = { 3 };
    int comparator[] = { 0 };
    int operand[] = { 1 };
    struct StaticUIBehavior active_behavior = {
        .scripts_count = 1,
        .scripts = (int*[]){ active_script },
        .scripts_lengths = script_lengths,
        .script_comparator = comparator,
        .script_operand = operand,
        .active_color = 0xBBBBBB,
        .active_over_color = 0xCCCCCC,
        .over_color = 0xAAAAAA,
    };
    uitree_set_behavior(tree, button, &active_behavior);

    host.csvm = csvm_new();
    color = uitree_component_rect_color(
        &tree->components[button], button, button, &host, 0x222222);
    TEST_ASSERT(color == 0xCCCCCC, "active hover color");

    csvm_free(host.csvm);
    uitree_free(tree);
    return 0;
}

static int
test_behavior_button_toggle(void)
{
    struct UITree* tree = uitree_new(2);
    struct UINodeSpec toggle_spec = { 0 };
    toggle_spec.type = UIELEM_RS_RECT;
    toggle_spec.component_id = 1;
    toggle_spec.x = 0;
    toggle_spec.y = 0;
    toggle_spec.width = 40;
    toggle_spec.height = 40;
    toggle_spec.u.rs_rect.filled = 1;
    int32_t button = uitree_push(tree, -1, &toggle_spec);

    int toggle_script[] = { 5, 0, 0 };
    int operand[] = { 0 };
    struct StaticUIBehavior behavior = {
        .scripts_count = 1,
        .scripts = (int*[]){ toggle_script },
        .script_operand = operand,
        .button_type = COMPONENT_BUTTON_TYPE_TOGGLE,
    };
    uitree_set_behavior(tree, button, &behavior);

    struct VarPVarBitManager mgr;
    varp_varbit_init(&mgr);
    mgr.varp_count = 1;
    mgr.var = calloc(1, sizeof(int));
    mgr.var_serv = calloc(1, sizeof(int));

    struct CSVM* csvm = csvm_new();

    struct UITreeBehaviorHost host = {
        .csvm = csvm,
        .csvm_state = { .get_varp = test_get_varp_cb, .ud = &mgr },
        .varp_varbit = &mgr,
    };

    struct UIInputState state = { .hovered = -1, .pressed = -1 };
    uitree_input_update(&state, tree, (struct UIInputEvent){ UI_INPUT_DOWN, 10, 10, 0 });
    struct UIInputResult result = uitree_input_update(
        &state, tree, (struct UIInputEvent){ UI_INPUT_UP, 10, 10, 0 });
    uitree_behavior_handle_input_result(&host, tree, &result);

    TEST_ASSERT(varp_varbit_get_varp(&mgr, 0) == 1, "toggle varp to 1");

    uitree_behavior_handle_input_result(&host, tree, &result);
    TEST_ASSERT(varp_varbit_get_varp(&mgr, 0) == 0, "toggle varp back to 0");

    csvm_free(csvm);
    varp_varbit_free(&mgr);
    uitree_free(tree);
    return 0;
}

int
main(void)
{
    int failures = 0;
    failures += test_hit_test_basic();
    failures += test_input_hover_and_click();
    failures += test_behavior_visibility_and_color();
    failures += test_behavior_button_toggle();

    if( failures == 0 )
    {
        printf("All ui_input tests passed.\n");
        return 0;
    }

    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
