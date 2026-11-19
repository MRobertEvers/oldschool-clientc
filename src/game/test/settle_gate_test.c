/*
 * The settle gate: when the frame may be published and what the latch says
 * holds it, when the packet transaction is open, when the frame resumes the
 * packet pipeline, and when the world is settled.
 *
 * A gate that says settled too early publishes half a transaction; one that
 * never says it freezes the picture on the last committed frame. A reason
 * name that drifts breaks every frame_latch log reader.
 *
 *   make -C src test-settle-gate
 */
#include "game/settle_gate.h"

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

static struct SettleGateInput
quiet(void)
{
    struct SettleGateInput in;

    memset(&in, 0, sizeof(in));
    in.resume_pausebutton_component_id = -1;
    return in;
}

static void
test_a_quiet_client_is_settled(void)
{
    struct SettleGateInput in = quiet();

    printf("TEST: a quiet client is settled in every sense\n");
    CHECK(SettleGate_FrameSettled(&in), "frame settled");
    CHECK(!SettleGate_PacketTransactionOpen(&in), "no transaction open");
    CHECK(!SettleGate_PacketPumpWanted(&in), "no pump wanted");
    CHECK(SettleGate_WorldSettled(&in), "world settled");
}

static void
test_each_frame_condition_holds_the_frame(void)
{
    struct SettleGateInput in;

    printf("TEST: each frame condition alone holds the frame\n");
    in = quiet(), in.frame_runner_had_work = true;
    CHECK(!SettleGate_FrameSettled(&in), "frame runner latch");
    in = quiet(), in.frame_settle_pending = true;
    CHECK(!SettleGate_FrameSettled(&in), "settle pending");
    in = quiet(), in.packet_runner_had_work = true;
    CHECK(!SettleGate_FrameSettled(&in), "packet runner latch");
    in = quiet(), in.server_tick_open = true;
    CHECK(!SettleGate_FrameSettled(&in), "server tick open");
    in = quiet(), in.clientscripts_held = 1;
    CHECK(!SettleGate_FrameSettled(&in), "held clientscript");
    in = quiet(), in.triggeroplocal_count = 1;
    CHECK(!SettleGate_FrameSettled(&in), "triggeroplocal");
    in = quiet(), in.close_modal_requested = true;
    CHECK(!SettleGate_FrameSettled(&in), "close modal");
    in = quiet(), in.resume_pausebutton_component_id = 0;
    CHECK(!SettleGate_FrameSettled(&in), "pause-button resume of component 0");
    in = quiet(), in.social_send_count = 1;
    CHECK(!SettleGate_FrameSettled(&in), "social send");
}

static void
test_world_and_queue_do_not_hold_the_frame(void)
{
    struct SettleGateInput in = quiet();

    printf("TEST: a queued packet or a world load does not hold the frame\n");
    in.packets_queued = true;
    in.world_job_pending = true;
    in.world_load_inflight = true;
    CHECK(SettleGate_FrameSettled(&in), "frame settled with world and queue busy");
}

