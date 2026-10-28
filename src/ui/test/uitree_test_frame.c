#include "test_harness.h"

#include "uitree_chatview.h"
#include "uitree_frame.h"
#include "uitree_input.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * The gameframe's roles (the app's binder stamps them, the slot lookups answer
 * from the stamps) and the retained entity/canvas overlays, against a cache
 * frame fixture.
 */

enum
{
    FRAME_GROUP = 161,
    CONTENT_GROUP = 12,

    FRAME_ROOT_ID = (FRAME_GROUP << 16) | 0,
    FRAME_WORLD_ID = (FRAME_GROUP << 16) | 5,
    FRAME_ENTITY_OVERLAY_ID = (FRAME_GROUP << 16) | 6,
    FRAME_CROSS_ID = (FRAME_GROUP << 16) | 7,
    SHELL_ROOT_ID = 1,
};

enum RetainedOverlaySource
{
    RETAINED_OVERLAY_ENTITY = 0,
    RETAINED_OVERLAY_CANVAS,
    RETAINED_OVERLAY_SOURCE_COUNT,
    RETAINED_OVERLAY_PHASE_COUNT = 4,
};

struct RetainedOverlayHost
{
    struct UITreeEntityOverlay
        items[RETAINED_OVERLAY_SOURCE_COUNT][RETAINED_OVERLAY_PHASE_COUNT][3];
    int calls[RETAINED_OVERLAY_SOURCE_COUNT];
    int last_call_seq[RETAINED_OVERLAY_SOURCE_COUNT];
    int call_seq;
    int phase;
    int zero_initial;
    struct UITree* mutate_tree;
    enum UITreeHostRequestKind mutate_kind;
    int mutated;
    uint8_t empty_mask[RETAINED_OVERLAY_PHASE_COUNT];
};

static int
retained_overlay_host_request(void* user, struct UITreeHostRequest* req)
{
    struct RetainedOverlayHost* state = user;
    int source;

    if( state->mutate_tree && !state->mutated && req->kind == state->mutate_kind )
    {
        state->mutated = 1;
        UITree_MarkNodeVisibilityDirty(state->mutate_tree, 0);
    }

    switch( req->kind )
    {
    case UITREE_HOST_GET_CROSS_ACTIVE:
        return 1;
    case UITREE_HOST_GET_CROSS_ATLAS_FRAME:
        return 0;
    case UITREE_HOST_GET_CROSS_POSITION:
        if( req->u.get_cross_position.out_x )
            *req->u.get_cross_position.out_x = 40;
        if( req->u.get_cross_position.out_y )
            *req->u.get_cross_position.out_y = 50;
        return 1;
    case UITREE_HOST_GET_ENTITY_OVERLAYS:
        source = RETAINED_OVERLAY_ENTITY;
        break;
    case UITREE_HOST_GET_CANVAS_OVERLAYS:
        source = RETAINED_OVERLAY_CANVAS;
        break;
    default:
        return 0;
    }

    state->calls[source]++;
    state->last_call_seq[source] = ++state->call_seq;
    if( req->u.get_entity_overlays.out_items )
        *req->u.get_entity_overlays.out_items = state->items[source][state->phase];
    if( req->u.get_entity_overlays.out_clip_x )
        *req->u.get_entity_overlays.out_clip_x = 10 * (source + 1) + state->phase;
    if( req->u.get_entity_overlays.out_clip_y )
        *req->u.get_entity_overlays.out_clip_y = 20 * (source + 1) + state->phase;
    if( req->u.get_entity_overlays.out_clip_w )
        *req->u.get_entity_overlays.out_clip_w = 100 + 10 * source + state->phase;
    if( req->u.get_entity_overlays.out_clip_h )
        *req->u.get_entity_overlays.out_clip_h = 200 + 10 * source + state->phase;
    return (state->zero_initial && state->phase == 0) ||
                   (state->empty_mask[state->phase] & (uint8_t)(1u << source))
               ? 0
               : source + 1;
}

static struct UITreeEmitDesc const*
emit_find(struct UITreeEmitBuffer const* buf, int32_t node)
{
    for( int i = 0; i < buf->count; i++ )
        if( buf->cmds[i].node_index == node )
            return &buf->cmds[i];
    return NULL;
}

