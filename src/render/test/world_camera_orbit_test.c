/*
 * The follow camera's arithmetic.
 *
 * Every rule here is one a still frame cannot check, which is why none of them
 * has ever had a test. A camera at the wrong distance looks like a camera at a
 * different zoom. One whose terrain clamp eases the wrong way looks like
 * terrain popping. One whose key velocities decay wrongly feels like a heavy
 * mouse, and "feels heavy" is not a bug report anyone can act on.
 *
 * The numbers are the reference's and the profile's, and the two are not the
 * same thing: the impulses and the ease rates are the reference client's, while
 * the pitch band, the distance terms and the near plane are stated per
 * revision, because two lanes of this client disagree about all of them.
 *
 *   make -C src test-world-camera
 */
#include "render/world_camera_orbit.h"

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

/* The OldSchool profile's own numbers: the 128..383 pitch band every one of
 * the four camera paths respects, `pitch * 3 + 600`, no viewport zoom, no
 * dolly. */
static struct WorldCameraLimits
osrs_limits(void)
{
    struct WorldCameraLimits limits;

    memset(&limits, 0, sizeof(limits));
    limits.pitch_flattest = 128;
    limits.pitch_steepest = 383;
    limits.pitch_distance = 3;
    limits.rest_zoom = 600;
    limits.viewport_zoom_256 = 0;
    limits.distance_scale_percent = 100;
    limits.near_plane_z = 50;
    return limits;
}

static struct WorldCameraKeys
no_keys(void)
{
    struct WorldCameraKeys keys;

    memset(&keys, 0, sizeof(keys));
    return keys;
}

/* ------------------------------------------------------------------ */

static void
test_the_pitch_band(void)
{
    struct WorldCameraLimits const limits = osrs_limits();

    printf("TEST: the pitch band the revision states\n");

    CHECK(WorldCameraLimits_ClampPitch(&limits, 200) == 200, "a pitch inside the band moved");
    CHECK(WorldCameraLimits_ClampPitch(&limits, 128) == 128, "the flattest end was clamped");
    CHECK(WorldCameraLimits_ClampPitch(&limits, 383) == 383, "the steepest end was clamped");
    CHECK(WorldCameraLimits_ClampPitch(&limits, 127) == 128, "one below the band");
    CHECK(WorldCameraLimits_ClampPitch(&limits, 384) == 383, "one above the band");
    CHECK(WorldCameraLimits_ClampPitch(&limits, -9000) == 128, "far below the band");
    CHECK(WorldCameraLimits_ClampPitch(&limits, 9000) == 383, "far above the band");

    /* A profile that states a band of ONE is a camera with a fixed pitch, and
     * has to come out that way rather than as an empty range. */
    {
        struct WorldCameraLimits flat = limits;
        flat.pitch_flattest = 200;
        flat.pitch_steepest = 200;
        CHECK(WorldCameraLimits_ClampPitch(&flat, 100) == 200, "a fixed-pitch camera moved");
        CHECK(WorldCameraLimits_ClampPitch(&flat, 300) == 200, "a fixed-pitch camera moved");
    }
}

