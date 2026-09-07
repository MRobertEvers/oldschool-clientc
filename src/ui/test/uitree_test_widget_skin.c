/*
 * Native re-skin through the widget API: a plugin retains art and/or a mask on
 * a native picture-bearing widget, the emit walk applies it last and only while
 * the native widget shows a graphic, and reset/release take it back.
 */
#include "test_harness.h"
#include "uitree_frame.h"

static struct UITreeEmitDesc const* desc_for(struct UITreeEmitBuffer const* buffer, int32_t node, enum UITreeEmitKind kind)
{
    for( int i = 0; i < buffer->count; i++ )
        if( buffer->cmds[i].node_index == node && buffer->cmds[i].kind == kind ) return &buffer->cmds[i];
    return NULL;
}

void test_widget_skin(void)
{
    struct UITree* tree = UITree_New(16);
    struct UITreeHost host;
    struct TestHostState state;
    struct UITreeEmitBuffer buffer;
    int32_t root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 795 << 16, 0, 0, 765, 503);
    int32_t sprite = UITree_TestPushXy(tree, root, UIELEM_BUILTIN_SPRITE, -1, 0, 0, 100, 100);
    int32_t compass = UITree_TestPushXy(tree, root, UIELEM_BUILTIN_COMPASS, -1, 10, 10, 32, 32);
    int32_t minimap = UITree_TestPushXy(tree, root, UIELEM_BUILTIN_MINIMAP, (795 << 16) | 4, 50, 50, 100, 100);
    int32_t rect = UITree_TestPushXy(tree, root, UIELEM_RS_RECT, (795 << 16) | 5, 200, 200, 10, 10);
    int32_t owned = UITree_WidgetCreateGraphic(tree, UITree_RefAt(tree, root), 9, "own");
    tree->components[sprite].u.sprite.scene_id = 5;
    tree->components[compass].u.sprite.scene_id = 6;
    tree->components[compass].u.sprite.mask_scene_id = 7;
    tree->components[minimap].u.minimap.mask_scene_id = 8;
    tree->mask_keep_opaque = 1;
    TEST_ASSERT(owned >= 0 && UITree_WidgetSetGraphic(tree, UITree_RefAt(tree, owned), 9, 950, 8, 8), "an owned image control exists");
    UITree_TestHostInit(&host, &state);
    state.minimap_scene_id = 123;
    UITree_TestResolve(tree);
    UITree_EmitBufferInit(&buffer);
