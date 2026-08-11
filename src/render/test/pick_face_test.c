/*
 * Per-face pick parity with the reference render2 pass.
 *
 * Reference: 239 deob class144.method4915:1574-1651 and Client-TS
 * Model.ts:1855-1886 / Model.isMouseRoughlyInsideTriangle:2413.
 *
 * The projection is not exercised here — these tests write screen-space
 * vertices into the scene scratch directly, which is exactly what the pick
 * reads. That keeps each case a statement about the pick rule alone.
 */

#include "toridraw.h"
#include "toridraw_lighting.h"
#include "toridraw_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, ...)                                                                          \
    do                                                                                            \
    {                                                                                             \
        if( !(cond) )                                                                             \
        {                                                                                         \
            g_failures++;                                                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                  \
            fprintf(stderr, __VA_ARGS__);                                                         \
            fprintf(stderr, "\n");                                                                \
        }                                                                                         \
    } while( 0 )

/* One model, one scene, rebuilt per case. Faces index a shared vertex pool the
 * test fills with screen coordinates. */
#define MAX_V 32
#define MAX_F 16

struct Fixture
{
    struct ToriDraw_Scene scene;
    struct ToriDraw_ViewPort view_port;
    struct ToriDraw_Model model;
    struct ToriDraw_ModelHandle hnd;

    int sx[MAX_V];
    int sy[MAX_V];
    int sz[MAX_V];

    faceint_t fa[MAX_F];
    faceint_t fb[MAX_F];
    faceint_t fc[MAX_F];
    hsl16_t colors_c[MAX_F];
};

static struct Fixture g_fx;

/* The viewport centre is non-zero on purpose: the pick is handed absolute
 * screen coordinates and must subtract it before comparing against the
 * centred projection, so a centre of 0 would hide that bug. */
#define CENTER_X 256
#define CENTER_Y 167

static void
fixture_reset(void)
{
    memset(&g_fx, 0, sizeof(g_fx));

    g_fx.scene.screen_vertices_x = g_fx.sx;
    g_fx.scene.screen_vertices_y = g_fx.sy;
    g_fx.scene.screen_vertices_z = g_fx.sz;

    g_fx.view_port.x_center = CENTER_X;
    g_fx.view_port.y_center = CENTER_Y;

    g_fx.model.face_indices_a = g_fx.fa;
    g_fx.model.face_indices_b = g_fx.fb;
    g_fx.model.face_indices_c = g_fx.fc;
    g_fx.model.face_colors_c = g_fx.colors_c;

    g_fx.hnd.kind = TORIDRAWMK_MODEL;
    g_fx.hnd.u.model.model = &g_fx.model;

    /* Wide open: the aabb gate is exercised by its own case, and every other
     * case wants it out of the way. */
    g_fx.scene.aabb.min_screen_x = -100000;
    g_fx.scene.aabb.max_screen_x = 100000;
    g_fx.scene.aabb.min_screen_y = -100000;
    g_fx.scene.aabb.max_screen_y = 100000;
}

/* Screen coordinates are centred (what the projection writes); callers pass
 * the same frame the triangle is described in. */
static int
add_vertex(int x, int y)
{
    int v = g_fx.model.vertex_count++;
    g_fx.sx[v] = x;
    g_fx.sy[v] = y;
    g_fx.sz[v] = 100;
    return v;
}

static int
add_face(int a, int b, int c)
{
    int f = g_fx.model.face_count++;
    g_fx.fa[f] = (faceint_t)a;
    g_fx.fb[f] = (faceint_t)b;
    g_fx.fc[f] = (faceint_t)c;
    g_fx.colors_c[f] = 0; /* lit, visible */
    return f;
}

/* Mouse coordinates are absolute screen; the pick re-centres them itself. */
static bool
pick(int mouse_x, int mouse_y)
{
    return ToriDraw_ProjectedModelMouseHitTest(
        &g_fx.scene, g_fx.hnd, &g_fx.view_port, CENTER_X + mouse_x, CENTER_Y + mouse_y);
}

/* The same click against a ground-tile mesh, which the reference resolves down
 * World3D.drawTileUnderlay / drawTileOverlay rather than Model.draw. */
static bool
pick_tile(int mouse_x, int mouse_y)
{
    return ToriDraw_ProjectedTileMouseHitTest(
        &g_fx.scene, g_fx.hnd, &g_fx.view_port, CENTER_X + mouse_x, CENTER_Y + mouse_y);
}

