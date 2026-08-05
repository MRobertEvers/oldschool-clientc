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
 * The reference batch sort itself (class112.java:1030-1058 + method3971):
 * within one ready batch, the entity whose footprint reaches FARTHEST from
 * the camera draws first; exact ties break toward the farther centre. Tested
 * on the helper directly — batches of more than one only form when several
 * entities become ready at the same pop, which a scene-level test cannot
 * stage deterministically.
 */
static void
test_ready_batch_sorts_by_far_corner(void)
{
    struct Painter* p = make_painter();
    int wall = painter_add_normal_scenery_ex(p, 3, 3, 0, 910, 3, 5, 0, 0); /* far corner z=7 */
    int near_1x1 = painter_add_normal_scenery_ex(p, 4, 4, 0, 911, 1, 1, 0, 0);
    int far_1x1 = painter_add_normal_scenery_ex(p, 3, 7, 0, 912, 1, 1, 0, 0);
    int batch[3] = { near_1x1, wall, far_1x1 };

    printf("a ready batch emits farthest-corner-first with the centre tie-break\n");
    /* Camera (4,1): keys — wall max(1,1)+max(-2,6)=7; far_1x1 1+6=7 (tie,
     * farther centre wins); near_1x1 0+3=3. */
    scenery_sort_ready_batch(p, batch, 3, 4, 1);
    expect(batch[0] == far_1x1, "tie broken to the farther centre (the 1x1 on the far corner)");
    expect(batch[1] == wall, "the wall next (same far-corner key)");
    expect(batch[2] == near_1x1, "the near 1x1 last");
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

int
main(void)
{
    test_default_is_own_mesh_only();
    test_vis_below_leaves_the_mesh_in_place();
    test_emission_is_from_own_level_and_ordered();
    test_flagged_tile_survives_a_level_mask();
    test_empty_set_emits_nothing();
    test_link_below_carries_ground_decor();
    test_bridge_underpass_draws_no_decor();
    test_vis_below_reveals_the_tiles_ground_decor();
    test_stack_base_does_not_defer_static_locs();
    test_ready_batch_sorts_by_far_corner();

    if( g_failures )
    {
        printf("painters_test_terrain_levels: %d FAILED\n", g_failures);
        return 1;
    }
    printf("OK: painters_test_terrain_levels\n");
    return 0;
}
