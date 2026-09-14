/*
 * The anchor reorder, against the implementation it replaced.
 *
 * UITree_FrameReorder is the one ordering every paint and input consumer goes
 * through, so the rewrite that made it linear and allocation-free has exactly
 * one acceptance test worth having: for every input, does it emit the same
 * record sequence, byte for byte, as the O(records x nodes) pass did?
 *
 * `reference_reorder` -- ui/test/uitree_anchor_reference.h -- IS that pass,
 * copied verbatim from ui/uitree_frame.c before the rewrite. The bench
 * (make -C src bench-uitree-anchor) times the same two.
 */
#include "test_harness.h"
#include "uitree_anchor_reference.h"
#include "uitree_frame.h"

#include <stdint.h>
#include <stdlib.h>

/* The record shape the emit/input/hover passes hand the reorder: an opaque row
 * whose only known field is a node index + 1 at `node_offset`. `tag` is the
 * identity the comparison reads -- two records on the same node must still come
 * out in the same relative order. */
struct ReorderRecord
{
    int32_t node_plus_one;
    int32_t tag;
    int32_t pad[2];
};

/* ------------------------------------------------------------------ */
/* Random scenes                                                       */
/* ------------------------------------------------------------------ */

static uint32_t g_rng;

static uint32_t
rng_next(void)
{
    /* xorshift32: reproducible everywhere, which is the only property a seeded
     * differential test needs from it. */
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static int
rng_below(int bound)
{
    assert(bound > 0);
    return (int)(rng_next() % (uint32_t)bound);
}

struct ReorderScene
{
    struct UITree* tree;
    struct UITreeHost host;
    struct TestHostState state;
    int32_t* nodes;
    int node_count;
    struct ReorderRecord* records;
    int record_count;
};

static int32_t
scene_push(struct ReorderScene* scene, int32_t parent)
{
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = (971 << 16) | (scene->node_count + 1);
    spec.width = 16;
    spec.height = 16;
    return UITree_Push(scene->tree, parent, &spec);
}

/*
 * One random tree: a spine of containers with children hung off earlier nodes,
 * a record list that mostly names live nodes, and up to `anchors` relations
 * among them. Rejections from UITree_WidgetSetAnchor (self, ancestor,
 * descendant, cycle) are expected and simply leave that anchor unstated.
 */
static void
scene_build(struct ReorderScene* scene, int node_count, int record_count, int anchors,
            int replace_share, int hidden)
{
    memset(scene, 0, sizeof(*scene));
    UITree_TestHostInit(&scene->host, &scene->state);
    scene->tree = UITree_New(node_count + 8);
    scene->nodes = malloc((size_t)node_count * sizeof(*scene->nodes));
    assert(scene->nodes);
    scene->records = malloc((size_t)record_count * sizeof(*scene->records));
    assert(scene->records);

    scene->nodes[0] = scene_push(scene, -1);
    scene->node_count = 1;
    for( int i = 1; i < node_count; i++ )
    {
        /* A mix of shallow and deep: most nodes hang off one of the last few,
         * which is what makes a memo worth having. */
        int const pick = rng_below(4) == 0 ? rng_below(scene->node_count)
                                           : scene->node_count - 1 - rng_below(scene->node_count < 8 ? scene->node_count : 8);
        scene->nodes[scene->node_count] = scene_push(scene, scene->nodes[pick < 0 ? 0 : pick]);
        scene->node_count++;
    }
    for( int a = 0; a < anchors; a++ )
    {
        int32_t const src = scene->nodes[rng_below(scene->node_count)];
        int32_t const dst = scene->nodes[rng_below(scene->node_count)];
        int const roll = rng_below(100);
        enum UITreeWidgetRelation rel = roll < replace_share      ? UITREE_WIDGET_RELATION_REPLACE
                                        : (roll & 1)              ? UITREE_WIDGET_RELATION_OVER
                                                                  : UITREE_WIDGET_RELATION_BEHIND;
        (void)UITree_WidgetSetAnchor(scene->tree, UITree_RefAt(scene->tree, src), 0x4200 + (uint64_t)a,
                                     UITree_RefAt(scene->tree, dst), rel);
    }
    for( int h = 0; h < hidden; h++ )
        UITree_SetHideAt(scene->tree, scene->nodes[rng_below(scene->node_count)], 1);

    scene->record_count = record_count;
    for( int i = 0; i < record_count; i++ )
    {
        memset(&scene->records[i], 0, sizeof(scene->records[i]));
        /* One in sixteen names no node at all -- the "unanchored paint" row the
         * reorder must leave exactly where it found it. */
        scene->records[i].node_plus_one =
            rng_below(16) == 0 ? 0 : scene->nodes[rng_below(scene->node_count)] + 1;
        scene->records[i].tag = i;
    }
}

static void
scene_free(struct ReorderScene* scene)
{
    UITree_Free(scene->tree);
    free(scene->nodes);
    free(scene->records);
}

/* Proof the sweep is not vacuous: a random tree whose anchors were all rejected
 * for containment would compare two no-ops and pass. */
static struct
{
    int scenes;
    int anchored_scenes;
    int reordered;   /* the output sequence differs from the input */
    int dropped;     /* a presented REPLACE removed the target's records */
    int anchor_edits;
} g_coverage;

/* Run both passes over a copy of the scene's records and compare. */
static int
scene_compare(struct ReorderScene* scene, char const* what, int case_index)
{
    size_t const bytes = (size_t)scene->record_count * sizeof(*scene->records);
    struct ReorderRecord* mine = malloc(bytes ? bytes : 1);
    struct ReorderRecord* theirs = malloc(bytes ? bytes : 1);
    int count_new, count_ref, ok;

    assert(mine);
    assert(theirs);
    memcpy(mine, scene->records, bytes);
    memcpy(theirs, scene->records, bytes);
    count_ref = reference_reorder(scene->tree, &scene->host, theirs, scene->record_count,
                                  sizeof(*theirs), offsetof(struct ReorderRecord, node_plus_one));
    count_new = UITree_FrameReorder(scene->tree, &scene->host, mine, scene->record_count,
                                    sizeof(*mine), offsetof(struct ReorderRecord, node_plus_one));
    ok = count_new == count_ref &&
         memcmp(mine, theirs, (size_t)(count_ref > 0 ? count_ref : 0) * sizeof(*mine)) == 0;
    g_coverage.scenes++;
    g_coverage.anchor_edits += UITree_WidgetAnchorCount(scene->tree);
    if( UITree_WidgetAnchorCount(scene->tree) > 0 )
        g_coverage.anchored_scenes++;
    if( count_ref < scene->record_count )
        g_coverage.dropped++;
    else if( memcmp(theirs, scene->records, bytes) != 0 )
        g_coverage.reordered++;
    if( !ok )
    {
        int shown = 0;
        fprintf(stderr, "FAIL: %s case %d (nodes=%d records=%d anchors=%d): "
                        "count new=%d ref=%d\n",
                what, case_index, scene->node_count, scene->record_count,
                UITree_WidgetAnchorCount(scene->tree), count_new, count_ref);
        for( int i = 0; i < count_ref && i < count_new && shown < 8; i++ )
            if( mine[i].tag != theirs[i].tag )
            {
                fprintf(stderr, "      at %d: new tag %d (node %d), ref tag %d (node %d)\n", i,
                        mine[i].tag, mine[i].node_plus_one - 1,
                        theirs[i].tag, theirs[i].node_plus_one - 1);
                shown++;
            }
        g_failures++;
    }
    free(mine);
    free(theirs);
    return ok;
}

/*
 * The differential sweep: many seeded random scenes, every anchor mix, every
 * shape the pass distinguishes.
 */
static void
test_anchor_reorder_matches_reference(void)
{
    int const cases = 240;
    int passed = 0;

    g_rng = 0x5eed1234u;
    for( int c = 0; c < cases; c++ )
    {
        struct ReorderScene scene;
        int const nodes = 50 + rng_below(2951);
        int const records = 1 + rng_below(nodes < 900 ? nodes : 900);
        int const anchors = rng_below(41);
        /* Sweep the REPLACE share so both extremes -- none, and a tree that is
         * almost all replacements -- are covered. */
        int const replace_share = (c % 4) * 33;
        int const hidden = c % 3 == 0 ? 1 + rng_below(6) : 0;
        scene_build(&scene, nodes, records, anchors, replace_share, hidden);
        passed += scene_compare(&scene, "reorder differs from the reference", c);
        scene_free(&scene);
    }
    TEST_ASSERT(passed == cases, "the linear reorder writes the reference's record sequence");
    printf("ANCHOR_REORDER differential: %d/%d random scenes identical "
           "(%d with anchors, %d edits, %d reordered, %d with records dropped by REPLACE)\n",
           passed, cases, g_coverage.anchored_scenes, g_coverage.anchor_edits,
           g_coverage.reordered, g_coverage.dropped);
    /* If any of these ever reads zero the sweep has stopped testing something. */
    TEST_ASSERT(g_coverage.anchored_scenes > cases / 2, "most random scenes state a live anchor");
    TEST_ASSERT(g_coverage.reordered > 0, "some scenes actually change the record order");
    TEST_ASSERT(g_coverage.dropped > 0, "some scenes drop a replaced target's records");
}

/*
 * Nodes created AFTER the plan was built.
 *
 * This is the one thing a cached node -> unit memo can get wrong and nothing
 * else would notice: the new nodes hang under an existing unit, so they must
 * resolve to that unit, not to "no unit".
 */
static void
test_anchor_reorder_survives_a_generation_change(void)
{
    int const rounds = 30;
    int passed = 0;

    g_rng = 0xa11ce99u;
    for( int r = 0; r < rounds; r++ )
    {
        struct ReorderScene scene;
        int const nodes = 80 + rng_below(400);
        scene_build(&scene, nodes, 1 + rng_below(200), 1 + rng_below(12), (r % 3) * 40, r % 2);
        /* Build the plan once. */
        passed += scene_compare(&scene, "generation-change warmup", r);
        /* Now push under nodes the plan has never seen, and give them records. */
        {
            int const added = 1 + rng_below(40);
            int const grown = scene.record_count + added;
            struct ReorderRecord* grown_records = realloc(scene.records, (size_t)grown * sizeof(*grown_records));
            assert(grown_records);
            scene.records = grown_records;
            for( int i = 0; i < added; i++ )
            {
                int32_t const parent = scene.nodes[rng_below(scene.node_count)];
                int32_t const pushed = scene_push(&scene, parent);
                int const slot = scene.record_count + i;
                memset(&scene.records[slot], 0, sizeof(scene.records[slot]));
                scene.records[slot].node_plus_one = pushed + 1;
                scene.records[slot].tag = 10000 + slot;
            }
            scene.record_count = grown;
        }
        passed += scene_compare(&scene, "reorder after a generation change", r);
        /* And once more with the plan warm on the grown tree. */
        passed += scene_compare(&scene, "reorder with a warm plan", r);
        scene_free(&scene);
    }
    TEST_ASSERT(passed == rounds * 3,
                "nodes pushed after the plan was built resolve to the unit above them");
    printf("ANCHOR_REORDER generation change: %d/%d reorders identical\n", passed, rounds * 3);
}

/*
 * Anchors moved after the plan was built: the unit list itself has to be the
 * thing that goes stale, not just the memo.
 */
static void
test_anchor_reorder_follows_a_moved_anchor(void)
{
    struct ReorderScene scene;
    int passed = 0;

    g_rng = 0xbeef77u;
    scene_build(&scene, 300, 200, 6, 33, 2);
    passed += scene_compare(&scene, "moved-anchor warmup", 0);
    for( int step = 0; step < 20; step++ )
    {
        int32_t const src = scene.nodes[rng_below(scene.node_count)];
        int32_t const dst = scene.nodes[rng_below(scene.node_count)];
        int const roll = rng_below(4);
        (void)UITree_WidgetSetAnchor(scene.tree, UITree_RefAt(scene.tree, src), 0x7700 + (uint64_t)step,
                                     UITree_RefAt(scene.tree, dst),
                                     roll == 0   ? UITREE_WIDGET_RELATION_REPLACE
                                     : roll == 1 ? UITREE_WIDGET_RELATION_BEHIND
                                     : roll == 2 ? UITREE_WIDGET_RELATION_OVER
                                                 : UITREE_WIDGET_RELATION_NATIVE);
        passed += scene_compare(&scene, "reorder after an anchor moved", step);
    }
    /* Dropping every edit puts the records back in native order. */
    for( int i = 0; i < scene.node_count; i++ )
        for( int owner = 0; owner < 26; owner++ )
            (void)UITree_WidgetReset(scene.tree, UITree_RefAt(scene.tree, scene.nodes[i]),
                                     owner < 6 ? 0x4200 + (uint64_t)owner : 0x7700 + (uint64_t)(owner - 6));
    TEST_ASSERT(UITree_WidgetAnchorCount(scene.tree) == 0, "every anchor edit was reset");
    passed += scene_compare(&scene, "reorder with every anchor reset", 0);
    TEST_ASSERT(passed == 22, "a rebuilt unit list still writes the reference's sequence");
    scene_free(&scene);
    printf("ANCHOR_REORDER moved anchors: %d/22 reorders identical\n", passed);
}

/*
 * The cost claim, on the shape the audit measured: a rev-239-sized gameframe
 * with one anchored widget. Zero heap allocation per call once the scratch has
 * been grown, and a loop-step count that is linear in nodes + records.
 */
static void
test_anchor_reorder_is_linear_and_allocation_free(void)
{
    struct ReorderScene scene;
    unsigned long allocations_first = 0, allocations_after = 0;
    unsigned long iterations = 0, unused = 0;
    size_t const bytes = 677 * sizeof(struct ReorderRecord);
    struct ReorderRecord* work = malloc(bytes);
    int const budget = 4 * (7202 + 677);

    assert(work);
    g_rng = 0xc0ffeeu;
    scene_build(&scene, 7202, 677, 1, 0, 0);
    TEST_ASSERT(UITree_WidgetAnchorCount(scene.tree) == 1, "the measured shape has one anchor edit");

    memcpy(work, scene.records, bytes);
    (void)UITree_FrameReorder(scene.tree, &scene.host, work, scene.record_count, sizeof(*work),
                              offsetof(struct ReorderRecord, node_plus_one));
    UITree_FrameReorderStats(scene.tree, &allocations_first, &unused);
    for( int call = 0; call < 8; call++ )
    {
        memcpy(work, scene.records, bytes);
        (void)UITree_FrameReorder(scene.tree, &scene.host, work, scene.record_count, sizeof(*work),
                                  offsetof(struct ReorderRecord, node_plus_one));
    }
    UITree_FrameReorderStats(scene.tree, &allocations_after, &iterations);
    printf("ANCHOR_REORDER measured shape: nodes=%d records=%d iterations=%lu budget=%d "
           "allocations warm=%lu steady=%lu\n",
           scene.node_count, scene.record_count, iterations, budget,
           allocations_first, allocations_after);
    TEST_ASSERT(allocations_after == allocations_first,
                "a frame's eight reorder calls allocate nothing");
    TEST_ASSERT(iterations < (unsigned long)budget,
                "the pass is linear in nodes + records on the measured shape");
    free(work);
    scene_free(&scene);
}

void
test_anchor_reorder(void)
{
    printf("TEST: anchor reorder against the pass it replaced\n");
    test_anchor_reorder_matches_reference();
    test_anchor_reorder_survives_a_generation_change();
    test_anchor_reorder_follows_a_moved_anchor();
    test_anchor_reorder_is_linear_and_allocation_free();
}
