#include "test_harness.h"

static struct UITreeEmitDesc const*
find_emit_desc(
    struct UITreeEmitBuffer const* buf,
    int component_id)
{
    for( int i = 0; i < buf->count; i++ )
    {
        if( buf->cmds[i].component_id == component_id )
            return &buf->cmds[i];
    }
    return NULL;
}

static int
node_list_contains(
    int32_t const* nodes,
    int count,
    int32_t wanted)
{
    for( int i = 0; i < count; i++ )
    {
        if( nodes[i] == wanted )
            return 1;
    }
    return 0;
}

void
test_mounted_world_resize(void)
{
    enum
    {
        FIXED_W = 765,
        FIXED_H = 503,
        RESIZABLE_W = 1024,
        RESIZABLE_H = 768,
        PANEL_W = 220,
        PANEL_H = 140,
        MODAL_W = 132,
        MODAL_H = 86,
        RIGHT_INSET = 24,
        BOTTOM_INSET = 30,
        HOST_SCROLL_X = 37,
        HOST_SCROLL_Y = 29,
    };
    int const root_uid = (500 << 16) | 0;
    int const world_uid = (500 << 16) | 1;
    int const mount_host_uid = (500 << 16) | 2;
    int const underlay_uid = (500 << 16) | 3;
    int const modal_root_uid = (600 << 16) | 0;
    int const modal_rect_uid = (600 << 16) | 1;
    int const modal_scroll_uid = (600 << 16) | 2;
    int const saved_root_w = UITREE_LAYOUT_ROOT_W;
    int const saved_root_h = UITREE_LAYOUT_ROOT_H;
    struct UITree* tree = UITree_New(8);
    struct TestHostState hs;
    struct UITreeHost host;
    struct UITreeEmitBuffer fixed_emit;
    struct UITreeEmitBuffer resized_emit;
    struct UITreeEmitDesc const* fixed_rect;
    struct UITreeEmitDesc const* resized_rect;
    int fixed_panel_x;
    int fixed_panel_y;
    int resized_panel_x;
    int resized_panel_y;
    struct UITreeResizeHookSnapshot resize_before[4];
    int resized_hook_ids[4];
    int resize_before_count;
    int resized_hook_count;

    printf("TEST: mounted world modal follows resizable host\n");
    TEST_ASSERT(tree != NULL, "mounted-world tree allocation");
    if( !tree )
        return;

    /* A canvas-sized world with a right/bottom-aligned mount host over it. */
    int32_t root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, root_uid, 0, 0, 0, 0);
    int32_t world = UITree_TestPushXy(tree, root, UIELEM_BUILTIN_WORLD, world_uid, 0, 0, 0, 0);
    tree->components[world].position.width_mode = 1;
    tree->components[world].position.height_mode = 1;

    int32_t mount_host = UITree_TestPushXy(
        tree, root, UIELEM_RS_LAYER, mount_host_uid, RIGHT_INSET, BOTTOM_INSET, PANEL_W, PANEL_H);
    tree->components[mount_host].position.x_mode = 2;
    tree->components[mount_host].position.y_mode = 2;
    tree->components[mount_host].u.rs_layer.scroll_width = PANEL_W + 80;
    tree->components[mount_host].u.rs_layer.scroll_height = PANEL_H + 70;
    tree->components[mount_host].scroll_x = HOST_SCROLL_X;
    tree->components[mount_host].scroll_y = HOST_SCROLL_Y;

    /* Build the sub-interface as its own group, then mount it exactly as
     * IF_OPENSUB does. The mounted root is deliberately smaller than its host:
     * Java class415 captures the clipped host rectangle for a type-0 mount, so
     * blank host space outside this root must not turn into a scene click. */
    int32_t modal_root =
        UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, modal_root_uid, 0, 0, MODAL_W, MODAL_H);
    int32_t modal_rect =
        UITree_TestPushXy(tree, modal_root, UIELEM_RS_RECT, modal_rect_uid, 12, 14, 72, 38);
    tree->components[modal_rect].behavior.button_type = 1;
    tree->components[modal_rect].behavior.click_mask |= UITREE_FLAG_DRAG_ON;
    tree->components[modal_rect].behavior.over_color = 0xFFFFFF;
    strncpy(
        tree->components[modal_rect].menu_options.ops[0],
        "Mounted action",
        sizeof(tree->components[modal_rect].menu_options.ops[0]) - 1);
    int32_t modal_scroll =
        UITree_TestPushXy(tree, modal_root, UIELEM_RS_LAYER, modal_scroll_uid, 82, 8, 30, 64);
    tree->components[modal_scroll].u.rs_layer.scroll_height = 160;
    (void)UITree_InterfaceParentSet(tree, mount_host_uid, 600, 0);
    UITree_Reparent(tree, modal_root, mount_host);

    /* Append ordinary host content after the mounted subtree. It scrolls to
     * the later blank-host probe and is menu-relevant but non-interactive. The
     * type-0 mount must still be traversed last and discard this underlay
     * target across the whole host clip, independent of sibling order. */
    int32_t underlay = UITree_TestPushXy(
        tree,
        mount_host,
        UIELEM_RS_RECT,
        underlay_uid,
        PANEL_W - 20 + HOST_SCROLL_X,
        PANEL_H - 18 + HOST_SCROLL_Y,
        24,
        20);
    tree->components[underlay].item_id = 995;
    tree->components[underlay].item_count = 1;

    /* Both roots listen for resize. The canvas-sized top root changes extent;
     * the mounted interface root only follows its right/bottom-aligned host.
     * Rev239 queues the former, not the latter. */
    UITree_HooksMut(&tree->components[root])->on_resize.script_id = 901;
    UITree_SyncHookMembership(tree, root);
    UITree_HooksMut(&tree->components[modal_root])->on_resize.script_id = 1903;
    UITree_SyncHookMembership(tree, modal_root);

    UITree_TestHostInit(&host, &hs);

    UITree_EmitBufferInit(&fixed_emit);
    UITree_EmitBufferInit(&resized_emit);

    UITree_LayoutSetRootSize(FIXED_W, FIXED_H);
    UITree_TestResolve(tree);
    fixed_panel_x = tree->components[modal_root].position.abs_x;
    fixed_panel_y = tree->components[modal_root].position.abs_y;
    TEST_ASSERT(
        fixed_panel_x == FIXED_W - RIGHT_INSET - PANEL_W &&
            fixed_panel_y == FIXED_H - BOTTOM_INSET - PANEL_H,
        "fixed modal resolves at its right/bottom-aligned mount host");
    TEST_ASSERT(
        tree->components[mount_host].position.abs_x == fixed_panel_x &&
            tree->components[mount_host].position.abs_y == fixed_panel_y,
        "fixed mounted root shares host origin");
    UITree_EmitWalk(tree, &host, &fixed_emit, -1);
    fixed_rect = find_emit_desc(&fixed_emit, modal_rect_uid);
    TEST_ASSERT(fixed_rect != NULL, "fixed mounted modal emits");
    int const fixed_rect_x = fixed_rect ? fixed_rect->x : -1;
    int const fixed_rect_y = fixed_rect ? fixed_rect->y : -1;

    resize_before_count = UITree_SnapshotResizeHooks(
        tree, resize_before, (int)(sizeof(resize_before) / sizeof(*resize_before)));
    TEST_ASSERT(resize_before_count == 2, "snapshot captures pre-resize hook eligibility");

    /* Register a third listener after the snapshot. Its world-sized box will
     * change below, but it must wait for a later resize rather than joining the
     * already-snapshotted dispatch. */
    UITree_HooksMut(&tree->components[world])->on_resize.script_id = 902;
    UITree_SyncHookMembership(tree, world);

    /* Resize without rebuilding or remounting. Layout, emitted pixels and input
     * must all use the same new screen-space origin. */
    UITree_LayoutSetRootSize(RESIZABLE_W, RESIZABLE_H);
    UITree_TestResolve(tree);
    TEST_ASSERT(
        UITree_CollectResizedHookIds(
            tree,
            resize_before,
            resize_before_count,
            0,
            resized_hook_ids,
            (int)(sizeof(resized_hook_ids) / sizeof(*resized_hook_ids))) == 0,
        "false resize trigger suppresses dimension-change hooks");
    resized_hook_count = UITree_CollectResizedHookIds(
        tree,
        resize_before,
        resize_before_count,
        1,
        resized_hook_ids,
        (int)(sizeof(resized_hook_ids) / sizeof(*resized_hook_ids)));
    TEST_ASSERT(
        resized_hook_count == 1 && resized_hook_ids[0] == root_uid,
        "resize queues only snapshotted hooks whose dimensions changed");
    TEST_ASSERT(
        !node_list_contains(resized_hook_ids, resized_hook_count, modal_root_uid),
        "position-only mounted root move does not queue onResize");
    TEST_ASSERT(
        !node_list_contains(resized_hook_ids, resized_hook_count, world_uid),
        "listener registered after resize snapshot is deferred");
    resized_panel_x = tree->components[modal_root].position.abs_x;
    resized_panel_y = tree->components[modal_root].position.abs_y;
    TEST_ASSERT(
        resized_panel_x == fixed_panel_x + (RESIZABLE_W - FIXED_W) &&
            resized_panel_y == fixed_panel_y + (RESIZABLE_H - FIXED_H),
        "mounted modal follows right/bottom host after root resize");
    TEST_ASSERT(
        tree->components[mount_host].position.abs_x == resized_panel_x &&
            tree->components[mount_host].position.abs_y == resized_panel_y,
        "resized mounted root shares moved host origin");

    UITree_EmitWalk(tree, &host, &resized_emit, -1);
    resized_rect = find_emit_desc(&resized_emit, modal_rect_uid);
    TEST_ASSERT(resized_rect != NULL, "resized mounted modal emits");
    if( resized_rect )
    {
        TEST_ASSERT(
            resized_rect->x == fixed_rect_x + (RESIZABLE_W - FIXED_W) &&
                resized_rect->y == fixed_rect_y + (RESIZABLE_H - FIXED_H),
            "mounted modal draw descriptor follows resized host");
        TEST_ASSERT(
            resized_rect->x == resized_panel_x + 12 && resized_rect->y == resized_panel_y + 14,
            "mounted modal draw coordinates are relative to moved host");

        int const rect_hit_x = resized_rect->x + resized_rect->w / 2;
        int const rect_hit_y = resized_rect->y + resized_rect->h / 2;
        TEST_ASSERT(
            UITree_HitTestInteractive(tree, &host, rect_hit_x, rect_hit_y) == modal_rect,
            "mounted modal input ignores host-local scroll like rendering");
        TEST_ASSERT(
            UITree_FindDropTarget(tree, rect_hit_x, rect_hit_y, -1) == modal_rect_uid,
            "mounted drop targeting ignores host-local scroll like rendering");

        {
            struct UITreeScrollbarHitInfo sb;
            int const scrollbar_x = resized_panel_x + 82 + 30 + 8;
            int const scrollbar_y = resized_panel_y + 8 + 8;
            TEST_ASSERT(
                UITree_FindScrollbarAt(tree, &host, scrollbar_x, scrollbar_y, &sb) &&
                    sb.layer_index == modal_scroll && sb.kind == UITREE_SCROLLBAR_V_UP,
                "mounted native scrollbar hit ignores host-local scroll like rendering");
            TEST_ASSERT(
                !UITree_FindScrollbarAt(
                    tree,
                    &host,
                    scrollbar_x - HOST_SCROLL_X,
                    scrollbar_y - HOST_SCROLL_Y,
                    &sb) ||
                    sb.layer_index != modal_scroll,
                "mounted native scrollbar rejects host-scroll-shifted stale position");
        }

        int32_t menu_nodes[8];
        int const menu_count =
            UITree_CollectNodesAt(tree, &host, rect_hit_x, rect_hit_y, menu_nodes, 8);
        TEST_ASSERT(
            menu_count > 0 && menu_nodes[0] == modal_rect,
            "mounted modal menu collection uses emitted unscrolled coordinates");
        TEST_ASSERT(
            UITree_FindHoveredComponentIdForRegion(
                tree, &host, -1, rect_hit_x, rect_hit_y, 0, 0, RESIZABLE_W, RESIZABLE_H) ==
                modal_rect_uid,
            "mounted modal hover uses emitted unscrolled coordinates");
    }

    /* Pick blank space inside the host but outside the smaller mounted root.
     * It has no interactive widget, yet class415's modal capture blocks the
     * world across the clipped host rectangle. The corresponding fixed-mode
     * point is now bare world. */
    int const resized_blank_x = resized_panel_x + PANEL_W - 8;
    int const resized_blank_y = resized_panel_y + PANEL_H - 8;
    int const fixed_blank_x = fixed_panel_x + PANEL_W - 8;
    int const fixed_blank_y = fixed_panel_y + PANEL_H - 8;
    TEST_ASSERT(
        UITree_HitTestInteractive(tree, &host, resized_blank_x, resized_blank_y) < 0,
        "blank modal-host space has no widget click target");
    TEST_ASSERT(
        resized_blank_x >= resized_panel_x + MODAL_W &&
            resized_blank_y >= resized_panel_y + MODAL_H,
        "world-block probe lies outside smaller mounted root");

    /* Prove the ordinary target is present at this drawn point, then that
     * changing only the InterfaceParent type to modal removes it. */
    {
        int32_t menu_nodes[8];
        (void)UITree_InterfaceParentSet(tree, mount_host_uid, 600, 1);
        int menu_count =
            UITree_CollectNodesAt(tree, &host, resized_blank_x, resized_blank_y, menu_nodes, 8);
        TEST_ASSERT(
            node_list_contains(menu_nodes, menu_count, underlay),
            "ordinary scrolled underlay menu target reaches blank host point");

        (void)UITree_InterfaceParentSet(tree, mount_host_uid, 600, 0);
        menu_count =
            UITree_CollectNodesAt(tree, &host, resized_blank_x, resized_blank_y, menu_nodes, 8);
        TEST_ASSERT(
            !node_list_contains(menu_nodes, menu_count, underlay),
            "type-0 modal host barrier hides ordinary underlay menu target");
    }
    TEST_ASSERT(
        UITree_PointBlocksWorld(tree, &host, resized_blank_x, resized_blank_y),
        "type-0 mount consumes world input across blank host rectangle");
    TEST_ASSERT(
        !UITree_PointBlocksWorld(tree, &host, fixed_blank_x, fixed_blank_y),
        "old fixed-mode modal position no longer blocks the world");

    UITree_EmitBufferFree(&resized_emit);
    UITree_EmitBufferFree(&fixed_emit);
    UITree_Free(tree);
    UITree_LayoutSetRootSize(saved_root_w, saved_root_h);

    /* Java integer division truncates toward zero. For a centered child one
     * pixel wider and three pixels taller than its parent, (-1)/2 is 0 and
     * (-3)/2 is -1; an arithmetic right shift incorrectly gives -1 and -2. A
     * nonzero parent origin keeps the x assertion clear of the canvas-left
     * overhang clamp. */
    {
        struct UITree* centered = UITree_New(4);
        TEST_ASSERT(centered != NULL, "negative-centered tree allocation");
        if( centered )
        {
            int32_t center_parent =
                UITree_TestPushXy(centered, -1, UIELEM_RS_LAYER, (700 << 16) | 0, 50, 60, 101, 101);
            int32_t center_child = UITree_TestPushXy(
                centered, center_parent, UIELEM_RS_RECT, (700 << 16) | 1, 0, 0, 102, 104);
            centered->components[center_child].position.x_mode = 1;
            centered->components[center_child].position.y_mode = 1;
            UITree_LayoutResolve(centered, 0, 0, saved_root_w, saved_root_h);
            TEST_ASSERT(
                centered->components[center_child].position.abs_x == 50 &&
                    centered->components[center_child].position.abs_y == 59,
                "negative odd centered overhang truncates toward zero like Java /2");
            UITree_Free(centered);
        }
    }
}
