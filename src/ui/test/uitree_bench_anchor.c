/*
 * Microbenchmark for the frame anchor-reorder pass (UITree_FrameReorder),
 * which the perf audit found to be 83-88% of all frame work as soon as a
 * single plugin widget anchor exists.
 *
 * It builds a synthetic tree shaped like the rev-239 gameframe the audit
 * measured (n = 7202, ~677 reorder records per call, BENCH_ANCHORS anchored
 * units) and times:
 *
 *   A. `reference_reorder` -- the shipping pass as it stood at 82f3e3e24:
 *      O(records x nodes), eight mallocs per call.
 *   B. the current UITree_FrameReorder: a per-tree plan rebuilt only when an
 *      anchor or the topology moves, and a per-call pass that is
 *      O(records + units + children) with zero allocation.
 *
 * Both must write the same order; the bench asserts that before reporting, so
 * a speedup here is never bought with a behaviour change. The differential
 * test that proves it for arbitrary inputs is
 * ui/test/uitree_test_anchor_reorder.c.
 *
 *   make -C src bench-uitree-anchor
 *   make -C src bench-uitree-anchor BENCH_ANCHORS=40
 */
#include "uitree.h"
#include "uitree_anchor_reference.h"
#include "uitree_frame.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef BENCH_NODES
#define BENCH_NODES 7202
#endif
#ifndef BENCH_RECORDS
#define BENCH_RECORDS 677
#endif
#ifndef BENCH_ANCHORS
#define BENCH_ANCHORS 1
#endif
#ifndef BENCH_ITERS
#define BENCH_ITERS 200
#endif

/* The record shape the emit/input/hover passes hand the reorder: an opaque
 * row whose only known field is a node index + 1 at `node_offset`. */
struct BenchRecord
{
    int32_t node_plus_one;
    int32_t payload[7];
};

static double
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

static int32_t
push_node(struct UITree* tree, int32_t parent, int component_id)
{
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = component_id;
    spec.width = 32;
    spec.height = 32;
    return UITree_Push(tree, parent, &spec);
}

int
main(void)
{
    struct UITree* tree = UITree_New(BENCH_NODES + 64);
    int32_t* nodes = malloc((size_t)BENCH_NODES * sizeof(*nodes));
    struct BenchRecord* records = malloc((size_t)BENCH_RECORDS * sizeof(*records));
    struct BenchRecord* golden = malloc((size_t)BENCH_RECORDS * sizeof(*records));
    struct BenchRecord* work = malloc((size_t)BENCH_RECORDS * sizeof(*records));
    double t0, ms_reference, ms_current;
    unsigned long allocations_warm = 0, allocations_steady = 0, iterations = 0;
    int count_a = 0, count_b = 0;

    assert(nodes);
    assert(records);
    assert(golden);
    assert(work);

    /* A gameframe-ish tree: a root, wide containers, shallow-ish children. */
    int32_t const root = push_node(tree, -1, (161 << 16) | 0);
    for( int i = 0; i < BENCH_NODES; i++ )
    {
        int32_t const parent = i < 16 ? root : nodes[(i / 24) % (i ? i : 1)];
        nodes[i] = push_node(tree, parent, (161 << 16) | (i + 1));
    }
    for( int i = 0; i < BENCH_RECORDS; i++ )
    {
        memset(&records[i], 0, sizeof(records[i]));
        records[i].node_plus_one = nodes[(i * 7 + 3) % BENCH_NODES] + 1;
        records[i].payload[0] = i;
    }
    /* The anchors: BENCH_ANCHORS sources, each over a distinct target. */
    for( int a = 0; a < BENCH_ANCHORS; a++ )
    {
        int32_t const src = nodes[(a * 137 + 11) % BENCH_NODES];
        int32_t const dst = nodes[(a * 211 + 909) % BENCH_NODES];
        if( src == dst )
            continue;
        (void)UITree_WidgetSetAnchor(tree, UITree_RefAt(tree, src), 0x9001 + (uint64_t)a,
                                     UITree_RefAt(tree, dst), UITREE_WIDGET_RELATION_OVER);
    }
    printf("nodes=%u records=%d anchor_edits=%d iters=%d\n",
           tree->component_count, BENCH_RECORDS, UITree_WidgetAnchorCount(tree), BENCH_ITERS);

    /* A. the pass this replaced. */
    t0 = now_ms();
    for( int it = 0; it < BENCH_ITERS; it++ )
    {
        memcpy(work, records, (size_t)BENCH_RECORDS * sizeof(*records));
        count_a = reference_reorder(tree, NULL, work, BENCH_RECORDS, sizeof(*work),
                                    offsetof(struct BenchRecord, node_plus_one));
    }
    ms_reference = now_ms() - t0;
    memcpy(golden, work, (size_t)count_a * sizeof(*work));

    /* B. the current pass. The first call builds the plan and grows the
     * scratch; the timed run is the steady state a frame actually pays. */
    memcpy(work, records, (size_t)BENCH_RECORDS * sizeof(*records));
    (void)UITree_FrameReorder(tree, NULL, work, BENCH_RECORDS, sizeof(*work),
                              offsetof(struct BenchRecord, node_plus_one));
    UITree_FrameReorderStats(tree, &allocations_warm, &iterations);
    t0 = now_ms();
    for( int it = 0; it < BENCH_ITERS; it++ )
    {
        memcpy(work, records, (size_t)BENCH_RECORDS * sizeof(*records));
        count_b = UITree_FrameReorder(tree, NULL, work, BENCH_RECORDS, sizeof(*work),
                                      offsetof(struct BenchRecord, node_plus_one));
    }
    ms_current = now_ms() - t0;
    UITree_FrameReorderStats(tree, &allocations_steady, &iterations);

    printf("reference (82f3e3e24)  : %8.4f ms/call  (%d records out)\n",
           ms_reference / BENCH_ITERS, count_a);
    printf("current   FrameReorder : %8.4f ms/call  (%d records out)\n",
           ms_current / BENCH_ITERS, count_b);
    printf("speedup   %.1fx   loop steps/call %lu   mallocs/call: reference 8, current %lu\n",
           ms_current > 0.0 ? ms_reference / ms_current : 0.0, iterations,
           allocations_steady - allocations_warm);

    if( count_a == count_b && memcmp(golden, work, (size_t)count_a * sizeof(*work)) == 0 )
        printf("ORDER MATCHES\n");
    else
    {
        printf("ORDER DIFFERS (count %d vs %d) -- the speedup is not a speedup\n", count_a, count_b);
        return 1;
    }
    free(nodes);
    free(records);
    free(golden);
    free(work);
    UITree_Free(tree);
    return 0;
}
