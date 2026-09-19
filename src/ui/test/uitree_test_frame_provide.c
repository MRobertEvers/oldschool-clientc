/*
 * A frame PROVIDED through the widget API: the engine takes the lane's chrome
 * and binds the roles, but places and hides no surface. The provider's
 * retained edits then move the surfaces, and every container above a surface
 * THE PROVIDER moved stops clipping -- another plugin's retained nudge on a
 * native row releases nothing.
 */
#include "test_harness.h"
#include "uitree_frame.h"

enum
{
    PROVIDER_OWNER = 7,
    OTHER_OWNER = 8,
};

/* Whether `layer` still clips its children at its resolved box -- the emit,
 * hit, hover and drop walks all ask this one rule. */
static int
layer_clips(struct UITree const* tree, int32_t layer)
{
    struct UITreeComponent const* c = &tree->components[layer];
    struct UITreeScrollClip child;
    struct UITreeScrollClip surface;
    return UITree_LayerChildClip(c, NULL, c->position.abs_x, c->position.abs_y,
                                 c->position.width, c->position.height, &child, &surface)
           ? 1 : 0;
}

void test_frame_provide(void)
{
    struct UITree* tree = UITree_New(16);
    struct UITreeHost host;
    struct TestHostState state;
    struct UITreeNodeSpec spec;
    int const group = 796;
    int32_t root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, group << 16, 0, 0, 765, 503);
    int32_t chrome = UITree_TestPushXy(tree, root, UIELEM_BUILTIN_SPRITE, -1, 0, 0, 765, 503);
    int32_t world = UITree_TestPushXy(tree, root, UIELEM_BUILTIN_WORLD, (group << 16) | 2, 4, 4, 512, 334);
    /* The chat sits inside a container the size of the lane's chat block, the
     * way a CS2 toplevel mounts its packs. */
    int32_t chat_box = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, (group << 16) | 9, 0, 338, 519, 165);
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER; spec.component_id = (group << 16) | 3;
    spec.x = 0; spec.y = 0; spec.width = 519; spec.height = 165; spec.slot_tag = UITREE_SLOT_CHAT;
    int32_t chat = UITree_Push(tree, chat_box, &spec);
    /* A cache-owned scroll layer the frame never places -- a settings list,
     * a quest journal -- with a row inside it. It clips because its content
     * is taller than its box. */
    int32_t scroll_layer = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, (group << 16) | 20, 553, 205, 190, 261);
    tree->components[scroll_layer].u.rs_layer.scroll_height = 900;
    int32_t scroll_row = UITree_TestPushXy(tree, scroll_layer, UIELEM_RS_RECT, (group << 16) | 21, 0, 40, 180, 20);
    tree->components[chrome].u.sprite.scene_id = 5;
    UITree_TestHostInit(&host, &state);
    UITree_TestResolve(tree);

    UITree_FrameProvide(tree, group, PROVIDER_OWNER);
    printf("FRAME_PROVIDE active=%d hidden=%d chrome=%d chat=%d world=%d\n", UITree_FrameActive(tree),
           UITree_FrameHiddenCount(tree), tree->components[chrome].frame_hidden, tree->components[chat].frame_hidden,
           tree->components[world].frame_hidden);
    TEST_ASSERT(UITree_FrameActive(tree) && tree->components[chrome].frame_hidden,
                "a provided frame suppresses the lane's chrome");
    TEST_ASSERT(!tree->components[chat].frame_hidden && !tree->components[world].frame_hidden,
                "a provided frame hides no surface: the provider decides with set_hidden");
    TEST_ASSERT(UITree_FrameSlotCount(tree, UITREE_FRAME_SLOT_CHAT) == 1 && UITree_FrameSlotCount(tree, UITREE_FRAME_SLOT_VIEWPORT) == 1,
                "the roles are bound so staleness and reassert keep working");
    TEST_ASSERT(!tree->components[chat_box].frame_stretched, "an unmoved chat leaves its container clipping");
    TEST_ASSERT(UITree_WidgetSetPosition(tree, UITree_RefAt(tree, chat), PROVIDER_OWNER, 17, 19), "the provider moves the chat with a retained edit");
    /* Another plugin -- a highlighter, a tweak -- nudges a row inside the
     * scroll layer the provider left alone. Its edit is retained on the same
     * widget-edit list the provider's are. */
    TEST_ASSERT(UITree_WidgetSetPosition(tree, UITree_RefAt(tree, scroll_row), OTHER_OWNER, 2, 40),
                "a second owner nudges a row under a native scroll layer");
    /* The app provides again after the provider's edits, at the layout fence. */
    UITree_FrameProvide(tree, group, PROVIDER_OWNER);
    UITree_TestResolve(tree);
    TEST_ASSERT(tree->components[chat].position.abs_x == 17 && tree->components[chat].position.abs_y == 357,
                "the retained edit is the chat's effective box under a provided frame");
    TEST_ASSERT(tree->components[chat_box].frame_stretched && !tree->components[chat].frame_stretched,
                "moving a surface releases the containers above it and keeps its own containment");
    printf("FRAME_PROVIDE other_owner scroll_layer stretched=%d clips=%d row_x=%d\n",
           tree->components[scroll_layer].frame_stretched, layer_clips(tree, scroll_layer),
           tree->components[scroll_row].position.abs_x);
    TEST_ASSERT(tree->components[scroll_row].position.abs_x == 555,
                "the second owner's nudge is still the row's effective box");
    TEST_ASSERT(!tree->components[scroll_layer].frame_stretched && layer_clips(tree, scroll_layer),
                "a second owner's nudge under a native scroll layer the provider did not place leaves that layer clipping");

    /* The provider's own move of that row is a placement, and does release. */
    TEST_ASSERT(UITree_WidgetSetPosition(tree, UITree_RefAt(tree, scroll_row), PROVIDER_OWNER, 4, 40),
                "the provider moves the same row");
    UITree_FrameProvide(tree, group, PROVIDER_OWNER);
    UITree_TestResolve(tree);
    TEST_ASSERT(tree->components[scroll_layer].frame_stretched && !layer_clips(tree, scroll_layer),
                "the provider's own move of a row releases the scroll layer above it");
    TEST_ASSERT(UITree_WidgetReset(tree, UITree_RefAt(tree, scroll_row), PROVIDER_OWNER),
                "the provider drops its edit on the row");
    UITree_FrameProvide(tree, group, PROVIDER_OWNER);
    UITree_TestResolve(tree);
    TEST_ASSERT(!tree->components[scroll_layer].frame_stretched && layer_clips(tree, scroll_layer),
                "with only the other owner's nudge left the layer clips again");

    /* The fence's reassert keeps a provided frame provided. */
    tree->generation++;
    UITree_FrameReassert(tree);
    TEST_ASSERT(UITree_FrameActive(tree) && tree->components[chrome].frame_hidden && !tree->components[chat].frame_hidden,
                "reassert re-collects the chrome and still hides no surface");

    UITree_FrameRelease(tree);
    TEST_ASSERT(!tree->components[chrome].frame_hidden && !UITree_FrameActive(tree), "release gives the chrome back");
    UITree_Free(tree);
}
