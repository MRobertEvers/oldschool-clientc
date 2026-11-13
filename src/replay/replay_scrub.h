#ifndef SRC_REPLAY_REPLAY_SCRUB_H
#define SRC_REPLAY_REPLAY_SCRUB_H

/*
 * The replay's playhead: the cursor into the recording's frames, the replay
 * clock, the map-build barrier's hold on it, and seeks (the replay scrubber
 * design, "A seek, step by step").
 *
 * The transport (platform/net_transport_replay.c) keeps the handshake, both
 * ciphers, its staging buffer and the bus; it no longer decides what goes
 * next. Each poll it advances the clock (ReplayScrub_Advance) and asks
 * ReplayScrub_Next what to stage until the answer is WAIT. A seek is done by
 * what Next hands out, so the client needs no seek mode.
 *
 * POSITION. Tick t means every server packet of ticks < t has been EXECUTED by
 * the client (not merely staged) and none of tick >= t has been staged. The
 * client's own count of executed SERVER_TICK_ENDs (App.server_tick_serial) is
 * handed over every frame (ReplayScrub_ClientTicks) and measured from the
 * count at the session's start, so "executed" is the client's word for it.
 *
 * A SEEK is a plan of steps (struct ReplaySeekPlan):
 *
 *   RESTART   the target is behind what has been staged: Next answers
 *             REPLAY_NEXT_RESTART once, the transport drops the session, the
 *             client re-establishes it (app_net_link_watch) and the new login
 *             plays from frame 0 (ReplayScrub_Rewind, _SessionBegin);
 *   FULLSIM   every packet of every tick before the target, on the replay
 *             clock (the client is expected to run unlocked, k cycles a frame,
 *             undrawn and muted while ReplayScrub_Seeking), none at or past it;
 *   CHASE     (the fast plan) every tick up to P = target - REPLAY_PREROLL_TICKS,
 *             session and stream packets only (replay_classify.h): the world
 *             and the transients are not handed out at all. Each tick goes
 *             as a REPLAY_NEXT_COORD (the local tile the ledger says) and its
 *             packets, `chase_cycles` client cycles apart. A chase from the
 *             login plays tick 0 whole: its REBUILD carries the player table's
 *             init and puts the client in the world;
 *   WORLD     REPLAY_NEXT_WORLD once: the ledger's keyframe at P, after the
 *             REBUILD it names when the client's scene is another (the
 *             map-build barrier waits for that one as for any REBUILD);
 *   PREROLL   P to the target, every packet, exactly as FULLSIM;
 *   PRESENT   the client has executed the target's predecessor tick end: the
 *             seek is over, and the replay holds or runs;
 *   RESTORE   (the fast plan, in place of RESTART) a session checkpoint at
 *             tick X <= P (replay/replay_checkpoint_store.h) is put back
 *             instead of logging in again: the tick the client is part way
 *             through is finished (its world withheld if a chase was
 *             handing it out), the client restores the checkpoint when it
 *             can (ReplayScrub_RestoreWanted / _Restored), and the cursor and
 *             the replay clock move back to X with the session kept. The
 *             chase then starts at X. Used for a backward seek, and for a
 *             forward one whose checkpoint is past the live client.
 *
 * The fast plan is (RESTART | RESTORE)? CHASE? WORLD PREROLL PRESENT; a seek of at most
 * REPLAY_PREROLL_TICKS forward, one whose P is in a world-entity span, or one
 * the ledger cannot serve, is FULLSIM and says so. Which plan is used is
 * ReplayScrub_SetSeekMode's (TORIRS_REPLAY_SEEK=fullsim|fast at the
 * transport).
 */

#include "replay/replay_timeline.h"

#include <stdint.h>

struct RsproxRecording;
struct GameProtoRevTable;
struct ReplayScrub;
struct ReplayCheckpoint;
struct ReplayCheckpointStore;

