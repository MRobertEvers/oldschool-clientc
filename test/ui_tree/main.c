#include "sidenav_fixture.h"
#include "ui_tree_draw.h"
#include "ui/uitree.h"
#include "ui/uitree_host.h"
#include "ui/uitree_layout.h"
#include "ui/ui_input.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TEST_ASSERT(cond, msg) \
    do \
    { \
        if( !(cond) ) \
        { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            return 1; \
        } \
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
    struct UITree* tree,
    struct UITreeHost* host,
    struct UIInputState* state,
    struct SidenavTabSlot const* slot,
    int32_t expected_index,
    int expected_tabno)
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
    struct SidenavFixture* fixture,
    struct UITreeHost* host,
    int* pixels,
    char const* filename)
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
assert_redstone_active(
    int* pixels,
    int tabno)
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

    int32_t root = uitree_push_rs_layer(tree, -1, -1, 0, 0, 200, 200);
    int32_t child = uitree_push_rs_rect(tree, root, 1, 0xFF0000, 1, 10, 10, 50, 50);

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

static int
test_sidebar_tab_click(void)
{
    struct UITree* tree = uitree_new(4);
    int32_t tab = uitree_push_tab_icon(tree, -1, 5, -1, 0, 100, 100, 32, 32);
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
    int const inv_active_sample =
        pixels[sidenav_fixture_redstone_sample_y(3) * SIDENAV_CANVAS_W +
               sidenav_fixture_redstone_sample_x(3)];
    int const equip_baseline =
        pixels[sidenav_fixture_redstone_sample_y(4) * SIDENAV_CANVAS_W +
               sidenav_fixture_redstone_sample_x(4)];
    if( assert_redstone_active(pixels, 3) != 0 )
        goto fail;

    struct UIInputState input = { .hovered = -1, .pressed = -1 };
    struct SidenavTabSlot const* equipment = sidenav_fixture_find_tab(&fixture, 4);
    struct SidenavTabSlot const* prayer = sidenav_fixture_find_tab(&fixture, 5);
    struct SidenavTabSlot const* inventory = sidenav_fixture_find_tab(&fixture, 3);
    TEST_ASSERT(equipment && prayer && inventory, "fixture tab lookup");

    if( click_tab(
            fixture.tree, &host, &input, equipment, fixture.equipment_icon_index, 4) != 0 )
        goto fail;
    render_and_write(&fixture, &host, pixels, "01_click_equipment_tab4.bmp");
    if( assert_redstone_active(pixels, 4) != 0 )
        goto fail;
    TEST_ASSERT(
        pixels[sidenav_fixture_redstone_sample_y(3) * SIDENAV_CANVAS_W +
               sidenav_fixture_redstone_sample_x(3)] != inv_active_sample,
        "inventory redstone highlight cleared");
    TEST_ASSERT(
        pixels[sidenav_fixture_redstone_sample_y(4) * SIDENAV_CANVAS_W +
               sidenav_fixture_redstone_sample_x(4)] != equip_baseline,
        "equipment redstone highlight appeared");

    if( click_tab(fixture.tree, &host, &input, prayer, fixture.prayer_icon_index, 5) != 0 )
        goto fail;
    render_and_write(&fixture, &host, pixels, "02_click_prayer_tab5.bmp");
    if( assert_redstone_active(pixels, 5) != 0 )
        goto fail;

    if( click_tab(
            fixture.tree, &host, &input, inventory, fixture.inventory_icon_index, 3) != 0 )
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
