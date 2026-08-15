/*
 * Do WASD go where the camera is looking?
 *
 * The failure mode this catches is a sign error: keys that behave correctly at
 * yaw 0 and invert somewhere past 90 degrees. So it checks the accumulated
 * world offset against the yaw at each quarter turn, and checks that turning
 * after moving does not move you.
 */
#include "ev_render.h"
#include "toridraw.h"

#include <stdio.h>
#include <stdlib.h>

static int fails = 0;

static void
check(const char* what, int got, int want)
{
    int ok = abs(got - want) <= 2; /* fixed-point rounding */
    if( !ok ) fails++;
    printf("  %-42s got=%-6d want=%-6d %s\n", what, got, want, ok ? "ok" : "FAIL");
}

int
main(void)
{
    ToriDraw_Init();

    int x, y, z;

    /* Yaw 0: forward is -z (into the screen), right is +x. */
    ev_move_reset();
    ev_move(100, 0, 0, 0);
    ev_move_get(&x, &y, &z);
    check("yaw 0    W  -> z", z, -100);
    check("yaw 0    W  -> x", x, 0);

    ev_move_reset();
    ev_move(0, 100, 0, 0);
    ev_move_get(&x, &y, &z);
    check("yaw 0    D  -> x", x, 100);
    check("yaw 0    D  -> z", z, 0);

    /* A quarter turn swaps the axes; the sign is what a bad rotation gets wrong. */
    ev_move_reset();
    ev_move(100, 0, 0, 512);
    ev_move_get(&x, &y, &z);
    check("yaw 512  W  -> x", x, -100);
    check("yaw 512  W  -> z", z, 0);

    /* Half a turn must invert, not repeat. */
    ev_move_reset();
    ev_move(100, 0, 0, 1024);
    ev_move_get(&x, &y, &z);
    check("yaw 1024 W  -> z", z, 100);

    ev_move_reset();
    ev_move(100, 0, 0, 1536);
    ev_move_get(&x, &y, &z);
    check("yaw 1536 W  -> x", x, 100);

    /* R/F are the world y axis and must not follow yaw. */
    ev_move_reset();
    ev_move(0, 0, 100, 700);
    ev_move_get(&x, &y, &z);
    check("yaw 700  R  -> y", y, 100);
    check("yaw 700  R  -> x", x, 0);
    check("yaw 700  R  -> z", z, 0);

    /* Turning after moving must not move you: the offset is world-space. */
    ev_move_reset();
    ev_move(100, 0, 0, 0);
    ev_move_get(&x, &y, &z);
    int before_x = x, before_z = z;
    ev_move(0, 0, 0, 1024); /* a turn, no step */
    ev_move_get(&x, &y, &z);
    check("turning after moving keeps x", x, before_x);
    check("turning after moving keeps z", z, before_z);

    /* Opposite keys cancel. */
    ev_move_reset();
    ev_move(100, 50, 25, 333);
    ev_move(-100, -50, -25, 333);
    ev_move_get(&x, &y, &z);
    check("W then S cancels (x)", x, 0);
    check("W then S cancels (y)", y, 0);
    check("W then S cancels (z)", z, 0);

    printf("%s\n", fails ? "FAILURES" : "all movement checks pass");
    return fails ? 1 : 0;
}
