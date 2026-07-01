#include "ui/ui_input.h"
#include "ui/ui_behavior.h"
#include "ui/uitree.h"
#include "ui/ui_scroll.h"
#include "ui/uitree_host.h"
#include "ui/uitree_layout.h"
#include "input/libtorirs_input.h"
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
        uitree_component_visible(&tree->components[hidden], hidden, 1), "hidden visible on hover id");

    struct UITreeBehaviorHost host = { 0 };
    int color = uitree_component_rect_color(
        &tree->components[button], 2, &host, 0x222222);
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
        &tree->components[button], 2, &host, 0x222222);
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

static int
test_overlayer_hover_id(void)
{
    struct UITree* tree = uitree_new(4);
    struct UINodeSpec tooltip_spec = { 0 };
    tooltip_spec.type = UIELEM_RS_LAYER;
    tooltip_spec.component_id = 100;
    tooltip_spec.x = 0;
    tooltip_spec.y = 0;
    tooltip_spec.width = 80;
    tooltip_spec.height = 20;
    int32_t tooltip = uitree_push(tree, -1, &tooltip_spec);
    tree->components[tooltip].behavior.hide = 1;

    struct UINodeSpec icon_spec = { 0 };
    icon_spec.type = UIELEM_RS_GRAPHIC;
    icon_spec.component_id = 200;
    icon_spec.x = 0;
    icon_spec.y = 0;
    icon_spec.width = 32;
    icon_spec.height = 32;
    icon_spec.u.rs_graphic.scene_id = 0;
    icon_spec.u.rs_graphic.atlas_index = 0;
    int32_t icon = uitree_push(tree, -1, &icon_spec);
    tree->components[icon].behavior.over_layer_id = 100;

    uitree_layout_resolve(tree, 0, 0, 200, 200);

    int hovered_id = -1;
    uitree_find_hovered_component_id(tree, NULL, NULL, 10, 10, &hovered_id);
    TEST_ASSERT(hovered_id == 100, "overLayer maps hover to tooltip component id");
    TEST_ASSERT(
        !uitree_component_visible_by_id(&tree->components[tooltip], -1),
        "hidden tooltip invisible at rest");
    TEST_ASSERT(
        uitree_component_visible_by_id(&tree->components[tooltip], 100),
        "hidden tooltip visible when hovered id matches");

    uitree_free(tree);
    return 0;
}

static bool
test_always_active(
    void* user,
    struct StaticUIComponent const* component)
{
    (void)user;
    (void)component;
    return true;
}

static int
test_active_text_source(void)
{
    struct UITree* tree = uitree_new(2);
    struct UINodeSpec text_spec = { 0 };
    text_spec.type = UIELEM_RS_TEXT;
    text_spec.component_id = 5;
    text_spec.u.rs_text.text = "inactive";
    text_spec.u.rs_text.text_active = "active";
    text_spec.u.rs_text.color = 0x111111;
    int32_t node = uitree_push(tree, -1, &text_spec);

    struct UITreeHost host;
    uitree_host_init(&host);

    TEST_ASSERT(
        strcmp(uitree_component_text_source_host(&host, &tree->components[node]), "inactive") == 0,
        "inactive text when not active");

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
    };
    uitree_set_behavior(tree, node, &active_behavior);

    host.is_active = test_always_active;
    TEST_ASSERT(
        strcmp(uitree_component_text_source_host(&host, &tree->components[node]), "active") == 0,
        "active text when getIfActive");

    uitree_free(tree);
    return 0;
}

static int
test_hover_color_without_scripts(void)
{
    struct UITree* tree = uitree_new(2);
    struct UINodeSpec rect_spec = { 0 };
    rect_spec.type = UIELEM_RS_RECT;
    rect_spec.component_id = 9;
    rect_spec.x = 0;
    rect_spec.y = 0;
    rect_spec.width = 20;
    rect_spec.height = 20;
    rect_spec.u.rs_rect.color = 0x111111;
    rect_spec.u.rs_rect.filled = 1;
    int32_t node = uitree_push(tree, -1, &rect_spec);
    tree->components[node].behavior.over_color = 0xAAAAAA;

    struct UITreeHost host;
    uitree_host_init(&host);

    int color = uitree_component_rect_color_host(
        &tree->components[node], 9, &host, tree->components[node].u.rs_rect.color);
    TEST_ASSERT(color == 0xAAAAAA, "hover color without CS1 scripts");

    uitree_free(tree);
    return 0;
}

