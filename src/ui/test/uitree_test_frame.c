#include "test_harness.h"

#include "uitree_frame.h"

#include <stdint.h>
#include <stdlib.h>

/*
 * A plugin gameframe is an EFFECTIVE view over the cache's own component
 * state, not a second writer of that state.
 *
 * The distinction is load-bearing on a CS2 frame. Its resize and transmit
 * hooks are allowed to restate the native component geometry every tick, and
 * the plugin declaration has to remain the box that layout and emit observe
 * without writing its rectangle back into `component.position`. Otherwise the
 * two owners alternate writes forever; depending on where a frame is sampled,
 * that is the original frame flashing through the plugin one.
 *
 * The compass skin is the same contract for art: emit sees the plugin image,
 * while the component continues to own the cache image that must return when
 * the claim is released.
 */

enum
{
    FRAME_GROUP = 161,
    CONTENT_GROUP = 12,

    FRAME_ROOT_ID = (FRAME_GROUP << 16) | 0,
    FRAME_CHAT_ID = (FRAME_GROUP << 16) | 1,
    FRAME_COMPASS_ID = (FRAME_GROUP << 16) | 2,
    FRAME_CHROME_ID = (FRAME_GROUP << 16) | 3,
    FRAME_MINIMAP_ID = (FRAME_GROUP << 16) | 4,
    FRAME_WORLD_ID = (FRAME_GROUP << 16) | 5,
    FRAME_ENTITY_OVERLAY_ID = (FRAME_GROUP << 16) | 6,
    FRAME_CROSS_ID = (FRAME_GROUP << 16) | 7,
    FRAME_CONTENT_ID = (CONTENT_GROUP << 16) | 0,
    SHELL_ROOT_ID = 1,

    NATIVE_CHAT_X = 17,
    NATIVE_CHAT_Y = 357,
    NATIVE_CHAT_W = 479,
    NATIVE_CHAT_H = 96,
    NATIVE_COMPASS_X = 575,
    NATIVE_COMPASS_Y = 9,
    NATIVE_COMPASS_W = 35,
    NATIVE_COMPASS_H = 35,

    PLUGIN_CHAT_X = 31,
    PLUGIN_CHAT_Y = 341,
    PLUGIN_CHAT_W = 503,
    PLUGIN_CHAT_H = 119,
    PLUGIN_COMPASS_X = 686,
    PLUGIN_COMPASS_Y = 13,
    PLUGIN_COMPASS_W = 42,
    PLUGIN_COMPASS_H = 42,

    NATIVE_COMPASS_ART = 41,
    NATIVE_COMPASS_MASK = 42,
    NATIVE_MINIMAP_MASK = 43,
    PLUGIN_COMPASS_ART = 141,
    PLUGIN_COMPASS_MASK = 142,
};

