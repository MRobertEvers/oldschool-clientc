#ifndef SRC_TORIRSMAPED_TORIRS_MAPED_H
#define SRC_TORIRSMAPED_TORIRS_MAPED_H

/*
 * ToriRSMapEd: the map editor's server, and the AUTHORITY on the world's
 * editing state.
 *
 * The server owns the document. The authoritative Editor_Doc, the undo/redo
 * stack, the content tree, its single-writer lock and the bake all live
 * here; nothing else writes any of them. Clients hold MIRRORS: a client
 * opens a square (the server answers its current text), sends edits as
 * intents, and mutates its mirror only when the server broadcasts the
 * command back as a fact. Every client is a peer on that wire — the world
 * viewer is one client, a command panel is another, a second viewer a third
 * — and none of them can disagree about the document, because none of them
 * owns it.
 *
 * What clients keep for themselves is everything derived and everything
 * cosmetic: parsing/emitting stays client-side too for the mirror, and
 * deriving authored tiles into renderable terrain happens per client (it
 * needs the cache profile, which the server deliberately does not link).
 *
 * It is a *server* in the same sense ToriRSServer is: a core with no main()
 * that reads and writes through a transport seam, an embed API for hosting
 * it inside the client's process (torirs_maped_embed.h), and a socket front
 * door (torirs_maped_main.c). The client picks which server a boot talks to
 * in the manifest: `[editor:boot] server=` names this one, `[net:boot]` the
 * game.
 *
 * Threading: none, same rule as the game server. In-process, client and
 * server share one thread, so nothing here may block; the session reads what
 * the transport has and returns.
 *
 * Wire format, little-endian throughout:
 *
 *     [u32 type][u32 payload_length][payload]
 *
 * CONNECTIONS, CLIENTS, AND STATE SCOPE. A connection is one stateful peer
 * on the wire, and a CLIENT is a group of them acting as one editing
 * session: its world viewer can be a connection that is nothing but a 3D
 * rendering of the mirror, its control panel a connection with no world at
 * all. HELLO declares membership — pass a client id to join that group, or
 * 0 to be granted a fresh one (FACT_HELLO returns it; hand it to the other
 * connections of the same session).
 *
 * The two broadcast scopes fall out of that split:
 *
 *   - DOCUMENT facts (FACT_CMD, FACT_SAVED) go to EVERY connection. There
 *     is one world; every Client sees every edit, whoever made it.
 *   - STATE facts (FACT_STATE) go only to the requester's OWN group, and
 *     the store is per group. A Client's selection, tool and picks are its
 *     session's business — its controllers must follow its viewer's click,
 *     while another Client editing the same world never sees them.
 *
 * A connection publishes a state field (STATE_SET — "the selection is now
 * this loc"), the server stores it last-writer-wins under the group and
 * relays it group-wide, and every member updates from the fact. The server
 * never interprets state values — semantics belong to the clients — which
 * is why the key space is open. STATE_SYNC replays the group's store, so a
 * connection attaching late starts current.
 *
 * Requests (client -> server) and the fact each is answered with. FACT_CMD
 * and FACT_SAVED broadcast to all sessions, FACT_STATE to the requester's
 * group — the requester included, in every case, and mirrors mutate only
 * on the broadcast:
 *
 *     HELLO        u32 version, u32 role,   -> FACT_HELLO   u32 version,
 *                  u32 client_id (0 = new)       u32 writable, u32 client_id
 *     SQUARE_LIST  (empty)                  -> FACT_SQUARE_LIST
 *                                                u32 status, u32 count,
 *                                                count * (i32 x, i32 z)
 *     SQUARE_LOAD  i32 x, i32 z             -> FACT_SQUARE_LOAD
 *                                                u32 status,
 *                                                u32 jm2_size_plus, u32 jl2_size_plus,
 *                                                jm2 bytes, jl2 bytes
 *                  (the raw files, no document involvement — tooling only)
 *     SQUARE_OPEN  i32 x, i32 z             -> FACT_SQUARE_TEXT
 *                                                u32 status, i32 x, i32 z,
 *                                                u32 jm2_size_plus, u32 jl2_size_plus,
 *                                                jm2 bytes, jl2 bytes
 *                  (opens the square in the AUTHORITATIVE doc; the text is
 *                   the document's CURRENT state — a square another client
 *                   already edited answers with those edits in it)
 *     CMD          encoded Editor_Cmd       -> broadcast FACT_CMD
 *                  (TORIRSMAPED_CMD_WIRE)        u32 seq, u32 direction,
 *                                                encoded Editor_Cmd
 *                                              then FACT_STATUS to the requester
 *     UNDO / REDO  (empty)                  -> one broadcast FACT_CMD per
 *                                              command in the reverted group,
 *                                              then FACT_STATUS (extra = count)
 *     STROKE       u32 begin                -> FACT_STATUS
 *                  (groups the commands between begin and end into one undo step)
 *     SAVE_ALL     (empty)                  -> broadcast FACT_SAVED
 *                                                u32 count, count * (i32 x, i32 z)
 *                                              then FACT_STATUS (extra = count)
 *     SQUARE_SAVE  i32 x, i32 z,            -> FACT_STATUS  (raw file write,
 *                  u32 jm2_size_plus, u32 jl2_size_plus,     tooling only —
 *                  jm2 bytes, jl2 bytes                      bypasses the doc)
 *     SPAWN_SAVE   i32 x, i32 z,            -> FACT_STATUS
 *                  u32 size_plus, bytes
 *     BAKE         (empty)                  -> 0+ FACT_BAKE_LINE (text),
 *                                              then FACT_BAKE_DONE u32 status
 *     STATE_SET    u32 key, u32 count,      -> FACT_STATE to the group
 *                  count * i32 values           u32 seq, u32 key, u32 count,
 *                                               count * i32 values
 *                  (fire-and-forget: no per-set ack, because state sets ride
 *                   interactive paths — a selection click must not stall on a
 *                   round trip; the sender applies its own echo like any peer)
 *     STATE_SYNC   (empty)                  -> one FACT_STATE per key stored
 *                                              for the group, to the requester
 *                                              only, then FACT_STATUS
 *                                              (extra = key count)
 *     BYE          (empty)                  -> nothing; the session ends
 *
 * FACT_STATUS is `u32 request, u32 status, u32 extra` — `extra` carries the
 * op's count where one exists (undo/redo commands reverted, squares saved)
 * and 0 elsewhere.
 *
 * `size_plus` is 0 for an ABSENT payload half and size+1 for a present one,
 * because absent and empty differ on both sides of the wire: a save with no
 * jl2 text leaves that file alone, a load of a square with no `.jl2` answers
 * absent, and flattening either to "" would rewrite or invent files.
 *
 * The tree lock is the SERVER's, taken at ToriRSMapEd_Open and held for its
 * lifetime: with the server as the only writer, "who may save" is a property
 * of the server, not of any client. FACT_HELLO's `writable` reports it, and
 * a server that found the tree locked (another server owns it) serves
 * everything read-only — SAVE_ALL answers LOCKED.
 */

