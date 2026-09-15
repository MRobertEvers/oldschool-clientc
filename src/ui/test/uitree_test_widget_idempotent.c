/*
 * Retained plugin edits are compare-then-set: restating a value that the
 * caller's own edit already holds AND already wins with is not a write.
 *
 * The frame that motivated this: with a plugin frame provider live, a steady
 * frame restated 8 identical hidden values and 2 identical skins, and every
 * one of them bumped the global edit serial, marked the node dirty, dirtied
 * layout and (for an anchor) bumped dirty_gen, which defeats the retained-emit
 * gate outright -- a full re-emit bought with nothing.
 *
 * What must NOT change is arbitration. The effective value of an aspect is the
 * edit with the highest serial for it, so a NON-winning owner restating its own
 * stored value is how that owner takes the aspect back. That write still has to
 * happen, and these tests pin both halves against each other.
 */
#include "test_harness.h"
#include "uitree_frame.h"

#define OWNER_A 7u
#define OWNER_B 8u

#define SPRITE_NATIVE_ART 5

struct IdemScene
{
    struct UITree* tree;
    struct UITreeHost host;
    struct TestHostState state;
    struct UITreeEmitBuffer buffer;
    struct UITreeEmitRetainGate gate;
    int32_t root, rect, sprite, target;
};

/* Everything a quiet frame must not move. */
struct Quiet
{
    uint64_t serial;
    uint32_t generation;
    uint32_t dirty_gen;
    uint32_t layout_resolve_seq;
    uint8_t layout_stale;
    uint8_t layout_force_full;
    int anchor_edits;
};

static void
scene_open(struct IdemScene* s)
{
    memset(s, 0, sizeof(*s));
    s->tree = UITree_New(16);
    assert(s->tree);
    UITree_TestHostInit(&s->host, &s->state);
    UITree_EmitBufferInit(&s->buffer);
    s->root = UITree_TestPushXy(s->tree, -1, UIELEM_RS_LAYER, 792 << 16, 0, 0, 765, 503);
    s->rect = UITree_TestPushXy(s->tree, s->root, UIELEM_RS_RECT, (792 << 16) | 1, 10, 10, 50, 50);
    s->sprite = UITree_TestPushXy(s->tree, s->root, UIELEM_BUILTIN_SPRITE, (792 << 16) | 2, 100, 10, 32, 32);
    s->target = UITree_TestPushXy(s->tree, s->root, UIELEM_RS_RECT, (792 << 16) | 3, 200, 10, 50, 50);
    s->tree->components[s->sprite].u.sprite.scene_id = SPRITE_NATIVE_ART;
}

static void
scene_close(struct IdemScene* s)
{
    UITree_EmitBufferFree(&s->buffer);
    UITree_Free(s->tree);
}

/* Resolve, publish, capture the retained-emit identity and clear every node's
 * dirty bit -- the state a settled client frame is actually in. */
static void
settle(struct IdemScene* s)
{
    UITree_TestResolve(s->tree);
    s->buffer.count = 0;
    UITree_EmitWalk(s->tree, &s->host, &s->buffer, -1);
    UITree_EmitRetainGateCapture(s->tree, &s->buffer, -1, &s->gate);
    for( uint32_t i = 0; i < s->tree->component_count; ++i )
        UITree_ClearNodeDirty(s->tree, (int32_t)i);
}

static struct Quiet
quiet_capture(struct IdemScene const* s)
{
    struct Quiet q;
    q.serial = UITree_WidgetEditSerial();
    q.generation = s->tree->generation;
    q.dirty_gen = s->tree->dirty_gen;
    q.layout_resolve_seq = s->tree->layout_resolve_seq;
    q.layout_stale = s->tree->layout_stale;
    q.layout_force_full = s->tree->layout_force_full;
    q.anchor_edits = UITree_WidgetAnchorCount(s->tree);
    return q;
}

/* The full "nothing happened" verdict for one node: no serial tick, no
 * generation or dirty_gen movement, no layout invalidation, no loss of the
 * resolved box, no emit mark -- and the retained-emit gate still authorizes
 * the published buffer. */
