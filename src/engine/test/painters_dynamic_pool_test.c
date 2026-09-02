/**
 * The painter's per-cycle dynamic pass, held to two claims.
 *
 * 1. The scenery pool does not grow. world_cycle re-registers every actor,
 *    projectile and graphic each cycle, and painter_reset_to_static unlinks
 *    their chain nodes -- but until the static high-water was recorded it never
 *    gave the nodes back, so scenery_pool_count climbed by the dynamic count
 *    every frame (measured on this harness before the fix: 212 after the first
 *    frame, 108,032 after 600, capacity 131,072).
 *
 * 2. The journal (TORIRS_PAINTER_DYN_SKIP) is invisible to the paint. A cycle
 *    whose registrations equal the previous cycle's is skipped; the command
 *    stream it paints must be byte-identical to the one a rebuild produces,
 *    and any difference -- an entity moved, one added, a static loc released,
 *    a re-mark -- must force the rebuild.
 *
 * Build/run: make -C src test-painters-dynamic-pool
 */
#include "painters/painters.h"
#include "painters_i.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

static void
expect(
    int cond,
    const char* msg)
{
    if( !cond )
    {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_failures++;
    }
    else
    {
        printf("  ok   %s\n", msg);
    }
}

#define SCENE 32
#define LEVELS 4
#define DYNAMICS 20

static struct Painter*
make_painter(void)
{
    struct Painter* p = painter_new(SCENE, SCENE, LEVELS, PAINTER_NEW_CTX_BUCKET);
    assert(p);
    painter_set_draw_distance(p, 25);
    for( int i = 0; i < 8; i++ )
        painter_add_normal_scenery(p, 2 + i * 3, 4, 0, 100 + i, 2, 2, 100);
    painter_add_wall(p, 20, 20, 0, 300, WALL_A, 0);
    painter_mark_static_count(p);
    return p;
}

/* One cycle's worth of registrations; `shift` moves every mover by a tile. */
static void
register_dynamics(
    struct Painter* p,
    int shift)
{
    for( int i = 0; i < DYNAMICS; i++ )
        painter_add_normal_scenery(p, 1 + i + shift, 10 + (i & 3), 0, 1000 + i, 3, 3, 200);
    painter_add_wall(p, 5, 5, 0, 2000, WALL_A, 0);
    painter_add_ground_decor_dynamic(p, 6, 6, 0, 2001);
}

static int
paint_into(
    struct Painter* p,
    struct PaintersBuffer* buf)
{
    buf->command_count = 0;
    return painter_paint_bucket(p, buf, 16, 16, 0);
}

static int
buffers_equal(
    const struct PaintersBuffer* a,
    const struct PaintersBuffer* b)
{
    if( a->command_count != b->command_count )
        return 0;
    return memcmp(
               a->commands,
               b->commands,
               (size_t)a->command_count * sizeof(struct PaintersElementCommand)) == 0;
}

static void
test_pool_does_not_grow(void)
{
    struct Painter* p = make_painter();
    int static_nodes = p->scenery_pool_count;
    int first = -1;
    int capacity_first = -1;

    printf("the scenery pool is flat across frames (reset_to_static)\n");
    for( int f = 0; f < 600; f++ )
    {
        painter_reset_to_static(p);
        register_dynamics(p, 0);
        if( f == 0 )
        {
            first = p->scenery_pool_count;
            capacity_first = p->scenery_pool_capacity;
        }
    }
    printf("       static=%d frame0=%d frame599=%d capacity=%d\n",
           static_nodes, first, p->scenery_pool_count, p->scenery_pool_capacity);
    expect(static_nodes == p->static_scenery_pool_count, "the mark recorded the static high-water");
    expect(first > static_nodes, "a frame's dynamics do append nodes");
    expect(p->scenery_pool_count == first, "600 frames later the count is what it was after frame 0");
    expect(p->scenery_pool_capacity == capacity_first, "and the pool never reallocated");
    painter_reset_to_static(p);
    expect(p->scenery_pool_count == static_nodes, "a reset hands every dynamic node back");
    painter_free(p);
}

