/**
 * Unit tests for VIS_BELOW's painter behaviour — the draw-level cull and the
 * per-tile terrain-mesh set.
 *
 * Reference semantics (rev-239 class112 + Client-TS, verified side by side in
 * docs/ORANGE_WEDGE.md §14): FLOFLAG_VIS_BELOW lowers a tile's DRAW LEVEL to 0
 * — the reference's renderLevel (method4161), our visible_gte_level — and
 * nothing else. The mesh stays on its own level's tile and pops in its own
 * traversal slot, after the tile below fully retires. The level mask (roof
 * hiding) gates per tile on visible_gte_level, so a flagged tile survives a
 * mask of 0x1 without its geometry moving anywhere.
 *
 * An earlier revision instead relocated the flagged mesh into the lower
 * level's terrain_levels set, which drew it before the lower tile's walls —
 * the reverse of the reference order. These tests pin the reference shape so
 * that mechanism cannot come back.
 *
 * What the world builder does (world_builder.c), restated here so the
 * simulation is checkable against something:
 *
 *   for each column, for each grid level g:
 *     painter_tile_set_draw_level(g, RSCache_MapFloorVisBelowDrawLevel(...))
 *   terrain_levels is left owning exactly the tile's own mesh; levels that
 *   decoded no mesh get their set cleared to 0 after the terrain build.
 *
 * Build/run: make -C src test-painters-terrain-levels
 */
#include "painters/painters.h"
/* For scenery_sort_ready_batch — the reference batch order is unit-tested on
 * the helper directly (see test_ready_batch_sorts_by_far_corner). */
#include "painters/painters_i.h"

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

#define SCENE 8
#define LEVELS 4

/* The flag bit, restated rather than included: this module must not depend on
 * the cache headers, and a test that quietly followed a changed constant would
 * stop testing the thing it names. */
#define TEST_FLOFLAG_VIS_BELOW 0x08

/*
 * The world builder's VisBelow pass, reduced to the one column it acts on:
 * set the draw level, touch nothing else. RSCache_MapFloorVisBelowDrawLevel
 * with no bridge collapses to "0 when flagged, own level otherwise".
 */
static void
apply_vis_below_for_column(
    struct Painter* painter,
    int sx,
    int sz,
    const unsigned char* settings)
{
    int level;

    for( level = 0; level < LEVELS; level++ )
    {
        int draw = (settings[level] & TEST_FLOFLAG_VIS_BELOW) != 0 ? 0 : level;
        painter_tile_set_draw_level(painter, sx, sz, level, draw);
    }
}

static struct Painter*
make_painter(void)
{
    struct Painter* p = painter_new(SCENE, SCENE, LEVELS, PAINTER_NEW_CTX_BUCKET);
    assert(p && "painter_new");
    painter_set_draw_distance(p, SCENE);
    return p;
}

/* Count terrain commands for one tile at one mesh level, and record the order
 * each landed at so relative order can be asserted. */
static int
terrain_emits(
    struct PaintersBuffer* buf,
    int sx,
    int sz,
    int mesh_level,
    int* first_index)
{
    int n = 0;
    int i;
    if( first_index )
        *first_index = -1;
    for( i = 0; i < buf->command_count; i++ )
    {
        struct PaintersElementCommand* c = &buf->commands[i];
        if( c->_bf_kind != PNTR_CMD_TERRAIN )
            continue;
        if( (int)c->_terrain._bf_terrain_x != sx || (int)c->_terrain._bf_terrain_z != sz )
            continue;
        if( (int)c->_terrain._bf_terrain_y != mesh_level )
            continue;
        if( first_index && *first_index < 0 )
            *first_index = i;
        n++;
    }
    return n;
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
        if( (int)c->_entity._bf_entity != entity )
            continue;
        if( first_index && *first_index < 0 )
            *first_index = i;
        n++;
    }
    return n;
}

/*
 * The world builder's LINK_BELOW shuffle, reduced to one column: every grid
 * level takes the tile from the level above, the original level-0 tile is
 * parked at level 3, and level 0 links to it as its underpass.
 * (world_builder.c, the RSCACHE_FLOFLAG_LINK_BELOW pass.)
 */