/* What the transport stages next (ReplayScrub_Next). */
enum ReplayNext
{
    /** Nothing is due: stop staging this poll. */
    REPLAY_NEXT_WAIT = 0,
    /** A server packet: frames[step.frame], re-enciphered as recorded. */
    REPLAY_NEXT_FRAME,
    /** RSProx's RECONNECT at frames[step.frame]: the recorded session's
     *  connection ended here. The transport ends this one once the client
     *  has executed everything before it, and answers the client's
     *  GAMERECONNECT with RECONNECT_OK carrying the frame's block
     *  (net_transport_replay.c); the cursor goes on after the frame, the
     *  executed-tick count with it (ReplayScrub_ConnectionLost / _Resumed). */
    REPLAY_NEXT_RESUME,
    /** A recorded RESUME_P_*DIALOG (step.dialog_name, a GameProtoPktOutName):
     *  a REPLAY_DIALOG control frame (net/torirs_control.h) at this byte of
     *  the stream. */
    REPLAY_NEXT_DIALOG,
    /** A seek behind the playhead: drop the session. The client's next login
     *  plays from frame 0 and the seek continues from there. */
    REPLAY_NEXT_RESTART,
    /** Every frame has been handed out: end the session (the LOGOUT). */
    REPLAY_NEXT_END,
    /** A chase tick begins: step.coord is the local player's tile (packed as
     *  `coord`), step.charge the client clocks the tick did not run.
     *  A REPLAY_COORD control frame (net/torirs_control.h) at this
     *  byte of the stream. */
    REPLAY_NEXT_COORD,
    /** The chase reached P: step.world is the keyframe (ownership moves to the
     *  transport, which posts it: replay_world.h), carrying the REBUILD the
     *  executor rebuilds from when the client's scene is another (the barrier
     *  then waits for its MAP_BUILD_COMPLETE). A REPLAY_WORLD control
     *  frame at this byte of the stream; the scrubber waits for the
     *  mailbox's slot (ReplayWorld_Pending) before it builds one. */
    REPLAY_NEXT_WORLD,
    /** A public chat line of a PLAYER_INFO the chase withheld: step.chat_*
     *  and step.name. A REPLAY_CHAT control frame at this byte
     *  of the stream. */
    REPLAY_NEXT_CHAT,
};

struct ReplayWorldDelivery;

struct ReplayStep
{
    int frame;
    int dialog_name;
    int coord;
    int charge;
    /** COORD: the local player's name on a chase's first tick, else "".
     *  CHAT: the speaker's. */
    char name[32];
    /** CHAT: a SAY (text in chat_data) or a CHAT (packed text). */
    int chat_say;
    int chat_crown;
    int chat_colour_effect;
    uint8_t const* chat_data;
    int chat_len;
    struct ReplayWorldDelivery* world;
};

enum ReplaySeekStepKind
{
    REPLAY_SEEK_RESTART = 0,
    REPLAY_SEEK_FULLSIM,
    REPLAY_SEEK_PRESENT,
    REPLAY_SEEK_CHASE,
    REPLAY_SEEK_WORLD,
    REPLAY_SEEK_PREROLL,
    REPLAY_SEEK_RESTORE,
};

/** Which plan a seek gets. */
enum ReplaySeekMode
{
    /** Every tick simulated (stage 0): the oracle and the fallback. */
    REPLAY_SEEK_MODE_FULLSIM = 0,
    /** Chase, keyframe, pre-roll (stage 2). */
    REPLAY_SEEK_MODE_FAST,
};

/** The plan a replay starts with (TORIRS_REPLAY_SEEK overrides it). Fast since
 *  replay-seek-check found every world domain (zones, npcs, players, ptable,
 *  varps, invs, stats, scene) equal to linear play at T and T+64 on a rev-241
 *  recording (500..4400) and a rev-227 one (3000, 16000). */
#define REPLAY_SEEK_DEFAULT_MODE REPLAY_SEEK_MODE_FAST
/** Client cycles a chase tick is given (TORIRS_REPLAY_CHASE_CYCLES). */
#define REPLAY_CHASE_DEFAULT_CYCLES 30

/** Ticks played in full in front of a fast seek's target. */
#define REPLAY_PREROLL_TICKS 16
/** Client cycles in one server tick (600 ms of 20 ms cycles). */
#define REPLAY_TICK_CYCLES 30

#define REPLAY_SEEK_PLAN_MAX 8

struct ReplaySeekPlan
{
    enum ReplaySeekStepKind steps[REPLAY_SEEK_PLAN_MAX];
    int count;
    /** The step in progress. */
    int at;
    int target;
    /** The tick the work started from: 0 after a restart. */
    int from;
    /** The fast plan's keyframe tick, P = target - REPLAY_PREROLL_TICKS; -1
     *  in a full simulation. */
    int world_tick;
    /** RESTORE: the checkpoint (borrowed: the store's, or the caller's of
     *  ReplayScrub_SeekFromCheckpoint), its tick and replay clock, and the
     *  tick boundary the client finishes before restoring. */
    struct ReplayCheckpoint const* restore_cp;
    int restore_tick;
    uint64_t restore_clock_ms;
    int drain_tick;
    /** The drain is the rest of a chase tick: its world stays withheld. */
    int drain_chasing;
};

struct ReplayPos
{
    /** ReplayScrub_Tick. */
    int tick;
    /** Logic cycles into the tick: the replay clock past its first packet,
     *  in 20 ms cycles (0 while a seek or a hold has it). */
    int cycle;
    /** The replay clock: recording ms since the login. */
    uint64_t t_ms;
    /** timestamp_ms + t_ms. */
    int64_t wall_ms;
    /** The scene in force at `tick` (ReplayTick.scene). */
    int scene;
};

