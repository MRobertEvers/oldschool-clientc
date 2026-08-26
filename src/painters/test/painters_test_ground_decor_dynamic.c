/**
 * Unit tests for RUNTIME-spawned ground decor — a shape-22 loc placed by a zone
 * LOC_ADD_CHANGE rather than baked into the static set.
 *
 * The build path registers such a loc with painter_add_ground_decor, which
 * claims the tile's exclusive decor slot. A runtime spawn cannot: the painter's
 * static set is frozen at painter_mark_static_count and painter_reset_to_static
 * truncates everything after it, so the loc has to be re-registered by
 * world_cycle every frame. It used to be re-registered as ordinary *scenery*,
 * and that is a visible defect rather than a tidiness one:
 *
 *   ground decor is emitted in a tile's BASE step, ahead of every scenery
 *   element whose footprint covers that tile;
 *   scenery is emitted after the base step, ordered against the other scenery
 *   on the tile.
 *
 * The tile that loses is the mover's NEAREST footprint corner — the tile whose
 * base step is the last one he was waiting on, so he and the puddle are ready
 * in the same visit and the chain sort (farthest corner first) puts the 5x5
 * ahead of the 1x1. Every other tile under him already emitted its scenery
 * earlier. One tile in twenty-five, and it is the one directly under the near
 * edge of a boss the player is standing beside.
 *
 * SCOPE, so this is not read as a cure for every overlap: a puddle OUTSIDE the
 * mover's painter footprint but nearer the camera still draws over him, decor
 * or not. That is the reference painter — the footprint is his tile span
 * (World.addDynamic's padded 5x5), his MODEL overhangs it, and no per-tile
 * painter can order geometry it does not know overlaps. Fixing that would mean
 * departing from the reference's traversal, not fixing a bug in it.
 *
 * Build/run: make -C src test-painters-ground-decor
 */
#include "painters/painters.h"

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

#define SCENE 16
#define LEVELS 4

static struct Painter*
make_painter(void)
{
    struct Painter* p = painter_new(SCENE, SCENE, LEVELS, PAINTER_NEW_CTX_BUCKET);
    assert(p && "painter_new");
    painter_set_draw_distance(p, SCENE);
    return p;
}

static struct PaintersBuffer*
make_buffer(void)
{
    struct PaintersBuffer* buf = painter_buffer_new();
    assert(buf && "painter_buffer_new");
    return buf;
}

static void
free_buffer(struct PaintersBuffer* buf)
{
    free(buf->commands);
    free(buf);
}

/* Count entity commands naming `entity`, and where the first landed. */
static int
entity_emits(
    struct PaintersBuffer* buf,
    int entity,
    int* first_index)
{
    int n = 0;
    int i;
    if( first_index )
        *first_index = -1;
    for( i = 0; i < buf->command_count; i++ )
    {
        struct PaintersElementCommand* c = &buf->commands[i];
        if( c->_bf_kind != PNTR_CMD_ELEMENT )
            continue;
        if( painter_command_element_id(c) != entity )
            continue;
        if( first_index && *first_index < 0 )
            *first_index = i;
        n++;
    }
    return n;
}

/* A 5x5 mover at (5,5)..(9,9) with one puddle on the tile named, drawn from a
 * camera at the scene's south-west corner. Returns 1 when the puddle is
 * emitted before the mover (drawn under him), 0 when after (over him). */
static int
puddle_draws_under(
    int px,
    int pz,
    int as_decor)
{
    struct Painter* p = make_painter();
    struct PaintersBuffer* buf = make_buffer();
    const int mover = 900;
    const int puddle = 902;
    int i_mover = -1, i_puddle = -1;
    int under;

    painter_mark_static_count(p);
    painter_reset_to_static(p);

    /* world_cycle's per-frame order: runtime locs first, then the movers. */
    if( as_decor )
        painter_add_ground_decor_dynamic(p, px, pz, 0, puddle);
    else
        painter_add_normal_scenery(p, px, pz, 0, puddle, 1, 1, 0);
    painter_add_normal_scenery(p, 5, 5, 0, mover, 5, 5, 0);

    painter_paint_bucket(p, buf, 0, 0, 0);

    entity_emits(buf, mover, &i_mover);
    entity_emits(buf, puddle, &i_puddle);
    under = (i_mover >= 0 && i_puddle >= 0 && i_puddle < i_mover);

    free_buffer(buf);
    painter_free(p);
    return under;
}

