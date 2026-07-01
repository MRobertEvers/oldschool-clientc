#include "sidenav_fixture.h"
#include "ui/ui_input.h"
#include "ui/uitree.h"
#include "ui/uitree_host.h"
#include "ui/uitree_layout.h"
#include "ui_tree_draw.h"
#include <sys/stat.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            return 1;                                                                              \
        }                                                                                          \
    } while( 0 )

#define OUTPUT_DIR "output"
#define BG_COLOR ((int)0xFF181818)

static int g_selected_tab = 3;
static int g_last_set_tab = -1;

static int
mock_get_selected_tab(void* user)
{
    (void)user;
    return g_selected_tab;
}

static void
mock_set_selected_tab(void* user, int tabno)
{
    (void)user;
    g_last_set_tab = tabno;
    g_selected_tab = tabno;
}

static int
ensure_output_dir(void)
{
#ifdef _WIN32
    return _mkdir(OUTPUT_DIR);
#else
    return mkdir(OUTPUT_DIR, 0755);
#endif
}

static int
click_tab(
    struct UITree* tree, struct UITreeHost* host, struct UIInputState* state,
    struct SidenavTabSlot const* slot, int32_t expected_index, int expected_tabno)
{
    int const cx = sidenav_fixture_click_center_x(slot);
    int const cy = sidenav_fixture_click_center_y(slot);

    uitree_input_update(state, tree, (struct UIInputEvent){ UI_INPUT_DOWN, cx, cy, 0 });
    struct UIInputResult result =
        uitree_input_update(state, tree, (struct UIInputEvent){ UI_INPUT_UP, cx, cy, 0 });

    TEST_ASSERT(result.clicked == expected_index, "click hit expected tab icon");
    uitree_behavior_handle_click_host(host, tree, result.clicked);
    TEST_ASSERT(g_last_set_tab == expected_tabno, "set_selected_tab called");
    TEST_ASSERT(g_selected_tab == expected_tabno, "selected tab updated");
    return 0;
}

static int
render_and_write(
    struct SidenavFixture* fixture, struct UITreeHost* host, int* pixels, char const* filename)
{
    uitree_layout_resolve(fixture->tree, 0, 0, SIDENAV_CANVAS_W, SIDENAV_CANVAS_H);

    struct UiTreeDrawContext draw = {
        .pixels = pixels,
        .width = SIDENAV_CANVAS_W,
        .height = SIDENAV_CANVAS_H,
        .sprites = &fixture->sprites,
        .host = host,
        .hovered = -1,
    };
    ui_tree_draw_clear(&draw, BG_COLOR);
    ui_tree_draw_tree(&draw, fixture->tree, fixture->root_index);

    char path[256];
    snprintf(path, sizeof(path), OUTPUT_DIR "/%s", filename);
    ui_tree_draw_write_bmp_cropped(&draw, path);
    return 0;
}

static int
assert_redstone_active(int* pixels, int tabno)
{
    int const x = sidenav_fixture_redstone_sample_x(tabno);
    int const y = sidenav_fixture_redstone_sample_y(tabno);
    int const sample = pixels[y * SIDENAV_CANVAS_W + x];
    TEST_ASSERT(sample != BG_COLOR, "active redstone drawn");
    return 0;
}

static int
test_layout_nested(void)
{
    struct UITree* tree = uitree_new(8);
    TEST_ASSERT(tree != NULL, "tree alloc");

    struct UINodeSpec root_spec = { 0 };
    root_spec.type = UIELEM_RS_LAYER;
    root_spec.component_id = -1;
    root_spec.x = 0;
    root_spec.y = 0;
    root_spec.width = 200;
    root_spec.height = 200;
    int32_t root = uitree_push(tree, -1, &root_spec);

    struct UINodeSpec child_spec = { 0 };
    child_spec.type = UIELEM_RS_RECT;
    child_spec.component_id = 1;
    child_spec.x = 10;
    child_spec.y = 10;
    child_spec.width = 50;
    child_spec.height = 50;
    child_spec.u.rs_rect.color = 0xFF0000;
    child_spec.u.rs_rect.filled = 1;
    int32_t child = uitree_push(tree, root, &child_spec);

    uitree_layout_resolve(tree, 0, 0, 200, 200);

    int cx = 0;
    int cy = 0;
    int cw = 0;
    int ch = 0;
    uitree_layout_get_bounds(&tree->components[child].position, &cx, &cy, &cw, &ch);
    TEST_ASSERT(cx == 10 && cy == 10, "child abs x/y");
    TEST_ASSERT(uitree_hit_test(tree, 20, 20) == child, "nested hit");

    uitree_free(tree);
    return 0;
}

