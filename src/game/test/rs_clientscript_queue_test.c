/*
 * Two small containers App used to hold inline, and the one rule in each that
 * is worth more than the container.
 *
 * The frame-time ring: a mean over the samples HELD, not over the ring. A ring
 * a third full averaged against seven zeroes reads as a client three times
 * faster than it is, which is the direction nobody investigates.
 *
 * The clientscript queue: a flush that is RE-ENTRANT. Dispatching a held
 * payload can push another -- CC_TRIGGEROP's queue drain reaches this path --
 * and the array-and-count this replaced walked an array it was also being
 * appended to. That loses entries and runs others twice, and a clientscript
 * run twice is a panel that draws its contents twice or a counter that
 * advances by two.
 *
 *   make -C src test-clientscript-queue
 */
#include "game/rs_clientscript_queue.h"
#include "perf/frame_time_ring.h"

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

/* ---------------------------------------------------------------- ring */

static void
test_the_ring_is_empty_before_it_is_used(void)
{
    struct FrameTimeRing ring;

    printf("TEST: an unused frame-time ring reads as nothing, not as zero\n");

    FrameTimeRing_Reset(&ring);
    CHECK(FrameTimeRing_NewestUs(&ring) == 0, "a fresh ring claimed a newest sample");
    CHECK(FrameTimeRing_MeanUs(&ring) == 0, "a fresh ring claimed a mean");
}

static void
test_the_mean_is_over_the_samples_held(void)
{
    struct FrameTimeRing ring;

    printf("TEST: the mean is over what is held, not over the ring\n");

    /*
     * Three samples in a ring of ten. Averaged over the ring the answer is
     * 3000 -- a client reading three times faster than it is, in the direction
     * nobody investigates. Averaged over what is held it is 10000.
     */
    FrameTimeRing_Reset(&ring);
    FrameTimeRing_Add(&ring, 9000);
    FrameTimeRing_Add(&ring, 10000);
    FrameTimeRing_Add(&ring, 11000);
    CHECK(FrameTimeRing_MeanUs(&ring) == 10000, "the mean is %u, not 10000",
        FrameTimeRing_MeanUs(&ring));
    CHECK(FrameTimeRing_NewestUs(&ring) == 11000, "the newest sample is not the last added");
}

static void
test_the_ring_forgets_the_oldest(void)
{
    struct FrameTimeRing ring;
    int i;

    printf("TEST: the ring holds the last N frames and no more\n");

    FrameTimeRing_Reset(&ring);
    /* Ten slow frames, then ten fast ones. A readout that is meant to move
     * while you watch it must not still be carrying the slow ones. */
    for( i = 0; i < FRAME_TIME_RING_SAMPLES; i++ )
        FrameTimeRing_Add(&ring, 100000);
    CHECK(FrameTimeRing_MeanUs(&ring) == 100000, "the slow run does not read as slow");
    for( i = 0; i < FRAME_TIME_RING_SAMPLES; i++ )
        FrameTimeRing_Add(&ring, 1000);
    CHECK(FrameTimeRing_MeanUs(&ring) == 1000, "a slow frame outlived the ring: mean %u",
        FrameTimeRing_MeanUs(&ring));

    /* And one slow frame among nine fast ones moves the mean by its SHARE and
     * not by more -- this is an average, not a high-water mark. A 101 ms frame
     * next to nine 1 ms ones reads as 11 ms, which is the arithmetic written
     * out rather than a number to trust. */
    FrameTimeRing_Add(&ring, 101000);
    CHECK(
        FrameTimeRing_MeanUs(&ring) ==
            (101000 + 1000 * (FRAME_TIME_RING_SAMPLES - 1)) / FRAME_TIME_RING_SAMPLES,
        "one slow frame in ten moved the mean to %u", FrameTimeRing_MeanUs(&ring));
    CHECK(FrameTimeRing_MeanUs(&ring) == 11000, "and that arithmetic is not 11000");
}

