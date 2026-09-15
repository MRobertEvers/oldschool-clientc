#include "test_harness.h"

#include <stdlib.h>

static struct UIBuildComponent const* g_build_comps;
static int g_build_count;

static struct UIBuildComponent const*
get_comp(void* ud, int index)
{
    (void)ud;
    if( index < 0 || index >= g_build_count )
        return NULL;
    return &g_build_comps[index];
}

static int
get_parent_id(void* ud, int index)
{
    (void)ud;
    if( index < 0 || index >= g_build_count )
        return -1;
    return g_build_comps[index].parent_id;
}

static int
resolve_sprite(void* ud, int graphic_id)
{
    (void)ud;
    return graphic_id >= 0 ? graphic_id + 1000 : -1;
}

void
test_layout_build(void)
{
    printf("TEST: layout / build / scroll\n");

    /* Parent + child XY abs bounds */
    {
        struct UITree* tree = UITree_New(8);
        int32_t layer = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 1, 10, 20, 200, 150);
        int32_t rect = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, 2, 5, 5, 50, 30);
        UITree_TestResolve(tree);

        TEST_ASSERT(tree->components[layer].position.abs_x == 10, "layer abs_x");
        TEST_ASSERT(tree->components[layer].position.abs_y == 20, "layer abs_y");
        TEST_ASSERT(tree->components[rect].position.abs_x == 15, "rect abs_x");
        TEST_ASSERT(tree->components[rect].position.abs_y == 25, "rect abs_y");
        TEST_ASSERT(tree->components[rect].position.abs_w == 50, "rect abs_w");
        UITree_Free(tree);
    }

    /* IF3 mode 0 identity via has_position */
    {
        struct UITree* tree = UITree_New(4);
        struct UITreeNodeSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_RECT;
        spec.component_id = 7;
        spec.has_position = 1;
        spec.position.kind = UIPOS_XY;
        spec.position.x = 40;
        spec.position.y = 50;
        spec.position.width = 20;
        spec.position.height = 10;
        spec.position.x_mode = 0;
        spec.position.y_mode = 0;
        spec.position.width_mode = 0;
        spec.position.height_mode = 0;
        spec.u.rs_rect.filled = 1;
        int32_t idx = UITree_Push(tree, -1, &spec);
        UITree_TestResolve(tree);
        TEST_ASSERT(tree->components[idx].position.abs_x == 40, "if3 mode0 x");
        TEST_ASSERT(tree->components[idx].position.abs_y == 50, "if3 mode0 y");
        TEST_ASSERT(tree->components[idx].position.abs_w == 20, "if3 mode0 w");
        UITree_Free(tree);
    }

    /* BuildFromSource parent-first */
    {
        struct UIBuildComponent comps[3];
        memset(comps, 0, sizeof(comps));
        comps[0].id = 100;
        comps[0].type = UIBUILD_LAYER;
        comps[0].parent_id = -1;
        comps[0].base_width = 100;
        comps[0].base_height = 80;
        comps[1].id = 101;
        comps[1].type = UIBUILD_RECT;
        comps[1].parent_id = 100;
        comps[1].base_x = 2;
        comps[1].base_y = 3;
        comps[1].base_width = 10;
        comps[1].base_height = 10;
        comps[1].color = 0xABCDEF;
        comps[1].filled = 1;
        comps[2].id = 102;
        comps[2].type = UIBUILD_GRAPHIC;
        comps[2].parent_id = 100;
        comps[2].base_x = 20;
        comps[2].graphic = 5;

        g_build_comps = comps;
        g_build_count = 3;

        struct UITree* tree = UITree_New(8);
        struct UITreeBuildSource src = {
            .count = 3,
            .get_component = get_comp,
            .get_parent_id = get_parent_id,
            .resolve_sprite = resolve_sprite,
            .resolve_font = NULL,
            .ud = NULL,
        };
        TEST_ASSERT(UITree_BuildFromSource(tree, &src) == 3, "build from source");
        TEST_ASSERT(tree->component_count == 3, "built 3 nodes");
        int32_t gidx = UITree_FindByComponentId(tree, 102);
        TEST_ASSERT(gidx >= 0, "graphic found");
        TEST_ASSERT(tree->components[gidx].u.rs_graphic.scene_id == 1005, "sprite resolve");
        UITree_TestResolve(tree);
        int32_t ridx = UITree_FindByComponentId(tree, 101);
        TEST_ASSERT(tree->components[ridx].position.abs_x == 2, "built rect abs_x");
        UITree_Free(tree);
    }

    /*
     * A sprite the caller already holds: graphic_scene_id wins over graphic,
     * and carries a frame with it.
     *
     * This is how the baked chrome skin reaches a component -- it is one
     * multi-frame scene sprite that never came from a cache archive, so there
     * is no graphic id to resolve and the resolver must not be consulted. The
     * control beside it is the point: an unset graphic_scene_id has to leave
     * the ordinary cache path exactly as it was.
     */
    {
        struct UIBuildComponent comps[2];
        memset(comps, 0, sizeof(comps));
        comps[0].id = 300;
        comps[0].type = UIBUILD_GRAPHIC;
        comps[0].parent_id = -1;
        comps[0].graphic = 5;                 /* would resolve to 1005 */
        comps[0].graphic_scene_id = 0x40000009;
        comps[0].graphic_atlas_index = 7;
        comps[1].id = 301;
        comps[1].type = UIBUILD_GRAPHIC;
        comps[1].parent_id = -1;
        comps[1].graphic = 5;                 /* no scene id: the cache path */

        g_build_comps = comps;
        g_build_count = 2;

        struct UITree* tree = UITree_New(8);
        struct UITreeBuildSource src = {
            .count = 2,
            .get_component = get_comp,
            .get_parent_id = get_parent_id,
            .resolve_sprite = resolve_sprite,
            .resolve_font = NULL,
            .ud = NULL,
        };
        TEST_ASSERT(UITree_BuildFromSource(tree, &src) == 2, "build baked + cache graphics");

        int32_t baked = UITree_FindByComponentId(tree, 300);
        TEST_ASSERT(baked >= 0, "baked graphic found");
        TEST_ASSERT(
            tree->components[baked].u.rs_graphic.scene_id == 0x40000009,
            "baked graphic keeps the caller's scene id");
        TEST_ASSERT(
            tree->components[baked].u.rs_graphic.atlas_index == 7,
            "baked graphic keeps the caller's frame");

        int32_t cached = UITree_FindByComponentId(tree, 301);
        TEST_ASSERT(cached >= 0, "cache graphic found");
        TEST_ASSERT(
            tree->components[cached].u.rs_graphic.scene_id == 1005,
            "cache graphic still resolves through the resolver");
        TEST_ASSERT(
            tree->components[cached].u.rs_graphic.atlas_index == 0,
            "cache graphic still draws frame 0");
        UITree_Free(tree);
    }

    /* BuildFromSource forward parent (child before parent in source order) */
    {
        struct UIBuildComponent comps[3];
        memset(comps, 0, sizeof(comps));
        /* Child first — parent id 200 appears later as comps[2]. */
        comps[0].id = 201;
        comps[0].type = UIBUILD_RECT;
        comps[0].parent_id = 200;
        comps[0].base_x = 5;
        comps[0].base_y = 7;
        comps[0].base_width = 10;
        comps[0].base_height = 10;
        comps[0].color = 0x112233;
        comps[0].filled = 1;
        comps[1].id = 202;
        comps[1].type = UIBUILD_GRAPHIC;
        comps[1].parent_id = 200;
        comps[1].base_x = 1;
        comps[1].base_y = 2;
        comps[1].graphic = 9;
        comps[2].id = 200;
        comps[2].type = UIBUILD_LAYER;
        comps[2].parent_id = -1;
        comps[2].base_x = 40;
        comps[2].base_y = 50;
        comps[2].base_width = 100;
        comps[2].base_height = 80;

        g_build_comps = comps;
        g_build_count = 3;

        struct UITree* tree = UITree_New(8);
        struct UITreeBuildSource src = {
            .count = 3,
            .get_component = get_comp,
            .get_parent_id = get_parent_id,
            .resolve_sprite = resolve_sprite,
            .resolve_font = NULL,
            .ud = NULL,
        };
        TEST_ASSERT(UITree_BuildFromSource(tree, &src) == 3, "forward parent build");
        int32_t layer = UITree_FindByComponentId(tree, 200);
        int32_t rect = UITree_FindByComponentId(tree, 201);
        int32_t gfx = UITree_FindByComponentId(tree, 202);
        TEST_ASSERT(layer >= 0 && rect >= 0 && gfx >= 0, "forward nodes found");
        TEST_ASSERT(tree->components[rect].parent == layer, "rect under forward layer");
        TEST_ASSERT(tree->components[gfx].parent == layer, "gfx under forward layer");
        UITree_TestResolve(tree);
        TEST_ASSERT(tree->components[rect].position.abs_x == 45, "forward rect abs_x");
        TEST_ASSERT(tree->components[rect].position.abs_y == 57, "forward rect abs_y");
        TEST_ASSERT(tree->components[gfx].position.abs_x == 41, "forward gfx abs_x");
        UITree_Free(tree);
    }

    /* Scrollbar hit on tall layer */
    {
        struct UITree* tree = UITree_New(4);
        struct TestHostState hs;
        struct UITreeHost host;
        UITree_TestHostInit(&host, &hs);

        int32_t L = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 60, 0, 0, 100, 80);
        tree->components[L].u.rs_layer.scroll_height = 400;
        UITree_TestResolve(tree);
        TEST_ASSERT(UITree_ScrollLayerNeedsVertical(&tree->components[L]), "needs vertical scroll");

        struct UITreeScrollbarHitInfo hit;
        memset(&hit, 0, sizeof(hit));
        /* Vertical bar is to the right of layer: x in [100, 116) */
        bool found = UITree_FindScrollbarAt(tree, &host, 108, 40, &hit);
        TEST_ASSERT(found, "scrollbar hit found");
        TEST_ASSERT(hit.layer_index == L, "scrollbar layer index");
        TEST_ASSERT(hit.kind != UITREE_SCROLLBAR_NONE, "scrollbar kind set");

        UITree_Free(tree);
    }

    /* A resolve with nothing invalidated since the last one is skipped, so
     * every layout input has to invalidate. The scroll extent is one: children
     * of an RS_LAYER lay out against it, not against its visible box. */
    {
        struct UITree* tree = UITree_New(4);
        int32_t L = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 60, 0, 0, 100, 80);
        /* height_mode 1 = fill the parent box, i.e. read the scroll extent. */
        int32_t child = UITree_TestPushXy(tree, L, UIELEM_RS_RECT, 61, 0, 0, 10, 0);
        tree->components[child].position.height_mode = 1;
        UITree_TestResolve(tree);
        TEST_ASSERT(tree->components[child].position.abs_h == 80, "child fills visible box");

        UITree_ApplyScrollSize(tree, 60, 0, 400);
        UITree_TestResolve(tree);
        TEST_ASSERT(
            tree->components[child].position.abs_h == 400,
            "scroll extent change re-resolves children");

        /* And the skip itself: a resolve with no invalidation leaves the boxes
         * alone rather than recomputing them (a poisoned box stays poisoned). */
        tree->components[child].position.abs_h = -1;
        UITree_TestResolve(tree);
        TEST_ASSERT(tree->components[child].position.abs_h == -1, "clean resolve is skipped");

        UITree_Free(tree);
    }

    /* Within a resolve that does run, a node is recomputed only if its own box
     * was invalidated or its parent's moved. Descendants have to follow a parent
     * that moves, however deep, and untouched branches have to be left alone. */
    {
        struct UITree* tree = UITree_New(4);
        int32_t outer = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 60, 10, 10, 200, 200);
        int32_t mid = UITree_TestPushXy(tree, outer, UIELEM_RS_LAYER, 61, 5, 5, 100, 100);
        int32_t leaf = UITree_TestPushXy(tree, mid, UIELEM_RS_RECT, 62, 2, 2, 10, 10);
        int32_t other = UITree_TestPushXy(tree, -1, UIELEM_RS_RECT, 70, 300, 300, 10, 10);
        UITree_TestResolve(tree);
        TEST_ASSERT(tree->components[leaf].position.abs_x == 17, "leaf abs_x before move");

        /* Poison the untouched branch: it must not be recomputed. */
        tree->components[other].position.abs_x = -1;
        /* Move the grandparent; only its own subtree may re-resolve. */
        UITree_ApplyPosition(tree, 60, 20, 10);
        UITree_TestResolve(tree);
        TEST_ASSERT(tree->components[leaf].position.abs_x == 27, "leaf follows moved grandparent");
        TEST_ASSERT(tree->components[other].position.abs_x == -1, "untouched branch not revisited");

        /* Rewriting a position with the value it already has is not a change, so
         * it must not invalidate — CS2 does this every frame. */
        tree->components[leaf].position.abs_x = -1;
        UITree_ApplyPosition(tree, 60, 20, 10);
        UITree_TestResolve(tree);
        TEST_ASSERT(tree->components[leaf].position.abs_x == -1, "no-op setposition does not resolve");

        UITree_Free(tree);
    }

    /* IF3 centre (xmode=1): undersized children centre and oversized children
     * overhang both sides. The reference preserves a negative canvas-space
     * origin (viewport_tracker is 765 wide in a canvas−42 gameframe, so x=-21)
     * rather than clamping it back onto the canvas. */
    {
        struct UITree* tree = UITree_New(4);
        struct UITreeNodeSpec parent_spec;
        struct UITreeNodeSpec child_spec;
        int32_t parent;
        int32_t child;

        memset(&parent_spec, 0, sizeof(parent_spec));
        parent_spec.type = UIELEM_RS_LAYER;
        parent_spec.component_id = 34;
        parent_spec.has_position = 1;
        parent_spec.position.kind = UIPOS_XY;
        parent_spec.position.width = 723;
        parent_spec.position.height = 503;
        parent_spec.position.x_mode = 0;
        parent_spec.position.y_mode = 0;
        parent_spec.position.width_mode = 0;
        parent_spec.position.height_mode = 0;
        parent = UITree_Push(tree, -1, &parent_spec);

        memset(&child_spec, 0, sizeof(child_spec));
        child_spec.type = UIELEM_RS_LAYER;
        child_spec.component_id = 92;
        child_spec.has_position = 1;
        child_spec.position.kind = UIPOS_XY;
        child_spec.position.width = 100;
        child_spec.position.height = 50;
        child_spec.position.x_mode = 1;
        child_spec.position.y_mode = 1;
        child_spec.position.width_mode = 0;
        child_spec.position.height_mode = 0;
        child = UITree_Push(tree, parent, &child_spec);
        UITree_TestResolve(tree);
        TEST_ASSERT(
            tree->components[child].position.abs_x == (723 - 100) / 2,
            "centre mode centres undersized child");
        TEST_ASSERT(
            tree->components[child].position.abs_y == (503 - 50) / 2,
            "centre mode centres undersized child y");

        tree->components[child].position.width = 765;
        tree->components[child].position.height = 503;
        tree->components[child].position.layout_resolved = 0;
        UITree_LayoutInvalidateBoxes(tree);
        UITree_TestResolve(tree);
        TEST_ASSERT(
            tree->components[child].position.abs_x == (723 - 765) / 2,
            "oversized centre child preserves canvas-left overhang");
        TEST_ASSERT(
            tree->components[child].position.abs_y == (503 - 503) / 2,
            "oversized centre child y when equal height is 0");
        TEST_ASSERT(tree->components[child].position.abs_w == 765, "oversized centre keeps width");

        UITree_Free(tree);
    }

    /* chat_left (231) mount chain into chatbox:chatmodal (479x96 at 20,11).
     * Universe 506x129 centres with overhang; safezone and content re-centre
     * so content lands exactly on the slot and continue stays clear of the
     * noclickthrough chatbox:controls bar at y=119. */
    {
        struct UITree* tree = UITree_New(8);
        struct UITreeNodeSpec spec;
        int32_t slot;
        int32_t universe;
        int32_t safezone;
        int32_t content;
        int32_t cont;
        int32_t head;

        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_LAYER;
        spec.component_id = (162 << 16) | 567;
        spec.has_position = 1;
        spec.position.kind = UIPOS_XY;
        spec.position.x = 20;
        spec.position.y = 11;
        spec.position.width = 479;
        spec.position.height = 96;
        slot = UITree_Push(tree, -1, &spec);

        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_LAYER;
        spec.component_id = (231 << 16) | 0;
        spec.has_position = 1;
        spec.position.kind = UIPOS_XY;
        spec.position.x = 1;
        spec.position.width = 506;
        spec.position.height = 129;
        spec.position.x_mode = 1;
        spec.position.y_mode = 1;
        universe = UITree_Push(tree, slot, &spec);

        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_LAYER;
        spec.component_id = (231 << 16) | 1;
        spec.has_position = 1;
        spec.position.kind = UIPOS_XY;
        spec.position.x = -1;
        spec.position.y = 1;
        spec.position.width = 481;
        spec.position.height = 110;
        spec.position.x_mode = 1;
        spec.position.y_mode = 1;
        safezone = UITree_Push(tree, universe, &spec);

        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_LAYER;
        spec.component_id = (231 << 16) | 3;
        spec.has_position = 1;
        spec.position.kind = UIPOS_XY;
        spec.position.width = 479;
        spec.position.height = 96;
        spec.position.x_mode = 1;
        spec.position.y_mode = 1;
        content = UITree_Push(tree, safezone, &spec);

        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_TEXT;
        spec.component_id = (231 << 16) | 5;
        spec.has_position = 1;
        spec.position.kind = UIPOS_XY;
        spec.position.x = 96;
        spec.position.y = 80;
        spec.position.width = 380;
        spec.position.height = 17;
        cont = UITree_Push(tree, content, &spec);

        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_MODEL;
        spec.component_id = (231 << 16) | 2;
        spec.has_position = 1;
        spec.position.kind = UIPOS_XY;
        spec.position.x = 35;
        spec.position.y = 43;
        spec.position.width = 32;
        spec.position.height = 32;
        head = UITree_Push(tree, safezone, &spec);

        UITree_TestResolve(tree);

        TEST_ASSERT(tree->components[universe].position.abs_x == 8, "chat_left universe abs_x");
        TEST_ASSERT(tree->components[universe].position.abs_y == -5, "chat_left universe abs_y");
        TEST_ASSERT(tree->components[safezone].position.abs_x == 19, "chat_left safezone abs_x");
        TEST_ASSERT(tree->components[safezone].position.abs_y == 5, "chat_left safezone abs_y");
        TEST_ASSERT(tree->components[content].position.abs_x == 20, "chat_left content abs_x");
        TEST_ASSERT(tree->components[content].position.abs_y == 12, "chat_left content abs_y");
        TEST_ASSERT(tree->components[cont].position.abs_x == 116, "chat_left continue abs_x");
        TEST_ASSERT(tree->components[cont].position.abs_y == 92, "chat_left continue abs_y");
        TEST_ASSERT(
            tree->components[cont].position.abs_y + tree->components[cont].position.abs_h <= 119,
            "continue clears chatbox:controls at y=119");
        TEST_ASSERT(tree->components[head].position.abs_x == 54, "chat_left head abs_x");
        TEST_ASSERT(tree->components[head].position.abs_y == 48, "chat_left head abs_y");

        UITree_Free(tree);
    }
    /*
     * safe_area=os:bottom: a row gives up the overlap with the OS band and no
     * more, and gets it all back when the band goes away.
     *
     * The login box is the row this exists for -- 360x200 at 202,171 in the
     * rs245/rs289 profile -- so it is the geometry used here.
     */
    {
        struct UITree* tree = UITree_New(8);
        int32_t box = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 1, 202, 171, 360, 200);
        int32_t row = UITree_TestPushXy(tree, box, UIELEM_RS_RECT, 2, 10, 120, 200, 20);
        int32_t plain = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 3, 202, 171, 360, 200);

        tree->components[box].position.safe_area_source = UITREE_SAFE_AREA_SOURCE_OS;
        tree->components[box].position.safe_area_flags = UITREE_SAFE_AREA_FLAG_BOTTOM;
        tree->components[box].position.safe_area_margin = 8;

        /* No keyboard: the authored place, exactly. */
        UITree_LayoutInvalidate(tree);
        UITree_TestResolve(tree);
        TEST_ASSERT(tree->components[box].position.abs_y == 171, "safe area: unlifted abs_y");

        /* 200 rows covered leaves 303; the box wants its bottom (371) plus an
         * 8px margin above that, so it gives up 76 and not a pixel more. */
        UITree_LayoutSetSafeBottomInset(200);
        UITree_LayoutInvalidate(tree);
        UITree_TestResolve(tree);
        TEST_ASSERT(tree->components[box].position.abs_y == 95, "safe area: lifted abs_y");
        TEST_ASSERT(tree->components[box].position.abs_x == 202, "safe area: x untouched");
        TEST_ASSERT(tree->components[box].position.abs_h == 200, "safe area: height untouched");
        TEST_ASSERT(tree->components[box].position.y == 171, "safe area: authored y untouched");
        TEST_ASSERT(tree->components[row].position.abs_y == 215, "safe area: child moves with it");
        TEST_ASSERT(
            tree->components[plain].position.abs_y == 171, "safe area: undeclared row stays put");

        /* Taller than what the keyboard leaves: stop at the canvas top rather
         * than sliding the box off it. */
        UITree_LayoutSetSafeBottomInset(480);
        UITree_LayoutInvalidate(tree);
        UITree_TestResolve(tree);
        TEST_ASSERT(tree->components[box].position.abs_y == 0, "safe area: clamped at canvas top");

        /* Keyboard away: back where the profile put it, with nothing
         * remembered about how far it had moved. */
        UITree_LayoutSetSafeBottomInset(0);
        UITree_LayoutInvalidate(tree);
        UITree_TestResolve(tree);
        TEST_ASSERT(tree->components[box].position.abs_y == 171, "safe area: restored abs_y");

        UITree_Free(tree);
    }
}

