#ifndef SRC_GAME_SETTLE_GATE_H
#define SRC_GAME_SETTLE_GATE_H

/*
 * Whether the client is settled, and if it is not, why.
 *
 * "Settled" is asked in four different senses, and each is its own function
 * here because their conditions differ:
 *
 *   - The FRAME is settled (SettleGate_FrameSettled): the live UI tree may be
 *     published. Neither runner's last pump left work parked, no CS2
 *     transaction is pending on the frame runner, no server tick is half
 *     applied, no RUNCLIENTSCRIPT is held for its fence, and the CS2 host has
 *     no request to the App left undrained (a TRIGGEROPLOCAL, a close-modal,
 *     a pause-button resume, a social send). When it is not, the reason
 *     (SettleGate_FrameUnsettledReason) names the first of the runner latches,
 *     the pending settle and the open tick that holds; anything after those --
 *     a held script or an undrained host request -- is reported as
 *     pending clientscripts.
 *   - The PACKET TRANSACTION is open (SettleGate_PacketTransactionOpen): the
 *     packet runner left work parked, a server tick is open, or a script is
 *     held. While it is, no widget hook may observe the tree.
 *   - The packet pipeline wants a PUMP (SettleGate_PacketPumpWanted): its
 *     transaction is open, or a packet is queued, or a background world job
 *     is pending.
 *   - The WORLD is settled (SettleGate_WorldSettled): no world load is in
 *     flight and no background world job is pending.
 *
 * The caller fills one SettleGateInput with what it reads off its runners,
 * its server tick, its clientscript queue, its CS2 host and its world loader;
 * each function reads only the fields its own sense names.
 *
 * This does not know what a runner, a server tick or a world job is, does not
 * set any latch, and does not print. The caller fills the input, acts on the
 * answer (holding the frame, skipping the hooks, pumping the packets) and logs
 * the reason's name if it logs one.
 */

#include <stdbool.h>

struct SettleGateInput
{
    /** The frame runner's latch: its last CS2 settle left work parked. */
    bool frame_runner_had_work;
    /** A CS2 transaction is pending on the frame runner. */
    bool frame_settle_pending;
    /** The packet runner's latch: the serial pipeline left work parked. */
    bool packet_runner_had_work;
    /** A fenced server tick has started applying and has not closed. */
    bool server_tick_open;
    /** RUNCLIENTSCRIPTs held for the tick fence. */
    int clientscripts_held;
    /** The CS2 host's undrained requests to the App. */
    int triggeroplocal_count;
    bool close_modal_requested;
    /** -1 when no pause-button resume is waiting. */
    int resume_pausebutton_component_id;
    int social_send_count;
    /** A received packet is queued and not yet popped. */
    bool packets_queued;
    /** A background world job is gathering, staging or committing. */
    bool world_job_pending;
    /** A blocking world load is in flight. */
    bool world_load_inflight;
};

/** Why the frame is not settled; the names are the frame latch's log text. */
enum SettleGateReason
{
    SETTLE_GATE_REASON_FRAME_RUNNER_HAD_WORK = 0,
    SETTLE_GATE_REASON_FRAME_SETTLE_PENDING,
    SETTLE_GATE_REASON_PACKET_RUNNER_HAD_WORK,
    SETTLE_GATE_REASON_SERVER_TICK_OPEN,
    /** A held script, or any undrained CS2 host request. */
    SETTLE_GATE_REASON_PENDING_CLIENTSCRIPTS,
    SETTLE_GATE_REASON_COUNT,
};

/** May the live UI tree be published? */
bool
SettleGate_FrameSettled(struct SettleGateInput const* in);

/**
 * The first reason the frame is not settled, in the order the latch reports
 * it. Asked only of an input SettleGate_FrameSettled refuses.
 */
enum SettleGateReason
SettleGate_FrameUnsettledReason(struct SettleGateInput const* in);

/** "settled:<reason>", the text the frame latch logs. */
char const*
SettleGate_ReasonName(enum SettleGateReason reason);

/** Is a packet/interface transaction still being applied? */
bool
SettleGate_PacketTransactionOpen(struct SettleGateInput const* in);

/** Should the frame resume the packet pipeline, outside a logic tick? */
bool
SettleGate_PacketPumpWanted(struct SettleGateInput const* in);

/** Is no world load in flight, blocking or background? */
bool
SettleGate_WorldSettled(struct SettleGateInput const* in);

#endif