static void
test_the_newest_wraps_with_the_head(void)
{
    struct FrameTimeRing ring;
    int i;

    printf("TEST: the newest sample is found across the wrap\n");

    /*
     * `head` is where the NEXT sample lands, so the newest is the slot before
     * it -- and at a wrap that is the LAST slot, not slot -1. Read the wrong
     * way the readout shows the oldest frame in the ring exactly once every
     * ten frames, which reads as a periodic stutter that is not there.
     */
    FrameTimeRing_Reset(&ring);
    for( i = 0; i < FRAME_TIME_RING_SAMPLES; i++ )
        FrameTimeRing_Add(&ring, (uint64_t)(1000 + i));
    CHECK(FrameTimeRing_NewestUs(&ring) == 1000 + FRAME_TIME_RING_SAMPLES - 1,
        "the newest across the wrap is %u", FrameTimeRing_NewestUs(&ring));

    FrameTimeRing_Add(&ring, 7777);
    CHECK(FrameTimeRing_NewestUs(&ring) == 7777, "the first sample after the wrap was lost");
}

static void
test_an_enormous_frame_saturates(void)
{
    struct FrameTimeRing ring;

    printf("TEST: a frame longer than the counter saturates rather than wrapping\n");

    /*
     * A wrapped sample makes the single worst frame of a session read as the
     * best one. Saturating keeps it the worst, which is the only answer that
     * is useful.
     */
    FrameTimeRing_Reset(&ring);
    FrameTimeRing_Add(&ring, (uint64_t)UINT32_MAX + 1000);
    CHECK(FrameTimeRing_NewestUs(&ring) == UINT32_MAX, "an enormous frame read as %u",
        FrameTimeRing_NewestUs(&ring));

    FrameTimeRing_Reset(&ring);
    FrameTimeRing_Add(&ring, UINT32_MAX);
    CHECK(FrameTimeRing_NewestUs(&ring) == UINT32_MAX, "the largest exact value was clamped");
}

/* --------------------------------------------------------------- queue */

static struct PktRunClientScript
payload(int script_id)
{
    struct PktRunClientScript request;

    memset(&request, 0, sizeof(request));
    request.script_id = script_id;
    request.argc = 1;
    request.intv[0] = script_id * 10;
    return request;
}

static void
test_an_empty_queue(void)
{
    struct RS_ClientScriptQueue queue;
    struct PktRunClientScript out;

    printf("TEST: an empty clientscript queue\n");

    RS_ClientScriptQueue_Reset(&queue);
    CHECK(RS_ClientScriptQueue_Count(&queue) == 0, "a fresh queue holds something");
    CHECK(!RS_ClientScriptQueue_Pop(&queue, &out), "an empty queue handed over a payload");
    /* Nothing is waiting, so nothing is overdue -- however long the client has
     * been running. */
    CHECK(!RS_ClientScriptQueue_FenceOverdue(&queue, 100000, 30),
        "an empty queue reported an overdue fence");
}

static void
test_payloads_come_out_in_the_order_they_went_in(void)
{
    struct RS_ClientScriptQueue queue;
    struct PktRunClientScript out;
    int i;

    printf("TEST: held payloads keep their order\n");

    /*
     * Order is the whole point of holding them. A tick's scripts are written
     * to run in the order the server pushed them -- a panel's open before its
     * fill -- and a queue that reverses or shuffles them paints the fill into
     * a panel that is not there yet.
     */
    RS_ClientScriptQueue_Reset(&queue);
    for( i = 1; i <= 5; i++ )
        CHECK(RS_ClientScriptQueue_Hold(&queue, &(struct PktRunClientScript){ .script_id = i }, 7),
            "payload %d was refused", i);
    CHECK(RS_ClientScriptQueue_Count(&queue) == 5, "the queue holds %d, not 5",
        RS_ClientScriptQueue_Count(&queue));

    for( i = 1; i <= 5; i++ )
    {
        CHECK(RS_ClientScriptQueue_Pop(&queue, &out), "payload %d did not come back", i);
        CHECK(out.script_id == i, "payload %d came out as %d", i, out.script_id);
    }
    CHECK(RS_ClientScriptQueue_Count(&queue) == 0, "the queue is not empty after draining it");
}

