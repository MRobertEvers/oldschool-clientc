#ifndef SRC_PLATFORM_NET_TRANSPORT_H
#define SRC_PLATFORM_NET_TRANSPORT_H

/*
 * Transport seam. A NetTransport bridges the net subsystem's outbound ring to a
 * concrete wire and pushes received bytes onto the command bus as
 * TORIRS_CMD_NET_* — exactly the PlatformSocket_Poll contract, so the net stack
 * (and record/replay) is transport-blind. The revision table's transport_kind
 * (enum NetTransportKind in rev/gameproto_revisions.h) selects the impl:
 *   NET_TRANSPORT_TCP -> raw TCP (thin adapter over PlatformSocket)
 *   NET_TRANSPORT_WS  -> a minimal RFC 6455 WebSocket client (xrsps)
 *
 * NET_RECV always carries de-framed application bytes: the WS impl strips the
 * WebSocket frame headers, so the framer above (packetbuffer) never sees them.
 */

struct NetTransport;
struct ToriRS_Network;
struct ToriRS_CmdBus;

struct NetTransportVTable
{
    void (*poll)(struct NetTransport* t, struct ToriRS_Network* net, struct ToriRS_CmdBus* bus);
    void (*free_)(struct NetTransport* t);
};

struct NetTransport
{
    struct NetTransportVTable const* vtable;
};

/** Create a transport for the given enum NetTransportKind; default_port applies
 * when the CONNECT target carries no ":port". `rev_name` is the client's
 * protocol name ("osrs239", ...): the embed transport hands it to the
 * in-process server so both ends always speak the same wire; the socket
 * transports ignore it (the remote host chose its own). Returns NULL on
 * unknown kind. */
struct NetTransport*
NetTransport_New(int kind, int default_port, char const* rev_name);

/** One poll step: drain net->out (connect/send), pump the connection, and push
 * received/de-framed bytes + status changes onto the bus. */
static inline void
NetTransport_Poll(struct NetTransport* t, struct ToriRS_Network* net, struct ToriRS_CmdBus* bus)
{
    t->vtable->poll(t, net, bus);
}

static inline void
NetTransport_Free(struct NetTransport* t)
{
    if( t )
        t->vtable->free_(t);
}

/* Explicit test clock; only works for the in-process transport. */
struct ToriRSServerEmbed;
struct ToriRSServerEmbed* NetTransport_TestClock(struct NetTransport* t, unsigned long long now);

/* The unlocked clock (App_SpeedUnlock): `cycles` is every client logic cycle
 * run so far, handed over each frame before the poll. The in-process server
 * then runs on cycles x 20 ms instead of the wall clock, so its 600 ms tick is
 * exactly 30 client cycles however fast frames come. A test clock, when one is
 * handed over, still wins: its driver owns the world's time. Only the embed
 * transport has a server whose clock is this client's; `t` must be one. */
void NetTransport_CycleClock(struct NetTransport* t, unsigned long long cycles);

/*
 * RSProx replay (net_transport_replay.c): the server's half of a recorded
 * Jagex session, played back at this client. Created directly rather than by
 * kind, because it needs the recording: `rec` is an indexed
 * RsproxRecording (net/rsprox_recording.h), moved into the transport.
 * `rev` is the client's table for the recording's revision.
 */
struct RsproxRecording;
struct GameProtoRevTable;

struct NetTransport*
NetTransport_NewReplay(struct RsproxRecording* rec, struct GameProtoRevTable const* rev);

int NetTransport_IsReplay(struct NetTransport const* t);

/** The client's logic cycles run so far, every frame before the poll: the
 *  unlocked replay clock is cycles x 20 ms. `t` must be a replay transport. */
void NetTransport_ReplayClock(struct NetTransport* t, unsigned long long cycles);

/** Unlocked: play as fast as the client runs, the recording's gaps kept in
 *  cycles. Locked (the default): at the recording's own pace. Switchable at
 *  any time; the clock does not jump. */
void NetTransport_ReplaySetUnlocked(struct NetTransport* t, int unlocked);
/** The clock in force: the setting above, or unlocked while a seek runs. */
int NetTransport_ReplayUnlocked(struct NetTransport* t);

/** SERVER_TICK_ENDs the client has executed, ever (App.server_tick_serial),
 *  every frame before the poll: a seek lands, and a hold reports, on the
 *  client's own count. */
void NetTransport_ReplayClientTicks(struct NetTransport* t, unsigned serial);
/** The client's packet pipeline is held on a load it awaits (a scene build, a
 *  cache fetch), every frame: the stalled-seek watchdog does not count it as
 *  a seek going nowhere (replay/replay_scrub.h ReplayScrub_ClientBusy). */
void NetTransport_ReplayClientBusy(struct NetTransport* t, int busy);

