#include "pacer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define TEST_ASSERT(cond, msg)                                                                 \
    do                                                                                         \
    {                                                                                          \
        if( !(cond) )                                                                          \
        {                                                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));                    \
            failures++;                                                                        \
        }                                                                                      \
    } while( 0 )

/*
 * Drive `frames` iterations that each take exactly `period_ms` of real time and
 * report how much LOGIC time the pacer handed out across them.
 *
 * This is the property that matters and the one the two pacers are supposed to
 * share: however slowly the machine draws, the world must still advance at 50
 * ticks a second. A pacer that returns fewer ticks than the wall clock owes is
 * a client whose game runs in slow motion.
 *
 * `warmup` iterations run first and are not counted. GameShell's ring is ten
 * iterations long and starts full of the seed timestamp, so until it has turned
 * over once the estimate reads "no time has passed" and the pacer issues one
 * tick per draw whatever the real rate is. That start-up deficit is a fixed
 * one-off, not a drift, and measuring through it would hide the steady-state
 * rate this is actually asserting.
 */
static uint64_t
run_frames(
    struct ToriRS_Pacer* pacer, int warmup, int frames, int period_ms, int* total_ticks)
{
    uint64_t now = 1000;
    uint64_t first_logic = 0;
    uint64_t logic = 0;

    *total_ticks = 0;
    for( int i = 0; i < warmup; i++ )
    {
        ToriRS_Pacer_BeginFrame(pacer, now);
        now += (uint64_t)period_ms;
    }
    for( int i = 0; i < frames; i++ )
    {
        logic = ToriRS_Pacer_BeginFrame(pacer, now);
        if( i == 0 )
            first_logic = logic;
        *total_ticks += ToriRS_Pacer_LastLogicTicks(pacer);
        now += (uint64_t)period_ms;
    }
    return logic - first_logic;
}

static void
test_names(void)
{
    int ok = 0;

    ToriRS_Pacer_KindFromName("gameshell", &ok);
    TEST_ASSERT(ok, "'gameshell' is a known pacer name");

    ok = 0;
    TEST_ASSERT(
        ToriRS_Pacer_KindFromName("deadline", &ok) == TORIRS_PACER_DEADLINE,
        "'deadline' maps to the deadline pacer");
    TEST_ASSERT(ok, "'deadline' is a known pacer name");

    ok = 1;
    ToriRS_Pacer_KindFromName("gamshell", &ok);
    TEST_ASSERT(!ok, "a misspelled pacer name is reported as unknown");

    TEST_ASSERT(
        strcmp(ToriRS_Pacer_KindName(TORIRS_PACER_GAMESHELL), "gameshell") == 0,
        "gameshell kind names itself");
    TEST_ASSERT(
        strcmp(ToriRS_Pacer_KindName(TORIRS_PACER_DEADLINE), "deadline") == 0,
        "deadline kind names itself");
}

static void
test_deadline_pacer(void)
{
    struct ToriRS_Pacer pacer;

    ToriRS_Pacer_Init(&pacer, TORIRS_PACER_DEADLINE, 20, 1);

    TEST_ASSERT(
        ToriRS_Pacer_BeginFrame(&pacer, 12345) == 12345,
        "the deadline pacer hands App_RunOnce the wall clock untouched");
    TEST_ASSERT(
        ToriRS_Pacer_LastLogicTicks(&pacer) == 0,
        "the deadline pacer counts no ticks of its own -- App_RunOnce does that");
    TEST_ASSERT(
        ToriRS_Pacer_WaitDeadline(&pacer, 1000, 1017) == 1020,
        "the deadline pacer waits to frame start + period, not to now + period");
    TEST_ASSERT(
        ToriRS_Pacer_WaitDeadline(&pacer, 1000, 1099) == 1020,
        "a deadline already past stays past, so an overrun costs no extra wait");
}

