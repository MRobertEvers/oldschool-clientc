/*
 * The map editor's hover ghost, and the camera on the editor's model-view
 * well. Two rules, neither of which anything downstream checks.
 *
 * The GHOST is a real scene placement, which is what makes it look right and
 * is also the whole difficulty: placing it evicts whatever held that tile's
 * slot. Forget the occupant and hovering over a door DELETES the door -- it
 * does not come back when you move away, and nothing says why, because the
 * document was never touched and only the scene lied. Restore it on a COMMIT
 * and the old door comes back on top of the one just placed.
 *
 * The CAMERA shares one zoom between the player's orbit and the fit that
 * frames a new model. Reset it on an orbit and the well snaps back to its
 * default every key press, so it cannot be turned at all. Keep it on a new
 * pick and a castle gate arrives at the distance that framed a candle.
 *
 *   make -C src test-editor-ghost
 */
#include "editor/editor_preview_camera.h"
#include "editor/map_editor_ghost.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static struct MapEditorGhostSpec
spec(int loc_id, int x, int z, int level, int shape, int angle)
{
    struct MapEditorGhostSpec out;

    memset(&out, 0, sizeof(out));
    out.loc_id = loc_id;
    out.scene_x = x;
    out.scene_z = z;
    out.level = level;
    out.shape = shape;
    out.angle = angle;
    return out;
}

/* ---------------------------------------------------------------- ghost */

static void
test_no_ghost_is_no_work(void)
{
    struct MapEditorGhost ghost;
    struct MapEditorGhostSpec removed;
    struct MapEditorGhostSpec restore;
    struct MapEditorGhostSpec want = spec(1, 2, 3, 0, 10, 1);
    struct MapEditorGhostSpec zeroes = spec(0, 0, 0, 0, 0, 0);
    bool has_restore = true;

    printf("TEST: a ghost that is not up\n");

    MapEditorGhost_Reset(&ghost);
    CHECK(!ghost.active, "a fresh ghost is up");
    CHECK(!MapEditorGhost_Matches(&ghost, &want), "a ghost that is not up matched a spec");
    /* And not the spec its own zeroed fields happen to describe, which is the
     * one a field-by-field comparison agrees with. Loc 0 on tile 0,0 is a real
     * thing to hover, so this is the first ghost of a session matching before
     * it has been placed. */
    CHECK(
        !MapEditorGhost_Matches(&ghost, &zeroes),
        "a ghost that is not up matched its own empty state");
    CHECK(!MapEditorGhost_NeedsFade(&ghost), "a ghost that is not up asked to be faded");
    CHECK(
        !MapEditorGhost_Remove(&ghost, &removed, &has_restore, &restore),
        "removing a ghost that is not up reported work");
}

static void
test_hovering_away_puts_the_occupant_back(void)
{
    struct MapEditorGhost ghost;
    struct MapEditorGhostSpec removed;
    struct MapEditorGhostSpec restore;
    struct MapEditorGhostSpec placed = spec(500, 30, 40, 1, 10, 2);
    struct MapEditorGhostSpec occupant = spec(77, 30, 40, 1, 10, 3);
    bool has_restore = false;

    printf("TEST: hovering away restores what the ghost displaced\n");

    /*
     * The painter holds one loc per layer per tile, so ghosting a wall onto a
     * tile that already has one REPLACES it in the scene. A plain delete on
     * hover-out leaves the slot empty, which reads as "hovering destroyed my
     * wall" -- and it is not in the document, so nothing undoes it and nothing
     * explains it.
     */
    MapEditorGhost_Reset(&ghost);
    MapEditorGhost_Place(&ghost, &placed, &occupant);
    CHECK(ghost.active, "the ghost did not go up");

    CHECK(
        MapEditorGhost_Remove(&ghost, &removed, &has_restore, &restore),
        "removing a live ghost reported no work");
    /*
     * The deletion names the GHOST's own slot -- tile, level, layer and pose.
     * The occupant shares the first three here, exactly as it does in the
     * scene, so the angle is what separates them: 2 is the ghost's and 3 is
     * the occupant's, and a removal that named the occupant's would delete
     * a placement that is not there.
     */
    CHECK(
        removed.scene_x == 30 && removed.scene_z == 40 && removed.level == 1 &&
            removed.shape == 10,
        "the removal named the wrong slot");
    CHECK(removed.angle == 2, "the removal named the occupant's pose, not the ghost's");
    CHECK(has_restore, "the displaced occupant was forgotten on a hover-away");
    CHECK(
        restore.loc_id == 77 && restore.shape == 10 && restore.angle == 3,
        "the occupant came back as loc %d shape %d angle %d",
        restore.loc_id, restore.shape, restore.angle);
    CHECK(
        restore.scene_x == 30 && restore.scene_z == 40 && restore.level == 1,
        "the occupant came back on the wrong tile");
    CHECK(!ghost.active, "the ghost is still up after being removed");
}

