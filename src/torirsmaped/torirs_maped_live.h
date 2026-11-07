#ifndef SRC_TORIRSMAPED_TORIRS_MAPED_LIVE_H
#define SRC_TORIRSMAPED_TORIRS_MAPED_LIVE_H

/*
 * The live link: the editor server's connection to a GAME server, and RUN's
 * last step (docs/MAPED_DESIGN.html #run; MAPED_IMPLEMENTATION.md sections 3
 * and 4, owner live-editor).
 *
 * The editor server attaches to one game server at a time, two ways:
 *
 *   launch  it starts the lane's torirsserver, js5_server and io_server
 *           (struct ToriRSMapEdLane, torirs_maped_build.h) itself and owns
 *           their lifetime: LIVE_DETACH or the server's Close stops them.
 *   attach  it dials a running server by address ("host:port").
 *
 * Either way it speaks the game server's CONTROL protocol on the game port:
 * one 0xED byte selects the link, then frames `[u32 type][u32 length]
 * [payload]`, little-endian, strings `u32 len, bytes` (brief section 4):
 *
 *   CTL_HELLO 1       u32 version=1, string ticket  -> CTL_FACT_HELLO 129
 *                                                      u32 version, u32 ok,
 *                                                      string server_name,
 *                                                      u32 players
 *   CTL_STATUS 2                                    -> CTL_FACT_STATUS 130
 *                                                      u32 players, u32 tick,
 *                                                      string script_stamp,
 *                                                      string servpack_stamp,
 *                                                      u32 stale_count, rows
 *   CTL_RELOAD 3      string script_manifest_path,  -> CTL_FACT_LINE 131 (0+),
 *                     u32 n, n * (u32 index, u32 archive),   CTL_FACT_RELOAD_DONE 132
 *                     u32 m, m * (i32 map_x, i32 map_z),     u32 status, u32 scripts,
 *                     u32 k, k * string spawn_file           u32 groups, u32 squares,
 *                                                            u32 spawns,
 *                                                            u32 restart_count, rows
 *   CTL_CHEAT 4       string player, string line    -> CTL_FACT_LINE rows,
 *                                                      then CTL_FACT_STATUS
 *   CTL_SUBSCRIBE 5   string player, u32 n, n * varp -> CTL_FACT_VARS 133 on change
 *                                                      string player, u32 n,
 *                                                      n * (u32 varp, i32 value)
 *   CTL_BYE 6
 *   CTL_PLAYERS 7                                   -> CTL_FACT_PLAYERS 134
 *                                                      u32 logins, u32 active,
 *                                                      u32 rows, rows * (string
 *                                                      account, string display,
 *                                                      u32 slot, u32 generation,
 *                                                      i32 x, i32 z, i32 level)
 *
 * The server sends one CTL_FACT_PLAYERS right after an accepted HELLO; only
 * then does the editor send CTL_PLAYERS (with each CTL_STATUS, every second
 * while the play client is logging in), because a server older than the frame
 * closes the link on it. `logins` counts every login the process took (a relog
 * is a new (slot, generation) pair), which is how a test proves a RUN did not
 * relog the play client.
 *
 * RUN's reload names, from what this server saw and built since the last
 * successful reload: the lane's script manifest (`<live_dir>/scripts/
 * pack.manifest`), the CLIENT-CACHE archives the client build wrote (the
 * clients' CACHE_INVALIDATE list), the squares saved, and the edited spawn
 * files saved (content-relative paths). A failed reload keeps them pending,
 * so the next RUN names them again.
 *
 * Editor frames (live-editor's requests 53..64 / facts 173..190):
 *
 *   LIVE_ATTACH    u32 mode (0 attach, 1 launch), string address ("host:port",
 *                  "" for launch), string ticket ("" when the server has no gate)
 *                  -> broadcast FACT_LIVE_STATUS (connecting, then attached
 *                     or failed)
 *   LIVE_DETACH    (empty) -> broadcast FACT_LIVE_STATUS (detached)
 *   LIVE_CHEAT     string player, string line
 *                  -> broadcast FACT_LIVE_LINE rows, then FACT_LIVE_STATUS
 *   LIVE_SUBSCRIBE string player, u32 n, n * u32 varp (n = 0 unsubscribes)
 *                  -> broadcast FACT_LIVE_VARS whenever a value changes
 *
 *   FACT_LIVE_STATUS  u32 state (enum ToriRSMapEdLiveState), u32 launched,
 *                     string address, string server_name, u32 players,
 *                     u32 tick, string script_stamp, string servpack_stamp,
 *                     u32 stale_count, stale_count * string, string message,
 *                     u32 run_step (enum ToriRSMapEdRunStep; IDLE when no RUN
 *                     is in flight), u32 pending_squares, u32 pending_spawns,
 *                     u32 pending_archives
 *   FACT_LIVE_LINE    string line
 *   FACT_LIVE_VARS    string player, u32 n, n * (u32 varp, i32 value)
 *
 *   LIVE_PLAY      u32 action (enum ToriRSMapEdPlayAction)
 *                  -> broadcast FACT_LIVE_PLAY (and FACT_LIVE_STATUS when it
 *                     launched the lane)
 *
 *   FACT_LIVE_PLAY    u32 play_state (enum ToriRSMapEdPlayState), u32 browser,
 *                     string account, i32 pid, i32 exit_status, string message,
 *                     i32 x, i32 z, i32 level (-1 until seen), u32 starts,
 *                     string web_url ("" when the lane serves no web page),
 *                     u32 players_known, u32 logins, u32 active,
 *                     u32 rows, rows * (string name, i32 x, i32 z, i32 level)
 *
 * A link that is not attached answers LIVE_CHEAT / LIVE_SUBSCRIBE with a
 * FACT_LIVE_STATUS whose message says so; a server with no link binding
 * (TORIRSMAPED_CAP_LIVE clear) answers every LIVE_* that way too.
 *
 * STATE_LIVE_FOCUS (40) is the followed player, shared across a Client so a
 * viewer can lock to them: values[0] name length (0 = nobody), values[1..4]
 * the name's bytes packed little-endian four to a value (16 bytes max),
 * values[5..7] absolute x, z, level (-1 while the link reports no position).
 *
 * BUILD NOTE: torirs_maped_live.c is compiled INTO torirs_maped.c, after
 * torirs_maped_build.c. Never link it on its own.
 */

