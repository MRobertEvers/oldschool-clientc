#ifndef SRC_REPLAY_REPLAY_CHECKPOINT_STORE_H
#define SRC_REPLAY_REPLAY_CHECKPOINT_STORE_H

/*
 * The session checkpoints a replay has taken (replay/replay_checkpoint.h),
 * by tick, under a byte budget. Owned by the scrubber (one per recording);
 * filled by the client as it plays or chases (replay/replay_checkpoint_play.c)
 * and read by the seek plan, which restores the nearest one at or before its
 * keyframe tick instead of restarting from the login.
 *
 * POLICY. A checkpoint is taken at a tick boundary at least
 * REPLAY_CKPT_STORE_SPACING ticks from every one the store holds, so play
 * and a chase leave one every 64 ticks (38 s of recording) behind them. A
 * checkpoint on recording 96 is about 12.5 MB, so the native budget (256 MB)
 * holds about twenty and the web's (64 MB) about five. Past the budget the
 * store drops the checkpoint whose removal costs least, weighing the gap it
 * opens against its distance from the playhead:
 *
 *     cost(i) = (tick[i+1] - tick[i-1]) / max(SPACING, |tick[i] - playhead|)
 *
 * so the store stays dense next to the playhead (where one-tick back-steps
 * and short scrubs land) and thins out with distance, keeping the first and
 * the last it has. TORIRS_REPLAY_CHECKPOINT_MB overrides the budget (0 turns
 * checkpoints off).
 */

#include <stddef.h>
#include <stdint.h>

struct ReplayCheckpoint;
struct ReplayCheckpointStore;

#define REPLAY_CKPT_STORE_SPACING 64
#if defined(__EMSCRIPTEN__)
/* 64 held five checkpoints (one is 12-16 MB, most of it the interface tree),
 * so a browser seek back from tick 6000 to 4500 on RSProx 96 found nothing
 * nearer than tick 3 and replayed 4,481 ticks (5.4 s, 2026-11-12). The heap
 * grows to 4 GB (platform.mk); 256 holds about nineteen. */
#define REPLAY_CKPT_STORE_DEFAULT_MB 256
#else
#define REPLAY_CKPT_STORE_DEFAULT_MB 256
#endif

struct ReplayCheckpointStore*
ReplayCheckpointStore_New(size_t budget_bytes);

/** Accepts NULL. */
void
ReplayCheckpointStore_Free(struct ReplayCheckpointStore* store);

/** Drop every checkpoint. */
void
ReplayCheckpointStore_Clear(struct ReplayCheckpointStore* store);

/** 0: the store takes nothing. */
size_t
ReplayCheckpointStore_Budget(struct ReplayCheckpointStore const* store);

/** Whether a checkpoint at `tick` would be kept: the budget is not 0 and no
 *  checkpoint is within REPLAY_CKPT_STORE_SPACING ticks of it. */
int
ReplayCheckpointStore_Wants(
    struct ReplayCheckpointStore const* store,
    int tick);

/**
 * Hand `cp`, taken at `tick` with the replay clock at `clock_ms`, to the
 * store, which owns it from here (and frees it at once when it is not wanted).
 * `playhead` is the tick the replay is at, for the eviction weight.
 */
void
ReplayCheckpointStore_Add(
    struct ReplayCheckpointStore* store,
    int tick,
    uint64_t clock_ms,
    struct ReplayCheckpoint* cp,
    int playhead);

/** The latest checkpoint at or before `tick`, or NULL. `*out_tick` and
 *  `*out_clock_ms` are its own when not NULL. */
struct ReplayCheckpoint const*
ReplayCheckpointStore_AtOrBefore(
    struct ReplayCheckpointStore const* store,
    int tick,
    int* out_tick,
    uint64_t* out_clock_ms);

int
ReplayCheckpointStore_Count(struct ReplayCheckpointStore const* store);

size_t
ReplayCheckpointStore_Bytes(struct ReplayCheckpointStore const* store);

/** The `i`th checkpoint's tick, ascending. */
int
ReplayCheckpointStore_TickAt(
    struct ReplayCheckpointStore const* store,
    int i);

#endif
