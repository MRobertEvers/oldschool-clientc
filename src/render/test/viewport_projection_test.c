/*
 * Choosing the world camera's projection from the viewport's height.
 *
 * The later client derives its projection scale from the viewport and
 * interpolates it over a band around the reference height of 334. The 2004
 * client does not: its projection is the bare `<< 9` in Model.project, scale
 * 512, whatever the viewport measures.
 *
 * Getting the era wrong is invisible until it is seen, and then obvious. A
 * 2004-era frame recomputed from a 335-high viewport with the zoom endpoints
 * that era never writes lands on 335 * 256 / 334 = 256 -- half the reference
 * scale, which reads as an eye at twice the distance. That was the "camera is
 * too far out" report against rev 289, and it is the first case below.
 *
 * What is asserted:
 *
 *   - the four inputs are answered in order of authority: a field-of-view
 *     override beats everything, OFF declines to touch the camera at all,
 *     AUTO on a fixed-camera revision pins the constant, and a FORCED scale
 *     still beats that -- which is the whole reason the forced form exists.
 *   - the interpolation band: below the reference height it is the near zoom,
 *     a hundred pixels above it is the far zoom, and between the two it eases.
 *     Both endpoints and the midpoint are pinned, because an off-by-one at
 *     either end is a magnification change nobody can see as a bug.
 *   - unstated zoom endpoints default to 256 rather than to zero, which would
 *     otherwise collapse the scale to 1.
 *   - before the first emit walk there is no viewport, and the camera is left
 *     alone rather than given a scale computed from nothing.
 *
 * Build and run:
 *   make -C src test-viewport-projection
 */

#include "render/torirs_viewport_projection.h"

#include "impl/projection/projection.scalar_reference.h"

#include <stdio.h>

#define VALID 1
#define ZOOM_ON 1
#define ZOOM_OFF 0
#define NO_FOV (-1)

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

static void
test_the_2004_camera_keeps_its_constant(void)
{
    /*
     * The regression this ordering exists for. A fixed-camera revision, a
     * 335-high viewport, and the default endpoints: recomputing gives 256,
     * which is half of TORIDRAW_PROJECTION_SCALE_DEFAULT and looks like the
     * camera has moved twice as far out.
     */
    struct ToriRS_ViewportProjection fixed = ToriRS_ProjectionForViewport(
        TORIRS_ENV_SCALE_AUTO, NO_FOV, ZOOM_OFF, VALID, 335, 256, 256);

    CHECK(fixed.action == TORIRS_VIEWPORT_PROJECTION_SCALE, "the fixed camera declined to act");
    CHECK(
        fixed.scale == TORIDRAW_PROJECTION_SCALE_DEFAULT,
        "the fixed camera got scale %d, want the constant %d",
        fixed.scale,
        TORIDRAW_PROJECTION_SCALE_DEFAULT);

    /* And the same viewport on a revision that DOES zoom recomputes it -- so
     * the difference really is the era and not the numbers. */
    {
        struct ToriRS_ViewportProjection zooming = ToriRS_ProjectionForViewport(
            TORIRS_ENV_SCALE_AUTO, NO_FOV, ZOOM_ON, VALID, 335, 256, 256);

        CHECK(zooming.scale == 256, "the zooming camera got scale %d, want 256", zooming.scale);
        CHECK(
            zooming.scale != fixed.scale,
            "the two eras produced the same scale; the viewport_zoom test does nothing");
    }
}

static void
test_the_order_of_authority(void)
{
    /* A field of view beats everything, including OFF. */
    {
        struct ToriRS_ViewportProjection projection = ToriRS_ProjectionForViewport(
            TORIRS_ENV_SCALE_OFF, 180, ZOOM_OFF, VALID, 400, 256, 256);

        CHECK(projection.action == TORIRS_VIEWPORT_PROJECTION_FOV, "the fov override lost");
        CHECK(projection.fov_rpi2048 == 180, "the fov value is %d", projection.fov_rpi2048);
    }

    /* OFF leaves the camera untouched -- not "sets it to the default", which
     * would still be a change to whatever it was. */
    {
        struct ToriRS_ViewportProjection projection = ToriRS_ProjectionForViewport(
            TORIRS_ENV_SCALE_OFF, NO_FOV, ZOOM_ON, VALID, 400, 256, 256);

        CHECK(projection.action == TORIRS_VIEWPORT_PROJECTION_KEEP, "OFF touched the camera");
    }

    /*
     * A FORCED scale beats the fixed-camera pin. That is the whole point of
     * the forced form: it exists to bisect exactly the case above, so a
     * revision that pins the constant must not swallow it.
     */
    {
        struct ToriRS_ViewportProjection projection = ToriRS_ProjectionForViewport(
            777, NO_FOV, ZOOM_OFF, VALID, 335, 256, 256);

        CHECK(projection.action == TORIRS_VIEWPORT_PROJECTION_SCALE, "the forced scale declined");
        CHECK(projection.scale == 777, "the forced scale is %d, want 777", projection.scale);
    }

    /* No viewport yet: every frame before the first emit walk. */
    {
        struct ToriRS_ViewportProjection projection = ToriRS_ProjectionForViewport(
            TORIRS_ENV_SCALE_AUTO, NO_FOV, ZOOM_ON, 0, 400, 256, 256);

        CHECK(
            projection.action == TORIRS_VIEWPORT_PROJECTION_KEEP,
            "a projection was computed with no viewport");
    }
    {
        struct ToriRS_ViewportProjection projection = ToriRS_ProjectionForViewport(
            TORIRS_ENV_SCALE_AUTO, NO_FOV, ZOOM_ON, VALID, 0, 256, 256);

        CHECK(
            projection.action == TORIRS_VIEWPORT_PROJECTION_KEEP,
            "a projection was computed from a zero-height viewport");
    }
}