enum RetainedOverlaySource
{
    RETAINED_OVERLAY_ENTITY = 0,
    RETAINED_OVERLAY_CANVAS,
    RETAINED_OVERLAY_FRAME,
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
    int role_anchor_seen;
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
    case UITREE_HOST_GET_ROLE_OVERLAY_GROUPS:
        if( req->u.get_role_overlay_groups.out_groups )
            *req->u.get_role_overlay_groups.out_groups = NULL;
        if( req->u.get_role_overlay_groups.out_anchor_seen )
            *req->u.get_role_overlay_groups.out_anchor_seen = state->role_anchor_seen;
        return 0;
    case UITREE_HOST_GET_ENTITY_OVERLAYS:
        source = RETAINED_OVERLAY_ENTITY;
        break;
    case UITREE_HOST_GET_CANVAS_OVERLAYS:
        source = RETAINED_OVERLAY_CANVAS;
        break;
    case UITREE_HOST_GET_FRAME_OVERLAYS:
        source = RETAINED_OVERLAY_FRAME;
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

struct FrameNodes
{
    int32_t root;
    int32_t chat;
    int32_t compass;
    int32_t chrome;
    int32_t content;
    int variant;
};

static int32_t
push_chat_slot(
    struct UITree* tree,
    int32_t parent,
    int x,
    int y,
    int w,
    int h)
{
    struct UITreeNodeSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = FRAME_CHAT_ID;
    spec.x = x;
    spec.y = y;
    spec.width = w;
    spec.height = h;
    spec.slot_tag = UITREE_SLOT_CHAT;
    return UITree_Push(tree, parent, &spec);
}

static int32_t
push_compass(
    struct UITree* tree,
    int32_t parent,
    int x,
    int y,
    int w,
    int h,
    int art,
    int mask)
{
    struct UITreeNodeSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_BUILTIN_COMPASS;
    spec.component_id = FRAME_COMPASS_ID;
    spec.x = x;
    spec.y = y;
    spec.width = w;
    spec.height = h;
    spec.u.sprite.scene_id = art;
    spec.u.sprite.atlas_index = 3;
    spec.u.sprite.mask_scene_id = mask;
    spec.u.sprite.mask_atlas_index = 4;
    return UITree_Push(tree, parent, &spec);
}

/* One cache-authored frame generation. `variant` makes a re-bake's native
 * state visibly different from the generation it replaced. */
static struct FrameNodes
push_cache_frame(struct UITree* tree, int32_t shell, int variant)
{
    struct FrameNodes out;
    int d = variant * 7;

    memset(&out, 0, sizeof(out));
    out.variant = variant;
    out.root = UITree_TestPushXy(
        tree, shell, UIELEM_RS_LAYER, FRAME_ROOT_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    out.chat = push_chat_slot(
        tree,
        out.root,
        NATIVE_CHAT_X + d,
        NATIVE_CHAT_Y - d,
        NATIVE_CHAT_W - d,
        NATIVE_CHAT_H + d);
    out.compass = push_compass(
        tree,
        out.root,
        NATIVE_COMPASS_X - d,
        NATIVE_COMPASS_Y + d,
        NATIVE_COMPASS_W + d,
        NATIVE_COMPASS_H + d,
        NATIVE_COMPASS_ART + variant,
        NATIVE_COMPASS_MASK + variant);
    /* Root-group RECT is the cache gameframe's own decoration and must be
     * suppressed. The mounted group beneath the chat slot is content and must
     * survive the same declaration. */
    out.chrome = UITree_TestPushXy(
        tree, out.root, UIELEM_RS_RECT, FRAME_CHROME_ID, 0, 0, 24, 24);
    out.content = UITree_TestPushXy(
        tree, out.chat, UIELEM_RS_RECT, FRAME_CONTENT_ID, 3, 4, 11, 12);

    TEST_ASSERT(
        out.root >= 0 && out.chat >= 0 && out.compass >= 0 && out.chrome >= 0 &&
            out.content >= 0,
        "cache frame fixture builds");
    return out;
}

static void
declare_plugin_frame(struct UITree* tree)
{
    struct UITreeFrameSlotRect slots[UITREE_FRAME_SLOT_COUNT];

    memset(slots, 0, sizeof(slots));
    slots[UITREE_FRAME_SLOT_CHAT].all.placed = 1;
    slots[UITREE_FRAME_SLOT_CHAT].all.x = PLUGIN_CHAT_X;
    slots[UITREE_FRAME_SLOT_CHAT].all.y = PLUGIN_CHAT_Y;
    slots[UITREE_FRAME_SLOT_CHAT].all.w = PLUGIN_CHAT_W;
    slots[UITREE_FRAME_SLOT_CHAT].all.h = PLUGIN_CHAT_H;
    slots[UITREE_FRAME_SLOT_COMPASS].all.placed = 1;
    slots[UITREE_FRAME_SLOT_COMPASS].all.x = PLUGIN_COMPASS_X;
    slots[UITREE_FRAME_SLOT_COMPASS].all.y = PLUGIN_COMPASS_Y;
    slots[UITREE_FRAME_SLOT_COMPASS].all.w = PLUGIN_COMPASS_W;
    slots[UITREE_FRAME_SLOT_COMPASS].all.h = PLUGIN_COMPASS_H;
    slots[UITREE_FRAME_SLOT_COMPASS].skin.placed = 1;
    slots[UITREE_FRAME_SLOT_COMPASS].skin.art_scene_id = PLUGIN_COMPASS_ART;
    slots[UITREE_FRAME_SLOT_COMPASS].skin.mask_scene_id = PLUGIN_COMPASS_MASK;

    UITree_FrameApply(tree, slots, FRAME_GROUP);
}

static int
raw_box_is(
    struct UITree const* tree,
    int32_t node,
    int x,
    int y,
    int w,
    int h)
{
    struct UITreeElemPosition const* p = &tree->components[node].position;
    return p->x == x && p->y == y && p->width == w && p->height == h;
}

static int
raw_modes_are(
    struct UITree const* tree,
    int32_t node,
    int x_mode,
    int y_mode,
    int width_mode,
    int height_mode)
{
    struct UITreeElemPosition const* p = &tree->components[node].position;
    return p->x_mode == x_mode && p->y_mode == y_mode &&
           p->width_mode == width_mode && p->height_mode == height_mode;
}

static int
effective_box_is(
    struct UITree const* tree,
    int32_t node,
    int x,
    int y,
    int w,
    int h)
{
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;

    UITree_LayoutGetBounds(&tree->components[node].position, &bx, &by, &bw, &bh);
    return bx == x && by == y && bw == w && bh == h;
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
emit_find_scene(struct UITreeEmitBuffer const* buf, int scene_id)
{
    for( int i = 0; i < buf->count; i++ )
        if( buf->cmds[i].kind == UITREE_EMIT_SPRITE &&
            buf->cmds[i].scene_id == scene_id )
            return i;
    return -1;
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
    else if( source == RETAINED_OVERLAY_FRAME )
        wanted = UITREE_EMIT_OVERLAY_FRAME;
    for( int i = 0; i < buf->count; i++ )
        if( buf->cmds[i].entity_overlay_source == wanted )
            return i;
    return -1;
}

static void
emit_frame(struct UITree* tree, struct UITreeEmitBuffer* buf)
{
    buf->count = 0;
    UITree_EmitWalk(tree, NULL, buf, -1);
}

static void
assert_native_art(struct UITree const* tree, struct FrameNodes const* frame)
{
    struct UITreeComponent const* compass = &tree->components[frame->compass];
    TEST_ASSERT(
        compass->u.sprite.scene_id == NATIVE_COMPASS_ART + frame->variant &&
            compass->u.sprite.atlas_index == 3,
        "the component keeps its cache-authored compass art");
    TEST_ASSERT(
        compass->u.sprite.mask_scene_id == NATIVE_COMPASS_MASK + frame->variant &&
            compass->u.sprite.mask_atlas_index == 4,
        "the component keeps its cache-authored compass mask");
}

static void
assert_plugin_emit(
    struct UITree* tree,
    struct UITreeEmitBuffer* buf,
    struct FrameNodes const* frame)
{
    struct UITreeEmitDesc const* compass;

    emit_frame(tree, buf);
    compass = emit_find(buf, frame->compass);
    TEST_ASSERT(compass != NULL, "the effective compass emits");
    if( compass )
    {
        TEST_ASSERT(
            compass->kind == UITREE_EMIT_COMPASS &&
                compass->x == PLUGIN_COMPASS_X && compass->y == PLUGIN_COMPASS_Y &&
                compass->w == PLUGIN_COMPASS_W && compass->h == PLUGIN_COMPASS_H,
            "the compass emits at the plugin rectangle");
        TEST_ASSERT(
            compass->scene_id == PLUGIN_COMPASS_ART && compass->atlas_index == 0 &&
                compass->mask_scene_id == PLUGIN_COMPASS_MASK &&
                compass->mask_atlas_index == 0,
            "emit sees the plugin skin rather than changing native art");
    }
    TEST_ASSERT(emit_find(buf, frame->chrome) == NULL, "the original CS2 chrome does not emit");
    TEST_ASSERT(emit_find(buf, frame->content) != NULL, "mounted content still emits");
}

static void
test_frame_keeps_native_state_beneath_effective_layout(void)
{
    struct UITree* tree = UITree_New(8);
    struct FrameNodes frame;
    struct UITreeEmitBuffer buf;
    int32_t shell;
    uint32_t quiet_dirty;

    TEST_ASSERT(tree != NULL, "UITree_New");
    UITree_EmitBufferInit(&buf);
    shell = UITree_TestPushXy(
        tree, -1, UIELEM_RS_LAYER, SHELL_ROOT_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    TEST_ASSERT(shell >= 0, "persistent display shell");
    frame = push_cache_frame(tree, shell, 0);
    declare_plugin_frame(tree);
    UITree_FrameReassert(tree);
    UITree_TestResolve(tree);

    TEST_ASSERT(
        raw_box_is(
            tree, frame.chat,
            NATIVE_CHAT_X, NATIVE_CHAT_Y, NATIVE_CHAT_W, NATIVE_CHAT_H),
        "applying a frame does not replace the chat component's native rectangle");
    TEST_ASSERT(
        raw_box_is(
            tree, frame.compass,
            NATIVE_COMPASS_X, NATIVE_COMPASS_Y, NATIVE_COMPASS_W, NATIVE_COMPASS_H),
        "applying a frame does not replace the compass component's native rectangle");
    TEST_ASSERT(
        effective_box_is(
            tree, frame.chat,
            PLUGIN_CHAT_X, PLUGIN_CHAT_Y, PLUGIN_CHAT_W, PLUGIN_CHAT_H),
        "layout resolves the chat through the plugin rectangle");
    TEST_ASSERT(
        effective_box_is(
            tree, frame.compass,
            PLUGIN_COMPASS_X, PLUGIN_COMPASS_Y, PLUGIN_COMPASS_W, PLUGIN_COMPASS_H),
        "layout resolves the compass through the plugin rectangle");
    assert_native_art(tree, &frame);
    assert_plugin_emit(tree, &buf, &frame);

    quiet_dirty = tree->dirty_gen;
    declare_plugin_frame(tree);
    TEST_ASSERT(
        tree->dirty_gen == quiet_dirty && tree->components[frame.chrome].frame_hidden,
        "an identical declaration is an atomic no-op, never a release/reapply flash");

    /* The cache's resize hook restates a new native box while the plugin owns
     * the frame. This must update native state without becoming visible until
     * release, and reassert must never write the plugin box back over it. */
    TEST_ASSERT(
        UITree_ApplyPositionModes(tree, FRAME_CHAT_ID, 43, 302, 0, 0),
        "CS2 restates native chat position");
    TEST_ASSERT(
        UITree_ApplySizeModes(tree, FRAME_CHAT_ID, 451, 107, 0, 0),
        "CS2 restates native chat size");
    TEST_ASSERT(
        UITree_ApplyPositionModes(tree, FRAME_COMPASS_ID, 548, 18, 0, 0),
        "CS2 restates native compass position");
    TEST_ASSERT(
        UITree_ApplySizeModes(tree, FRAME_COMPASS_ID, 37, 38, 0, 0),
        "CS2 restates native compass size");

    UITree_FrameReassert(tree);
    UITree_TestResolve(tree);
    TEST_ASSERT(
        raw_box_is(tree, frame.chat, 43, 302, 451, 107) &&
            raw_modes_are(tree, frame.chat, 0, 0, 0, 0),
        "reassert leaves the latest CS2 chat state native");
    TEST_ASSERT(
        raw_box_is(tree, frame.compass, 548, 18, 37, 38) &&
            raw_modes_are(tree, frame.compass, 0, 0, 0, 0),
        "reassert leaves the latest CS2 compass state native");
    TEST_ASSERT(
        effective_box_is(
            tree, frame.chat,
            PLUGIN_CHAT_X, PLUGIN_CHAT_Y, PLUGIN_CHAT_W, PLUGIN_CHAT_H) &&
            effective_box_is(
                tree, frame.compass,
                PLUGIN_COMPASS_X, PLUGIN_COMPASS_Y,
                PLUGIN_COMPASS_W, PLUGIN_COMPASS_H),
        "CS2 writes do not displace either effective plugin rectangle");
    assert_native_art(tree, &frame);
    assert_plugin_emit(tree, &buf, &frame);

    /* Once the same native values have landed, neither CS2 nor reassert has a
     * second write to make. Pin the tree generation used by retained emit so a
     * fix cannot regress into an invisible but permanent write contest. */
    quiet_dirty = tree->dirty_gen;
    for( int i = 0; i < 8; i++ )
    {
        UITree_ApplyPositionModes(tree, FRAME_CHAT_ID, 43, 302, 0, 0);
        UITree_ApplySizeModes(tree, FRAME_CHAT_ID, 451, 107, 0, 0);
        UITree_ApplyPositionModes(tree, FRAME_COMPASS_ID, 548, 18, 0, 0);
        UITree_ApplySizeModes(tree, FRAME_COMPASS_ID, 37, 38, 0, 0);
        UITree_FrameReassert(tree);
        UITree_TestResolve(tree);
    }
    TEST_ASSERT(tree->dirty_gen == quiet_dirty, "steady CS2/reassert frames perform no writes");
    TEST_ASSERT(
        raw_box_is(tree, frame.chat, 43, 302, 451, 107) &&
            raw_box_is(tree, frame.compass, 548, 18, 37, 38),
        "steady reassert never copies plugin geometry into native fields");

    UITree_FrameRelease(tree);
    UITree_TestResolve(tree);
    TEST_ASSERT(
        effective_box_is(tree, frame.chat, 43, 302, 451, 107) &&
            effective_box_is(tree, frame.compass, 548, 18, 37, 38),
        "release reveals the latest CS2-authored geometry");
    TEST_ASSERT(!tree->components[frame.chrome].frame_hidden, "release reveals native chrome");
    assert_native_art(tree, &frame);

    emit_frame(tree, &buf);
    {
        struct UITreeEmitDesc const* compass = emit_find(&buf, frame.compass);
        TEST_ASSERT(compass != NULL, "native compass emits after release");
        if( compass )
            TEST_ASSERT(
                compass->scene_id == NATIVE_COMPASS_ART && compass->atlas_index == 3 &&
                    compass->mask_scene_id == NATIVE_COMPASS_MASK &&
                    compass->mask_atlas_index == 4,
                "release emits the untouched native compass skin");
    }
    TEST_ASSERT(emit_find(&buf, frame.chrome) != NULL, "release emits native CS2 chrome");

    UITree_EmitBufferFree(&buf);
    UITree_Free(tree);
}

static void
test_frame_reconciles_rebuilt_nodes_before_emit(void)
{
    struct UITree* tree = UITree_New(8);
    struct FrameNodes frame;
    struct UITreeEmitBuffer buf;
    int32_t decoy[3];
    int32_t shell;

    TEST_ASSERT(tree != NULL, "UITree_New");
    UITree_EmitBufferInit(&buf);
    shell = UITree_TestPushXy(
        tree, -1, UIELEM_RS_LAYER, SHELL_ROOT_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    TEST_ASSERT(shell >= 0, "persistent display shell");
    frame = push_cache_frame(tree, shell, 0);
    declare_plugin_frame(tree);
    assert_plugin_emit(tree, &buf, &frame);

    for( int generation = 1; generation <= 3; generation++ )
    {
        int32_t const old_chat = frame.chat;
        int32_t const old_compass = frame.compass;
        int const decoy_id = ((80 + generation) << 16) | generation;
        int const decoy_x = generation * 9;
        int const decoy_y = generation * 11;

        /* A root remount reclaims the old semantic nodes after the declaration
         * was applied. Push an unrelated node first so the free-list gives it
         * an old frame index; a stale reassert must not place or restore it. */
        UITree_ReclaimInterfaceGroup(tree, FRAME_GROUP);
        decoy[generation - 1] = UITree_TestPushXy(
            tree, shell, UIELEM_RS_RECT, decoy_id, decoy_x, decoy_y, 13, 14);
        frame = push_cache_frame(tree, shell, generation);
        TEST_ASSERT(
            frame.chat != old_chat && frame.compass != old_compass,
            "the rebuilt roles moved onto recycled node indices");
        TEST_ASSERT(
            !tree->components[frame.chrome].frame_hidden,
            "the rebuilt native chrome starts unsuppressed");

        UITree_FrameReassert(tree);
        UITree_TestResolve(tree);

        TEST_ASSERT(
            raw_box_is(
                tree,
                frame.chat,
                NATIVE_CHAT_X + generation * 7,
                NATIVE_CHAT_Y - generation * 7,
                NATIVE_CHAT_W - generation * 7,
                NATIVE_CHAT_H + generation * 7),
            "rebind preserves the rebuilt chat's native geometry");
        TEST_ASSERT(
            raw_box_is(
                tree,
                frame.compass,
                NATIVE_COMPASS_X - generation * 7,
                NATIVE_COMPASS_Y + generation * 7,
                NATIVE_COMPASS_W + generation * 7,
                NATIVE_COMPASS_H + generation * 7),
            "rebind preserves the rebuilt compass's native geometry");
        TEST_ASSERT(
            effective_box_is(
                tree, frame.chat,
                PLUGIN_CHAT_X, PLUGIN_CHAT_Y, PLUGIN_CHAT_W, PLUGIN_CHAT_H) &&
                effective_box_is(
                    tree, frame.compass,
                    PLUGIN_COMPASS_X, PLUGIN_COMPASS_Y,
                    PLUGIN_COMPASS_W, PLUGIN_COMPASS_H),
            "reassert binds the standing declaration to the rebuilt roles");
        TEST_ASSERT(
            tree->components[frame.chrome].frame_hidden,
            "reassert suppresses rebuilt original chrome before emit");
        TEST_ASSERT(
            !tree->components[frame.content].frame_hidden,
            "reassert does not suppress rebuilt mounted content");
        TEST_ASSERT(
            UITree_FrameHiddenCount(tree) == 1 &&
                UITree_FrameSlotCount(tree, UITREE_FRAME_SLOT_CHAT) == 1 &&
                UITree_FrameSlotCount(tree, UITREE_FRAME_SLOT_COMPASS) == 1,
            "reconciliation replaces rather than accumulates its node tables");
        assert_native_art(tree, &frame);
        assert_plugin_emit(tree, &buf, &frame);

        for( int i = 0; i < generation; i++ )
        {
            TEST_ASSERT(
                raw_box_is(tree, decoy[i], (i + 1) * 9, (i + 1) * 11, 13, 14) &&
                    !tree->components[decoy[i]].frame_hidden,
                "a recycled unrelated node is untouched by stale frame state");
        }
    }

    UITree_FrameRelease(tree);
    UITree_TestResolve(tree);
    TEST_ASSERT(
        effective_box_is(
            tree,
            frame.chat,
            NATIVE_CHAT_X + frame.variant * 7,
            NATIVE_CHAT_Y - frame.variant * 7,
            NATIVE_CHAT_W - frame.variant * 7,
            NATIVE_CHAT_H + frame.variant * 7),
        "release restores the newest frame generation's native chat box");
    TEST_ASSERT(
        effective_box_is(
            tree,
            frame.compass,
            NATIVE_COMPASS_X - frame.variant * 7,
            NATIVE_COMPASS_Y + frame.variant * 7,
            NATIVE_COMPASS_W + frame.variant * 7,
            NATIVE_COMPASS_H + frame.variant * 7),
        "release restores the newest frame generation's native compass box");
    TEST_ASSERT(!tree->components[frame.chrome].frame_hidden, "release reveals rebuilt chrome");
    assert_native_art(tree, &frame);
    for( int i = 0; i < 3; i++ )
        TEST_ASSERT(
            raw_box_is(tree, decoy[i], (i + 1) * 9, (i + 1) * 11, 13, 14),
            "release never restores stale frame state onto a recycled node");

    UITree_EmitBufferFree(&buf);
    UITree_Free(tree);
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
        desc[RETAINED_OVERLAY_ENTITY] < 0 && desc[RETAINED_OVERLAY_CANVAS] < 0 &&
            desc[RETAINED_OVERLAY_FRAME] < 0,
        "zero-count overlay sources add no renderer commands");
    TEST_ASSERT(
        buf.volatile_refs == RETAINED_OVERLAY_SOURCE_COUNT && !buf.volatile_unrefreshable,
        "zero-count overlay sources remain refreshable out of band");
    TEST_ASSERT(
        buf.volatile_desc_refs == 0,
        "standing overlay records do not request a whole-list volatile scan");
    TEST_ASSERT(
        state.calls[RETAINED_OVERLAY_ENTITY] == 1 &&
            state.calls[RETAINED_OVERLAY_CANVAS] == 1 &&
            state.calls[RETAINED_OVERLAY_FRAME] == 1,
        "the initial walk requests every overlay source once");

    state.mutate_tree = tree;
    state.mutate_kind = UITREE_HOST_GET_ROLE_OVERLAY_GROUPS;
    TEST_ASSERT(
        !UITree_EmitRefreshVolatile(tree, &host, &buf),
        "a replacement visibility mutation during Canvas preflight rejects retention");
    TEST_ASSERT(
        state.calls[RETAINED_OVERLAY_ENTITY] == 1 &&
            state.calls[RETAINED_OVERLAY_CANVAS] == 1 &&
            state.calls[RETAINED_OVERLAY_FRAME] == 1,
        "replacement mutation rejects before disposable overlay callbacks");
    state.mutate_tree = NULL;
    state.mutate_kind = 0;

    state.role_anchor_seen = 1;
    TEST_ASSERT(
        !UITree_EmitRefreshVolatile(tree, &host, &buf),
        "a role anchor appearing on a retained frame requires a local full walk");
    TEST_ASSERT(
        state.calls[RETAINED_OVERLAY_ENTITY] == 1 &&
            state.calls[RETAINED_OVERLAY_CANVAS] == 1 &&
            state.calls[RETAINED_OVERLAY_FRAME] == 1,
        "anchor preflight rejects retention before any disposable overlay callback");
    state.role_anchor_seen = 0;

    state.phase = 1;
    TEST_ASSERT(
        UITree_EmitRefreshVolatile(tree, &host, &buf),
        "zero-to-nonzero refresh mutates the retained list without a second walk");

    for( int source = 0; source < RETAINED_OVERLAY_SOURCE_COUNT; source++ )
        desc[source] = emit_find_overlay_source(&buf, source);
    TEST_ASSERT(
        desc[RETAINED_OVERLAY_ENTITY] >= 0 && desc[RETAINED_OVERLAY_FRAME] >= 0 &&
            desc[RETAINED_OVERLAY_CANVAS] >= 0,
        "nonempty refresh inserts all three renderer descriptors");
    TEST_ASSERT(
        desc[RETAINED_OVERLAY_ENTITY] < desc[RETAINED_OVERLAY_FRAME] &&
            desc[RETAINED_OVERLAY_FRAME] < desc[RETAINED_OVERLAY_CANVAS],
        "entity overlays stay below frame chrome and canvas chrome stays above interfaces");
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
            state.calls[RETAINED_OVERLAY_CANVAS] == 2 &&
            state.calls[RETAINED_OVERLAY_FRAME] == 2,
        "retained refresh reissues entity, canvas, and frame requests once each");
    TEST_ASSERT(
        state.last_call_seq[RETAINED_OVERLAY_FRAME] <
            state.last_call_seq[RETAINED_OVERLAY_CANVAS],
        "retained refresh rebuilds frame state before the canvas overlay consumes it");

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
            emit_find_overlay_source(&buf, RETAINED_OVERLAY_FRAME) < 0 &&
            emit_find_overlay_source(&buf, RETAINED_OVERLAY_CANVAS) < 0,
        "zeroed overlay sources leave no stale renderer descriptors");
    TEST_ASSERT(
        state.calls[RETAINED_OVERLAY_ENTITY] == 4 &&
            state.calls[RETAINED_OVERLAY_CANVAS] == 4 &&
            state.calls[RETAINED_OVERLAY_FRAME] == 4,
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

static void
test_zero_mask_skin_explicitly_unmasks(void)
{
    struct UITree* tree = UITree_New(8);
    struct UITreeFrameSlotRect slots[UITREE_FRAME_SLOT_COUNT];
    struct UITreeEmitBuffer buf;
    struct UITreeHost host;
    struct TestHostState host_state;
    struct FrameNodes frame;
    struct UITreeEmitDesc const* compass;
    struct UITreeEmitDesc const* minimap_desc;
    int32_t shell;
    int32_t minimap;

    TEST_ASSERT(tree != NULL, "UITree_New");
    tree->mask_keep_opaque = 1;
    shell = UITree_TestPushXy(
        tree, -1, UIELEM_RS_LAYER, SHELL_ROOT_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    frame = push_cache_frame(tree, shell, 0);
    minimap = UITree_TestPushXy(
        tree, frame.root, UIELEM_BUILTIN_MINIMAP, FRAME_MINIMAP_ID, 600, 20, 120, 120);
    TEST_ASSERT(shell >= 0 && minimap >= 0, "skinned minimap fixture");
    tree->components[minimap].u.minimap.mask_scene_id = NATIVE_MINIMAP_MASK;
    tree->components[minimap].u.minimap.mask_atlas_index = 5;

    memset(slots, 0, sizeof(slots));
    slots[UITREE_FRAME_SLOT_COMPASS].all.placed = 1;
    slots[UITREE_FRAME_SLOT_COMPASS].all.x = PLUGIN_COMPASS_X;
    slots[UITREE_FRAME_SLOT_COMPASS].all.y = PLUGIN_COMPASS_Y;
    slots[UITREE_FRAME_SLOT_COMPASS].all.w = PLUGIN_COMPASS_W;
    slots[UITREE_FRAME_SLOT_COMPASS].all.h = PLUGIN_COMPASS_H;
    slots[UITREE_FRAME_SLOT_COMPASS].skin.placed = 1;
    slots[UITREE_FRAME_SLOT_COMPASS].skin.art_scene_id = PLUGIN_COMPASS_ART;
    slots[UITREE_FRAME_SLOT_COMPASS].skin.mask_scene_id = 0;
    slots[UITREE_FRAME_SLOT_MINIMAP].all.placed = 1;
    slots[UITREE_FRAME_SLOT_MINIMAP].all.x = 600;
    slots[UITREE_FRAME_SLOT_MINIMAP].all.y = 20;
    slots[UITREE_FRAME_SLOT_MINIMAP].all.w = 120;
    slots[UITREE_FRAME_SLOT_MINIMAP].all.h = 120;
    slots[UITREE_FRAME_SLOT_MINIMAP].skin.placed = 1;
    slots[UITREE_FRAME_SLOT_MINIMAP].skin.mask_scene_id = 0;
    UITree_FrameApply(tree, slots, FRAME_GROUP);

    UITree_TestHostInit(&host, &host_state);
    host_state.minimap_scene_id = 501;
    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(tree, &host, &buf, -1);
    compass = emit_find(&buf, frame.compass);
    minimap_desc = emit_find(&buf, minimap);
    TEST_ASSERT(compass != NULL && minimap_desc != NULL, "zero-mask frame surfaces emit");
    if( compass )
        TEST_ASSERT(
            compass->scene_id == PLUGIN_COMPASS_ART && compass->mask_scene_id == 0 &&
                compass->mask_atlas_index == 0 && !compass->mask_keep_opaque,
            "a placed zero compass mask disables the native mask and its polarity");
    if( minimap_desc )
        TEST_ASSERT(
            minimap_desc->scene_id == host_state.minimap_scene_id &&
                minimap_desc->mask_scene_id == 0 && minimap_desc->mask_atlas_index == 0 &&
                !minimap_desc->mask_keep_opaque,
            "a placed zero minimap mask disables the native mask and its polarity");
    TEST_ASSERT(
        tree->components[frame.compass].u.sprite.mask_scene_id == NATIVE_COMPASS_MASK &&
            tree->components[frame.compass].u.sprite.mask_atlas_index == 4 &&
            tree->components[minimap].u.minimap.mask_scene_id == NATIVE_MINIMAP_MASK &&
            tree->components[minimap].u.minimap.mask_atlas_index == 5,
        "explicit unmasking leaves both cache-authored masks native");

    UITree_FrameRelease(tree);
    buf.count = 0;
    UITree_EmitWalk(tree, &host, &buf, -1);
    compass = emit_find(&buf, frame.compass);
    minimap_desc = emit_find(&buf, minimap);
    TEST_ASSERT(
        compass && compass->mask_scene_id == NATIVE_COMPASS_MASK &&
            compass->mask_atlas_index == 4 && compass->mask_keep_opaque == 1 && minimap_desc &&
            minimap_desc->mask_scene_id == NATIVE_MINIMAP_MASK &&
            minimap_desc->mask_atlas_index == 5 && minimap_desc->mask_keep_opaque == 1,
        "release restores the native compass and minimap masks and polarity");

    UITree_EmitBufferFree(&buf);
    UITree_Free(tree);
}

static void
test_frame_visibility_invalidates_retention_and_hover(void)
{
    struct UITree* tree = UITree_New(4);
    struct UITreeFrameSlotRect slots[UITREE_FRAME_SLOT_COUNT];
    struct UITreeEmitBuffer buf;
    int32_t shell;
    int32_t root;
    int32_t chrome;
    uint32_t hidden_dirty;
    int hovered;

    TEST_ASSERT(tree != NULL, "UITree_New");
    shell = UITree_TestPushXy(
        tree, -1, UIELEM_RS_LAYER, SHELL_ROOT_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    root = UITree_TestPushXy(
        tree, shell, UIELEM_RS_LAYER, FRAME_ROOT_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    chrome = UITree_TestPushXy(
        tree, root, UIELEM_RS_RECT, FRAME_CHROME_ID, 0, 0, 24, 24);
    TEST_ASSERT(shell >= 0 && root >= 0 && chrome >= 0, "frame visibility fixture");
    tree->components[chrome].behavior.over_color = 0x00FF00;

    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(tree, NULL, &buf, -1);
    TEST_ASSERT(emit_find(&buf, chrome) != NULL, "native chrome starts reached and visible");

    memset(slots, 0, sizeof(slots));
    UITree_FrameApply(tree, slots, FRAME_GROUP);
    buf.count = 0;
    UITree_EmitWalk(tree, NULL, &buf, -1);
    TEST_ASSERT(tree->components[chrome].frame_hidden, "frame claim hides native chrome");
    TEST_ASSERT(emit_find(&buf, chrome) == NULL, "hidden frame chrome is not emitted");
    TEST_ASSERT(
        (uint32_t)chrome < tree->emit_visited_cap && !tree->emit_visited[chrome],
        "the retained reachability map records hidden chrome as unreachable");
    hovered = UITree_FindHoveredComponentIdForRegion(
        tree, NULL, -1, 5, 5, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    TEST_ASSERT(hovered != FRAME_CHROME_ID, "hidden frame chrome cannot win hover routing");

    hidden_dirty = tree->dirty_gen;
    UITree_FrameRelease(tree);
    TEST_ASSERT(!tree->components[chrome].frame_hidden, "frame release reveals native chrome");
    TEST_ASSERT(
        tree->dirty_gen > hidden_dirty,
        "hidden-to-visible frame chrome invalidates a retained emit list");
    UITree_TestResolve(tree);
    hovered = UITree_FindHoveredComponentIdForRegion(
        tree, NULL, -1, 5, 5, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    TEST_ASSERT(hovered == FRAME_CHROME_ID, "released native chrome participates in hover again");

    UITree_EmitBufferFree(&buf);
    UITree_Free(tree);
}

static void
test_large_ancestor_remains_effectively_stretched_after_shrink(void)
{
    struct UITree* tree = UITree_New(8);
    struct FrameNodes frame;
    int32_t shell;
    int const native_x = 37;
    int const native_y = 23;
    int const native_large_w = UITREE_LAYOUT_ROOT_W + 173;
    int const native_large_h = UITREE_LAYOUT_ROOT_H + 91;
    int const native_small_w = 320;
    int const native_small_h = 240;

    TEST_ASSERT(tree != NULL, "UITree_New");
    shell = UITree_TestPushXy(
        tree,
        -1,
        UIELEM_RS_LAYER,
        SHELL_ROOT_ID,
        native_x,
        native_y,
        native_large_w,
        native_large_h);
    TEST_ASSERT(shell >= 0, "large frame ancestor");
    frame = push_cache_frame(tree, shell, 0);
    declare_plugin_frame(tree);
    UITree_TestResolve(tree);
    TEST_ASSERT(
        raw_box_is(tree, shell, native_x, native_y, native_large_w, native_large_h) &&
            effective_box_is(
                tree, shell, native_x, native_y, native_large_w, native_large_h),
        "an initially oversized ancestor keeps its native effective extent");

    TEST_ASSERT(
        UITree_ApplySizeModes(tree, SHELL_ROOT_ID, native_small_w, native_small_h, 0, 0),
        "CS2 later shrinks the frame ancestor");
    UITree_FrameReassert(tree);
    UITree_TestResolve(tree);
    TEST_ASSERT(
        raw_box_is(tree, shell, native_x, native_y, native_small_w, native_small_h) &&
            raw_modes_are(tree, shell, -1, -1, 0, 0),
        "the shrunken ancestor remains native beneath the frame");
    TEST_ASSERT(
        effective_box_is(
            tree,
            shell,
            native_x,
            native_y,
            UITREE_LAYOUT_ROOT_W,
            UITREE_LAYOUT_ROOT_H),
        "the active frame keeps a later-shrunken ancestor canvas-sized");
    TEST_ASSERT(
        effective_box_is(
            tree, frame.compass,
            PLUGIN_COMPASS_X, PLUGIN_COMPASS_Y, PLUGIN_COMPASS_W, PLUGIN_COMPASS_H),
        "canvas-coordinate plugin placement ignores the native ancestor offset");

    UITree_FrameRelease(tree);
    UITree_TestResolve(tree);
    TEST_ASSERT(
        effective_box_is(
            tree, shell, native_x, native_y, native_small_w, native_small_h),
        "release reveals the ancestor's later native shrink");

    UITree_Free(tree);
}

static void
test_release_ignores_same_id_recycled_incarnations(void)
{
    struct UITree* tree = UITree_New(4);
    int32_t shell;
    int32_t old_root;
    int32_t old_compass;
    int32_t new_root;
    int32_t new_compass;
    uint32_t old_incarnation;
    struct UITreeEmitDesc native_desc;

    TEST_ASSERT(tree != NULL, "UITree_New");
    shell = UITree_TestPushXy(
        tree, -1, UIELEM_RS_LAYER, SHELL_ROOT_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    old_root = UITree_TestPushXy(
        tree, shell, UIELEM_RS_LAYER, FRAME_ROOT_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    old_compass = push_compass(
        tree,
        old_root,
        NATIVE_COMPASS_X,
        NATIVE_COMPASS_Y,
        NATIVE_COMPASS_W,
        NATIVE_COMPASS_H,
        NATIVE_COMPASS_ART,
        NATIVE_COMPASS_MASK);
    TEST_ASSERT(shell >= 0 && old_root >= 0 && old_compass >= 0, "recycle fixture");
    old_incarnation = tree->components[old_compass].incarnation;
    declare_plugin_frame(tree);

    UITree_ReclaimInterfaceGroup(tree, FRAME_GROUP);
    new_root = UITree_TestPushXy(
        tree, shell, UIELEM_RS_LAYER, FRAME_ROOT_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    new_compass = push_compass(tree, new_root, 512, 27, 31, 32, 901, 902);
    TEST_ASSERT(
        new_root == old_root && new_compass == old_compass,
        "free-list rebuild reuses the exact former frame indices");
    TEST_ASSERT(
        tree->components[new_compass].component_id == FRAME_COMPASS_ID &&
            tree->components[new_compass].incarnation != old_incarnation,
        "replacement keeps the same semantic id but has a new incarnation");

    /* Do not reassert: the standing declaration still names the reclaimed
     * incarnation. Effective geometry and skin lookups must reject that stale
     * entry even though both its array index and semantic component id collide. */
    UITree_TestResolve(tree);
    memset(&native_desc, 0, sizeof(native_desc));
    TEST_ASSERT(
        effective_box_is(tree, new_compass, 512, 27, 31, 32),
        "a recycled same-id node does not inherit stale effective geometry");
    TEST_ASSERT(
        UITree_EmitFill(
            tree, NULL, &tree->components[new_compass], new_compass, -1, &native_desc) &&
            native_desc.scene_id == 901 && native_desc.mask_scene_id == 902,
        "a recycled same-id node does not inherit stale effective skin");

    /* Release the declaration without reasserting it onto the replacement.
     * Index + component id both collide; only incarnation can distinguish the
     * new native node from the one whose frame layer is being dropped. */
    UITree_FrameRelease(tree);
    TEST_ASSERT(
        raw_box_is(tree, new_compass, 512, 27, 31, 32) &&
            tree->components[new_compass].u.sprite.scene_id == 901 &&
            tree->components[new_compass].u.sprite.mask_scene_id == 902,
        "release never restores stale geometry or skin onto a recycled same-id node");

    UITree_Free(tree);
}

static void
test_frame_slot_overlay_follows_target_subtree(void)
{
    enum
    {
        ANCHOR_GROUP = 190,
        ANCHOR_ROOT_ID = (ANCHOR_GROUP << 16) | 0,
        ANCHOR_BUTTON_ID = (191 << 16) | 0,
        ANCHOR_CHILD_ID = (CONTENT_GROUP << 16) | 20,
        ANCHOR_SIBLING_ID = (CONTENT_GROUP << 16) | 21,
        ANCHOR_SCENE = 7171,
        ANCHOR_SCENE_CHANGED = 7172,
        ANCHOR_X = 91,
        ANCHOR_Y = 72,
        ANCHOR_W = 280,
        ANCHOR_H = 180,
    };
    struct UITree* tree = UITree_New(8);
    struct UITreeEmitBuffer buf;
    struct UITreeFrameSlotRect slots[UITREE_FRAME_SLOT_COUNT];
    struct UITreeHost host;
    int32_t shell;
    int32_t root;
    int32_t button;
    int32_t child;
    int32_t sibling;
    int overlay_at;
    int child_at = -1;
    int sibling_at = -1;
    int button_descs = 0;
    uint32_t quiet_dirty;

    TEST_ASSERT(tree != NULL, "UITree_New");
    UITree_EmitBufferInit(&buf);
    UITree_HostInit(&host);
    shell = UITree_TestPushXy(
        tree, -1, UIELEM_RS_LAYER, SHELL_ROOT_ID, 0, 0,
        UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    root = UITree_TestPushXy(
        tree, shell, UIELEM_RS_LAYER, ANCHOR_ROOT_ID, 10, 11, 120, 90);
    tree->components[root].slot_tag = UITREE_SLOT_CHAT;
    button = UITree_TestPushXy(
        tree, root, UIELEM_BUILTIN_CHAT_BUTTON, ANCHOR_BUTTON_ID, 8, 9, 100, 32);
    child = UITree_TestPushXy(
        tree, button, UIELEM_RS_RECT, ANCHOR_CHILD_ID, 2, 3, 12, 13);
    sibling = UITree_TestPushXy(
        tree, root, UIELEM_RS_RECT, ANCHOR_SIBLING_ID, 30, 40, 14, 15);
    TEST_ASSERT(
        shell >= 0 && root >= 0 && button >= 0 && child >= 0 && sibling >= 0,
        "slot-overlay fixture builds");
    {
        struct UITreeChatButtonConfig* cfg = UITree_ChatButtonMut(&tree->components[button]);
        snprintf(cfg->label, sizeof(cfg->label), "%s", "Public chat");
        snprintf(cfg->mode_label[0], sizeof(cfg->mode_label[0]), "%s", "On");
    }

    memset(slots, 0, sizeof(slots));
    slots[UITREE_FRAME_SLOT_CHAT].all =
        (struct UITreeFrameRect){ 1, ANCHOR_X, ANCHOR_Y, ANCHOR_W, ANCHOR_H };
    slots[UITREE_FRAME_SLOT_CHAT_BUTTONS].all =
        (struct UITreeFrameRect){ 1, ANCHOR_X + 8, ANCHOR_Y + 9, 100, 32 };
    slots[UITREE_FRAME_SLOT_CHAT_BUTTONS].overlay =
        (struct UITreeFrameOverlay){ 1, ANCHOR_SCENE, ANCHOR_X + 3, ANCHOR_Y + 4, 17 };
    UITree_FrameApply(tree, slots, /*root_group=*/-1);
    UITree_EmitWalk(tree, &host, &buf, -1);

    overlay_at = emit_find_scene(&buf, ANCHOR_SCENE);
    for( int i = 0; i < buf.count; i++ )
    {
        if( buf.cmds[i].node_index == button )
            button_descs++;
        if( buf.cmds[i].node_index == child )
            child_at = i;
        if( buf.cmds[i].node_index == sibling )
            sibling_at = i;
    }
    TEST_ASSERT(button_descs == 2, "the target expands into both of its own descriptors");
    TEST_ASSERT(
        child_at >= 0 && overlay_at == child_at + 1 && sibling_at == overlay_at + 1,
        "slot paint is immediately after the whole target subtree and before its sibling");
    if( overlay_at >= 0 )
    {
        struct UITreeEmitDesc const* overlay = &buf.cmds[overlay_at];
        TEST_ASSERT(
            overlay->node_index == -1 && overlay->component_id == -1,
            "attached paint does not become an interactive copy of the target");
        TEST_ASSERT(
            overlay->x == ANCHOR_X + 3 && overlay->y == ANCHOR_Y + 4 &&
                overlay->trans == 17 && !overlay->if3,
            "attached paint retains its canvas sprite declaration");
        TEST_ASSERT(
            overlay->clip.x == ANCHOR_X && overlay->clip.y == ANCHOR_Y &&
                overlay->clip.w == ANCHOR_W && overlay->clip.h == ANCHOR_H,
            "attached paint uses the target parent clip, not the whole canvas");
    }

    quiet_dirty = tree->dirty_gen;
    UITree_FrameApply(tree, slots, /*root_group=*/-1);
    TEST_ASSERT(
        tree->dirty_gen == quiet_dirty,
        "an identical attached-paint declaration preserves the retained list");

    slots[UITREE_FRAME_SLOT_CHAT_BUTTONS].overlay.scene_id = ANCHOR_SCENE_CHANGED;
    UITree_FrameApply(tree, slots, /*root_group=*/-1);
    TEST_ASSERT(
        tree->dirty_gen > quiet_dirty,
        "changing attached paint invalidates the target's retained subtree");
    buf.count = 0;
    UITree_EmitWalk(tree, &host, &buf, -1);
    TEST_ASSERT(
        emit_find_scene(&buf, ANCHOR_SCENE) < 0 &&
            emit_find_scene(&buf, ANCHOR_SCENE_CHANGED) >= 0,
        "a retained declaration changes overlay scene atomically");

    quiet_dirty = tree->dirty_gen;
    slots[UITREE_FRAME_SLOT_CHAT_BUTTONS].overlay.placed = 0;
    UITree_FrameApply(tree, slots, /*root_group=*/-1);
    TEST_ASSERT(tree->dirty_gen > quiet_dirty, "nonzero-to-zero attached paint invalidates emit");
    buf.count = 0;
    UITree_EmitWalk(tree, &host, &buf, -1);
    TEST_ASSERT(
        emit_find_scene(&buf, ANCHOR_SCENE_CHANGED) < 0,
        "omitting attached paint removes it from the declaration");

    slots[UITREE_FRAME_SLOT_CHAT_BUTTONS].overlay.placed = 1;
    UITree_FrameApply(tree, slots, /*root_group=*/-1);
    TEST_ASSERT(UITree_ApplyHide(tree, ANCHOR_BUTTON_ID, 1), "hide the semantic target");
    buf.count = 0;
    UITree_EmitWalk(tree, &host, &buf, -1);
    TEST_ASSERT(
        emit_find_scene(&buf, ANCHOR_SCENE_CHANGED) < 0,
        "a hidden target drops its attached paint with the whole subtree");

    TEST_ASSERT(UITree_ApplyHide(tree, ANCHOR_BUTTON_ID, 0), "show the semantic target");
    UITree_ReclaimInterfaceGroup(tree, 191);
    buf.count = 0;
    UITree_EmitWalk(tree, &host, &buf, -1);
    TEST_ASSERT(
        emit_find_scene(&buf, ANCHOR_SCENE_CHANGED) < 0 &&
            emit_find(&buf, sibling) != NULL,
        "an absent target drops attached paint without disturbing later siblings");

    UITree_EmitBufferFree(&buf);
    UITree_Free(tree);
}

/*
 * A synthesised press reaches a button the FRAME PLUGIN is merely not showing.
 *
 * The mobile gameframe puts every sidebar panel away when its drawer is shut,
 * and it says so with `frame_hidden`. That flag is a statement about pixels: a
 * click on the screen must not land in a panel nobody can see, and it does not.
 * But a plugin pressing a button by NAME -- the minimap orbs' run toggle, which
 * lives in the controls panel -- is not a click on pixels, and fencing it there
 * is what made those orbs do nothing until the player opened a sidetab by hand.
 *
 * A hide the CACHE or a script authored stays a fence, in both forms, because
 * that one means the game says the button is not there.
 */
static void
test_synthetic_press_sees_through_frame_hidden(void)
{
    struct UITree* tree = UITree_New(4);
    int32_t panel;
    int32_t button;

    printf("TEST: a synthesised press sees through the frame plugin's own hiding\n");

    TEST_ASSERT(tree != NULL, "UITree_New");
    panel = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 900, 0, 0, 200, 200);
    button = UITree_TestPushXy(tree, panel, UIELEM_RS_GRAPHIC, 904, 10, 10, 40, 20);
    UITree_TestResolve(tree);

    TEST_ASSERT(
        !UITree_NodeOrAncestorDisplayHidden(tree, button) &&
            !UITree_NodeOrAncestorDisplayHiddenEx(tree, button, 0, 1),
        "a shown button is hidden by neither query");

    tree->components[panel].frame_hidden = 1;
    TEST_ASSERT(
        UITree_NodeOrAncestorDisplayHidden(tree, button),
        "a click on pixels still refuses a frame-hidden subtree");
    TEST_ASSERT(
        !UITree_NodeOrAncestorDisplayHiddenEx(tree, button, 0, 1),
        "a synthesised press reaches into a frame-hidden subtree");

    tree->components[panel].screen_hidden = 1;
    TEST_ASSERT(
        UITree_NodeOrAncestorDisplayHiddenEx(tree, button, 0, 1),
        "screen suppression is not the frame plugin's and still fences");
    tree->components[panel].screen_hidden = 0;

    tree->components[panel].behavior.hide = 1;
    TEST_ASSERT(
        UITree_NodeOrAncestorDisplayHiddenEx(tree, button, 0, 1),
        "a hide the cache or a script authored still fences the press");

    UITree_Free(tree);
}

void
test_frame_replacement(void)
{
    printf("TEST: plugin frame replacement ownership / rebuild stability\n");

    test_frame_keeps_native_state_beneath_effective_layout();
    test_frame_reconciles_rebuilt_nodes_before_emit();
    test_retained_overlay_refresh_preserves_source();
    test_retained_empty_entity_keeps_final_no_world_order();
    test_transparent_entity_overlay_stays_absent_on_refresh();
    test_zero_mask_skin_explicitly_unmasks();
    test_frame_visibility_invalidates_retention_and_hover();
    test_large_ancestor_remains_effectively_stretched_after_shrink();
    test_release_ignores_same_id_recycled_incarnations();
    test_frame_slot_overlay_follows_target_subtree();
    test_synthetic_press_sees_through_frame_hidden();
}