static void
apply_link_below_for_column(
    struct Painter* painter,
    int sx,
    int sz)
{
    struct PaintersTile parked = *painter_tile_at(painter, sx, sz, 0);
    int level;

    for( level = 0; level < LEVELS - 1; level++ )
        painter_tile_copyto(painter, sx, sz, level + 1, sx, sz, level);

    *painter_tile_at(painter, sx, sz, 3) = parked;
    painters_tile_set_paintgrid_level(painter_tile_at(painter, sx, sz, 3), 3);
    painter_tile_set_bridge(painter, sx, sz, 0, sx, sz, 3);

    /*
     * The draw-level pass is part of the same LINK_BELOW handling and must not
     * be left out: `painter_tile_copyto` deliberately does NOT move
     * visible_gte_level, so without this the pushed-down deck keeps the source
     * level's value and a level-0 mask hides the tile it just became. The
     * builder recomputes it for every grid level from the SOURCE level's
     * settings — `src = g < 3 ? g + 1 : 0` under a bridge — which is what puts
     * the deck at draw level 0.
     */
    for( level = 0; level < LEVELS; level++ )
    {
        int src = level < 3 ? level + 1 : 0;
        int draw = (src > 0) ? src - 1 : src; /* link_below, no VisBelow here */
        painter_tile_set_draw_level(painter, sx, sz, level, draw);
    }
}

/*
 * A bridge deck's ground decor must ride the push-down with everything else on
 * its tile.
 *
 * Both references move the whole tile record rather than its parts —
 * Client-TS `World.pushDown` reassigns the `Square` objects (which own
 * groundDecor, walls, locs and ground), and `class112.method3884` relinks the
 * per-tile arrays wholesale. Ours is `painter_tile_copyto`, which copies the
 * struct. A decor left behind on the level it came from would be drawn at the
 * wrong depth, or under a mask that hides it entirely — the same class of
 * defect VIS_BELOW had. 45,536 ground-decor locs sit on LINK_BELOW columns in
 * this cache, so this is the common case, not a corner.
 */
static void
test_link_below_carries_ground_decor(void)
{
    struct Painter* p = make_painter();
    struct PaintersBuffer* buf = painter_buffer_new();
    const int deck_entity = 4242;
    int deck_index;

    printf("a bridge push-down carries the deck's ground decor down with it\n");

    /* Decor on cache level 1 — the deck — before the shuffle. */
    painter_add_ground_decor(p, 4, 4, 1, deck_entity);
    apply_link_below_for_column(p, 4, 4);

    expect(painter_tile_at(p, 4, 4, 0)->ground_decor >= 0,
           "grid level 0 now holds a ground decor");
    expect(painter_tile_at(p, 4, 4, 1)->ground_decor < 0,
           "and cache level 1's slot no longer does");

    /* And it reaches the command stream: the deck is what the player walks on,
     * so a mask showing only level 0 must still draw it. */
    painter_set_level_mask(p, 0x1);
    painter_paint_bucket(p, buf, 4, 4, 0);
    expect(entity_emits(buf, deck_entity, &deck_index) == 1,
           "the decor is emitted exactly once, from the pushed-down tile");

    free(buf->commands);
    free(buf);
    painter_free(p);
}

/*
 * The parked underpass tile draws ground, wall and scenery — and NOT decor.
 *
 * `class112.java:792-812` is the whole of the reference's linked-tile block:
 * the ground (flags 0x100/0x400), the wall (0x4000) and the scenery loop over
 * `field1685`. Ground decor and wall decor are absent from it. This is easy to
 * "fix" into a divergence by assuming everything on a tile should be drawn, so
 * it is asserted rather than left to a reader.
 */
static void
test_bridge_underpass_draws_no_decor(void)
{
    struct Painter* p = make_painter();
    struct PaintersBuffer* buf = painter_buffer_new();
    const int under_entity = 777;

    printf("the parked underpass tile does not draw its ground decor\n");

    /* Decor on the original level 0 — what ends up parked under the deck. */
    painter_add_ground_decor(p, 4, 4, 0, under_entity);
    apply_link_below_for_column(p, 4, 4);

    painter_set_level_mask(p, 0x1);
    painter_paint_bucket(p, buf, 4, 4, 0);

    expect(entity_emits(buf, under_entity, NULL) == 0,
           "the underpass decor is not emitted (reference class112:792-812)");

    free(buf->commands);
    free(buf);
    painter_free(p);
}

/*
 * VIS_BELOW lowers the whole TILE's draw level, so everything standing on that
 * tile — ground decor included — is revealed with it.
 *
 * The reference has no per-feature draw level: `method4161` answers per tile
 * (flag 0x40), and `method4241`'s mark gate tests that one value before any of
 * the tile's contents are considered. Ours is the same shape —
 * `tile_excluded_by_bridge_or_draw_mask` gates the tile — and this pins it, so
 * a future per-feature level cannot creep in unnoticed.
 */