/*
 * THE DEFECT AND ITS FIX, as one before/after pair on the tile that loses.
 *
 * (5,5) is the mover's south-west corner and the camera is south-west of the
 * scene, so it is the last footprint tile he waits on: he and a scenery puddle
 * there come ready in the same visit, and the chain sorts the 5x5 first. As
 * decor the puddle goes out in that tile's base step, which the mover is by
 * construction waiting for.
 *
 * The scenery half is what makes this a proof rather than a restatement — it
 * is the exact registration world_cycle used before, and it fails.
 */
static void
test_dynamic_decor_draws_under_a_mover_standing_on_it(void)
{
    printf("a runtime puddle on the mover's near corner draws under him\n");

    expect(puddle_draws_under(5, 5, /*as_decor=*/0) == 0,
           "as SCENERY the near-corner puddle draws OVER the mover (the defect)");
    expect(puddle_draws_under(5, 5, /*as_decor=*/1) == 1,
           "as DECOR it draws under him");
}

/*
 * The rest of the footprint was never broken, and says so.
 *
 * Every other tile under the mover emits its scenery in an earlier visit than
 * the one that finally releases him, so a puddle there already drew first. The
 * decor registration must not regress them, and a reader comparing this file
 * against the screenshot that prompted it should be able to see that the fix
 * covers one tile, not twenty-five.
 */
static void
test_the_rest_of_the_footprint_was_already_under(void)
{
    printf("the mover's other footprint tiles are unaffected either way\n");

    expect(puddle_draws_under(7, 7, 0) == 1 && puddle_draws_under(7, 7, 1) == 1,
           "centre tile: under him as scenery and as decor");
    expect(puddle_draws_under(9, 9, 0) == 1 && puddle_draws_under(9, 9, 1) == 1,
           "far corner: under him as scenery and as decor");
    expect(puddle_draws_under(9, 5, 0) == 1 && puddle_draws_under(9, 5, 1) == 1,
           "side tile: under him as scenery and as decor");
}

/*
 * And the scope limit, pinned so nobody reads the fix as more than it is.
 *
 * A tile OUTSIDE the mover's footprint but nearer the camera is visited after
 * he has been emitted, so its decor lands on top of him. That is the reference
 * traversal, not a defect: the painter orders tile spans, and a model that
 * overhangs its span is beyond what tile order can express. Xarpus' legs reach
 * well past his 5x5, which is why acid two tiles in front of him still clips
 * over them.
 */
static void
test_decor_outside_the_footprint_still_draws_over(void)
{
    printf("decor outside the footprint still draws over the mover (reference)\n");

    expect(puddle_draws_under(4, 4, 1) == 0,
           "a nearer tile outside the 5x5 draws over him, decor or not");
    expect(puddle_draws_under(11, 11, 1) == 1,
           "a farther tile outside the 5x5 still draws under him");
}

/*
 * The slot is borrowed, not taken.
 *
 * A runtime spawn can land on a tile that baked its own floor decor — a path
 * tile, a floor plate. The dynamic add displaces it for the frame and
 * painter_reset_to_static must hand it back, or the next frame's static decor
 * is gone for good and the tile's slot names an element the truncation has
 * already given away.
 */
