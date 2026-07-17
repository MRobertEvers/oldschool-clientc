#include "test_harness.h"

#include <stdlib.h>

static struct UIBuildComponent const* g_build_comps;
static int g_build_count;

static struct UIBuildComponent const*
get_comp(void* ud, int index)
{
    (void)ud;
    if( index < 0 || index >= g_build_count )
        return NULL;
    return &g_build_comps[index];
}

static int
get_parent_id(void* ud, int index)
{
    (void)ud;
    if( index < 0 || index >= g_build_count )
        return -1;
    return g_build_comps[index].parent_id;
}

static int
resolve_sprite(void* ud, int graphic_id)
{
    (void)ud;
    return graphic_id >= 0 ? graphic_id + 1000 : -1;
}

void
test_layout_build(void)
{
    printf("TEST: layout / build / scroll\n");

    /* Parent + child XY abs bounds */
    {
        struct UITree* tree = UITree_New(8);
        int32_t layer = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 1, 10, 20, 200, 150);
        int32_t rect = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, 2, 5, 5, 50, 30);
        UITree_TestResolve(tree);

        TEST_ASSERT(tree->components[layer].position.abs_x == 10, "layer abs_x");
        TEST_ASSERT(tree->components[layer].position.abs_y == 20, "layer abs_y");
        TEST_ASSERT(tree->components[rect].position.abs_x == 15, "rect abs_x");
        TEST_ASSERT(tree->components[rect].position.abs_y == 25, "rect abs_y");
        TEST_ASSERT(tree->components[rect].position.abs_w == 50, "rect abs_w");
        UITree_Free(tree);
    }

    /* IF3 mode 0 identity via has_position */
    {
        struct UITree* tree = UITree_New(4);
        struct UITreeNodeSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_RECT;
        spec.component_id = 7;
        spec.has_position = 1;
        spec.position.kind = UIPOS_XY;
        spec.position.x = 40;
        spec.position.y = 50;
        spec.position.width = 20;
        spec.position.height = 10;
        spec.position.x_mode = 0;
        spec.position.y_mode = 0;
        spec.position.width_mode = 0;
        spec.position.height_mode = 0;
        spec.u.rs_rect.filled = 1;
        int32_t idx = UITree_Push(tree, -1, &spec);
        UITree_TestResolve(tree);
        TEST_ASSERT(tree->components[idx].position.abs_x == 40, "if3 mode0 x");
        TEST_ASSERT(tree->components[idx].position.abs_y == 50, "if3 mode0 y");
        TEST_ASSERT(tree->components[idx].position.abs_w == 20, "if3 mode0 w");
        UITree_Free(tree);
    }

    /* BuildFromSource parent-first */
    {
        struct UIBuildComponent comps[3];
        memset(comps, 0, sizeof(comps));
        comps[0].id = 100;
        comps[0].type = UIBUILD_LAYER;
        comps[0].parent_id = -1;
        comps[0].base_width = 100;
        comps[0].base_height = 80;
        comps[1].id = 101;
        comps[1].type = UIBUILD_RECT;
        comps[1].parent_id = 100;
        comps[1].base_x = 2;
        comps[1].base_y = 3;
        comps[1].base_width = 10;
        comps[1].base_height = 10;
        comps[1].color = 0xABCDEF;
        comps[1].filled = 1;
        comps[2].id = 102;
        comps[2].type = UIBUILD_GRAPHIC;
        comps[2].parent_id = 100;
        comps[2].base_x = 20;
        comps[2].graphic = 5;

        g_build_comps = comps;
        g_build_count = 3;

        struct UITree* tree = UITree_New(8);
        struct UITreeBuildSource src = {
            .count = 3,
            .get_component = get_comp,
            .get_parent_id = get_parent_id,
            .resolve_sprite = resolve_sprite,
            .resolve_font = NULL,
            .ud = NULL,
        };
        TEST_ASSERT(UITree_BuildFromSource(tree, &src) == 3, "build from source");
        TEST_ASSERT(tree->component_count == 3, "built 3 nodes");
        int32_t gidx = UITree_FindByComponentId(tree, 102);
        TEST_ASSERT(gidx >= 0, "graphic found");
        TEST_ASSERT(tree->components[gidx].u.rs_graphic.scene_id == 1005, "sprite resolve");
        UITree_TestResolve(tree);
        int32_t ridx = UITree_FindByComponentId(tree, 101);
        TEST_ASSERT(tree->components[ridx].position.abs_x == 2, "built rect abs_x");
        UITree_Free(tree);
    }

    /* Scrollbar hit on tall layer */
    {
        struct UITree* tree = UITree_New(4);
        struct TestHostState hs;
        struct UITreeHost host;
        UITree_TestHostInit(&host, &hs);

        int32_t L = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 60, 0, 0, 100, 80);
        tree->components[L].u.rs_layer.scroll_height = 400;
        UITree_TestResolve(tree);
        TEST_ASSERT(UITree_ScrollLayerNeedsVertical(&tree->components[L]), "needs vertical scroll");

        int* sx = calloc((size_t)UITREE_SCROLL_MAX, sizeof(int));
        int* sy = calloc((size_t)UITREE_SCROLL_MAX, sizeof(int));
        TEST_ASSERT(sx && sy, "scroll alloc");
        struct UITreeScrollState scroll = { .scroll_x = sx, .scroll_y = sy };

        struct UITreeScrollbarHitInfo hit;
        memset(&hit, 0, sizeof(hit));
        /* Vertical bar is to the right of layer: x in [100, 116) */
        bool found = UITree_FindScrollbarAt(tree, &host, &scroll, 108, 40, &hit);
        TEST_ASSERT(found, "scrollbar hit found");
        TEST_ASSERT(hit.layer_index == L, "scrollbar layer index");
        TEST_ASSERT(hit.kind != UITREE_SCROLLBAR_NONE, "scrollbar kind set");

        free(sx);
        free(sy);
        UITree_Free(tree);
    }
}