static void
test_vis_below_reveals_the_tiles_ground_decor(void)
{
    struct Painter* p = make_painter();
    struct PaintersBuffer* buf = painter_buffer_new();
    unsigned char settings[LEVELS] = { 0, TEST_FLOFLAG_VIS_BELOW, 0, 0 };
    const int flagged_entity = 555;
    const int plain_entity = 556;

    printf("VisBelow reveals the flagged tile's ground decor, not just its mesh\n");

    painter_add_ground_decor(p, 4, 4, 1, flagged_entity);
    apply_vis_below_for_column(p, 4, 4, settings);
    /* An unflagged neighbour's level-1 decor is the control. */
    painter_add_ground_decor(p, 6, 4, 1, plain_entity);

    painter_set_level_mask(p, 0x1);
    painter_paint_bucket(p, buf, 4, 4, 0);

    expect(entity_emits(buf, flagged_entity, NULL) == 1,
           "the flagged tile's decor draws under a level-0-only mask");
    expect(entity_emits(buf, plain_entity, NULL) == 0,
           "an unflagged neighbour's level-1 decor stays masked away");

    free(buf->commands);
    free(buf);
    painter_free(p);
}

/*
 * The Inferno glyph-wall case, docs/ORANGE_WEDGE.md §24: a multi-tile loc
 * must NOT hold the static 1x1 locs on its own farther footprint tiles
 * hostage.
 *
 * Every multi-tile loc is registered PNTR_SCENERY_STACK_BASE (the builder's
 * size>1 rule, world_scenery.u.c), and scenery_blocked_by_stack_base used to
 * defer any contained co-tile scenery until the base drew. For static cache
 * locs that inverted the paint order: the glyph wall (3x6) emitted at its
 * NEAREST footprint tile's pop, and only then were the fifteen
 * `inferno_floor_small_plane_01` planes on its farther footprint tiles
 * released — farther geometry painting after a tall near wall paints OVER it.
 * The reference has no loc-vs-loc containment: a loc draws when its own
 * footprint grounds are down.
 *
 * The containment rule stays for DYNAMIC elements — an obj stack dropped on
 * a table must still draw after the table under it.
 */
static void
test_stack_base_does_not_defer_static_locs(void)
{
    struct Painter* p = make_painter();
    struct PaintersBuffer* buf = painter_buffer_new();
    const int wall_entity = 900;   /* 3x5 STACK_BASE at (3,3)..(5,7) */
    const int far_plane_a = 901;   /* 1x1 on the wall's farthest row */
    const int far_plane_b = 902;   /* 1x1 mid-footprint */
    const int dropped_obj = 903;   /* DYNAMIC 1x1 on the far row */
    int wall_i, a_i, b_i, obj_i;

    printf("a multi-tile loc does not defer the 1x1 locs on its own footprint\n");

    painter_add_normal_scenery_ex(p, 3, 3, 0, wall_entity, 3, 5, 0, PNTR_SCENERY_STACK_BASE);
    painter_add_normal_scenery_ex(p, 3, 7, 0, far_plane_a, 1, 1, 0, 0);
    painter_add_normal_scenery_ex(p, 5, 6, 0, far_plane_b, 1, 1, 0, 0);
    painter_mark_static_count(p);
    /* Runtime add on the far row: the reference makes NO static/dynamic
     * distinction — a 1x1 draws at its own tile's pop, before a big loc
     * sharing the tile becomes ready (class112 label614 skips only the
     * unready entity, never the pass). */
    painter_add_normal_scenery_ex(p, 3, 7, 0, dropped_obj, 1, 1, 0, 0);

    /* Camera south of the wall: rows 7 and 6 are its farther footprint, and
     * the wall becomes ready only at its NEAREST footprint row's pop. */
    painter_paint_bucket(p, buf, 4, 1, 0);

    expect(entity_emits(buf, wall_entity, &wall_i) == 1, "the wall emits once");
    expect(entity_emits(buf, far_plane_a, &a_i) == 1, "far plane A emits once");
    expect(entity_emits(buf, far_plane_b, &b_i) == 1, "far plane B emits once");
    expect(entity_emits(buf, dropped_obj, &obj_i) == 1, "the dropped obj emits once");
    if( wall_i >= 0 && a_i >= 0 && b_i >= 0 )
    {
        expect(a_i < wall_i, "farthest-row plane draws BEFORE the wall (reference order)");
        expect(b_i < wall_i, "mid-footprint plane draws BEFORE the wall");
    }
    if( wall_i >= 0 && obj_i >= 0 )
        expect(obj_i < wall_i, "the dynamic 1x1 draws before the wall too — no containment");

    free(buf->commands);
    free(buf);
    painter_free(p);
}

/*
 * The reference emission sort itself (class112.java:1030-1058 + method3971):
 * the entity whose footprint reaches FARTHEST from the camera draws first;
 * exact ties break toward the farther centre. Production pays it ONCE per
 * tile per paint by relinking the tile's chain (scenery_chain_sort_once) —
 * tested here directly on one shared tile, including the once-per-paint
 * latch and that each node's span rides with its element.
 */
