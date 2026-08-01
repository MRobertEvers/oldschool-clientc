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

    /* EmitWalk hard-skips hidden node and its children (interfacex semantics). */
    {
        struct UITreeNodeSpec child;
        memset(&child, 0, sizeof(child));
        child.type = UIELEM_RS_RECT;
        child.component_id = 450;
        child.width = 10;
        child.height = 10;
        child.u.rs_rect.color = 0xabcdef;
        child.u.rs_rect.filled = 1;
        int32_t ci = UITree_Push(tree, layer, &child);
        TEST_ASSERT(ci >= 0, "push child under layer");

        UITree_TestResolve(tree);
        TEST_ASSERT(UITree_ApplyHide(tree, 400, 1), "hide layer for emit walk");

        struct UITreeEmitBuffer buf;
        UITree_EmitBufferInit(&buf);
        UITree_EmitWalk(tree, &host, &buf, -1);
        TEST_ASSERT(buf.count == 0, "hidden subtree emits nothing");
        UITree_EmitBufferFree(&buf);

        TEST_ASSERT(UITree_ApplyHide(tree, 400, 0), "unhide layer after emit walk");
    }

    int32_t dyn = UITree_CcCreate(tree, layer, 400, 3, 1);
    TEST_ASSERT(dyn >= 0, "cc_create");
    TEST_ASSERT(tree->components[dyn].dynamic == 1, "dynamic");
    TEST_ASSERT(tree->components[dyn].type == UIELEM_RS_RECT, "cc type rect for widget 3");
    TEST_ASSERT(tree->components[dyn].if3 == 0, "cc inherits if3=0 from parent");
    TEST_ASSERT(tree->components[layer].is_dirty == 1 || tree->components[dyn].is_dirty == 1,
                "cc dirties");

    tree->components[layer].if3 = 1;
    int32_t dyn_if3 = UITree_CcCreate(tree, layer, 400, 5, 2);
    TEST_ASSERT(dyn_if3 >= 0, "cc_create graphic under if3 parent");
    TEST_ASSERT(tree->components[dyn_if3].if3 == 1, "cc inherits if3=1 from parent");
    TEST_ASSERT(tree->components[dyn_if3].type == UIELEM_RS_GRAPHIC, "cc type graphic for widget 5");
    tree->components[layer].if3 = 0;

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
        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[ri], ri, -1, &desc), "emit rect");
        TEST_ASSERT(desc.kind == UITREE_EMIT_RECT, "kind rect");
        TEST_ASSERT(desc.color == 0x112233, "rect color");

        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[ti], ti, -1, &desc), "emit text");
        TEST_ASSERT(desc.kind == UITREE_EMIT_TEXT, "kind text");
        TEST_ASSERT(desc.text && desc.text[0] == 'o', "text ptr");

        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[gi], gi, -1, &desc), "emit graphic");
        TEST_ASSERT(desc.kind == UITREE_EMIT_SPRITE, "kind sprite");
        TEST_ASSERT(desc.scene_id == 9, "sprite scene_id");

        TEST_ASSERT(!UITree_EmitFill(tree, &host, &tree->components[si], si, -1, &desc),
                    "sidebar no emit");
        TEST_ASSERT(!UITree_EmitFill(tree, &host, &tree->components[layer], layer, -1, &desc),
                    "layer no emit without scrollbar");

        tree->components[layer].u.rs_layer.scroll_height = 400;
        tree->components[layer].if3 = 0;
        UITree_TestResolve(tree);
        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[layer], layer, -1, &desc),
                    "layer emits scrollbar when scroll_height > height");
        TEST_ASSERT(desc.kind == UITREE_EMIT_SCROLLBAR_V, "kind scrollbar_v");
        TEST_ASSERT(desc.scroll_content == 400, "scroll_content height");
        TEST_ASSERT(desc.w == 16, "scrollbar thickness");

        tree->components[layer].scroll_y = 50;
        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[layer], layer, -1, &desc),
                    "scrollbar emit with scroll_y");
        TEST_ASSERT(desc.scroll_off_y == 50, "scroll_off_y from component");
    }

    TEST_ASSERT(UITree_ApplyColour(tree, 501, 0x99), "apply colour");
    TEST_ASSERT(UITree_ApplyPosition(tree, 501, 3, 4), "apply position");
    TEST_ASSERT(UITree_ApplySize(tree, 501, 11, 12), "apply size");

    UITree_Free(tree);
}