static void
test_a_held_key_accelerates_and_a_released_one_coasts(void)
{
    struct WorldCameraOrbit orbit;
    struct WorldCameraLimits const limits = osrs_limits();
    struct WorldCameraKeys keys = no_keys();
    int first;
    int second;
    int i;

    printf("TEST: the arrow keys accelerate into a turn and coast out of it\n");

    /*
     * An impulse-and-decay model, not a rate. A key adds toward its impulse by
     * half the difference each cycle, so the first cycle turns least and the
     * turn builds; a released key halves what is left, so it coasts. Replace
     * it with a fixed rate and the camera starts and stops dead, which reads
     * as a dropped frame every time it is touched.
     */
    WorldCameraOrbit_Reset(&orbit);
    orbit.pitch = 200;
    keys.right = true;

    WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    first = orbit.yaw;
    WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    second = orbit.yaw - first;
    CHECK(first > 0, "a held key turned nothing");
    CHECK(second > first, "the turn did not accelerate: %d then %d", first, second);

    /*
     * It settles one SHORT of the impulse, and that is the integer model
     * rather than a mistake: `vel += (24 - vel) / 2` adds nothing once the gap
     * is 1, so the velocity asymptotes to 23 and stays there. Written out
     * because a rewrite in float would reach 24, turn a fraction faster, and
     * present as "the camera feels different" with nothing to point at.
     */
    for( i = 0; i < 40; i++ )
        WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(orbit.yaw_velocity == 23, "the yaw settled at %d, not one short of the impulse",
        orbit.yaw_velocity);

    /* Released, it coasts down rather than stopping -- halving, and rounding
     * toward zero on the way. */
    keys = no_keys();
    WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(orbit.yaw_velocity == 11, "the release did not halve the velocity: %d",
        orbit.yaw_velocity);
    WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(orbit.yaw_velocity == 5, "the coast is not a halving: %d", orbit.yaw_velocity);
    for( i = 0; i < 20; i++ )
        WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(orbit.yaw_velocity == 0, "the coast never reaches a stop");

    /* The pitch axis coasts too, and by its own number. Written separately
     * because the two axes are separate code, and a coast that only worked on
     * one of them would read as the camera being smooth to turn and abrupt to
     * tip. */
    WorldCameraOrbit_Reset(&orbit);
    orbit.pitch = 250;
    keys = no_keys();
    keys.up = true;
    for( i = 0; i < 40; i++ )
        WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    keys = no_keys();
    WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(orbit.pitch_velocity == 5, "the pitch release did not halve: %d", orbit.pitch_velocity);
    WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(orbit.pitch_velocity == 2, "the pitch coast is not a halving: %d", orbit.pitch_velocity);

    /*
     * And the velocity is applied at HALF, which is where the actual turn rate
     * comes from. Settled at 23, the yaw advances 11 a cycle and not 23 --
     * apply it whole and the camera turns twice as fast as the reference's,
     * which is a control that feels wrong and reads as nothing at all.
     */
    WorldCameraOrbit_Reset(&orbit);
    orbit.pitch = 250;
    keys = no_keys();
    keys.right = true;
    for( i = 0; i < 40; i++ )
        WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    {
        int const before = orbit.yaw;
        WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
        CHECK(
            ((orbit.yaw - before) & 0x7ff) == orbit.yaw_velocity / 2,
            "a settled turn advanced %d a cycle at velocity %d",
            (orbit.yaw - before) & 0x7ff, orbit.yaw_velocity);
        CHECK(((orbit.yaw - before) & 0x7ff) == 11, "the settled turn rate is not 11 a cycle");
    }
}