/*
 * Interface 164's sidebar tab strip, to the pixel, in both of the states its
 * own resize script puts it in.
 *
 * Recorded because the strip's bottom row shows ONE EMPTY PLATE at its left
 * end on the modern-resizable toplevel, and that was read as a missing logout
 * tab and filed as a defect. It is neither missing nor a defect: 164 is the
 * toplevel whose logout is the corner door (164|34/35, graphic 542, at
 * 737,2), so its strip carries THIRTEEN buttons, not fourteen -- seven in the
 * top row and six in the bottom -- while both rows' backing plates are the
 * same 231px seven-cell tile.
 *
 * In the single-row state the two plates overlap by exactly one cell and the
 * thirteen buttons run unbroken. The cache's own toplevel-resize script
 * (901/902/903/904) then stacks the rows with ONE op, measured through
 * TORIRS_DUMP_SETPOS:
 *
 *     SETPOS com=0x00a4005f (164|95) 0,36 modes=2,2 script=901
 *
 * and it never touches the bottom row's plate (164|36), its button layer
 * (164|37) or any of the six buttons. So the overlap cell stops being covered
 * and becomes a bare plate. Every number below is the cache's own -- the
 * `raw=` column of tools/dump_interface -- and the expectations are what the
 * client renders at 765x503.
 *
 * The test exists so that "tidying" that plate away -- sliding 164|37 to the
 * left edge, or shrinking 164|36 to six cells -- fails here instead of
 * silently making the client disagree with the interface it is running.
 */