#include "torirs_maped.h"
#include "torirs_maped_build.h"

#include <stddef.h>
#include <stdint.h>

enum
{
    TORIRSMAPED_REQ_LIVE_ATTACH = 55,
    TORIRSMAPED_REQ_LIVE_DETACH = 56,
    TORIRSMAPED_REQ_LIVE_CHEAT = 57,
    TORIRSMAPED_REQ_LIVE_SUBSCRIBE = 58,
    TORIRSMAPED_REQ_LIVE_PLAY = 59,

    TORIRSMAPED_FACT_LIVE_STATUS = 175,
    TORIRSMAPED_FACT_LIVE_LINE = 176,
    TORIRSMAPED_FACT_LIVE_VARS = 177,
    TORIRSMAPED_FACT_LIVE_PLAY = 179,

    TORIRSMAPED_STATE_LIVE_FOCUS = 40,
};

/** STATE_LIVE_FOCUS's layout. */
enum
{
    TORIRSMAPED_LIVE_FOCUS_NAME_LEN = 0,
    TORIRSMAPED_LIVE_FOCUS_NAME = 1, /* 4 values */
    TORIRSMAPED_LIVE_FOCUS_X = 5,
    TORIRSMAPED_LIVE_FOCUS_Z = 6,
    TORIRSMAPED_LIVE_FOCUS_LEVEL = 7,
    TORIRSMAPED_LIVE_FOCUS_VALUES = 8,
    TORIRSMAPED_LIVE_FOCUS_NAME_MAX = 16,
};

_Static_assert(
    TORIRSMAPED_LIVE_FOCUS_VALUES <= TORIRSMAPED_STATE_VALUES_MAX,
    "the live focus is one state field");

/** Pack a player name (at most TORIRSMAPED_LIVE_FOCUS_NAME_MAX bytes; longer
 *  is cut) and a position into STATE_LIVE_FOCUS values. "" means nobody. */
void
ToriRSMapEd_LiveFocusPack(
    const char* name,
    int x,
    int z,
    int level,
    int32_t out[TORIRSMAPED_LIVE_FOCUS_VALUES]);

/** The name out of STATE_LIVE_FOCUS values; 0 when `count` is short or the
 *  length is out of range (a peer's bytes are data). */
int
ToriRSMapEd_LiveFocusName(
    const int32_t* values,
    int count,
    char* out,
    size_t out_size);

/** The game server's control protocol (brief section 4). */
enum
{
    TORIRSMAPED_CTL_SELECT_BYTE = 0xED,
    TORIRSMAPED_CTL_VERSION = 1,

