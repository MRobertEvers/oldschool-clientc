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
    /** The server's catalog index is still being built (its warm build runs
     *  on a thread for the first seconds of a session): ask again shortly.
     *  Never a refusal -- the same request answers once the build is done. */
    EDITOR_HOST_BUSY,
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
     *           -- or, for an overlay key "<type>:<name>@<overlay>", out of
     *           `server/scripts/<overlay>` (editor_cfg.h) -- from its header
     *           to the next header; ABSENT when the record does not exist.
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
     * (or the overlay file an "@<overlay>" key names; appends it when new)
     * and leaves every other byte alone. Local binding
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

/* ---- open in the user's editor ------------------------------------------- */

/**
 * Hand `path` (absolute, on this machine) and its 1-based `line` to the
 * user's own editor: TORIRS_EDITOR_OPEN_CMD (`{path}` / `{line}` placeholders,
 * editor_open_cmd.h), else `code --goto {path}:{line}` when `code` is on
 * PATH, else `open {path}` (macOS) / `xdg-open {path}`. Spawned as
 * `/bin/sh -c '( <cmd> ) >/dev/null 2>&1 &'` and that shell reaped at once: the
 * editor outlives it and the UI thread never waits on the editor. The
 * command line goes to `out_cmdline` (may be NULL) for the Console.
 * On Windows the line is cmd.exe's (`cmd.exe /d /c <cmd>`, no console, not
 * waited on): VS Code when `code.cmd` is on PATH, else `start "" {path}`,
 * the program the file type is given to.
 * OK when the shell ran; UNSUPPORTED on the web (show the path instead);
 * REFUSED for a command that does not fit; IO_ERROR when the shell could
 * not be started.
 */
enum EditorHost_Status
Editor_HostLocalOpenInEditor(
    const char* path,
    int line,
    char* out_cmdline,
    size_t out_size);

/**
 * Editor_HostLocalOpenInEditor with the FOLDER the work is in: `folder`
 * opened as the editor's workspace and `path` (a file in it) at `line` --
 * a new quest's folder with its script in view. The command is
 * TORIRS_EDITOR_OPEN_FOLDER_CMD (`{folder}`, `{path}`, `{line}`), else
 * TORIRS_EDITOR_OPEN_CMD (the user's own editor command: the file alone,
 * unless it names `{folder}`), else `code {folder} --goto {path}:{line}`
 * when VS Code is on PATH, else the desktop's opener on the folder (its file
 * manager). `folder` NULL is Editor_HostLocalOpenInEditor.
 */
enum EditorHost_Status
Editor_HostLocalOpenFolderInEditor(
    const char* folder,
    const char* path,
    int line,
    char* out_cmdline,
    size_t out_size);

/** What the two calls above start on this machine, in words for a status
 *  line ("VS Code", "your TORIRS_EDITOR_OPEN_CMD command", "the program
 *  Windows opens that file type with"; with `folder`, "Explorer" when there
 *  is neither). A static string. */
const char*
Editor_HostLocalEditorWho(int folder);

/** 1 when Open in editor has an EDITOR to start: TORIRS_EDITOR_OPEN_CMD is
 *  set, or VS Code is on PATH. 0: it falls back to the desktop's opener,
 *  which for a `.rs2` is whatever the file type is given to -- often
 *  nothing -- and the caller says so, naming TORIRS_EDITOR_OPEN_CMD. */
int
Editor_HostLocalEditorFound(void);

/**
 * Show `path` (absolute, on this machine) in the desktop's file manager with
 * the file selected: Explorer on Windows (`explorer.exe /select,<path>`),
 * Finder on macOS (`open -R <path>`), the containing directory through
 * `xdg-open` elsewhere. A path that does not exist yet reveals its nearest
 * existing parent directory. Scripts are written in the user's own editor
 * (owner, 2026-11-09); this is how they get from a record to its files.
 * Spawned and never waited on, like Editor_HostLocalOpenInEditor; the command
 * line goes to `out_cmdline` (may be NULL) for the Console. OK when the
 * command was started; UNSUPPORTED on the web; REFUSED for a path that does
 * not fit or of which nothing exists (a missing drive); IO_ERROR when it
 * could not be started. A folder is opened, not selected. The command is
 * TORIRS_EDITOR_REVEAL_CMD when set (`{path}`: the file when it is on disk,
 * else its nearest folder), else editor_open_cmd.h's
 * Editor_RevealCommandDefault / Editor_RevealCommandDefaultWindows.
 */
