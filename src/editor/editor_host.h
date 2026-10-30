#ifndef SRC_EDITOR_EDITOR_HOST_H
#define SRC_EDITOR_EDITOR_HOST_H

/**
 * The EditorHost seam: everything the editor cannot do from inside a wasm
 * sandbox, and nothing else.
 *
 * The editor frontend — panels, tools, undo, the document, the text codec, the
 * world rebuilds — is one body of code that runs unchanged on the desktop and
 * in the browser. The only thing it cannot carry across is touching the
 * filesystem and running a bake, so exactly that goes behind this vtable, the
 * same way `NetTransport` puts the socket behind two function pointers and
 * lets the whole net stack stay ignorant of which one it got.
 *
 * Two bindings:
 *
 *   - editor_host_local.c   direct calls, same process. Today this is the
 *                           SERVER's file layer: ToriRSMapEd dispatches wire
 *                           frames onto it (torirsmaped/torirs_maped.c), and
 *                           its unit tests drive it directly.
 *   - editor_host_remote.c  the same operations as frames to ToriRSMapEd —
 *                           embedded in this process (`[editor:boot]
 *                           server=embed`) or the torirsmaped daemon over
 *                           TCP (`server=tcp`). The client half of the wire.
 *
 * The host trades in opaque bytes: parsing and emitting `.jm2` text stays in
 * the frontend, so the wire carries files, not tiles, and the host never needs
 * to know the map format. It also means the host is the only thing that builds
 * a path — the frontend addresses a square by (map_x, map_z) and cannot name a
 * file outside the content tree.
 */

#include "editor_cmd.h"
#include "mem/torirs_mem.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/** A file's bytes. NUL-terminated for the text the codec parses; `size`
 *  excludes that terminator. Freed with Editor_HostBlobFree. */
struct EditorHost_Blob
{
    char* data;
    size_t size;
};

/* Inline because both bindings free blobs and neither owns the concept: the
 * local binding fills them from files, the remote one from wire frames, and a
 * shared .c for four lines would be a unit with nothing else in it. */
static inline void
Editor_HostBlobFree(struct EditorHost_Blob* blob)
{
    if( !blob )
        return;

    ToriMem_Free(ToriMem_Editor(), blob->data);
    blob->data = NULL;
    blob->size = 0;
}

/** Progress line from a bake, without its newline. */
typedef void (*EditorHost_ProgressFn)(void* user_data, const char* line);

enum EditorHost_Status
{
    EDITOR_HOST_OK = 0,
    /** The square has no `.jm2` in the content tree. */
    EDITOR_HOST_ABSENT,
    /** Open/read/write/rename failed; the host logs the detail. */
    EDITOR_HOST_IO_ERROR,
    /** Another editor session holds the content tree. */
    EDITOR_HOST_LOCKED,
    /** The bake ran and exited non-zero. */
    EDITOR_HOST_BAKE_FAILED,
    /** The request is well-formed but not allowed: a DOC_CREATE whose key
     *  already exists in that storage, a key or storage that names nothing a
     *  document may address, an edit the document does not match. */
    EDITOR_HOST_REFUSED,
    /** This binding or server does not offer the operation (yet). */
    EDITOR_HOST_UNSUPPORTED,
};

struct EditorHost_VTable
{
    /**
     * Every square the content tree ships, as (map_x, map_z) pairs written
     * into `out_coords` (2 ints each). Writes at most `max` pairs and always
     * reports the true total, so a caller can size a buffer from a probe.
     */
    enum EditorHost_Status (*square_list)(
        void* user_data,
        int* out_coords,
        int max,
        int* out_count);

    /**
     * A square's two text files. Either may come back empty (`data` NULL) when
     * the tree has only one half, which is legal — a square with no scenery
     * ships no `.jl2`.
     */
    enum EditorHost_Status (*square_load)(
        void* user_data,
        int map_x,
        int map_z,
        struct EditorHost_Blob* out_jm2,
        struct EditorHost_Blob* out_jl2);

    /**
     * Write a square's two halves.
     *
     * Either text may be NULL to leave that half untouched, which is what
     * keeps a terrain edit from rewriting the square's `.jl2` and reordering
     * placements the user never touched.
     *
     * The write is atomic per file — a temporary beside the target, then a
     * rename — so an interrupted save leaves the old file, never a half one.
     */
    enum EditorHost_Status (*square_save)(
        void* user_data,
        int map_x,
        int map_z,
        const char* jm2_text,
        size_t jm2_length,
        const char* jl2_text,
        size_t jl2_length);