/**
 * A playhead over `rec` (indexed), which the caller keeps alive and unmoved
 * for the scrubber's life; `rev` frames and decodes it. Builds the timeline.
 */
struct ReplayScrub*
ReplayScrub_New(
    struct RsproxRecording const* rec,
    struct GameProtoRevTable const* rev);

/** Accepts NULL. */
void
ReplayScrub_Free(struct ReplayScrub* scrub);

struct ReplayTimeline const*
ReplayScrub_Timeline(struct ReplayScrub const* scrub);

/* --- the controls -------------------------------------------------------- */

/** Seek to `tick` (clamped to 0..tick_count). Replaces a seek in flight; if
 *  the new target is ahead of everything staged, the work just continues.
 *  Keeps the hold / run state: a held replay holds at the target. */
void
ReplayScrub_SeekTick(
    struct ReplayScrub* scrub,
    int tick);

/** Seek to ReplayScrub_Tick + `ticks`, then hold. */
void
ReplayScrub_Step(
    struct ReplayScrub* scrub,
    int ticks);

/** Stop the clock at the next tick boundary (at a seek's target, when one is
 *  in flight). */
void
ReplayScrub_Hold(struct ReplayScrub* scrub);

/** Release a hold. */
void
ReplayScrub_Run(struct ReplayScrub* scrub);

/** Play on, and hold at `tick` (not behind what has been handed out): the
 *  seek check's "64 ticks later" without a seek. */
void
ReplayScrub_HoldAt(
    struct ReplayScrub* scrub,
    int tick);

/* --- session checkpoints -------------------------------------------------- */

/** The checkpoints this replay keeps (the scrubber owns them). */
struct ReplayCheckpointStore*
ReplayScrub_Checkpoints(struct ReplayScrub* scrub);

/**
 * A moment to checkpoint: the session is live, every packet handed out has
 * been executed and the cursor is at a tick boundary (`*out_tick`, with the
 * replay clock `*out_clock_ms`), no restore or restart is in flight, and the
 * store wants one there. The transport adds "nothing staged on the bus".
 */
int
ReplayScrub_CheckpointWanted(
    struct ReplayScrub const* scrub,
    int* out_tick,
    uint64_t* out_clock_ms);

/** A seek's RESTORE is ready for the client: the tick it was finishing has
 *  executed. The checkpoint to restore, or NULL. */
struct ReplayCheckpoint const*
ReplayScrub_RestoreWanted(struct ReplayScrub const* scrub);

/** The client restored it (in `cost_ms`): the cursor and the clock move back
 *  to its tick, the session kept, and the plan goes on. */
void
ReplayScrub_Restored(
    struct ReplayScrub* scrub,
    double cost_ms);

/**
 * Seek to `tick` from `cp` (taken at `cp_tick`, replay clock `cp_clock_ms`),
 * which the caller keeps alive until the seek lands, with a pre-roll of
 * `preroll` ticks (the keyframe at tick - preroll, which must be >= cp_tick).
 * The checkpoint check's second pass (replay/replay_checkpoint_check.c):
 * preroll = tick - cp_tick restores, keyframes the world at cp_tick, and plays
 * every packet from there.
 */
void
ReplayScrub_SeekFromCheckpoint(
    struct ReplayScrub* scrub,
    int tick,
    struct ReplayCheckpoint const* cp,
    int cp_tick,
    uint64_t cp_clock_ms,
    int preroll);

/* --- what it says -------------------------------------------------------- */

/** The position: a seek's target while one is in flight, the hold tick while
 *  held, else the next tick boundary staging can stop at (no tick at or
 *  beyond it has been staged). Step counts from here. */
int
ReplayScrub_Tick(struct ReplayScrub const* scrub);

/** Ticks the client has executed this session (0 before it starts). */
int
ReplayScrub_Reached(struct ReplayScrub const* scrub);

/** The tick of the last packet handed out (0 before any). */
int
ReplayScrub_LastTick(struct ReplayScrub const* scrub);

int
ReplayScrub_Held(struct ReplayScrub const* scrub);

int
ReplayScrub_Seeking(struct ReplayScrub const* scrub);

/** The seek in flight. 0 when there is none, and `out` is left alone. */
int
ReplayScrub_Plan(
    struct ReplayScrub const* scrub,
    struct ReplaySeekPlan* out);

/** "" or "restart" or "chase" or "world" or "preroll" (a full simulation is
 *  "preroll" throughout: every packet, undrawn and muted). */
char const*
ReplayScrub_SeekPhase(struct ReplayScrub const* scrub);

/** Where a seek's time went, on the transport's wall clock (ms; 0: the
 *  phase did not happen). `serial` counts landed seeks. */