enum EditorHost_Status
Editor_HostLocalRevealInFileManager(
    const char* path,
    char* out_cmdline,
    size_t out_size);

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

/**
 * Who holds the content tree, after a `session(acquire)` on a LOCAL binding
 * answered EDITOR_HOST_LOCKED: "<content_dir> is held by pid 43893 (torirs
 * --manifest ..., since 16:22)" -- the pid, command line and start the
 * lockfile records (or, for a lockfile that predates them, the executable
 * the pid runs). "" when the last acquire took the lock. A lockfile whose
 * pid no longer runs is never reported: the acquire removes it and takes
 * the tree. `host` must be a local binding.
 */
const char*
Editor_HostLocalLockHolder(const struct EditorHost* host);

/**
 * A LOCAL binding's write guard: asked with the content-relative path
 * ('/'-separated, "maps/m50_50.jm2") of every file the binding is about to
 * create, replace or remove, before the mkdirs above it, the save's
 * temporary and its rename, or the delete. 1 lets the write go on; 0 fails it
 * -- `why` (one sentence) goes to the log and the save answers
 * EDITOR_HOST_IO_ERROR, as any failed write does.
 *
 * An editor lane's server sets ToriRSMapEdLanes_MakeWritable here
 * (ToriRSMapEd_OpenLane): a directory of a lane may still be a LINK to the
 * base tree, and a write that reaches one lands in the base. With a guard
 * set, the binding also checks, right before it opens a temporary or removes
 * a file, that no directory between the content root and the target is a
 * link: a write the guard missed stops there (an assert; a refused write in
 * a build without asserts) instead of landing in the base.
 *
 * No guard (the default: the main tree, a lane-less session) writes as
 * before. The session lockfile at the content root is not guarded: a lane's
 * root is always a real directory. `host` must be a local binding; `guard`
 * must not be NULL.
 */
typedef int (*Editor_HostWriteGuardFn)(
    void* ctx,
    const char* rel,
    char* why,
    size_t why_size);

void
Editor_HostLocalSetWriteGuard(
    struct EditorHost* host,
    Editor_HostWriteGuardFn guard,
    void* ctx);

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

/** Editor_HostOpenMapEdEmbed on an editor lane (torirsmaped/
 *  torirs_maped_lanes.h): `lane` NULL, "" or "main" is the tree itself. */
int
Editor_HostOpenMapEdEmbedLane(
    struct EditorHost* host,
    const char* content_dir,
    const char* repo_root,
    const char* lane);

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

struct ToriRSMapEd;

/**
 * The server an embedded MapEd host runs in THIS process (server=embed, the
 * host Editor_HostOpenMapEdEmbed opened or a peer of it), or NULL for a
 * connection to a daemon. What the client's on-demand loopback listener
 * serves (torirsmaped/torirs_maped_net.h), so a second process can join an
 * embedded session. Asserts the host is a MapEd binding.
 */
struct ToriRSMapEd*
Editor_HostMapEdEmbedServer(const struct EditorHost* host);

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

/** Why the server cannot save, when it said (FACT_HELLO's trailing string):
 *  "<content_dir> is held by pid N (<command>, since HH:MM)". "" when the
 *  server is writable or did not say. */
const char*
Editor_HostMapEdReadOnlyReason(const struct EditorHost* host);

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
    /** Level 0 at `fill_height` (0: no `h` token, the perlin ground), underlay
     *  `fill_underlay`. */
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
    /** 1: the fill joins the stroke this client has open instead of being
     *  its own undo step (the instance helper's squares, spawns and files
     *  as one step). Only to a server with TORIRSMAPED_CAP_DOC_CREATE_STROKE;
     *  outside a stroke it changes nothing. */
    int join_stroke;
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

/**
 * SAVE_AS: copy a document under a new key in the authority (design #save
 * "Save as new"). Today a CFG record of a loc, npc or obj type: "loc:<name>"
 * to "loc:<new name>", with a fresh id from the namespace's .alloc ledger,
 * its pack/<type>.client line and every field of the source, broadcast as
 * one undo step (editor_alloc.h Editor_AllocCopyPlan). `out_id` (may be
 * NULL) gets the new id. REFUSED when the new name is taken or not a name,
 * ABSENT when the source record is not there, UNSUPPORTED for other kinds.
 * Nothing is written until Save.
 */
enum EditorHost_Status
Editor_HostMapEdSaveAs(
    struct EditorHost* host,
    enum Editor_DocKind kind,
    const char* key,
    const char* new_key,
    int* out_id);

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