static void
test_a_full_queue_refuses_rather_than_dropping(void)
{
    struct RS_ClientScriptQueue queue;
    int i;

    printf("TEST: a full queue refuses, so the caller can run it now\n");

    /*
     * Past the cap the caller runs the script immediately. Degrading to the
     * old ordering is a cosmetic bug; losing a script is not, and a queue that
     * silently dropped one would be indistinguishable from a server that never
     * sent it.
     */
    RS_ClientScriptQueue_Reset(&queue);
    for( i = 0; i < RS_CLIENTSCRIPT_QUEUE_MAX; i++ )
        CHECK(RS_ClientScriptQueue_Hold(&queue, &(struct PktRunClientScript){ .script_id = i }, 1),
            "payload %d was refused below the cap", i);
    CHECK(!RS_ClientScriptQueue_Hold(&queue, &(struct PktRunClientScript){ .script_id = 999 }, 1),
        "the queue accepted one past its cap");
    CHECK(RS_ClientScriptQueue_Count(&queue) == RS_CLIENTSCRIPT_QUEUE_MAX,
        "the refused payload changed the count");

    /* And the refusal did not overwrite anything: the oldest is still the
     * oldest. */
    {
        struct PktRunClientScript out;
        CHECK(RS_ClientScriptQueue_Pop(&queue, &out), "the full queue would not hand one back");
        CHECK(out.script_id == 0, "the refused payload overwrote the oldest");
    }
}

static void
test_a_flush_that_pushes(void)
{
    struct RS_ClientScriptQueue queue;
    struct PktRunClientScript out;
    int snapshot;
    int dispatched[8];
    int dispatched_count = 0;
    int i;

    printf("TEST: a flush that pushes does not lose or repeat a payload\n");

    /*
     * This is the case the ring exists for. Dispatching a held payload can
     * push another, so the flush loop is re-entrant. An array with a count --
     * cleared first, then walked -- has the pushed payload land in a slot the
     * loop has not read yet: the original is lost, the new one runs in its
     * place, and the new one is ALSO still queued, so it runs again next time.
     *
     * Three held, and the first dispatch pushes two.
     */
    RS_ClientScriptQueue_Reset(&queue);
    for( i = 1; i <= 3; i++ )
        RS_ClientScriptQueue_Hold(&queue, &(struct PktRunClientScript){ .script_id = i }, 4);

    snapshot = RS_ClientScriptQueue_Count(&queue);
    for( i = 0; i < snapshot; i++ )
    {
        CHECK(RS_ClientScriptQueue_Pop(&queue, &out), "the flush ran short at %d", i);
        dispatched[dispatched_count++] = out.script_id;
        if( out.script_id == 1 )
        {
            RS_ClientScriptQueue_Hold(&queue, &(struct PktRunClientScript){ .script_id = 91 }, 5);
            RS_ClientScriptQueue_Hold(&queue, &(struct PktRunClientScript){ .script_id = 92 }, 5);
        }
    }

    CHECK(dispatched_count == 3, "the flush ran %d payloads, not 3", dispatched_count);
    CHECK(dispatched[0] == 1 && dispatched[1] == 2 && dispatched[2] == 3,
        "the flush ran %d, %d, %d", dispatched[0], dispatched[1], dispatched[2]);

    /* The two pushed inside the flush are waiting for the NEXT one, in order,
     * and neither has been run. */
    CHECK(RS_ClientScriptQueue_Count(&queue) == 2, "the pushed payloads are not both waiting");
    CHECK(RS_ClientScriptQueue_Pop(&queue, &out) && out.script_id == 91, "pushed payload 91");
    CHECK(RS_ClientScriptQueue_Pop(&queue, &out) && out.script_id == 92, "pushed payload 92");
}