static void
test_the_zoom_band(void)
{
    int const near_zoom = 200;
    int const far_zoom = 400;

    /* Below the reference height: the near endpoint, flat. */
    {
        struct ToriRS_ViewportProjection low = ToriRS_ProjectionForViewport(
            TORIRS_ENV_SCALE_AUTO, NO_FOV, ZOOM_ON, VALID, 100, near_zoom, far_zoom);
        struct ToriRS_ViewportProjection just_below = ToriRS_ProjectionForViewport(
            TORIRS_ENV_SCALE_AUTO,
            NO_FOV,
            ZOOM_ON,
            VALID,
            TORIRS_VIEWPORT_REFERENCE_HEIGHT - 1,
            near_zoom,
            far_zoom);

        CHECK(low.zoom == near_zoom, "a short viewport zoomed %d, want %d", low.zoom, near_zoom);
        CHECK(just_below.zoom == near_zoom, "one pixel below the reference is not the near zoom");
    }

    /*
     * The band is CONTINUOUS at both ends, which is why the two clamp
     * comparisons are not observable: at the reference height the
     * interpolation already yields the near zoom, and at the far end it
     * already yields the far zoom. Mutating `<` to `<=` or `>=` to `>` changes
     * nothing, and that is the property worth having rather than a gap worth
     * closing -- a discontinuity at either end would be a magnification that
     * jumps by a pixel of window resize.
     */
    {
        struct ToriRS_ViewportProjection at = ToriRS_ProjectionForViewport(
            TORIRS_ENV_SCALE_AUTO,
            NO_FOV,
            ZOOM_ON,
            VALID,
            TORIRS_VIEWPORT_REFERENCE_HEIGHT,
            near_zoom,
            far_zoom);

        CHECK(at.zoom == near_zoom, "at the reference the zoom is %d, want %d", at.zoom, near_zoom);
        CHECK(
            at.scale == near_zoom,
            "at the reference height the scale should equal the zoom, got %d",
            at.scale);
    }

    /* The far end of the band, and everything past it. */
    {
        struct ToriRS_ViewportProjection at_far = ToriRS_ProjectionForViewport(
            TORIRS_ENV_SCALE_AUTO,
            NO_FOV,
            ZOOM_ON,
            VALID,
            TORIRS_VIEWPORT_REFERENCE_HEIGHT + TORIRS_VIEWPORT_ZOOM_BAND,
            near_zoom,
            far_zoom);
        struct ToriRS_ViewportProjection past = ToriRS_ProjectionForViewport(
            TORIRS_ENV_SCALE_AUTO, NO_FOV, ZOOM_ON, VALID, 2000, near_zoom, far_zoom);

        CHECK(at_far.zoom == far_zoom, "at the band's end the zoom is %d", at_far.zoom);
        CHECK(past.zoom == far_zoom, "past the band the zoom is %d", past.zoom);
    }

    /* Halfway across the band, halfway between the endpoints. */
    {
        struct ToriRS_ViewportProjection middle = ToriRS_ProjectionForViewport(
            TORIRS_ENV_SCALE_AUTO,
            NO_FOV,
            ZOOM_ON,
            VALID,
            TORIRS_VIEWPORT_REFERENCE_HEIGHT + TORIRS_VIEWPORT_ZOOM_BAND / 2,
            near_zoom,
            far_zoom);

        CHECK(
            middle.zoom == (near_zoom + far_zoom) / 2,
            "halfway across the band the zoom is %d, want %d",
            middle.zoom,
            (near_zoom + far_zoom) / 2);
    }

    /* Unstated endpoints default to 256, not to zero -- which would collapse
     * every scale to the 1 the floor clamps it to. */
    {
        struct ToriRS_ViewportProjection unstated = ToriRS_ProjectionForViewport(
            TORIRS_ENV_SCALE_AUTO, NO_FOV, ZOOM_ON, VALID, 334, 0, 0);

        CHECK(
            unstated.zoom == TORIRS_VIEWPORT_ZOOM_DEFAULT,
            "unstated endpoints gave zoom %d, want %d",
            unstated.zoom,
            TORIRS_VIEWPORT_ZOOM_DEFAULT);
        CHECK(unstated.scale > 1, "unstated endpoints collapsed the scale to %d", unstated.scale);
    }
}

int
main(void)
{
    test_the_2004_camera_keeps_its_constant();
    test_the_order_of_authority();
    test_the_zoom_band();

    if( g_failures )
    {
        printf("viewport_projection_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("viewport_projection_test: OK\n");
    return 0;
}
