/**
 * Painter descent into nested world views (sailing, SAILING_PLAN C3).
 *
 * A world entity (a boat) is its own `struct World` with its own
 * `struct Painter`. It takes its place in the PARENT painter's draw order as a
 * 1x1 pseudo-loc (`painter_add_world_entity`, flag PNTR_SCENERY_WORLDENTITY),
 * and when the bucket drain reaches that pseudo-loc it emits
 * PNTR_CMD_BEGIN_WORLD(view id), suspends, runs the boat's painter to
 * completion, emits PNTR_CMD_END_WORLD and resumes exactly where it stopped.
 *
 * The properties that make that worth having, and that this file pins:
 *
 *   - the boat's commands land EXACTLY between its two markers, and nothing
 *     of the parent's does;
 *   - the markers land in painter order — a loc farther from the eye than the
 *     boat emits before BEGIN_WORLD, a loc nearer emits after END_WORLD. That
 *     is the whole reason for the pseudo-loc: ordering against real locs,
 *     actors and projectiles comes free from the reference traversal;
 *   - the marker stream is ALWAYS balanced, including on the two refusals
 *     (a view with no painter bound, and a cycle A -> B -> A). The emit side
 *     tracks its current world off these markers, so an unbalanced stream is
 *     not a cosmetic defect, it is a wrong-world terrain lookup or an assert
 *     in frame_view_pop;
 *   - nesting works to the registry bound (root + views 1..15) on an EXPLICIT
 *     stack, never the C call stack;
 *   - with no world entity live the stream carries no markers at all, i.e.
 *     the restructure is invisible to every existing scene.
 *
 * No cache, no world, no server: a bare painter and a command buffer, so this
 * gate can never degrade into a SKIP.
 *
 * Build/run: make -C src test-painters-world-entity
 */
#include "graphics/shared_tables.h"
#include "painters/painters.h"
#include "world/wev.h"

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

/* Root-scene locs, laid out on the camera's diagonal so Manhattan distance
 * from the eye at (0,0) orders them unambiguously: FAR is behind the boat,
 * NEAR is in front of it. */
#define LOC_FAR 901
#define LOC_NEAR 902
/* Deck locs, in the boat's own scene. */
#define LOC_DECK_A 701
#define LOC_DECK_B 702
/* Three tile-mates sharing the boat's tile, so one ready batch holds them
 * all and the descent has to suspend partway through it. */
#define LOC_SAME_A 801
#define LOC_SAME_B 802
#define LOC_SAME_C 803
/* One actor, routed either onto the deck or left ashore (C5). */
#define ACTOR_ID 601

