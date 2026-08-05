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

    if( g_failures )
    {
        printf("painters_test_terrain_levels: %d FAILED\n", g_failures);
        return 1;
    }
    printf("OK: painters_test_terrain_levels\n");
    return 0;
}
