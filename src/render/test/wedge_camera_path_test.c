/*
 * The scripted capture camera (TORIRS_WEDGE_CAM_PATH).
 *
 * This route exists so that two builds of the renderer can be compared over
 * the same geometry. That only holds if the path is a bit-exact function of
 * the frame ordinal, and if a spec that does not fully parse yields no path at
 * all rather than a truncated one -- a half-read route is a camera quietly
 * measuring somewhere else, and the counter columns then compare two different
 * scenes while looking like they compare one.
 *
 * What is asserted:
 *
 *   - a malformed spec parses as nothing. Every way a spec can be wrong
 *     (unknown mode, unknown wrap, zero frames, one waypoint, trailing
 *     garbage, a short waypoint) is rejected, because each of them is a
 *     plausible typo and none of them may half-succeed.
 *   - the three wrap modes do what they say at the seam: loop returns to the
 *     first waypoint, pingpong turns round at the end, hold stops there.
 *   - yaw takes the short way round. 1900 -> 100 is +248, not -1800, so the
 *     camera does not spin almost a full turn to reach somewhere it was next
 *     to; and a looping orbit keeps turning the same way across the seam
 *     rather than unwinding at it.
 *   - both angles come back inside [0, 2048) even from a spline, which
 *     overshoots its control points.
 *   - evaluation is pure: the same frame ordinal gives the same key, twice.
 *
 * Build and run:
 *   make -C src test-wedge-camera-path
 */

#include "render/torirs_wedge_camera_path.h"

#include <stdio.h>
#include <stdlib.h>

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

static struct ToriRS_WedgeCameraKey
eval_at(char const* spec, long frame)
{
    struct ToriRS_WedgeCameraPath path;
    struct ToriRS_WedgeCameraKey key;

    if( !ToriRS_WedgeCameraPathParse(spec, &path) )
    {
        printf("FAIL: spec did not parse but was expected to: %s\n", spec);
        g_failures++;
        exit(1);
    }
    ToriRS_WedgeCameraPathEval(&path, frame, &key);
    return key;
}

static void
test_rejects_everything_it_cannot_fully_read(void)
{
    struct ToriRS_WedgeCameraPath path;
    static char const* const bad[] = {
        "",
        "orbit,100,loop;0,0,0,0,0;128,0,0,0,0",          /* unknown mode */
        "linear,100,bounce;0,0,0,0,0;128,0,0,0,0",       /* unknown wrap */
        "linear,0,loop;0,0,0,0,0;128,0,0,0,0",           /* zero frames */
        "linear,-5,loop;0,0,0,0,0;128,0,0,0,0",          /* negative frames */
        "linear,100,loop;0,0,0,0,0",                     /* one waypoint */
        "linear,100,loop",                               /* no waypoints */
        "linear,100,loop;0,0,0,0;128,0,0,0,0",           /* short waypoint */
        "linear,100,loop;0,0,0,0,0;128,0,0,0,0;garbage", /* trailing junk */
        "linear,100,loop;0,0,0,0,0;128,0,0,0,0 trailing",
    };

    for( size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++ )
        CHECK(!ToriRS_WedgeCameraPathParse(bad[i], &path), "accepted a bad spec: \"%s\"", bad[i]);

    CHECK(
        ToriRS_WedgeCameraPathParse("linear,100,loop;0,0,0,0,0;128,0,0,0,0", &path),
        "rejected a good spec");
    CHECK(path.count == 2, "count %d, want 2", path.count);
    CHECK(path.frames == 100, "frames %ld, want 100", path.frames);
    CHECK(path.mode == TORIRS_WEDGE_CAMERA_LINEAR, "mode %d", path.mode);
    CHECK(path.wrap == TORIRS_WEDGE_CAMERA_LOOP, "wrap %d", path.wrap);

    /* Whitespace around the separators is the spelling a person actually
     * types into a shell, so it must read the same as the tight form. */
    CHECK(
        ToriRS_WedgeCameraPathParse(" linear , 100 , loop ; 0 , 0 , 0 , 0 , 0 ; 128 , 0 , 0 , 0 , 0 ", &path),
        "rejected a spaced spec");
    CHECK(path.count == 2, "spaced count %d, want 2", path.count);
    CHECK(path.keys[1].x == 128, "spaced key x %d, want 128", path.keys[1].x);
}

static void
test_endpoints_and_wrap(void)
{
    char const* const loop = "linear,100,loop;0,0,0,0,0;1000,0,0,0,0";
    char const* const hold = "linear,100,hold;0,0,0,0,0;1000,0,0,0,0";
    char const* const pingpong = "linear,100,pingpong;0,0,0,0,0;1000,0,0,0,0";
    struct ToriRS_WedgeCameraKey key;

    key = eval_at(loop, 0);
    CHECK(key.x == 0, "loop frame 0 x %d, want 0", key.x);

    /* A loop has as many legs as waypoints: the second half of its 100 frames
     * is the closing leg back to waypoint 0, so it is home again at 100. */
    key = eval_at(loop, 50);
    CHECK(key.x == 1000, "loop frame 50 x %d, want 1000 (at waypoint 1)", key.x);
    key = eval_at(loop, 100);
    CHECK(key.x == 0, "loop frame 100 x %d, want 0 (back at the start)", key.x);
    key = eval_at(loop, 250);
    CHECK(key.x == 1000, "loop frame 250 x %d, want 1000 (frame 50 of lap 3)", key.x);

    /* Hold has one leg and stops on it; frames past the end do not walk off. */
    key = eval_at(hold, 50);
    CHECK(key.x == 500, "hold frame 50 x %d, want 500 (halfway)", key.x);
    key = eval_at(hold, 100);
    CHECK(key.x == 1000, "hold frame 100 x %d, want 1000 (the end)", key.x);
    key = eval_at(hold, 100000);
    CHECK(key.x == 1000, "hold frame 100000 x %d, want 1000 (still the end)", key.x);

    /* Pingpong turns round: frame 150 is 50 frames back from the end. */
    key = eval_at(pingpong, 100);
    CHECK(key.x == 1000, "pingpong frame 100 x %d, want 1000", key.x);
    key = eval_at(pingpong, 150);
    CHECK(key.x == 500, "pingpong frame 150 x %d, want 500 (walking back)", key.x);
    key = eval_at(pingpong, 200);
    CHECK(key.x == 0, "pingpong frame 200 x %d, want 0 (home)", key.x);
}