static void
test_ready_batch_sorts_by_far_corner(void)
{
    struct Painter* p = make_painter();
    /* All three share tile (4,4): the wall covers it, the 1x1s sit on it. */
    int wall = painter_add_normal_scenery_ex(p, 2, 4, 0, 910, 3, 4, 0, 0); /* far corner z=7 */
    int near_1x1 = painter_add_normal_scenery_ex(p, 4, 4, 0, 911, 1, 1, 0, 0);
    struct PaintersTile* tile = painter_tile_at(p, 4, 4, 0);
    struct TilePaint tp = { 0 };
    int order[3];
    int count = 0;
    uint8_t span_of_wall_before = 0;
    uint8_t span_of_wall_after = 0;

    printf("the chain sorts farthest-corner-first, once per paint, spans riding along\n");

    for( int32_t sn = tile->scenery_head; sn != -1; sn = p->scenery_pool[sn].next )
        if( p->scenery_pool[sn].element_idx == wall )
            span_of_wall_before = p->scenery_pool[sn].span;

    /* Camera (4,1): keys — wall max(4-2,4-4)+max(1-4,7-1)=2+6=8;
     * near_1x1 0+3=3. Wall must come first. */
    scenery_chain_sort_once(p, tile, &tp, 4, 1);
    for( int32_t sn = tile->scenery_head; sn != -1 && count < 3;
         sn = p->scenery_pool[sn].next )
    {
        order[count++] = p->scenery_pool[sn].element_idx;
        if( p->scenery_pool[sn].element_idx == wall )
            span_of_wall_after = p->scenery_pool[sn].span;
    }
    expect(count == 2, "both elements still on the chain");
    expect(order[0] == wall, "the wall (farthest corner) first");
    expect(order[1] == near_1x1, "the near 1x1 after it");
    expect(span_of_wall_after == span_of_wall_before, "the wall's span moved with it");
    expect(tp.scenery_sorted == 1, "the once-per-paint latch is set");
    /* Idempotence: reverse the chain payloads by hand, call again — the latch
     * must make it a no-op. */
    {
        int32_t head = tile->scenery_head;
        int16_t e0 = p->scenery_pool[head].element_idx;
        p->scenery_pool[head].element_idx =
            p->scenery_pool[p->scenery_pool[head].next].element_idx;
        p->scenery_pool[p->scenery_pool[head].next].element_idx = e0;
    }
    scenery_chain_sort_once(p, tile, &tp, 4, 1);
    expect((int)p->scenery_pool[tile->scenery_head].element_idx == near_1x1,
           "second call is a no-op — no re-sort within one paint");

    painter_free(p);
}

/*
 * The default: with no flags, every level emits exactly its own mesh and
 * nothing else. Establishes that the set is initialised, not merely respected.
 */
static void
test_default_is_own_mesh_only(void)
{
    struct Painter* p = make_painter();
    int level;

    printf("a tile with no flags owns exactly its own mesh\n");
    for( level = 0; level < LEVELS; level++ )
    {
        unsigned got = painter_tile_get_terrain_levels(p, 3, 3, level);
        char msg[96];
        snprintf(msg, sizeof(msg), "level %d owns only mesh %d (0x%x)", level, level, got);
        expect(got == (1u << level), msg);
    }
    painter_free(p);
}

/*
 * VisBelow does not move geometry: the flagged level keeps its own mesh, the
 * level below gains nothing, and only the draw level changes.
 */
static void
test_vis_below_leaves_the_mesh_in_place(void)
{
    struct Painter* p = make_painter();
    unsigned char settings[LEVELS] = { 0, TEST_FLOFLAG_VIS_BELOW, 0, 0 };

    printf("VisBelow lowers the draw level and leaves every mesh in place\n");
    apply_vis_below_for_column(p, 4, 4, settings);

    expect(painter_tile_get_terrain_levels(p, 4, 4, 1) == 0x2u,
           "the flagged level still owns its own mesh");
    expect(painter_tile_get_terrain_levels(p, 4, 4, 0) == 0x1u,
           "level 0 owns only its own mesh");
    expect(painter_tile_get_terrain_levels(p, 4, 4, 2) == 0x4u,
           "an unflagged level is untouched");
    painter_free(p);
}

/*
 * The flagged mesh draws from its OWN level's traversal slot, after the level
 * below it — the reference pops a plane-1 tile only once the plane-0 tile has
 * fully retired (walls and all), so the floor above paints over what is below.
 */
