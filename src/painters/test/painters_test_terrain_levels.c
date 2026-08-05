/**
 * Unit tests for PaintersTile::terrain_levels — the set of cache-level terrain
 * meshes a tile's ground pass emits.
 *
 * Why this lives in the painters module even though the flag it models is a
 * *world* concept: the painter is what decides WHEN a mesh is emitted, and that
 * is the whole bug this exists for. VisBelow terrain has a real colour and a
 * real position; drawing it during its own level's pass simply puts it at the
 * wrong point in the back-to-front order. So the tests set the tile state a
 * world build would produce — by hand, with no cache and no world builder — and
 * assert on the command stream.
 *
 * What the world builder does, restated here so the simulation is checkable
 * against something (world_builder.c, the RSCACHE_FLOFLAG_VIS_BELOW pass):
 *
 *   for each column:
 *     every grid level starts owning exactly its own mesh:  1 << src(level)
 *     a level whose settings carry VIS_BELOW, and whose draw level is therefore
 *     not itself, hands its mesh to the draw level and keeps none:
 *         set[own]  &= ~(1 << mesh)
 *         set[draw] |=  (1 << mesh)
 *
 * Build/run: make -C src test-painters-terrain-levels
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

#define SCENE 8
#define LEVELS 4

/* The flag bit, restated rather than included: this module must not depend on
 * the cache headers, and a test that quietly followed a changed constant would
 * stop testing the thing it names. */
#define TEST_FLOFLAG_VIS_BELOW 0x08

/*
 * The world builder's VisBelow pass, reduced to the one column it acts on.
 * `settings[level]` is the raw floor settings byte for that cache level.
 */
static void
apply_vis_below_for_column(
    struct Painter* painter,
    int sx,
    int sz,
    const unsigned char* settings)
{
    unsigned set[LEVELS];
    int level;

    for( level = 0; level < LEVELS; level++ )
        set[level] = 1u << level;

    for( level = 0; level < LEVELS; level++ )
    {
        if( (settings[level] & TEST_FLOFLAG_VIS_BELOW) == 0 )
            continue;
        /* RSCache_MapFloorVisBelowDrawLevel: VisBelow draws on level 0. */
        if( level == 0 )
            continue;
        set[level] &= ~(1u << level);
        set[0] |= 1u << level;
    }

    for( level = 0; level < LEVELS; level++ )
        painter_tile_set_terrain_levels(painter, sx, sz, level, set[level]);
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
 * each landed at so adjacency can be asserted. */
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
 * The move itself, at the level the world builder works on: a VisBelow level
 * keeps nothing and level 0 gains its mesh.
 */
static void
test_vis_below_moves_the_mesh_down(void)
{
    struct Painter* p = make_painter();
    unsigned char settings[LEVELS] = { 0, TEST_FLOFLAG_VIS_BELOW, 0, 0 };

    printf("VisBelow on level 1 moves that mesh onto level 0\n");
    apply_vis_below_for_column(p, 4, 4, settings);

    expect(painter_tile_get_terrain_levels(p, 4, 4, 1) == 0u,
           "the flagged level emits nothing of its own");
    expect(painter_tile_get_terrain_levels(p, 4, 4, 0) == 0x3u,
           "level 0 emits both mesh 0 and mesh 1");
    expect(painter_tile_get_terrain_levels(p, 4, 4, 2) == 0x4u,
           "an unflagged level is untouched");

    /* A column with the flag on two levels hands both down. */
    {
        unsigned char two[LEVELS] = { 0, TEST_FLOFLAG_VIS_BELOW, TEST_FLOFLAG_VIS_BELOW, 0 };
        apply_vis_below_for_column(p, 5, 4, two);
        expect(painter_tile_get_terrain_levels(p, 5, 4, 0) == 0x7u,
               "two flagged levels both land on level 0");
        expect(painter_tile_get_terrain_levels(p, 5, 4, 1) == 0u &&
                   painter_tile_get_terrain_levels(p, 5, 4, 2) == 0u,
               "and neither keeps a mesh");
    }
    painter_free(p);
}

/*
 * The two meshes must reach the command stream adjacently, during one ground
 * pass, with the borrowed mesh second so it lands on top.
 *
 * Read this one for what it is: a regression guard, not a proof. The bucket
 * painter already walks a column's levels back to back (a level-N tile waits on
 * level N-1 and is pushed at the same distance), so this passes with or without
 * the mask — a mutant that ignored terrain_levels entirely still satisfied it.
 * test_borrowed_mesh_survives_a_level_mask below is the discriminating one.
 */
static void
test_emission_is_adjacent_and_ordered(void)
{
    struct Painter* p = make_painter();
    struct PaintersBuffer* buf = painter_buffer_new();
    unsigned char settings[LEVELS] = { 0, TEST_FLOFLAG_VIS_BELOW, 0, 0 };
    int i0 = -1, i1 = -1;
    int n0, n1;

    printf("the borrowed mesh is emitted with the level it moved onto\n");
    apply_vis_below_for_column(p, 4, 4, settings);

    painter_paint_bucket(p, buf, 4, 4, 0);

    n0 = terrain_emits(buf, 4, 4, 0, &i0);
    n1 = terrain_emits(buf, 4, 4, 1, &i1);

    expect(n0 == 1, "mesh 0 is emitted exactly once");
    expect(n1 == 1, "mesh 1 is emitted exactly once (from level 0's pass)");
    if( n0 == 1 && n1 == 1 )
    {
        expect(i1 == i0 + 1, "the two are back to back in the command stream");
        expect(i0 < i1, "and ascending, so the borrowed mesh draws on top");
    }

    free(buf->commands);
    free(buf);
    painter_free(p);
}

/*
 * A tile whose set is empty emits no terrain at all. This is the state a
 * VisBelow level is left in, and "emits nothing" has to be representable —
 * falling back to the tile's own mesh level would silently undo the move.
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
 * VisBelow terrain belongs to level 0's picture, so it must still be drawn when
 * the upper levels are not being drawn at all. Roof hiding and the level mask
 * both do exactly that — draw level 0 only — and a mesh that is emitted from
 * its own level's pass simply vanishes there.
 *
 * This is what a mask-ignoring implementation cannot pass: with the mask honoured
 * mesh 1 arrives from level 0's pass; without it, mesh 1 is never emitted.
 */
static void
test_borrowed_mesh_survives_a_level_mask(void)
{
    struct Painter* p = make_painter();
    struct PaintersBuffer* buf = painter_buffer_new();
    unsigned char settings[LEVELS] = { 0, TEST_FLOFLAG_VIS_BELOW, 0, 0 };

    printf("the borrowed mesh still draws when only level 0 is drawn\n");
    apply_vis_below_for_column(p, 4, 4, settings);
    painter_set_level_mask(p, 0x1);

    painter_paint_bucket(p, buf, 4, 4, 0);

    expect(terrain_emits(buf, 4, 4, 0, NULL) == 1, "mesh 0 is drawn");
    expect(terrain_emits(buf, 4, 4, 1, NULL) == 1,
           "mesh 1 is drawn too, even though level 1 is masked off");
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
    test_vis_below_moves_the_mesh_down();
    test_emission_is_adjacent_and_ordered();
    test_borrowed_mesh_survives_a_level_mask();
    test_empty_set_emits_nothing();

    if( g_failures )
    {
        printf("painters_test_terrain_levels: %d FAILED\n", g_failures);
        return 1;
    }
    printf("OK: painters_test_terrain_levels\n");
    return 0;
}