    TORIRSMAPED_CTL_HELLO = 1,
    TORIRSMAPED_CTL_STATUS = 2,
    TORIRSMAPED_CTL_RELOAD = 3,
    TORIRSMAPED_CTL_CHEAT = 4,
    TORIRSMAPED_CTL_SUBSCRIBE = 5,
    TORIRSMAPED_CTL_BYE = 6,
    TORIRSMAPED_CTL_PLAYERS = 7,

    TORIRSMAPED_CTL_FACT_HELLO = 129,
    TORIRSMAPED_CTL_FACT_STATUS = 130,
    TORIRSMAPED_CTL_FACT_LINE = 131,
    TORIRSMAPED_CTL_FACT_RELOAD_DONE = 132,
    TORIRSMAPED_CTL_FACT_VARS = 133,
    TORIRSMAPED_CTL_FACT_PLAYERS = 134,
};

enum ToriRSMapEdLiveState
{
    TORIRSMAPED_LIVE_DETACHED = 0,
    /** Launched or dialled; waiting for the connection and CTL_FACT_HELLO. */
    TORIRSMAPED_LIVE_CONNECTING = 1,
    TORIRSMAPED_LIVE_ATTACHED = 2,
    /** The last attach failed or the link dropped; `message` says why. */
    TORIRSMAPED_LIVE_FAILED = 3,
};

const char*
ToriRSMapEd_LiveStateName(uint32_t state);

/** What LIVE_ATTACH asks for. */
struct ToriRSMapEdLiveTarget
{
    /** 1: start the lane's servers and dial the game port on loopback. */
    int launch;
    char host[256];
    int port;
};

/**
 * The seam under the link: a byte stream to one game server, opened by
 * launching or dialling. The server core speaks the protocol on top; the
 * binding only connects, carries bytes and stops what it started. Nothing
 * blocks.
 */
struct ToriRSMapEdLiveLink
{
    void* ctx;
    /** Begin: launch the lane's servers (target->launch) or dial. OK, or why
     *  not (`why` filled). The connection comes up in `poll`. */
    enum EditorHost_Status (*open)(
        void* ctx,
        const struct ToriRSMapEdLiveTarget* target,
        char* why,
        size_t why_size);
    /** Advance the connection: 1 connected, 0 still coming up, -1 failed
     *  (`why` filled). */
    int (*poll)(void* ctx, char* why, size_t why_size);
    /** As ToriRSMapEdTransport: > 0 taken, 0 nothing yet, < 0 gone. */
    int (*recv)(void* ctx, uint8_t* dst, int max);
    /** len on success (buffering internally), -1 once dead. */
    int (*send)(void* ctx, const uint8_t* src, int len);
    /** Drop the connection and stop every process `open` launched. */
    void (*close)(void* ctx);
    /** Release ctx (after close). May be NULL for a host-owned binding. */
    void (*free_)(void* ctx);
};

/**
 * Bind (or clear) the live link; sets TORIRSMAPED_CAP_LIVE. Owned as the
 * BuildRunner is. Attaching needs no lane; launching and RUN's reload do
 * (ToriRSMapEd_SetLane).
 */
void
ToriRSMapEd_SetLiveLink(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdLiveLink* link);

/**
 * The native link: TCP to the game port (non-blocking connect, retried every
 * 250 ms for up to 60 s while a launched server boots) and, for launch,
 * posix_spawn of the lane's binaries:
 *
 *   <server_bin> <game_port> --rev <rev>   env TORIRSSERVER_CONTENT=<content>,
 *       TORIRSSERVER_CACHE=<live>/cache, TORIRSSERVER_SCRIPTS=<live>/scripts,
 *       TORIRSSERVER_PACK_DIR=<live>/servpack
 *   <js5_bin> --cache <live>/cache --revision <rev number> --port <js5_port>
 *       --max-pending 65535
 *   <io_bin> --root <io_root> --port <io_port> --boot-root <live>/play/web
 *       (only when io_manifest is set: it switches io_server on; the page's
 *       manifest is the lane's web play tree's, ToriRSMapEd_LaneWebPlayUrl)
 *
 * Close sends SIGTERM to each, waits up to two seconds, then SIGKILL.
 * Returns 0 where processes and sockets are unavailable (web, Windows).
 * `lane` may be NULL for attach-only.
 */
int
ToriRSMapEd_LiveLinkNative(
    struct ToriRSMapEdLiveLink* out,
    const struct ToriRSMapEdLane* lane);

/** The pids a native link launched (0 for one it did not start), for a test
 *  to see the lifetime. `link` must be a native binding. */
void
ToriRSMapEd_LiveLinkNativePids(
    const struct ToriRSMapEdLiveLink* link,
    int out_pids[3]);

