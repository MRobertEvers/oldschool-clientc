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

    /* Typed by-index mutation owns all of its cache impacts.  Geometry makes
     * layout stale; paint-only fields do not, and repeated values are quiet. */
    {
        int32_t const graphic =
            UITree_TestPushXy(tree, layer, UIELEM_RS_GRAPHIC, 103, 10, 10, 16, 16);
        int32_t const arc = UITree_TestPushXy(tree, layer, UIELEM_RS_ARC, 104, 0, 0, 20, 20);
        int32_t const model =
            UITree_TestPushXy(tree, layer, UIELEM_RS_MODEL, 105, 0, 0, 40, 40);
        int32_t const minimenu =
            UITree_TestPushXy(tree, -1, UIELEM_BUILTIN_MINIMENU, 106, 0, 0, 0, 0);
        uint32_t dirty_before;

        TEST_ASSERT(graphic >= 0 && arc >= 0 && model >= 0 && minimenu >= 0,
                    "typed-setter nodes pushed");
        UITree_TestResolve(tree);

        UITree_ClearNodeDirty(tree, rect);
        dirty_before = tree->dirty_gen;
        TEST_ASSERT(UITree_SetPositionAt(tree, rect, 8, 9), "set position at");
        TEST_ASSERT(tree->components[rect].position.x == 8 &&
                        tree->components[rect].position.y == 9,
                    "position fields changed");
        TEST_ASSERT(!tree->components[rect].position.layout_resolved && tree->layout_stale,
                    "position invalidates its box and layout walk");
        TEST_ASSERT(tree->components[rect].is_dirty && tree->dirty_gen != dirty_before,
                    "position invalidates retained emit");

        UITree_TestResolve(tree);
        UITree_ClearNodeDirty(tree, rect);
        dirty_before = tree->dirty_gen;
        TEST_ASSERT(UITree_SetPositionAt(tree, rect, 8, 9), "repeat position at");
        TEST_ASSERT(!tree->layout_stale && tree->components[rect].position.layout_resolved,
                    "same position leaves layout current");
        TEST_ASSERT(!tree->components[rect].is_dirty && tree->dirty_gen == dirty_before,
                    "same position is emit-quiet");

        TEST_ASSERT(UITree_SetXYBoxAt(tree, rect, 12, 13, 48, 34), "set explicit XY box at");
        TEST_ASSERT(tree->components[rect].position.kind == UIPOS_XY &&
                        tree->components[rect].position.x == 12 &&
                        tree->components[rect].position.y == 13 &&
                        tree->components[rect].position.width == 48 &&
                        tree->components[rect].position.height == 34 &&
                        tree->layout_stale,
                    "explicit XY box changes all geometry through one impact");
        UITree_TestResolve(tree);

        TEST_ASSERT(UITree_SetSizeModesAt(tree, rect, 50, 35, 0, 0), "set size modes at");
        TEST_ASSERT(!tree->components[rect].position.layout_resolved && tree->layout_stale,
                    "size modes invalidate layout");
        UITree_TestResolve(tree);

        UITree_ClearNodeDirty(tree, layer);
        TEST_ASSERT(UITree_SetScrollSizeAt(tree, layer, 300, 400), "set scroll size at");
        TEST_ASSERT(!tree->components[layer].position.layout_resolved &&
                        !tree->components[rect].position.layout_resolved,
                    "scroll extent invalidates child coordinate-space boxes");
        UITree_TestResolve(tree);
        UITree_ClearNodeDirty(tree, layer);
        TEST_ASSERT(UITree_SetScrollPosAt(tree, layer, 4, 6), "set scroll position at");
        TEST_ASSERT(!tree->layout_stale && tree->components[layer].position.layout_resolved,
                    "scroll offset is paint-only for cached boxes");
        TEST_ASSERT(tree->components[layer].is_dirty, "scroll offset invalidates emit");

        UITree_ClearNodeDirty(tree, text);
        TEST_ASSERT(UITree_SetTextAt(tree, text, "typed"), "set text at");
        TEST_ASSERT(strcmp(tree->components[text].u.rs_text.text, "typed") == 0,
                    "typed text copied");
        TEST_ASSERT(!tree->layout_stale && tree->components[text].is_dirty,
                    "text is paint-only");

        UITree_ClearNodeDirty(tree, graphic);
        TEST_ASSERT(UITree_SetGraphicAt(tree, graphic, 7, 2), "set graphic at");
        TEST_ASSERT(tree->components[graphic].is_dirty && !tree->layout_stale,
                    "graphic is paint-only");

        UITree_ClearNodeDirty(tree, rect);
        TEST_ASSERT(UITree_SetColourAt(tree, rect, 0x123456), "set colour at");
        TEST_ASSERT(tree->components[rect].u.rs_rect.color == 0x123456,
                    "colour updates type payload");
        TEST_ASSERT(UITree_SetFillColourAt(tree, rect, 0x654321), "set fill colour at");
        TEST_ASSERT(UITree_SetTransparencyAt(tree, rect, 128), "set transparency at");
        TEST_ASSERT(tree->components[rect].fill_colour == 0x654321 &&
                        tree->components[rect].trans == 128,
                    "visual fields changed");
        TEST_ASSERT(!tree->layout_stale, "visual setters preserve layout cache");

        UITree_ClearNodeDirty(tree, minimenu);
        TEST_ASSERT(UITree_SetMinimenuFontAt(tree, minimenu, 77), "set minimenu font at");
        TEST_ASSERT(tree->components[minimenu].u.minimenu.font_id == 77 &&
                        tree->components[minimenu].is_dirty,
                    "minimenu font invalidates emit");

        UITree_ClearNodeDirty(tree, arc);
        TEST_ASSERT(UITree_SetArcAnglesAt(tree, arc, 1024, 32768), "set arc angles at");
        TEST_ASSERT(tree->components[arc].u.rs_arc.arc_start == 1024 &&
                        tree->components[arc].u.rs_arc.arc_end == 32768 &&
                        tree->components[arc].is_dirty,
                    "arc angles invalidate emit");

        UITree_ClearNodeDirty(tree, model);
        dirty_before = tree->dirty_gen;
        TEST_ASSERT(UITree_SetModelAt(tree, model, 321), "set model at");
        TEST_ASSERT(tree->components[model].u.rs_model.gamecache_model_id == 321 &&
                        tree->components[model].is_dirty && tree->dirty_gen != dirty_before,
                    "model replacement invalidates retained emit");
        UITree_ClearNodeDirty(tree, model);
        dirty_before = tree->dirty_gen;
        TEST_ASSERT(UITree_SetModelAt(tree, model, 321), "repeat model at");
        TEST_ASSERT(!tree->components[model].is_dirty && tree->dirty_gen == dirty_before,
                    "same model replacement is emit-quiet");

        TEST_ASSERT(UITree_SetModelPoseAt(tree, model, 3, 4, 100, 200, 300, 900),
                    "set model pose at");
        TEST_ASSERT(tree->components[model].u.rs_model.x_offset == 3 &&
                        tree->components[model].u.rs_model.y_offset == 4 &&
                        tree->components[model].u.rs_model.xan == 100 &&
                        tree->components[model].u.rs_model.yan == 200 &&
                        tree->components[model].u.rs_model.zan == 300 &&
                        tree->components[model].u.rs_model.zoom == 900 &&
                        tree->components[model].is_dirty,
                    "model pose invalidates emit");
        TEST_ASSERT(UITree_SetModelPoseAt(tree, model, 5, 6, 101, 201, 301, 0),
                    "set model pose preserving zoom");
        TEST_ASSERT(tree->components[model].u.rs_model.zoom == 900,
                    "non-positive model pose zoom preserves zoom");

        UITree_ClearNodeDirty(tree, layer);
        dirty_before = tree->dirty_gen;
        TEST_ASSERT(UITree_SetHideAt(tree, layer, 0), "set hide at");
        TEST_ASSERT(!tree->components[layer].behavior.hide && tree->components[layer].is_dirty,
                    "hide changes visibility and dirties");
        TEST_ASSERT(tree->dirty_gen != dirty_before, "hide always invalidates reachability");

        UITree_ClearNodeDirty(tree, rect);
        dirty_before = tree->dirty_gen;
        TEST_ASSERT(UITree_SetCS1ActiveAt(tree, rect, 1), "set CS1 active at");
        TEST_ASSERT(tree->components[rect].cs1_active && tree->components[rect].is_dirty &&
                        tree->dirty_gen != dirty_before,
                    "CS1 active result invalidates retained emit");
        UITree_ClearNodeDirty(tree, rect);
        dirty_before = tree->dirty_gen;
        TEST_ASSERT(UITree_SetCS1ValueAt(tree, rect, 0, 42), "set CS1 value at");
        TEST_ASSERT(tree->components[rect].cs1_values[0] == 42 &&
                        tree->components[rect].is_dirty && tree->dirty_gen != dirty_before,
                    "CS1 placeholder result invalidates retained emit");

        UITree_ClearNodeDirty(tree, layer);
        dirty_before = tree->dirty_gen;
        TEST_ASSERT(UITree_SetFrameHiddenAt(tree, layer, 1), "set frame hidden at");
        TEST_ASSERT(tree->components[layer].frame_hidden && tree->components[layer].is_dirty &&
                        tree->dirty_gen != dirty_before,
                    "frame suppression invalidates reachability");
        TEST_ASSERT(UITree_SetFrameHiddenAt(tree, layer, 0), "release frame hidden at");

        TEST_ASSERT(!UITree_SetPositionAt(tree, -1, 0, 0), "invalid index rejected");
        TEST_ASSERT(!UITree_SetCS1ValueAt(tree, rect, UITREE_CS1_VALUE_MAX, 0),
                    "invalid CS1 value index rejected");
        TEST_ASSERT(!UITree_SetGraphicAt(tree, rect, 0, 0), "wrong graphic type rejected");
        TEST_ASSERT(!UITree_SetModelAt(tree, rect, 0), "wrong model type rejected");
        TEST_ASSERT(!UITree_SetScrollSizeAt(tree, rect, 0, 0), "wrong layer type rejected");
    }

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