static void
test_the_two_axes_and_their_directions(void)
{
    struct WorldCameraOrbit orbit;
    struct WorldCameraLimits const limits = osrs_limits();
    struct WorldCameraKeys keys;
    int i;

    printf("TEST: each key turns its own axis, its own way\n");

    /* Yaw turns twice as fast as pitch tips -- 24 against 12 -- which is what
     * makes looking around feel like turning and looking up feel like leaning.
     * Settled, so the impulses are read rather than the ramp. */
    WorldCameraOrbit_Reset(&orbit);
    orbit.pitch = 250;
    keys = no_keys();
    keys.right = true;
    for( i = 0; i < 40; i++ )
        WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(orbit.yaw_velocity == 23, "right settled at %d", orbit.yaw_velocity);
    CHECK(orbit.pitch_velocity == 0, "turning right also tipped the camera");

    WorldCameraOrbit_Reset(&orbit);
    orbit.pitch = 250;
    keys = no_keys();
    keys.left = true;
    for( i = 0; i < 40; i++ )
        WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(orbit.yaw_velocity == -23, "left settled at %d", orbit.yaw_velocity);

    WorldCameraOrbit_Reset(&orbit);
    orbit.pitch = 250;
    keys = no_keys();
    keys.up = true;
    for( i = 0; i < 40; i++ )
        WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(orbit.pitch_velocity == 11, "up settled at %d", orbit.pitch_velocity);
    CHECK(orbit.yaw_velocity == 0, "looking up also turned the camera");

    WorldCameraOrbit_Reset(&orbit);
    orbit.pitch = 250;
    keys = no_keys();
    keys.down = true;
    for( i = 0; i < 40; i++ )
        WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(orbit.pitch_velocity == -11, "down settled at %d", orbit.pitch_velocity);

    /* Both directions of one axis held picks ONE of them and commits to it --
     * left, because it is tested first. Not an average and not a stall: a
     * camera that fought itself would judder while both are down, and a player
     * rolling their hand across the arrow keys would feel it. */
    WorldCameraOrbit_Reset(&orbit);
    orbit.pitch = 250;
    keys = no_keys();
    keys.left = true;
    keys.right = true;
    for( i = 0; i < 40; i++ )
        WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(
        orbit.yaw_velocity == -23,
        "both yaw keys held produced %d, not a committed turn", orbit.yaw_velocity);
}

static void
test_yaw_wraps_and_pitch_does_not(void)
{
    struct WorldCameraOrbit orbit;
    struct WorldCameraLimits const limits = osrs_limits();
    struct WorldCameraKeys keys = no_keys();
    int i;

    printf("TEST: yaw wraps, pitch stops\n");

    /*
     * There is no far side of a turn, so yaw wraps; there IS a far side of
     * looking up, so pitch is held in the band. A yaw that clamped would stop
     * the camera dead once round, and a pitch that wrapped would flip the
     * world upside down at the top of a drag.
     */
    WorldCameraOrbit_Reset(&orbit);
    orbit.yaw = 2040;
    orbit.pitch = 250;
    keys.right = true;
    for( i = 0; i < 40; i++ )
        WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(orbit.yaw >= 0 && orbit.yaw < 2048, "the yaw left its range: %d", orbit.yaw);
    CHECK(orbit.yaw < 2040, "the yaw did not wrap past the top");

    WorldCameraOrbit_Reset(&orbit);
    orbit.pitch = 380;
    keys = no_keys();
    keys.up = true;
    for( i = 0; i < 200; i++ )
        WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(orbit.pitch == limits.pitch_steepest, "the pitch went past the band to %d", orbit.pitch);

    WorldCameraOrbit_Reset(&orbit);
    orbit.pitch = 130;
    keys = no_keys();
    keys.down = true;
    for( i = 0; i < 200; i++ )
        WorldCameraOrbit_StepAngles(&orbit, &keys, &limits);
    CHECK(orbit.pitch == limits.pitch_flattest, "the pitch went below the band to %d", orbit.pitch);
}