static void
test_an_empty_tile_restores_nothing(void)
{
    struct MapEditorGhost ghost;
    struct MapEditorGhostSpec removed;
    struct MapEditorGhostSpec restore;
    struct MapEditorGhostSpec placed = spec(500, 30, 40, 1, 10, 2);
    bool has_restore = true;

    printf("TEST: a ghost on an empty tile puts nothing back\n");

    /* The common case, and it must not invent an occupant -- placing loc 0 or
     * a stale one would put a loc on a tile the player never touched. */
    MapEditorGhost_Reset(&ghost);
    MapEditorGhost_Place(&ghost, &placed, NULL);
    CHECK(MapEditorGhost_Remove(&ghost, &removed, &has_restore, &restore), "removal reported none");
    CHECK(!has_restore, "an empty tile produced something to restore");
}

static void
test_committing_forgets_the_occupant(void)
{
    struct MapEditorGhost ghost;
    struct MapEditorGhostSpec removed;
    struct MapEditorGhostSpec restore;
    struct MapEditorGhostSpec placed = spec(500, 30, 40, 1, 10, 2);
    struct MapEditorGhostSpec occupant = spec(77, 30, 40, 1, 10, 3);
    bool has_restore = false;

    printf("TEST: a commit forgets what the ghost displaced\n");

    /*
     * The other half, and the opposite answer. The click's real placement has
     * just replaced the ghost's element on the same tile and layer: removing
     * would delete the loc that was placed, and restoring the occupant would
     * resurrect the old one on top of it. A commit forgets both.
     */
    MapEditorGhost_Reset(&ghost);
    MapEditorGhost_Place(&ghost, &placed, &occupant);
    MapEditorGhost_Commit(&ghost);
    CHECK(!ghost.active, "the ghost survived the commit");
    CHECK(
        !MapEditorGhost_Remove(&ghost, &removed, &has_restore, &restore),
        "a committed ghost still had something to remove");
    /* Asserted as state, not only as behaviour. Nothing reads the occupant
     * once the ghost is down, so a commit that merely hid it would behave
     * identically today -- and would leave a struct carrying a loc id that is
     * no longer true for the next reader to trip over. */
    CHECK(!ghost.displaced_valid, "the commit kept the occupant it overwrote");
}

static void
test_the_ghost_follows_the_hover(void)
{
    struct MapEditorGhost ghost;
    struct MapEditorGhostSpec placed = spec(500, 30, 40, 1, 10, 2);

    printf("TEST: any change of tile, loc or pose is a new ghost\n");

    /*
     * Same everything is nothing to do -- the ghost is already standing there,
     * and taking it down and putting it back every frame would restart its
     * async placement and the fade with it, so it would never finish going
     * translucent.
     */
    MapEditorGhost_Reset(&ghost);
    MapEditorGhost_Place(&ghost, &placed, NULL);
    CHECK(MapEditorGhost_Matches(&ghost, &placed), "an unchanged hover did not match");

    /* Every field on its own, because each is a different way for the preview
     * to be wrong and a match that ignores one is a ghost that lies about it. */
    {
        struct MapEditorGhostSpec moved;

        moved = placed;
        moved.scene_x = 31;
        CHECK(!MapEditorGhost_Matches(&ghost, &moved), "a move east matched");
        moved = placed;
        moved.scene_z = 41;
        CHECK(!MapEditorGhost_Matches(&ghost, &moved), "a move north matched");
        moved = placed;
        moved.level = 2;
        CHECK(!MapEditorGhost_Matches(&ghost, &moved), "a change of storey matched");
        moved = placed;
        moved.loc_id = 501;
        CHECK(!MapEditorGhost_Matches(&ghost, &moved), "a different loc matched");
        moved = placed;
        moved.shape = 11;
        CHECK(!MapEditorGhost_Matches(&ghost, &moved), "a different shape matched");
        moved = placed;
        moved.angle = 3;
        CHECK(!MapEditorGhost_Matches(&ghost, &moved), "a rotation matched");
    }
}