/**
 * Bind the native runner and link from the environment, for hosts with no
 * flags of their own (the embedded server): $TORIRSMAPED_LIVE_DIR names the
 * lane's live dir under `repo_root`/`content_dir`; unset binds nothing.
 * Returns 1 when bound. The daemon calls the same with its own flags.
 */
int
ToriRSMapEd_BindLiveFromEnv(
    struct ToriRSMapEd* maped,
    const char* repo_root,
    const char* content_dir);

/**
 * Advance the build and the live link: poll the running build, read the
 * control connection, send the periodic CTL_STATUS, time out a reload. Never
 * blocks. ToriRSMapEd_SessionPump calls it, so an embed that pumps every
 * frame needs nothing more; a host whose loop can sit idle (the daemon's
 * select) calls it each pass, and while ToriRSMapEd_Busy answers 1 should
 * not sleep longer than ~50 ms.
 */
void
ToriRSMapEd_Service(struct ToriRSMapEd* maped);

/** 1 while a build, a RUN or a connecting link wants servicing soon. */
int
ToriRSMapEd_Busy(const struct ToriRSMapEd* maped);

/**
 * The lane as it is, to one session that just said HELLO: FACT_LIVE_STATUS
 * and FACT_LIVE_PLAY, which are otherwise broadcast only when they change.
 * A window opened mid-session (a popped-out panel is a second process on
 * the desktop) showed "play: not started" beside a play client that was
 * logged in, until the next change (2026-11-06). Nothing for a v1 peer.
 */
void
ToriRSMapEd_LiveWelcome(
    struct ToriRSMapEd* maped,
    struct ToriRSMapEdSession* session);

/* ---- the play client --------------------------------------------------------
 *
 * Each lane has one PLAY CLIENT: a game client (the lane's play_bin, by
 * default src/torirs) logged into the lane's game server through the lane's
 * JS5, as a dev account the lane owns (ToriRSMapEd_LanePlayAccount; its first
 * save is the lane's play_fixture, so it does not start on Tutorial Island).
 * The editor server starts and tracks it the way it starts and tracks the
 * lane's servers, so a RUN from a window, the Live panel's Play and
 * `torirsmapedctl play` all reach the same one, with or without a window.
 *
 * Liveness is two facts: the process (reaped by the service pass; EXITED with
 * its status) and the server's player list (CTL_FACT_PLAYERS: ONLINE when the
 * account is on, LOGGED_OUT when it was and is not any more).
 *
 * RUN, after the reload (RUN_STEP_PLAY):
 *   - no play client yet           -> start it; it logs in on the new content
 *   - alive and on the server      -> it took the reload live (CACHE_INVALIDATE
 *                                     and the REBUILD); "reloaded in the
 *                                     running client"
 *   - exited, or logged out        -> start it again
 *   - still logging in             -> left alone; it boots on the new content
 * and when the lane is not up at the reload: a lane that can launch is
 * launched (on the packs and cache the RUN just built, so no reload is owed)
 * and the play client started; its login builds the main torirsserver's world,
 * which is when the server first answers the control link and the link
 * attaches. A lane that cannot launch keeps the old failure, plus "Launch the
 * lane first".
 *
 * A browser window (LIVE_PLAY browser) plays in a tab of its own: the editor
 * server then never spawns a native client, and RUN's play step answers with
 * the lane's web play URL (FACT_LIVE_PLAY web_url) for the window to open
 * when the account is not on the server.
 */

enum ToriRSMapEdPlayState
{
    /** Not started (or stopped). */
    TORIRSMAPED_PLAY_NONE = 0,
    /** Spawned; not on the server yet. */
    TORIRSMAPED_PLAY_STARTING = 1,
    /** On the server (CTL_FACT_PLAYERS names the account). */
    TORIRSMAPED_PLAY_ONLINE = 2,
    /** The process runs but the account left the server. */
    TORIRSMAPED_PLAY_LOGGED_OUT = 3,
    /** The process is gone; FACT_LIVE_PLAY exit_status says how. */
    TORIRSMAPED_PLAY_EXITED = 4,
};

enum ToriRSMapEdPlayAction
{
    /** Start it unless it runs and is on the server (a logged-out or exited
     *  one is started again); launches the lane first when it is down. */
    TORIRSMAPED_PLAY_START = 0,
    /** Stop it and start it again. */
    TORIRSMAPED_PLAY_RESTART = 1,
    TORIRSMAPED_PLAY_STOP = 2,
    /** This window plays in a browser tab: never spawn a native client;
     *  answer with the web play URL. */
    TORIRSMAPED_PLAY_BROWSER = 3,
};

const char*
ToriRSMapEd_PlayStateName(uint32_t state);