/*
 * A right triangle with legs on the axes: (0,0), (40,0), (0,40). The point
 * (30,30) is well outside the hypotenuse but inside the bounding box — the
 * reference picks it, exact barycentric containment does not. This is the
 * agility-rock case in miniature: the gaps a sparse model leaves between its
 * faces stay clickable.
 */
static void
test_bbox_not_containment(void)
{
    fixture_reset();
    int a = add_vertex(0, 0);
    int b = add_vertex(40, 0);
    int c = add_vertex(0, 40);
    add_face(a, b, c);

    CHECK(pick(10, 10), "inside the triangle should pick");
    CHECK(pick(30, 30), "outside the hypotenuse but inside the bbox should pick");
    CHECK(pick(39, 39), "the far bbox corner should pick");
}

/* The 239 deob grows the cursor by 5 before the bbox compare (class144:1572,
 * `var7 = var3 ? 20 : 5`, and var3 is false on the per-face path). */
static void
test_slop_is_five(void)
{
    fixture_reset();
    int a = add_vertex(0, 0);
    int b = add_vertex(20, 0);
    int c = add_vertex(0, 20);
    add_face(a, b, c);

    CHECK(pick(25, 10), "5px past the bbox edge should still pick");
    CHECK(!pick(26, 10), "6px past the bbox edge should miss");
    CHECK(pick(-5, 10), "5px before the bbox edge should still pick");
    CHECK(!pick(-6, 10), "6px before the bbox edge should miss");
    CHECK(pick(10, 25), "5px below the bbox edge should still pick");
    CHECK(!pick(10, 26), "6px below the bbox edge should miss");
    CHECK(pick(10, -5), "5px above the bbox edge should still pick");
    CHECK(!pick(10, -6), "6px above the bbox edge should miss");
}

/*
 * faceColourC == -2 is the reference's hidden marker (class144:1575). It
 * covers a fully transparent face and every face mergeNormals removed where
 * two adjacent locs share all three vertices, and neither draws nor picks.
 */
static void
test_hidden_faces_do_not_pick(void)
{
    fixture_reset();
    int a = add_vertex(0, 0);
    int b = add_vertex(40, 0);
    int c = add_vertex(0, 40);
    int f = add_face(a, b, c);

    CHECK(pick(10, 10), "sanity: the visible face picks");
    g_fx.colors_c[f] = TORIDRAWHSL16_HIDDEN;
    CHECK(!pick(10, 10), "a hidden face must not pick");
}

/* A second, visible face keeps picking after its neighbour is hidden — the
 * skip is per face, not per model. */
static void
test_hidden_face_does_not_disable_the_model(void)
{
    fixture_reset();
    int a = add_vertex(0, 0);
    int b = add_vertex(20, 0);
    int c = add_vertex(0, 20);
    int d = add_vertex(100, 100);
    int e = add_vertex(120, 100);
    int g = add_vertex(100, 120);
    int f0 = add_face(a, b, c);
    add_face(d, e, g);

    g_fx.colors_c[f0] = TORIDRAWHSL16_HIDDEN;
    CHECK(!pick(5, 5), "the hidden face must not pick");
    CHECK(pick(105, 105), "the visible face must still pick");
}

/*
 * A near-clipped vertex is parked at screen x -5000 with an *undivided* y. Its
 * bounding box therefore spans the screen, so without the skip this face would
 * answer yes to every click behind the model.
 */
static void
test_near_clipped_faces_do_not_pick(void)
{
    fixture_reset();
    int a = add_vertex(0, 0);
    int b = add_vertex(20, 0);
    int c = add_vertex(0, 20);
    add_face(a, b, c);

    g_fx.sx[b] = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
    g_fx.sy[b] = 3000000; /* what the projection leaves behind: y, not y/z */
    g_fx.scene.near_clipped = true;

    CHECK(!pick(5, 5), "a face with a near-clipped vertex must not pick");
    CHECK(!pick(400, 400), "and must not swallow clicks far from the model");
}

/* -5001 is the projection's escape for a legitimately projected -5000; it must
 * be treated as an ordinary coordinate. */
static void
test_near_clip_escape_value_still_picks(void)
{
    fixture_reset();
    int a = add_vertex(TORIDRAW_SCREEN_X_NEAR_CLIPPED - 1, 0);
    int b = add_vertex(TORIDRAW_SCREEN_X_NEAR_CLIPPED + 19, 0);
    int c = add_vertex(TORIDRAW_SCREEN_X_NEAR_CLIPPED - 1, 20);
    add_face(a, b, c);

    CHECK(pick(TORIDRAW_SCREEN_X_NEAR_CLIPPED + 4, 5), "x=-5001 is a real coordinate");
}