static void
assert_node_abs_bounds(
    struct UITree* tree,
    int32_t node,
    int expected_x,
    int expected_y,
    char const* label)
{
    int ax = 0;
    int ay = 0;
    int aw = 0;
    int ah = 0;
    uitree_layout_get_bounds(&tree->components[node].position, &ax, &ay, &aw, &ah);
    if( ax != expected_x || ay != expected_y )
    {
        fprintf(
            stderr,
            "FAIL: %s abs=(%d,%d) expected=(%d,%d)\n",
            label,
            ax,
            ay,
            expected_x,
            expected_y);
        exit(1);
    }
}

static int
test_rs_layer_nested_child_layout(void)
{
    struct UITree* tree = uitree_new(16);
    TEST_ASSERT(tree != NULL, "tree alloc");

    struct UINodeSpec outer_spec = { 0 };
    outer_spec.type = UIELEM_RS_LAYER;
    outer_spec.component_id = 1;
    outer_spec.x = 10;
    outer_spec.y = 20;
    outer_spec.width = 180;
    outer_spec.height = 200;
    int32_t outer_layer = uitree_push(tree, -1, &outer_spec);
    TEST_ASSERT(outer_layer >= 0, "outer layer push");

    struct UINodeSpec text_a_spec = { 0 };
    text_a_spec.type = UIELEM_RS_TEXT;
    text_a_spec.component_id = 2;
    text_a_spec.x = 5;
    text_a_spec.y = 8;
    text_a_spec.width = 40;
    text_a_spec.height = 12;
    text_a_spec.u.rs_text.font_id = 1;
    text_a_spec.u.rs_text.text = "Next";
    int32_t text_a = uitree_push(tree, outer_layer, &text_a_spec);
    TEST_ASSERT(text_a >= 0, "text_a push");

    struct UINodeSpec text_b_spec = { 0 };
    text_b_spec.type = UIELEM_RS_TEXT;
    text_b_spec.component_id = 3;
    text_b_spec.x = 60;
    text_b_spec.y = 8;
    text_b_spec.width = 40;
    text_b_spec.height = 12;
    text_b_spec.u.rs_text.font_id = 1;
    text_b_spec.u.rs_text.text = "level";
    int32_t text_b = uitree_push(tree, outer_layer, &text_b_spec);
    TEST_ASSERT(text_b >= 0, "text_b push");

    struct UINodeSpec inner_spec = { 0 };
    inner_spec.type = UIELEM_RS_LAYER;
    inner_spec.component_id = 4;
    inner_spec.x = 0;
    inner_spec.y = 40;
    inner_spec.width = 180;
    inner_spec.height = 100;
    int32_t inner_layer = uitree_push(tree, outer_layer, &inner_spec);
    TEST_ASSERT(inner_layer >= 0, "inner layer push");

    struct UINodeSpec text_c_spec = { 0 };
    text_c_spec.type = UIELEM_RS_TEXT;
    text_c_spec.component_id = 5;
    text_c_spec.x = 12;
    text_c_spec.y = 6;
    text_c_spec.width = 40;
    text_c_spec.height = 12;
    text_c_spec.u.rs_text.font_id = 1;
    text_c_spec.u.rs_text.text = "at:";
    int32_t text_c = uitree_push(tree, inner_layer, &text_c_spec);
    TEST_ASSERT(text_c >= 0, "text_c push");

    uitree_layout_resolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    assert_node_abs_bounds(tree, outer_layer, 10, 20, "outer_layer");
    assert_node_abs_bounds(tree, text_a, 15, 28, "text_a");
    assert_node_abs_bounds(tree, text_b, 70, 28, "text_b");
    assert_node_abs_bounds(tree, inner_layer, 10, 60, "inner_layer");
    assert_node_abs_bounds(tree, text_c, 22, 66, "text_c");

    TEST_ASSERT(uitree_hit_test(tree, 20, 30) == text_a, "hit text_a");
    TEST_ASSERT(uitree_hit_test(tree, 75, 30) == text_b, "hit text_b");
    TEST_ASSERT(uitree_hit_test(tree, 30, 70) == text_c, "hit text_c");
    TEST_ASSERT(uitree_hit_test(tree, 75, 30) != text_a, "text_b does not hit text_a");

    /* Sidebar mount offset propagates through nested layers. */
    struct UINodeSpec sidebar_spec = { 0 };
    sidebar_spec.type = UIELEM_BUILTIN_SIDEBAR;
    sidebar_spec.x = 553;
    sidebar_spec.y = 205;
    sidebar_spec.width = 190;
    sidebar_spec.height = 261;
    sidebar_spec.u.sidebar.tabno = 1;
    int32_t sidebar = uitree_push(tree, -1, &sidebar_spec);
    TEST_ASSERT(sidebar >= 0, "sidebar push");

    struct UINodeSpec mounted_layer_spec = { 0 };
    mounted_layer_spec.type = UIELEM_RS_LAYER;
    mounted_layer_spec.component_id = 10;
    mounted_layer_spec.x = 5;
    mounted_layer_spec.y = 10;
    mounted_layer_spec.width = 180;
    mounted_layer_spec.height = 200;
    int32_t mounted_layer = uitree_push(tree, sidebar, &mounted_layer_spec);
    TEST_ASSERT(mounted_layer >= 0, "mounted layer push");

    struct UINodeSpec mounted_text_spec = { 0 };
    mounted_text_spec.type = UIELEM_RS_TEXT;
    mounted_text_spec.component_id = 11;
    mounted_text_spec.x = 20;
    mounted_text_spec.y = 15;
    mounted_text_spec.width = 80;
    mounted_text_spec.height = 12;
    mounted_text_spec.u.rs_text.font_id = 1;
    mounted_text_spec.u.rs_text.text = "mounted";
    int32_t mounted_text = uitree_push(tree, mounted_layer, &mounted_text_spec);
    TEST_ASSERT(mounted_text >= 0, "mounted text push");

    uitree_layout_resolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    assert_node_abs_bounds(tree, mounted_text, 553 + 5 + 20, 205 + 10 + 15, "mounted_text");

    uitree_free(tree);
    return 0;
}

