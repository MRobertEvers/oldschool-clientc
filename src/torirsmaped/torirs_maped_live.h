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

    TORIRSMAPED_FACT_LIVE_STATUS = 175,
    TORIRSMAPED_FACT_LIVE_LINE = 176,
    TORIRSMAPED_FACT_LIVE_VARS = 177,

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

    TORIRSMAPED_CTL_FACT_HELLO = 129,
    TORIRSMAPED_CTL_FACT_STATUS = 130,
    TORIRSMAPED_CTL_FACT_LINE = 131,
    TORIRSMAPED_CTL_FACT_RELOAD_DONE = 132,
    TORIRSMAPED_CTL_FACT_VARS = 133,
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
 *   <io_bin> --manifest <io_manifest> --root <io_root> --port <io_port>
 *       (only with an io_manifest)
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
