#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_CONTROL_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_CONTROL_H

/*
 * The game server's control link, and the live reload it carries.
 *
 * docs/MAPED_IMPLEMENTATION.md section 4 is the wire; docs/MAPED_DESIGN.html
 * #run is the behaviour. An editor (torirsmaped's LiveLink) connects to the
 * GAME port and says 0xED as its first byte -- 'G' is a WebSocket upgrade, 15
 * is JS5, the login opcodes are the game -- and from then on speaks frames
 * `[u32 type][u32 length][payload]`, little-endian, strings `u32 len, bytes`,
 * the same framing as ToriRSMapEd.
 *
 * Who may: behind the sign-in gate (TORIRSSERVER_GATE=1 / --gate) CTL_HELLO
 * must carry a ticket io_gate.c minted (ToriRSServer_GateTicketCheck); with no
 * gate only a loopback peer is accepted, at the first byte. Nothing but
 * CTL_HELLO is answered before a HELLO was accepted.
 *
 * When: every request is read and answered at the world's tick boundary
 * (ToriRSServer_ControlTick, the first thing ToriRSServer_WorldTick does), so a
 * RELOAD has been applied by the time CTL_FACT_RELOAD_DONE is written and no
 * script is mid-instruction while tables are swapped.
 *
 * The reload itself (ToriRSServer_Reload) is callable without a socket: the
 * tests drive it directly.
 */

#include <stddef.h>
#include <stdint.h>

struct ToriRSServer;
struct ToriRSServerPlayer;
struct RSCache_ServerPack;

#define TORIRSSERVER_CTL_FIRST_BYTE 0xED
#define TORIRSSERVER_CTL_VERSION 1

enum ToriRSServerCtlFrame
{
    TORIRSSERVER_CTL_HELLO = 1,
    TORIRSSERVER_CTL_STATUS = 2,
    TORIRSSERVER_CTL_RELOAD = 3,
    TORIRSSERVER_CTL_CHEAT = 4,
    TORIRSSERVER_CTL_SUBSCRIBE = 5,
    TORIRSSERVER_CTL_BYE = 6,
    /** Who is on: answered by CTL_FACT_PLAYERS. */
    TORIRSSERVER_CTL_PLAYERS = 7,

    TORIRSSERVER_CTL_FACT_HELLO = 129,
    TORIRSSERVER_CTL_FACT_STATUS = 130,
    TORIRSSERVER_CTL_FACT_LINE = 131,
    TORIRSSERVER_CTL_FACT_RELOAD_DONE = 132,
    TORIRSSERVER_CTL_FACT_VARS = 133,
    /**
     * u32 logins (every login this process took: the sum of the pool slots'
     * login generations), u32 active (players on now), u32 rows (at most
     * TORIRSSERVER_CTL_PLAYER_ROWS of them), rows * (string account_name,
     * string display_name, u32 slot, u32 login_generation, i32 x, i32 z,
     * i32 level). Sent once after an accepted CTL_HELLO -- which is how an
     * editor learns the server answers CTL_PLAYERS at all (an older server
     * closes the link on a frame it does not know; an older editor ignores a
     * fact it does not know) -- and then for every CTL_PLAYERS. A relog is a
     * new (slot, login_generation) pair.
     */
    TORIRSSERVER_CTL_FACT_PLAYERS = 134,
};

/** Rows CTL_FACT_PLAYERS lists; `active` still counts everyone. */
#define TORIRSSERVER_CTL_PLAYER_ROWS 256

/* CTL_FACT_RELOAD_DONE.status */
enum ToriRSServerReloadStatus
{
    /** Everything asked for was applied. */
    TORIRSSERVER_RELOAD_OK = 0,
    /** Applied what can be taken live; `restart` names what cannot. */
    TORIRSSERVER_RELOAD_RESTART_NEEDED = 1,
    /** A part that was asked for failed (a pack that does not load); what
     *  failed was not applied, and `lines` says why. */
    TORIRSSERVER_RELOAD_FAILED = 2,
};