static void
test_reset_restores_a_displaced_static_decor(void)
{
    struct Painter* p = make_painter();
    struct PaintersBuffer* buf = make_buffer();
    const int baked = 800;
    const int spawned = 801;
    int baked_slot;

    printf("a dynamic decor borrows a baked decor's slot and gives it back\n");

    painter_add_ground_decor(p, 4, 4, 0, baked);
    painter_mark_static_count(p);
    baked_slot = painter_tile_at(p, 4, 4, 0)->ground_decor;
    expect(baked_slot >= 0, "the baked decor owns the slot");

    painter_reset_to_static(p);
    painter_add_ground_decor_dynamic(p, 4, 4, 0, spawned);
    expect(painter_tile_at(p, 4, 4, 0)->ground_decor != baked_slot,
           "the spawn takes the slot for the frame");

    painter_paint_bucket(p, buf, 0, 0, 0);
    expect(entity_emits(buf, spawned, NULL) == 1, "the spawn draws");
    expect(entity_emits(buf, baked, NULL) == 0, "and the baked decor does not");

    /* Next frame. */
    painter_reset_to_static(p);
    expect(painter_tile_at(p, 4, 4, 0)->ground_decor == baked_slot,
           "the reset hands the slot back to the baked decor");

    buf->command_count = 0;
    painter_paint_bucket(p, buf, 0, 0, 0);
    expect(entity_emits(buf, baked, NULL) == 1,
           "so a frame with no spawn draws the baked decor again");

    free_buffer(buf);
    painter_free(p);
}

/*
 * Two dynamic decors chaining on one tile within a frame, unwound in the right
 * order. B displaced A, A displaced the baked one; the reset walks newest-first
 * so the tile ends on the baked slot rather than on A, which the same pass is
 * throwing away.
 *
 * world_cycle should never produce this — the scenery pool holds one loc per
 * (tile, shape) — but the unwind is one loop direction away from being wrong,
 * and a stale slot index is a use-after-truncate, not a cosmetic slip.
 */
static void
test_reset_unwinds_a_chain_newest_first(void)
{
    struct Painter* p = make_painter();
    const int baked = 810;
    int baked_slot;

    printf("two dynamic decors on one tile unwind back to the baked one\n");

    painter_add_ground_decor(p, 6, 6, 0, baked);
    painter_mark_static_count(p);
    baked_slot = painter_tile_at(p, 6, 6, 0)->ground_decor;

    painter_reset_to_static(p);
    painter_add_ground_decor_dynamic(p, 6, 6, 0, 811);
    painter_add_ground_decor_dynamic(p, 6, 6, 0, 812);
    painter_reset_to_static(p);

    expect(painter_tile_at(p, 6, 6, 0)->ground_decor == baked_slot,
           "the tile is back on the baked slot, not on the first dynamic one");

    painter_free(p);
}

/*
 * An empty slot is the ordinary case: the arena floor Xarpus fights on bakes no
 * decor at all, so nearly every puddle displaces nothing and the reset must
 * leave the tile at -1 rather than at a truncated index.
 */
static void
test_reset_clears_a_slot_that_was_empty(void)
{
    struct Painter* p = make_painter();

    printf("a dynamic decor on a bare tile leaves the slot empty again\n");

    painter_mark_static_count(p);
    painter_reset_to_static(p);

    painter_add_ground_decor_dynamic(p, 7, 7, 0, 820);
    expect(painter_tile_at(p, 7, 7, 0)->ground_decor >= 0, "the spawn took the slot");

    painter_reset_to_static(p);
    expect(painter_tile_at(p, 7, 7, 0)->ground_decor == -1,
           "and the reset leaves it empty, not naming a truncated element");

    painter_free(p);
}

int
main(void)
{
    test_dynamic_decor_draws_under_a_mover_standing_on_it();
    test_the_rest_of_the_footprint_was_already_under();
    test_decor_outside_the_footprint_still_draws_over();
    test_reset_restores_a_displaced_static_decor();
    test_reset_unwinds_a_chain_newest_first();
    test_reset_clears_a_slot_that_was_empty();

    if( g_failures )
    {
        printf("painters_test_ground_decor_dynamic: %d FAILED\n", g_failures);
        return 1;
    }
    printf("OK: painters_test_ground_decor_dynamic\n");
    return 0;
}
