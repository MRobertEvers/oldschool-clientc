#include "test_harness.h"

#include "render/torirs_arc.h"

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

    /* A camera turn moves the ordinary component after the first emit. The
     * retained-list gate watches dirty_gen before the next EmitWalk, while the
     * incremental layout only recomputes nodes whose cached box was cleared.
     * Both signals must change or the fish sprite remains at its old angle even
     * though host-drawn world primitives are refreshed. */
    {
        uint32_t const dirty_before = tree->dirty_gen;
        int const moved_x = 180;
        int const moved_y = 140;

        TEST_ASSERT(
            UITree_EntityOverlaySetLayerPosition(tree, layer, moved_x, moved_y),
            "camera projection can move the scripted overlay layer");
        TEST_ASSERT(
            tree->dirty_gen != dirty_before,
            "moving a visible overlay invalidates the retained emit list");
        TEST_ASSERT(
            !tree->components[layer].position.layout_resolved,
            "moving an overlay invalidates its cached absolute box");

        emit.count = 0;
        UITree_EmitWalk(tree, &host, &emit, -1);
        child_at = -1;
        for( int i = 0; i < emit.count; i++ )
            if( emit.cmds[i].component_id == child_com )
                child_at = i;

        TEST_ASSERT(child_at >= 0, "the moved overlay child is emitted again");
        if( child_at >= 0 )
            TEST_ASSERT(
                emit.cmds[child_at].x == OV_WORLD_X + moved_x &&
                    emit.cmds[child_at].y == OV_WORLD_Y + moved_y,
                "the sprite follows the updated camera projection");
    }

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

/*
 * The countdown pie -- All Settings > Activities rows 187 / 188 / 242 / 243.
 *
 * Clientscript 5480 builds it out of THREE type-10 children of the overlay
 * layer: a full translucent disc, the swept wedge over it, and the wedge's
 * one-pixel arc outline. Each is `cc_create(.., 10, n)` plus a colour, a
 * transparency, a fill flag and `_1128(start, end)`.
 *
 * This is the case a screenshot cannot answer. Four separate things make the
 * pie not appear -- the setting is off, the server never started a timer, the
 * component type has no arm, the opcode is ignored -- and all four produce the
 * same empty patch of grass. So the model is pinned here: the widget type
 * becomes an arc, the two angles reach it, and the geometry the frame
 * translator will step is the geometry the script asked for.
 */
