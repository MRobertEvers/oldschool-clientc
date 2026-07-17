#include "test_harness.h"

void
test_mutate_emit(void)
{
    printf("TEST: mutate / emit / hide\n");

    struct UITree* tree = UITree_New(16);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    int32_t layer = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 400, 0, 0, 200, 200);
    TEST_ASSERT(UITree_ApplyHide(tree, 400, 1), "apply hide");
    TEST_ASSERT(tree->components[layer].behavior.hide == 1, "hide set");
    TEST_ASSERT(!UITree_ComponentVisibleById(&tree->components[layer], -1), "hide gated");
    TEST_ASSERT(UITree_ComponentVisibleById(&tree->components[layer], 400), "hide shown when hovered");
    TEST_ASSERT(UITree_ApplyHide(tree, 400, 0), "unhide");

    int32_t dyn = UITree_CcCreate(tree, layer, 400, 3, 1);
    TEST_ASSERT(dyn >= 0, "cc_create");
    TEST_ASSERT(tree->components[dyn].dynamic == 1, "dynamic");
    TEST_ASSERT(tree->components[dyn].type == UIELEM_RS_RECT, "cc type rect for widget 3");
    TEST_ASSERT(tree->components[layer].is_dirty == 1 || tree->components[dyn].is_dirty == 1,
                "cc dirties");

    int indices[8];
    int n = UITree_CollectDynamicChildIndices(tree, 400, 0, indices, 8);
    TEST_ASSERT(n >= 1, "collect dynamic indices");

    UITree_CcDeleteAll(tree, layer);
    int32_t still = UITree_FindChildBySubid(tree, layer, 400, 1);
    TEST_ASSERT(still < 0 || !tree->components[still].dynamic ||
                    tree->components[still].parent < 0,
                "dynamic detached after deleteall");

    /* EmitFill kinds */
    {
        struct UITreeNodeSpec rect;
        memset(&rect, 0, sizeof(rect));
        rect.type = UIELEM_RS_RECT;
        rect.component_id = 501;
        rect.width = 20;
        rect.height = 20;
        rect.u.rs_rect.color = 0x112233;
        rect.u.rs_rect.filled = 1;
        int32_t ri = UITree_Push(tree, -1, &rect);

        struct UITreeNodeSpec text;
        memset(&text, 0, sizeof(text));
        text.type = UIELEM_RS_TEXT;
        text.component_id = 502;
        text.width = 40;
        text.height = 16;
        text.u.rs_text.font_id = 1;
        text.u.rs_text.text = "ok";
        text.u.rs_text.color = 0xFFFFFF;
        int32_t ti = UITree_Push(tree, -1, &text);

        struct UITreeNodeSpec gfx;
        memset(&gfx, 0, sizeof(gfx));
        gfx.type = UIELEM_RS_GRAPHIC;
        gfx.component_id = 503;
        gfx.width = 16;
        gfx.height = 16;
        gfx.u.rs_graphic.scene_id = 9;
        int32_t gi = UITree_Push(tree, -1, &gfx);

        struct UITreeNodeSpec side;
        memset(&side, 0, sizeof(side));
        side.type = UIELEM_BUILTIN_SIDEBAR;
        side.width = 10;
        side.height = 10;
        side.u.sidebar.tabno = 0;
        int32_t si = UITree_Push(tree, -1, &side);

        UITree_TestResolve(tree);

        struct UITreeEmitDesc desc;
        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[ri], ri, &desc), "emit rect");
        TEST_ASSERT(desc.kind == UITREE_EMIT_RECT, "kind rect");
        TEST_ASSERT(desc.color == 0x112233, "rect color");

        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[ti], ti, &desc), "emit text");
        TEST_ASSERT(desc.kind == UITREE_EMIT_TEXT, "kind text");
        TEST_ASSERT(desc.text && desc.text[0] == 'o', "text ptr");

        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[gi], gi, &desc), "emit graphic");
        TEST_ASSERT(desc.kind == UITREE_EMIT_SPRITE, "kind sprite");
        TEST_ASSERT(desc.scene_id == 9, "sprite scene_id");

        TEST_ASSERT(!UITree_EmitFill(tree, &host, &tree->components[si], si, &desc),
                    "sidebar no emit");
        TEST_ASSERT(!UITree_EmitFill(tree, &host, &tree->components[layer], layer, &desc),
                    "layer no emit without scrollbar");
    }

    TEST_ASSERT(UITree_ApplyColour(tree, 501, 0x99), "apply colour");
    TEST_ASSERT(UITree_ApplyPosition(tree, 501, 3, 4), "apply position");
    TEST_ASSERT(UITree_ApplySize(tree, 501, 11, 12), "apply size");

    UITree_Free(tree);
}