/** One frame's payload cap: a RELOAD of a whole client pack's archive list
 *  fits many times over. */
#define TORIRSSERVER_CTL_FRAME_MAX (4 * 1024 * 1024)

#define TORIRSSERVER_RELOAD_GROUPS_MAX 8192
#define TORIRSSERVER_RELOAD_SQUARES_MAX 1024
#define TORIRSSERVER_RELOAD_SPAWNS_MAX 256
#define TORIRSSERVER_RELOAD_PATH_MAX 1024
#define TORIRSSERVER_RELOAD_LINES_MAX 64
#define TORIRSSERVER_RELOAD_LINE_MAX 256
#define TORIRSSERVER_RELOAD_RESTART_MAX 32

/**
 * CTL_RELOAD, decoded. `script_dir` empty: scripts unchanged; otherwise the
 * wire's script_manifest_path -- the pack's `pack.manifest`, whose directory
 * is the pack (a directory is taken as the pack itself). `groups` are
 * (index, archive) pairs of the CLIENT cache the build rewrote -- the server
 * pack's own archives are named by index 255 rows (`index ==
 * TORIRSSERVER_RELOAD_SERVPACK_INDEX`, archive = the server pack group) -- and
 * are forwarded to every client as CACHE_INVALIDATE. Heap-only: it is ~0.4 MB.
 */
#define TORIRSSERVER_RELOAD_SERVPACK_INDEX 255

struct ToriRSServerReloadRequest
{
    char script_dir[TORIRSSERVER_RELOAD_PATH_MAX];
    int group_count;
    struct
    {
        int index;
        int archive;
    } groups[TORIRSSERVER_RELOAD_GROUPS_MAX];
    int square_count;
    struct
    {
        int map_x;
        int map_z;
    } squares[TORIRSSERVER_RELOAD_SQUARES_MAX];
    int spawn_count;
    char spawn_files[TORIRSSERVER_RELOAD_SPAWNS_MAX][TORIRSSERVER_RELOAD_PATH_MAX];
};

struct ToriRSServerReloadResult
{
    enum ToriRSServerReloadStatus status;
    int scripts_swapped;
    int groups_replaced;
    int squares_reloaded;
    int spawns_changed;
    /** Players sent a CACHE_INVALIDATE. */
    int clients_invalidated;
    int restart_count;
    char restart[TORIRSSERVER_RELOAD_RESTART_MAX][TORIRSSERVER_RELOAD_LINE_MAX];
    int line_count;
    char lines[TORIRSSERVER_RELOAD_LINES_MAX][TORIRSSERVER_RELOAD_LINE_MAX];
};

/**
 * Apply a reload NOW. Call only at a tick boundary (ToriRSServer_ControlTick
 * does; a test between ticks may). In order, as the contract has it: scripts
 * (shadow, then swap), server-pack config groups, map squares (re-read, then
 * a REBUILD for every player whose window covers one), the edited lane's
 * spawns, and last the client cache's archive list sent to every logged-in
 * client as CACHE_INVALIDATE.
 */
void
ToriRSServer_Reload(
    struct ToriRSServer* srv,
    const struct ToriRSServerReloadRequest* request,
    struct ToriRSServerReloadResult* result);

/** A line for the reload's answer (also printed). */
void
ToriRSServer_ReloadLine(
    struct ToriRSServerReloadResult* result,
    const char* fmt,
    ...);

/** A part that cannot be taken live: the restart-needed list. */
void
ToriRSServer_ReloadRestart(
    struct ToriRSServerReloadResult* result,
    const char* fmt,
    ...);

/* ------------------------------------------------------------------ */
/* The link                                                            */
/* ------------------------------------------------------------------ */

/**
 * A connection whose first byte was 0xED. Called from the connection layer's
 * first-byte select (ToriRSServer_ConnOpenStep), which then lets its own copy
 * go: the link keeps a duplicate of the descriptor and owns it from here on.
 * `gated`: the server is behind the sign-in gate, and CTL_HELLO must carry a
 * ticket. Without the gate a peer that is not loopback is refused here.
 * Returns 1 when adopted.
 */