static void
test_reason_order_and_names(void)
{
    struct SettleGateInput in = quiet();

    printf("TEST: the reason is the first holding condition, with today's names\n");
    in.frame_runner_had_work = true;
    in.frame_settle_pending = true;
    in.packet_runner_had_work = true;
    in.server_tick_open = true;
    in.clientscripts_held = 1;
    CHECK(SettleGate_FrameUnsettledReason(&in) == SETTLE_GATE_REASON_FRAME_RUNNER_HAD_WORK, "frame first");
    in.frame_runner_had_work = false;
    CHECK(SettleGate_FrameUnsettledReason(&in) == SETTLE_GATE_REASON_FRAME_SETTLE_PENDING, "settle second");
    in.frame_settle_pending = false;
    CHECK(SettleGate_FrameUnsettledReason(&in) == SETTLE_GATE_REASON_PACKET_RUNNER_HAD_WORK, "packet third");
    in.packet_runner_had_work = false;
    CHECK(SettleGate_FrameUnsettledReason(&in) == SETTLE_GATE_REASON_SERVER_TICK_OPEN, "tick fourth");
    in.server_tick_open = false;
    CHECK(SettleGate_FrameUnsettledReason(&in) == SETTLE_GATE_REASON_PENDING_CLIENTSCRIPTS, "scripts last");

    in = quiet();
    in.social_send_count = 2;
    CHECK(SettleGate_FrameUnsettledReason(&in) == SETTLE_GATE_REASON_PENDING_CLIENTSCRIPTS,
          "an undrained host request reads as pending clientscripts");

    CHECK(strcmp(SettleGate_ReasonName(SETTLE_GATE_REASON_FRAME_RUNNER_HAD_WORK),
                 "settled:frame_runner_had_work") == 0, "name 0");
    CHECK(strcmp(SettleGate_ReasonName(SETTLE_GATE_REASON_FRAME_SETTLE_PENDING),
                 "settled:frame_settle_pending") == 0, "name 1");
    CHECK(strcmp(SettleGate_ReasonName(SETTLE_GATE_REASON_PACKET_RUNNER_HAD_WORK),
                 "settled:packet_runner_had_work") == 0, "name 2");
    CHECK(strcmp(SettleGate_ReasonName(SETTLE_GATE_REASON_SERVER_TICK_OPEN),
                 "settled:server_tick_open") == 0, "name 3");
    CHECK(strcmp(SettleGate_ReasonName(SETTLE_GATE_REASON_PENDING_CLIENTSCRIPTS),
                 "settled:pending_clientscripts") == 0, "name 4");
}

static void
test_packet_transaction_and_pump(void)
{
    struct SettleGateInput in;

    printf("TEST: the packet transaction and the frame's pump\n");
    in = quiet(), in.packet_runner_had_work = true;
    CHECK(SettleGate_PacketTransactionOpen(&in) && SettleGate_PacketPumpWanted(&in), "packet latch");
    in = quiet(), in.server_tick_open = true;
    CHECK(SettleGate_PacketTransactionOpen(&in) && SettleGate_PacketPumpWanted(&in), "open tick");
    in = quiet(), in.clientscripts_held = 3;
    CHECK(SettleGate_PacketTransactionOpen(&in) && SettleGate_PacketPumpWanted(&in), "held script");

    in = quiet(), in.frame_runner_had_work = true, in.frame_settle_pending = true;
    CHECK(!SettleGate_PacketTransactionOpen(&in), "the frame runner is not the packet transaction");
    in = quiet(), in.triggeroplocal_count = 1, in.close_modal_requested = true, in.social_send_count = 1;
    in.resume_pausebutton_component_id = 7;
    CHECK(!SettleGate_PacketTransactionOpen(&in), "host requests are not the packet transaction");

    in = quiet(), in.packets_queued = true;
    CHECK(!SettleGate_PacketTransactionOpen(&in), "a queued packet is not an open transaction");
    CHECK(SettleGate_PacketPumpWanted(&in), "but it wants a pump");
    in = quiet(), in.world_job_pending = true;
    CHECK(SettleGate_PacketPumpWanted(&in), "a pending world job wants a pump");
    in = quiet(), in.world_load_inflight = true;
    CHECK(!SettleGate_PacketPumpWanted(&in), "a blocking load alone does not");
}

static void
test_world_settled(void)
{
    struct SettleGateInput in;

    printf("TEST: the world is settled only with no load of either kind\n");
    in = quiet(), in.world_load_inflight = true;
    CHECK(!SettleGate_WorldSettled(&in), "blocking load");
    in = quiet(), in.world_job_pending = true;
    CHECK(!SettleGate_WorldSettled(&in), "background job");
    in = quiet(), in.packet_runner_had_work = true, in.frame_runner_had_work = true;
    CHECK(SettleGate_WorldSettled(&in), "runner latches are not the world");
}

int
main(void)
{
    test_a_quiet_client_is_settled();
    test_each_frame_condition_holds_the_frame();
    test_world_and_queue_do_not_hold_the_frame();
    test_reason_order_and_names();
    test_packet_transaction_and_pump();
    test_world_settled();
    if( g_failures )
    {
        printf("settle_gate_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("settle_gate_test: all passed\n");
    return 0;
}