static int
test_sidebar_tab_click(void)
{
    struct UITree* tree = uitree_new(4);
    struct UINodeSpec tab_spec = { 0 };
    tab_spec.type = UIELEM_BUILTIN_TAB_ICONS;
    tab_spec.x = 100;
    tab_spec.y = 100;
    tab_spec.width = 32;
    tab_spec.height = 32;
    tab_spec.u.tab_icon.tabno = 5;
    tab_spec.u.tab_icon.scene_id = -1;
    tab_spec.u.tab_icon.atlas_index = 0;
    int32_t tab = uitree_push(tree, -1, &tab_spec);
    uitree_layout_resolve(tree, 0, 0, 765, 503);

    struct UITreeHost host = { 0 };
    host.user = NULL;
    host.get_selected_tab = mock_get_selected_tab;
    host.set_selected_tab = mock_set_selected_tab;

    TEST_ASSERT(uitree_hit_test(tree, 110, 110) == tab, "tab icon hit inside bounds");
    TEST_ASSERT(uitree_hit_test(tree, 99, 110) < 0, "tab icon miss left of bounds");

    g_last_set_tab = -1;
    uitree_behavior_handle_click_host(&host, tree, tab);
    TEST_ASSERT(g_last_set_tab == 5, "tab click sets selected tab");

    uitree_free(tree);
    return 0;
}