    /**
     * Write one square's EDITED spawn file -- the hand-authored lane beside
     * the generated one.
     *
     * `server/scripts/areas/edited/configs/m<x>_<z>.spawn`, never the
     * generated `areas/world` file: that one opens with "do not hand-edit,
     * the next run overwrites it", and writing into it would hand the next
     * regeneration our edits to destroy. The server loader recurses the whole
     * scripts tree and merges every .spawn it finds, so the second file needs
     * no registration anywhere. Atomic like square_save. NULL text with zero
     * length deletes the file (a square whose session spawns were all
     * removed).
     */
    enum EditorHost_Status (*spawn_save)(
        void* user_data,
        int map_x,
        int map_z,
        const char* text,
        size_t length);

    /**
     * Bake the content tree into the cache the game reads.
     *
     * **Only ever called from the editor's Bake action.** Saving writes text
     * and stops there; the bake is a separate thing the user asks for, because
     * it is slow, it rewrites a cache other processes may be reading, and the
     * editor itself never needs it — the editor loads from the text sources,
     * so it cannot go stale waiting for one.
     *
     * Runs the project's own bake target rather than assembling a cachepack
     * command line here. The makefile is the authority on how a bake is done
     * (base cache, asset flags, name tables), and a second copy of those flags
     * in C would drift out of agreement with the one the project actually uses.
     */
    enum EditorHost_Status (*bake)(
        void* user_data,
        EditorHost_ProgressFn on_progress,
        void* progress_user_data);

    /**
     * Take or release the single-writer lock on the content tree.
     *
     * `acquire` non-zero takes it. EDITOR_HOST_LOCKED means somebody else has
     * it and this session is read-only — which is a state the editor can
     * usefully run in (look, don't save), not a failure to start.
     */
    enum EditorHost_Status (*session)(void* user_data, int acquire);

    /** Release the binding's own resources. NULL-tolerant, like every other
     *  deallocator in this tree. */
    void (*free_)(void* user_data);

    /**
     * One part of a document's text, addressed by (kind, key, part) — never
     * by path. The binding resolves the path (Editor_HostDocPath), so a
     * caller cannot name a file outside the content root.
     *
     *   SQUARE  part 0 `.jm2`, 1 `.jl2`, 2 `.filepack`; the key may carry
     *           its storage ("ported/<lane>/maps/m16_101").
     *   SPAWN   part 0, the edited `.spawn`.
     *   CFG     part 0, the `[name]` block's text out of `configs/all.<type>`
     *           (from its header to the next header; ABSENT when the record
     *           does not exist).
     *   IFACE   part 0, `interfaces/<name>.if`.
     *   TEXT    part 0, the script.
     *
     * ABSENT when the file (or record) does not exist — data, not failure.
     * The remote binding answers from the AUTHORITY's document (DOC_OPEN).
     */
    enum EditorHost_Status (*doc_load)(
        void* user_data,
        enum Editor_DocKind kind,
        const char* key,
        int part,
        struct EditorHost_Blob* out_blob);

    /**
     * Write one part of a document, atomically, creating directories the
     * path needs. NULL text deletes the file (a CFG record: removes the
     * block). A CFG save replaces the record's block in `configs/all.<type>`
     * (appends it when new) and leaves every other byte alone. Local binding
     * only: the remote one answers UNSUPPORTED, because a client never writes
     * — the authority saves its own documents (Editor_HostMapEdSave).
     */
    enum EditorHost_Status (*doc_save)(
        void* user_data,
        enum Editor_DocKind kind,
        const char* key,
        int part,
        const char* text,
        size_t length);
};

/**
 * The path, relative to the content root, of one part of a document —
 * the ONE place a document key becomes a file name (see doc_load for the
 * table). Returns 0 when the key or part names nothing a document may
 * address; keys arrive off the wire, so that is an answer, not an assert.
 */
int
Editor_HostDocPath(
    enum Editor_DocKind kind,
    const char* key,
    int part,
    char* out,
    size_t out_size);

struct EditorHost
{
    const struct EditorHost_VTable* vtable;
    void* user_data;
};

/* ---- local binding ------------------------------------------------------- */

/**
 * The desktop binding: direct filesystem access, no daemon, no socket.
 *
 * @param content_dir  the revision's content root, the directory holding
 *                     `maps/` (e.g. `OSRS-Content/osrs-content`).
 * @param repo_root    where the bake runs from; NULL disables baking, which is
 *                     what a session that only wants to read should pass.
 */
int
Editor_HostOpenLocal(
    struct EditorHost* host,
    const char* content_dir,
    const char* repo_root);

/** Calls the binding's free_ and clears the host. NULL-tolerant.
 *
 * Inline for the same reason as Editor_HostBlobFree: it belongs to the SEAM,
 * not to either binding, and a caller that links only one binding must still
 * be able to close it. */
static inline void
Editor_HostClose(struct EditorHost* host)
{
    if( !host )
        return;

    if( host->vtable && host->vtable->free_ )
        host->vtable->free_(host->user_data);
    host->vtable = NULL;
    host->user_data = NULL;
}