/** The play account of `lane`: its play_user/play_pass, else "maped<game
 *  port>" and "lane<game port>run" (names are 1..12 characters; passwords
 *  8..20 with a letter and a digit and not containing the name). */
void
ToriRSMapEd_LanePlayAccount(
    const struct ToriRSMapEdLane* lane,
    char* out_user,
    size_t user_size,
    char* out_pass,
    size_t pass_size);

/** The URL a browser plays the lane at ("" when the lane launches no
 *  io_server, i.e. has no io_manifest): io_server's page with the lane's game
 *  port as both the connect target and the WebSocket, its JS5 as js5_url. */
void
ToriRSMapEd_LaneWebPlayUrl(
    const struct ToriRSMapEdLane* lane,
    char* out,
    size_t out_size);

/** What the play launcher starts. */
struct ToriRSMapEdPlaySpec
{
    char user[16];
    char pass[24];
};

/**
 * The seam under the play client, as the live link is under the control
 * connection: start a process, say whether it still runs, stop it. Nothing
 * blocks except `stop` (bounded: SIGTERM, a grace, SIGKILL).
 */
struct ToriRSMapEdPlayLauncher
{
    void* ctx;
    /** 1 when the lane's game port (and JS5 port, when the lane has a JS5)
     *  accept a connection: a client started before that fails its boot. */
    int (*ready)(void* ctx);
    /** Prepare (manifest, save, directories) and start. The pid in
     *  `out_pid`; why not in `why`. */
    enum EditorHost_Status (*start)(
        void* ctx,
        const struct ToriRSMapEdPlaySpec* spec,
        int* out_pid,
        char* why,
        size_t why_size);
    /** 1 while it runs; 0 once it exited (status in `out_status`: the exit
     *  code, or -signal); 0 too when nothing was started (status 0). */
    int (*alive)(void* ctx, int* out_status);
    /** Stop what `start` started (nothing when nothing runs). */
    void (*stop)(void* ctx);
    void (*free_)(void* ctx);
};

/** Bind (or, with NULL, clear) the play launcher. Owned as the live link is. */
void
ToriRSMapEd_SetPlayLauncher(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdPlayLauncher* launcher);

/**
 * The native play launcher. `start` makes <live>/play, <live>/saves and
 * <live>/logs, writes <live>/play/manifest.ini from lane->play_manifest (the
 * [cache:boot] dir becomes <live>/play/cache, [net:boot] host/port the lane's
 * game port on 127.0.0.1, every "../" or "./" value absolute against the
 * source manifest's directory), copies lane->play_fixture to
 * <live>/saves/<account>.ini when that save does not exist yet (its `name =`
 * the account), and spawns
 *
 *   <play_bin> --manifest <live>/play/manifest.ini --js5 --js5-host 127.0.0.1
 *       --js5-port <js5_port> --js5-fallback-port 0 --user <user> --pass <pass>
 *
 * with output to <live>/logs/play.log and this process's environment minus
 * the capture and editor keys (TORIRS_SIM_HOTKEY, TORIRS_EXIT_BMP,
 * TORIRS_MAX_FRAMES, TORIRS_BMP_SERIES, TORIRS_PRESENT_BMP*, TORIRS_SCREENSHOT*,
 * TORIRS_EDITOR_*, TORIRSMAPED_*), TORIRS_PREFS=<live>/play/preferences.ini,
 * and every TORIRSMAPED_PLAY_ENV_<NAME>=<value> as NAME=<value> (how a
 * headless run gives the play client its own capture keys). Returns 0 where
 * processes are unavailable (web, Windows) or the lane has no play_bin.
 */
int
ToriRSMapEd_PlayLauncherNative(
    struct ToriRSMapEdPlayLauncher* out,
    const struct ToriRSMapEdLane* lane);

/** Write the play manifest (as the native launcher's `start` does) to
 *  `out_path`. 1 written; 0 with `why` (the source cannot be read, the
 *  destination cannot be written). Exported for the tests. */
int
ToriRSMapEd_PlayManifestWrite(
    const struct ToriRSMapEdLane* lane,
    const char* out_path,
    char* why,
    size_t why_size);

/* ---- the control protocol, for a peer --------------------------------------
 *
 * The encoders the editor server sends with, exported so a test's mock peer
 * and the client's decoders read the same layouts. */

/** A control frame `[type][len][payload]` appended to `out`. */
void
ToriRSMapEd_CtlFrame(
    struct ToriRSMapEdBuf* out,
    uint32_t type,
    const struct ToriRSMapEdBuf* payload);

#endif