static void
assert_quiet(struct IdemScene* s, struct Quiet const* before, int32_t node, char const* what)
{
    char msg[192];
    snprintf(msg, sizeof(msg), "%s: the edit serial does not tick", what);
    TEST_ASSERT(UITree_WidgetEditSerial() == before->serial, msg);
    snprintf(msg, sizeof(msg), "%s: the tree generation does not move", what);
    TEST_ASSERT(s->tree->generation == before->generation, msg);
    snprintf(msg, sizeof(msg), "%s: dirty_gen does not move", what);
    TEST_ASSERT(s->tree->dirty_gen == before->dirty_gen, msg);
    snprintf(msg, sizeof(msg), "%s: layout is not invalidated", what);
    TEST_ASSERT(s->tree->layout_stale == before->layout_stale, msg);
    snprintf(msg, sizeof(msg), "%s: no full layout is forced", what);
    TEST_ASSERT(s->tree->layout_force_full == before->layout_force_full, msg);
    snprintf(msg, sizeof(msg), "%s: the layout resolve sequence does not move", what);
    TEST_ASSERT(s->tree->layout_resolve_seq == before->layout_resolve_seq, msg);
    snprintf(msg, sizeof(msg), "%s: the node keeps its resolved box", what);
    TEST_ASSERT(s->tree->components[node].position.layout_resolved != 0, msg);
    snprintf(msg, sizeof(msg), "%s: the node is not marked for emit", what);
    TEST_ASSERT(!UITree_NodeNeedsEmit(&s->tree->components[node]), msg);
    snprintf(msg, sizeof(msg), "%s: the retained-emit gate still retains", what);
    TEST_ASSERT(
        UITree_EmitRetainGateQuiet(s->tree, &s->host, &s->buffer, -1, &s->gate), msg);
    snprintf(msg, sizeof(msg), "%s: the anchor-edit census does not move", what);
    TEST_ASSERT(UITree_WidgetAnchorCount(s->tree) == before->anchor_edits, msg);
}

static void
assert_wrote(struct IdemScene* s, struct Quiet const* before, int32_t node, char const* what)
{
    char msg[192];
    snprintf(msg, sizeof(msg), "%s: the edit serial ticks", what);
    TEST_ASSERT(UITree_WidgetEditSerial() > before->serial, msg);
    snprintf(msg, sizeof(msg), "%s: the node is marked for emit", what);
    TEST_ASSERT(UITree_NodeNeedsEmit(&s->tree->components[node]), msg);
}

static void
effective_box(struct IdemScene const* s, int32_t node, int* x, int* y, int* w, int* h)
{
    struct UITreeElemPosition pos = s->tree->components[node].position;
    UITree_WidgetPositionOverride(s->tree, node, &pos);
    *x = pos.x; *y = pos.y; *w = pos.width; *h = pos.height;
}

static int
effective_art(struct IdemScene const* s)
{
    int art = -1, mask = -1;
    UITree_WidgetSkinAt(s->tree, s->sprite, &art, &mask);
    return art;
}

static int
effective_mask(struct IdemScene const* s)
{
    int art = -1, mask = -1;
    UITree_WidgetSkinAt(s->tree, s->sprite, &art, &mask);
    return mask;
}

/* ---------------------------------------------------------------- geometry */