static void
test_the_fade_is_written_once(void)
{
    struct MapEditorGhost ghost;
    struct MapEditorGhostSpec placed = spec(500, 30, 40, 1, 10, 2);
    struct MapEditorGhostSpec moved = spec(500, 31, 40, 1, 10, 2);
    struct MapEditorGhostSpec removed;
    struct MapEditorGhostSpec restore;
    bool has_restore = false;

    printf("TEST: the translucency pass retries until it lands, then stops\n");

    /*
     * The placement is asynchronous: the ghost exists for some frames before
     * there is an element to fade, so the pass has to retry. Once it has
     * landed it must stop -- the fade is written onto the element's own model,
     * and writing it every frame is work on the hot path for no change.
     */
    MapEditorGhost_Reset(&ghost);
    MapEditorGhost_Place(&ghost, &placed, NULL);
    CHECK(MapEditorGhost_NeedsFade(&ghost), "a fresh ghost does not want fading");
    CHECK(MapEditorGhost_NeedsFade(&ghost), "the retry stopped before the fade landed");
    MapEditorGhost_NoteFaded(&ghost);
    CHECK(!MapEditorGhost_NeedsFade(&ghost), "the fade is asked for again after landing");

    /* And a NEW ghost wants its own: the fade is on the element, and the new
     * ghost is a different element. */
    (void)MapEditorGhost_Remove(&ghost, &removed, &has_restore, &restore);
    MapEditorGhost_Place(&ghost, &moved, NULL);
    CHECK(MapEditorGhost_NeedsFade(&ghost), "a new ghost inherited the old one's fade");

    /* Including a Place straight over a faded ghost, without a removal in
     * between. The caller does not do that today; Place says it is a fresh
     * start, and a caller that starts doing it must not get a ghost that never
     * goes translucent. */
    MapEditorGhost_NoteFaded(&ghost);
    MapEditorGhost_Place(&ghost, &placed, NULL);
    CHECK(
        MapEditorGhost_NeedsFade(&ghost),
        "a ghost placed over a faded one inherited its fade");
}

/* --------------------------------------------------------------- camera */

static void
test_an_orbit_survives_its_own_render(void)
{
    struct EditorPreviewCamera camera;

    printf("TEST: turning the well does not snap it back\n");

    EditorPreviewCamera_Reset(&camera);
    /* A fresh camera wants a fit for whatever model it is first shown; spend
     * it, the way the first real render does, so the orbit below is measured
     * against a settled camera and not against the boot state. */
    (void)EditorPreviewCamera_TakeFit(&camera, 200, 0, 100);

    /*
     * An orbit invalidates the well, and the render it causes must not reset
     * the camera it came from. Reset it and every key press snaps the model
     * back to its default angle, so the well cannot be turned at all -- which
     * looks like a control that does nothing.
     */
    EditorPreviewCamera_Orbit(&camera, 0, 12);
    CHECK(camera.dirty, "an orbit did not ask for a re-render");
    CHECK(camera.yaw == EDITOR_PREVIEW_DEFAULT_YAW + 12, "the orbit did not take");
    CHECK(camera.yaw == EDITOR_PREVIEW_DEFAULT_YAW + 12, "the orbit reset the camera");
    CHECK(!camera.fit_pending, "the orbit asked to be re-framed");
    CHECK(EditorPreviewCamera_TakeRender(&camera), "the orbit's render was not offered");
    CHECK(camera.yaw == EDITOR_PREVIEW_DEFAULT_YAW + 12, "the render snapped the camera back");
    CHECK(!camera.fit_pending, "the orbit's render asked to be re-framed");

    /* The angles wrap: the well is a turntable, and a model seen from behind
     * is a legitimate thing to want. */
    EditorPreviewCamera_Reset(&camera);
    EditorPreviewCamera_Orbit(&camera, 0, -(EDITOR_PREVIEW_DEFAULT_YAW + 1));
    CHECK(camera.yaw == 2047, "the yaw did not wrap, it went to %d", camera.yaw);
    EditorPreviewCamera_Orbit(&camera, 0, 1);
    CHECK(camera.yaw == 0, "the yaw did not wrap back round");
}