static int
test_sidenav_interactive(void)
{
    ensure_output_dir();

    struct SidenavFixture fixture;
    TEST_ASSERT(sidenav_fixture_build(&fixture), "sidenav fixture build");

    struct UITreeHost host = { 0 };
    host.get_selected_tab = mock_get_selected_tab;
    host.set_selected_tab = mock_set_selected_tab;

    g_selected_tab = 3;
    g_last_set_tab = -1;

    int* pixels = calloc((size_t)SIDENAV_CANVAS_W * (size_t)SIDENAV_CANVAS_H, sizeof(int));
    TEST_ASSERT(pixels != NULL, "pixel buffer alloc");

    render_and_write(&fixture, &host, pixels, "00_initial_tab3.bmp");
    int const inv_active_sample = pixels
        [sidenav_fixture_redstone_sample_y(3) * SIDENAV_CANVAS_W +
         sidenav_fixture_redstone_sample_x(3)];
    int const equip_baseline = pixels
        [sidenav_fixture_redstone_sample_y(4) * SIDENAV_CANVAS_W +
         sidenav_fixture_redstone_sample_x(4)];
    if( assert_redstone_active(pixels, 3) != 0 )
        goto fail;

    struct UIInputState input = { .hovered = -1, .pressed = -1 };
    struct SidenavTabSlot const* equipment = sidenav_fixture_find_tab(&fixture, 4);
    struct SidenavTabSlot const* prayer = sidenav_fixture_find_tab(&fixture, 5);
    struct SidenavTabSlot const* inventory = sidenav_fixture_find_tab(&fixture, 3);
    TEST_ASSERT(equipment && prayer && inventory, "fixture tab lookup");

    if( click_tab(fixture.tree, &host, &input, equipment, fixture.equipment_icon_index, 4) != 0 )
        goto fail;
    render_and_write(&fixture, &host, pixels, "01_click_equipment_tab4.bmp");
    if( assert_redstone_active(pixels, 4) != 0 )
        goto fail;
    TEST_ASSERT(
        pixels
                [sidenav_fixture_redstone_sample_y(3) * SIDENAV_CANVAS_W +
                 sidenav_fixture_redstone_sample_x(3)] != inv_active_sample,
        "inventory redstone highlight cleared");
    TEST_ASSERT(
        pixels
                [sidenav_fixture_redstone_sample_y(4) * SIDENAV_CANVAS_W +
                 sidenav_fixture_redstone_sample_x(4)] != equip_baseline,
        "equipment redstone highlight appeared");

    if( click_tab(fixture.tree, &host, &input, prayer, fixture.prayer_icon_index, 5) != 0 )
        goto fail;
    render_and_write(&fixture, &host, pixels, "02_click_prayer_tab5.bmp");
    if( assert_redstone_active(pixels, 5) != 0 )
        goto fail;

    if( click_tab(fixture.tree, &host, &input, inventory, fixture.inventory_icon_index, 3) != 0 )
        goto fail;
    render_and_write(&fixture, &host, pixels, "03_click_inventory_tab3.bmp");
    if( assert_redstone_active(pixels, 3) != 0 )
        goto fail;

    free(pixels);
    sidenav_fixture_free(&fixture);
    return 0;

fail:
    free(pixels);
    sidenav_fixture_free(&fixture);
    return 1;
}

int
main(void)
{
    int failures = 0;
    failures += test_layout_nested();
    failures += test_rs_layer_nested_child_layout();
    failures += test_sidebar_tab_click();
    failures += test_sidenav_interactive();

    if( failures == 0 )
    {
        printf("All ui_tree tests passed.\n");
        printf("BMP output written to %s/\n", OUTPUT_DIR);
        return 0;
    }

    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