static void
test_idempotent_geometry(struct IdemScene* s)
{
    struct UITreeNodeRef const r = UITree_RefAt(s->tree, s->rect);
    struct Quiet q;
    int x, y, w, h;

    TEST_ASSERT(UITree_WidgetSetPosition(s->tree, r, OWNER_A, 40, 50), "owner A places the rect");
    TEST_ASSERT(UITree_WidgetSetSize(s->tree, r, OWNER_A, 64, 24), "owner A sizes the rect");
    settle(s);

    /* (a) the winner restates what it already holds. */
    q = quiet_capture(s);
    TEST_ASSERT(UITree_WidgetSetPosition(s->tree, r, OWNER_A, 40, 50), "an identical position still reports success");
    assert_quiet(s, &q, s->rect, "identical position by the winner");
    TEST_ASSERT(UITree_WidgetSetSize(s->tree, r, OWNER_A, 64, 24), "an identical size still reports success");
    assert_quiet(s, &q, s->rect, "identical size by the winner");
    effective_box(s, s->rect, &x, &y, &w, &h);
    TEST_ASSERT(x == 40 && y == 50 && w == 64 && h == 24, "the effective rectangle is unchanged");

    /* An eight-write steady frame -- the shape measured in the live client. */
    q = quiet_capture(s);
    for( int i = 0; i < 8; ++i )
    {
        UITree_WidgetSetPosition(s->tree, r, OWNER_A, 40, 50);
        UITree_WidgetSetSize(s->tree, r, OWNER_A, 64, 24);
    }
    assert_quiet(s, &q, s->rect, "a steady frame of identical geometry writes");

    /* (b) a changed value by the same owner still applies and still dirties. */
    q = quiet_capture(s);
    TEST_ASSERT(UITree_WidgetSetPosition(s->tree, r, OWNER_A, 41, 50), "a moved position applies");
    assert_wrote(s, &q, s->rect, "a changed position");
    TEST_ASSERT(s->tree->layout_stale, "a changed position invalidates layout");
    TEST_ASSERT(!s->tree->components[s->rect].position.layout_resolved, "a changed position unresolves the node");
    settle(s);
    q = quiet_capture(s);
    TEST_ASSERT(UITree_WidgetSetSize(s->tree, r, OWNER_A, 65, 24), "a changed size applies");
    assert_wrote(s, &q, s->rect, "a changed size");
    settle(s);
    effective_box(s, s->rect, &x, &y, &w, &h);
    TEST_ASSERT(x == 41 && y == 50 && w == 65 && h == 24, "the changed rectangle took effect");

    /* (c) a NON-winning owner restating its own value still takes the win. */
    TEST_ASSERT(UITree_WidgetSetPosition(s->tree, r, OWNER_A, 60, 60), "owner A states its rectangle");
    TEST_ASSERT(UITree_WidgetSetPosition(s->tree, r, OWNER_B, 70, 70), "owner B outbids it");
    settle(s);
    effective_box(s, s->rect, &x, &y, &w, &h);
    TEST_ASSERT(x == 70 && y == 70, "the later writer owns the position");
    q = quiet_capture(s);
    TEST_ASSERT(UITree_WidgetSetPosition(s->tree, r, OWNER_A, 60, 60), "owner A re-asserts the value it already stored");
    assert_wrote(s, &q, s->rect, "an identical re-assert by a losing owner");
    settle(s);
    effective_box(s, s->rect, &x, &y, &w, &h);
    TEST_ASSERT(x == 60 && y == 60, "re-asserting an unchanged value takes the win back");

    /* (d) two owners alternating identical values: each write arrives from the
     * losing side, so each is a real write -- two serial ticks per round, and
     * the round's last writer owns the rectangle, exactly as before. */
    for( int round = 0; round < 4; ++round )
    {
        uint64_t const before = UITree_WidgetEditSerial();
        TEST_ASSERT(UITree_WidgetSetPosition(s->tree, r, OWNER_B, 70, 70), "owner B restates");
        TEST_ASSERT(UITree_WidgetSetPosition(s->tree, r, OWNER_A, 60, 60), "owner A restates");
        TEST_ASSERT(UITree_WidgetEditSerial() == before + 2, "alternating owners each pay one serial");
        settle(s);
        effective_box(s, s->rect, &x, &y, &w, &h);
        TEST_ASSERT(x == 60 && y == 60, "the last of the two alternating writers wins the round");
    }

    TEST_ASSERT(UITree_WidgetReset(s->tree, r, OWNER_A), "owner A releases the rect");
    TEST_ASSERT(UITree_WidgetReset(s->tree, r, OWNER_B), "owner B releases the rect");
    settle(s);
}

/* ------------------------------------------------------------------ hidden */

