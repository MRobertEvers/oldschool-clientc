#include "test_harness.h"

/*
 * Scripted entity overlays: an interface component hung off something in the
 * world (game/rs_entity_overlay.h, src/game/rs_client_trigger.h).
 *
 * This is the DRAW half, which is the half a screenshot cannot pin down: the
 * cache only builds these when a setting is on, so "nothing on screen" is the
 * same picture whether the feature is off, the trigger never fired, or the
 * layer drew somewhere nobody can see.
 *
 * Three things have to hold, and each of them was wrong at some point while
 * this was being written:
 *
 *   - the layer's children reach the emit list at all,
 *   - they are HOISTED with the world rather than left where the tree lists
 *     them, which is after the gameframe and so over the inventory,
 *   - the `entity_overlay` builtin CLIPS them, so a 60x60 marker on a loc at
 *     the edge of the viewport does not spill onto the side panels.
 */

/* The world rect the App writes onto the overlay builtin each frame. */
#define OV_WORLD_X 4
#define OV_WORLD_Y 4
#define OV_WORLD_W 512
#define OV_WORLD_H 334

static int32_t
overlay_build_tree(
    struct UITree* tree,
    int root_uid,
    int world_uid,
    int overlay_uid,
    int panel_uid)
{
    int32_t root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, root_uid, 0, 0, 0, 0);
    int32_t world = UITree_TestPushXy(
        tree, root, UIELEM_BUILTIN_WORLD, world_uid, OV_WORLD_X, OV_WORLD_Y, OV_WORLD_W, OV_WORLD_H);
    /* The panel is listed BEFORE the overlay, exactly as the manifest lists the
     * gameframe before entity_overlay -- which is what makes the hoist the only
     * thing standing between a world marker and the inventory. */
    int32_t panel = UITree_TestPushXy(tree, root, UIELEM_RS_RECT, panel_uid, 520, 4, 250, 300);
    int32_t overlay = UITree_TestPushXy(
        tree,
        root,
        UIELEM_BUILTIN_ENTITY_OVERLAY,
        overlay_uid,
        OV_WORLD_X,
        OV_WORLD_Y,
        OV_WORLD_W,
        OV_WORLD_H);
    (void)world;
    (void)panel;
    return overlay;
}

void
test_scripted_entity_overlay(void)
{
    int const root_uid = (701 << 16) | 0;
    int const world_uid = (701 << 16) | 1;
    int const panel_uid = (701 << 16) | 2;
    int const overlay_uid = (701 << 16) | 3;
    struct UITree* tree = UITree_New(8);
    struct TestHostState hs;
    struct UITreeHost host;
    struct UITreeEmitBuffer emit;
    int32_t overlay;
    int32_t layer;
    int32_t child;
    int child_com;
    int world_at = -1;
    int panel_at = -1;
    int child_at = -1;

    printf("TEST: scripted entity overlays draw in the world, clipped to it\n");
    TEST_ASSERT(tree != NULL, "scripted-overlay tree allocation");
    if( !tree )
        return;

    overlay = overlay_build_tree(tree, root_uid, world_uid, overlay_uid, panel_uid);
    TEST_ASSERT(overlay >= 0, "entity_overlay builtin pushed");
    TEST_ASSERT(
        tree->entity_overlay_index == overlay,
        "the tree tracks the entity_overlay builtin as a singleton");

    /* What OVERLAY_LOC_CREATE does: a 60x60 layer for overlay index 0. */
    layer = UITree_EntityOverlayCreateLayer(tree, 0, 60, 60);
    TEST_ASSERT(layer >= 0, "overlay layer created under the builtin");
    if( layer < 0 )
    {
        UITree_Free(tree);
        return;
    }
    TEST_ASSERT(tree->components[layer].parent == overlay, "the layer hangs off the builtin");

    /* ... and what the App does each frame: put it where the subject projects.
     * Relative to the builtin, which is at the world rect. */
    tree->components[layer].position.x = 100;
    tree->components[layer].position.y = 80;

    /* What OVERLAY_CC_CREATE + cc_setsize does: a graphic inside it. */
    child = UITree_CcCreate(tree, layer, tree->components[layer].component_id, 3, 0);
    TEST_ASSERT(child >= 0, "overlay child created");
    if( child < 0 )
    {
        UITree_Free(tree);
        return;
    }
    child_com = tree->components[child].component_id;
    tree->components[child].position.width = 36;
    tree->components[child].position.height = 32;

    UITree_TestHostInit(&host, &hs);
    UITree_LayoutInvalidateBoxes(tree);
    UITree_TestResolve(tree);
    UITree_EmitBufferInit(&emit);
    UITree_EmitWalk(tree, &host, &emit, -1);

    for( int i = 0; i < emit.count; i++ )
    {
        if( emit.cmds[i].kind == UITREE_EMIT_WORLD )
            world_at = i;
        else if( emit.cmds[i].component_id == panel_uid )
            panel_at = i;
        else if( emit.cmds[i].component_id == child_com )
            child_at = i;
    }

    TEST_ASSERT(world_at >= 0, "world emitted");
    TEST_ASSERT(panel_at >= 0, "side panel emitted");
    TEST_ASSERT(child_at >= 0, "the overlay's child reached the emit list");
    if( child_at < 0 )
    {
        UITree_EmitBufferFree(&emit);
        UITree_Free(tree);
        return;
    }

    /* Hoisted: the panel is listed first in the tree and must still draw over
     * the marker. Without the subtree hoist the marker lands after it. */
    TEST_ASSERT(child_at > world_at, "the marker draws after the world");
    TEST_ASSERT(child_at < panel_at, "the marker draws UNDER the side panel");

    /* Absolute box: the builtin's world rect + the layer offset the App wrote. */
    TEST_ASSERT(
        emit.cmds[child_at].x == OV_WORLD_X + 100 && emit.cmds[child_at].y == OV_WORLD_Y + 80,
        "the marker sits where the App anchored its layer");
    TEST_ASSERT(
        emit.cmds[child_at].w == 36 && emit.cmds[child_at].h == 32,
        "the marker keeps the size the script gave it");

    /* Clipped to the world, not to the canvas. */
    /*
     * Clipped INSIDE the world, not to the canvas.
     *
     * Not equality with the world rect: the layer is itself a clipping layer,
     * so what reaches the child is the layer's own box already intersected
     * with the world -- which is the point. The invariant that matters is that
     * nothing the overlay draws can land outside the viewport, and that is
     * containment.
     */
    {
        struct UITreeEmitClip const clip = emit.cmds[child_at].clip;
        TEST_ASSERT(
            clip.x >= OV_WORLD_X && clip.y >= OV_WORLD_Y &&
                clip.x + clip.w <= OV_WORLD_X + OV_WORLD_W &&
                clip.y + clip.h <= OV_WORLD_Y + OV_WORLD_H,
            "the marker's clip lies inside the world viewport");
    }

    UITree_EmitBufferFree(&emit);
    UITree_Free(tree);
}