/** SPAWN_VIEW (torirsmaped/torirs_maped_view.h): ask the SERVER's host to
 *  start a viewer joined to this connection's Client, looking at `camera`
 *  (TORIRSMAPED_CAMERA_VALUES, absolute fine units). OK and *out_pid;
 *  UNSUPPORTED when the server has no binding (capability bit clear);
 *  REFUSED for a pose out of range. */
enum EditorHost_Status
Editor_HostMapEdSpawnView(
    struct EditorHost* host,
    const int32_t* camera,
    int* out_pid);

/* ---- the catalog (design #catalog, #squares) ------------------------------
 *
 * Read-only questions about the content tree, answered by the server's index
 * (torirsmaped/torirs_maped_catalog.h has the kinds, flags and the wire).
 * Each answer is one allocation the caller frees; every string in it is
 * NUL-terminated and lives as long as the answer does.
 */

/** One `key=value` summary field. */
struct EditorHost_CatalogField
{
    const char* key;
    const char* value;
};

struct EditorHost_CatalogRow
{
    /** enum ToriRSMapEdCatalogKind. */
    uint32_t kind;
    /** -1 when the entity has no id (a server-only overlay, a trigger). */
    int32_t id;
    /** TORIRSMAPED_PROV_* bits: every source that said something. */
    uint32_t provenance;
    const char* name;
    /** The `name=` field; "" when there is none. */
    const char* display;
    int field_count;
    const struct EditorHost_CatalogField* fields;
};

struct EditorHost_CatalogPage
{
    /** Matches across every page. */
    uint32_t total;
    uint32_t page;
    uint32_t page_size;
    int row_count;
    struct EditorHost_CatalogRow* rows;
    void* storage;
    /** TORIRSMAPED_CATALOG_PAGE_* (PARTIAL: answered from the warm build's
     *  partial index; SCOPED: an "all" query stopped early, so `total` is
     *  only what was found) and the build's progress in KiB, done of
     *  `progress_total` (0 of 0 while the tree is scanned, 1 of 1 once
     *  built). All 0 for a v2 answer: a query without
     *  TORIRSMAPED_CATALOG_PROGRESS, or a server without
     *  TORIRSMAPED_CAP_CATALOG_PROGRESS. */
    uint32_t page_flags;
    uint32_t progress_done;
    uint32_t progress_total;
};

/**
 * CATALOG_QUERY: one page of the records of `kind` (TORIRSMAPED_CATALOG_ANY
 * for every kind) matching `filter` — free terms against name, display name
 * and id, plus `key=value` filters (`op=Open`, `multivar=varb4153`). `flags`
 * TORIRSMAPED_CATALOG_EXACT makes the text one whole name;
 * TORIRSMAPED_CATALOG_PROGRESS asks for an answer that never waits for the
 * server's warm build (page_flags / progress_* above) and is dropped, the
 * answer then a v2 one, when the server lacks
 * TORIRSMAPED_CAP_CATALOG_PROGRESS. `page_size` 0 is the server's default. REFUSED for an unknown kind; UNSUPPORTED when the
 * server has no catalog (capability bit clear).
 */
enum EditorHost_Status
Editor_HostMapEdCatalogQuery(
    struct EditorHost* host,
    uint32_t kind,
    const char* filter,
    uint32_t flags,
    uint32_t page,
    uint32_t page_size,
    struct EditorHost_CatalogPage* out_page);

/** NULL-tolerant. */
void
Editor_HostCatalogPageFree(struct EditorHost_CatalogPage* page);

struct EditorHost_CatalogSource
{
    /** Relative to the content root. */
    const char* path;
    /** The one TORIRSMAPED_PROV_* bit this source contributed as. */
    uint32_t provenance;
    /** 1-based line of the record in that file (its block header). */
    uint32_t line;
};

struct EditorHost_CatalogRecordField
{
    const char* key;
    const char* value;
    /** Index into `sources`, or -1 for a field only the index derives
     *  (`dialogue`, `users`, a square's `area`). */
    int source;
    /** When a later source (a server overlay) restated a field: the value
     *  it replaced and where that came from; NULL / -1 otherwise. */
    const char* overridden_value;
    int overridden_source;
};

struct EditorHost_CatalogRecord
{
    uint32_t kind;
    int32_t id;
    uint32_t provenance;
    const char* name;
    const char* display;
    int source_count;
    struct EditorHost_CatalogSource* sources;
    int field_count;
    struct EditorHost_CatalogRecordField* fields;
    void* storage;
};