#include <stdint.h>

#include "editor/editor_cmd.h"
#include "editor/editor_host.h"

enum
{
    TORIRSMAPED_PROTO_VERSION = 1,
    /** Frame header: u32 type + u32 payload length. */
    TORIRSMAPED_FRAME_HEADER = 8,
    /** Sanity bound on one payload. The largest legitimate frame is a square
     *  save (two text files); the whole content tree's square list is ~24KB.
     *  A length beyond this is a corrupt or hostile stream, not data. */
    TORIRSMAPED_PAYLOAD_MAX = 16 * 1024 * 1024,
    /** Above the game servers' 43594..43601 block. */
    TORIRSMAPED_DEFAULT_PORT = 43610,
};

enum ToriRSMapEdFrameType
{
    TORIRSMAPED_REQ_HELLO = 1,
    TORIRSMAPED_REQ_SQUARE_LIST = 2,
    TORIRSMAPED_REQ_SQUARE_LOAD = 3,
    TORIRSMAPED_REQ_SQUARE_SAVE = 4,
    TORIRSMAPED_REQ_SPAWN_SAVE = 5,
    TORIRSMAPED_REQ_BAKE = 6,
    TORIRSMAPED_REQ_BYE = 8,
    TORIRSMAPED_REQ_SQUARE_OPEN = 9,
    TORIRSMAPED_REQ_CMD = 10,
    TORIRSMAPED_REQ_UNDO = 11,
    TORIRSMAPED_REQ_REDO = 12,
    TORIRSMAPED_REQ_STROKE = 13,
    TORIRSMAPED_REQ_SAVE_ALL = 14,
    TORIRSMAPED_REQ_STATE_SET = 15,
    TORIRSMAPED_REQ_STATE_SYNC = 16,