static void
test_idempotent_hidden(struct IdemScene* s)
{
    struct UITreeNodeRef const r = UITree_RefAt(s->tree, s->rect);
    struct Quiet q;

    TEST_ASSERT(UITree_WidgetSetHidden(s->tree, r, OWNER_A, true), "owner A hides the rect");
    settle(s);
    TEST_ASSERT(s->tree->components[s->rect].widget_hidden, "the rect is hidden");

    /* (a) -- eight identical hidden writes, the measured steady frame. */
    q = quiet_capture(s);
    for( int i = 0; i < 8; ++i )
        TEST_ASSERT(UITree_WidgetSetHidden(s->tree, r, OWNER_A, true), "an identical hide still reports success");
    assert_quiet(s, &q, s->rect, "eight identical hidden writes by the winner");
    TEST_ASSERT(s->tree->components[s->rect].widget_hidden, "the rect is still hidden");

    /* (b) */
    q = quiet_capture(s);
    TEST_ASSERT(UITree_WidgetSetHidden(s->tree, r, OWNER_A, false), "unhiding applies");
    assert_wrote(s, &q, s->rect, "a changed hidden value");
    TEST_ASSERT(s->tree->dirty_gen != q.dirty_gen, "a visibility change is a reachability change");
    TEST_ASSERT(!s->tree->components[s->rect].widget_hidden, "the rect is shown");
    settle(s);

    /* (c) */
    TEST_ASSERT(UITree_WidgetSetHidden(s->tree, r, OWNER_A, true), "owner A hides");
    TEST_ASSERT(UITree_WidgetSetHidden(s->tree, r, OWNER_B, false), "owner B shows");
    settle(s);
    TEST_ASSERT(!s->tree->components[s->rect].widget_hidden, "the later writer decides visibility");
    q = quiet_capture(s);
    TEST_ASSERT(UITree_WidgetSetHidden(s->tree, r, OWNER_A, true), "owner A re-asserts hidden");
    assert_wrote(s, &q, s->rect, "an identical hide by a losing owner");
    TEST_ASSERT(s->tree->components[s->rect].widget_hidden, "re-asserting hidden takes the win back");

    /* (d) */
    settle(s);
    for( int round = 0; round < 4; ++round )
    {
        uint64_t const before = UITree_WidgetEditSerial();
        TEST_ASSERT(UITree_WidgetSetHidden(s->tree, r, OWNER_B, false), "owner B restates shown");
        TEST_ASSERT(UITree_WidgetSetHidden(s->tree, r, OWNER_A, true), "owner A restates hidden");
        TEST_ASSERT(UITree_WidgetEditSerial() == before + 2, "alternating hidden owners each pay one serial");
        TEST_ASSERT(s->tree->components[s->rect].widget_hidden, "owner A ends each round hidden");
        settle(s);
    }

    TEST_ASSERT(UITree_WidgetReset(s->tree, r, OWNER_A), "owner A releases the rect");
    TEST_ASSERT(UITree_WidgetReset(s->tree, r, OWNER_B), "owner B releases the rect");
    settle(s);
}

/* -------------------------------------------------------------------- skin */

static void
test_idempotent_skin(struct IdemScene* s)
{
    struct UITreeNodeRef const sp = UITree_RefAt(s->tree, s->sprite);
    struct Quiet q;

    TEST_ASSERT(UITree_WidgetSetArt(s->tree, sp, OWNER_A, 900), "owner A re-skins the sprite");
    TEST_ASSERT(UITree_WidgetSetMask(s->tree, sp, OWNER_A, 902), "owner A masks the sprite");
    settle(s);
    TEST_ASSERT(effective_art(s) == 900 && effective_mask(s) == 902, "the skin is in force");

    /* (a) -- two identical skin writes, the measured steady frame. */
    q = quiet_capture(s);
    for( int i = 0; i < 2; ++i )
    {
        TEST_ASSERT(UITree_WidgetSetArt(s->tree, sp, OWNER_A, 900), "an identical art still reports success");
        TEST_ASSERT(UITree_WidgetSetMask(s->tree, sp, OWNER_A, 902), "an identical mask still reports success");
    }
    assert_quiet(s, &q, s->sprite, "identical skin writes by the winner");
    TEST_ASSERT(effective_art(s) == 900 && effective_mask(s) == 902, "the skin is unchanged");

    /* (b) */
    q = quiet_capture(s);
    TEST_ASSERT(UITree_WidgetSetArt(s->tree, sp, OWNER_A, 901), "a changed art applies");
    assert_wrote(s, &q, s->sprite, "a changed art");
    settle(s);
    q = quiet_capture(s);
    TEST_ASSERT(UITree_WidgetSetMask(s->tree, sp, OWNER_A, 0), "an empty mask is a real value, not an absence");
    assert_wrote(s, &q, s->sprite, "a changed mask");
    settle(s);
    TEST_ASSERT(effective_art(s) == 901 && effective_mask(s) == 0, "the changed skin took effect");

    /* Restating that empty mask is still a no-op: 0 is stated, not unstated. */
    q = quiet_capture(s);
    TEST_ASSERT(UITree_WidgetSetMask(s->tree, sp, OWNER_A, 0), "the empty mask restates");
    assert_quiet(s, &q, s->sprite, "an identical empty mask by the winner");

    /* (c) */
    TEST_ASSERT(UITree_WidgetSetArt(s->tree, sp, OWNER_B, 950), "owner B outbids the art");
    settle(s);
    TEST_ASSERT(effective_art(s) == 950, "the later writer's art shows");
    q = quiet_capture(s);
    TEST_ASSERT(UITree_WidgetSetArt(s->tree, sp, OWNER_A, 901), "owner A re-asserts the art it already stored");
    assert_wrote(s, &q, s->sprite, "an identical art re-assert by a losing owner");
    settle(s);
    TEST_ASSERT(effective_art(s) == 901, "re-asserting an unchanged art takes the win back");

    /* (d) */
    for( int round = 0; round < 4; ++round )
    {
        uint64_t const before = UITree_WidgetEditSerial();
        TEST_ASSERT(UITree_WidgetSetArt(s->tree, sp, OWNER_B, 950), "owner B restates its art");
        TEST_ASSERT(UITree_WidgetSetArt(s->tree, sp, OWNER_A, 901), "owner A restates its art");
        TEST_ASSERT(UITree_WidgetEditSerial() == before + 2, "alternating art owners each pay one serial");
        TEST_ASSERT(effective_art(s) == 901, "owner A ends each round wearing its own art");
        settle(s);
    }

    TEST_ASSERT(UITree_WidgetReset(s->tree, sp, OWNER_A), "owner A releases the sprite");
    TEST_ASSERT(UITree_WidgetReset(s->tree, sp, OWNER_B), "owner B releases the sprite");
    settle(s);
}