static void
test_a_new_pick_is_reframed(void)
{
    struct EditorPreviewCamera camera;

    printf("TEST: a new model arrives framed, not at the last one's distance\n");

    EditorPreviewCamera_Reset(&camera);
    (void)EditorPreviewCamera_TakeFit(&camera, 200, 0, 100);
    EditorPreviewCamera_Orbit(&camera, 40, 400);
    (void)EditorPreviewCamera_TakeRender(&camera);

    /*
     * A new pick resets the angle and asks for a fit. Keeping the camera would
     * show a castle gate from the angle and at the distance that framed a
     * candle, which reads as a model that failed to load.
     */
    EditorPreviewCamera_Invalidate(&camera, false);
    /* Decided here, not remembered for the render: a flag saying "the next
     * render is a new model" is true until something else invalidates, and
     * every reader in between would have to know that. */
    CHECK(camera.pitch == EDITOR_PREVIEW_DEFAULT_PITCH, "the new pick kept the old pitch");
    CHECK(camera.yaw == EDITOR_PREVIEW_DEFAULT_YAW, "the new pick kept the old yaw");
    CHECK(camera.fit_pending, "the new pick was not re-framed");
    CHECK(EditorPreviewCamera_TakeRender(&camera), "the new pick's render was not offered");

    /* And the flag does not leak into the pick after it. */
    EditorPreviewCamera_Reset(&camera);
    (void)EditorPreviewCamera_TakeFit(&camera, 200, 0, 100);
    EditorPreviewCamera_Orbit(&camera, 40, 400);
    (void)EditorPreviewCamera_TakeRender(&camera);
    EditorPreviewCamera_Invalidate(&camera, false);
    (void)EditorPreviewCamera_TakeRender(&camera);
    CHECK(camera.pitch == EDITOR_PREVIEW_DEFAULT_PITCH,
        "a pick after an orbit inherited the orbit's camera");
}

static void
test_a_render_is_offered_once(void)
{
    struct EditorPreviewCamera camera;

    printf("TEST: a render is taken once\n");

    EditorPreviewCamera_Reset(&camera);
    CHECK(!EditorPreviewCamera_TakeRender(&camera), "a clean camera offered a render");

    EditorPreviewCamera_Invalidate(&camera, true);
    CHECK(EditorPreviewCamera_TakeRender(&camera), "an invalidated camera offered nothing");
    CHECK(!EditorPreviewCamera_TakeRender(&camera), "the same render was offered twice");
}

static void
test_the_fit_holds_whichever_way_the_model_is_long(void)
{
    struct EditorPreviewCamera camera;
    int wide_zoom;
    int tall_zoom;

    printf("TEST: the fit frames a model on its larger dimension\n");

    /*
     * A gate is wide and short; a lamp post is narrow and tall. Fitting on
     * height alone crops the gate to a wall of pixels, and on width alone
     * loses the lamp post in the middle of the well. The rule is the larger of
     * the diameter and the height, and these two models have the same larger
     * dimension by construction -- so they must frame the same.
     */
    EditorPreviewCamera_Reset(&camera);
    CHECK(EditorPreviewCamera_TakeFit(&camera, 200, 0, 100), "the first fit was refused");
    wide_zoom = camera.zoom;

    EditorPreviewCamera_Reset(&camera);
    CHECK(EditorPreviewCamera_TakeFit(&camera, 50, 0, 400), "the second fit was refused");
    tall_zoom = camera.zoom;

    CHECK(wide_zoom == tall_zoom,
        "a 400-wide model framed at %d and a 400-tall one at %d", wide_zoom, tall_zoom);
    CHECK(wide_zoom == (400 * 9) / 2, "the fit is %d, not the calibrated (size * 9) / 2", wide_zoom);

    /* A model whose origin is not at its feet: the height is the SPAN, not the
     * top. A loc hanging below zero is otherwise framed as if it were flat. */
    EditorPreviewCamera_Reset(&camera);
    (void)EditorPreviewCamera_TakeFit(&camera, 50, -200, 200);
    CHECK(camera.zoom == wide_zoom, "a model spanning the origin was framed on its top alone");
}