static void
test_the_terrain_clamp_rises_faster_than_it_falls(void)
{
    struct WorldCameraOrbit rising;
    struct WorldCameraOrbit falling;
    struct WorldCameraLimits const limits = osrs_limits();
    int i;

    printf("TEST: the terrain clamp rises fast and falls slowly\n");

    /*
     * The rule this module exists for, and the one that is invisible either
     * way it breaks. The camera has to CLEAR a hill before the player reaches
     * it, so the clamp rises quickly; it has to come back down slowly enough
     * that cresting a ridge does not drop the view into the ground. Symmetric,
     * one of those two is wrong whichever rate is chosen -- and both failures
     * read as "the terrain popped".
     */
    WorldCameraOrbit_Reset(&rising);
    WorldCameraOrbit_Reset(&falling);
    rising.pitch_clamp = 200 * WORLD_CAMERA_PITCH_CLAMP_SCALE;
    falling.pitch_clamp = 200 * WORLD_CAMERA_PITCH_CLAMP_SCALE;

    /* The same distance, in opposite directions, for the same number of
     * frames. */
    for( i = 0; i < 20; i++ )
    {
        WorldCameraOrbit_EaseTerrainClamp(&rising, &limits, 300);
        WorldCameraOrbit_EaseTerrainClamp(&falling, &limits, 130);
    }
    {
        int const climbed = rising.pitch_clamp - 200 * WORLD_CAMERA_PITCH_CLAMP_SCALE;
        int const dropped = 200 * WORLD_CAMERA_PITCH_CLAMP_SCALE - falling.pitch_clamp;

        CHECK(climbed > 0, "the clamp did not rise at all");
        CHECK(dropped > 0, "the clamp did not fall at all");
        CHECK(
            climbed > dropped * 2,
            "the clamp rose %d and fell %d: the two rates are too close", climbed, dropped);
    }

    /* And neither runs away past the revision's own band: a profile that
     * states a flat camera must not be tipped by a mountain. */
    for( i = 0; i < 2000; i++ )
        WorldCameraOrbit_EaseTerrainClamp(&rising, &limits, 100000);
    CHECK(
        rising.pitch_clamp <= limits.pitch_steepest * WORLD_CAMERA_PITCH_CLAMP_SCALE,
        "a mountain tipped the camera past its band");

    for( i = 0; i < 5000; i++ )
        WorldCameraOrbit_EaseTerrainClamp(&falling, &limits, -100000);
    CHECK(
        falling.pitch_clamp >= limits.pitch_flattest * WORLD_CAMERA_PITCH_CLAMP_SCALE - 1,
        "flat ground flattened the camera past its band: %d",
        falling.pitch_clamp / WORLD_CAMERA_PITCH_CLAMP_SCALE);
}

static void
test_the_clamp_only_ever_raises_the_pitch(void)
{
    struct WorldCameraOrbit orbit;

    printf("TEST: the terrain clamp is a floor, not an override\n");

    /*
     * Ground in the way raises the eye. It must never LOWER one the player has
     * already tipped up, or looking over a wall would be undone by the flat
     * ground the camera happens to be standing on.
     */
    WorldCameraOrbit_Reset(&orbit);
    orbit.pitch = 300;
    orbit.pitch_clamp = 200 * WORLD_CAMERA_PITCH_CLAMP_SCALE;
    CHECK(WorldCameraOrbit_EffectivePitch(&orbit) == 300, "a low clamp pulled the pitch down");

    orbit.pitch_clamp = 350 * WORLD_CAMERA_PITCH_CLAMP_SCALE;
    CHECK(WorldCameraOrbit_EffectivePitch(&orbit) == 350, "a high clamp did not raise the pitch");

    /* The clamp is 24.8, so a fraction of a pitch unit is not a pitch unit --
     * read as whole units it would jitter between two values every frame while
     * the ease crosses a boundary. */
    orbit.pitch_clamp = 350 * WORLD_CAMERA_PITCH_CLAMP_SCALE + 255;
    CHECK(WorldCameraOrbit_EffectivePitch(&orbit) == 350, "the clamp's fraction reached the pitch");
}