void
test_scripted_overlay_arc(void)
{
    int const root_uid = (703 << 16) | 0;
    int const world_uid = (703 << 16) | 1;
    int const panel_uid = (703 << 16) | 2;
    int const overlay_uid = (703 << 16) | 3;
    struct UITree* tree = UITree_New(8);
    struct TestHostState hs;
    struct UITreeHost host;
    struct UITreeEmitBuffer emit;
    int32_t overlay;
    int32_t layer;
    int32_t ring;
    int32_t wedge;
    int32_t outline;
    int ring_com;
    int wedge_com;
    int outline_com;
    int ring_at = -1;
    int wedge_at = -1;
    int outline_at = -1;
    /* 5480's own numbers: a quarter of the way through the countdown. */
    int const swept = 65536 / 4;

    printf("TEST: the overlay countdown pie -- type 10, CC_SETARC, and its spans\n");
    TEST_ASSERT(tree != NULL, "arc tree allocation");
    if( !tree )
        return;

    overlay = overlay_build_tree(tree, root_uid, world_uid, overlay_uid, panel_uid);
    layer = UITree_EntityOverlayCreateLayer(tree, 0, 25, 25);
    if( overlay < 0 || layer < 0 )
    {
        UITree_Free(tree);
        return;
    }
    tree->components[layer].position.x = 100;
    tree->components[layer].position.y = 80;

    ring = UITree_CcCreate(tree, layer, tree->components[layer].component_id, 10, 0);
    wedge = UITree_CcCreate(tree, layer, tree->components[layer].component_id, 10, 1);
    outline = UITree_CcCreate(tree, layer, tree->components[layer].component_id, 10, 2);
    TEST_ASSERT(ring >= 0 && wedge >= 0 && outline >= 0, "three type-10 children created");
    if( ring < 0 || wedge < 0 || outline < 0 )
    {
        UITree_Free(tree);
        return;
    }

    /*
     * Widget type 10 used to fall to cc_create's `default:` arm, which makes an
     * objbox -- a component that draws nothing until SETOBJECT fills it. That
     * is the whole bug this case exists for, and it is invisible on screen.
     */
    TEST_ASSERT(
        tree->components[ring].type == UIELEM_RS_ARC &&
            tree->components[wedge].type == UIELEM_RS_ARC &&
            tree->components[outline].type == UIELEM_RS_ARC,
        "cc_create(.., 10, n) makes an arc, not an objbox");

    ring_com = tree->components[ring].component_id;
    wedge_com = tree->components[wedge].component_id;
    outline_com = tree->components[outline].component_id;

    /* cc_setsize(0, 0, 1, 1) -- the whole 25x25 layer, for all three. */
    for( int32_t n = 0; n < 3; n++ )
    {
        int32_t const idx = n == 0 ? ring : (n == 1 ? wedge : outline);
        tree->components[idx].position.width = 25;
        tree->components[idx].position.height = 25;
    }

    /* ...and the rest of what 5480 says, through the paths the host uses. */
    (void)UITree_ApplyColour(tree, ring_com, 0xFFFFFF);
    (void)UITree_ApplyColour(tree, wedge_com, 0xFFFFFF);
    (void)UITree_ApplyColour(tree, outline_com, 0xFFFFFF);
    tree->components[ring].u.rs_arc.filled = 1;
    tree->components[ring].trans = 200;
    tree->components[ring].u.rs_arc.arc_start = 0;
    tree->components[ring].u.rs_arc.arc_end = 65536;
    tree->components[wedge].u.rs_arc.filled = 1;
    tree->components[wedge].trans = 128;
    tree->components[wedge].u.rs_arc.arc_start = 0;
    tree->components[wedge].u.rs_arc.arc_end = swept;
    tree->components[outline].u.rs_arc.filled = 0;
    tree->components[outline].u.rs_arc.line_width = 1;
    tree->components[outline].u.rs_arc.arc_start = 0;
    tree->components[outline].u.rs_arc.arc_end = swept;

    TEST_ASSERT(
        tree->components[ring].u.rs_arc.color == 0xFFFFFF,
        "cc_setcolour reaches an arc (it used to only know rects and text)");

    UITree_TestHostInit(&host, &hs);
    UITree_LayoutInvalidateBoxes(tree);
    UITree_TestResolve(tree);
    UITree_EmitBufferInit(&emit);
    UITree_EmitWalk(tree, &host, &emit, -1);

    for( int i = 0; i < emit.count; i++ )
    {
        if( emit.cmds[i].component_id == ring_com )
            ring_at = i;
        else if( emit.cmds[i].component_id == wedge_com )
            wedge_at = i;
        else if( emit.cmds[i].component_id == outline_com )
            outline_at = i;
    }

    TEST_ASSERT(
        ring_at >= 0 && wedge_at >= 0 && outline_at >= 0,
        "all three pie segments reach the emit list");
    if( ring_at < 0 || wedge_at < 0 || outline_at < 0 )
    {
        UITree_EmitBufferFree(&emit);
        UITree_Free(tree);
        return;
    }

    TEST_ASSERT(
        emit.cmds[ring_at].kind == UITREE_EMIT_ARC &&
            emit.cmds[wedge_at].kind == UITREE_EMIT_ARC &&
            emit.cmds[outline_at].kind == UITREE_EMIT_ARC,
        "they emit as arcs");
    TEST_ASSERT(
        emit.cmds[wedge_at].arc_start == 0 && emit.cmds[wedge_at].arc_end == swept,
        "CC_SETARC's two angles survive to the draw layer");
    TEST_ASSERT(
        emit.cmds[ring_at].trans == 200 && emit.cmds[wedge_at].trans == 128,
        "the two fills keep their separate transparencies");
    TEST_ASSERT(
        emit.cmds[ring_at].w == 25 && emit.cmds[ring_at].h == 25,
        "the pie is the 25x25 the overlay layer gave it");

    /*
     * The geometry, from the same descs the frame translator steps.
     *
     * "The arc drew wrong" and "the arc did not draw" are one picture, so the
     * shape is checked here rather than inferred from a screenshot: a full disc
     * covers its centre row edge to edge, a quarter wedge from straight up
     * covers the upper RIGHT and nothing else, and the outline is a band a
     * pixel or two wide rather than a solid slice.
     */
    {
        struct ToriRS_ArcShape shape;
        struct ToriRS_ArcSpan spans[TORIRS_ARC_ROW_SPANS_MAX];
        int rows;
        int n;

        shape.x = emit.cmds[ring_at].x;
        shape.y = emit.cmds[ring_at].y;
        shape.w = 25;
        shape.h = 25;
        shape.arc_start = 0;
        shape.arc_end = 65536;
        shape.filled = 1;
        shape.line_width = 1;
        rows = ToriRS_ArcRowCount(&shape);
        TEST_ASSERT(rows == 25, "a 25x25 disc spans 25 rows");
        n = ToriRS_ArcRowSpans(&shape, 12, spans);
        TEST_ASSERT(n == 1 && spans[0].w == 25, "its middle row is one run, 25 wide");

        /* The wedge: 0 -> a quarter turn is up, clockwise, to the right. */
        shape.arc_end = swept;
        n = ToriRS_ArcRowSpans(&shape, 12, spans);
        TEST_ASSERT(
            n == 1 && spans[0].x == shape.x + 12 && spans[0].w == 13,
            "a quarter wedge's middle row starts at the centre and runs right");
        n = ToriRS_ArcRowSpans(&shape, 20, spans);
        TEST_ASSERT(n == 0, "and it covers nothing below the centre");

        /* The outline: the same sector, hollowed out to a one-pixel band. */
        shape.filled = 0;
        n = ToriRS_ArcRowSpans(&shape, 12, spans);
        TEST_ASSERT(
            n == 1 && spans[0].w <= 2,
            "unfilled leaves a band along the arc, not a slice");
        TEST_ASSERT(
            spans[0].x + spans[0].w == shape.x + 25,
            "and the band sits at the rim, where the arc is");

        /* A zero sweep is how the wedge both starts and ends. */
        shape.filled = 1;
        shape.arc_end = 0;
        TEST_ASSERT(ToriRS_ArcRowCount(&shape) == 0, "a zero-width sector draws nothing");
    }

    UITree_EmitBufferFree(&emit);
    UITree_Free(tree);
}