static void
test_yaw_takes_the_short_way(void)
{
    struct ToriRS_WedgeCameraPath path;
    struct ToriRS_WedgeCameraKey key;

    /* 1900 -> 100 crosses the seam. The short way is +248, so halfway the
     * camera is past 2048, i.e. normalised to a small angle -- NOT at 1000,
     * which is where a naive interpolation between the raw numbers lands. */
    CHECK(
        ToriRS_WedgeCameraPathParse("linear,100,hold;0,0,0,0,1900;0,0,0,0,100", &path),
        "seam spec did not parse");
    CHECK(path.keys[1].yaw == 2148, "unwrapped yaw %d, want 2148 (1900 + 248)", path.keys[1].yaw);

    ToriRS_WedgeCameraPathEval(&path, 50, &key);
    CHECK(key.yaw == 2024 % 2048, "seam midpoint yaw %d, want 2024", key.yaw);
    CHECK(key.yaw >= 0 && key.yaw < 2048, "yaw %d outside [0,2048)", key.yaw);

    /* And the same seam crossed the other way. 100 -> 1900 is -248, not
     * +1800. Both directions are tested because they are separate branches of
     * the shortest-arc fold, and a test that only crosses one way leaves the
     * other free to be wrong. */
    CHECK(
        ToriRS_WedgeCameraPathParse("linear,100,hold;0,0,0,0,100;0,0,0,0,1900", &path),
        "reverse seam spec did not parse");
    CHECK(
        path.keys[1].yaw == -148,
        "reverse unwrapped yaw %d, want -148 (100 - 248)",
        path.keys[1].yaw);
    ToriRS_WedgeCameraPathEval(&path, 50, &key);
    CHECK(key.yaw == 2024, "reverse seam midpoint yaw %d, want 2024", key.yaw);
    CHECK(key.yaw >= 0 && key.yaw < 2048, "reverse yaw %d outside [0,2048)", key.yaw);

    /* A deliberate quarter-turn orbit: four waypoints 512 apart, looping. One
     * lap is a whole turn in the same direction, which is what `turn` carries
     * across the seam so the fifth leg keeps going rather than unwinding. */
    CHECK(
        ToriRS_WedgeCameraPathParse(
            "linear,400,loop;0,0,0,0,0;0,0,0,0,512;0,0,0,0,1024;0,0,0,0,1536",
            &path),
        "orbit spec did not parse");
    CHECK(path.turn == 2048, "orbit turn %ld, want 2048 (one whole lap)", path.turn);
    ToriRS_WedgeCameraPathEval(&path, 350, &key);
    CHECK(key.yaw == 1792, "orbit frame 350 yaw %d, want 1792 (still turning)", key.yaw);
}

static void
test_spline_stays_in_range_and_is_pure(void)
{
    /* A spline overshoots its control points, so a yaw near the top of the
     * turn can leave [0,2048) before normalisation. It must not come back
     * out that way: the trig tables are indexed by this. */
    char const* const spec =
        "spline,240,loop;0,0,0,40,0;2000,0,0,2040,600;4000,0,600,60,1200;2000,0,1200,2000,1800";
    long frame;

    for( frame = 0; frame < 600; frame += 7 )
    {
        struct ToriRS_WedgeCameraKey a = eval_at(spec, frame);
        struct ToriRS_WedgeCameraKey b = eval_at(spec, frame);

        CHECK(a.yaw >= 0 && a.yaw < 2048, "frame %ld yaw %d outside [0,2048)", frame, a.yaw);
        CHECK(a.pitch >= 0 && a.pitch < 2048, "frame %ld pitch %d outside [0,2048)", frame, a.pitch);
        CHECK(
            a.x == b.x && a.y == b.y && a.z == b.z && a.pitch == b.pitch && a.yaw == b.yaw,
            "frame %ld evaluated differently twice",
            frame);
    }

    /* A negative ordinal is reachable -- the frame counter is a long the
     * caller supplies -- and must land inside the loop, not off the front. */
    {
        struct ToriRS_WedgeCameraKey key = eval_at(spec, -13);
        CHECK(key.yaw >= 0 && key.yaw < 2048, "negative frame yaw %d outside [0,2048)", key.yaw);
    }
}

int
main(void)
{
    test_rejects_everything_it_cannot_fully_read();
    test_endpoints_and_wrap();
    test_yaw_takes_the_short_way();
    test_spline_stays_in_range_and_is_pure();

    if( g_failures )
    {
        printf("wedge_camera_path_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("wedge_camera_path_test: OK\n");
    return 0;
}