static void
test_the_fit_is_clamped_at_both_ends(void)
{
    struct EditorPreviewCamera camera;

    printf("TEST: a degenerate model does not zoom to nothing or to infinity\n");

    /*
     * A model with no faces, or one that has not finished loading, has bounds
     * of nothing. Unclamped that is a zoom of zero -- an empty well, which
     * reads as a missing model rather than a bad number -- and a model the
     * size of a map square is the other end of the same problem.
     */
    EditorPreviewCamera_Reset(&camera);
    (void)EditorPreviewCamera_TakeFit(&camera, 0, 0, 0);
    CHECK(camera.zoom == EDITOR_PREVIEW_FIT_ZOOM_MIN, "a degenerate model fitted to %d",
        camera.zoom);

    EditorPreviewCamera_Reset(&camera);
    (void)EditorPreviewCamera_TakeFit(&camera, 100000, 0, 100000);
    CHECK(camera.zoom == EDITOR_PREVIEW_FIT_ZOOM_MAX, "an enormous model fitted to %d",
        camera.zoom);
}

static void
test_the_fit_happens_once(void)
{
    struct EditorPreviewCamera camera;

    printf("TEST: the fit is taken once, so an orbit's zoom survives\n");

    EditorPreviewCamera_Reset(&camera);
    CHECK(EditorPreviewCamera_TakeFit(&camera, 200, 0, 100), "the fit was refused");
    CHECK(!EditorPreviewCamera_TakeFit(&camera, 999, 0, 999),
        "the fit ran again and overwrote the zoom");

    /*
     * Which is what makes the player's zoom stick. Without it, every re-render
     * an orbit causes would re-frame the model, and the well would spring back
     * to the fitted distance the moment it was turned.
     */
    EditorPreviewCamera_Zoom(&camera, 1);
    {
        int const zoomed = camera.zoom;
        (void)EditorPreviewCamera_TakeRender(&camera);
        CHECK(!EditorPreviewCamera_TakeFit(&camera, 200, 0, 100), "the orbit's render re-fitted");
        CHECK(camera.zoom == zoomed, "the orbit's zoom was overwritten by a fit");
    }
}

static void
test_the_players_zoom_is_clamped_wider_than_the_fits(void)
{
    struct EditorPreviewCamera camera;
    int i;

    printf("TEST: a person may go closer and further out than a fit would\n");

    /*
     * The two ranges are deliberately different. A fit frames a model; a
     * person looking at one is allowed past framing in both directions, and a
     * player range no wider than the fit's makes the zoom keys do nothing on a
     * model that was already fitted to an end.
     */
    EditorPreviewCamera_Reset(&camera);
    for( i = 0; i < 2000; i++ )
        EditorPreviewCamera_Zoom(&camera, -1);
    CHECK(camera.zoom == EDITOR_PREVIEW_ZOOM_MIN, "zooming all the way in reached %d", camera.zoom);
    CHECK(EDITOR_PREVIEW_ZOOM_MIN < EDITOR_PREVIEW_FIT_ZOOM_MIN,
        "a fitted model cannot be zoomed any closer");

    for( i = 0; i < 2000; i++ )
        EditorPreviewCamera_Zoom(&camera, 1);
    CHECK(camera.zoom == EDITOR_PREVIEW_ZOOM_MAX, "zooming all the way out reached %d", camera.zoom);
    CHECK(EDITOR_PREVIEW_ZOOM_MAX > EDITOR_PREVIEW_FIT_ZOOM_MAX,
        "a fitted model cannot be zoomed any further out");

    /* A zero step is not a zoom, and must not invalidate the well: it would
     * re-render every frame a key is not held. */
    EditorPreviewCamera_Reset(&camera);
    EditorPreviewCamera_Zoom(&camera, 0);
    CHECK(!camera.dirty, "a zero-step zoom asked for a re-render");
}

int
main(void)
{
    test_no_ghost_is_no_work();
    test_hovering_away_puts_the_occupant_back();
    test_an_empty_tile_restores_nothing();
    test_committing_forgets_the_occupant();
    test_the_ghost_follows_the_hover();
    test_the_fade_is_written_once();

    test_an_orbit_survives_its_own_render();
    test_a_new_pick_is_reframed();
    test_a_render_is_offered_once();
    test_the_fit_holds_whichever_way_the_model_is_long();
    test_the_fit_is_clamped_at_both_ends();
    test_the_fit_happens_once();
    test_the_players_zoom_is_clamped_wider_than_the_fits();

    if( g_failures )
    {
        printf("editor_ghost_preview_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("editor_ghost_preview_test: OK\n");
    return 0;
}