static void
test_emission_is_from_own_level_and_ordered(void)
{
    struct Painter* p = make_painter();
    struct PaintersBuffer* buf = painter_buffer_new();
    unsigned char settings[LEVELS] = { 0, TEST_FLOFLAG_VIS_BELOW, 0, 0 };
    int i0 = -1, i1 = -1;
    int n0, n1;

    printf("the flagged mesh is emitted from its own level, after the one below\n");
    apply_vis_below_for_column(p, 4, 4, settings);

    painter_paint_bucket(p, buf, 4, 4, 0);

    n0 = terrain_emits(buf, 4, 4, 0, &i0);
    n1 = terrain_emits(buf, 4, 4, 1, &i1);

    expect(n0 == 1, "mesh 0 is emitted exactly once");
    expect(n1 == 1, "mesh 1 is emitted exactly once");
    if( n0 == 1 && n1 == 1 )
        expect(i0 < i1, "mesh 0 first, mesh 1 after — the floor above draws on top");

    free(buf->commands);
    free(buf);
    painter_free(p);
}

/*
 * A tile whose set is empty emits no terrain at all. This is the state the
 * world builder leaves a mesh-less level in, and "emits nothing" has to be
 * representable — falling back to the tile's own mesh level would emit
 * commands for geometry that does not exist.
 */
static void
test_empty_set_emits_nothing(void)
{
    struct Painter* p = make_painter();
    struct PaintersBuffer* buf = painter_buffer_new();
    int n;

    printf("an empty terrain set emits no terrain command\n");
    painter_tile_set_terrain_levels(p, 4, 4, 1, 0u);
    painter_paint_bucket(p, buf, 4, 4, 0);

    n = terrain_emits(buf, 4, 4, 1, NULL);
    expect(n == 0, "no command for the emptied level");

    free(buf->commands);
    free(buf);
    painter_free(p);
}

/* Depth mode collects a visible set rather than deriving correctness from the
 * camera-relative painter wavefront. Multi-tile scenery must still be emitted
 * once. Through-wall decor remains a directional model choice rather than an
 * occlusion-order choice, so its camera-facing alternative is preserved. */
static void
test_depth_collector_emits_visible_models_once(void)
{
    struct Painter* p = make_painter();
    struct PaintersBuffer* buf = painter_buffer_new();
    const int wall_a = 1200;
    const int wall_b = 1201;
    const int decor_a = 1202;
    const int decor_b = 1203;
    const int scenery = 1204;

    printf("depth collection emits the visible model set without painter ordering\n");
    painter_add_wall(p, 4, 4, 0, wall_a, WALL_A, WALL_SIDE_WEST);
    painter_add_wall(p, 4, 4, 0, wall_b, WALL_B, WALL_SIDE_EAST);
    painter_add_wall_decor(
        p, 4, 4, 0, decor_a, WALL_A, WALL_CORNER_NORTHWEST, THROUGHWALL, 64);
    painter_add_wall_decor(
        p, 4, 4, 0, decor_b, WALL_B, WALL_CORNER_SOUTHEAST, 0, 64);
    painter_add_normal_scenery_ex(p, 3, 3, 0, scenery, 2, 2, 64, 0);

    painter_collect_visible_depth(p, buf, 4, 2, 0);
    expect(entity_emits(buf, wall_a, NULL) == 1, "depth collector keeps wall A once");
    expect(entity_emits(buf, wall_b, NULL) == 1, "depth collector keeps wall B once");
    expect(entity_emits(buf, decor_a, NULL) == 0,
           "south camera omits the non-facing through-wall alternative");
    expect(entity_emits(buf, decor_b, NULL) == 1,
           "south camera keeps through-wall decor B once");
    expect(entity_emits(buf, scenery, NULL) == 1,
           "depth collector deduplicates multi-tile scenery");

    /* Moving across the tile does not alter ordinary geometry, but must flip
     * the explicitly directional through-wall model. */
    painter_collect_visible_depth(p, buf, 4, 6, 0);
    expect(entity_emits(buf, wall_a, NULL) == 1,
           "opposite camera keeps the same wall A set");
    expect(entity_emits(buf, wall_b, NULL) == 1,
           "opposite camera keeps the same wall B set");
    expect(entity_emits(buf, decor_a, NULL) == 1,
           "north camera selects decor A");
    expect(entity_emits(buf, decor_b, NULL) == 0,
           "north camera omits decor B");
    expect(entity_emits(buf, scenery, NULL) == 1,
           "opposite camera still emits scenery once");

    free(buf->commands);
    free(buf);
    painter_free(p);
}