/* ---- ToriRSMapEd bindings (editor_host_remote.c) ------------------------- */

/**
 * The embedded server: ToriRSMapEd started inside this process, the wire a
 * pair of in-memory queues. Owns the server; closing the host stops it.
 * Returns 0 when the handshake fails, which for an in-process wire means a
 * build defect rather than a deployment one.
 */
int
Editor_HostOpenMapEdEmbed(
    struct EditorHost* host,
    const char* content_dir,
    const char* repo_root);

/**
 * The torirsmaped daemon over TCP. `port` <= 0 means the default
 * (TORIRSMAPED_DEFAULT_PORT). Returns 0 when the daemon cannot be reached —
 * a legitimate runtime state the caller must handle, unlike a bad argument.
 *
 * `role` is enum ToriRSMapEdRole. `join_group` is the Client id to join — a
 * viewer's, when this connection is a controller for the session that viewer
 * already started — or 0 to be granted a fresh Client. The id the server
 * settled on is readable afterwards with Editor_HostMapEdClientId, which is
 * what a viewer prints for its controllers to pass back.
 */
int
Editor_HostOpenMapEdTcp(
    struct EditorHost* host,
    const char* server_host,
    int port,
    int role,
    uint32_t join_group);

/**
 * A second connection to the embedded server another MapEd host owns —
 * a process holding several connections is the design, not a trick. `role`
 * is enum ToriRSMapEdRole; `join_group` non-zero joins the peer's Client
 * (its state relay), zero makes this connection its own Client. The peer
 * host must outlive this one, which does not own the server.
 */
int
Editor_HostOpenMapEdEmbedPeer(
    struct EditorHost* host,
    const struct EditorHost* peer,
    int role,
    int join_group);

/* ---- the document and state layer ----------------------------------------
 *
 * Only a MapEd binding offers these — the authoritative document lives in
 * the server, so no local binding could. Every mutating call is an INTENT:
 * the mirror changes only when the server's broadcast comes back through
 * Editor_HostMapEdDrainFacts, the sender consuming its own echo exactly
 * like every other connection.
 */

/** 1 when this host is a ToriRSMapEd connection. The calls below assert it. */
int
Editor_HostIsMapEd(const struct EditorHost* host);

/** FACT_HELLO v2's capability bits (TORIRSMAPED_CAP_*): which optional
 *  bindings — build, live, catalog, spawn_view — the server has. */
uint32_t
Editor_HostMapEdCapabilities(const struct EditorHost* host);

/** What FACT_HELLO reported: can the server save its tree? */
int
Editor_HostMapEdWritable(const struct EditorHost* host);

/** The Client (session group) this connection was granted or joined. */
uint32_t
Editor_HostMapEdClientId(const struct EditorHost* host);

/**
 * Open a square in the AUTHORITATIVE document and receive its current text —
 * another Client's unsaved edits included — plus its dirty flags, so a
 * mirror joining late agrees about what is unsaved.
 */
enum EditorHost_Status
Editor_HostMapEdSquareOpen(
    struct EditorHost* host,
    int map_x,
    int map_z,
    struct EditorHost_Blob* out_jm2,
    struct EditorHost_Blob* out_jl2,
    int* out_dirty_map,
    int* out_dirty_loc);

/** A document's current text as the authority answers DOC_OPEN. */
struct EditorHost_DocText
{
    enum Editor_DocKind kind;
    /** The canonical key: a square's storage prefix moves to `storage`. */
    char key[EDITOR_DOC_KEY_MAX];
    /** Squares only: "" for the content tree's maps/, else the lane's. */
    char storage[EDITOR_DOC_STORAGE_MAX];
    /** Bit 0: the first part is unsaved (a square's terrain, any other
     *  document's text). Bit 1: the second part (a square's locs). */
    uint32_t dirty;
    /** 2 for a square (jm2, jl2), 1 otherwise. A part with NULL data is
     *  absent (no file yet), which differs from empty. */
    int part_count;
    struct EditorHost_Blob parts[2];
};

/** Frees the parts. NULL-tolerant. */
void
Editor_HostDocTextFree(struct EditorHost_DocText* text);

/**
 * Open (kind, key) in the AUTHORITATIVE document and receive its current
 * text — another Client's unsaved edits included. A SQUARE key may carry its
 * storage ("ported/<lane>/maps/m16_101"). The v1 SQUARE_OPEN alias below
 * answers the same text for a square.
 */
enum EditorHost_Status
Editor_HostMapEdDocOpen(
    struct EditorHost* host,
    enum Editor_DocKind kind,
    const char* key,
    struct EditorHost_DocText* out_text);