/**
 * CATALOG_RECORD: the merged record — the cache block, every server overlay
 * over it, ids, membership — with the provenance of each field. Addressed
 * by `name` when non-empty, else by `id`. ABSENT when there is no such
 * record.
 */
enum EditorHost_Status
Editor_HostMapEdCatalogRecord(
    struct EditorHost* host,
    uint32_t kind,
    int32_t id,
    const char* name,
    struct EditorHost_CatalogRecord* out_record);

/** NULL-tolerant. */
void
Editor_HostCatalogRecordFree(struct EditorHost_CatalogRecord* record);

/** One FACT_CATALOG_USES row (torirs_maped_catalog.h): TORIRSMAPED_USE_*. */
struct EditorHost_CatalogUse
{
    uint32_t use;
    /** Relative to the content root. */
    const char* path;
    uint32_t line;
    uint32_t count;
    const char* text;
};

struct EditorHost_CatalogUses
{
    uint32_t kind;
    /** 0: the search is done; else ask again from here. */
    uint32_t next_cursor;
    uint32_t files_done;
    uint32_t files_total;
    int row_count;
    struct EditorHost_CatalogUse* rows;
    void* storage;
};

/**
 * CATALOG_USES (workbench 6.6): one slice of the search for where record
 * `name` of `kind` is named, from `cursor` (0 to start). The server reads
 * for a few milliseconds and answers with the cursor to go on from, so the
 * caller asks again on a later frame until `next_cursor` is 0. ABSENT when
 * the index has no such record (palettes and stamps are still read);
 * UNSUPPORTED without TORIRSMAPED_CAP_CATALOG_USES.
 */
enum EditorHost_Status
Editor_HostMapEdCatalogUses(
    struct EditorHost* host,
    uint32_t kind,
    const char* name,
    uint32_t cursor,
    struct EditorHost_CatalogUses* out_uses);

/** NULL-tolerant. */
void
Editor_HostCatalogUsesFree(struct EditorHost_CatalogUses* uses);

/** A world-map area: the surface, a dungeon map, an instance map. */
struct EditorHost_CatalogArea
{
    /** The `.wma` block name ("main", "ancient_cavern"). */
    const char* internal_name;
    /** What the world map calls it ("Gielinor Surface"). */
    const char* display_name;
    int is_main;
};

struct EditorHost_CatalogSquare
{
    int map_x;
    int map_z;
    /** Index into the atlas's `storages`. */
    int storage;
    /** TORIRSMAPED_SQUARE_* bits. */
    uint32_t flags;
    int spawn_count;
    /** The locs its .jl2 places on disk; 0 from a server without
     *  TORIRSMAPED_CAP_CATALOG_SQUARE_LOCS (the row did not say). */
    int loc_count;
    /** 0xRRGGBB averaged over the level-0 floors; 0 when it has none. */
    uint32_t rgb;
    /** Index into `areas` of the most specific claiming area, or -1 for an
     *  unmapped square (the picker's Unmapped tab). */
    int primary_area;
    /** Every area that claims it, most specific first. */
    int area_count;
    const int* areas;
};

struct EditorHost_CatalogSquares
{
    /** "maps" first, then "ported/<lane>/maps" for every lane directory —
     *  what the New dialog offers as storage. */
    int storage_count;
    const char* const* storages;
    int area_count;
    const struct EditorHost_CatalogArea* areas;
    int square_count;
    const struct EditorHost_CatalogSquare* squares;
    void* storage_mem;
};

/** CATALOG_SQUARES: the square atlas the picker draws. Waits for the
 *  server's index when it is still building. */
enum EditorHost_Status
Editor_HostMapEdCatalogSquares(
    struct EditorHost* host,
    struct EditorHost_CatalogSquares* out_atlas);

/** The same with ToriRSMapEdCatalogSquaresFlag bits:
 *  TORIRSMAPED_CATALOG_SQUARES_NOWAIT answers EDITOR_HOST_BUSY (an empty
 *  atlas) while the index builds on the server's worker thread, for a caller
 *  that asks again every tick (dropped for a server without
 *  TORIRSMAPED_CAP_CATALOG_SQUARES_NOWAIT, which then waits). The loc
 *  counts are asked for whenever the server offers them. */
enum EditorHost_Status
Editor_HostMapEdCatalogSquaresFlags(
    struct EditorHost* host,
    uint32_t flags,
    struct EditorHost_CatalogSquares* out_atlas);