/*
 * A layer the App parked outside the viewport draws nothing.
 *
 * The reason this is worth its own case: the clip is the ONLY thing stopping
 * it. Nothing in the layout says a scripted overlay belongs in the world --
 * the layer is a plain component at a plain offset, and an offset past the
 * viewport's right edge is exactly what a marker on a loc at the edge of the
 * scene produces.
 */
void
test_scripted_entity_overlay_clipped(void)
{
    int const root_uid = (702 << 16) | 0;
    int const world_uid = (702 << 16) | 1;
    int const panel_uid = (702 << 16) | 2;
    int const overlay_uid = (702 << 16) | 3;
    struct UITree* tree = UITree_New(8);
    struct TestHostState hs;
    struct UITreeHost host;
    struct UITreeEmitBuffer emit;
    int32_t overlay;
    int32_t layer;
    int32_t child;
    int child_com;
    int child_at = -1;

    printf("TEST: a scripted overlay past the viewport edge is clipped away\n");
    TEST_ASSERT(tree != NULL, "clipped-overlay tree allocation");
    if( !tree )
        return;

    overlay = overlay_build_tree(tree, root_uid, world_uid, overlay_uid, panel_uid);
    layer = UITree_EntityOverlayCreateLayer(tree, 0, 60, 60);
    if( overlay < 0 || layer < 0 )
    {
        UITree_Free(tree);
        return;
    }
    /* Well past the right edge of the world rect, over the side panel. */
    tree->components[layer].position.x = OV_WORLD_W + 40;
    tree->components[layer].position.y = 40;

    child = UITree_CcCreate(tree, layer, tree->components[layer].component_id, 3, 0);
    if( child < 0 )
    {
        UITree_Free(tree);
        return;
    }
    child_com = tree->components[child].component_id;
    tree->components[child].position.width = 36;
    tree->components[child].position.height = 32;

    UITree_TestHostInit(&host, &hs);
    UITree_LayoutInvalidateBoxes(tree);
    UITree_TestResolve(tree);
    UITree_EmitBufferInit(&emit);
    UITree_EmitWalk(tree, &host, &emit, -1);

    for( int i = 0; i < emit.count; i++ )
        if( emit.cmds[i].component_id == child_com )
            child_at = i;

    if( child_at >= 0 )
    {
        int const cx = emit.cmds[child_at].clip.x;
        int const cw = emit.cmds[child_at].clip.w;
        TEST_ASSERT(
            emit.cmds[child_at].x >= cx + cw,
            "an out-of-viewport marker is scissored off the world rect");
    }

    UITree_EmitBufferFree(&emit);
    UITree_Free(tree);
}