/* ------------------------------------------------------------------ anchor */

static enum UITreeWidgetRelation
effective_anchor(struct IdemScene const* s, int32_t* out_target)
{
    return UITree_WidgetAnchorAt(s->tree, s->rect, out_target);
}

static void
test_idempotent_anchor(struct IdemScene* s)
{
    struct UITreeNodeRef const r = UITree_RefAt(s->tree, s->rect);
    struct UITreeNodeRef const t = UITree_RefAt(s->tree, s->target);
    struct UITreeNodeRef const root_ref = UITree_RefAt(s->tree, s->root);
    struct Quiet q;
    int32_t at = -1;

    TEST_ASSERT(
        UITree_WidgetSetAnchor(s->tree, r, OWNER_A, t, UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_OK,
        "owner A anchors the rect over the target");
    settle(s);
    TEST_ASSERT(effective_anchor(s, &at) == UITREE_WIDGET_RELATION_OVER && at == s->target, "the anchor is in force");

    /* (a) -- an anchor write is the expensive one: it bumps dirty_gen, which
     * is exactly the term the retained-emit gate compares. */
    q = quiet_capture(s);
    for( int i = 0; i < 4; ++i )
        TEST_ASSERT(
            UITree_WidgetSetAnchor(s->tree, r, OWNER_A, t, UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_OK,
            "an identical anchor still reports OK");
    assert_quiet(s, &q, s->rect, "identical anchor writes by the winner");
    TEST_ASSERT(effective_anchor(s, &at) == UITREE_WIDGET_RELATION_OVER && at == s->target, "the anchor is unchanged");

    /* (b) a different relation to the same target still applies. */
    q = quiet_capture(s);
    TEST_ASSERT(
        UITree_WidgetSetAnchor(s->tree, r, OWNER_A, t, UITREE_WIDGET_RELATION_BEHIND) == UITREE_WIDGET_ANCHOR_OK,
        "a changed relation applies");
    assert_wrote(s, &q, s->rect, "a changed anchor relation");
    TEST_ASSERT(s->tree->dirty_gen != q.dirty_gen, "an anchor change is a reachability change");
    settle(s);
    TEST_ASSERT(effective_anchor(s, &at) == UITREE_WIDGET_RELATION_BEHIND, "the changed relation took effect");

    /* The target is part of the value: a different widget, same relation. */
    {
        int32_t const other = UITree_TestPushXy(s->tree, s->root, UIELEM_RS_RECT, (792 << 16) | 9, 300, 10, 20, 20);
        settle(s);
        q = quiet_capture(s);
        TEST_ASSERT(
            UITree_WidgetSetAnchor(s->tree, r, OWNER_A, UITree_RefAt(s->tree, other),
                                   UITREE_WIDGET_RELATION_BEHIND) == UITREE_WIDGET_ANCHOR_OK,
            "a changed target applies");
        assert_wrote(s, &q, s->rect, "a changed anchor target");
        settle(s);
        TEST_ASSERT(effective_anchor(s, &at) == UITREE_WIDGET_RELATION_BEHIND && at == other,
                    "the changed target took effect");
    }

    /* A dead target replaced by a fresh widget is a DIFFERENT value even when
     * the index is reused: the ref carries an incarnation, and it is compared. */
    {
        int32_t const first = UITree_WidgetCreateText(s->tree, root_ref, OWNER_A, "idem-anchor-target", 0);
        struct UITreeNodeRef const first_ref = UITree_RefAt(s->tree, first);
        TEST_ASSERT(first >= 0, "an owned control to anchor to");
        TEST_ASSERT(
            UITree_WidgetSetAnchor(s->tree, r, OWNER_A, first_ref, UITREE_WIDGET_RELATION_OVER) ==
                UITREE_WIDGET_ANCHOR_OK,
            "the rect anchors over the owned control");
        settle(s);
        TEST_ASSERT(UITree_WidgetRemove(s->tree, first_ref, OWNER_A), "the owner removes the control");
        int32_t const second = UITree_WidgetCreateText(s->tree, root_ref, OWNER_A, "idem-anchor-target", 0);
        struct UITreeNodeRef const second_ref = UITree_RefAt(s->tree, second);
        TEST_ASSERT(second >= 0, "a fresh control replaces it");
        printf("WIDGET_IDEMPOTENT anchor target reuse: index %d -> %d, incarnation %llu -> %llu\n",
               first, second, (unsigned long long)first_ref.incarnation,
               (unsigned long long)second_ref.incarnation);
        settle(s);
        q = quiet_capture(s);
        TEST_ASSERT(
            UITree_WidgetSetAnchor(s->tree, r, OWNER_A, second_ref, UITREE_WIDGET_RELATION_OVER) ==
                UITREE_WIDGET_ANCHOR_OK,
            "the same relation to a fresh incarnation applies");
        assert_wrote(s, &q, s->rect, "an anchor to a fresh incarnation of a dead target");
        settle(s);
        TEST_ASSERT(effective_anchor(s, &at) == UITREE_WIDGET_RELATION_OVER && at == second,
                    "the fresh target took effect");
        TEST_ASSERT(UITree_WidgetRemove(s->tree, second_ref, OWNER_A), "the owner removes the control again");
        settle(s);
    }

    /* (c) */
    TEST_ASSERT(
        UITree_WidgetSetAnchor(s->tree, r, OWNER_A, t, UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_OK,
        "owner A anchors over the target");
    TEST_ASSERT(
        UITree_WidgetSetAnchor(s->tree, r, OWNER_B, t, UITREE_WIDGET_RELATION_BEHIND) == UITREE_WIDGET_ANCHOR_OK,
        "owner B outbids with BEHIND");
    settle(s);
    TEST_ASSERT(effective_anchor(s, &at) == UITREE_WIDGET_RELATION_BEHIND, "the later writer owns the relation");
    q = quiet_capture(s);
    TEST_ASSERT(
        UITree_WidgetSetAnchor(s->tree, r, OWNER_A, t, UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_OK,
        "owner A re-asserts the anchor it already stored");
    assert_wrote(s, &q, s->rect, "an identical anchor re-assert by a losing owner");
    settle(s);
    TEST_ASSERT(effective_anchor(s, &at) == UITREE_WIDGET_RELATION_OVER,
                "re-asserting an unchanged anchor takes the win back");

    /* (d) */
    for( int round = 0; round < 4; ++round )
    {
        uint64_t const before = UITree_WidgetEditSerial();
        TEST_ASSERT(
            UITree_WidgetSetAnchor(s->tree, r, OWNER_B, t, UITREE_WIDGET_RELATION_BEHIND) == UITREE_WIDGET_ANCHOR_OK,
            "owner B restates BEHIND");
        TEST_ASSERT(
            UITree_WidgetSetAnchor(s->tree, r, OWNER_A, t, UITREE_WIDGET_RELATION_OVER) == UITREE_WIDGET_ANCHOR_OK,
            "owner A restates OVER");
        TEST_ASSERT(UITree_WidgetEditSerial() == before + 2, "alternating anchor owners each pay one serial");
        TEST_ASSERT(effective_anchor(s, &at) == UITREE_WIDGET_RELATION_OVER, "owner A ends each round on top");
        settle(s);
    }
}

void
test_widget_idempotent_edits(void)
{
    struct IdemScene s;
    printf("TEST: retained widget edits are compare-then-set\n");
    scene_open(&s);
    test_idempotent_geometry(&s);
    test_idempotent_hidden(&s);
    test_idempotent_skin(&s);
    test_idempotent_anchor(&s);
    scene_close(&s);
}
