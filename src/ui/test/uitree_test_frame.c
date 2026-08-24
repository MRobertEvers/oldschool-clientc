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
    PLUGIN_COMPASS_ART = 141,
    PLUGIN_COMPASS_MASK = 142,
};

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

void
test_frame_replacement(void)
{
    printf("TEST: plugin frame replacement ownership / rebuild stability\n");

    test_frame_keeps_native_state_beneath_effective_layout();
    test_frame_reconciles_rebuilt_nodes_before_emit();
}
