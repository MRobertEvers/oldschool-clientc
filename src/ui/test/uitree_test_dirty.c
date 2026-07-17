#include "test_harness.h"

#include <stdlib.h>

void
test_dirty_marking(void)
{
    printf("TEST: dirty marking\n");

    struct UITree* tree = UITree_New(16);
    TEST_ASSERT(tree != NULL, "UITree_New");

    int32_t layer = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 100, 0, 0, 200, 150);
    int32_t rect = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, 101, 5, 5, 40, 30);
    TEST_ASSERT(layer >= 0 && rect >= 0, "push nodes");

    TEST_ASSERT(tree->components[layer].is_dirty == 1, "push marks layer dirty");
    TEST_ASSERT(tree->components[rect].is_dirty == 1, "push marks rect dirty");
    TEST_ASSERT(UITree_NodeNeedsEmit(&tree->components[rect]), "needs emit when dirty");

    UITree_ClearNodeDirty(tree, rect);
    TEST_ASSERT(tree->components[rect].is_dirty == 0, "clear dirty");
    TEST_ASSERT(!UITree_NodeNeedsEmit(&tree->components[rect]), "no emit when clean");

    UITree_MarkNodeDirty(tree, rect);
    TEST_ASSERT(tree->components[rect].is_dirty == 1, "mark node dirty");

    UITree_ClearNodeDirty(tree, layer);
    UITree_ClearNodeDirty(tree, rect);
    UITree_MarkAllDirty(tree);
    TEST_ASSERT(tree->components[layer].is_dirty == 1, "mark all dirty layer");
    TEST_ASSERT(tree->components[rect].is_dirty == 1, "mark all dirty rect");

    /* always_dirty keeps emit after clear */
    tree->components[rect].always_dirty = 1;
    UITree_ClearNodeDirty(tree, rect);
    TEST_ASSERT(UITree_NodeNeedsEmit(&tree->components[rect]), "always_dirty still needs emit");
    tree->components[rect].always_dirty = 0;
    TEST_ASSERT(!UITree_NodeNeedsEmit(&tree->components[rect]), "cleared always_dirty");

    /* Frame always-dirty types */
    int32_t world = UITree_TestPushXy(tree, -1, UIELEM_BUILTIN_WORLD, -1, 0, 0, 100, 100);
    int32_t compass = UITree_TestPushXy(tree, -1, UIELEM_BUILTIN_COMPASS, -1, 0, 0, 32, 32);
    UITree_ClearNodeDirty(tree, world);
    UITree_ClearNodeDirty(tree, compass);
    UITree_MarkFrameAlwaysDirtyTypes(tree);
    TEST_ASSERT(tree->components[world].is_dirty == 1, "world dirty after frame mark");
    TEST_ASSERT(tree->components[world].always_dirty == 1, "world always_dirty set");
    TEST_ASSERT(tree->components[compass].always_dirty == 1, "compass always_dirty set");
    TEST_ASSERT(UITree_TypeIsAlwaysDirtyFrame(UIELEM_BUILTIN_MINIMAP), "minimap always-dirty type");
    TEST_ASSERT(!UITree_TypeIsAlwaysDirtyFrame(UIELEM_RS_RECT), "rect not always-dirty type");

    /* Apply_* re-dirties */
    UITree_ClearNodeDirty(tree, layer);
    TEST_ASSERT(UITree_ApplyHide(tree, 100, 1), "apply hide");
    TEST_ASSERT(tree->components[layer].is_dirty == 1, "apply hide dirties");

    struct UITreeNodeSpec text_spec;
    memset(&text_spec, 0, sizeof(text_spec));
    text_spec.type = UIELEM_RS_TEXT;
    text_spec.component_id = 102;
    text_spec.x = 0;
    text_spec.y = 0;
    text_spec.width = 50;
    text_spec.height = 20;
    text_spec.u.rs_text.font_id = 1;
    text_spec.u.rs_text.text = "hi";
    int32_t text = UITree_Push(tree, layer, &text_spec);
    UITree_ClearNodeDirty(tree, text);
    TEST_ASSERT(UITree_ApplyText(tree, 102, "bye"), "apply text");
    TEST_ASSERT(tree->components[text].is_dirty == 1, "apply text dirties");

    /* Emit-loop simulation: second frame only always_dirty */
    UITree_MarkAllDirty(tree);
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( UITree_NodeNeedsEmit(&tree->components[i]) )
            UITree_ClearNodeDirty(tree, (int32_t)i);
    }
    int need_after = 0;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( UITree_NodeNeedsEmit(&tree->components[i]) )
            need_after++;
    }
    TEST_ASSERT(need_after >= 1, "always_dirty world/compass still need emit");
    TEST_ASSERT(!UITree_NodeNeedsEmit(&tree->components[rect]), "clean rect no emit after frame");

    UITree_Free(tree);
}