static void
test_the_distance(void)
{
    struct WorldCameraLimits limits = osrs_limits();

    printf("TEST: how far back the eye sits\n");

    /* `pitch * 3 + 600`, the reference's own expression, with both terms
     * stated by the profile. */
    CHECK(WorldCameraOrbit_Distance(&limits, 128) == 128 * 3 + 600, "the flattest distance");
    CHECK(WorldCameraOrbit_Distance(&limits, 383) == 383 * 3 + 600, "the steepest distance");
    CHECK(
        WorldCameraOrbit_Distance(&limits, 383) > WorldCameraOrbit_Distance(&limits, 128),
        "tipping the camera over did not pull it back");

    /* The wheel moves the additive term only, which is why it buys less and
     * less as the camera tips over -- the reason the dolly below exists. */
    {
        struct WorldCameraLimits closer = limits;
        closer.rest_zoom = 300;
        CHECK(
            WorldCameraOrbit_Distance(&closer, 128) == 128 * 3 + 300,
            "the wheel did not move the additive term");
        CHECK(
            WorldCameraOrbit_Distance(&closer, 383) == 383 * 3 + 300,
            "the wheel moved something other than the additive term");
    }

    /*
     * The viewport zoom is a later client's way of zooming, and a revision
     * that says it is the 2004 client has said its camera has none. Applying
     * one anyway halves the scale, which reads as the camera being twice as
     * far out -- and that is exactly what was reported against rev 289.
     */
    limits.viewport_zoom_256 = 128;
    CHECK(
        WorldCameraOrbit_Distance(&limits, 200) == (200 * 3 + 600) * 128 / 256,
        "the viewport zoom was not applied");
    limits.viewport_zoom_256 = 0;
    CHECK(
        WorldCameraOrbit_Distance(&limits, 200) == 200 * 3 + 600,
        "a revision with no viewport zoom got one anyway");

    /*
     * The device's own dolly goes over the WHOLE distance, pitch term
     * included, which is the point of it: the band under `rest` moves the
     * additive term only, and overhead `pitch * 3` is most of the distance.
     */
    limits.distance_scale_percent = 50;
    CHECK(
        WorldCameraOrbit_Distance(&limits, 200) == (200 * 3 + 600) / 2,
        "the dolly did not reach the pitch term");
    limits.distance_scale_percent = 100;

    /*
     * And the near plane is the floor, applied LAST. Anything closer is not a
     * closer view, it is a dropped one -- the anchor itself fails the
     * projection's own near test and the whole frame goes.
     */
    limits.rest_zoom = 0;
    limits.distance_scale_percent = 1;
    CHECK(
        WorldCameraOrbit_Distance(&limits, 128) == limits.near_plane_z,
        "a camera dollied inside the near plane was allowed to stay there");
}

