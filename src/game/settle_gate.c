#include "game/settle_gate.h"

#include <assert.h>
#include <stddef.h>

static char const* const k_reason_names[SETTLE_GATE_REASON_COUNT] = {
    [SETTLE_GATE_REASON_FRAME_RUNNER_HAD_WORK] = "settled:frame_runner_had_work",
    [SETTLE_GATE_REASON_FRAME_SETTLE_PENDING] = "settled:frame_settle_pending",
    [SETTLE_GATE_REASON_PACKET_RUNNER_HAD_WORK] = "settled:packet_runner_had_work",
    [SETTLE_GATE_REASON_SERVER_TICK_OPEN] = "settled:server_tick_open",
    [SETTLE_GATE_REASON_PENDING_CLIENTSCRIPTS] = "settled:pending_clientscripts",
};

bool
SettleGate_FrameSettled(struct SettleGateInput const* in)
{
    assert(in);
    return !in->frame_runner_had_work && !in->frame_settle_pending && !in->packet_runner_had_work &&
           !in->server_tick_open && in->clientscripts_held == 0 && in->triggeroplocal_count == 0 &&
           !in->close_modal_requested && in->resume_pausebutton_component_id == -1 &&
           in->social_send_count == 0;
}

enum SettleGateReason
SettleGate_FrameUnsettledReason(struct SettleGateInput const* in)
{
    assert(in);
    assert(!SettleGate_FrameSettled(in));
    if( in->frame_runner_had_work )
        return SETTLE_GATE_REASON_FRAME_RUNNER_HAD_WORK;
    if( in->frame_settle_pending )
        return SETTLE_GATE_REASON_FRAME_SETTLE_PENDING;
    if( in->packet_runner_had_work )
        return SETTLE_GATE_REASON_PACKET_RUNNER_HAD_WORK;
    if( in->server_tick_open )
        return SETTLE_GATE_REASON_SERVER_TICK_OPEN;
    return SETTLE_GATE_REASON_PENDING_CLIENTSCRIPTS;
}

char const*
SettleGate_ReasonName(enum SettleGateReason reason)
{
    assert(reason >= 0);
    assert(reason < SETTLE_GATE_REASON_COUNT);
    return k_reason_names[reason];
}

bool
SettleGate_PacketTransactionOpen(struct SettleGateInput const* in)
{
    assert(in);
    return in->packet_runner_had_work || in->server_tick_open || in->clientscripts_held > 0;
}

bool
SettleGate_PacketPumpWanted(struct SettleGateInput const* in)
{
    assert(in);
    return SettleGate_PacketTransactionOpen(in) || in->packets_queued || in->world_job_pending;
}

bool
SettleGate_WorldSettled(struct SettleGateInput const* in)
{
    assert(in);
    return !in->world_load_inflight && !in->world_job_pending;
}

bool
SettleGate_ReasonPacketSide(enum SettleGateReason reason)
{
    assert(reason >= 0);
    assert(reason < SETTLE_GATE_REASON_COUNT);
    /* All five: two script latches, the packet queue's, the tick, the
     * scripts' requests. Stated as a switch-free table of one answer so a
     * reason added later has to be classed here. */
    return reason == SETTLE_GATE_REASON_FRAME_RUNNER_HAD_WORK || reason == SETTLE_GATE_REASON_FRAME_SETTLE_PENDING ||
           reason == SETTLE_GATE_REASON_PACKET_RUNNER_HAD_WORK || reason == SETTLE_GATE_REASON_SERVER_TICK_OPEN ||
           reason == SETTLE_GATE_REASON_PENDING_CLIENTSCRIPTS;
}

static char const* const k_withhold_names[SETTLE_GATE_WITHHOLD_COUNT] = {
    [SETTLE_GATE_WITHHOLD_BOOT] = "withheld:boot",
    [SETTLE_GATE_WITHHOLD_LOGOUT_HOLD] = "withheld:logout_hold",
    [SETTLE_GATE_WITHHOLD_SCRIPTS_LATCHED] = "withheld:scripts_latched",
    [SETTLE_GATE_WITHHOLD_SCRIPTS_PENDING] = "withheld:scripts_pending",
    [SETTLE_GATE_WITHHOLD_GATE] = "withheld:gate",
    [SETTLE_GATE_WITHHOLD_REPLAY_SEEK] = "withheld:replay_seek",
};

bool
SettleGate_WithholdPacketSide(enum SettleGateWithhold why)
{
    assert(why >= 0);
    assert(why < SETTLE_GATE_WITHHOLD_COUNT);
    return why != SETTLE_GATE_WITHHOLD_REPLAY_SEEK;
}

char const*
SettleGate_WithholdName(enum SettleGateWithhold why)
{
    assert(why >= 0);
    assert(why < SETTLE_GATE_WITHHOLD_COUNT);
    return k_withhold_names[why];
}