/*
 * The scrubber's controls (replay/replay_scrub.h). A position is a tick: tick
 * t is every packet of ticks < t executed by the client, none later staged.
 *
 *   Seek  to `tick` (clamped to 0..tick_count); replaces a seek in flight;
 *         keeps the hold / run state. Behind the playhead -- or after the
 *         recording finished -- the session restarts from the login.
 *   Step  to the position + `ticks` (a seek's target while one is in flight),
 *         then hold.
 *   Hold  stop the replay clock at the next tick boundary.
 *   Run   release a hold.
 *
 * While NetTransport_ReplaySeeking the client is expected to run as fast as it can,
 * undrawn and muted (main.c main_rsprox_replay_frame).
 */
void NetTransport_ReplaySeek(struct NetTransport* t, int tick);
void NetTransport_ReplayStep(struct NetTransport* t, int ticks);
void NetTransport_ReplayHold(struct NetTransport* t);
void NetTransport_ReplayRun(struct NetTransport* t);
/** Play on from a hold and hold again at `tick` (not behind the playhead). */
void NetTransport_ReplayHoldAt(struct NetTransport* t, int tick);
int NetTransport_ReplaySeeking(struct NetTransport* t);
int NetTransport_ReplayHeld(struct NetTransport* t);
/*
 * Session checkpoints (replay/replay_checkpoint.h, the store the scrubber
 * keeps: replay/replay_checkpoint_store.h). The client side is
 * replay/replay_checkpoint_play.c, every replay frame:
 *
 *   CheckpointWanted  a clean tick boundary (everything handed out executed,
 *                     nothing on the bus) the store wants a checkpoint at;
 *                     the client takes one and hands it over (CheckpointAdd);
 *   RestoreWanted     a seek's RESTORE step is ready: the checkpoint to put
 *                     back; the client restores it and says so (Restored).
 *   SeekFromCheckpoint the checkpoint check's seek (replay_scrub.h).
 */
#define NETTRANSPORT_HAS_REPLAY_CHECKPOINTS 1
struct ReplayCheckpoint;
int NetTransport_ReplayCheckpointWanted(struct NetTransport* t, int* out_tick, unsigned long long* out_clock_ms);
void NetTransport_ReplayCheckpointAdd(
    struct NetTransport* t,
    int tick,
    unsigned long long clock_ms,
    struct ReplayCheckpoint* cp);
struct ReplayCheckpoint const* NetTransport_ReplayRestoreWanted(struct NetTransport* t);
void NetTransport_ReplayRestored(struct NetTransport* t, double cost_ms);
void NetTransport_ReplaySeekFromCheckpoint(
    struct NetTransport* t,
    int tick,
    struct ReplayCheckpoint const* cp,
    int cp_tick,
    unsigned long long cp_clock_ms,
    int preroll);
/** The store's count and bytes, for the report. */
void NetTransport_ReplayCheckpointStats(struct NetTransport* t, int* out_count, unsigned long long* out_bytes);

/** The plan later seeks get: 1 the fast one (chase, keyframe, pre-roll), 0
 *  every tick simulated. Starts as TORIRS_REPLAY_SEEK says (`fast` or
 *  `fullsim`; unset is the default, replay/replay_scrub.h). */
void NetTransport_ReplaySetSeekFast(struct NetTransport* t, int fast);
struct ReplaySeekTimes;
/** The last landed seek's phase times (replay/replay_scrub.h). */
void NetTransport_ReplaySeekTimes(struct NetTransport* t, struct ReplaySeekTimes* out);
/** Client cycles per chase tick (1..30): TORIRS_REPLAY_CHASE_CYCLES. */
void NetTransport_ReplaySetChaseCycles(struct NetTransport* t, int cycles);

/** The recording's timeline as the page's timeline() JSON, malloc'd. */
char* NetTransport_ReplayTimelineJson(struct NetTransport* t);

struct NetTransportReplayStatus
{
    /** "idle", "connecting", "logging_in", "playing", "finished". */
    char const* phase;
    int tick;
    int tick_count;
    unsigned long long clock_ms;
    unsigned long long duration_ms;
    int unlocked;
    /** Held at a rebuild until the client reports MAP_BUILD_COMPLETE. */
    int waiting_map;
    int revision;
    int world;
    /** Logins answered so far: "idle" with none is a client still booting. */
    int sessions;
    /** Of those, the ones that took the session up again over a new
     *  connection -- a recorded RECONNECT's RECONNECT_OK, a hop's login --
     *  rather than playing it from the start. */
    int resumed;
    /** The replay clock is held (a hold, or after a step). */
    int held;
    /** A seek is in flight; seek_target / seek_from are -1 when not. */
    int seeking;
    int seek_target;
    int seek_from;
    /** "", "restart", "chase", "world" or "preroll" (replay/replay_scrub.h). */
    char const* seek_phase;
    /** The recording header's timestamp: unix ms at t_ms 0. */
    long long timestamp_ms;
};

void NetTransport_ReplayGetStatus(struct NetTransport* t, struct NetTransportReplayStatus* out);

#endif