static void
test_the_camera_survives_a_reload(void)
{
    struct WorldCameraHold hold;
    int x;
    int y;
    int z;
    int pitch;
    int yaw;

    printf("TEST: the camera is held across a world reload\n");

    /*
     * A hold that was never captured restores nothing. Against a scene based
     * at the origin, because that is the one where a zeroed hold's
     * coordinates fall INSIDE the scene and the containment test below would
     * happily accept them -- the client would place its camera in the corner
     * of the map on its first load.
     */
    memset(&hold, 0, sizeof(hold));
    CHECK(
        !WorldCameraHold_Restore(&hold, 0, 0, 104, &x, &y, &z, &pitch, &yaw),
        "an empty hold restored something");

    /*
     * Captured in ABSOLUTE coordinates, because the scene window moves. A
     * rebuild of the same region has the same base tile and the camera lands
     * exactly where it was.
     */
    WorldCameraHold_Capture(&hold, 40, 50, 1000, -2000, 2000, 300, 512);
    CHECK(hold.valid, "the capture did not take");
    CHECK(
        WorldCameraHold_Restore(&hold, 40, 50, 104, &x, &y, &z, &pitch, &yaw),
        "the same scene did not restore the camera");
    CHECK(x == 1000 && z == 2000, "the camera came back at %d,%d", x, z);
    CHECK(y == -2000 && pitch == 300 && yaw == 512, "the pose did not come back");

    /*
     * A scene whose window has MOVED puts the camera back where it was in the
     * world, not where it was on the screen. Absolute is the whole reason: the
     * base tile moved by 8 tiles, so the scene-local answer moves by 8 * 128
     * the other way.
     */
    WorldCameraHold_Capture(&hold, 40, 50, 1000, -2000, 2000, 300, 512);
    CHECK(
        WorldCameraHold_Restore(&hold, 32, 50, 104, &x, &y, &z, &pitch, &yaw),
        "a shifted window refused a point it contains");
    CHECK(x == 1000 + 8 * 128, "the shifted window put the camera at %d", x);

    /*
     * And a scene that does NOT contain the held point recentres instead --
     * the square browser opened somewhere distant, and a hold from two worlds
     * ago is meaningless. A containment test rather than a flag, because
     * "same region" is not something the loader knows.
     */
    WorldCameraHold_Capture(&hold, 40, 50, 1000, -2000, 2000, 300, 512);
    CHECK(
        !WorldCameraHold_Restore(&hold, 4000, 50, 104, &x, &y, &z, &pitch, &yaw),
        "a distant scene restored a camera it does not contain");

    /* Spent either way. A hold that survived a failed restore would be applied
     * to some later, unrelated load -- the camera jumping to where it was two
     * worlds ago, which looks like a corrupt save rather than a stale flag. */
    CHECK(!hold.valid, "a failed restore left the hold armed");
    WorldCameraHold_Capture(&hold, 40, 50, 1000, -2000, 2000, 300, 512);
    (void)WorldCameraHold_Restore(&hold, 40, 50, 104, &x, &y, &z, &pitch, &yaw);
    CHECK(!hold.valid, "a successful restore left the hold armed");

    /* The edges of containment: the last fine unit inside the scene is in, and
     * the first one past it is out. */
    WorldCameraHold_Capture(&hold, 40, 50, 104 * 128 - 1, 0, 0, 0, 0);
    CHECK(
        WorldCameraHold_Restore(&hold, 40, 50, 104, &x, &y, &z, &pitch, &yaw),
        "the last unit inside the scene was refused");
    WorldCameraHold_Capture(&hold, 40, 50, 104 * 128, 0, 0, 0, 0);
    CHECK(
        !WorldCameraHold_Restore(&hold, 40, 50, 104, &x, &y, &z, &pitch, &yaw),
        "the first unit past the scene was accepted");
    WorldCameraHold_Capture(&hold, 40, 50, -1, 0, 0, 0, 0);
    CHECK(
        !WorldCameraHold_Restore(&hold, 40, 50, 104, &x, &y, &z, &pitch, &yaw),
        "a point before the scene was accepted");

    /* Both axes, separately. The scene is square and the two tests are written
     * out twice, so a containment that only checks one lets a camera through
     * that is off the north edge -- which is a projection that drops the whole
     * frame rather than a camera that looks odd. */
    WorldCameraHold_Capture(&hold, 40, 50, 0, 0, 104 * 128, 0, 0);
    CHECK(
        !WorldCameraHold_Restore(&hold, 40, 50, 104, &x, &y, &z, &pitch, &yaw),
        "a point past the north edge was accepted");
    WorldCameraHold_Capture(&hold, 40, 50, 0, 0, -1, 0, 0);
    CHECK(
        !WorldCameraHold_Restore(&hold, 40, 50, 104, &x, &y, &z, &pitch, &yaw),
        "a point south of the scene was accepted");
    WorldCameraHold_Capture(&hold, 40, 50, 0, 0, 104 * 128 - 1, 0, 0);
    CHECK(
        WorldCameraHold_Restore(&hold, 40, 50, 104, &x, &y, &z, &pitch, &yaw),
        "the last unit inside the scene's north edge was refused");
}

int
main(void)
{
    test_the_pitch_band();
    test_a_held_key_accelerates_and_a_released_one_coasts();
    test_the_two_axes_and_their_directions();
    test_yaw_wraps_and_pitch_does_not();
    test_the_terrain_clamp_rises_faster_than_it_falls();
    test_the_clamp_only_ever_raises_the_pitch();
    test_the_distance();
    test_the_camera_survives_a_reload();

    if( g_failures )
    {
        printf("world_camera_orbit_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("world_camera_orbit_test: OK\n");
    return 0;
}