/*
 * The discriminating case, and the one with a real client behaviour behind it:
 * roof hiding and the level mask draw level 0 only, and a VisBelow tile must
 * still be drawn there. The reference gets this from the mark gate testing
 * renderLevel (method4241: `renderLevel <= topLevel`), not the tile's physical
 * plane; ours is tile_excluded_by_bridge_or_draw_mask testing
 * visible_gte_level. No geometry has to move for this to work.
 */
static void
test_flagged_tile_survives_a_level_mask(void)
{
    struct Painter* p = make_painter();
    struct PaintersBuffer* buf = painter_buffer_new();
    unsigned char settings[LEVELS] = { 0, TEST_FLOFLAG_VIS_BELOW, 0, 0 };
    int i0 = -1, i1 = -1;

    printf("the flagged tile still draws when only level 0 is drawn\n");
    apply_vis_below_for_column(p, 4, 4, settings);
    painter_set_level_mask(p, 0x1);

    painter_paint_bucket(p, buf, 4, 4, 0);

    expect(terrain_emits(buf, 4, 4, 0, &i0) == 1, "mesh 0 is drawn");
    expect(terrain_emits(buf, 4, 4, 1, &i1) == 1,
           "mesh 1 is drawn too, even though level 1 is masked off");
    if( i0 >= 0 && i1 >= 0 )
        expect(i0 < i1, "and still after mesh 0 — order does not change under the mask");
    /* A neighbour without the flag keeps the ordinary behaviour: its upper mesh
     * is masked away. Otherwise "everything draws" would pass this too. */
    expect(terrain_emits(buf, 6, 4, 1, NULL) == 0,
           "an unflagged neighbour's mesh 1 is still masked away");

    free(buf->commands);
    free(buf);
    painter_free(p);
}

/*
 * The bucket drain is one globally distance-ordered sweep: terrain comes out
 * farthest-first for the WHOLE box, so all four quadrants advance toward the
 * eye together.
 *
 * The failure this pins is not subtle once measured but invisible in a
 * screenshot: if the setup pass stops bulk-pushing ready tiles and lets the
 * perimeter seed generator drive instead, the first seed's wave floods its
 * entire quadrant (every ground dependency points outward, into the same
 * quadrant) before the queue drains and the next seed is taken. The box then
 * paints one corner at a time and the distance sequence restarts at ~2R three
 * times — a near tile of the first quadrant emitted ahead of a far tile of the
 * next. See docs/painter_bucket_vs_world3d.md, "Why the bulk push".
 */
static void
test_bucket_emits_one_globally_distance_ordered_sweep(void)
{
#define ORDER_SCENE 24
#define ORDER_CAM 12
    struct Painter* p = painter_new(ORDER_SCENE, ORDER_SCENE, LEVELS, PAINTER_NEW_CTX_BUCKET);
    struct PaintersBuffer* buf = painter_buffer_new();
    int prev = -1;
    int runs = 1;
    int emitted = 0;
    int x, z;

    printf("the bucket drain is one distance-ordered sweep, not one corner at a time\n");
    painter_set_draw_distance(p, ORDER_SCENE);
    for( x = 0; x < ORDER_SCENE; x++ )
        for( z = 0; z < ORDER_SCENE; z++ )
        {
            painter_tile_set_terrain_levels(p, x, z, 0, 1u << 0);
            painter_tile_set_terrain_levels(p, x, z, 1, 0);
            painter_tile_set_terrain_levels(p, x, z, 2, 0);
            painter_tile_set_terrain_levels(p, x, z, 3, 0);
        }

    painter_paint_bucket(p, buf, ORDER_CAM, ORDER_CAM, 0);

    for( int i = 0; i < buf->command_count; i++ )
    {
        int tx, tz, d;
        if( buf->commands[i]._bf_kind != PNTR_CMD_TERRAIN )
            continue;
        tx = (int)buf->commands[i]._terrain._bf_terrain_x;
        tz = (int)buf->commands[i]._terrain._bf_terrain_z;
        d = abs(tx - ORDER_CAM) + abs(tz - ORDER_CAM);
        if( prev >= 0 && d > prev )
            runs++;
        prev = d;
        emitted++;
    }

    expect(emitted > 0, "the box emitted terrain at all");
    expect(runs == 1, "terrain distance never increases — a single farthest-first sweep");
    if( runs != 1 )
        printf("       (%d monotone runs over %d tiles: the box painted %d corners in turn)\n",
               runs, emitted, runs);

    free(buf->commands);
    free(buf);
    painter_free(p);
#undef ORDER_SCENE
#undef ORDER_CAM
}

