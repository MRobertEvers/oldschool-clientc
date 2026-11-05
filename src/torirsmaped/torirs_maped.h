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
 *                                                [v2: u32 capabilities
 *                                                 [string read_only_reason,
 *                                                  only when not writable]]
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
 * PROTOCOL v2 (docs/MAPED_IMPLEMENTATION.md sections 2 and 3). Every v1
 * number above keeps its meaning; a v1 client (HELLO version 1) is answered
 * with the 12-byte v1 FACT_HELLO and only ever sees v1 frames — FACT_CMD in
 * the fixed 144-byte form (square TILE/LOC only), FACT_SAVED as square
 * coordinates. A v2 client gets:
 *
 *     HELLO        u32 version=2, u32 role, -> FACT_HELLO   u32 version,
 *                  u32 client_id                 u32 writable, u32 client_id,
 *                                                u32 capabilities
 *                                                (TORIRSMAPED_CAP_*)
 *     DOC_OPEN     u32 kind, string key     -> FACT_DOC_TEXT
 *                                                u32 status, u32 kind,
 *                                                string key, string storage,
 *                                                u32 dirty, u32 part_count,
 *                                                part_count * (u32 size_plus,
 *                                                bytes)
 *                  (a square answers two parts, jm2 then jl2, dirty bit 0
 *                   terrain and bit 1 locs, and its key may carry a storage
 *                   prefix "ported/<lane>/maps/m16_101"; every other kind one
 *                   part. SQUARE_OPEN stays as the v1 alias of the square
 *                   form and answers the same text.)
 *     DOC_CLOSE    u32 kind, string key     -> FACT_STATUS
 *     CMD          the v2 command encoding  -> broadcast FACT_CMD u32 seq,
 *                  (ToriRSMapEd_CmdEncode)       u32 direction, encoded cmd,
 *                                              then FACT_STATUS (REFUSED when
 *                                              the document does not take it)
 *     SAVE         u32 kind, string key     -> broadcast FACT_SAVED, then
 *                                              FACT_STATUS (extra = count)
 *     SAVE_ALL     (empty)                  -> broadcast FACT_SAVED
 *                                                u32 count, count *
 *                                                (u32 kind, string key)
 *                                              then FACT_STATUS (extra = count)
 *     SAVE_AS      u32 kind, string key,    -> FACT_CMD stream (one undo
 *                  string new_key              step), then FACT_STATUS
 *                                              (extra = the new id)
 *                  (design #save "Save as new". A CFG record of a loc, npc
 *                   or obj type: "loc:<name>" copied to "loc:<new name>" --
 *                   the id from pack/<type>.alloc, the pack/<type>.client
 *                   line, the [new name] block with every field of the
 *                   source; editor_alloc.h Editor_AllocCopyPlan. REFUSED
 *                   when the new name is taken or not a name, ABSENT when
 *                   the source record is not there, UNSUPPORTED for every
 *                   other kind; a square's save-as-new is DOC_CREATE.)
 *     DOC_CREATE   u32 kind, string key,    -> FACT_CMD stream (the fill, one
 *                  string storage,             undo step), then FACT_STATUS
 *                  u32 template,               (REFUSED when the key exists
 *                  string source_key,          in that storage)
 *                  i32 fill_height,
 *                  i32 fill_underlay
 *
 * Strings are `u32 length, bytes`, never NUL-terminated on the wire.
 *
 * THE AREAS DOCUMENT (kind 5, EDITOR_DOC_AREAS, key "areas" -- the content
 * tree's `editor/areas.txt`, editor/editor_areas.h). Opened with DOC_OPEN like
 * any text document (one part, the file's text; absent = no areas yet) and
 * saved by SAVE / SAVE_ALL. Every client session opens it at
 * Editor_OpenHost, so every window mirrors it. Its commands (cmd kinds
 * 10..13: AREA_SET, AREA_REMOVE, COLLECTION_SET, COLLECTION_REMOVE) carry
 * the payload
 *
 *     i32 line, string before, string after
 *
 * -- one record line before and after (each <= 511 bytes, "" when the
 * command has none) and, for a remove, the line index it stood at. A SET
 * with has_before replaces the record named in `before` (a rename when the
 * names differ); without it adds one. REFUSED when the bytes do not match
 * the document's line or a name is taken.
 *
 * The tree lock is the SERVER's, taken at ToriRSMapEd_Open and held for its
 * lifetime: with the server as the only writer, "who may save" is a property
 * of the server, not of any client. FACT_HELLO's `writable` reports it, and
 * a server that found the tree locked (another server owns it) serves
 * everything read-only — SAVE_ALL answers LOCKED.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "editor/editor_cmd.h"
#include "editor/editor_host.h"
#include "torirs_maped_buf.h"
#include "torirs_maped_lanes.h"

enum
{
    /** What this build speaks. A HELLO naming TORIRSMAPED_PROTO_VERSION_V1
     *  is still answered — in v1 frames only. */
    TORIRSMAPED_PROTO_VERSION = 2,
    TORIRSMAPED_PROTO_VERSION_V1 = 1,
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

    /* v2. Request numbers are reserved by owner so parallel phases never
     * collide (MAPED_IMPLEMENTATION.md section 3):
     *   17..40  foundations (below; 22..40 free for this owner)
     *   41..48  catalog      CATALOG_QUERY 41, CATALOG_RECORD 42,
     *                        CATALOG_SQUARES 43
     *   49..52  views        SPAWN_VIEW 49 (torirs_maped_view.h)
     *   53..64  live-editor  BUILD 53, RUN 54, LIVE_ATTACH 55, LIVE_DETACH 56,
     *                        LIVE_CHEAT 57, LIVE_SUBSCRIBE 58
     *   65..80  accounts     ACCOUNTS_LIST 65, ACCOUNT_VIEW 66, ACCOUNT_CREATE 67,
     *                        ACCOUNT_DELETE 68, ACCOUNT_DUPLICATE 69, ACCOUNT_USE 70,
     *                        ACCOUNT_SET 71, ACCOUNT_PLACE 72, ACCOUNTS_QUESTS 73
     *                        (torirs_maped_accounts.h) */
    TORIRSMAPED_REQ_DOC_OPEN = 17,
    TORIRSMAPED_REQ_DOC_CLOSE = 18,
    TORIRSMAPED_REQ_SAVE = 19,
    TORIRSMAPED_REQ_SAVE_AS = 20,
    TORIRSMAPED_REQ_DOC_CREATE = 21,

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

    /* v2 facts, reserved by owner:
     *   139..160  foundations (below; 140..160 free for this owner)
     *   161..168  catalog      FACT_CATALOG_PAGE 161, FACT_CATALOG_RECORD 162,
     *                          FACT_CATALOG_SQUARES 163
     *   169..172  views
     *   173..190  live-editor  FACT_BUILD_LINE 173, FACT_BUILD_DONE 174,
     *                          FACT_LIVE_STATUS 175, FACT_LIVE_LINE 176,
     *                          FACT_LIVE_VARS 177
     *   191..200  accounts     FACT_ACCOUNTS 191, FACT_ACCOUNT 192,
     *                          FACT_ACCOUNT_STATUS 193, FACT_QUESTS 194
     *                          (torirs_maped_accounts.h) */
    /** A document's current text: the answer to DOC_OPEN. */
    TORIRSMAPED_FACT_DOC_TEXT = 139,
};

/** FACT_HELLO v2's capability bits: the optional bindings this server has. */
enum ToriRSMapEdCapability
{
    TORIRSMAPED_CAP_BUILD = 1,
    TORIRSMAPED_CAP_LIVE = 2,
    TORIRSMAPED_CAP_CATALOG = 4,
    TORIRSMAPED_CAP_SPAWN_VIEW = 8,
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

    /* v2 keys, reserved by owner (MAPED_IMPLEMENTATION.md section 3):
     *   6..9    foundations
     *   10..39  views        STATE_CAMERA_BASE 10 + connection index,
     *                        STATE_VIEW_SELF 39 (torirs_maped_view.h)
     *   40..47  live-editor  STATE_LIVE_FOCUS 40 */
    /** The var values a preview resolves multilocs/multinpcs against. */
    TORIRSMAPED_STATE_VAR_PREVIEW = 6,
    /** The editing tools every window of a Client shares beside the tool
     *  itself (editor_ui.c ui_tools_pack): brush radius, falloff, step,
     *  strength; underlay id, overlay id, overlay none, overlay shape,
     *  overlay rotation; flags value; place shape, place rotation; view
     *  level, view solo; Fit walls to neighbours (workbench 6.3) --
     *  TORIRSMAPED_TOOLS_VALUES in that order. */
    TORIRSMAPED_STATE_TOOLS = 7,
    /** A window with no world of its own (a popped-out Squares panel) asks
     *  the windows that show one to open a square: map_x, map_z, add (1
     *  keeps what is open), the picker's storage index, and a serial so the
     *  same square asked twice is two requests (editor_ui.c
     *  Editor_UiOpenSquare / Editor_UiApplyState). */
    TORIRSMAPED_STATE_OPEN_SQUARE = 8,
    /** A popped-out window's Dock button (workbench 6.0b, design #workbench
     *  "Every panel pops out"): the registry index of the module it showed
     *  (EditorUi::panels order, which every window of one build registers
     *  alike), a serial, so docking the same panel twice is two requests,
     *  and where it was scrolled: the panel's scroll in logical px and its
     *  first list's first row (-1 none), which the docked panel takes.
     *  The window that shows the panel set opens that panel again exactly as
     *  View > <panel> does; the popped-out window then closes (editor_ui.c
     *  Editor_UiDockBack / Editor_UiApplyState). */
    TORIRSMAPED_STATE_DOCK_PANEL = 9,
};

enum
{
    TORIRSMAPED_TOOLS_VALUES = 15,
};

enum
{
    /** Values one state field can carry. Sized for records, not arrays. */
    TORIRSMAPED_STATE_VALUES_MAX = 16,
    /** Distinct keys the store holds. */
    TORIRSMAPED_STATE_KEYS_MAX = 64,
};

/** The v1 command form: a square TILE/LOC with every field as a u32/i32,
 *  both halves always present, 144 bytes. Spoken only to v1 sessions. */
enum
{
    TORIRSMAPED_CMD_WIRE_V1 = 144,
};

/** Returns 0 when the command has no v1 form (anything but a square TILE or
 *  LOC); a v1 session never hears those. */
int
ToriRSMapEd_CmdEncodeV1(
    const struct Editor_Cmd* command,
    uint8_t out[TORIRSMAPED_CMD_WIRE_V1]);

/** Returns 1 on success; 0 means the bytes are not a v1 command. */
int
ToriRSMapEd_CmdDecodeV1(
    const uint8_t* body,
    uint32_t length,
    struct Editor_Cmd* out_command);

/**
 * The v2 command form (MAPED_IMPLEMENTATION.md section 2):
 *
 *     u32 doc_kind, u32 key_len, key, u32 cmd_kind, u32 has_before,
 *     u32 has_after, u32 payload_len, payload
 *
 * every int little-endian, every string `u32 len, bytes`. Appends to `out`.
 */
void
ToriRSMapEd_CmdEncode(
    const struct Editor_Cmd* command,
    struct ToriRSMapEdBuf* out);

enum ToriRSMapEdCmdDecodeResult
{
    TORIRSMAPED_CMD_DECODE_OK = 0,
    /** The bytes are not a command: a framing-level error. */
    TORIRSMAPED_CMD_DECODE_MALFORMED,
    /** A well-framed command of a kind this build does not know. doc_kind
     *  and doc_key are filled; nothing else is. A mirror applies nothing and
     *  re-opens the document. */
    TORIRSMAPED_CMD_DECODE_UNKNOWN_KIND,
};

/** Decode exactly `length` bytes. */
enum ToriRSMapEdCmdDecodeResult
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

/* ---- variable-length payloads ----------------------------------------- */

/** Append a u32 to a growable payload. */
static inline void
ToriRSMapEd_PutU32(
    struct ToriRSMapEdBuf* out,
    uint32_t value)
{
    uint8_t bytes[4];
    ToriRSMapEd_WriteU32(bytes, value);
    ToriRSMapEd_BufWrite(out, bytes, 4);
}

/** Append `u32 length, bytes`. */
static inline void
ToriRSMapEd_PutBytes(
    struct ToriRSMapEdBuf* out,
    const void* data,
    uint32_t length)
{
    ToriRSMapEd_PutU32(out, length);
    if( length > 0 )
        ToriRSMapEd_BufWrite(out, (const uint8_t*)data, (int)length);
}

static inline void
ToriRSMapEd_PutString(
    struct ToriRSMapEdBuf* out,
    const char* text)
{
    ToriRSMapEd_PutBytes(out, text, (uint32_t)strlen(text));
}

/**
 * A bounds-checked cursor over one payload. Every Get on an exhausted or
 * malformed payload sets `bad` and returns zero/empty, so a handler reads
 * every field and checks once — a short frame from a remote peer is data,
 * never a crash.
 */
struct ToriRSMapEdReader
{
    const uint8_t* data;
    uint32_t length;
    uint32_t at;
    int bad;
};

static inline void
ToriRSMapEd_ReaderInit(
    struct ToriRSMapEdReader* reader,
    const uint8_t* data,
    uint32_t length)
{
    reader->data = data;
    reader->length = length;
    reader->at = 0;
    reader->bad = 0;
}

static inline uint32_t
ToriRSMapEd_GetU32(struct ToriRSMapEdReader* reader)
{
    uint32_t value;
    if( reader->bad || reader->length - reader->at < 4 )
    {
        reader->bad = 1;
        return 0;
    }
    value = ToriRSMapEd_ReadU32(reader->data + reader->at);
    reader->at += 4;
    return value;
}

/** `u32 length, bytes` as a pointer into the payload; NULL (and `bad`) when
 *  it overruns. */
static inline const uint8_t*
ToriRSMapEd_GetBytes(
    struct ToriRSMapEdReader* reader,
    uint32_t* out_length)
{
    uint32_t length = ToriRSMapEd_GetU32(reader);
    const uint8_t* bytes;
    *out_length = 0;
    if( reader->bad || reader->length - reader->at < length )
    {
        reader->bad = 1;
        return NULL;
    }
    bytes = reader->data + reader->at;
    reader->at += length;
    *out_length = length;
    return bytes;
}

/** A string into `out` (NUL-terminated). Too long for `out`, or carrying a
 *  NUL, is malformed. */
static inline void
ToriRSMapEd_GetString(
    struct ToriRSMapEdReader* reader,
    char* out,
    size_t out_size)
{
    uint32_t length;
    const uint8_t* bytes = ToriRSMapEd_GetBytes(reader, &length);
    out[0] = '\0';
    if( !bytes )
        return;
    if( length >= out_size || memchr(bytes, '\0', length) )
    {
        reader->bad = 1;
        return;
    }
    memcpy(out, bytes, length);
    out[length] = '\0';
}

/** 1 when every byte was read and nothing overran. */
static inline int
ToriRSMapEd_ReaderDone(const struct ToriRSMapEdReader* reader)
{
    return !reader->bad && reader->at == reader->length;
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
struct ToriRSMapEdCatalog;
struct ToriRSMapEdJobs;

/**
 * The SPAWN_VIEW binding (torirs_maped_view.h): how this server's host starts
 * a viewer process joined to a Client. Optional — a server without one
 * answers SPAWN_VIEW UNSUPPORTED and leaves TORIRSMAPED_CAP_SPAWN_VIEW clear.
 * Set with ToriRSMapEd_SetViewSpawner; the daemon binds it to the launch
 * service (platform/launch_sessions.h `view`).
 */
struct ToriRSMapEdViewSpawner
{
    void* ctx;
    /** Start one viewer joined to Client `group`, its camera at `camera`
     *  (TORIRSMAPED_CAMERA_* layout, absolute fine units). OK and *out_pid,
     *  or why not. */
    enum EditorHost_Status (*spawn)(
        void* ctx,
        uint32_t group,
        const int32_t* camera,
        int* out_pid);
};

/** Sessions one server can hold at once: a viewer, a couple of control
 *  clients, and slack. Bounds the broadcast fan-out, not the world. */
enum
{
    TORIRSMAPED_SESSION_MAX = 8,
};

/**
 * One content tree being served, and the authoritative editing state over it.
 *
 * NEVER a stack local: the document is ~8 MB of squares. Hosts hold one on the heap (the embed does) or in static storage
 * (the daemon does).
 *
 * The file layer is the same EditorHost local binding a desktop session used
 * to call directly — the server owns it now, and the wire is the only way in.
 */
struct ToriRSMapEd
{
    struct EditorHost files;
    int open;
    /** The tree `files` serves; SAVE_AS reads its allocation layers. */
    char content_dir[1024];
    /** The repo root Open was given ("" when none): builds run from it, the
     *  lanes live under it. */
    char repo_root[1024];
    /** This server's lane (torirs_maped_lanes.h): the one OpenLane resolved,
     *  else the main lane of `content_dir` itself. */
    struct ToriRSMapEdLaneDef lane;
    int lane_bound;

    /** The document. Clients hold mirrors of it; this is the original. */
    struct Editor_Doc doc;
    struct Editor_UndoStack undo;
    /** 0 when another server holds the tree's lock: serve, but never save. */
    int writable;
    /** When not writable, who holds the tree (Editor_HostLocalLockHolder):
     *  "<content_dir> is held by pid N (<command>, since HH:MM)". Sent after
     *  FACT_HELLO's fields and named in RUN's read-only stop. */
    char read_only_reason[1024];
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

    /** TORIRSMAPED_CAP_* for the bindings this server has; FACT_HELLO v2
     *  reports it. The catalog is bound; spawn_view when a host binds it
     *  (ToriRSMapEd_SetViewSpawner); build and live when a host binds a
     *  BuildRunner / LiveLink (torirs_maped_build.h, torirs_maped_live.h). */
    uint32_t capabilities;
    /** The asset catalog and square atlas (torirs_maped_catalog.h); built on
     *  its first query. */
    struct ToriRSMapEdCatalog* catalog;
    /** SPAWN_VIEW's binding; spawn NULL when this host cannot start viewers
     *  (torirs_maped_view.h). */
    struct ToriRSMapEdViewSpawner view_spawner;
    /** BUILD, RUN and the live link: the BuildRunner and LiveLink bindings,
     *  the lane, the build in flight, RUN's step, what the next reload names
     *  (torirs_maped_build.h, torirs_maped_live.h). Created at Open; the
     *  bindings stay empty until a host sets them (capabilities build/live). */
    struct ToriRSMapEdJobs* jobs;
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

/**
 * Open on a lane (torirs_maped_lanes.h): `lane_name` "main" serves
 * `base_tree` itself; any other name serves <repo>/build/lanes/<name>/content,
 * a mirror of `base_tree` made now when it does not exist, and binds the
 * lane's own live dir for BUILD and RUN. 0 with `why` when the lane cannot
 * be made (then nothing is open).
 */
int
ToriRSMapEd_OpenLane(
    struct ToriRSMapEd* maped,
    const char* base_tree,
    const char* repo_root,
    const char* lane_name,
    char* why,
    size_t why_size);

void
ToriRSMapEd_Close(struct ToriRSMapEd* maped);

/* ------------------------------------------------------------------ */
/* One connection                                                      */
/* ------------------------------------------------------------------ */

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
    /** The protocol this peer speaks: TORIRSMAPED_PROTO_VERSION_V1 or
     *  TORIRSMAPED_PROTO_VERSION, from HELLO. Facts are encoded per peer. */
    uint32_t version;
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