/* On budget the reference runs exactly one logic tick per draw. */
static void
test_gameshell_on_budget(void)
{
    struct ToriRS_Pacer pacer;
    int ticks = 0;
    uint64_t logic;

    ToriRS_Pacer_Init(&pacer, TORIRS_PACER_GAMESHELL, 20, 1);
    logic = run_frames(&pacer, 20, 200, 20, &ticks);

    TEST_ASSERT(pacer.ratio == 256, "a client meeting its 20 ms budget settles at ratio 256");
    TEST_ASSERT(ticks == 200, "one logic tick per draw when every frame fits the budget");
    TEST_ASSERT(logic == 200 * 20 - 20, "the logic clock advances 20 ms per tick");
}

/*
 * Half rate. The draw rate halves and the tick count per draw doubles, which is
 * the whole point of the design: logic is still 50 a second.
 */
static void
test_gameshell_catches_up(void)
{
    struct ToriRS_Pacer pacer;
    int ticks = 0;

    ToriRS_Pacer_Init(&pacer, TORIRS_PACER_GAMESHELL, 20, 1);
    run_frames(&pacer, 20, 200, 40, &ticks);

    TEST_ASSERT(pacer.ratio == 128, "a client at half rate settles at ratio 128");
    TEST_ASSERT(ticks == 400, "two logic ticks per draw at half the draw rate");
}

/*
 * The rate need not divide the tick evenly. At 30 ms a frame the tick count
 * alternates, and only the AVERAGE is right -- which is what `count` carrying
 * its remainder across iterations buys.
 */
static void
test_gameshell_holds_logic_rate(void)
{
    int const periods[] = {20, 25, 30, 40, 55, 80};

    for( int i = 0; i < (int)(sizeof periods / sizeof periods[0]); i++ )
    {
        struct ToriRS_Pacer pacer;
        int ticks = 0;
        int const frames = 400;
        int const elapsed_ms = frames * periods[i];
        int expected = elapsed_ms / 20;
        int drift;

        ToriRS_Pacer_Init(&pacer, TORIRS_PACER_GAMESHELL, 20, 1);
        run_frames(&pacer, 20, frames, periods[i], &ticks);

        drift = ticks - expected;
        if( drift < 0 )
            drift = -drift;
        /* Two ticks of slack for the `count` remainder in flight at either end
         * of the window; anything beyond that is the logic clock genuinely
         * losing time against the wall clock. */
        TEST_ASSERT(drift <= 2, "logic ticks track the wall clock at every draw rate");
    }
}

/*
 * The floor under the wait, and the reason the Java client gives up 41 % of its
 * frame on the XP box. `mindel` is what it cannot go below; 0 removes it.
 */
static void
test_gameshell_mindel_floor(void)
{
    struct ToriRS_Pacer pacer;
    int ticks = 0;

    ToriRS_Pacer_Init(&pacer, TORIRS_PACER_GAMESHELL, 20, 1);
    run_frames(&pacer, 20, 50, 40, &ticks);
    TEST_ASSERT(pacer.del_ms == 1, "a client that is behind falls back to the mindel floor");
    TEST_ASSERT(
        ToriRS_Pacer_WaitDeadline(&pacer, 1000, 5000) == 5001,
        "the gameshell wait is a duration from now, not a deadline from frame start");

    ToriRS_Pacer_Init(&pacer, TORIRS_PACER_GAMESHELL, 20, 5);
    run_frames(&pacer, 20, 50, 40, &ticks);
    TEST_ASSERT(pacer.del_ms == 5, "a larger mindel raises the floor a behind frame waits");

    ToriRS_Pacer_Init(&pacer, TORIRS_PACER_GAMESHELL, 20, 0);
    run_frames(&pacer, 20, 50, 40, &ticks);
    TEST_ASSERT(pacer.del_ms == 0, "mindel 0 removes the floor");
    TEST_ASSERT(
        ToriRS_Pacer_WaitDeadline(&pacer, 1000, 5000) == 5000,
        "with no floor a client that is behind does not wait at all");
}

int
main(void)
{
    test_names();
    test_deadline_pacer();
    test_gameshell_on_budget();
    test_gameshell_catches_up();
    test_gameshell_holds_logic_rate();
    test_gameshell_mindel_floor();

    if( failures )
    {
        fprintf(stderr, "pacer_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("pacer_test: ok\n");
    return 0;
}