/*
 * The QBD arena seam, docs/qbd_toridraw_streaks_debug.md "platform strip".
 *
 * The arena floor is two 12x18 plane-0 locs that meet on one column, and the
 * camera can come to rest on that column. A tile with `sx == camera_sx` is
 * gated on BOTH horizontal neighbours (the reference's `x <= cameraX` and
 * `x >= cameraX` are both true there), and the neighbour across the seam
 * belongs to the OTHER loc — so this tile carries no span flag in that
 * direction and the reference span exception cannot fire. It therefore waits
 * for that neighbour to reach PAINT_STEP_DONE, which cannot happen until the
 * neighbour's 216-tile loc is released, which happens at the loc's NEAREST
 * footprint tile, a handful of tiles from the eye.
 *
 * The whole seam column is consequently held until the drain is almost at the
 * eye, and its floor — twenty tiles away — is then emitted on top of a loc
 * that was drawn at distance 5. On screen that is a one-tile-wide strip of
 * ground running up over the platform.
 *
 * What makes the wait unnecessary: the blocking loc reaches CLOSER to the eye
 * than the tile being held, so it is drawn nearer than that tile regardless —
 * and it lies BESIDE the seam column's line of sight, not behind it (the
 * lateral gate, bucket_gate_blocks). The two floor tiles diagonally in front
 * of each loc's near corner, (13,7) and (19,7), are a different case: the loc
 * is behind them along the view ray, so the reference order (loc first, then
 * their floor) is the right one, and the sweep is allowed to dip there.
 */
static void
test_seam_between_two_large_locs_keeps_the_sweep(void)
{
#define SEAM_SCENE 32
#define SEAM_CAM_X 16
#define SEAM_CAM_Z 4
    struct Painter* p = painter_new(SEAM_SCENE, SEAM_SCENE, LEVELS, PAINTER_NEW_CTX_BUCKET);
    struct PaintersBuffer* buf = painter_buffer_new();
    const int west_loc = 1300;
    const int east_loc = 1301;
    int east_i = -1;
    int emitted = 0;
    int seam_after_east = 0;
    int late_beside_or_behind = 0;
    int x, z;

    printf("a floor column on the seam of two large locs still sweeps farthest-first\n");
    painter_set_draw_distance(p, SEAM_SCENE);
    for( x = 0; x < SEAM_SCENE; x++ )
        for( z = 0; z < SEAM_SCENE; z++ )
        {
            painter_tile_set_terrain_levels(p, x, z, 0, 1u << 0);
            painter_tile_set_terrain_levels(p, x, z, 1, 0);
            painter_tile_set_terrain_levels(p, x, z, 2, 0);
            painter_tile_set_terrain_levels(p, x, z, 3, 0);
        }

    /* x[8,16] and x[17,25], both z[8,23]: the seam falls on the camera column
     * and neither loc covers a tile of the other. */
    painter_add_normal_scenery_ex(p, 8, 8, 0, west_loc, 9, 16, 0, PNTR_SCENERY_STACK_BASE);
    painter_add_normal_scenery_ex(p, 17, 8, 0, east_loc, 9, 16, 0, PNTR_SCENERY_STACK_BASE);

    painter_paint_bucket(p, buf, SEAM_CAM_X, SEAM_CAM_Z, 0);

    entity_emits(buf, east_loc, &east_i);
    for( int i = 0; i < buf->command_count; i++ )
    {
        int tx, tz, d;
        if( buf->commands[i]._bf_kind != PNTR_CMD_TERRAIN )
            continue;
        tx = (int)buf->commands[i]._terrain._bf_terrain_x;
        tz = (int)buf->commands[i]._terrain._bf_terrain_z;
        d = abs(tx - SEAM_CAM_X) + abs(tz - SEAM_CAM_Z);
        emitted++;
        if( east_i < 0 || i < east_i )
            continue;
        /* The defect: the seam column's own floor, under the west loc and
         * farther out than the east loc's release ring (5), emitted after
         * it. The two seam tiles at rings 4 and 5 are nearer than or level
         * with the loc and rightly follow it. */
        if( tx == SEAM_CAM_X && tz >= 8 && tz <= 23 && d > 5 )
            seam_after_east++;
        /* Anything farther than the east loc's release ring (5) that is
         * beside or behind the locs' rows. Rows south of the locs (tz < 8)
         * are in front of them along the view ray and may legitimately wait. */
        if( d > 5 && tz >= 8 )
        {
            late_beside_or_behind++;
            printf("       (floor (%d,%d) d=%d emitted after the east loc)\n", tx, tz, d);
        }
    }

    expect(emitted > 0, "the box emitted terrain at all");
    expect(east_i >= 0, "the east loc is emitted");
    expect(seam_after_east == 0, "the seam column's floor all precedes the east loc");
    if( seam_after_east )
        printf("       (%d seam tiles ran late)\n", seam_after_east);
    expect(late_beside_or_behind == 0,
           "no floor beside or behind the locs, farther than the east loc's ring, follows it");

    free(buf->commands);
    free(buf);
    painter_free(p);
#undef SEAM_SCENE
#undef SEAM_CAM_X
#undef SEAM_CAM_Z
}