static void
test_the_ring_wraps_without_losing_order(void)
{
    struct RS_ClientScriptQueue queue;
    struct PktRunClientScript out;
    int i;

    printf("TEST: the queue keeps its order across the ring's wrap\n");

    /*
     * Ticks come and go, so head walks all the way round. Every flush must
     * behave the same as the first one -- and an off-by-one in the wrap is a
     * queue that works perfectly for the first sixty-four scripts of a
     * session.
     */
    RS_ClientScriptQueue_Reset(&queue);
    for( i = 0; i < RS_CLIENTSCRIPT_QUEUE_MAX * 3 + 5; i++ )
    {
        RS_ClientScriptQueue_Hold(&queue, &(struct PktRunClientScript){ .script_id = i }, 1);
        CHECK(RS_ClientScriptQueue_Pop(&queue, &out), "payload %d did not come back", i);
        CHECK(out.script_id == i, "payload %d came out as %d after the wrap", i, out.script_id);
    }

    /* And a partial drain leaves the rest in order across the wrap. */
    for( i = 0; i < 5; i++ )
        RS_ClientScriptQueue_Hold(&queue, &(struct PktRunClientScript){ .script_id = 300 + i }, 1);
    for( i = 0; i < 5; i++ )
    {
        CHECK(RS_ClientScriptQueue_Pop(&queue, &out), "the partial drain ran short");
        CHECK(out.script_id == 300 + i, "the partial drain reordered: %d", out.script_id);
    }
}

static void
test_the_backstop_measures_the_oldest(void)
{
    struct RS_ClientScriptQueue queue;
    struct PktRunClientScript out;

    printf("TEST: the fence backstop is measured from the oldest held payload\n");

    RS_ClientScriptQueue_Reset(&queue);
    RS_ClientScriptQueue_Hold(&queue, &(struct PktRunClientScript){ .script_id = 1 }, 100);

    /* One cycle inside the bound is still waiting. */
    CHECK(!RS_ClientScriptQueue_FenceOverdue(&queue, 129, 30), "overdue one cycle early");
    CHECK(RS_ClientScriptQueue_FenceOverdue(&queue, 130, 30), "not overdue on the bound");

    /*
     * A LATER arrival does not restart the clock. The backstop asks how long
     * the queue has been waiting, and a busy tick pushing a script every few
     * cycles would otherwise hold the first one indefinitely -- which is
     * exactly the stranding the backstop exists to prevent.
     */
    RS_ClientScriptQueue_Reset(&queue);
    RS_ClientScriptQueue_Hold(&queue, &(struct PktRunClientScript){ .script_id = 1 }, 100);
    RS_ClientScriptQueue_Hold(&queue, &(struct PktRunClientScript){ .script_id = 2 }, 125);
    CHECK(RS_ClientScriptQueue_FenceOverdue(&queue, 130, 30),
        "a later arrival restarted the backstop clock");

    /* Draining and refilling DOES start a new one: that is a new wait. */
    RS_ClientScriptQueue_Pop(&queue, &out);
    RS_ClientScriptQueue_Pop(&queue, &out);
    RS_ClientScriptQueue_Hold(&queue, &(struct PktRunClientScript){ .script_id = 3 }, 200);
    CHECK(!RS_ClientScriptQueue_FenceOverdue(&queue, 210, 30),
        "a fresh wait inherited the previous one's clock");
}

int
main(void)
{
    test_the_ring_is_empty_before_it_is_used();
    test_the_mean_is_over_the_samples_held();
    test_the_ring_forgets_the_oldest();
    test_the_newest_wraps_with_the_head();
    test_an_enormous_frame_saturates();

    test_an_empty_queue();
    test_payloads_come_out_in_the_order_they_went_in();
    test_a_full_queue_refuses_rather_than_dropping();
    test_a_flush_that_pushes();
    test_the_ring_wraps_without_losing_order();
    test_the_backstop_measures_the_oldest();

    if( g_failures )
    {
        printf("rs_clientscript_queue_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_clientscript_queue_test: OK\n");
    return 0;
}
