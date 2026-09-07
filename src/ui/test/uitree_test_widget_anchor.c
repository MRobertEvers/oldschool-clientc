/*
 * Widget anchors: a plugin states that one widget is drawn and hit directly
 * OVER, directly BEHIND, or in place of (REPLACE) another widget. The relation
 * is a retained per-owner edit; paint, hit testing and presentation follow it
 * through the one common ordering pass, UITree_FrameReorder.
 */
#include "test_harness.h"
#include "uitree_frame.h"
#include "uitree_input.h"

static int rect_pixel(struct UITreeEmitBuffer const* buffer, int x, int y)
{
    int pixel = -1;
    for( int i = 0; i < buffer->count; i++ )
    {
        struct UITreeEmitDesc const* d = &buffer->cmds[i];
        if( d->kind == UITREE_EMIT_RECT && d->filled && x >= d->x && y >= d->y &&
            x < d->x + d->w && y < d->y + d->h ) pixel = d->color;
    }
    return pixel;
}

static int32_t anchor_button(struct UITree* tree, int32_t parent, int id, int color, int x, int y, int w, int h)
{
    int32_t node = UITree_TestPushXy(tree, parent, UIELEM_RS_RECT, id, x, y, w, h);
    tree->components[node].if3 = 1;
    tree->components[node].u.rs_rect.filled = 1;
    tree->components[node].u.rs_rect.color = color;
    UITree_HooksMut(&tree->components[node])->on_click.script_id = 42;
    UITree_HooksMut(&tree->components[node])->on_mouse_over.script_id = 43;
    return node;
}

struct AnchorScene
{
    struct UITree* tree;
    struct UITreeHost host;
    struct TestHostState state;
    struct UITreeEmitBuffer buffer;
    int32_t root, a, b, c;
};

static void scene_open(struct AnchorScene* s)
{
    memset(s, 0, sizeof(*s));
    s->tree = UITree_New(16);
    s->root = UITree_TestPushXy(s->tree, -1, UIELEM_RS_LAYER, 790 << 16, 0, 0, 765, 503);
    /* Native order a, b, c: c paints last and wins every overlap. */
    s->a = anchor_button(s->tree, s->root, (790 << 16) | 1, 0xff0000, 10, 10, 50, 50);
    s->b = anchor_button(s->tree, s->root, (790 << 16) | 2, 0x00ff00, 20, 20, 20, 20);
    s->c = anchor_button(s->tree, s->root, (790 << 16) | 3, 0x0000ff, 25, 25, 10, 10);
    UITree_TestHostInit(&s->host, &s->state);
    UITree_TestResolve(s->tree);
    UITree_EmitBufferInit(&s->buffer);
}

static void scene_publish(struct AnchorScene* s)
{
    UITree_TestResolve(s->tree);
    s->buffer.count = 0;
    UITree_EmitWalk(s->tree, &s->host, &s->buffer, -1);
}

static void scene_close(struct AnchorScene* s)
{
    UITree_EmitBufferFree(&s->buffer);
    UITree_Free(s->tree);
}

static struct UITreeNodeRef ref(struct AnchorScene* s, int32_t node) { return UITree_RefAt(s->tree, node); }