/*
 * "Invisible" is a much wider set than "hidden", and only the narrow
 * `faceColourC == -2` set is excluded from picking. Everything below is
 * geometry you cannot see that the reference still hit-tests.
 *
 * Backfacing is the big one: the reference computes its winding cull into
 * field2099 and then runs the pick regardless (class144:1616 vs :1618). Here
 * the cull lives in bucket_sort_by_average_depth, which the pick never
 * consults, so the property holds by construction — this pins it.
 */
static void
test_backfacing_faces_still_pick(void)
{
    fixture_reset();
    int a = add_vertex(0, 0);
    int b = add_vertex(0, 40); /* reversed winding */
    int c = add_vertex(40, 0);
    add_face(a, b, c);

    CHECK(pick(10, 10), "a backfacing face must still pick");
}

/*
 * Lit-colour mapping for the two alpha bytes that mean "you cannot see this",
 * because the two land on opposite sides of the pick gate (deob
 * class136:500-513, ModelData.light).
 *
 *   cache alpha 255 -> render type 2 -> faceColourC = -2   -> hidden, no pick
 *   cache alpha 254 -> render type 3 -> colour 128, flat   -> picks
 *
 * The 254 case is the interesting one: the raster drops it anyway on its
 * alpha rule, so it is invisible either way — but it must stay clickable.
 * Marking it hidden (which this used to do) made those faces silently
 * unclickable.
 */
static void
test_alpha_254_stays_pickable_255_does_not(void)
{
    static const alphaint_t alphas[2] = { 255, 254 };
    hsl16_t colors_a[2] = { 0, 0 };
    hsl16_t colors_b[2] = { 0, 0 };
    hsl16_t colors_c[2] = { 0, 0 };
    hsl16_t flat_colors[2] = { 40, 40 };
    faceint_t fa[2] = { 0, 0 };
    faceint_t fb[2] = { 1, 1 };
    faceint_t fc[2] = { 2, 2 };
    vertexint_t vx[3] = { 0, 10, 0 };
    vertexint_t vy[3] = { 0, 0, 10 };
    vertexint_t vz[3] = { 0, 0, 0 };
    struct ToriDraw_Normal normals[3];
    int face_infos[2] = { 0, 0 };

    memset(normals, 0, sizeof(normals));

    ToriDraw_ApplyLighting(
        colors_a, colors_b, colors_c, normals, NULL, fa, fb, fc, 2, flat_colors,
        /* face_alphas   */ (alphaint_t*)alphas,
        /* face_textures */ NULL,
        face_infos, 64, 768, -50, -10, -50, vx, vy, vz);

    CHECK(
        colors_c[0] == TORIDRAWHSL16_HIDDEN,
        "cache alpha 255 must light to the hidden sentinel (0x%04X)",
        (unsigned)colors_c[0]);
    CHECK(
        colors_c[1] == TORIDRAWHSL16_FLAT,
        "cache alpha 254 must light to flat black, not hidden (0x%04X)",
        (unsigned)colors_c[1]);
    CHECK(colors_a[1] == 128, "cache alpha 254 must light to colour 128 (%d)", (int)colors_a[1]);

    /* And the consequence the gate above only implies: one picks, one does not. */
    fixture_reset();
    int a = add_vertex(0, 0);
    int b = add_vertex(40, 0);
    int c = add_vertex(0, 40);
    int f0 = add_face(a, b, c);
    g_fx.colors_c[f0] = colors_c[0];
    CHECK(!pick(10, 10), "the alpha-255 face must not pick");
    g_fx.colors_c[f0] = colors_c[1];
    CHECK(pick(10, 10), "the alpha-254 face must still pick");
}

/* The screen aabb is the cheap reject in front of the face walk. */
static void
test_aabb_gate(void)
{
    fixture_reset();
    int a = add_vertex(0, 0);
    int b = add_vertex(40, 0);
    int c = add_vertex(0, 40);
    add_face(a, b, c);

    g_fx.scene.aabb.min_screen_x = CENTER_X + 100;
    g_fx.scene.aabb.max_screen_x = CENTER_X + 200;
    g_fx.scene.aabb.min_screen_y = CENTER_Y + 100;
    g_fx.scene.aabb.max_screen_y = CENTER_Y + 200;

    CHECK(!pick(10, 10), "a point outside the model aabb must reject before the face walk");
}