static int
test_scroll_offset_apply(void)
{
    struct UITree* tree = uitree_new(4);
    struct UINodeSpec layer_spec = { 0 };
    layer_spec.type = UIELEM_RS_LAYER;
    layer_spec.component_id = 50;
    layer_spec.x = 10;
    layer_spec.y = 20;
    layer_spec.width = 100;
    layer_spec.height = 80;
    layer_spec.u.rs_layer.scroll_height = 500;
    int32_t layer = uitree_push(tree, -1, &layer_spec);

    struct UINodeSpec text_spec = { 0 };
    text_spec.type = UIELEM_RS_TEXT;
    text_spec.component_id = 51;
    text_spec.x = 0;
    text_spec.y = 200;
    text_spec.width = 80;
    text_spec.height = 14;
    text_spec.u.rs_text.text = "quest";
    int32_t text = uitree_push(tree, layer, &text_spec);

    uitree_layout_resolve(tree, 0, 0, 765, 503);

    int scroll_x[UITREE_SCROLL_MAX] = { 0 };
    int scroll_y[UITREE_SCROLL_MAX] = { 0 };
    scroll_y[50] = 150;
    struct UITreeScrollState scroll = { .scroll_x = scroll_x, .scroll_y = scroll_y };

    int32_t ancestors[] = { layer };
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    struct UITreeScrollClip clip = { 0 };
    uitree_layout_get_bounds(&tree->components[text].position, &bx, &by, &bw, &bh);
    uitree_scroll_apply_ancestors(tree, &scroll, ancestors, 1, &bx, &by, &clip);
    TEST_ASSERT(by == 70, "child Y offset by layer scroll");
    TEST_ASSERT(clip.clip_w == 100 && clip.clip_h == 80, "layer clip applied");

    uitree_free(tree);
    return 0;
}

static int
test_scrollbar_handle_gating(void)
{
    struct UITree* tree = uitree_new(4);
    struct UINodeSpec layer_spec = { 0 };
    layer_spec.type = UIELEM_RS_LAYER;
    layer_spec.component_id = 60;
    layer_spec.x = 100;
    layer_spec.y = 50;
    layer_spec.width = 100;
    layer_spec.height = 80;
    layer_spec.u.rs_layer.scroll_height = 500;
    int32_t layer = uitree_push(tree, -1, &layer_spec);
    uitree_layout_resolve(tree, 0, 0, 765, 503);

    int scroll_x[UITREE_SCROLL_MAX] = { 0 };
    int scroll_y[UITREE_SCROLL_MAX] = { 0 };
    scroll_y[60] = 40;
    struct UITreeScrollState scroll = { .scroll_x = scroll_x, .scroll_y = scroll_y };

    struct UITreeScrollbarHitInfo hit = { 0 };
    hit.kind = UITREE_SCROLLBAR_V_TRACK;
    hit.layer_index = layer;
    hit.layer_x = 100;
    hit.layer_y = 50;
    hit.layer_w = 100;
    hit.layer_h = 80;
    hit.scroll_height = 500;
    hit.scroll_width = 0;

    int const track_py = hit.layer_y + UITREE_SCROLLBAR_THICKNESS + 20;
    TEST_ASSERT(
        !uitree_scrollbar_handle(
            tree, &scroll, &hit, hit.layer_x + hit.layer_w + 8, track_py,
            UITREE_SCROLLBAR_ACTION_NONE, 0),
        "hover over track does not scroll");
    TEST_ASSERT(scroll_y[60] == 40, "scroll unchanged on hover");

    TEST_ASSERT(
        !uitree_scrollbar_handle(
            tree, &scroll, &hit, hit.layer_x + hit.layer_w + 8, track_py,
            UITREE_SCROLLBAR_ACTION_ARROW_STEP, 4),
        "track hit ignores arrow action");
    TEST_ASSERT(scroll_y[60] == 40, "scroll unchanged when arrow action on track");

    hit.kind = UITREE_SCROLLBAR_V_DOWN;
    TEST_ASSERT(
        uitree_scrollbar_handle(
            tree, &scroll, &hit, hit.layer_x + hit.layer_w + 8,
            hit.layer_y + hit.layer_h - 8, UITREE_SCROLLBAR_ACTION_ARROW_STEP, 4),
        "arrow step applies on down arrow");
    TEST_ASSERT(scroll_y[60] == 44, "scroll increased by arrow step");

    scroll_y[60] = 40;
    hit.kind = UITREE_SCROLLBAR_V_TRACK;
    TEST_ASSERT(
        uitree_scrollbar_handle(
            tree, &scroll, &hit, hit.layer_x + hit.layer_w + 8, track_py,
            UITREE_SCROLLBAR_ACTION_GRIP_DRAG, 0),
        "grip drag updates scroll");
    TEST_ASSERT(scroll_y[60] != 40, "scroll changed by grip drag");

    uitree_free(tree);
    return 0;
}