/**
 * The atlas changed under every window — a DOC_CREATE (the new square's row
 * arrives flagged TORIRSMAPED_SQUARE_CREATED before anything is on disk), a
 * save that wrote squares. The server broadcasts the whole atlas; DrainFacts
 * keeps the newest one here instead of handing it to the fact sink (whose
 * callers fill their sinks field by field). Returns 1 and fills `out_atlas`
 * (free it) when one arrived since the last call, else 0. Poll it after
 * Editor_HostMapEdDrainFacts.
 */
int
Editor_HostMapEdCatalogSquaresChanged(
    struct EditorHost* host,
    struct EditorHost_CatalogSquares* out_atlas);

/** NULL-tolerant. */
void
Editor_HostCatalogSquaresFree(struct EditorHost_CatalogSquares* atlas);

/* ---- BUILD, RUN and the live link -------------------------------------------
 *
 * torirsmaped/torirs_maped_build.h and torirs_maped_live.h define the frames;
 * editor/live/live_client.h encodes the requests and decodes the facts. The
 * host only carries them, so a new live frame needs no change here.
 */

/** Send one live-editor request (BUILD, RUN, LIVE_*: types 53..64) with its
 *  payload, fire and forget -- every answer is a broadcast fact. 1 when sent,
 *  0 when the connection is gone. */
int
Editor_HostMapEdLiveSend(
    struct EditorHost* host,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length);

/** The oldest live-editor fact (types 173..190) Editor_HostMapEdDrainFacts
 *  kept, oldest first; 0 when none is waiting. *out_body is ToriMem_Editor's
 *  (ToriMem_Free it). Poll after DrainFacts. */
int
Editor_HostMapEdLiveTake(
    struct EditorHost* host,
    uint32_t* out_type,
    uint8_t** out_body,
    uint32_t* out_length);

/* ---- accounts (torirsmaped/torirs_maped_accounts.h) ---------------------------
 *
 * Carried like the live frames: requests 65..80 fire and forget, facts
 * 191..200 kept for the Accounts panel's poll; editor/accounts/
 * accounts_client.h encodes and decodes them. */

int
Editor_HostMapEdAccountsSend(
    struct EditorHost* host,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length);

/** The oldest accounts fact DrainFacts kept; 0 when none. *out_body is
 *  ToriMem_Editor's (ToriMem_Free it). */
int
Editor_HostMapEdAccountsTake(
    struct EditorHost* host,
    uint32_t* out_type,
    uint8_t** out_body,
    uint32_t* out_length);

/* ---- editor lanes (torirsmaped/torirs_maped_lanes.h) --------------------------
 *
 * Carried like the accounts frames: requests 81..90 fire and forget, facts
 * 201..210 kept for the Publish panel's poll. */

int
Editor_HostMapEdLanesSend(
    struct EditorHost* host,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length);

/** The oldest lanes fact DrainFacts kept; 0 when none. *out_body is
 *  ToriMem_Editor's (ToriMem_Free it). */
int
Editor_HostMapEdLanesTake(
    struct EditorHost* host,
    uint32_t* out_type,
    uint8_t** out_body,
    uint32_t* out_length);

/**
 * LANE_HANDOUT (torirs_maped_lanes.h): `rel` -- a file, a directory or a
 * file yet to be made, relative to the content root -- is about to be given
 * to a program outside the editor (the user's text editor, the file manager,
 * the clipboard). The server makes it the lane's own first
 * (ToriRSMapEdLanes_HandOut: real directories above it, a real file in place
 * of a link to the base), so a save made in place out there stays in the
 * lane, and answers the path as its tree spells it: `out_path`, absolute
 * when the server's content root is. Waits for the answer.
 *
 * `folder_max` 0 hands out `rel` alone. Above 0, `rel` is a folder shown to
 * a program that shows its files too (a file manager, a workspace): every
 * file in it, every level down, is handed out with it, when there are at
 * most that many (ToriRSMapEdLanes_HandOutFolder); past that, OK with
 * `out_why` saying only the folder was.
 *
 * EDITOR_HOST_OK with the path; EDITOR_HOST_UNSUPPORTED (a server without
 * TORIRSMAPED_CAP_LANE_HANDOUT: nothing was sent, `out_path` is ""); any
 * other status with `out_why` saying why the path must NOT be handed out.
 */
enum EditorHost_Status
Editor_HostMapEdLaneHandOut(
    struct EditorHost* host,
    const char* rel,
    int folder_max,
    char* out_path,
    size_t path_size,
    char* out_why,
    size_t why_size);

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