static struct Painter*
make_painter(void)
{
    struct Painter* p = painter_new(SCENE, SCENE, LEVELS, PAINTER_NEW_CTX_BUCKET);
    assert(p && "painter_new");
    painter_set_draw_distance(p, SCENE);
    painter_mark_static_count(p);
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

/**
 * Same commands, in the same order.
 *
 * Field-wise rather than memcmp: PaintersElementCommand is a union of bitfield
 * views that between them leave 6 of the 32 bits unnamed, and what a compound
 * literal leaves in those is unspecified — at -O0 it is whatever was on the
 * stack. Comparing the words instead of the fields would make this test fail on
 * a build flag rather than on a change in the paint order.
 */
static int
commands_match(
    struct PaintersBuffer* a,
    struct PaintersBuffer* b)
{
    for( int i = 0; i < a->command_count; i++ )
    {
        struct PaintersElementCommand* x = &a->commands[i];
        struct PaintersElementCommand* y = &b->commands[i];
        if( x->_bf_kind != y->_bf_kind )
            return 0;
        if( x->_bf_kind == PNTR_CMD_TERRAIN || x->_bf_kind == PNTR_CMD_TERRAIN_PICK_ONLY )
        {
            if( x->_terrain._bf_terrain_x != y->_terrain._bf_terrain_x ||
                x->_terrain._bf_terrain_z != y->_terrain._bf_terrain_z ||
                x->_terrain._bf_terrain_y != y->_terrain._bf_terrain_y )
                return 0;
        }
        else if( x->_entity._bf_entity != y->_entity._bf_entity )
        {
            return 0;
        }
    }
    return 1;
}

/** Index of the first command of `kind` naming `entity`, or -1. */
static int
index_of(
    struct PaintersBuffer* buf,
    int kind,
    int entity)
{
    for( int i = 0; i < buf->command_count; i++ )
    {
        struct PaintersElementCommand* c = &buf->commands[i];
        if( (int)c->_bf_kind != kind )
            continue;
        if( (int)c->_entity._bf_entity != entity )
            continue;
        return i;
    }
    return -1;
}

static int
count_of(
    struct PaintersBuffer* buf,
    int kind,
    int entity)
{
    int n = 0;
    for( int i = 0; i < buf->command_count; i++ )
    {
        struct PaintersElementCommand* c = &buf->commands[i];
        if( (int)c->_bf_kind != kind )
            continue;
        if( (int)c->_entity._bf_entity != entity )
            continue;
        n++;
    }
    return n;
}

static int
count_kind(
    struct PaintersBuffer* buf,
    int kind)
{
    int n = 0;
    for( int i = 0; i < buf->command_count; i++ )
        if( (int)buf->commands[i]._bf_kind == kind )
            n++;
    return n;
}

/**
 * Walk the stream keeping a marker depth. Returns 1 when every BEGIN has its
 * END, nothing closes at depth 0, and the stream ends at depth 0. `max_depth`
 * receives the deepest nesting reached.
 */
static int
markers_balanced(
    struct PaintersBuffer* buf,
    int* max_depth)
{
    int depth = 0;
    int deepest = 0;
    int stack[PAINTER_MAX_WORLD_VIEWS + 4];
    for( int i = 0; i < buf->command_count; i++ )
    {
        struct PaintersElementCommand* c = &buf->commands[i];
        if( (int)c->_bf_kind == PNTR_CMD_BEGIN_WORLD )
        {
            if( depth >= (int)(sizeof(stack) / sizeof(stack[0])) )
                return 0;
            stack[depth++] = (int)c->_entity._bf_entity;
            if( depth > deepest )
                deepest = depth;
        }
        else if( (int)c->_bf_kind == PNTR_CMD_END_WORLD )
        {
            if( depth <= 0 )
                return 0;
            depth--;
            /* The close must name the view that was opened; the emit side
             * pops by depth and would otherwise silently swap worlds. */
            if( stack[depth] != (int)c->_entity._bf_entity )
                return 0;
        }
    }
    if( max_depth )
        *max_depth = deepest;
    return depth == 0;
}

/* -------------------------------------------------------------------------
 * 1. The boat's commands land exactly between its markers, in painter order.
 * ---------------------------------------------------------------------- */

static void
test_descent_emits_deck_between_markers(void)
{
    printf("test_descent_emits_deck_between_markers\n");

    struct Painter* root = make_painter();
    struct Painter* deck = make_painter();
    struct PaintersBuffer* buf = make_buffer();

    /* Eye at the scene's south-west corner: painter order is farthest first,
     * so distance from (0,0) is simply sx + sz. */
    painter_add_normal_scenery(root, 12, 12, 0, LOC_FAR, 1, 1, 0);  /* d = 24 */
    painter_add_world_entity(root, 0, 7, 7, /*view_id=*/1, 0, 1, 1);      /* d = 14 */
    painter_add_normal_scenery(root, 2, 2, 0, LOC_NEAR, 1, 1, 0);   /* d =  4 */

    painter_add_normal_scenery(deck, 6, 6, 0, LOC_DECK_A, 1, 1, 0);
    painter_add_normal_scenery(deck, 1, 1, 0, LOC_DECK_B, 1, 1, 0);

    painter_clear_world_entity_views(root);
    painter_set_world_entity_view(root, 1, deck, /*cam_sx=*/0, /*cam_sz=*/0, 0);

    painter_paint_bucket(root, buf, 0, 0, 0);

    int begin = index_of(buf, PNTR_CMD_BEGIN_WORLD, 1);
    int end = index_of(buf, PNTR_CMD_END_WORLD, 1);
    int far_i = index_of(buf, PNTR_CMD_ELEMENT, LOC_FAR);
    int near_i = index_of(buf, PNTR_CMD_ELEMENT, LOC_NEAR);
    int deck_a = index_of(buf, PNTR_CMD_ELEMENT, LOC_DECK_A);
    int deck_b = index_of(buf, PNTR_CMD_ELEMENT, LOC_DECK_B);

    expect(count_of(buf, PNTR_CMD_BEGIN_WORLD, 1) == 1, "exactly one BEGIN_WORLD(1)");
    expect(count_of(buf, PNTR_CMD_END_WORLD, 1) == 1, "exactly one END_WORLD(1)");
    expect(begin >= 0 && end > begin, "END_WORLD follows BEGIN_WORLD");
    expect(markers_balanced(buf, NULL), "marker stream is balanced");

    expect(deck_a > begin && deck_a < end, "deck loc A emits inside the markers");
    expect(deck_b > begin && deck_b < end, "deck loc B emits inside the markers");
    /* Deck order is the deck painter's own: eye at deck (0,0), so the far
     * deck loc precedes the near one. */
    expect(deck_a >= 0 && deck_b > deck_a, "deck locs keep the deck painter's order");

    /* The point of the pseudo-loc: the boat sits in the PARENT's draw order. */
    expect(far_i >= 0 && far_i < begin, "a loc behind the boat emits before BEGIN_WORLD");
    expect(near_i > end, "a loc in front of the boat emits after END_WORLD");

    /* And nothing of the parent's leaks into the boat's span. */
    int parent_inside = 0;
    for( int i = begin + 1; i < end; i++ )
    {
        struct PaintersElementCommand* c = &buf->commands[i];
        if( (int)c->_bf_kind != PNTR_CMD_ELEMENT )
            continue;
        if( (int)c->_entity._bf_entity == LOC_FAR ||
            (int)c->_entity._bf_entity == LOC_NEAR )
            parent_inside++;
    }
    expect(parent_inside == 0, "no parent loc emits between the markers");

    /* The pseudo-loc is a view id, never a scene element id: it must not reach
     * the stream as an ELEMENT command. */
    expect(count_of(buf, PNTR_CMD_ELEMENT, 1) == 0, "view id never emitted as an element");

    free_buffer(buf);
    painter_free(deck);
    painter_free(root);
}

/* -------------------------------------------------------------------------
 * 1b. The boat shares its tile: the batch is cut in half and resumed.
 * ---------------------------------------------------------------------- */

static void
test_descent_resumes_the_rest_of_the_tile(void)
{
    printf("test_descent_resumes_the_rest_of_the_tile\n");

    struct Painter* root = make_painter();
    struct Painter* deck = make_painter();
    struct PaintersBuffer* buf = make_buffer();

    /* All four on ONE tile, so one pop collects them into a single ready batch
     * and the descent suspends partway through it. This is the only shape that
     * exercises the resume with elements still to emit; a boat alone on its
     * tile resumes with an empty remainder. */
    painter_add_normal_scenery(root, 7, 7, 0, LOC_SAME_A, 1, 1, 0);
    painter_add_world_entity(root, 0, 7, 7, /*view_id=*/1, 0, 1, 1);
    painter_add_normal_scenery(root, 7, 7, 0, LOC_SAME_B, 1, 1, 0);
    painter_add_normal_scenery(root, 7, 7, 0, LOC_SAME_C, 1, 1, 0);

    painter_add_normal_scenery(deck, 6, 6, 0, LOC_DECK_A, 1, 1, 0);
    painter_add_normal_scenery(deck, 1, 1, 0, LOC_DECK_B, 1, 1, 0);

    painter_clear_world_entity_views(root);
    painter_set_world_entity_view(root, 1, deck, /*cam_sx=*/0, /*cam_sz=*/0, 0);

    painter_paint_bucket(root, buf, 0, 0, 0);

    int begin = index_of(buf, PNTR_CMD_BEGIN_WORLD, 1);
    int end = index_of(buf, PNTR_CMD_END_WORLD, 1);
    int a = index_of(buf, PNTR_CMD_ELEMENT, LOC_SAME_A);
    int b = index_of(buf, PNTR_CMD_ELEMENT, LOC_SAME_B);
    int c = index_of(buf, PNTR_CMD_ELEMENT, LOC_SAME_C);

    expect(markers_balanced(buf, NULL), "shared-tile marker stream is balanced");
    expect(count_of(buf, PNTR_CMD_BEGIN_WORLD, 1) == 1, "one BEGIN_WORLD on the shared tile");
    expect(count_of(buf, PNTR_CMD_END_WORLD, 1) == 1, "one END_WORLD on the shared tile");

    /* Every tile-mate emits exactly once: the ones before the boat from the
     * interrupted batch, the ones after it from the resumed one. A resume that
     * restarts the batch would double them, one that drops it would lose them. */
    expect(count_of(buf, PNTR_CMD_ELEMENT, LOC_SAME_A) == 1, "tile-mate A emits once");
    expect(count_of(buf, PNTR_CMD_ELEMENT, LOC_SAME_B) == 1, "tile-mate B emits once");
    expect(count_of(buf, PNTR_CMD_ELEMENT, LOC_SAME_C) == 1, "tile-mate C emits once");

    /* At least one of them has to land on the far side of the boat, or the
     * batch was never actually cut and this test proves nothing. */
    expect(
        (a > end) || (b > end) || (c > end),
        "the batch really is cut: a tile-mate emits after END_WORLD");

    /* And none of the parent's own locs may sit inside the boat's span. */
    int parent_inside = 0;
    for( int i = begin + 1; i < end; i++ )
    {
        struct PaintersElementCommand* cmd = &buf->commands[i];
        if( (int)cmd->_bf_kind != PNTR_CMD_ELEMENT )
            continue;
        if( (int)cmd->_entity._bf_entity == LOC_SAME_A ||
            (int)cmd->_entity._bf_entity == LOC_SAME_B ||
            (int)cmd->_entity._bf_entity == LOC_SAME_C )
            parent_inside++;
    }
    expect(parent_inside == 0, "no tile-mate emits between the markers");

    expect(
        index_of(buf, PNTR_CMD_ELEMENT, LOC_DECK_A) > begin &&
            index_of(buf, PNTR_CMD_ELEMENT, LOC_DECK_A) < end,
        "the deck still emits inside the markers");

    free_buffer(buf);
    painter_free(deck);
    painter_free(root);
}

/* -------------------------------------------------------------------------
 * 2. Refusals still balance the stream.
 * ---------------------------------------------------------------------- */

static void
test_unbound_view_emits_an_empty_pair(void)
{
    printf("test_unbound_view_emits_an_empty_pair\n");

    struct Painter* root = make_painter();
    struct PaintersBuffer* buf = make_buffer();

    painter_add_normal_scenery(root, 12, 12, 0, LOC_FAR, 1, 1, 0);
    painter_add_world_entity(root, 0, 7, 7, /*view_id=*/3, 0, 1, 1);
    painter_add_normal_scenery(root, 2, 2, 0, LOC_NEAR, 1, 1, 0);

    /* Deliberately NOT bound: the painter can reach a pseudo-loc for a view the
     * App never published (despawn between the cycle and the paint). */
    painter_clear_world_entity_views(root);

    painter_paint_bucket(root, buf, 0, 0, 0);

    int begin = index_of(buf, PNTR_CMD_BEGIN_WORLD, 3);
    int end = index_of(buf, PNTR_CMD_END_WORLD, 3);
    expect(begin >= 0, "unbound view still opens");
    expect(end == begin + 1, "unbound view closes immediately, empty");
    expect(markers_balanced(buf, NULL), "unbound view leaves the stream balanced");
    expect(
        index_of(buf, PNTR_CMD_ELEMENT, LOC_NEAR) > end,
        "the parent resumes after the empty pair");

    free_buffer(buf);
    painter_free(root);
}

static void
test_cycle_is_refused_not_re_entered(void)
{
    printf("test_cycle_is_refused_not_re_entered\n");

    /* root -> view 1 (deck1) -> view 2 (deck2) -> view 1 again. Re-entering
     * deck1 would memset its element_paints out from under the suspended
     * frame, so the third hop must be refused, not taken. */
    struct Painter* root = make_painter();
    struct Painter* deck1 = make_painter();
    struct Painter* deck2 = make_painter();
    struct PaintersBuffer* buf = make_buffer();

    painter_add_world_entity(root, 0, 7, 7, 1, 0, 1, 1);
    painter_add_normal_scenery(deck1, 6, 6, 0, LOC_DECK_A, 1, 1, 0);
    painter_add_world_entity(deck1, 0, 3, 3, 2, 0, 1, 1);
    painter_add_normal_scenery(deck2, 6, 6, 0, LOC_DECK_B, 1, 1, 0);
    painter_add_world_entity(deck2, 0, 3, 3, 1, 0, 1, 1);

    painter_clear_world_entity_views(root);
    painter_set_world_entity_view(root, 1, deck1, 0, 0, 0);
    painter_clear_world_entity_views(deck1);
    painter_set_world_entity_view(deck1, 2, deck2, 0, 0, 0);
    painter_clear_world_entity_views(deck2);
    painter_set_world_entity_view(deck2, 1, deck1, 0, 0, 0);

    painter_paint_bucket(root, buf, 0, 0, 0);

    int max_depth = 0;
    expect(markers_balanced(buf, &max_depth), "cycle leaves the stream balanced");
    expect(max_depth == 3, "the cycle stops at the third level (root, 1, 2)");
    expect(count_of(buf, PNTR_CMD_BEGIN_WORLD, 1) == 2, "view 1 is opened twice");
    expect(count_of(buf, PNTR_CMD_END_WORLD, 1) == 2, "view 1 is closed twice");
    /* Once really (its loc drawn), once as the refused empty pair. */
    expect(count_of(buf, PNTR_CMD_ELEMENT, LOC_DECK_A) == 1, "deck1 painted exactly once");
    expect(count_of(buf, PNTR_CMD_ELEMENT, LOC_DECK_B) == 1, "deck2 painted exactly once");

    free_buffer(buf);
    painter_free(deck2);
    painter_free(deck1);
    painter_free(root);
}

/* -------------------------------------------------------------------------
 * 3. Nesting to the registry bound, on the explicit stack.
 * ---------------------------------------------------------------------- */

static void
test_nesting_to_the_registry_bound(void)
{
    printf("test_nesting_to_the_registry_bound\n");

    /* root + views 1..15 == PAINTER_MAX_WORLD_VIEWS contexts on the stack. */
    struct Painter* p[PAINTER_MAX_WORLD_VIEWS];
    struct PaintersBuffer* buf = make_buffer();

    for( int i = 0; i < PAINTER_MAX_WORLD_VIEWS; i++ )
        p[i] = make_painter();

    for( int i = 0; i < PAINTER_MAX_WORLD_VIEWS; i++ )
    {
        painter_clear_world_entity_views(p[i]);
        if( i + 1 < PAINTER_MAX_WORLD_VIEWS )
        {
            painter_add_world_entity(p[i], 0, 7, 7, i + 1, 0, 1, 1);
            painter_set_world_entity_view(p[i], i + 1, p[i + 1], 0, 0, 0);
        }
        painter_add_normal_scenery(p[i], 2, 2, 0, 800 + i, 1, 1, 0);
    }

    painter_paint_bucket(p[0], buf, 0, 0, 0);

    int max_depth = 0;
    expect(markers_balanced(buf, &max_depth), "deep chain is balanced");
    expect(
        max_depth == PAINTER_MAX_WORLD_VIEWS - 1,
        "the chain nests to the registry bound");
    expect(
        count_kind(buf, PNTR_CMD_BEGIN_WORLD) == PAINTER_MAX_WORLD_VIEWS - 1,
        "one BEGIN per view");

    int all_painted = 1;
    for( int i = 0; i < PAINTER_MAX_WORLD_VIEWS; i++ )
        if( count_of(buf, PNTR_CMD_ELEMENT, 800 + i) != 1 )
            all_painted = 0;
    expect(all_painted, "every level in the chain painted exactly once");

    /* Innermost view opens last and closes first: strict nesting, not a flat
     * list of pairs. */
    int outer_begin = index_of(buf, PNTR_CMD_BEGIN_WORLD, 1);
    int inner_begin = index_of(buf, PNTR_CMD_BEGIN_WORLD, PAINTER_MAX_WORLD_VIEWS - 1);
    int inner_end = index_of(buf, PNTR_CMD_END_WORLD, PAINTER_MAX_WORLD_VIEWS - 1);
    int outer_end = index_of(buf, PNTR_CMD_END_WORLD, 1);
    expect(
        outer_begin < inner_begin && inner_begin < inner_end && inner_end < outer_end,
        "the innermost view is strictly inside the outermost");

    free_buffer(buf);
    for( int i = 0; i < PAINTER_MAX_WORLD_VIEWS; i++ )
        painter_free(p[i]);
}

/* -------------------------------------------------------------------------
 * 4. Zero boats: the restructure is invisible.
 * ---------------------------------------------------------------------- */

static void
test_no_world_entity_emits_no_markers(void)
{
    printf("test_no_world_entity_emits_no_markers\n");

    struct Painter* root = make_painter();
    struct PaintersBuffer* a = make_buffer();
    struct PaintersBuffer* b = make_buffer();

    painter_add_normal_scenery(root, 12, 12, 0, LOC_FAR, 1, 1, 0);
    painter_add_normal_scenery(root, 2, 2, 0, LOC_NEAR, 1, 1, 0);

    painter_paint_bucket(root, a, 0, 0, 0);
    /* Repaint the same painter: the per-context setup must fully re-arm, so a
     * second paint is bit-identical to the first. */
    painter_paint_bucket(root, b, 0, 0, 0);

    expect(count_kind(a, PNTR_CMD_BEGIN_WORLD) == 0, "no BEGIN_WORLD with zero boats");
    expect(count_kind(a, PNTR_CMD_END_WORLD) == 0, "no END_WORLD with zero boats");
    expect(a->command_count > 0, "the boat-free scene still paints");
    expect(a->command_count == b->command_count, "repaint emits the same count");
    expect(
        a->command_count == b->command_count && commands_match(a, b),
        "repaint emits the same stream");

    free_buffer(b);
    free_buffer(a);
    painter_free(root);
}

/* -------------------------------------------------------------------------
 * 7. Actors aboard (SAILING_PLAN C5.1): an actor inside a boat's footprint
 *    emits between that boat's markers, one outside it does not, and moving
 *    the SAME element across the boundary moves it across the markers.
 * ---------------------------------------------------------------------- */

/*
 * An actor reaches the painter as an ordinary dynamic scenery element — that
 * is the whole design: routing decides WHICH painter registers it, and nothing
 * downstream has to know an actor from a loc. So the painter-level contract is
 * exactly "registered on the deck painter => inside the markers; registered on
 * the root painter => outside them", and this pins it in both directions with
 * one element id, which is what a boarding actually is.
 */
static void
test_actor_aboard_emits_inside_the_markers(void)
{
    printf("test_actor_aboard_emits_inside_the_markers\n");

    struct Painter* root = make_painter();
    struct Painter* deck = make_painter();
    struct PaintersBuffer* buf = make_buffer();

    /* Eye at (0,0): painter order is farthest first, so distance is sx + sz.
     * The shore actor stands in FRONT of the boat, which is the placement that
     * makes "outside the markers" mean something stronger than "anywhere". */
    painter_add_normal_scenery(root, 12, 12, 0, LOC_FAR, 1, 1, 0); /* d = 24 */
    painter_add_world_entity(root, 0, 7, 7, /*view_id=*/1, 0, 1, 1);     /* d = 14 */
    painter_add_normal_scenery(root, 2, 2, 0, ACTOR_ID, 1, 1, 0);  /* d =  4 */

    painter_clear_world_entity_views(root);
    painter_set_world_entity_view(root, 1, deck, 0, 0, 0);
    painter_paint_bucket(root, buf, 0, 0, 0);

    {
        int begin = index_of(buf, PNTR_CMD_BEGIN_WORLD, 1);
        int end = index_of(buf, PNTR_CMD_END_WORLD, 1);
        int actor = index_of(buf, PNTR_CMD_ELEMENT, ACTOR_ID);

        expect(markers_balanced(buf, NULL), "ashore: marker stream is balanced");
        expect(begin >= 0 && end > begin, "ashore: the boat still opens and closes");
        expect(actor >= 0, "ashore: the actor emits");
        expect(
            actor > end,
            "an actor outside the boat's footprint emits outside the markers");
    }

    free_buffer(buf);
    painter_free(deck);
    painter_free(root);

    /* The membership change. Same element id, same scene layout, same eye —
     * only the painter it is registered with moves, which is precisely what
     * app_wev_route_actors decides and what the deck's
     * World_ForeignActorRegisterFn then acts on. */
    root = make_painter();
    deck = make_painter();
    buf = make_buffer();

    painter_add_normal_scenery(root, 12, 12, 0, LOC_FAR, 1, 1, 0);
    painter_add_world_entity(root, 0, 7, 7, /*view_id=*/1, 0, 1, 1);
    painter_add_normal_scenery(deck, 6, 6, 0, ACTOR_ID, 1, 1, 0);

    painter_clear_world_entity_views(root);
    painter_set_world_entity_view(root, 1, deck, 0, 0, 0);
    painter_paint_bucket(root, buf, 0, 0, 0);

    {
        int begin = index_of(buf, PNTR_CMD_BEGIN_WORLD, 1);
        int end = index_of(buf, PNTR_CMD_END_WORLD, 1);
        int actor = index_of(buf, PNTR_CMD_ELEMENT, ACTOR_ID);
        int far_i = index_of(buf, PNTR_CMD_ELEMENT, LOC_FAR);

        expect(markers_balanced(buf, NULL), "aboard: marker stream is balanced");
        expect(count_of(buf, PNTR_CMD_ELEMENT, ACTOR_ID) == 1, "aboard: the actor emits once");
        expect(
            actor > begin && actor < end,
            "an actor inside the boat's footprint emits between the markers");
        expect(far_i >= 0 && far_i < begin, "aboard: the shore loc still emits before the boat");
    }

    free_buffer(buf);
    painter_free(deck);
    painter_free(root);
}

/* -------------------------------------------------------------------------
 * 8. Membership and the deck transform (SAILING_PLAN C5.1/C5.2).
 * ---------------------------------------------------------------------- */

/* A hull with an asymmetric deck, so a transposed axis cannot pass. */
#define DECK_W 4
#define DECK_H 6
#define HULL_X 5000
#define HULL_Z 9000

static struct WevDeckBox
make_box(int angle)
{
    struct WevDeckBox box;
    box.pos_x = HULL_X;
    box.pos_z = HULL_Z;
    box.angle = angle;
    /* The recenter C3 composes: -size*64 puts the deck's CENTRE on the hull
     * position, and this fixture carries no config pivot. */
    box.recenter_x = -(DECK_W * 64);
    box.recenter_z = -(DECK_H * 64);
    box.size_x_tiles = DECK_W;
    box.size_z_tiles = DECK_H;
    return box;
}

static int
near_enough(
    int a,
    int b)
{
    int d = a - b;
    /* Two Q16 rotations, each truncating: a couple of fine units, i.e. well
     * under a fiftieth of a tile. */
    return d >= -4 && d <= 4;
}

static void
test_deck_membership_and_transform(void)
{
    printf("test_deck_membership_and_transform\n");

    /* --- The base rectangle is half-open on both axes, so hulls partition. */
    {
        struct WevDeckBox box = make_box(0);
        expect(Wev_DeckContainsDeckPoint(&box, 0, 0), "deck origin is aboard");
        expect(
            Wev_DeckContainsDeckPoint(&box, DECK_W * 128 - 1, DECK_H * 128 - 1),
            "the far corner one unit inside is aboard");
        expect(!Wev_DeckContainsDeckPoint(&box, DECK_W * 128, 0), "the +x edge is not aboard");
        expect(!Wev_DeckContainsDeckPoint(&box, 0, DECK_H * 128), "the +z edge is not aboard");
        expect(!Wev_DeckContainsDeckPoint(&box, -1, 0), "one unit off the -x side is not aboard");
        expect(!Wev_DeckContainsDeckPoint(&box, 0, -1), "one unit off the -z side is not aboard");
        /* An unpublished deck size owns nothing rather than owning everything:
         * the window between Wevs_Spawn and the first REBUILD_WORLDENTITY. */
        box.size_x_tiles = 0;
        expect(!Wev_DeckContainsDeckPoint(&box, 0, 0), "a deck with no size owns nothing");
    }

    /* --- Round trip at every one of the 16 baked headings. */
    {
        int const probes[][2] = {
            { 0, 0 },
            { DECK_W * 64, DECK_H * 64 },
            { DECK_W * 128 - 1, DECK_H * 128 - 1 },
            { DECK_W * 128 - 8, 8 },
            { 8, DECK_H * 128 - 8 },
            { 37, 511 },
        };
        int bad_trip = 0;
        int bad_member = 0;
        for( int bucket = 0; bucket < 16; bucket++ )
        {
            struct WevDeckBox box = make_box(bucket * 128);
            for( int p = 0; p < (int)(sizeof(probes) / sizeof(probes[0])); p++ )
            {
                int px;
                int pz;
                int back_x;
                int back_z;
                Wev_ParentFromDeck(&box, probes[p][0], probes[p][1], &px, &pz);
                Wev_DeckFromParent(&box, px, pz, &back_x, &back_z);
                if( !near_enough(back_x, probes[p][0]) || !near_enough(back_z, probes[p][1]) )
                    bad_trip++;
                /* Membership only where truncation cannot decide it. A point
                 * one fine unit inside the gunwale round-trips to within a
                 * couple of units, which is enough to land outside a half-open
                 * edge -- inherent to a Q16 rotate-and-truncate, and 1/128 of
                 * a tile, so it is a fixture concern and not a defect. */
                if( probes[p][0] >= 4 && probes[p][1] >= 4 &&
                    probes[p][0] < DECK_W * 128 - 4 && probes[p][1] < DECK_H * 128 - 4 &&
                    !Wev_DeckContainsParentPoint(&box, px, pz) )
                    bad_member++;
            }
        }
        expect(bad_trip == 0, "deck <-> parent round trips at all 16 headings");
        expect(bad_member == 0, "every deck point is aboard from the parent side too");
    }

    /* --- A player at the bow stays at the bow through a full 360.
     *
     * The bow is the +z edge of the deck's centre line. Rotating the hull must
     * sweep its ROOT position around the hull on a circle of fixed radius
     * (otherwise the transform is translating instead of rotating) while its
     * DECK position never moves (otherwise the actor slides around the boat). */
    {
        int const bow_x = DECK_W * 64;
        int const bow_z = DECK_H * 128 - 64;
        int r2_ref = -1;
        int bad_bow = 0;
        int bad_radius = 0;
        int moved = 0;
        int base_px;
        int base_pz;
        {
            struct WevDeckBox upright = make_box(0);
            Wev_ParentFromDeck(&upright, bow_x, bow_z, &base_px, &base_pz);
        }
        for( int angle = 0; angle < 2048; angle += 8 )
        {
            struct WevDeckBox box = make_box(angle);
            int px;
            int pz;
            int back_x;
            int back_z;
            int dx;
            int dz;
            int r2;

            Wev_ParentFromDeck(&box, bow_x, bow_z, &px, &pz);
            Wev_DeckFromParent(&box, px, pz, &back_x, &back_z);
            if( !near_enough(back_x, bow_x) || !near_enough(back_z, bow_z) )
                bad_bow++;
            if( !Wev_DeckContainsParentPoint(&box, px, pz) )
                bad_bow++;

            dx = px - HULL_X;
            dz = pz - HULL_Z;
            r2 = dx * dx + dz * dz;
            if( r2_ref < 0 )
                r2_ref = r2;
            /* Radius in fine units is ~192 here, so r2 ~ 36864; a few units of
             * truncation per rotation moves r2 by a couple of thousand at
             * most. Generous on purpose: the assertion is "a circle", not "an
             * exact circle". */
            else if( r2 - r2_ref > 4000 || r2_ref - r2 > 4000 )
                bad_radius++;
            if( px != base_px || pz != base_pz )
                moved++;
        }
        expect(bad_bow == 0, "the bow stays at the bow through a full 360");
        expect(bad_radius == 0, "the bow traces a circle around the hull, not a drift");
        expect(moved > 0, "the bow's root position actually moves as the hull turns");
    }

    /* --- Somewhere well off the hull is aboard at no heading at all. */
    {
        int aboard = 0;
        for( int angle = 0; angle < 2048; angle += 8 )
        {
            struct WevDeckBox box = make_box(angle);
            if( Wev_DeckContainsParentPoint(&box, HULL_X + 4000, HULL_Z - 3000) )
                aboard++;
        }
        expect(aboard == 0, "a point far from the hull is aboard at no heading");
    }
}

int
main(void)
{
    /* The deck transform reads the Q16 trig tables, and nothing in a bare
     * painter build has built them yet. */
    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();

    test_descent_emits_deck_between_markers();
    test_descent_resumes_the_rest_of_the_tile();
    test_unbound_view_emits_an_empty_pair();
    test_cycle_is_refused_not_re_entered();
    test_nesting_to_the_registry_bound();
    test_no_world_entity_emits_no_markers();
    test_actor_aboard_emits_inside_the_markers();
    test_deck_membership_and_transform();

    if( g_failures )
    {
        fprintf(stderr, "\n%d failure(s)\n", g_failures);
        return 1;
    }
    printf("\nall painter world-entity tests passed\n");
    return 0;
}
