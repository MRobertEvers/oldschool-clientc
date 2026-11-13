#ifndef SRC_REPLAY_REPLAY_CHECKPOINT_CHECK_H
#define SRC_REPLAY_REPLAY_CHECKPOINT_CHECK_H

/*
 * TORIRS_REPLAY_CHECKPOINT_CHECK=K,N: the proof that a session checkpoint
 * (replay/replay_checkpoint.h) leaves nothing out.
 *
 * Run a replay with `--replay-at K`. Once it holds at tick K and the session
 * can be checkpointed:
 *
 *   1. take A; restore A; take A2; A == A2   ("roundtrip": restore puts back
 *      everything take read);
 *   2. seek to K+N through the scrubber's own RESTORE
 *      (NetTransport_ReplaySeekFromCheckpoint): A is put back, the cursor and
 *      the replay clock go to K with the session kept, the world is keyframed
 *      at K by the ledger, and the N ticks play in full (a pre-roll of N);
 *      take B;
 *   3. the same seek again, now from K+N; take B2; B == B2.
 *
 * Steps 2 and 3 differ in one thing: what the session held OUTSIDE A is its
 * K value in step 2 and its K+N value in step 3. So anything the session
 * depends on that A did not hold makes B2 come out different. (The world is
 * keyframed the same way in both, so it does not; choose a window with no
 * REBUILD in it, or the step-3 keyframe builds a scene step 2 kept.) Only the session is compared (the world
 * is the ledger's). When the replay digests (replay_digest.h) are built in,
 * their session domains are printed for B and B2 too.
 *
 * One line each: `replay: checkpoint roundtrip tick=K PASS|FAIL <why>`,
 * `replay: checkpoint check tick=K n=N PASS|FAIL <why>`, and the cost lines
 * (ReplayCheckpoint_PrintCost). Then the run ends.
 *
 */

struct App;
struct NetTransport;

/** Every replay frame, after the seek bookkeeping (main.c). 1 once the check
 *  has printed its verdict and the run should end; 0 otherwise, including
 *  when TORIRS_REPLAY_CHECKPOINT_CHECK is unset. */
int
ReplayCheckpointCheck_Frame(
    struct App* app,
    struct NetTransport* sock,
    int seeking);

#endif