int
ToriRSServer_ControlAdopt(
    int fd,
    int gated);

/**
 * Read, apply and answer every control request that arrived, then retire
 * script tables nothing runs on any more. The world tick's first step. A
 * world with no control connection pays one branch.
 */
void
ToriRSServer_ControlTick(struct ToriRSServer* srv);

/**
 * A loopback listener that takes ONLY the control link, for a process with no
 * game port of its own (torirsserver --scriptrun --control-port). Open
 * returns the listening descriptor, or -1 (the reason printed). Pump accepts
 * what waits and adopts every connection whose first byte is 0xED
 * (ToriRSServer_ControlAdopt, loopback, no gate); call it before each world
 * tick, whose first step pumps the adopted links. Close closes the listener
 * and every connection still waiting for its first byte. Not supported on
 * win32 or the browser (Open answers -1).
 */
int
ToriRSServer_ControlListenOpen(int port);

void
ToriRSServer_ControlListenPump(int listen_fd);

void
ToriRSServer_ControlListenClose(int listen_fd);

/** Close every control connection (shutdown, tests). */
void
ToriRSServer_ControlCloseAll(void);

/** Open control connections (tests). */
int
ToriRSServer_ControlCount(void);

/* ------------------------------------------------------------------ */
/* Reload parts, one per owning file                                   */
/* ------------------------------------------------------------------ */

/* torirs_server_scripts.c */

/**
 * Load the pack at `dir` into a shadow table and swap it in, keeping the
 * running table alive while any parked or queued instance still runs on it.
 * Refused (the running table untouched, `why` filled) when the pack does not
 * load, uses opcodes this binary lacks, binds npc_del to queue 1, or was
 * renumbered. `out_changed`: scripts whose code differs, plus added and
 * retired ids. Returns 1 when swapped.
 */
int
ToriRSServer_ScriptsReload(
    struct ToriRSServer* srv,
    const char* dir,
    int* out_changed,
    char* why,
    size_t why_cap);

/** Free every retired table no holder runs on. Returns how many remain. */
int
ToriRSServer_ScriptsRetireSweep(struct ToriRSServer* srv);

/** Retired tables still alive for `srv` (tests). */
int
ToriRSServer_ScriptsRetiredCount(const struct ToriRSServer* srv);

/** The running pack's stamp for CTL_FACT_STATUS: "" when none is loaded. */
void
ToriRSServer_ScriptsStampText(
    const struct ToriRSServer* srv,
    char* out,
    size_t cap);

/* torirs_server_servpack.c */

/**
 * Re-load one config kind's table from an open server pack. Returns 1 when
 * replaced, 0 when that kind cannot be replaced live (its records are copied
 * into, or pointed at by, state the world holds), -1 when the pack refused it
 * (the running table is kept). `why` says which, or carries a note on a 1.
 */
typedef int (*ToriRSServerServPackKindReload)(
    void* context,
    struct RSCache_ServerPack* pack,
    int config_kind,
    char* why,
    size_t why_size);

/**
 * The game server's answer (`context` is the ToriRSServer): seq, healthbar,
 * struct, loc, npc and obj are replaced; the rest answer 0 with the reason
 * (torirs_server_control.c). Public for the tests, which hand it a pack of
 * their own.
 */
int
ToriRSServer_ReloadConfigKind(
    void* context,
    struct RSCache_ServerPack* pack,
    int config_kind,
    char* why,
    size_t why_size);

/**
 * Re-read the server pack at `pack_dir` (under its shared lock) for the
 * changed server-pack groups, and hand every config kind they hold to
 * `reload` once. One line per kind goes into `result` ("config: loc 2 of 2
 * group(s) replaced"); a group that names no config kind (a name table) is
 * added to `result`'s restart list, as is every kind `reload` answers 0 for.
 * `*out_refused` counts the kinds `reload` refused (-1). Returns the number of
 * groups whose kind was replaced, or -1 when the pack does not open.
 */
