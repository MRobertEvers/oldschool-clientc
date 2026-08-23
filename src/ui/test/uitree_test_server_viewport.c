#include "test_harness.h"

/*
 * Three viewport widgets the SERVER decides the visibility of, none of which
 * any interface owns: the minimap the server can take away (MINIMAP_TOGGLE),
 * the multi-combat indicator (SET_MULTIWAY), and the system-update countdown
 * (UPDATE_REBOOT_TIMER).
 *
 * All three spent a while decoded-but-inert -- the packet reached the client
 * and stopped there. The property that makes them alive is that the emit walk
 * asks the host and changes its output, so that is what this pins: the same
 * tree, walked twice, emitting different things.
 */
void
test_server_driven_viewport_widgets(void)
{
    int const root_uid = (900 << 16) | 0;
    int const map_uid = (900 << 16) | 1;
    int const multi_uid = (900 << 16) | 2;
    int const reboot_uid = (900 << 16) | 3;
    struct UITree* tree = UITree_New(8);
    struct TestHostState hs;
    struct UITreeHost host;
    struct UITreeEmitBuffer emit;
    int32_t root, map, multi, reboot;

    printf("TEST: minimap toggle, multiway icon and reboot countdown gate on the host\n");
    TEST_ASSERT(tree != NULL, "server-viewport tree allocation");
    if( !tree )
        return;

    root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, root_uid, 0, 0, 0, 0);
    map = UITree_TestPushXy(tree, root, UIELEM_BUILTIN_MINIMAP, map_uid, 575, 9, 146, 151);
    multi = UITree_TestPushXy(tree, root, UIELEM_BUILTIN_MULTIWAY, multi_uid, 476, 300, 16, 16);
    reboot = UITree_TestPushXy(tree, root, UIELEM_BUILTIN_REBOOT_TIMER, reboot_uid, 8, 333, 0, 0);
    TEST_ASSERT(map >= 0 && multi >= 0 && reboot >= 0, "server-viewport nodes pushed");

    /* Both widgets carry what they draw with; revconfig fills these at bake,
     * and neither is allowed to invent one when the host says "draw". */
    tree->components[multi].u.sprite.scene_id = 77;
    tree->components[multi].u.sprite.atlas_index = 1;
    tree->components[reboot].u.reboot_timer.font_id = 3;
    tree->components[reboot].u.reboot_timer.color = 16776960;

    UITree_TestHostInit(&host, &hs);
    /* A baked map to withhold. Without one the minimap emits nothing anyway,
     * and "hidden" would be indistinguishable from "not loaded yet". */
    hs.minimap_scene_id = 42;
    UITree_TestResolve(tree);

    /* Nothing happening: the map draws, and neither the icon nor the countdown
     * does. */
    UITree_EmitBufferInit(&emit);
    UITree_EmitWalk(tree, &host, &emit, -1);
    {
        int maps = 0;
        int sprites = 0;
        int texts = 0;
        for( int i = 0; i < emit.count; i++ )
        {
            if( emit.cmds[i].node_index == map && emit.cmds[i].kind == UITREE_EMIT_MINIMAP )
                maps++;
            if( emit.cmds[i].node_index == multi && emit.cmds[i].kind == UITREE_EMIT_SPRITE )
                sprites++;
            if( emit.cmds[i].node_index == reboot && emit.cmds[i].kind == UITREE_EMIT_TEXT )
                texts++;
        }
        TEST_ASSERT(maps == 1, "the minimap draws when the server has not hidden it");
        TEST_ASSERT(sprites == 0, "no multiway icon outside a multi-combat zone");
        TEST_ASSERT(texts == 0, "no countdown with no update pending");
    }

    /* MINIMAP_TOGGLE: the map goes away and nothing replaces it -- the hole in
     * the mapback frame art is what shows through. */
    hs.minimap_hidden = 1;
    UITree_EmitBufferInit(&emit);
    UITree_EmitWalk(tree, &host, &emit, -1);
    {
        int maps = 0;
        for( int i = 0; i < emit.count; i++ )
            if( emit.cmds[i].node_index == map )
                maps++;
        TEST_ASSERT(maps == 0, "a hidden minimap emits nothing at all");
    }
    hs.minimap_hidden = 0;

    /* Server says both. */
    hs.multiway = 1;
    hs.reboot_timer_text = "System update in: 1:00";
    UITree_EmitBufferInit(&emit);
    UITree_EmitWalk(tree, &host, &emit, -1);
    {
        struct UITreeEmitDesc const* icon = NULL;
        struct UITreeEmitDesc const* line = NULL;
        for( int i = 0; i < emit.count; i++ )
        {
            if( emit.cmds[i].node_index == multi && emit.cmds[i].kind == UITREE_EMIT_SPRITE )
                icon = &emit.cmds[i];
            if( emit.cmds[i].node_index == reboot && emit.cmds[i].kind == UITREE_EMIT_TEXT )
                line = &emit.cmds[i];
        }
        TEST_ASSERT(icon != NULL, "multiway icon emits in a multi-combat zone");
        if( icon )
        {
            /* Frame 1 of the pack, not frame 0: dropping the atlas index here
             * would draw protect-from-melee over the viewport instead. */
            TEST_ASSERT(icon->scene_id == 77, "multiway icon uses the widget's sprite");
            TEST_ASSERT(icon->atlas_index == 1, "multiway icon keeps its atlas frame");
            TEST_ASSERT(icon->x == 476 && icon->y == 300, "multiway icon sits where placed");
        }
        TEST_ASSERT(line != NULL, "countdown emits while an update is pending");
        if( line )
        {
            TEST_ASSERT(
                strcmp(line->text, "System update in: 1:00") == 0, "countdown shows host text");
            TEST_ASSERT(line->font_id == 3, "countdown uses the widget's font");
            TEST_ASSERT(line->color == 16776960, "countdown uses the widget's colour");
            /* The reference draws this with drawString(s, x, y), so y is the
             * baseline. Without the flag the line would be aligned inside a
             * zero-height box instead and land a font's height too high. */
            TEST_ASSERT(line->text_baseline == 1, "countdown is baseline-positioned");
        }
    }

    /* An armed widget with nothing to draw with stays silent rather than
     * emitting a blank blit: the sprite pack can fail to load. */
    tree->components[multi].u.sprite.scene_id = -1;
    UITree_EmitBufferInit(&emit);
    UITree_EmitWalk(tree, &host, &emit, -1);
    {
        int sprites = 0;
        for( int i = 0; i < emit.count; i++ )
            if( emit.cmds[i].node_index == multi && emit.cmds[i].kind == UITREE_EMIT_SPRITE )
                sprites++;
        TEST_ASSERT(sprites == 0, "multiway icon with no sprite emits nothing");
    }

    UITree_Free(tree);
}
