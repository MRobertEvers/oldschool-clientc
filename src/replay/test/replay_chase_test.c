/*
 * A replay seek's chase: the tile it states and the cycles each chase tick
 * charges the client clock.
 *
 * A wrong charge does not fail anything visibly: the seek lands, and every
 * clientclock a script reads afterwards is off by the cycles that were
 * charged twice or not at all. A tile that is not cleared keeps `coord`
 * reading the recording's tile after the world is back.
 *
 *   make -C src test-replay-chase
 */
#include "replay/replay_chase.h"

#include <stdint.h>
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
test_a_zeroed_chase_states_no_tile(void)
{
    struct ReplayChase chase;

    printf("TEST: a zeroed chase states no tile\n");
    memset(&chase, 0, sizeof(chase));
    CHECK(!ReplayChase_HasCoord(&chase), "a zeroed chase states a tile");
}

static void
test_a_stated_tile_holds_until_cleared(void)
{
    struct ReplayChase chase;
    int const coord = (1 << 28) | (3222 << 14) | 3218;

    printf("TEST: a stated tile holds until it is cleared, and the latest wins\n");
    memset(&chase, 0, sizeof(chase));
    ReplayChase_StateCoord(&chase, coord);
    CHECK(ReplayChase_HasCoord(&chase), "the stated tile is not held");
    CHECK(ReplayChase_Coord(&chase) == coord, "coord %d, want %d", ReplayChase_Coord(&chase), coord);
    ReplayChase_StateCoord(&chase, coord + 1);
    CHECK(ReplayChase_Coord(&chase) == coord + 1, "the next tick's tile did not replace the first");
    ReplayChase_ClearCoord(&chase);
    CHECK(!ReplayChase_HasCoord(&chase), "a cleared chase still states a tile");
}

static void
test_clearing_the_tile_keeps_the_clock(void)
{
    struct ReplayChase chase;
    int64_t charge;

    printf("TEST: clearing the tile leaves the clock where it was\n");
    memset(&chase, 0, sizeof(chase));
    ReplayChase_StateCoord(&chase, 7);
    (void)ReplayChase_Charge(&chase, 300, true, 1000);
    ReplayChase_ClearCoord(&chase);
    CHECK(chase.cycle == 300, "cycle %lld, want 300", (long long)chase.cycle);
    CHECK(chase.logic_cycle == 1000, "logic cycle %llu, want 1000",
          (unsigned long long)chase.logic_cycle);
    /* The next tick still measures from the cleared chase's clock. */
    charge = ReplayChase_Charge(&chase, 330, false, 1000);
    CHECK(charge == 30, "charge %lld, want 30", (long long)charge);
}

static void
test_a_first_tick_charges_its_cycle_whole(void)
{
    struct ReplayChase chase;
    int64_t charge;

    printf("TEST: a chase's first tick charges its named cycle whole\n");
    memset(&chase, 0, sizeof(chase));
    chase.cycle = 99999;
    chase.logic_cycle = 5;
    charge = ReplayChase_Charge(&chase, 1234, true, 800);
    CHECK(charge == 1234, "charge %lld, want 1234", (long long)charge);
    CHECK(chase.cycle == 1234, "cycle %lld, want 1234", (long long)chase.cycle);
    CHECK(chase.logic_cycle == 800, "logic cycle %llu, want 800",
          (unsigned long long)chase.logic_cycle);
}

static void
test_a_tick_charges_what_it_did_not_run(void)
{
    struct ReplayChase chase;
    int64_t charge;

    printf("TEST: a later tick charges the gap less the cycles this client ran\n");
    memset(&chase, 0, sizeof(chase));
    (void)ReplayChase_Charge(&chase, 600, true, 100);
    /* 30 cycles named, 1 run: 29 charged. */
    charge = ReplayChase_Charge(&chase, 630, false, 101);
    CHECK(charge == 29, "charge %lld, want 29", (long long)charge);
    CHECK(chase.cycle == 630, "cycle %lld, want 630", (long long)chase.cycle);
    CHECK(chase.logic_cycle == 101, "logic cycle %llu, want 101",
          (unsigned long long)chase.logic_cycle);
    /* Exactly the cycles named were run: nothing charged. */
    charge = ReplayChase_Charge(&chase, 660, false, 131);
    CHECK(charge == 0, "charge %lld, want 0", (long long)charge);
    CHECK(chase.cycle == 660, "cycle %lld, want 660", (long long)chase.cycle);
}

static void
test_a_tick_held_up_moves_the_chase_cycle_up(void)
{
    struct ReplayChase chase;
    int64_t charge;

    printf("TEST: a tick held up past its named cycle charges nothing and moves the cycle up\n");
    memset(&chase, 0, sizeof(chase));
    (void)ReplayChase_Charge(&chase, 600, true, 100);
    /* 30 named, 45 run: the chase ran late too, to 645. */
    charge = ReplayChase_Charge(&chase, 630, false, 145);
    CHECK(charge == 0, "charge %lld, want 0", (long long)charge);
    CHECK(chase.cycle == 645, "cycle %lld, want 645", (long long)chase.cycle);
    CHECK(chase.logic_cycle == 145, "logic cycle %llu, want 145",
          (unsigned long long)chase.logic_cycle);
    /* The next tick measures from the raised cycle: 660 named, 5 run. */
    charge = ReplayChase_Charge(&chase, 660, false, 150);
    CHECK(charge == 10, "charge %lld, want 10", (long long)charge);
    CHECK(chase.cycle == 660, "cycle %lld, want 660", (long long)chase.cycle);
}

static void
test_ran_is_a_signed_difference(void)
{
    struct ReplayChase chase;
    int64_t charge;

    printf("TEST: a logic cycle behind the recorded one counts as negative cycles run\n");
    memset(&chase, 0, sizeof(chase));
    (void)ReplayChase_Charge(&chase, 600, true, 100);
    /* ran = -10: the floor is 590, the charge 630 - 600 + 10. */
    charge = ReplayChase_Charge(&chase, 630, false, 90);
    CHECK(charge == 40, "charge %lld, want 40", (long long)charge);
    CHECK(chase.cycle == 630, "cycle %lld, want 630", (long long)chase.cycle);
    CHECK(chase.logic_cycle == 90, "logic cycle %llu, want 90",
          (unsigned long long)chase.logic_cycle);
}

int
main(void)
{
    test_a_zeroed_chase_states_no_tile();
    test_a_stated_tile_holds_until_cleared();
    test_clearing_the_tile_keeps_the_clock();
    test_a_first_tick_charges_its_cycle_whole();
    test_a_tick_charges_what_it_did_not_run();
    test_a_tick_held_up_moves_the_chase_cycle_up();
    test_ran_is_a_signed_difference();
    if( g_failures )
    {
        printf("replay_chase_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("replay_chase_test: all passed\n");
    return 0;
}