/*
 * The other half of the seam exception's contract (2026-08-19, ToB Xarpus /
 * Maiden): a large loc that sits BEHIND a tile along the view ray must still
 * be emitted before that tile's ground, however near the loc's nearest corner
 * reaches in Manhattan terms.
 *
 * Eye at (16,4) looking up +z. A 6x5 loc at x[10,15] z[28,32] has its nearest
 * footprint tile (15,28) at ring 25, but the floor directly in front of its
 * z=28 row — (10..13, 27), rings 26..29 — is nearer the eye in depth and sits
 * under the loc on screen. Every floor tile in the loc's x band with z < 28
 * must therefore be emitted AFTER the loc. The first seam exception relaxed
 * those tiles' north gate (the loc "reached nearer"), painted them first, and
 * the loc's tall far part landed on top of them.
 */
static void
test_loc_behind_a_tile_in_depth_is_emitted_first(void)
{
#define BEHIND_SCENE 32
#define BEHIND_CAM_X 16
#define BEHIND_CAM_Z 4
    struct Painter* p = painter_new(BEHIND_SCENE, BEHIND_SCENE, LEVELS, PAINTER_NEW_CTX_BUCKET);
    struct PaintersBuffer* buf = painter_buffer_new();
    const int ledge = 1400;
    int ledge_i = -1;
    int floor_before_ledge = 0;
    int floor_in_front = 0;
    int x, z;

    printf("a loc behind a tile along the view ray is emitted before that tile's floor\n");
    painter_set_draw_distance(p, BEHIND_SCENE);
    for( x = 0; x < BEHIND_SCENE; x++ )
        for( z = 0; z < BEHIND_SCENE; z++ )
        {
            painter_tile_set_terrain_levels(p, x, z, 0, 1u << 0);
            painter_tile_set_terrain_levels(p, x, z, 1, 0);
            painter_tile_set_terrain_levels(p, x, z, 2, 0);
            painter_tile_set_terrain_levels(p, x, z, 3, 0);
        }
    painter_add_normal_scenery_ex(p, 10, 28, 0, ledge, 6, 5, 0, PNTR_SCENERY_STACK_BASE);

    painter_paint_bucket(p, buf, BEHIND_CAM_X, BEHIND_CAM_Z, 0);

    expect(entity_emits(buf, ledge, &ledge_i) == 1, "the ledge is emitted exactly once");
    for( int i = 0; i < buf->command_count; i++ )
    {
        int tx, tz;
        if( buf->commands[i]._bf_kind != PNTR_CMD_TERRAIN )
            continue;
        tx = (int)buf->commands[i]._terrain._bf_terrain_x;
        tz = (int)buf->commands[i]._terrain._bf_terrain_z;
        if( tx < 10 || tx > 15 || tz >= 28 || tz < BEHIND_CAM_Z )
            continue;
        floor_in_front++;
        if( ledge_i >= 0 && i < ledge_i )
            floor_before_ledge++;
    }
    expect(floor_in_front > 0, "the box emitted the floor in front of the ledge");
    expect(floor_before_ledge == 0, "no floor in front of the ledge is emitted before it");
    if( floor_before_ledge )
        printf("       (%d of %d floor tiles in front of the ledge emitted before it)\n",
               floor_before_ledge, floor_in_front);

    free(buf->commands);
    free(buf);
    painter_free(p);
#undef BEHIND_SCENE
#undef BEHIND_CAM_X
#undef BEHIND_CAM_Z
}

int
main(void)
{
    test_default_is_own_mesh_only();
    test_vis_below_leaves_the_mesh_in_place();
    test_emission_is_from_own_level_and_ordered();
    test_flagged_tile_survives_a_level_mask();
    test_empty_set_emits_nothing();
    test_depth_collector_emits_visible_models_once();
    test_link_below_carries_ground_decor();
    test_bridge_underpass_draws_no_decor();
    test_vis_below_reveals_the_tiles_ground_decor();
    test_stack_base_does_not_defer_static_locs();
    test_ready_batch_sorts_by_far_corner();
    test_bucket_emits_one_globally_distance_ordered_sweep();
    test_seam_between_two_large_locs_keeps_the_sweep();
    test_loc_behind_a_tile_in_depth_is_emitted_first();

    if( g_failures )
    {
        printf("painters_test_terrain_levels: %d FAILED\n", g_failures);
        return 1;
    }
    printf("OK: painters_test_terrain_levels\n");
    return 0;
}