static void
input_tick(
    struct LibToriRS_Input* input,
    uint64_t time)
{
    LibToriRS_Input_Begin(input, time);
    LibToriRS_Input_End(input);
}

static int
test_input_drag_lifecycle(void)
{
    struct LibToriRS_Input input_storage;
    struct LibToriRS_Input* input = LibToriRS_Input_Init(&input_storage, 0);
    TEST_ASSERT(input != NULL, "input init");

    LibToriRS_Input_Begin(input, 100);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 10, 10);
    LibToriRS_Input_End(input);

    TEST_ASSERT(LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT), "mouse down edge on press");
    TEST_ASSERT(LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT), "mouse held on press");
    TEST_ASSERT(!LibToriRS_Input_IsDragging(input, TORIRSM_LEFT), "not dragging before deadzone");

    input_tick(input, 110);
    TEST_ASSERT(!LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT), "mouse down edge only first tick");
    TEST_ASSERT(LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT), "mouse held on move tick");

    LibToriRS_Input_Begin(input, 120);
    LibToriRS_Input_PushMouseMove(input, 10, 20);
    LibToriRS_Input_End(input);

    TEST_ASSERT(LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT), "mouse held after move");
    TEST_ASSERT(LibToriRS_Input_IsDragging(input, TORIRSM_LEFT), "dragging after deadzone");
    TEST_ASSERT(LibToriRS_Input_IsDragStart(input, TORIRSM_LEFT), "drag start on deadzone cross");

    input_tick(input, 130);
    TEST_ASSERT(LibToriRS_Input_IsDragging(input, TORIRSM_LEFT), "dragging continues");
    TEST_ASSERT(!LibToriRS_Input_IsDragStart(input, TORIRSM_LEFT), "drag start only first drag tick");

    LibToriRS_Input_Begin(input, 140);
    LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, 10, 20);
    LibToriRS_Input_End(input);

    TEST_ASSERT(!LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT), "mouse not held after release");
    TEST_ASSERT(!LibToriRS_Input_IsClick(input, TORIRSM_LEFT), "no click after drag");
    TEST_ASSERT(LibToriRS_Input_IsDragEnd(input, TORIRSM_LEFT), "drag end on release");

    LibToriRS_Input_Begin(input, 200);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 50, 50);
    LibToriRS_Input_End(input);
    LibToriRS_Input_Begin(input, 210);
    LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, 50, 50);
    LibToriRS_Input_End(input);

    TEST_ASSERT(LibToriRS_Input_IsClick(input, TORIRSM_LEFT), "click when released without drag");
    TEST_ASSERT(!LibToriRS_Input_IsDragEnd(input, TORIRSM_LEFT), "no drag end without drag");

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
    failures += test_overlayer_hover_id();
    failures += test_active_text_source();
    failures += test_hover_color_without_scripts();
    failures += test_scroll_offset_apply();
    failures += test_scrollbar_handle_gating();
    failures += test_input_drag_lifecycle();

    if( failures == 0 )
    {
        printf("All ui_input tests passed.\n");
        return 0;
    }

    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