/** This connection no longer looks at (kind, key). */
enum EditorHost_Status
Editor_HostMapEdDocClose(
    struct EditorHost* host,
    enum Editor_DocKind kind,
    const char* key);

/** Save one document server-side. Returns 1 when it was written, 0 when it
 *  had nothing unsaved, -1 when the server is read-only or the save failed.
 *  FACT_SAVED tells every mirror what landed. */
int
Editor_HostMapEdSave(
    struct EditorHost* host,
    enum Editor_DocKind kind,
    const char* key);

/** What a NEW square starts as (design #squares). */
enum Editor_DocTemplate
{
    /** All-default tiles, no locs. */
    EDITOR_DOC_TEMPLATE_EMPTY = 0,
    /** A copy of `source_key`'s tiles and locs. */
    EDITOR_DOC_TEMPLATE_COPY,
    /** Level 0 authored flat at `fill_height`, underlay `fill_underlay`. */
    EDITOR_DOC_TEMPLATE_FLAT,
};

struct EditorHost_DocCreate
{
    /** SQUARE; the server refuses every other kind for now. */
    enum Editor_DocKind kind;
    /** "m<x>_<z>". */
    char key[EDITOR_DOC_KEY_MAX];
    /** "" (or "maps") for the content tree's maps/, "ported/<lane>/maps". */
    char storage[EDITOR_DOC_STORAGE_MAX];
    enum Editor_DocTemplate template_kind;
    /** TEMPLATE_COPY: the square to copy, which may carry its storage. */
    char source_key[EDITOR_DOC_KEY_MAX];
    int fill_height;
    int fill_underlay;
};

/**
 * DOC_CREATE: make a NEW document in the authority. For a square the server
 * refuses (EDITOR_HOST_REFUSED) when the key already exists in that storage
 * or is open from another one; otherwise it creates the square and its
 * `.filepack` line, broadcasts the commands that fill it from the template
 * (one undo step), and writes nothing until Save.
 */
enum EditorHost_Status
Editor_HostMapEdDocCreate(
    struct EditorHost* host,
    const struct EditorHost_DocCreate* request);

/** Submit one edit. OK means the authority applied it and the echo is on
 *  its way; ABSENT means the square was never opened server-side. */
enum EditorHost_Status
Editor_HostMapEdCmd(
    struct EditorHost* host,
    const struct Editor_Cmd* command);

/** Revert / re-apply the newest history group on the authority. Returns the
 *  command count; the commands themselves arrive as FACT_CMD echoes. */
int
Editor_HostMapEdUndo(struct EditorHost* host);

int
Editor_HostMapEdRedo(struct EditorHost* host);

/** Group the commands until the matching end into one undo step. */
enum EditorHost_Status
Editor_HostMapEdStroke(
    struct EditorHost* host,
    int begin);

/** Save every dirty square server-side. Returns the count, or -1 when the
 *  server is read-only. FACT_SAVED tells every mirror what landed. */
int
Editor_HostMapEdSaveAll(struct EditorHost* host);

/** Publish one shared-state field to this connection's Client. Fire and
 *  forget — the group's echo is the application. Returns 1 if sent. */
int
Editor_HostMapEdStateSet(
    struct EditorHost* host,
    uint32_t key,
    const int32_t* values,
    int count);

/** Replay the Client's whole state store (as stashed FACT_STATEs, delivered
 *  by the next DrainFacts). Returns the key count, or -1. */
int
Editor_HostMapEdStateSync(struct EditorHost* host);

/** Where broadcasts land. Any callback may be NULL to ignore that kind. */
struct EditorHostMapEdFacts
{
    void* user_data;
    /** A document mutation: apply `command` in `direction` to the mirror. */
    void (*on_cmd)(
        void* user_data,
        uint32_t seq,
        int direction,
        const struct Editor_Cmd* command);
    /** Documents written to disk, as `count` (kind, key) rows. */
    void (*on_saved)(void* user_data, int count, const struct Editor_DocRef* rows);
    /** A shared-state field changed somewhere in this connection's Client. */
    void (*on_state)(
        void* user_data,
        uint32_t key,
        const int32_t* values,
        int count);
    /** A FACT_CMD of a kind this build does not know arrived for (kind,
     *  key): nothing was applied, and the mirror must re-open that document
     *  to resync (MAPED_IMPLEMENTATION.md section 2). */
    void (*on_resync)(void* user_data, enum Editor_DocKind kind, const char* key);
};

/**
 * Deliver every pending broadcast: the ones stashed while requests awaited
 * their replies, then whatever the wire holds right now. A poll, never a
 * wait — call it once per frame, and after any intent whose echo the caller
 * wants applied before proceeding. Returns the number delivered.
 */
int
Editor_HostMapEdDrainFacts(
    struct EditorHost* host,
    const struct EditorHostMapEdFacts* sink);

#endif
