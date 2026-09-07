/*
 * A frame PROVIDED through the widget API: the engine takes the lane's chrome
 * and binds the roles, but places and hides no surface -- unlike a declaration,
 * whose unplaced surfaces are hidden. The provider's retained edits then move
 * the surfaces without the declaration's ownership gate standing in the way.
 */
#include "test_harness.h"
#include "uitree_frame.h"

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
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER; spec.component_id = (group << 16) | 3;
    spec.x = 0; spec.y = 338; spec.width = 519; spec.height = 165; spec.slot_tag = UITREE_SLOT_CHAT;
    int32_t chat = UITree_Push(tree, root, &spec);
    tree->components[chrome].u.sprite.scene_id = 5;
    UITree_TestHostInit(&host, &state);
    UITree_TestResolve(tree);

    /* Contrast: an empty DECLARATION hides every surface it did not place. */
    {
        struct UITreeFrameSlotRect slots[UITREE_FRAME_SLOT_COUNT] = { 0 };
        UITree_FrameApply(tree, slots, group);
        TEST_ASSERT(tree->components[chrome].frame_hidden && tree->components[chat].frame_hidden,
                    "an empty declaration hides the chrome and the unplaced chat");
        UITree_FrameRelease(tree);
        TEST_ASSERT(!tree->components[chrome].frame_hidden && !tree->components[chat].frame_hidden, "release reveals both");
    }

    UITree_FrameProvide(tree, group);
    printf("FRAME_PROVIDE active=%d hidden=%d chrome=%d chat=%d world=%d owned=%d\n", UITree_FrameActive(tree),
           UITree_FrameHiddenCount(tree), tree->components[chrome].frame_hidden, tree->components[chat].frame_hidden,
           tree->components[world].frame_hidden, UITree_FramePositionOwned(tree, chat));
    TEST_ASSERT(UITree_FrameActive(tree) && tree->components[chrome].frame_hidden,
                "a provided frame suppresses the lane's chrome");
    TEST_ASSERT(!tree->components[chat].frame_hidden && !tree->components[world].frame_hidden,
                "a provided frame hides no surface: the provider decides with set_hidden");
    TEST_ASSERT(UITree_FrameSlotCount(tree, UITREE_FRAME_SLOT_CHAT) == 1 && UITree_FrameSlotCount(tree, UITREE_FRAME_SLOT_VIEWPORT) == 1,
                "the roles are bound so staleness and reassert keep working");
    TEST_ASSERT(!UITree_FramePositionOwned(tree, chat), "no declaration owns the chat's box");
    TEST_ASSERT(UITree_WidgetSetPosition(tree, UITree_RefAt(tree, chat), 7, 17, 357), "the provider moves the chat with a retained edit");
    UITree_TestResolve(tree);
    TEST_ASSERT(tree->components[chat].position.abs_x == 17 && tree->components[chat].position.abs_y == 357,
                "the retained edit is the chat's effective box under a provided frame");

    /* The fence's reassert keeps a provided frame provided. */
    tree->generation++;
    UITree_FrameReassert(tree);
    TEST_ASSERT(UITree_FrameActive(tree) && tree->components[chrome].frame_hidden && !tree->components[chat].frame_hidden,
                "reassert re-collects the chrome and still hides no surface");

    UITree_FrameRelease(tree);
    TEST_ASSERT(!tree->components[chrome].frame_hidden && !UITree_FrameActive(tree), "release gives the chrome back");
    UITree_Free(tree);
}
