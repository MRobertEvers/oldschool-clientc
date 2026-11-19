/*
 * The boot bar across loud and quiet bakes, the root interface a bake
 * mounts, and the once-per-session preamble latch.
 *
 * A quiet bake that walked the bar back to zero would put a second loading
 * sequence after the first; a latch that a remount cleared would rerun the
 * preamble and zero every varp the server has sent.
 *
 *   make -C src test-rs-boot-state
 */
#include "game/rs_boot_state.h"

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
    struct RS_BootState boot;

    printf("TEST: reset is nothing booted\n");

    memset(&boot, 0x5a, sizeof(boot));
    RS_BootState_Reset(&boot);
    CHECK(RS_BootState_Progress(&boot) == 0, "progress %d", RS_BootState_Progress(&boot));
    CHECK(RS_BootState_InterfaceId(&boot) == 0, "interface %d", RS_BootState_InterfaceId(&boot));
    CHECK(!RS_BootState_ConfigReady(&boot), "preamble not run");
    CHECK(boot.preview.failed == 0, "no preview failure");
    CHECK(boot.preview.stat_mask == 0, "no preview stats");
}

static void
test_loud_and_quiet_bakes(void)
{
    struct RS_BootState boot;

    printf("TEST: a loud bake walks the bar back, a quiet one leaves it\n");

    RS_BootState_Reset(&boot);
    RS_BootState_BakeStarted(&boot, -1, false);
    CHECK(RS_BootState_InterfaceId(&boot) == -1, "title tree has no root");
    RS_BootState_SetProgress(&boot, 10);
    RS_BootState_SetProgress(&boot, 60);
    RS_BootState_SetProgress(&boot, 100);
    CHECK(RS_BootState_Progress(&boot) == 100, "bar at %d", RS_BootState_Progress(&boot));

    /* The post-login gameframe rebake is quiet: the bar stays parked. */
    RS_BootState_BakeStarted(&boot, 161, true);
    CHECK(RS_BootState_Progress(&boot) == 100, "quiet bake moved the bar to %d", RS_BootState_Progress(&boot));
    CHECK(RS_BootState_InterfaceId(&boot) == 161, "root %d", RS_BootState_InterfaceId(&boot));

    /* A loud one starts over. */
    RS_BootState_BakeStarted(&boot, 548, false);
    CHECK(RS_BootState_Progress(&boot) == 0, "loud bake left the bar at %d", RS_BootState_Progress(&boot));
    CHECK(RS_BootState_InterfaceId(&boot) == 548, "root %d", RS_BootState_InterfaceId(&boot));
}

static void
test_config_latch(void)
{
    struct RS_BootState boot;

    printf("TEST: the preamble latch survives every later bake\n");

    RS_BootState_Reset(&boot);
    CHECK(!RS_BootState_ConfigReady(&boot), "fresh latch is open");
    RS_BootState_MarkConfigReady(&boot);
    CHECK(RS_BootState_ConfigReady(&boot), "marked");
    RS_BootState_BakeStarted(&boot, 164, false);
    RS_BootState_BakeStarted(&boot, 548, true);
    CHECK(RS_BootState_ConfigReady(&boot), "a remount does not reopen it");
}

int
main(void)
{
    test_reset();
    test_loud_and_quiet_bakes();
    test_config_latch();
    if( g_failures )
    {
        printf("rs_boot_state: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_boot_state: all checks passed\n");
    return 0;
}
