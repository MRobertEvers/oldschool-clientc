#include "test_harness.h"

/*
 * Health bars and hitsplats draw with the scene, under every interface.
 *
 * The entity overlay is a root-level builtin listed AFTER the gameframe --
 * it has to be, since it is projected against the world rect the gameframe's
 * viewport reports. At a resizable layout that viewport is the whole canvas
 * and the chatbox floats on top of it, so emitting the overlay where the tree
 * lists it painted bars and hitsplats over the chat text. Its own scene clip
 * cannot catch that: the chat is inside the world rect.
 *
 * So the walk hoists the overlay to directly after the world. This pins both
 * halves of that: it lands after the world, and it lands before the chrome
 * that is listed after it (cross, hovertext, minimenu -- which stay on top,
 * as the reference draws them).
 */
void
test_entity_overlay_draw_order(void)
{
    int const root_uid = (700 << 16) | 0;
    int const world_uid = (700 << 16) | 1;
    int const chat_uid = (700 << 16) | 2;
    int const overlay_uid = (700 << 16) | 3;
    int const chrome_uid = (700 << 16) | 4;
    struct UITree* tree = UITree_New(8);
    struct TestHostState hs;
    struct UITreeHost host;
    struct UITreeEmitBuffer emit;
    struct UITreeEntityOverlay bar = {
        .kind = UITREE_ENTITY_OVERLAY_RECT,
        .x = 300,
        .y = 400,
        .w = 30,
        .h = 5,
        .color = 0xFF00FF00u,
    };
    int world_at = -1;
    int chat_at = -1;
    int overlay_at = -1;
    int chrome_at = -1;

    printf("TEST: entity overlays draw with the scene, under the interfaces\n");
    TEST_ASSERT(tree != NULL, "entity-overlay tree allocation");
    if( !tree )
        return;

    /* The resizable shape: a canvas-sized world, a chat panel over its bottom
     * strip, then the overlay and the screen chrome the layout lists last. */
    int32_t root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, root_uid, 0, 0, 0, 0);
    int32_t world = UITree_TestPushXy(
        tree, root, UIELEM_BUILTIN_WORLD, world_uid, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    int32_t chat = UITree_TestPushXy(tree, root, UIELEM_RS_RECT, chat_uid, 0, 380, 500, 140);
    int32_t overlay =
        UITree_TestPushXy(tree, root, UIELEM_BUILTIN_ENTITY_OVERLAY, overlay_uid, 0, 0, 0, 0);
    int32_t chrome = UITree_TestPushXy(tree, root, UIELEM_RS_RECT, chrome_uid, 40, 40, 16, 16);
    (void)world;
    (void)chat;
    (void)overlay;
    (void)chrome;

    UITree_TestHostInit(&host, &hs);
    hs.entity_overlays = &bar;
    hs.entity_overlay_count = 1;
    hs.entity_overlay_clip_w = UITREE_LAYOUT_ROOT_W;
    hs.entity_overlay_clip_h = UITREE_LAYOUT_ROOT_H;

    UITree_TestResolve(tree);
    UITree_EmitBufferInit(&emit);
    UITree_EmitWalk(tree, &host, &emit, -1);

    for( int i = 0; i < emit.count; i++ )
    {
        switch( emit.cmds[i].kind )
        {
        case UITREE_EMIT_WORLD:
            world_at = i;
            break;
        case UITREE_EMIT_ENTITY_OVERLAY:
            overlay_at = i;
            break;
        default:
            if( emit.cmds[i].component_id == chat_uid )
                chat_at = i;
            else if( emit.cmds[i].component_id == chrome_uid )
                chrome_at = i;
            break;
        }
    }

    TEST_ASSERT(world_at >= 0, "world emitted");
    TEST_ASSERT(chat_at >= 0, "chat panel emitted");
    TEST_ASSERT(overlay_at >= 0, "entity overlay emitted");
    TEST_ASSERT(chrome_at >= 0, "screen chrome emitted");
    TEST_ASSERT(overlay_at == world_at + 1, "entity overlay draws directly after the world");
    TEST_ASSERT(overlay_at < chat_at, "entity overlay draws under the chat panel");
    TEST_ASSERT(overlay_at < chrome_at, "screen chrome still draws over the entity overlay");

    UITree_EmitBufferFree(&emit);
    UITree_Free(tree);
}