static int
emit_find_overlay_source(
    struct UITreeEmitBuffer const* buf,
    int source)
{
    uint8_t wanted = UITREE_EMIT_OVERLAY_NONE;

    if( source == RETAINED_OVERLAY_ENTITY )
        wanted = UITREE_EMIT_OVERLAY_ENTITY;
    else if( source == RETAINED_OVERLAY_CANVAS )
        wanted = UITREE_EMIT_OVERLAY_CANVAS;
    for( int i = 0; i < buf->count; i++ )
        if( buf->cmds[i].entity_overlay_source == wanted )
            return i;
    return -1;
}

static void
test_retained_overlay_refresh_preserves_source(void)
{
    struct UITree* tree = UITree_New(4);
    struct UITreeEmitBuffer buf;
    struct UITreeEmitBuffer fresh;
    struct UITreeHost host;
    struct RetainedOverlayHost state;
    int32_t root;
    int32_t world;
    int32_t entity_overlay;
    int desc[RETAINED_OVERLAY_SOURCE_COUNT];

    TEST_ASSERT(tree != NULL, "UITree_New");
    memset(&state, 0, sizeof(state));
    state.zero_initial = 1;
    UITree_HostInit(&host);
    host.user = &state;
    host.request = retained_overlay_host_request;

    root = UITree_TestPushXy(
        tree, -1, UIELEM_RS_LAYER, SHELL_ROOT_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    world = UITree_TestPushXy(
        tree, root, UIELEM_BUILTIN_WORLD, FRAME_WORLD_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    entity_overlay = UITree_TestPushXy(
        tree, root, UIELEM_BUILTIN_ENTITY_OVERLAY, FRAME_ENTITY_OVERLAY_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    TEST_ASSERT(root >= 0 && world >= 0 && entity_overlay >= 0, "volatile overlay fixture");

    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(tree, &host, &buf, -1);

    for( int source = 0; source < RETAINED_OVERLAY_SOURCE_COUNT; source++ )
        desc[source] = emit_find_overlay_source(&buf, source);
    TEST_ASSERT(
        desc[RETAINED_OVERLAY_ENTITY] < 0 && desc[RETAINED_OVERLAY_CANVAS] < 0,
        "zero-count overlay sources add no renderer commands");
    TEST_ASSERT(
        buf.volatile_refs == RETAINED_OVERLAY_SOURCE_COUNT && !buf.volatile_unrefreshable,
        "zero-count overlay sources remain refreshable out of band");
    TEST_ASSERT(
        buf.volatile_desc_refs == 0,
        "standing overlay records do not request a whole-list volatile scan");
    TEST_ASSERT(
        state.calls[RETAINED_OVERLAY_ENTITY] == 1 &&
            state.calls[RETAINED_OVERLAY_CANVAS] == 1,
        "the initial walk requests every overlay source once");

    state.mutate_tree = tree;
    state.mutate_kind = UITREE_HOST_BEGIN_OVERLAYS;
    TEST_ASSERT(
        !UITree_EmitRefreshVolatile(tree, &host, &buf),
        "a visibility mutation while the overlays begin rejects retention");
    TEST_ASSERT(
        state.calls[RETAINED_OVERLAY_ENTITY] == 1 &&
            state.calls[RETAINED_OVERLAY_CANVAS] == 1,
        "the mutation rejects before any disposable overlay callback");
    state.mutate_tree = NULL;
    state.mutate_kind = 0;

    state.phase = 1;
    TEST_ASSERT(
        UITree_EmitRefreshVolatile(tree, &host, &buf),
        "zero-to-nonzero refresh mutates the retained list without a second walk");

    for( int source = 0; source < RETAINED_OVERLAY_SOURCE_COUNT; source++ )
        desc[source] = emit_find_overlay_source(&buf, source);
    TEST_ASSERT(
        desc[RETAINED_OVERLAY_ENTITY] >= 0 && desc[RETAINED_OVERLAY_CANVAS] >= 0,
        "nonempty refresh inserts both renderer descriptors");
    TEST_ASSERT(
        desc[RETAINED_OVERLAY_ENTITY] < desc[RETAINED_OVERLAY_CANVAS],
        "entity overlays stay below canvas chrome, which stays above interfaces");
    for( int source = 0; source < RETAINED_OVERLAY_SOURCE_COUNT; source++ )
    {
        struct UITreeEmitDesc const* refreshed =
            desc[source] >= 0 ? &buf.cmds[desc[source]] : NULL;
        TEST_ASSERT(
            refreshed && refreshed->entity_overlays == state.items[source][1] &&
                refreshed->entity_overlay_count == source + 1,
            "retained refresh preserves each overlay descriptor's host source");
        if( refreshed )
            TEST_ASSERT(
                refreshed->clip.x == 10 * (source + 1) + 1 &&
                    refreshed->clip.y == 20 * (source + 1) + 1 &&
                    refreshed->clip.w == 100 + 10 * source + 1 &&
                    refreshed->clip.h == 200 + 10 * source + 1,
                "retained refresh also takes the matching source clip");
    }
    TEST_ASSERT(
        state.calls[RETAINED_OVERLAY_ENTITY] == 2 &&
            state.calls[RETAINED_OVERLAY_CANVAS] == 2,
        "retained refresh reissues the entity and canvas requests once each");
    TEST_ASSERT(
        state.last_call_seq[RETAINED_OVERLAY_ENTITY] <
            state.last_call_seq[RETAINED_OVERLAY_CANVAS],
        "retained refresh asks for the entity overlays before the canvas ones");

    UITree_EmitBufferInit(&fresh);
    UITree_EmitWalk(tree, &host, &fresh, -1);
    TEST_ASSERT(
        fresh.count == buf.count &&
            (fresh.count == 0 ||
             memcmp(
                 fresh.cmds,
                 buf.cmds,
                 (size_t)fresh.count * sizeof(*fresh.cmds)) == 0),
        "zero-to-nonzero refresh is byte-identical to a fresh full walk");
    UITree_EmitBufferFree(&fresh);

    state.phase = 0;
    TEST_ASSERT(
        UITree_EmitRefreshVolatile(tree, &host, &buf),
        "nonzero-to-zero refresh removes descriptors without a second walk");
    TEST_ASSERT(
        emit_find_overlay_source(&buf, RETAINED_OVERLAY_ENTITY) < 0 &&
            emit_find_overlay_source(&buf, RETAINED_OVERLAY_CANVAS) < 0,
        "zeroed overlay sources leave no stale renderer descriptors");
    TEST_ASSERT(
        state.calls[RETAINED_OVERLAY_ENTITY] == 4 &&
            state.calls[RETAINED_OVERLAY_CANVAS] == 4,
        "each retained transition dispatches every overlay source exactly once");

    UITree_EmitBufferInit(&fresh);
    UITree_EmitWalk(tree, &host, &fresh, -1);
    TEST_ASSERT(
        fresh.count == buf.count &&
            (fresh.count == 0 ||
             memcmp(
                 fresh.cmds,
                 buf.cmds,
                 (size_t)fresh.count * sizeof(*fresh.cmds)) == 0),
        "nonzero-to-zero refresh is byte-identical to a fresh full walk");
    UITree_EmitBufferFree(&fresh);

    UITree_EmitBufferFree(&buf);
    UITree_Free(tree);
}

static void
test_retained_empty_entity_keeps_final_no_world_order(void)
{
    struct UITree* tree = UITree_New(8);
    struct UITreeEmitBuffer buf;
    struct UITreeEmitBuffer fresh;
    struct UITreeHost host;
    struct RetainedOverlayHost state;
    struct UITreeNodeSpec cross_spec;
    int32_t root;
    int32_t before;
    int32_t cross;
    int32_t entity_overlay;
    int32_t after;
    int entity_desc;
    int canvas_desc;
    int cross_desc;

    TEST_ASSERT(tree != NULL, "UITree_New");
    memset(&state, 0, sizeof(state));
    /* Canvas is already standing while ENTITY is empty. This makes the canvas
     * rotate ahead of pointer feedback before the empty entity's conceptual
     * insertion slot is recorded. */
    state.empty_mask[0] = (uint8_t)(1u << RETAINED_OVERLAY_ENTITY);
    state.empty_mask[1] =
        (uint8_t)((1u << RETAINED_OVERLAY_ENTITY) | (1u << RETAINED_OVERLAY_CANVAS));
    state.empty_mask[2] = (uint8_t)(1u << RETAINED_OVERLAY_CANVAS);
    UITree_HostInit(&host);
    host.user = &state;
    host.request = retained_overlay_host_request;

    root = UITree_TestPushXy(
        tree, -1, UIELEM_RS_LAYER, SHELL_ROOT_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    before = UITree_TestPushXy(tree, root, UIELEM_RS_RECT, 8001, 0, 0, 8, 8);
    memset(&cross_spec, 0, sizeof(cross_spec));
    cross_spec.type = UIELEM_BUILTIN_CROSS;
    cross_spec.component_id = FRAME_CROSS_ID;
    cross_spec.width = 16;
    cross_spec.height = 16;
    cross_spec.u.sprite.scene_id = 77;
    cross = UITree_Push(tree, root, &cross_spec);
    entity_overlay = UITree_TestPushXy(
        tree, root, UIELEM_BUILTIN_ENTITY_OVERLAY, FRAME_ENTITY_OVERLAY_ID,
        0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    after = UITree_TestPushXy(tree, root, UIELEM_RS_RECT, 8002, 0, 0, 8, 8);
    TEST_ASSERT(
        root >= 0 && before >= 0 && cross >= 0 && entity_overlay >= 0 && after >= 0,
        "no-world retained overlay fixture");

    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(tree, &host, &buf, -1);
    entity_desc = emit_find_overlay_source(&buf, RETAINED_OVERLAY_ENTITY);
    canvas_desc = emit_find_overlay_source(&buf, RETAINED_OVERLAY_CANVAS);
    cross_desc = emit_find(&buf, cross) ? (int)(emit_find(&buf, cross) - buf.cmds) : -1;
    TEST_ASSERT(entity_desc < 0, "empty no-world entity source is not published");
    TEST_ASSERT(
        canvas_desc >= 0 && cross_desc >= 0 && canvas_desc < cross_desc,
        "standing canvas chrome remains below pointer feedback");

    /* First remove the standing CANVAS while ENTITY remains absent. The
     * entity's saved no-world slot must move left with that removal. */
    state.phase = 1;
    TEST_ASSERT(
        UITree_EmitRefreshVolatile(tree, &host, &buf),
        "mixed no-world refresh can remove canvas while entity remains empty");
    TEST_ASSERT(
        emit_find_overlay_source(&buf, RETAINED_OVERLAY_ENTITY) < 0 &&
            emit_find_overlay_source(&buf, RETAINED_OVERLAY_CANVAS) < 0,
        "mixed transition leaves both sources absent");

    state.phase = 2;
    TEST_ASSERT(
        UITree_EmitRefreshVolatile(tree, &host, &buf),
        "no-world entity source can become nonempty in the retained list");
    entity_desc = emit_find_overlay_source(&buf, RETAINED_OVERLAY_ENTITY);
    canvas_desc = emit_find_overlay_source(&buf, RETAINED_OVERLAY_CANVAS);
    cross_desc = emit_find(&buf, cross) ? (int)(emit_find(&buf, cross) - buf.cmds) : -1;
    TEST_ASSERT(
        canvas_desc < 0 && cross_desc >= 0 && entity_desc >= 0 && cross_desc < entity_desc,
        "retained insertion rebases the entity slot after an earlier canvas removal");

    UITree_EmitBufferInit(&fresh);
    UITree_EmitWalk(tree, &host, &fresh, -1);
    TEST_ASSERT(
        fresh.count == buf.count &&
            (fresh.count == 0 ||
             memcmp(
                 fresh.cmds,
                 buf.cmds,
                 (size_t)fresh.count * sizeof(*fresh.cmds)) == 0),
        "no-world retained insertion is byte-identical to a fresh full walk");

    UITree_EmitBufferFree(&fresh);
    UITree_EmitBufferFree(&buf);
    UITree_Free(tree);
}

static void
test_transparent_entity_overlay_stays_absent_on_refresh(void)
{
    struct UITree* tree = UITree_New(4);
    struct UITreeEmitBuffer buf;
    struct UITreeHost host;
    struct RetainedOverlayHost state;
    int32_t root;
    int32_t entity_overlay;

    TEST_ASSERT(tree != NULL, "UITree_New");
    memset(&state, 0, sizeof(state));
    UITree_HostInit(&host);
    host.user = &state;
    host.request = retained_overlay_host_request;
    root = UITree_TestPushXy(
        tree, -1, UIELEM_RS_LAYER, SHELL_ROOT_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    entity_overlay = UITree_TestPushXy(
        tree, root, UIELEM_BUILTIN_ENTITY_OVERLAY, FRAME_ENTITY_OVERLAY_ID,
        0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    TEST_ASSERT(root >= 0 && entity_overlay >= 0, "transparent entity overlay fixture");
    tree->components[entity_overlay].trans = 255;

    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(tree, &host, &buf, -1);
    TEST_ASSERT(
        state.calls[RETAINED_OVERLAY_ENTITY] == 0 &&
            !(buf.volatile_overlay_seen &
              (uint8_t)(1u << UITREE_EMIT_OVERLAY_ENTITY)),
        "a fully transparent entity node never issues or registers its source");
    TEST_ASSERT(
        UITree_EmitRefreshVolatile(tree, &host, &buf),
        "other retained overlay sources still refresh");
    TEST_ASSERT(
        state.calls[RETAINED_OVERLAY_ENTITY] == 0 &&
            emit_find_overlay_source(&buf, RETAINED_OVERLAY_ENTITY) < 0,
        "retained refresh cannot resurrect a transparent entity overlay");

    UITree_EmitBufferFree(&buf);
    UITree_Free(tree);
}

/*
 * A synthesised press names a component, so a plugin's own presentation hide
 * (`widget_hidden`, which Porcelain's `hide` writes) must not refuse it: a
 * plugin that covers a native control still has to be able to press it.
 */
static void
test_synthetic_press_sees_through_widget_hidden(void)
{
    struct UITree* tree = UITree_New(4);
    int32_t orb;
    int32_t button;

    printf("TEST: a synthesised press sees through a plugin's widget hide\n");

    TEST_ASSERT(tree != NULL, "UITree_New");
    orb = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 900, 0, 0, 57, 34);
    button = UITree_TestPushXy(tree, orb, UIELEM_RS_GRAPHIC, 904, 3, 5, 50, 26);
    UITree_TestResolve(tree);

    tree->components[orb].widget_hidden = 1;
    TEST_ASSERT(
        UITree_NodeOrAncestorDisplayHidden(tree, button),
        "a click on pixels still refuses a widget-hidden subtree");
    TEST_ASSERT(
        !UITree_NodeOrAncestorDisplayHiddenEx(tree, button, 1),
        "a synthesised press reaches the button under a plugin's own cover");

    tree->components[orb].behavior.hide = 1;
    TEST_ASSERT(
        UITree_NodeOrAncestorDisplayHiddenEx(tree, button, 1),
        "a hide the cache or a script authored still fences it");
    tree->components[orb].behavior.hide = 0;

    /* The node the plugin hid, asked about itself: a cover that replaces a
     * control delegates to THAT control as often as to a child of it. */
    TEST_ASSERT(
        !UITree_NodeOrAncestorDisplayHiddenEx(tree, orb, 1),
        "the hidden node itself answers the press too");

    UITree_Free(tree);
}

/*
 * A cache gameframe names its regions through a BINDER.
 *
 * The tree recognises the world, the minimap and the compass by widget type;
 * the chat container, the side panels and the orb column are plain layers a
 * binder stamps with a slot tag and a member number before every collection.
 * Two things are asserted here: that a stamped layer answers the slot lookup
 * (with its member), and that the widened chrome rule hides a root-group
 * LAYER only when it is a control or a click-blocker -- a plain container in
 * the same group, and the stamped surfaces, stay.
 */
static int g_binder_calls;
static int32_t g_binder_chat;
static int32_t g_binder_side3;
static int32_t g_binder_orbs;
static int32_t g_binder_adviser;
static int32_t g_binder_globe;
static int32_t g_binder_banner;

static void
stamping_binder(struct UITree* tree, void* user)
{
    (void)user;
    g_binder_calls++;
    UITree_FrameStamp(tree, g_binder_chat, UITREE_SLOT_CHAT,
                      tree->components[g_binder_chat].frame_member_plus1);
    UITree_FrameStamp(tree, g_binder_side3, UITREE_SLOT_SIDE_MODAL, 3 + 1);
    UITree_FrameStamp(tree, g_binder_orbs, UITREE_SLOT_ORBS,
                      tree->components[g_binder_orbs].frame_member_plus1);
    /* A button INSIDE the orb block the profile names on its own, as the
     * activity adviser is: orbs member 0. */
    UITree_FrameStamp(tree, g_binder_adviser, UITREE_SLOT_ORBS, 0 + 1);
    /* And one the profile names as furniture the block CARRIES, as the
     * world-map globe is: orbs member 1. */
    UITree_FrameStamp(tree, g_binder_globe, UITREE_SLOT_ORBS, 1 + 1);
    /* And one nested a level deeper than the pack root, as the wiki banner is
     * on a toplevel that groups it: orbs member 2. */
    UITree_FrameStamp(tree, g_binder_banner, UITREE_SLOT_ORBS, 2 + 1);
}

static void
test_binder_stamps_cache_regions_and_layer_chrome(void)
{
    struct UITree* tree = UITree_New(8);
    int32_t shell;
    int32_t root;
    int32_t chat;
    int32_t side3;
    int32_t orbs;
    int32_t pack;
    int32_t adviser;
    int32_t globe;
    int32_t banner_group;
    int32_t banner;
    int32_t container;
    int32_t blocker;
    int32_t control;
    int32_t tip_host;
    int32_t tip_plate;
    int32_t panel_border;
    int32_t status_host;
    int32_t status_glyph;
    int32_t status_tint;

    shell = UITree_TestPushXy(
        tree, -1, UIELEM_RS_LAYER, SHELL_ROOT_ID, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    root = UITree_TestPushXy(
        tree, shell, UIELEM_RS_LAYER, FRAME_ROOT_ID, 0, 0, UITREE_LAYOUT_ROOT_W,
        UITREE_LAYOUT_ROOT_H);
    /* A container layer of the toplevel's own with the chat inside it: kept,
     * both as a container and as an ancestor of a bound surface. */
    container = UITree_TestPushXy(
        tree, root, UIELEM_RS_LAYER, (FRAME_GROUP << 16) | 20, 0, 300, 519, 165);
    chat = UITree_TestPushXy(
        tree, container, UIELEM_RS_LAYER, (FRAME_GROUP << 16) | 21, 0, 0, 519, 165);
    side3 = UITree_TestPushXy(
        tree, root, UIELEM_RS_LAYER, (FRAME_GROUP << 16) | 22, 547, 205, 190, 261);
    orbs = UITree_TestPushXy(
        tree, root, UIELEM_RS_LAYER, (FRAME_GROUP << 16) | 23, 516, 4, 236, 163);
    /* The pack mounted in the block (another group), and the adviser button
     * inside it at the toplevel's own spot for it. */
    pack = UITree_TestPushXy(
        tree, orbs, UIELEM_RS_LAYER, ((FRAME_GROUP + 1) << 16) | 0, 0, 0, 236, 163);
    adviser = UITree_TestPushXy(
        tree, pack, UIELEM_RS_LAYER, ((FRAME_GROUP + 1) << 16) | 48, 202, 50, 34, 34);
    /* The globe, flush to the right edge of the block the pack fills, where
     * the pack's own script puts it on a resizable toplevel. */
    globe = UITree_TestPushXy(
        tree, pack, UIELEM_RS_LAYER, ((FRAME_GROUP + 1) << 16) | 49, 206, 115, 30, 30);
    /* The wiki banner, a level deeper again: the pack groups it with the art
     * beside it, so the box the lane authored for the BANNER is 6,7 inside the
     * group and 8,12 inside that -- neither number on its own places it. */
    banner_group = UITree_TestPushXy(
        tree, pack, UIELEM_RS_LAYER, ((FRAME_GROUP + 1) << 16) | 60, 6, 7, 60, 40);
    banner = UITree_TestPushXy(
        tree, banner_group, UIELEM_RS_LAYER, ((FRAME_GROUP + 1) << 16) | 50, 8, 12, 24, 22);
    /* A click-blocker and a control, both root-group layers: chrome. */
    {
        struct UITreeNodeSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_LAYER;
        spec.component_id = (FRAME_GROUP << 16) | 24;
        spec.x = 600;
        spec.y = 4;
        spec.width = 100;
        spec.height = 100;
        spec.no_click_through = 1;
        blocker = UITree_Push(tree, root, &spec);
        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_LAYER;
        spec.component_id = (FRAME_GROUP << 16) | 25;
        spec.x = 4;
        spec.y = 400;
        spec.width = 40;
        spec.height = 40;
        strncpy(spec.menu_options.ops[0], "Toggle", sizeof(spec.menu_options.ops[0]) - 1);
        control = UITree_Push(tree, root, &spec);
    }
    /* A bare root-group layer the toplevel keeps for its clientscripts to draw
     * into -- rev-239's `mouseover`, which holds nothing until the cursor
     * tooltip is built in it. Neither a control nor an ancestor of anything
     * bound, so the provision does not take it over. */
    tip_host = UITree_TestPushXy(
        tree, root, UIELEM_RS_LAYER, (FRAME_GROUP << 16) | 26, 0, 0, UITREE_LAYOUT_ROOT_W,
        UITREE_LAYOUT_ROOT_H);
    /* The two script-drawn rectangles this test is about. Both are cc_create
     * children, so both carry a component id in the TOPLEVEL's group whatever
     * they are pictures of -- the group alone cannot tell them apart. */
    tip_plate = UITree_CcCreate(tree, tip_host, (FRAME_GROUP << 16) | 26, 3, 0);
    panel_border = UITree_CcCreate(tree, container, (FRAME_GROUP << 16) | 20, 3, 0);
    /*
     * The toplevel's STATUS CORNER: a bare root-group layer of its own with no
     * op on it, and a sprite the toplevel's script draws into it -- 601's wifi
     * bars and its battery, which are two 24x24 layers holding one cc_setgraphic
     * each. Structurally indistinguishable from `tip_host` and its plate: same
     * type, same group, same "the frame took neither container over".
     */
    status_host = UITree_TestPushXy(
        tree, root, UIELEM_RS_LAYER, (FRAME_GROUP << 16) | 27, 711, 0, 24, 24);
    status_glyph = UITree_CcCreate(tree, status_host, (FRAME_GROUP << 16) | 27, 5, 0);
    /* And a RECT drawn into the same container, so the rule below is provably
     * about what the node IS and not about where it was drawn. */
    status_tint = UITree_CcCreate(tree, status_host, (FRAME_GROUP << 16) | 27, 3, 1);
    TEST_ASSERT(
        chat >= 0 && side3 >= 0 && orbs >= 0 && pack >= 0 && adviser >= 0 && globe >= 0 &&
            banner_group >= 0 && banner >= 0 && blocker >= 0 &&
            control >= 0 && tip_host >= 0 && tip_plate >= 0 && panel_border >= 0 &&
            status_host >= 0 && status_glyph >= 0 && status_tint >= 0,
        "cache frame fixture with layers builds");
    TEST_ASSERT(
        (tree->components[tip_plate].component_id >> 16) == FRAME_GROUP &&
            (tree->components[panel_border].component_id >> 16) == FRAME_GROUP,
        "a cc_create child is given the group of the container it was made in");

    TEST_ASSERT(
        UITree_FrameSlotNode(tree, UITREE_FRAME_SLOT_CHAT) < 0,
        "before the binder runs the cache chat layer is not the chat slot");

    g_binder_calls = 0;
    g_binder_chat = chat;
    g_binder_side3 = side3;
    g_binder_orbs = orbs;
    g_binder_adviser = adviser;
    g_binder_globe = globe;
    g_binder_banner = banner;
    UITree_FrameSetBinder(tree, stamping_binder, NULL);

    UITree_FrameBind(tree);

    TEST_ASSERT(g_binder_calls == 1, "a bind runs the binder once");
    TEST_ASSERT(
        UITree_FrameSlotNode(tree, UITREE_FRAME_SLOT_CHAT) == chat,
        "the stamped layer is the chat slot");

    TEST_ASSERT(
        UITree_FrameSlotMemberNode(tree, UITREE_FRAME_SLOT_SIDEBAR, 3) == side3,
        "the stamped side panel answers as member 3");
    TEST_ASSERT(
        UITree_FrameSlotMemberNode(tree, UITREE_FRAME_SLOT_SIDEBAR, 4) < 0,
        "and as no other member");
    TEST_ASSERT(
        UITree_FrameSlotNode(tree, UITREE_FRAME_SLOT_ORBS) == orbs,
        "the stamped orb block is the orbs slot");
    TEST_ASSERT(
        UITree_FrameSlotMemberNode(tree, UITREE_FRAME_SLOT_ORBS, 0) == adviser,
        "and the stamped button inside it is orbs member 0");
    TEST_ASSERT(
        UITree_FrameSlotMemberNode(tree, UITREE_FRAME_SLOT_ORBS, 1) == globe,
        "and the stamped globe is orbs member 1");
    TEST_ASSERT(
        UITree_FrameSlotIndex(&tree->components[side3], UITREE_FRAME_SLOT_SIDEBAR) == 3,
        "the member number is read off the stamp");
    UITree_Free(tree);
}

/*
 * A member lookup that MISSES builds the candidate list; a binder stamp made
 * afterwards, with no topology change, must still be found. The list is what
 * keeps fourteen sidebar misses a frame from walking the whole tree, and a
 * stamp that did not invalidate it would leave the newly named tab unbound.
 */
static void
test_member_lookup_sees_a_stamp_after_a_miss(void)
{
    struct UITree* tree = UITree_New(8);
    struct UITreeNodeSpec spec;
    int32_t root;
    int32_t side5;
    uint32_t generation;

    TEST_ASSERT(tree != NULL, "UITree_New");
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.width = 100;
    spec.height = 100;
    root = UITree_Push(tree, -1, &spec);
    for( int i = 0; i < 40; i++ )
        (void)UITree_Push(tree, root, &spec);
    side5 = UITree_Push(tree, root, &spec);

    TEST_ASSERT(
        UITree_FrameSlotMemberNode(tree, UITREE_FRAME_SLOT_SIDEBAR, 5) < 0,
        "no tab is named yet");
    generation = tree->generation;
    UITree_FrameStamp(tree, side5, UITREE_SLOT_SIDE_MODAL, 5 + 1);
    TEST_ASSERT(tree->generation == generation, "a stamp is not a topology change");
    TEST_ASSERT(
        UITree_FrameSlotMemberNode(tree, UITREE_FRAME_SLOT_SIDEBAR, 5) == side5,
        "the stamp made after the miss is found");
    TEST_ASSERT(
        UITree_FrameSlotMemberNode(tree, UITREE_FRAME_SLOT_SIDEBAR, 4) < 0,
        "and it answers only to its own number");

    UITree_FrameStamp(tree, side5, UITREE_SLOT_NONE, 0);
    TEST_ASSERT(
        UITree_FrameSlotMemberNode(tree, UITREE_FRAME_SLOT_SIDEBAR, 5) < 0,
        "a cleared stamp is gone");
    UITree_Free(tree);
}

/*
 * The scan meter's verdict (@see UITREE_SCAN_METER): steady frames that keep
 * walking the tree are the failure; a burst that does not repeat is not.
 */
static void
test_scan_meter_flags_a_repeated_walk(void)
{
    struct UITree* tree = UITree_New(8);
    struct UITreeNodeSpec spec;
    struct UITreeScanMeterReport report;
    int32_t root;
    int over;

    TEST_ASSERT(tree != NULL, "UITree_New");
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.width = 10;
    spec.height = 10;
    root = UITree_Push(tree, -1, &spec);
    for( int i = 0; i < 500; i++ )
        (void)UITree_Push(tree, root, &spec);

    /* The fixed lookups: fourteen misses a frame cost no walk at all. */
    (void)UITree_ScanMeterEndFrame(tree, &report);
    over = 0;
    for( int frame = 0; frame < 3 * UITREE_SCAN_METER_WINDOW; frame++ )
    {
        for( int tab = 0; tab < 14; tab++ )
            (void)UITree_FrameSlotMemberNode(tree, UITREE_FRAME_SLOT_SIDEBAR, tab);
        over |= UITree_ScanMeterEndFrame(tree, &report);
    }
    TEST_ASSERT(!over, "fourteen tab misses a frame do not walk the tree");
    TEST_ASSERT(report.steady, "and the frames were steady");

    /* One burst -- the frame after a rebuild -- averages out. */
    for( int i = 0; i < UITREE_SCAN_METER_WINDOW / 2; i++ )
        UITREE_SCAN_METER(tree);
    over = 0;
    for( int frame = 0; frame < 2 * UITREE_SCAN_METER_WINDOW; frame++ )
        over |= UITree_ScanMeterEndFrame(tree, &report);
    TEST_ASSERT(!over, "a one-off burst is not the failure");

    /* The failure: a lookup that walks on every steady frame. */
    over = 0;
    for( int frame = 0; frame < UITREE_SCAN_METER_WINDOW && !over; frame++ )
    {
        for( int walk = 0; walk <= UITREE_SCAN_METER_STEADY_WALKS; walk++ )
            UITREE_SCAN_METER(tree);
        over = UITree_ScanMeterEndFrame(tree, &report);
    }
    TEST_ASSERT(over, "one walk over the budget on every steady frame trips the meter");
    TEST_ASSERT(report.top.site && strcmp(report.top.site, __func__) == 0,
        "and it names the site that walked");
    UITree_Free(tree);
}

void
test_frame_replacement(void)
{
    printf("TEST: plugin frame replacement ownership / rebuild stability\n");
    test_binder_stamps_cache_regions_and_layer_chrome();
    test_member_lookup_sees_a_stamp_after_a_miss();
    test_scan_meter_flags_a_repeated_walk();

    test_retained_overlay_refresh_preserves_source();
    test_retained_empty_entity_keeps_final_no_world_order();
    test_transparent_entity_overlay_stays_absent_on_refresh();
    test_synthetic_press_sees_through_widget_hidden();
}