#define PUBLISH() do { UITree_TestResolve(tree); buffer.count = 0; UITree_EmitWalk(tree, &host, &buffer, -1); } while( 0 )
    PUBLISH();
    struct UITreeEmitDesc const* d;
    d = desc_for(&buffer, sprite, UITREE_EMIT_SPRITE);
    TEST_ASSERT(d && d->scene_id == 5, "the native sprite emits its own art");
    d = desc_for(&buffer, compass, UITREE_EMIT_COMPASS);
    TEST_ASSERT(d && d->scene_id == 6 && d->mask_scene_id == 7 && d->mask_keep_opaque == 1, "the native compass emits its art, mask and era polarity");
    d = desc_for(&buffer, minimap, UITREE_EMIT_MINIMAP);
    TEST_ASSERT(d && d->mask_scene_id == 8 && d->mask_keep_opaque == 1, "the native minimap emits its mask and era polarity");

    struct UITreeNodeRef const rs = UITree_RefAt(tree, sprite), rc = UITree_RefAt(tree, compass),
                              rm = UITree_RefAt(tree, minimap), rr = UITree_RefAt(tree, rect), ro = UITree_RefAt(tree, owned);
    TEST_ASSERT(UITree_WidgetSetArt(tree, rs, 7, 900) && UITree_WidgetSetArt(tree, rc, 7, 901) &&
                UITree_WidgetSetMask(tree, rc, 7, 902) && UITree_WidgetSetMask(tree, rm, 7, 0),
                "art on sprite and compass, masks on compass and minimap are accepted");
    TEST_ASSERT(!UITree_WidgetSetArt(tree, rm, 7, 903), "the minimap has no art to replace");
    TEST_ASSERT(!UITree_WidgetSetMask(tree, rr, 7, 903), "a rect has no mask");
    TEST_ASSERT(!UITree_WidgetSetArt(tree, rs, 7, 0), "art needs a scene id");
    TEST_ASSERT(!UITree_WidgetSetArt(tree, ro, 9, 904) && !UITree_WidgetSetArt(tree, ro, 7, 904),
                "owned controls take their picture through set_image, not a skin");
    PUBLISH();
    d = desc_for(&buffer, sprite, UITREE_EMIT_SPRITE);
    printf("WIDGET_SKIN sprite art=%d compass art/mask=%d/%d keep=%d minimap mask=%d keep=%d\n",
           d ? d->scene_id : -1,
           desc_for(&buffer, compass, UITREE_EMIT_COMPASS) ? desc_for(&buffer, compass, UITREE_EMIT_COMPASS)->scene_id : -1,
           desc_for(&buffer, compass, UITREE_EMIT_COMPASS) ? desc_for(&buffer, compass, UITREE_EMIT_COMPASS)->mask_scene_id : -1,
           desc_for(&buffer, compass, UITREE_EMIT_COMPASS) ? desc_for(&buffer, compass, UITREE_EMIT_COMPASS)->mask_keep_opaque : -1,
           desc_for(&buffer, minimap, UITREE_EMIT_MINIMAP) ? desc_for(&buffer, minimap, UITREE_EMIT_MINIMAP)->mask_scene_id : -1,
           desc_for(&buffer, minimap, UITREE_EMIT_MINIMAP) ? desc_for(&buffer, minimap, UITREE_EMIT_MINIMAP)->mask_keep_opaque : -1);
    TEST_ASSERT(d && d->scene_id == 900 && d->atlas_index == 0, "the sprite wears the plugin art");
    d = desc_for(&buffer, compass, UITREE_EMIT_COMPASS);
    TEST_ASSERT(d && d->scene_id == 901 && d->mask_scene_id == 902 && d->mask_keep_opaque == 0,
                "the compass wears plugin art and a transparent-is-window plugin mask");
    d = desc_for(&buffer, minimap, UITREE_EMIT_MINIMAP);
    TEST_ASSERT(d && d->mask_scene_id == 0 && d->mask_keep_opaque == 0, "an empty plugin mask draws the minimap unmasked");

    /* Latest writer wins; resetting that writer reveals the earlier one. */
    TEST_ASSERT(UITree_WidgetSetArt(tree, rs, 8, 950), "a second owner re-skins the sprite");
    PUBLISH();
    d = desc_for(&buffer, sprite, UITREE_EMIT_SPRITE);
    TEST_ASSERT(d && d->scene_id == 950, "the latest writer's art shows");
    TEST_ASSERT(UITree_WidgetReset(tree, rs, 8), "the later owner resets");
    PUBLISH();
    d = desc_for(&buffer, sprite, UITREE_EMIT_SPRITE);
    TEST_ASSERT(d && d->scene_id == 900, "the remaining owner's art is revealed");

    /* Releasing the image drops every skin naming it; the mask edit survives. */
    TEST_ASSERT(UITree_WidgetClearSkin(tree, 901) == 1, "one skin named the released image");
    PUBLISH();
    d = desc_for(&buffer, compass, UITREE_EMIT_COMPASS);
    TEST_ASSERT(d && d->scene_id == 6 && d->mask_scene_id == 902, "released art reveals the native compass; the mask stays");
    TEST_ASSERT(UITree_WidgetReset(tree, rc, 7), "the compass owner resets");
    PUBLISH();
    d = desc_for(&buffer, compass, UITREE_EMIT_COMPASS);
    TEST_ASSERT(d && d->mask_scene_id == 7 && d->mask_keep_opaque == 1, "reset restores the native mask and its polarity");

    /* A native widget that stopped showing a graphic is not resurrected by a skin. */
    tree->components[sprite].u.sprite.scene_id = 0;
    UITree_MarkNodeDirty(tree, sprite);
    PUBLISH();
    TEST_ASSERT(desc_for(&buffer, sprite, UITREE_EMIT_SPRITE) == NULL, "a skin does not substitute for a graphic the lane took away");
#undef PUBLISH
    UITree_EmitBufferFree(&buffer);
    UITree_Free(tree);
}