static void test_anchor_behind_and_over(void)
{
    struct AnchorScene s;
    scene_open(&s);
    scene_publish(&s);
    TEST_ASSERT(rect_pixel(&s.buffer, 30, 30) == 0x0000ff && UITree_HitTestInteractive(s.tree, &s.host, 30, 30) == s.c,
                "natively the last sibling paints and is hit on top");

    TEST_ASSERT(UITree_WidgetSetAnchor(s.tree, ref(&s, s.c), 7, ref(&s, s.a), UITREE_WIDGET_RELATION_BEHIND) == UITREE_WIDGET_ANCHOR_OK,
                "a later sibling can be anchored behind an earlier one");
    TEST_ASSERT(UITree_WidgetAnchorCount(s.tree) == 1, "one retained anchor edit is counted");
    scene_publish(&s);
    printf("WIDGET_ANCHOR behind pixel=%06x hit=%d a=%d b=%d c=%d\n", rect_pixel(&s.buffer, 30, 30),
           UITree_HitTestInteractive(s.tree, &s.host, 30, 30), s.a, s.b, s.c);
    TEST_ASSERT(rect_pixel(&s.buffer, 30, 30) == 0x00ff00 && UITree_HitTestInteractive(s.tree, &s.host, 30, 30) == s.b,
                "BEHIND a: c paints under a and b, and b takes the hit");
    TEST_ASSERT(rect_pixel(&s.buffer, 12, 12) == 0xff0000, "the target keeps painting where nothing covers it");

    TEST_ASSERT(UITree_WidgetSetAnchor(s.tree, ref(&s, s.a), 7, ref(&s, s.b), UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_OK,
                "an earlier sibling can be anchored over a later one");
    scene_publish(&s);
    printf("WIDGET_ANCHOR over pixel=%06x hit=%d\n", rect_pixel(&s.buffer, 30, 30),
           UITree_HitTestInteractive(s.tree, &s.host, 30, 30));
    TEST_ASSERT(rect_pixel(&s.buffer, 30, 30) == 0xff0000 && UITree_HitTestInteractive(s.tree, &s.host, 30, 30) == s.a,
                "OVER b: a paints over b, c stays behind a, and a takes the hit");

    /* Reset drops only that owner's edit and restores the native order. */
    TEST_ASSERT(UITree_WidgetReset(s.tree, ref(&s, s.a), 7) && UITree_WidgetReset(s.tree, ref(&s, s.c), 7),
                "reset accepts the anchored widgets");
    TEST_ASSERT(UITree_WidgetAnchorCount(s.tree) == 0, "reset releases the anchor edits");
    scene_publish(&s);
    TEST_ASSERT(rect_pixel(&s.buffer, 30, 30) == 0x0000ff && UITree_HitTestInteractive(s.tree, &s.host, 30, 30) == s.c,
                "after reset the native order is back");
    scene_close(&s);
}

static void test_anchor_replace_follows_native_visibility(void)
{
    struct AnchorScene s;
    scene_open(&s);
    TEST_ASSERT(UITree_WidgetSetAnchor(s.tree, ref(&s, s.b), 7, ref(&s, s.a), UITREE_WIDGET_RELATION_REPLACE) == UITREE_WIDGET_ANCHOR_OK,
                "b replaces a");
    scene_publish(&s);
    printf("WIDGET_ANCHOR replace covered=%06x uncovered=%06x hit_uncovered=%d\n", rect_pixel(&s.buffer, 30, 30),
           rect_pixel(&s.buffer, 12, 12), UITree_HitTestInteractive(s.tree, &s.host, 12, 12));
    TEST_ASSERT(rect_pixel(&s.buffer, 12, 12) == -1 && UITree_HitTestInteractive(s.tree, &s.host, 12, 12) == -1,
                "REPLACE removes the target's paint and input even where the replacement does not cover");
    TEST_ASSERT(rect_pixel(&s.buffer, 22, 22) == 0x00ff00 && UITree_HitTestInteractive(s.tree, &s.host, 22, 22) == s.b,
                "the replacement paints and is hit in the target's place");
    TEST_ASSERT(!UITree_FrameNodePresented(s.tree, &s.host, s.a) && UITree_FrameNodeReplaced(s.tree, &s.host, s.a),
                "a replaced target is not presented");
    TEST_ASSERT(UITree_FrameNodePresented(s.tree, &s.host, s.b), "the presented replacement is presented");

    /* A hidden replacement reveals the target. */
    TEST_ASSERT(UITree_WidgetSetHidden(s.tree, ref(&s, s.b), 7, true), "hide the replacement");
    scene_publish(&s);
    TEST_ASSERT(rect_pixel(&s.buffer, 12, 12) == 0xff0000 && UITree_HitTestInteractive(s.tree, &s.host, 12, 12) == s.a,
                "a hidden replacement reveals the target");
    TEST_ASSERT(UITree_FrameNodePresented(s.tree, &s.host, s.a), "the revealed target is presented again");
    TEST_ASSERT(UITree_WidgetSetHidden(s.tree, ref(&s, s.b), 7, false), "show the replacement");

    /* A natively hidden target drops its replacement: REPLACE inherits the veto. */
    UITree_SetHideAt(s.tree, s.a, 1);
    scene_publish(&s);
    printf("WIDGET_ANCHOR replace target_hidden covered=%06x uncovered=%06x hit=%d presented=%d\n",
           rect_pixel(&s.buffer, 30, 30), rect_pixel(&s.buffer, 22, 22),
           UITree_HitTestInteractive(s.tree, &s.host, 30, 30), UITree_FrameNodePresented(s.tree, &s.host, s.b));
    TEST_ASSERT(rect_pixel(&s.buffer, 30, 30) == 0x0000ff && UITree_HitTestInteractive(s.tree, &s.host, 30, 30) == s.c &&
                rect_pixel(&s.buffer, 22, 22) == -1 && UITree_HitTestInteractive(s.tree, &s.host, 22, 22) == -1,
                "a natively hidden target takes its replacement down with it");
    TEST_ASSERT(!UITree_FrameNodePresented(s.tree, &s.host, s.b), "a replacement of a hidden target is not presented");
    scene_close(&s);
}

static void test_anchor_chain_and_absent_target(void)
{
    struct AnchorScene s;
    scene_open(&s);
    /* b behind a, c over b: the tree is b, c, a -- c under a's red. */
    TEST_ASSERT(UITree_WidgetSetAnchor(s.tree, ref(&s, s.b), 7, ref(&s, s.a), UITREE_WIDGET_RELATION_BEHIND) == UITREE_WIDGET_ANCHOR_OK &&
                UITree_WidgetSetAnchor(s.tree, ref(&s, s.c), 7, ref(&s, s.b), UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_OK,
                "anchors chain");
    scene_publish(&s);
    printf("WIDGET_ANCHOR chain pixel=%06x hit=%d\n", rect_pixel(&s.buffer, 30, 30), UITree_HitTestInteractive(s.tree, &s.host, 30, 30));
    TEST_ASSERT(rect_pixel(&s.buffer, 30, 30) == 0xff0000 && UITree_HitTestInteractive(s.tree, &s.host, 30, 30) == s.a,
                "c over b, b behind a: a still covers both");
    /* The target of the chain disappears: the children keep their native positions. */
    UITree_SetHideAt(s.tree, s.a, 1);
    scene_publish(&s);
    TEST_ASSERT(rect_pixel(&s.buffer, 30, 30) == 0x0000ff && UITree_HitTestInteractive(s.tree, &s.host, 30, 30) == s.c,
                "with the target hidden, OVER/BEHIND children keep their native order");
    scene_close(&s);
}

static void test_anchor_rejections(void)
{
    struct AnchorScene s;
    scene_open(&s);
    struct UITreeNodeRef stale = ref(&s, s.c);
    int32_t owned = UITree_WidgetCreateText(s.tree, ref(&s, s.root), 9, "other", 0);
    TEST_ASSERT(owned >= 0, "another owner's control exists");
    TEST_ASSERT(UITree_WidgetSetAnchor(s.tree, ref(&s, s.a), 7, ref(&s, s.a), UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_INVALID,
                "a widget cannot anchor to itself");
    TEST_ASSERT(UITree_WidgetSetAnchor(s.tree, ref(&s, s.a), 7, ref(&s, s.root), UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_INVALID,
                "a widget cannot anchor to its ancestor");
    TEST_ASSERT(UITree_WidgetSetAnchor(s.tree, ref(&s, s.root), 7, ref(&s, s.a), UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_INVALID,
                "a widget cannot anchor to its descendant");
    TEST_ASSERT(UITree_WidgetSetAnchor(s.tree, ref(&s, s.a), 7, ref(&s, s.b), (enum UITreeWidgetRelation)9) == UITREE_WIDGET_ANCHOR_INVALID,
                "an unknown relation is rejected");
    TEST_ASSERT(UITree_WidgetSetAnchor(s.tree, ref(&s, s.a), 7, ref(&s, s.b), UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_OK &&
                UITree_WidgetSetAnchor(s.tree, ref(&s, s.b), 7, ref(&s, s.a), UITREE_WIDGET_RELATION_BEHIND) == UITREE_WIDGET_ANCHOR_INVALID,
                "a cycle through the effective anchors is rejected");
    TEST_ASSERT(UITree_WidgetSetAnchor(s.tree, UITree_RefAt(s.tree, owned), 7, ref(&s, s.a), UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_BLOCKED,
                "another owner's control cannot be anchored");
    TEST_ASSERT(UITree_WidgetSetAnchor(s.tree, UITree_RefAt(s.tree, owned), 9, ref(&s, s.a), UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_OK,
                "its own owner can anchor it");
    TEST_ASSERT(UITree_WidgetRemove(s.tree, stale, 7) == false, "a native widget is not removable");
    UITree_ClearChildren(s.tree, s.root);
    TEST_ASSERT(UITree_WidgetSetAnchor(s.tree, stale, 7, ref(&s, s.root), UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_STALE,
                "a dead widget reports stale");
    /* The clear keeps plugin-owned children; only the native anchor edit went with its node. */
    TEST_ASSERT(UITree_WidgetAnchorCount(s.tree) == 1, "freed native widgets release their anchor edits");
    TEST_ASSERT(UITree_WidgetRemove(s.tree, UITree_RefAt(s.tree, owned), 9), "the owner removes its control");
    TEST_ASSERT(UITree_WidgetAnchorCount(s.tree) == 0, "a removed owned control releases its anchor edit");
    scene_close(&s);
}

void test_widget_anchor_depth(void)
{
    test_anchor_behind_and_over();
    test_anchor_replace_follows_native_visibility();
    test_anchor_chain_and_absent_target();
    test_anchor_rejections();
}