/* A model whose faces are all hidden is unpickable, which is what lets a
 * fully merged-away loc fall through to whatever is behind it. */
static void
test_all_hidden_model(void)
{
    fixture_reset();
    int a = add_vertex(0, 0);
    int b = add_vertex(40, 0);
    int c = add_vertex(0, 40);
    int f = add_face(a, b, c);
    g_fx.colors_c[f] = TORIDRAWHSL16_HIDDEN;

    CHECK(!pick(10, 10), "an entirely hidden model must not pick");
}

/* Unlit models carry no colour channel; every face is then testable, matching
 * the reference where the hidden marker only exists once light() has run. */
static void
test_missing_colour_channel_picks_every_face(void)
{
    fixture_reset();
    int a = add_vertex(0, 0);
    int b = add_vertex(40, 0);
    int c = add_vertex(0, 40);
    add_face(a, b, c);
    g_fx.model.face_colors_c = NULL;

    CHECK(pick(10, 10), "a model with no lit colours must still pick");
}

/*
 * Ground tiles are the one mesh the reference does NOT pick through Model.draw,
 * and the difference is exactly the hidden marker.
 *
 * World3D.drawTileUnderlay and drawTileOverlay run
 *
 *     if (takingInput && pointInsideTriangle(...)) { clickTileX = x; clickTileZ = z; }
 *     if (colour !== 12345678) { fillGouraudTriangle(...); }
 *
 * in that order — the click test comes first and the colour sentinel gates only
 * the fill. So a tile decoded with no underlay and a 0xFF00FF overlay (the
 * "draw nothing" flotype) is invisible and still clickable, which is what makes
 * the Inferno's lava moat answer a walk-to-nearest instead of nothing at all.
 * The identical face on a loc model neither draws nor picks.
 */
static void
test_hidden_tile_faces_still_pick(void)
{
    fixture_reset();
    int a = add_vertex(0, 0);
    int b = add_vertex(40, 0);
    int c = add_vertex(0, 40);
    int f = add_face(a, b, c);
    g_fx.colors_c[f] = TORIDRAWHSL16_HIDDEN;

    CHECK(!pick(10, 10), "as a model face, hidden must not pick");
    CHECK(pick_tile(10, 10), "as a tile face, hidden must still pick");
}

/* Everything else the tile pick inherits unchanged — an invisible tile must not
 * become a click sponge for geometry that was never under the cursor. */
static void
test_tile_pick_keeps_the_other_gates(void)
{
    fixture_reset();
    int a = add_vertex(0, 0);
    int b = add_vertex(20, 0);
    int c = add_vertex(0, 20);
    int f = add_face(a, b, c);
    g_fx.colors_c[f] = TORIDRAWHSL16_HIDDEN;

    CHECK(!pick_tile(26, 10), "6px past the bbox edge must miss on a tile too");

    g_fx.sx[b] = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
    g_fx.sy[b] = 3000000;
    g_fx.scene.near_clipped = true;
    CHECK(!pick_tile(5, 5), "a near-clipped tile face must not pick");

    fixture_reset();
    a = add_vertex(0, 0);
    b = add_vertex(40, 0);
    c = add_vertex(0, 40);
    f = add_face(a, b, c);
    g_fx.colors_c[f] = TORIDRAWHSL16_HIDDEN;
    g_fx.scene.aabb.min_screen_x = CENTER_X + 100;
    g_fx.scene.aabb.max_screen_x = CENTER_X + 200;
    g_fx.scene.aabb.min_screen_y = CENTER_Y + 100;
    g_fx.scene.aabb.max_screen_y = CENTER_Y + 200;
    CHECK(!pick_tile(10, 10), "the aabb gate still fronts the tile face walk");
}

int
main(void)
{
    test_bbox_not_containment();
    test_slop_is_five();
    test_hidden_faces_do_not_pick();
    test_hidden_face_does_not_disable_the_model();
    test_near_clipped_faces_do_not_pick();
    test_near_clip_escape_value_still_picks();
    test_backfacing_faces_still_pick();
    test_alpha_254_stays_pickable_255_does_not();
    test_aabb_gate();
    test_all_hidden_model();
    test_missing_colour_channel_picks_every_face();
    test_hidden_tile_faces_still_pick();
    test_tile_pick_keeps_the_other_gates();

    if( g_failures )
    {
        fprintf(stderr, "pick_face_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("pick_face_test: all cases passed\n");
    return 0;
}
