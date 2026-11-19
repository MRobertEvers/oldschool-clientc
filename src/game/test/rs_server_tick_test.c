/*
 * The server tick fence: when a tick is open, what an open tick remembers, and
 * how many ticks have run.
 *
 * Each failure here reads on screen as something else. A tick that never
 * closes freezes the picture on the last committed frame; one that closes
 * early publishes half a tick; a varp-only mark that sticks past its tick
 * sends every later REBUILD down the blocking path; and a serial that skips or
 * repeats makes a loc change look built a tick early or late.
 *
 *   make -C src test-rs-server-tick
 */
#include "game/rs_server_tick.h"

#include <stdint.h>
#include <stdio.h>

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

#define MAX_CYCLES 30u

static void
test_a_reset_tick_is_closed_and_unfenced(void)
{
    struct RS_ServerTick tick;

    printf("TEST: a reset tick is closed, has seen no fence and counted nothing\n");

    RS_ServerTick_Reset(&tick);
    CHECK(!RS_ServerTick_IsOpen(&tick), "a fresh tick claimed to be open");
    CHECK(!RS_ServerTick_FenceSeen(&tick), "a fresh tick claimed a fence");
    CHECK(!RS_ServerTick_OpenNonVar(&tick), "a fresh tick claimed a non-varp packet");
    CHECK(RS_ServerTick_Serial(&tick) == 0, "a fresh tick counted %u", RS_ServerTick_Serial(&tick));
    CHECK(!RS_ServerTick_Overdue(&tick, 1000, MAX_CYCLES), "a closed tick was overdue");
}

static void
test_the_first_packet_opens_and_the_fence_closes(void)
{
    struct RS_ServerTick tick;

    printf("TEST: the first UI packet opens the tick, later ones do not reopen it, the fence closes it\n");

    RS_ServerTick_Reset(&tick);
    CHECK(RS_ServerTick_NotePacket(&tick, false, 100), "the first packet did not report the open");
    CHECK(RS_ServerTick_IsOpen(&tick), "the tick is not open after its first packet");
    CHECK(!RS_ServerTick_NotePacket(&tick, false, 105), "a second packet reported an open");
    CHECK(!RS_ServerTick_Overdue(&tick, 100 + MAX_CYCLES - 1, MAX_CYCLES),
        "overdue a cycle early: the second packet moved the opening cycle");
    CHECK(RS_ServerTick_Overdue(&tick, 100 + MAX_CYCLES, MAX_CYCLES),
        "not overdue at the bound, measured from the opening packet");

    RS_ServerTick_Fence(&tick);
    CHECK(!RS_ServerTick_IsOpen(&tick), "the fence left the tick open");
    CHECK(RS_ServerTick_FenceSeen(&tick), "the fence was not remembered");
    CHECK(RS_ServerTick_Serial(&tick) == 1, "one fence counted %u ticks", RS_ServerTick_Serial(&tick));
    CHECK(!RS_ServerTick_Overdue(&tick, 100000, MAX_CYCLES), "a fenced tick was overdue");

    CHECK(RS_ServerTick_NotePacket(&tick, true, 200), "the next tick's first packet did not open it");
    RS_ServerTick_Fence(&tick);
    CHECK(RS_ServerTick_Serial(&tick) == 2, "two fences counted %u ticks", RS_ServerTick_Serial(&tick));
}

static void
test_the_varp_only_mark_is_per_tick(void)
{
    struct RS_ServerTick tick;

    printf("TEST: a tick is varps-only until a non-varp packet, and the next tick starts again\n");

    RS_ServerTick_Reset(&tick);
    RS_ServerTick_NotePacket(&tick, true, 10);
    CHECK(!RS_ServerTick_OpenNonVar(&tick), "a varp opening marked the tick non-varp");
    RS_ServerTick_NotePacket(&tick, true, 11);
    CHECK(!RS_ServerTick_OpenNonVar(&tick), "a second varp marked the tick non-varp");
    RS_ServerTick_NotePacket(&tick, false, 12);
    CHECK(RS_ServerTick_OpenNonVar(&tick), "an interface packet did not mark the tick");
    RS_ServerTick_NotePacket(&tick, true, 13);
    CHECK(RS_ServerTick_OpenNonVar(&tick), "a later varp cleared the mark inside the same tick");

    RS_ServerTick_Fence(&tick);
    RS_ServerTick_NotePacket(&tick, true, 20);
    CHECK(!RS_ServerTick_OpenNonVar(&tick), "the mark outlived its tick");

    RS_ServerTick_Fence(&tick);
    RS_ServerTick_NotePacket(&tick, false, 30);
    CHECK(RS_ServerTick_OpenNonVar(&tick), "a non-varp opening packet did not mark its tick");
}

