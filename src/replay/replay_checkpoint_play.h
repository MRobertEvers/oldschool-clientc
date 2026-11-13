#ifndef SRC_REPLAY_REPLAY_CHECKPOINT_PLAY_H
#define SRC_REPLAY_REPLAY_CHECKPOINT_PLAY_H

/*
 * The client's half of replay session checkpoints, run every replay frame
 * (main.c main_rsprox_replay_frame, after the executed tick count is handed
 * to the transport):
 *
 *   - a seek's RESTORE step is ready (NetTransport_ReplayRestoreWanted) and
 *     the session can be put back (ReplayCheckpoint_CanTake): restore the
 *     checkpoint and tell the transport, which moves its cursor back;
 *   - otherwise, at a clean tick boundary the scrubber's store wants a
 *     checkpoint at (NetTransport_ReplayCheckpointWanted), playing or
 *     chasing: take one and hand it over.
 *
 * Nothing else: what to keep and when to restore is the scrubber's.
 */

struct App;
struct NetTransport;

void
ReplayCheckpointPlay_Frame(
    struct App* app,
    struct NetTransport* sock);

#endif
