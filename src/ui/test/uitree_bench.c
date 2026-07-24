/* Microbenchmark for the widget-rebuild burst: cc_deleteall followed by a run of
 * cc_create, which is what a CS2 transmit script does to repopulate a container
 * (bank, spellbook, inventory-like lists). Steady-state frames barely touch this
 * path — the per-tick rebuilds are gone — but a rebuild of a few hundred rows is
 * still the worst thing the tree is asked to do, and it is where node size,
 * UITree_FindChildBySubid and the id index all show up.
 *
 *   make -C src bench-uitree
 *   make -C src bench-uitree BENCH_ROWS=800 BENCH_ITERS=40
 */
#include "uitree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef BENCH_ROWS
#define BENCH_ROWS 400
#endif
#ifndef BENCH_ITERS
#define BENCH_ITERS 20
#endif
#ifndef BENCH_STATIC_CHILDREN
#define BENCH_STATIC_CHILDREN 20
#endif

static double
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

static int32_t
push_xy(
    struct UITree* tree,
    int32_t parent,
    enum UITreeComponentType type,
    int component_id,
    int y)
{
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = type;
    spec.component_id = component_id;
    spec.y = y;
    spec.width = 32;
    spec.height = 32;
    return UITree_Push(tree, parent, &spec);
}

int
main(void)
{
    int const iface = 161;
    int const parent_id = (iface << 16) | 10;
    struct UITree* tree = UITree_New(64);
    double t0, elapsed;
    long created = 0;

    if( !tree )
        return 1;

    int32_t const parent = push_xy(tree, -1, UIELEM_RS_LAYER, parent_id, 0);
    for( int i = 0; i < BENCH_STATIC_CHILDREN; i++ )
        push_xy(tree, parent, UIELEM_RS_RECT, (iface << 16) | (100 + i), i * 32);

    t0 = now_ms();
    for( int iter = 0; iter < BENCH_ITERS; iter++ )
    {
        UITree_CcDeleteAll(tree, parent);
        for( int row = 0; row < BENCH_ROWS; row++ )
        {
            int32_t const idx = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_RECT, row);
            if( idx < 0 )
            {
                fprintf(stderr, "bench: cc_create failed at row %d\n", row);
                return 1;
            }
            /* A real script follows every create with geometry writes that
             * resolve the widget by id — the lookup path, not just the push. */
            int const uid = tree->components[idx].component_id;
            UITree_ApplyPosition(tree, uid, 0, row * 32);
            UITree_ApplySize(tree, uid, 32, 32);
            created++;
        }
    }
    elapsed = now_ms() - t0;

    printf(
        "uitree bench: %d rebuilds x %d rows (%d static children)\n"
        "  component struct  %zu bytes\n"
        "  live components   %u (capacity %u, %.1f MB)\n"
        "  total             %.1f ms\n"
        "  per rebuild       %.2f ms\n"
        "  per cc_create     %.1f us\n",
        (int)BENCH_ITERS,
        (int)BENCH_ROWS,
        (int)BENCH_STATIC_CHILDREN,
        sizeof(struct UITreeComponent),
        tree->component_count,
        tree->component_capacity,
        (double)tree->component_capacity * sizeof(struct UITreeComponent) / (1024.0 * 1024.0),
        elapsed,
        elapsed / (double)BENCH_ITERS,
        elapsed * 1000.0 / (double)created);

    UITree_Free(tree);
    return 0;
}
