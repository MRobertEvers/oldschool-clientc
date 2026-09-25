/*
 * The reachable-children sidecar (UITreeComponent::visible_children) against
 * the sibling chain it stands in for.
 *
 * Every walk that consumes the sidecar used to enter each child and reject it
 * on the flags UITree_ChildHiddenForWalks folds, so the only thing the sidecar
 * may ever differ in is nothing: for every live node, UITree_VisibleChildren
 * must be exactly the sibling chain with those children left out, in chain
 * order, after any sequence of the mutations that maintain it -- creates
 * (append), the six reachability setters (reconcile in place or rebuild),
 * reparenting (unlink + append), clearing a container, cc_delete and
 * cc_deleteall (whose slots the next create recycles), and the tree clear.
 *
 * Two regimes matter and both are run: checking after EVERY step keeps every
 * list valid, so the incremental seams are what is being tested; checking
 * every few dozen steps lets lists go stale, so the lazy rebuild is.
 */
#include "test_harness.h"
#include "uitree.h"

#include <stdlib.h>
#include <string.h>

static uint32_t
vc_rand(uint32_t* s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

static int
vc_range(uint32_t* s, int lo, int hi)
{
    return lo + (int)(vc_rand(s) % (uint32_t)(hi - lo + 1));
}

static int
vc_parity(struct UITree* t)
{
    for( uint32_t i = 0; i < t->component_count; i++ )
    {
        int32_t n = 0;
        int32_t k = 0;
        int32_t const* v;
        if( t->components[i].freed )
            continue;
        v = UITree_VisibleChildren(t, (int32_t)i, &n);
        for( int32_t c = t->components[i].first_child; c >= 0; c = t->components[c].next_sibling )
        {
            if( UITree_ChildHiddenForWalks(&t->components[c]) )
                continue;
            if( k >= n || v[k] != c )
                return 0;
            k++;
        }
        if( k != n )
            return 0;
    }
    return 1;
}

static void
vc_run(uint32_t seed, int check_every, int steps)
{
    struct UITree* t = UITree_New(16);
    int32_t live[512];
    int live_count = 0;
    uint32_t rng = seed;
    int failures_before = g_failures;

    live[live_count++] = UITree_TestPushXy(t, -1, UIELEM_RS_LAYER, (700 << 16) | 0, 0, 0, 500, 300);
    for( int step = 0; step < steps; step++ )
    {
        int op = vc_range(&rng, 0, 99);
        int32_t node = live[vc_range(&rng, 0, live_count - 1)];
        if( t->components[node].freed )
            continue;
        if( op < 35 && live_count < 512 )
        {
            enum UITreeComponentType type =
                vc_range(&rng, 0, 2) == 0 ? UIELEM_RS_LAYER : (vc_range(&rng, 0, 1) ? UIELEM_RS_TEXT : UIELEM_RS_RECT);
            int32_t idx = UITree_TestPushXy(t, node, type, (700 << 16) | (live_count + 1), 0, 0, 20, 20);
            /* An IF3 node's script hide is unconditional, an IF1 node's is
             * hover-gated: the sidecar must tell them apart. Set at creation,
             * before any hide, the way CC_CREATE does. */
            t->components[idx].if3 = (uint8_t)vc_range(&rng, 0, 1);
            /* Half of them script-created, so cc_delete / cc_deleteall have
             * rows to remove and the slots they free are recycled by the
             * creates that follow -- the path that left stale entries. */
            t->components[idx].dynamic = (uint8_t)vc_range(&rng, 0, 1);
            live[live_count++] = idx;
        }
        else if( op < 60 )
            UITree_SetHideAt(t, node, vc_range(&rng, 0, 1));
        else if( op < 66 )
            UITree_SetFrameHiddenAt(t, node, vc_range(&rng, 0, 1));
        else if( op < 72 )
            UITree_SetScreenHiddenAt(t, node, vc_range(&rng, 0, 1));
        else if( op < 78 )
            UITree_SetMountHiddenAt(t, node, vc_range(&rng, 0, 1));
        else if( op < 92 )
        {
            int32_t target = vc_range(&rng, 0, 4) == 0 ? -1 : live[vc_range(&rng, 0, live_count - 1)];
            if( target < 0 || !t->components[target].freed )
                (void)UITree_Reparent(t, node, target);
        }
        else if( op < 94 )
            UITree_ClearChildren(t, node);
        else if( op < 96 )
            UITree_CcDelete(t, node);
        else if( op < 98 )
            UITree_CcDeleteAll(t, node);
        else
            (void)UITree_VisibleChildren(t, node, &(int32_t){ 0 });
        if( (step % check_every) == 0 && !vc_parity(t) )
        {
            TEST_ASSERT(0, "sidecar equals the filtered sibling chain after every mutation");
            break;
        }
    }
    if( g_failures == failures_before )
        TEST_ASSERT(vc_parity(t), "sidecar equals the filtered sibling chain at the end");
    UITree_Clear(t);
    TEST_ASSERT(vc_parity(t), "sidecar survives the tree clear");
    UITree_Free(t);
}

void
test_visible_children(void)
{
    printf("TEST: reachable-children sidecar parity\n");
    /* Every step checked: the incremental seams. */
    for( uint32_t seed = 1; seed <= 12; seed++ )
        vc_run(seed * 7919u, 1, 1500);
    /* Sparse checks: lists go stale and are rebuilt lazily. */
    for( uint32_t seed = 1; seed <= 12; seed++ )
        vc_run(seed * 104729u, 37, 1500);
}
