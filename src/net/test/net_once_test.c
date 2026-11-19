/*
 * The network events this client acts on once per run.
 *
 * With no socket and no App: the first packet is marked once and never
 * again; the boot cheat fires the first time it is asked, then only on the
 * repeat cadence when there is one, and never when there is not.
 *
 * Run: make -C src test-net-once
 */

#include "net/net_once.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static struct NetOnce
fresh(void)
{
    struct NetOnce once;

    memset(&once, 0x5a, sizeof(once));
    NetOnce_Reset(&once);
    return once;
}

static void
test_the_first_packet_is_marked_once(void)
{
    struct NetOnce once = fresh();

    printf("TEST: the first packet is marked once\n");

    TEST_ASSERT(NetOnce_TakeFirstPacket(&once), "the first packet is the first");
    TEST_ASSERT(!NetOnce_TakeFirstPacket(&once), "the second is not");
    TEST_ASSERT(!NetOnce_TakeFirstPacket(&once), "nor any after it");
    TEST_ASSERT(once.cheat_sent == 0, "marking a packet sends no cheat");
}

static void
test_a_one_shot_cheat_fires_once(void)
{
    struct NetOnce once = fresh();

    printf("TEST: with no cadence the cheat fires once\n");

    TEST_ASSERT(NetOnce_CheatFires(&once, 7, 0), "fires the first time, on any cycle");
    TEST_ASSERT(once.cheat_sent == 1, "and counts as sent");
    TEST_ASSERT(!NetOnce_CheatFires(&once, 8, 0), "not on the next cycle");
    TEST_ASSERT(!NetOnce_CheatFires(&once, 0, 0), "not on cycle 0 either");
    TEST_ASSERT(!NetOnce_CheatFires(&once, 100, -5), "a negative cadence is one-shot too");
    TEST_ASSERT(once.first_packet_marked == 0, "sending a cheat marks no packet");
}

static void
test_a_cadence_repeats_on_its_multiples(void)
{
    struct NetOnce once = fresh();

    printf("TEST: with a cadence the cheat repeats on its multiples\n");

    TEST_ASSERT(NetOnce_CheatFires(&once, 13, 50), "the first shot ignores the cadence");
    TEST_ASSERT(!NetOnce_CheatFires(&once, 49, 50), "not between multiples");
    TEST_ASSERT(NetOnce_CheatFires(&once, 50, 50), "on a multiple");
    TEST_ASSERT(NetOnce_CheatFires(&once, 50, 50), "on the same multiple asked again");
    TEST_ASSERT(!NetOnce_CheatFires(&once, 51, 50), "not just after it");
    TEST_ASSERT(NetOnce_CheatFires(&once, 100, 50), "on the next multiple");
}

int
main(void)
{
    test_the_first_packet_is_marked_once();
    test_a_one_shot_cheat_fires_once();
    test_a_cadence_repeats_on_its_multiples();

    if( g_failures )
    {
        printf("net_once: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("net_once: all checks passed\n");
    return 0;
}