    TORIRSMAPED_FACT_HELLO = 129,
    TORIRSMAPED_FACT_SQUARE_LIST = 130,
    TORIRSMAPED_FACT_SQUARE_LOAD = 131,
    TORIRSMAPED_FACT_STATUS = 132,
    TORIRSMAPED_FACT_BAKE_LINE = 133,
    TORIRSMAPED_FACT_BAKE_DONE = 134,
    TORIRSMAPED_FACT_SQUARE_TEXT = 135,
    /** Broadcast: the authoritative document changed. Mirrors apply it. */
    TORIRSMAPED_FACT_CMD = 136,
    /** Broadcast: these squares were written to disk; mirrors clear dirty. */
    TORIRSMAPED_FACT_SAVED = 137,
    /** Broadcast: one shared-state field changed. Mirrors store it and tell
     *  whoever registered for it. */
    TORIRSMAPED_FACT_STATE = 138,
};

/** What a connection is for. Informational today (the server relays every
 *  broadcast to every session); the seam where per-role filtering would go. */
enum ToriRSMapEdRole
{
    TORIRSMAPED_ROLE_GENERIC = 0,
    /** A 3D rendering of the mirror; publishes picks, draws the document. */
    TORIRSMAPED_ROLE_VIEWER = 1,
    /** Panels and inputs; may hold no world at all. */
    TORIRSMAPED_ROLE_CONTROLLER = 2,
};

/**
 * Shared-state keys. An OPEN set: the server stores and relays any key
 * verbatim, so a new panel field needs a constant here and nothing
 * server-side. Value layouts are the clients' contract with each other.
 */
enum ToriRSMapEdStateKey
{
    /** kind (enum Editor_SelectionKind), scene_x, scene_z, level, loc_id,
     *  shape, angle — what the SELECT tool is latched onto. */
    TORIRSMAPED_STATE_SELECTION = 1,
    /** enum Editor_Tool. */
    TORIRSMAPED_STATE_TOOL = 2,
    /** The pinned edit plane, -1..3. */
    TORIRSMAPED_STATE_EDIT_LEVEL = 3,
    /** kind (enum CacheProvider_CatalogKind), id — the catalog's pick. */
    TORIRSMAPED_STATE_CATALOG_PICK = 4,
    /** map_x, map_z — the square the session is looking at. */
    TORIRSMAPED_STATE_VIEW_SQUARE = 5,
};

enum
{
    /** Values one state field can carry. Sized for records, not arrays. */
    TORIRSMAPED_STATE_VALUES_MAX = 16,
    /** Distinct keys the store holds. */
    TORIRSMAPED_STATE_KEYS_MAX = 64,
};

/** One Editor_Cmd on the wire: every field as a u32/i32, both halves always
 *  present. Fixed-size on purpose — a variable encoding saves tens of bytes
 *  on a loopback wire and costs a second code path on both ends. */
enum
{
    TORIRSMAPED_CMD_WIRE = 144,
};

void
ToriRSMapEd_CmdEncode(
    const struct Editor_Cmd* command,
    uint8_t out[TORIRSMAPED_CMD_WIRE]);

/** Returns 1 on success; 0 means the bytes are not a command (wrong size or
 *  an unknown kind), which on this wire is a framing-level error. */
int
ToriRSMapEd_CmdDecode(
    const uint8_t* body,
    uint32_t length,
    struct Editor_Cmd* out_command);

static inline uint32_t
ToriRSMapEd_ReadU32(const uint8_t* src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16)
           | ((uint32_t)src[3] << 24);
}

static inline void
ToriRSMapEd_WriteU32(
    uint8_t* dst,
    uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xff);
    dst[1] = (uint8_t)((value >> 8) & 0xff);
    dst[2] = (uint8_t)((value >> 16) & 0xff);
    dst[3] = (uint8_t)((value >> 24) & 0xff);
}

/*
 * The byte-stream seam, mirroring ToriRSServerTransport's contract:
 *
 *   recv   > 0  bytes taken
 *          = 0  nothing available yet — not end-of-stream
 *          < 0  the peer is gone
 *   send   len  on success (buffering or looping internally), -1 once dead
 */
struct ToriRSMapEdTransport
{
    void* ctx;
    int (*recv)(void* ctx, uint8_t* dst, int max);
    int (*send)(void* ctx, const uint8_t* src, int len);
};

