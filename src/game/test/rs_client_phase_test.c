/*
 * The client's lifecycle: a bake that starts or settles, the screen each
 * event puts up, and the logout hold.
 *
 * A hold that outlives its bake leaves a settled title screen refusing to
 * paint; a bake that forgets to clear it freezes every later bake too.
 *
 *   make -C src test-rs-client-phase
 */
#include "game/rs_client_phase.h"

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

static void
test_reset(void)
{
    struct RS_ClientPhase phase;

    printf("TEST: reset is a client that has not started\n");

    memset(&phase, 0x5a, sizeof(phase));
    RS_ClientPhase_Reset(&phase);
    CHECK(RS_ClientPhase_Booting(&phase), "booting");
    CHECK(!RS_ClientPhase_Ready(&phase), "not ready");
    CHECK(RS_ClientPhase_Screen(&phase) == APP_SCREEN_BOOT, "screen %d", RS_ClientPhase_Screen(&phase));
    CHECK(!RS_ClientPhase_OnTitle(&phase), "not on the title");
    CHECK(!RS_ClientPhase_HoldsLastFrame(&phase), "holds nothing");

    /* A zeroed phase is the same client: App is memset before it boots. */
    memset(&phase, 0, sizeof(phase));
    CHECK(RS_ClientPhase_Booting(&phase), "zeroed is booting");
    CHECK(RS_ClientPhase_Screen(&phase) == APP_SCREEN_BOOT, "zeroed is the boot screen");
}

static void
test_bake_cycle(void)
{
    struct RS_ClientPhase phase;

    printf("TEST: a bake starts BOOTING and settles READY, the screen untouched\n");

    RS_ClientPhase_Reset(&phase);
    RS_ClientPhase_BootBake(&phase);
    RS_ClientPhase_BakeStarted(&phase);
    CHECK(RS_ClientPhase_Booting(&phase), "started bake is booting");
    RS_ClientPhase_BakeSettled(&phase);
    CHECK(RS_ClientPhase_Ready(&phase), "settled bake is ready");
    CHECK(!RS_ClientPhase_Booting(&phase), "settled bake is not booting");
    CHECK(RS_ClientPhase_Screen(&phase) == APP_SCREEN_BOOT, "bake does not move the screen");

    /* The title swap: the screen changes, then a new bake starts. */
    RS_ClientPhase_ShowTitle(&phase);
    CHECK(RS_ClientPhase_Ready(&phase), "the screen does not touch the bake state");
    RS_ClientPhase_BakeStarted(&phase);
    CHECK(RS_ClientPhase_Booting(&phase), "a title tree can be booting");
    CHECK(RS_ClientPhase_OnTitle(&phase), "title is on the title");
}

static void
test_screens(void)
{
    struct RS_ClientPhase phase;

    printf("TEST: each event puts up its screen\n");

    RS_ClientPhase_Reset(&phase);
    RS_ClientPhase_ShowTitle(&phase);
    CHECK(RS_ClientPhase_Screen(&phase) == APP_SCREEN_TITLE, "title %d", RS_ClientPhase_Screen(&phase));
    RS_ClientPhase_LoginSubmitted(&phase);
    CHECK(RS_ClientPhase_Screen(&phase) == APP_SCREEN_CONNECTING, "connecting %d", RS_ClientPhase_Screen(&phase));
    CHECK(RS_ClientPhase_OnTitle(&phase), "connecting is still the title tree");
    /* Refused: back to the form. */
    RS_ClientPhase_ShowTitle(&phase);
    CHECK(RS_ClientPhase_Screen(&phase) == APP_SCREEN_TITLE, "refused back to title");
    RS_ClientPhase_LoginSubmitted(&phase);
    RS_ClientPhase_GameRooted(&phase);
    CHECK(RS_ClientPhase_Screen(&phase) == APP_SCREEN_GAME, "game %d", RS_ClientPhase_Screen(&phase));
    CHECK(!RS_ClientPhase_OnTitle(&phase), "game is not the title");
    /* Re-rooting the gameframe keeps it the game. */
    RS_ClientPhase_GameRooted(&phase);
    CHECK(RS_ClientPhase_Screen(&phase) == APP_SCREEN_GAME, "re-root stays game");
    RS_ClientPhase_BootBake(&phase);
    CHECK(RS_ClientPhase_Screen(&phase) == APP_SCREEN_BOOT, "boot bake %d", RS_ClientPhase_Screen(&phase));
}

static void
test_logout_hold(void)
{
    struct RS_ClientPhase phase;

    printf("TEST: a logout holds only the bake it was raised for\n");

    RS_ClientPhase_Reset(&phase);
    RS_ClientPhase_GameRooted(&phase);
    RS_ClientPhase_BakeSettled(&phase);

    /* The logout's order: the title bake starts, then the hold is raised. */
    RS_ClientPhase_ShowTitle(&phase);
    RS_ClientPhase_BakeStarted(&phase);
    RS_ClientPhase_LogoutHold(&phase);
    CHECK(RS_ClientPhase_HoldsLastFrame(&phase), "the logout's bake holds");

    RS_ClientPhase_BakeSettled(&phase);
    CHECK(!RS_ClientPhase_HoldsLastFrame(&phase), "a settled bake never holds");
    CHECK(phase.hold_last_frame == 0, "settling clears the flag");

    /* Raised while READY, the hold is not honoured -- and the next bake that
     * starts clears it. */
    RS_ClientPhase_LogoutHold(&phase);
    CHECK(!RS_ClientPhase_HoldsLastFrame(&phase), "a ready phase does not hold");
    RS_ClientPhase_BakeStarted(&phase);
    CHECK(!RS_ClientPhase_HoldsLastFrame(&phase), "a new bake may paint");
}

int
main(void)
{
    test_reset();
    test_bake_cycle();
    test_screens();
    test_logout_hold();
    if( g_failures )
    {
        printf("rs_client_phase: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_client_phase: all checks passed\n");
    return 0;
}