static int32_t
tabstrip_push(
    struct UITree* tree,
    int32_t parent,
    int component_id,
    int x,
    int y,
    int w,
    int h,
    int x_mode,
    int y_mode)
{
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = component_id;
    spec.has_position = 1;
    spec.position.kind = UIPOS_XY;
    spec.position.x = x;
    spec.position.y = y;
    spec.position.width = w;
    spec.position.height = h;
    spec.position.x_mode = (int8_t)x_mode;
    spec.position.y_mode = (int8_t)y_mode;
    spec.position.width_mode = 0;
    spec.position.height_mode = 0;
    return UITree_Push(tree, parent, &spec);
}

void
test_toplevel164_tab_strip(void)
{
    /* 164|66, the block both rows hang under, is the window. */
    int const canvas_w = 765;
    int const canvas_h = 503;
    struct UITree* tree = UITree_New(16);

    printf("TEST: layout / toplevel 164 tab strip\n");

    /* raw=0,0 231x36 modes=x2,y2 -- the BOTTOM row's block. */
    int32_t const row_bottom = tabstrip_push(tree, -1, (164 << 16) | 94, 0, 0, 231, 36, 2, 2);
    /* raw=198,0 231x36 modes=x2,y2 -- the TOP row's block, which the cache
     * states to the LEFT of the bottom one, on the same line. */
    int32_t const row_top = tabstrip_push(tree, -1, (164 << 16) | 95, 198, 0, 231, 36, 2, 2);
    /* raw=0,0 231x36 modes=x2,y2 -- the bottom row's backing plate: SEVEN
     * cells of 33. */
    int32_t const plate = tabstrip_push(tree, row_bottom, (164 << 16) | 36, 0, 0, 231, 36, 2, 2);
    /* raw=0,0 198x36 modes=x2,y1 -- the bottom row's button layer: SIX. */
    int32_t const buttons = tabstrip_push(tree, row_bottom, (164 << 16) | 37, 0, 0, 198, 36, 2, 1);
    /* raw=165/132/99,0 33x36 modes=x2,y1 -- friends, account, clan, in the
     * cache's own left-to-right order, each anchored off the layer's RIGHT. */
    int32_t const friends = tabstrip_push(tree, buttons, (164 << 16) | 46, 165, 0, 33, 36, 2, 1);
    int32_t const account = tabstrip_push(tree, buttons, (164 << 16) | 45, 132, 0, 33, 36, 2, 1);
    int32_t const clan = tabstrip_push(tree, buttons, (164 << 16) | 44, 99, 0, 33, 36, 2, 1);

    /* The state the cache STATES: one row of thirteen, the two plates
     * overlapping by the one cell the top row's last button sits in. */
    UITree_LayoutResolve(tree, 0, 0, canvas_w, canvas_h);
    TEST_ASSERT(tree->components[row_bottom].position.abs_x == 534, "164 strip: bottom row x");
    TEST_ASSERT(tree->components[row_bottom].position.abs_y == 467, "164 strip: bottom row y");
    TEST_ASSERT(tree->components[row_top].position.abs_x == 336, "164 strip: top row x, unstacked");
    TEST_ASSERT(tree->components[row_top].position.abs_y == 467, "164 strip: top row y, unstacked");
    /* The top row ENDS where the bottom row's first button begins, so the one
     * cell of bottom plate to the left of that button (534..567) is the cell
     * the top row's last button stands in. Nothing is bare. */
    TEST_ASSERT(
        tree->components[row_top].position.abs_x + 231 ==
            tree->components[buttons].position.abs_x,
        "164 strip: unstacked, the top row ends at the bottom row's first button");
    TEST_ASSERT(
        tree->components[plate].position.abs_x + 33 == tree->components[buttons].position.abs_x,
        "164 strip: unstacked, the bottom plate's spare cell is under the top row");

    /* The one op script 901 issues, verbatim from TORIRS_DUMP_SETPOS. */
    TEST_ASSERT(
        UITree_ApplyPositionModes(tree, (164 << 16) | 95, 0, 36, 2, 2),
        "164 strip: the resize script's stacking op lands");
    UITree_LayoutInvalidate(tree);
    UITree_LayoutResolve(tree, 0, 0, canvas_w, canvas_h);

    TEST_ASSERT(tree->components[row_top].position.abs_x == 534, "164 strip: top row x, stacked");
    TEST_ASSERT(tree->components[row_top].position.abs_y == 431, "164 strip: top row y, stacked");
    /* The plate is still seven cells and the buttons are still six, because
     * the script moved neither. */
    TEST_ASSERT(tree->components[plate].position.abs_x == 534, "164 strip: plate x");
    TEST_ASSERT(tree->components[plate].position.abs_w == 231, "164 strip: plate is seven cells");
    TEST_ASSERT(tree->components[buttons].position.abs_x == 567, "164 strip: button layer x");
    TEST_ASSERT(
        tree->components[buttons].position.abs_w == 198, "164 strip: button layer is six cells");
    /* Which is the empty plate, stated as the measurement it is: one cell of
     * backing at the row's left end with no button over it. */
    TEST_ASSERT(
        tree->components[buttons].position.abs_x - tree->components[plate].position.abs_x == 33,
        "164 strip: exactly one uncovered cell at the bottom row's left end");

    /* And the six that are there, where 164 puts them. */
    TEST_ASSERT(tree->components[friends].position.abs_x == 567, "164 strip: friends x");
    TEST_ASSERT(tree->components[account].position.abs_x == 600, "164 strip: account x");
    TEST_ASSERT(tree->components[clan].position.abs_x == 633, "164 strip: clan x");
    TEST_ASSERT(tree->components[friends].position.abs_y == 467, "164 strip: friends y");

    UITree_Free(tree);
}