static void
test_a_tick_without_a_fence_is_counted_only(void)
{
    struct RS_ServerTick tick;

    printf("TEST: a tick ended without a fence is counted and changes nothing else\n");

    RS_ServerTick_Reset(&tick);
    RS_ServerTick_EndUnfenced(&tick);
    RS_ServerTick_EndUnfenced(&tick);
    CHECK(RS_ServerTick_Serial(&tick) == 2, "two unfenced ticks counted %u", RS_ServerTick_Serial(&tick));
    CHECK(!RS_ServerTick_FenceSeen(&tick), "an unfenced tick claimed a fence");

    RS_ServerTick_NotePacket(&tick, false, 5);
    RS_ServerTick_EndUnfenced(&tick);
    CHECK(RS_ServerTick_IsOpen(&tick), "an unfenced tick end closed an open tick");
}

static void
test_close_is_the_recovery(void)
{
    struct RS_ServerTick tick;

    printf("TEST: closing without a fence counts nothing and remembers no fence\n");

    RS_ServerTick_Reset(&tick);
    RS_ServerTick_NotePacket(&tick, false, 50);
    CHECK(RS_ServerTick_Overdue(&tick, 50 + MAX_CYCLES, MAX_CYCLES), "the lost fence was not overdue");
    RS_ServerTick_Close(&tick);
    CHECK(!RS_ServerTick_IsOpen(&tick), "close left the tick open");
    CHECK(!RS_ServerTick_Overdue(&tick, 50 + MAX_CYCLES, MAX_CYCLES), "a closed tick stayed overdue");
    CHECK(RS_ServerTick_Serial(&tick) == 0, "close counted a tick");
    CHECK(!RS_ServerTick_FenceSeen(&tick), "close claimed a fence");
}

static void
test_suspend_and_resume_rearm_from_the_resume(void)
{
    struct RS_ServerTick tick;
    bool was_open;

    printf("TEST: a suspended tick is closed for the hold and re-armed from the resume cycle\n");

    RS_ServerTick_Reset(&tick);
    RS_ServerTick_NotePacket(&tick, true, 100);
    was_open = RS_ServerTick_Suspend(&tick);
    CHECK(was_open, "suspend did not report the open tick");
    CHECK(!RS_ServerTick_IsOpen(&tick), "suspend left the tick open");

    RS_ServerTick_Resume(&tick, 500);
    CHECK(RS_ServerTick_IsOpen(&tick), "resume did not re-open the tick");
    CHECK(!RS_ServerTick_OpenNonVar(&tick), "resume changed the varp-only mark");
    CHECK(!RS_ServerTick_Overdue(&tick, 500 + MAX_CYCLES - 1, MAX_CYCLES),
        "the backstop counted the hold: overdue before the bound from the resume");
    CHECK(RS_ServerTick_Overdue(&tick, 500 + MAX_CYCLES, MAX_CYCLES), "not overdue at the bound from the resume");

    RS_ServerTick_Close(&tick);
    CHECK(!RS_ServerTick_Suspend(&tick), "suspending a closed tick reported it open");
    CHECK(!RS_ServerTick_IsOpen(&tick), "suspending a closed tick opened it");
}

static void
test_the_backstop_reads_a_truncated_cycle(void)
{
    struct RS_ServerTick tick;
    uint64_t const big = ((uint64_t)1 << 32) + 7;

    printf("TEST: the opening cycle is stored truncated and the backstop subtracts it as stored\n");

    RS_ServerTick_Reset(&tick);
    RS_ServerTick_NotePacket(&tick, false, big);
    /* Stored as (int)big == 7, so the elapsed count is big - 7 = 2^32. */
    CHECK(RS_ServerTick_Overdue(&tick, big, MAX_CYCLES), "a truncated opening cycle was read untruncated");
}

int
main(void)
{
    test_a_reset_tick_is_closed_and_unfenced();
    test_the_first_packet_opens_and_the_fence_closes();
    test_the_varp_only_mark_is_per_tick();
    test_a_tick_without_a_fence_is_counted_only();
    test_close_is_the_recovery();
    test_suspend_and_resume_rearm_from_the_resume();
    test_the_backstop_reads_a_truncated_cycle();

    if( g_failures )
    {
        printf("rs_server_tick_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_server_tick_test: OK\n");
    return 0;
}