void
test_apply_object_silhouette(void)
{
    printf("TEST: ApplyObject silhouette vs bank grid\n");

    struct UITree* tree = UITree_New(16);

    /* Bank/grid: many CC_OBJ siblings under one parent. SETOBJECT on 0/1 must
     * not hide sibling sub_id 2 (that was the IF 12 / IF 605 3rd-icon bug). */
    {
        int32_t parent = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 100, 0, 0, 200, 200);
        int32_t c0 = UITree_CcCreate(tree, parent, 100, 2, 0);
        int32_t c1 = UITree_CcCreate(tree, parent, 100, 2, 1);
        int32_t c2 = UITree_CcCreate(tree, parent, 100, 2, 2);
        TEST_ASSERT(c0 >= 0 && c1 >= 0 && c2 >= 0, "bank cc_create 0/1/2");
        TEST_ASSERT(tree->components[c0].type == UIELEM_CC_OBJ, "c0 is cc_obj");
        TEST_ASSERT(tree->components[c1].dynamic_child_index == 1, "c1 sub_id 1");
        TEST_ASSERT(tree->components[c2].dynamic_child_index == 2, "c2 sub_id 2");

        TEST_ASSERT(
            UITree_ApplyObject(tree, tree->components[c0].component_id, 995, 1, 10, 0, 0),
            "setobject bank slot 0");
        TEST_ASSERT(
            UITree_ApplyObject(tree, tree->components[c1].component_id, 1333, 1, 11, 0, 0),
            "setobject bank slot 1");
        TEST_ASSERT(!tree->components[c2].behavior.hide, "bank child 2 not hidden by siblings");

        TEST_ASSERT(
            UITree_ApplyObject(tree, tree->components[c2].component_id, 1153, 1, 12, 0, 0),
            "setobject bank slot 2");
        TEST_ASSERT(!tree->components[c2].behavior.hide, "bank child 2 stays visible");
        TEST_ASSERT(tree->components[c2].item_id == 1153, "bank child 2 item set");
        TEST_ASSERT(tree->components[c2].item_scene_id == 12, "bank child 2 scene_id");
    }

    /* Equipment slot: d1 overlay + d2 silhouette — SETOBJECT on overlay hides
     * silhouette; clear unhides it. */
    {
        int32_t slot = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 200, 0, 0, 36, 32);
        int32_t overlay = UITree_CcCreate(tree, slot, 200, 2, 1);
        int32_t sil = UITree_CcCreate(tree, slot, 200, 5, 2);
        TEST_ASSERT(overlay >= 0 && sil >= 0, "equipment d1/d2 create");
        TEST_ASSERT(tree->components[sil].type == UIELEM_RS_GRAPHIC, "d2 is graphic");

        TEST_ASSERT(
            UITree_ApplyObject(tree, tree->components[overlay].component_id, 1153, 1, 20, 0, 0),
            "setobject equipment overlay");
        TEST_ASSERT(tree->components[sil].behavior.hide == 1, "silhouette hidden while occupied");
        TEST_ASSERT(!tree->components[overlay].behavior.hide, "overlay visible");

        TEST_ASSERT(
            UITree_ApplyObject(tree, tree->components[overlay].component_id, -1, 0, -1, 0, 0),
            "clear equipment overlay");
        TEST_ASSERT(tree->components[sil].behavior.hide == 0, "silhouette shown when cleared");

        /* SETOBJECT on static parent redirects to d1 overlay. */
        TEST_ASSERT(UITree_ApplyObject(tree, 200, 1725, 1, 21, 0, 0), "setobject via static parent");
        TEST_ASSERT(tree->components[overlay].item_id == 1725, "redirect set overlay item");
        TEST_ASSERT(tree->components[sil].behavior.hide == 1, "silhouette hidden after redirect");
    }

    /*
     * IF_SETANIM restarts a *different* sequence and leaves the running one
     * alone. The second half is the one with a bug behind it: a dialogue
     * chathead is rebound (model + anim) every time the tree generation moves,
     * so a reset-on-every-apply pinned it to frame 0 for the whole
     * conversation while the animator dutifully advanced it in between.
     */
    {
        int32_t model = UITree_TestPushXy(tree, -1, UIELEM_RS_MODEL, 700, 0, 0, 32, 32);
        TEST_ASSERT(model >= 0, "push model widget");

        TEST_ASSERT(UITree_ApplyModelAnim(tree, 700, 588), "set chathead anim");
        TEST_ASSERT(tree->components[model].u.rs_model.anim_seq_id == 588, "anim seq stored");

        /* Two ticks of the animator. */
        tree->components[model].u.rs_model.anim_frame = 7;
        tree->components[model].u.rs_model.anim_frame_cycle = 3;

        TEST_ASSERT(UITree_ApplyModelAnim(tree, 700, 588), "re-apply the same anim");
        TEST_ASSERT(tree->components[model].u.rs_model.anim_frame == 7,
                    "re-applying the running anim keeps the frame");
        TEST_ASSERT(tree->components[model].u.rs_model.anim_frame_cycle == 3,
                    "re-applying the running anim keeps the cycle");

        TEST_ASSERT(UITree_ApplyModelAnim(tree, 700, 591), "switch to another anim");
        TEST_ASSERT(tree->components[model].u.rs_model.anim_seq_id == 591, "new anim seq stored");
        TEST_ASSERT(tree->components[model].u.rs_model.anim_frame == 0,
                    "a different anim restarts at frame 0");
        TEST_ASSERT(tree->components[model].u.rs_model.anim_frame_cycle == 0,
                    "a different anim restarts at cycle 0");
    }

    UITree_Free(tree);
}