int
ToriRSServer_ServPackReloadGroups(
    const char* pack_dir,
    const int* groups,
    int group_count,
    ToriRSServerServPackKindReload reload,
    void* context,
    struct ToriRSServerReloadResult* result,
    int* out_refused);

/** The pack's fingerprint as text, for CTL_FACT_STATUS. */
void
ToriRSServer_ServPackStampText(
    const char* pack_dir,
    char* out,
    size_t cap);

/* torirs_server_world.c */

/**
 * Re-read map squares from the cache (ToriRSServer_SceneReloadSquares) and owe
 * a REBUILD to every player whose scene window covers one of them; a player
 * in an instance gets REBUILD_REGION with reload=1. Returns squares reloaded.
 */
int
ToriRSServer_WorldReloadSquares(
    struct ToriRSServer* srv,
    const int* map_x,
    const int* map_z,
    int count);

/**
 * The loc records were replaced (ToriRSServer_ContentReloadLocs): owe every
 * logged-in player one REBUILD, through the same barrier a square reload uses,
 * so a client re-places its scenery and re-reads each placed loc's record --
 * its menu ops, name and footprint come from the record at placement, and the
 * CACHE_INVALIDATE sent before the REBUILD has dropped its copies. Returns the
 * players owed.
 */
int
ToriRSServer_WorldReloadOweRebuild(struct ToriRSServer* srv);

/**
 * The edited lane's spawn files changed: re-read them, diff the roster, take
 * the removed spawns out of the world and let the added ones stand up through
 * the ordinary roster sync. Returns spawns added plus removed.
 */
int
ToriRSServer_WorldReloadSpawns(
    struct ToriRSServer* srv,
    const char* scripts_dir,
    const char* const* files,
    int file_count,
    struct ToriRSServerReloadResult* result);

/**
 * 1 once, and cleared, when `player`'s next REBUILD_REGION should say
 * reload=1 (a reloaded square is in its window). For the encoder.
 */
int
ToriRSServer_WorldTakeRebuildReload(struct ToriRSServerPlayer* player);

/* torirs_server_control.c */

/** Archives one CACHE_INVALIDATE names; a longer list is several packets. */
#define TORIRSSERVER_CACHE_INVALIDATE_PER_PACKET 4096

/**
 * CACHE_INVALIDATE's payload (3rd/rsprot/spec/packets/cache_invalidate.ini:
 * U1 index, U2_12 count, count x U4_1234 archive) for at most
 * TORIRSSERVER_CACHE_INVALIDATE_PER_PACKET archives, into `out` (3 + 4 x
 * count bytes). Returns the length.
 */
int
ToriRSServer_CacheInvalidateEncode(
    int index,
    const int* archives,
    int count,
    uint8_t* out);

/**
 * CACHE_INVALIDATE (our own server prot, 3rd/rsprot/spec/packets/
 * cache_invalidate.ini) to `player`: forget `count` archives of cache index
 * `index`. Returns 0 when the player's revision has no opcode for it.
 */
int
ToriRSServer_SendCacheInvalidate(
    struct ToriRSServerPlayer* player,
    int index,
    const int* archives,
    int count);

/* torirs_server_content.c */

/**
 * Re-read `files` (paths under `scripts_dir`, or absolute) into the spawn
 * tables: their old rows and suppression rows are dropped and the files
 * parsed again where they still exist; suppressions are then re-applied over
 * the merged rows. The generated lane (areas/world/configs) is boot-only and
 * is refused into `result`'s restart list. Returns 1 when the tables were
 * rebuilt. Call ToriRSServer_ContentNpcSpawns / ObjSpawns again after: the
 * arrays move.
 */
int
ToriRSServer_ContentReloadSpawnFiles(
    const char* scripts_dir,
    const char* const* files,
    int file_count,
    struct ToriRSServerReloadResult* result);

#endif
