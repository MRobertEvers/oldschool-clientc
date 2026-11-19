/*
 * When the client owes the server a keepalive.
 *
 * The idle rule with no socket and no App: nothing sent yet reads as a send
 * at 0; a keepalive is due once the last send is the idle bound or more
 * behind the clock, and not a millisecond sooner; every send restarts the
 * wait; and a reset is the only thing that forgets the last send.
 *
 * Run: make -C src test-net-keepalive
 */

#include "net/net_keepalive.h"

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

static struct NetKeepalive
fresh(void)
{
    struct NetKeepalive keepalive;

    memset(&keepalive, 0x5a, sizeof(keepalive));
    NetKeepalive_Reset(&keepalive);
    return keepalive;
}

static void
test_nothing_sent_counts_from_zero(void)
{
    struct NetKeepalive keepalive = fresh();

    printf("TEST: with nothing sent, the wait is measured from 0\n");

    TEST_ASSERT(keepalive.last_send_ms == 0, "a reset forgets the last send");
    TEST_ASSERT(!NetKeepalive_Due(&keepalive, 999, 1000), "not due a millisecond early");
    TEST_ASSERT(NetKeepalive_Due(&keepalive, 1000, 1000), "due at the bound exactly");
}

static void
test_every_send_restarts_the_wait(void)
{
    struct NetKeepalive keepalive = fresh();

    printf("TEST: every packet that goes out restarts the wait\n");

    NetKeepalive_NoteSend(&keepalive, 5000);
    TEST_ASSERT(!NetKeepalive_Due(&keepalive, 5000, 1000), "not due on the frame it sent");
    TEST_ASSERT(!NetKeepalive_Due(&keepalive, 5999, 1000), "not due inside the bound");
    TEST_ASSERT(NetKeepalive_Due(&keepalive, 6000, 1000), "due a full bound after the send");

    NetKeepalive_NoteSend(&keepalive, 5900);
    TEST_ASSERT(!NetKeepalive_Due(&keepalive, 6000, 1000), "a later send moved the wait");
    TEST_ASSERT(NetKeepalive_Due(&keepalive, 6900, 1000), "and it falls due from there");
}

static void
test_only_a_reset_forgets(void)
{
    struct NetKeepalive keepalive = fresh();

    printf("TEST: the last send survives until a reset\n");

    NetKeepalive_NoteSend(&keepalive, 40000);
    TEST_ASSERT(!NetKeepalive_Due(&keepalive, 40500, 1000), "the stamp holds");
    NetKeepalive_Reset(&keepalive);
    TEST_ASSERT(NetKeepalive_Due(&keepalive, 40500, 1000), "a reset measures from 0 again");
}

int
main(void)
{
    test_nothing_sent_counts_from_zero();
    test_every_send_restarts_the_wait();
    test_only_a_reset_forgets();

    if( g_failures )
    {
        printf("net_keepalive: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("net_keepalive: all checks passed\n");
    return 0;
}