static void
test_journal_is_invisible_to_the_paint(void)
{
    struct Painter* p = make_painter();
    struct PaintersBuffer* rebuilt = painter_buffer_new();
    struct PaintersBuffer* skipped = painter_buffer_new();
    int static_nodes = p->scenery_pool_count;
    int rebuilt_count;
    int r;

    printf("the journal skips identical cycles and paints the same stream\n");
    painter_set_dynamics_skip(p, 1);

    painter_dynamics_begin(p);
    register_dynamics(p, 0);
    r = painter_dynamics_commit(p);
    expect(r == 1, "the first cycle has nothing to compare against and rebuilds");
    expect(p->element_count > p->static_element_count, "and the dynamics are registered");
    rebuilt_count = p->element_count;
    paint_into(p, rebuilt);
    expect(rebuilt->command_count > 0, "the rebuilt registration paints something");

    painter_dynamics_begin(p);
    register_dynamics(p, 0);
    r = painter_dynamics_commit(p);
    expect(r == 0, "an identical cycle is skipped");
    expect(p->dyn_skipped_count == 1, "and counted");
    expect(p->element_count == rebuilt_count, "the elements from the previous cycle are still registered");
    paint_into(p, skipped);
    expect(buffers_equal(rebuilt, skipped), "a skipped cycle paints the identical command stream");

    for( int f = 0; f < 600; f++ )
    {
        painter_dynamics_begin(p);
        register_dynamics(p, 0);
        painter_dynamics_commit(p);
    }
    expect(p->scenery_pool_count == static_nodes + (DYNAMICS * 9 + 0),
           "600 skipped cycles leave the pool at static + one cycle of dynamics");

    painter_dynamics_begin(p);
    register_dynamics(p, 1);
    r = painter_dynamics_commit(p);
    expect(r == 1, "moving every actor a tile forces a rebuild");
    paint_into(p, skipped);
    expect(!buffers_equal(rebuilt, skipped), "and the paint changed with it");

    /* Rebuild on demand must equal a from-scratch registration of the same
     * list: reset explicitly, register directly, compare. */
    {
        struct PaintersBuffer* direct = painter_buffer_new();
        painter_set_dynamics_skip(p, 0);
        painter_dynamics_begin(p); /* control arm: this resets */
        register_dynamics(p, 1);
        painter_dynamics_commit(p);
        paint_into(p, direct);
        expect(buffers_equal(direct, skipped), "the replayed registration paints like a direct one");
        painter_set_dynamics_skip(p, 1);
        free(direct->commands);
        free(direct);
    }

    painter_dynamics_begin(p);
    register_dynamics(p, 1);
    r = painter_dynamics_commit(p);
    expect(r == 1, "the first journaled cycle after the control arm rebuilds (no previous list)");
    painter_dynamics_begin(p);
    register_dynamics(p, 1);
    r = painter_dynamics_commit(p);
    expect(r == 0, "and the next identical one skips again");

    painter_dynamics_begin(p);
    register_dynamics(p, 1);
    painter_add_normal_scenery(p, 30, 30, 0, 5000, 1, 1, 50);
    r = painter_dynamics_commit(p);
    expect(r == 1, "one more registration forces a rebuild");

    painter_release_scenery(p, 2, 4, 0, 100);
    painter_dynamics_begin(p);
    register_dynamics(p, 1);
    painter_add_normal_scenery(p, 30, 30, 0, 5000, 1, 1, 50);
    r = painter_dynamics_commit(p);
    expect(r == 1, "a static loc released between cycles forces a rebuild");

    painter_dynamics_begin(p);
    register_dynamics(p, 1);
    painter_add_normal_scenery(p, 30, 30, 0, 5000, 1, 1, 50);
    r = painter_dynamics_commit(p);
    expect(r == 0, "then it settles again");

    painter_mark_static_count(p);
    painter_dynamics_begin(p);
    register_dynamics(p, 1);
    painter_add_normal_scenery(p, 30, 30, 0, 5000, 1, 1, 50);
    r = painter_dynamics_commit(p);
    expect(r == 1, "a re-mark of the static set forces a rebuild");

    painter_dynamics_begin(p);
    r = painter_dynamics_commit(p);
    expect(r == 1, "an empty cycle after a full one rebuilds (to nothing)");
    expect(p->element_count == p->static_element_count, "and leaves only the statics");
    painter_dynamics_begin(p);
    r = painter_dynamics_commit(p);
    expect(r == 0, "two empty cycles in a row skip");

    free(rebuilt->commands);
    free(rebuilt);
    free(skipped->commands);
    free(skipped);
    painter_free(p);
}

int
main(void)
{
    test_pool_does_not_grow();
    test_journal_is_invisible_to_the_paint();
    if( g_failures )
    {
        fprintf(stderr, "FAILED: painters_dynamic_pool_test (%d)\n", g_failures);
        return 1;
    }
    printf("OK: painters_dynamic_pool_test\n");
    return 0;
}