struct ReplaySeekTimes
{
    unsigned serial;
    int target;
    int from;
    int fast;
    int restarted;
    int chase_ticks;
    int world_rebuilt;
    uint64_t start_ms;
    /** The restarted session's login done (the transport saw the game
     *  stream). */
    uint64_t session_ms;
    /** The first and the last chase tick handed out. */
    uint64_t chase_start_ms;
    uint64_t chase_end_ms;
    /** The keyframe handed out, and its scene built (= handed out when the
     *  scene was kept). */
    uint64_t world_ms;
    uint64_t world_built_ms;
    uint64_t landed_ms;
    /** RESTORE: the checkpoint's tick (-1: none), when the client restored
     *  it, and how long the restore itself took (ms, the client's measure). */
    int restore_tick;
    uint64_t restored_ms;
    double restore_cost_ms;
};

/** The last landed seek's times. */
void
ReplayScrub_SeekTimes(
    struct ReplayScrub const* scrub,
    struct ReplaySeekTimes* out);

/** The wall clock, every poll (ms): what the seek times are measured on. */
void
ReplayScrub_Wall(
    struct ReplayScrub* scrub,
    uint64_t wall_ms);

/** The plan later seeks get (a seek in flight keeps its own). */
void
ReplayScrub_SetSeekMode(
    struct ReplayScrub* scrub,
    enum ReplaySeekMode mode);

enum ReplaySeekMode
ReplayScrub_SeekMode(struct ReplayScrub const* scrub);

/** Client cycles a chase tick is given (1..REPLAY_TICK_CYCLES); the rest of
 *  the tick's 30 are charged to clientclock instead of run. */
void
ReplayScrub_SetChaseCycles(
    struct ReplayScrub* scrub,
    int cycles);

void
ReplayScrub_Pos(
    struct ReplayScrub const* scrub,
    struct ReplayPos* out);

uint64_t
ReplayScrub_ClockMs(struct ReplayScrub const* scrub);

/** Held at a REBUILD until the client reports MAP_BUILD_COMPLETE. */
int
ReplayScrub_WaitingMap(struct ReplayScrub const* scrub);

/* --- the transport's side ------------------------------------------------ */

/** A new connection: the cursor, the clock, the inputs and the barrier go back
 *  to frame 0. A seek in flight survives it (a restart is one). */
void
ReplayScrub_Rewind(struct ReplayScrub* scrub);

/** The client entered the game stream: ticks executed from now on count from
 *  the client tick count last handed over. */
void
ReplayScrub_SessionBegin(struct ReplayScrub* scrub);

/** The session's connection ended where the recording's did (a RECONNECT, a
 *  hop): nothing goes, no checkpoint is taken, and a seek counts the wait as
 *  a bounded one, until _ConnectionResumed. The cursor and the executed-tick
 *  count are kept. */
void
ReplayScrub_ConnectionLost(struct ReplayScrub* scrub);

/** A new connection took the session up again (RECONNECT_OK, a hop's login):
 *  the session goes on from the cursor, its executed ticks still counted from
 *  the session's start. */
void
ReplayScrub_ConnectionResumed(struct ReplayScrub* scrub);

/** The frame Next hands out next when it is a RECONNECT (PACKET_GROUP_ENDs
 *  stepped over), else -1: what a hop's login is followed by. */
int
ReplayScrub_NextReconnect(struct ReplayScrub const* scrub);

/** SERVER_TICK_ENDs the client has executed, ever (App.server_tick_serial),
 *  handed over every frame. */
void
ReplayScrub_ClientTicks(
    struct ReplayScrub* scrub,
    unsigned serial);

/** The client's packet pipeline is (1) or is not (0) held on a load it
 *  awaits -- a scene build, a cache fetch -- handed over every frame. Each has
 *  a budget of its own, so the stalled-seek watchdog counts that time apart
 *  from time the seek had no excuse (replay_scrub.c, REPLAY_SEEK_STALL_MS). */
void
ReplayScrub_ClientBusy(
    struct ReplayScrub* scrub,
    int busy);

/** The client sent MAP_BUILD_COMPLETE (ignored unless a rebuild is waiting). */
void
ReplayScrub_MapBuilt(struct ReplayScrub* scrub);

/** Move the clock `step_ms` of replay time; `wall_ms` is the wall clock, which
 *  bounds the map-build wait. The barrier, and a hold at its boundary, keep
 *  the clock where it is. */
void
ReplayScrub_Advance(
    struct ReplayScrub* scrub,
    uint64_t step_ms,
    uint64_t wall_ms);

/** What to stage next. Every answer but WAIT consumes what it names. */
enum ReplayNext
ReplayScrub_Next(
    struct ReplayScrub* scrub,
    struct ReplayStep* out);

#endif