/* ------------------------------------------------------------------ */
/* The server                                                          */
/* ------------------------------------------------------------------ */

#include "editor/editor_doc.h"

struct ToriRSMapEdSession;

/** Sessions one server can hold at once: a viewer, a couple of control
 *  clients, and slack. Bounds the broadcast fan-out, not the world. */
enum
{
    TORIRSMAPED_SESSION_MAX = 8,
};

/**
 * One content tree being served, and the authoritative editing state over it.
 *
 * NEVER a stack local: the document is ~8 MB of squares and the undo stack
 * larger. Hosts hold one on the heap (the embed does) or in static storage
 * (the daemon does).
 *
 * The file layer is the same EditorHost local binding a desktop session used
 * to call directly — the server owns it now, and the wire is the only way in.
 */
struct ToriRSMapEd
{
    struct EditorHost files;
    int open;

    /** The document. Clients hold mirrors of it; this is the original. */
    struct Editor_Doc doc;
    struct Editor_UndoStack undo;
    /** 0 when another server holds the tree's lock: serve, but never save. */
    int writable;
    /** Stamped onto every FACT_CMD, so a mirror can assert it missed nothing. */
    uint32_t seq;

    /** Every attached session, for broadcasts. Registered by SessionInit. */
    struct ToriRSMapEdSession* sessions[TORIRSMAPED_SESSION_MAX];

    /** The shared-state store: last write per (client group, key), for relay
     *  and for STATE_SYNC replay. Values are opaque here — clients own
     *  semantics; the group scope is what keeps one Client's selection out
     *  of another Client's panels. */
    struct
    {
        uint32_t client_id;
        uint32_t key;
        uint32_t count;
        int32_t values[TORIRSMAPED_STATE_VALUES_MAX];
    } state[TORIRSMAPED_STATE_KEYS_MAX];
    int state_count;

    /** Group ids granted to HELLOs that asked for a new one. */
    uint32_t next_client_id;
};

/**
 * Open a server over a content tree and take its single-writer lock.
 *
 * @param repo_root  where a bake runs from; NULL disables baking.
 *
 * Always succeeds structurally; a tree with no readable `maps/` serves ABSENT
 * and IO_ERROR answers rather than refusing to start, the same way the local
 * host behaved. A tree whose lock another server holds opens READ-ONLY
 * (`writable` 0) — serving viewers of a tree someone else is editing is a
 * state, not a failure.
 */
void
ToriRSMapEd_Open(
    struct ToriRSMapEd* maped,
    const char* content_dir,
    const char* repo_root);

void
ToriRSMapEd_Close(struct ToriRSMapEd* maped);

/* ------------------------------------------------------------------ */
/* One connection                                                      */
/* ------------------------------------------------------------------ */

#include "torirs_maped_buf.h"

struct ToriRSMapEdSession
{
    struct ToriRSMapEdTransport transport;
    /** Inbound bytes not yet parsed into whole frames. */
    struct ToriRSMapEdBuf in;
    int alive;
    /** The server this session is registered with, for broadcasts. */
    struct ToriRSMapEd* maped;
    /** enum ToriRSMapEdRole, from HELLO. Informational until filtering. */
    uint32_t role;
    /** The Client (session group) this connection belongs to, from HELLO.
     *  0 until the handshake; state facts relay only within a group. */
    uint32_t client_id;
};

/**
 * Attach a connection to a server. Asserts a free session slot — the hosts
 * (embed, daemon) both cap their accept paths at the same bound, so a full
 * table here is a host bug, not load.
 */
void
ToriRSMapEd_SessionInit(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdTransport* transport);

int
ToriRSMapEd_SessionAlive(const struct ToriRSMapEdSession* session);

/**
 * Read what the transport has, act on every complete frame, send the answers
 * — and the broadcasts to every OTHER registered session a frame provokes.
 *
 * Never blocks. Returns 0 once the session is dead — peer gone, BYE received,
 * or a malformed frame (a framing error is unrecoverable: with no resync
 * marker every later byte would be misread, so the session ends loudly).
 */
int
ToriRSMapEd_SessionPump(struct ToriRSMapEdSession* session);

/** Unregisters from the server and frees the buffers. NULL-tolerant. */
void
ToriRSMapEd_SessionFree(struct ToriRSMapEdSession* session);

#endif
