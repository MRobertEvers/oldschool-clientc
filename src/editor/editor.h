#ifndef SRC_EDITOR_EDITOR_H
#define SRC_EDITOR_EDITOR_H

/**
 * The editor session: one connection's MIRROR of the ToriRSMapEd server.
 *
 * The authoritative document, the undo history, the content tree and the
 * shared session state all live in the server; this session holds a mirror
 * document and the connection. Every edit path — interactive tools, replayed
 * input, undo, redo — goes through the same door (Editor_Apply and friends),
 * but the door now sends an INTENT: the mirror mutates only when the
 * server's broadcast comes back, in Editor_PumpFacts, exactly as it does for
 * every other connection. That is what keeps N clients agreeing about the
 * world: nobody applies their own edit, everybody applies the authority's
 * echo — and the dirty flags, rebuild queue and undo answers can never
 * disagree with a server they all came from.
 *
 * The session never talks to a GAME server. An editor boot's [net:boot]
 * phase does not run; the server here is the editor's own
 * (`[editor:boot] server=embed|tcp`).
 */

#include "editor_cmd.h"
#include "editor_doc.h"
#include "editor_host.h"
#include "editor_types.h"

#include <stdint.h>

struct CacheProvider;
struct RSCache;

/** Squares queued for a remesh in one frame. Bounded by the load path's own
 *  ceiling; a single edit dirties at most four. */
#define EDITOR_REBUILD_QUEUE_MAX 64

struct Editor
{
    /** The mirror. The authoritative document is the server's. */
    struct Editor_Doc doc;
    /** The connection — always a ToriRSMapEd binding. */
    struct EditorHost host;
    int host_open;

    /** 0 when the SERVER cannot save its tree (another server holds the
     *  lock). The editor still runs — looking at a tree somebody else is
     *  editing is useful — it just cannot save. */
    int writable;

    /** FACT_HELLO v2's TORIRSMAPED_CAP_* bits: which optional server
     *  bindings (build, live, catalog, spawn_view) exist. */
    uint32_t capabilities;

    /** Where shared-state facts land (the panel registers here). May be
     *  NULL; the session still mirrors nothing for state — semantics are
     *  the registrant's. */
    void (*on_state)(
        void* user_data,
        uint32_t key,
        const int32_t* values,
        int count);
    void* on_state_user_data;

    /** The newest document-fact seq this mirror applied, for the divergence
     *  check in mirror_on_cmd. 0 until the first one arrives. */
    uint32_t last_cmd_seq;

    /**
     * Squares whose meshes are stale, as (map_x, map_z) pairs.
     *
     * Coalesced rather than rebuilt per edit: a brush drag produces one
     * command per tile, and rebuilding a square for each of them would spend
     * the whole frame remeshing terrain nobody has seen yet. The queue is
     * drained once per frame instead, so a drag costs one rebuild per square
     * per frame however fast the mouse moves.
     */
    int rebuild_queue[EDITOR_REBUILD_QUEUE_MAX * 2];
    int rebuild_count;

    /** The undo groups as this mirror saw them (editor_cmd.h Editor_History):
     *  derived from the FACT_CMD stream and this session's own intents, for
     *  the History panel. The authority's stack is still the only one Undo
     *  and Redo act on. */
    struct Editor_History history;
    /** Inside an intent (Apply, Undo, Redo, a create): the facts its pump
     *  drains are this window's, not a batch from elsewhere. */
    int history_intent;
    /** Between StrokeBegin and StrokeEnd: the facts join one group. */
    int history_stroke;
};

/**
 * Open a session over an already-opened host — the general form.
 *
 * The host is how the session reaches its content tree, and since the
 * MapEditor became the ToriRSMapEd server it is normally one of the
 * editor_host_remote.c bindings: the embedded server or the torirsmaped
 * daemon. The session takes ownership; Editor_Close closes it.
 *
 * @param label    what to call the tree in messages ("OSRS-Content/…",
 *                 "maped://localhost:43610").
 * @param profile  cache identity, for the terrain codec's tile widths.
 *
 * Takes the content-tree lock if it can. Returns 1 always — a session that
 * could not lock is read-only, not a failure; check `writable`.
 */
int
Editor_OpenHost(
    struct Editor* editor,
    const struct EditorHost* host,
    const char* label,
    const struct RSCache* profile);

/**
 * Convenience form: an embedded ToriRSMapEd over a local content tree — the
 * whole client-server loop in one process, one call.
 *
 * @param content_dir  the revision content root holding `maps/`.
 * @param repo_root    where a bake would run; NULL disables baking.
 *
 * Returns 0 when the embedded server could not start.
 */
int
Editor_Open(
    struct Editor* editor,
    const char* content_dir,
    const char* repo_root,
    const struct RSCache* profile);

void
Editor_Close(struct Editor* editor);

/**
 * Load a square's text into the document and seed the provider with what it
 * derives to, so the next world load meshes the file rather than the bake.
 *
 * Seeding rather than converting in place is what keeps the editor off the
 * baked cache entirely: the world load task skips a square the provider
 * already holds, so the text wins by being there first.
 *
 * Returns 1 on success.
 */
int
Editor_LoadSquare(
    struct Editor* editor,
    struct CacheProvider* provider,
    int map_x,
    int map_z);

/**
 * Open any document in the mirror — a square (without seeding the provider;
 * Editor_LoadSquare does that), a spawn file, a config record, an interface,
 * a script — by asking the authority for its current text (DOC_OPEN).
 * Re-opening a loaded document re-syncs it from the authority. A SQUARE key
 * may carry its storage ("ported/<lane>/maps/m16_101").
 *
 * Returns 1 on success.
 */
int
Editor_OpenDoc(
    struct Editor* editor,
    enum Editor_DocKind kind,
    const char* key);

/** Tell the authority this session no longer looks at (kind, key). The
 *  mirror keeps its copy. */
void
Editor_CloseDoc(
    struct Editor* editor,
    enum Editor_DocKind kind,
    const char* key);

/** Save one document on the authority. Returns 1 when written, 0 when it had
 *  nothing unsaved, -1 when the server is read-only or the save failed. */
int
Editor_SaveDoc(
    struct Editor* editor,
    enum Editor_DocKind kind,
    const char* key);

/**
 * New square (design #squares): DOC_CREATE on the authority with the storage
 * and template the New dialog chose, then open the created square in this
 * mirror. EDITOR_HOST_REFUSED when the key already exists in that storage.
 * Nothing is written to disk until a save.
 */
enum EditorHost_Status
Editor_CreateSquare(
    struct Editor* editor,
    const struct EditorHost_DocCreate* request);

/**
 * Copy a loc / npc / obj definition (CFG "<type>:<name>") to `new_name` with
 * a fresh id: SAVE_AS on the authority (one undo step: the id line, the
 * client membership line, the new block with every field of the source),
 * then open "<type>:<new_name>" in this mirror. `out_id` (may be NULL) gets
 * the new id. EDITOR_HOST_REFUSED when the name is taken or not a name.
 */
enum EditorHost_Status
Editor_CopyRecord(
    struct Editor* editor,
    const char* type,
    const char* name,
    const char* new_name,
    int* out_id);

/**
 * Apply an edit: submit it to the authority, and on acceptance apply its
 * echo to the mirror, mark dirty, and queue the squares it invalidates —
 * the same three effects the local apply used to have, now guaranteed to
 * match every other client's, because everyone applied the same echo.
 *
 * Returns 1 when the authority accepted the command.
 */
int
Editor_Apply(
    struct Editor* editor,
    const struct Editor_Cmd* command);

/** Undo/redo the newest history group ON THE SERVER — the history is the
 *  authority's, shared by every client. Returns the commands reverted. */
int
Editor_Undo(struct Editor* editor);

int
Editor_Redo(struct Editor* editor);

/** Group every command until StrokeEnd into one undo step (a brush drag). */
void
Editor_StrokeBegin(struct Editor* editor);

/** The mirror's derived history (read only): one summary per undo group,
 *  `applied` the position, `serial` bumping on every change. */
const struct Editor_History*
Editor_GetHistory(const struct Editor* editor);

/** Name the action the open (or next) undo group records -- "Raise",
 *  "nudge locs" -- for the History panel's row. Affects nothing else. */
void
Editor_HistoryName(
    struct Editor* editor,
    const char* verb);

void
Editor_StrokeEnd(struct Editor* editor);

/**
 * Apply every pending broadcast from the server to the mirror: other
 * clients' edits, save confirmations, and this Client's shared-state
 * changes (forwarded to `on_state`). Editor_DrainRebuilds calls this, so a
 * session that rebuilds each frame is always current; call it directly when
 * acting on the mirror outside that cadence.
 */
int
Editor_PumpFacts(struct Editor* editor);

/** Publish one shared-state field to this session's Client — selection,
 *  tool, catalog pick. Semantics live with the subscribers; see
 *  enum ToriRSMapEdStateKey for the registry. */
int
Editor_StateSet(
    struct Editor* editor,
    uint32_t key,
    const int32_t* values,
    int count);

/** Register where shared-state facts land. One registrant; NULL clears. */
void
Editor_SetStateCallback(
    struct Editor* editor,
    void (*on_state)(void* user_data, uint32_t key, const int32_t* values, int count),
    void* user_data);

/**
 * Push the document's current derivation of every queued square back into the
 * provider and clear the queue.
 *
 * Returns the squares that need a world rebuild as (map_x, map_z) pairs, so
 * the caller can hand them to the load path. Call once per frame.
 */
int
Editor_DrainRebuilds(
    struct Editor* editor,
    struct CacheProvider* provider,
    int* out_coords,
    int max);

/**
 * Write every dirty document back to the content tree as text.
 *
 * Only the halves that changed are written — a terrain edit does not rewrite
 * the square's `.jl2` — and each file lands atomically. Saving does NOT bake:
 * the cache the game reads is updated only by Editor_Bake, which the user asks
 * for explicitly.
 *
 * Returns the number of documents saved, or -1 when the session is read-only.
 */
int
Editor_SaveAll(struct Editor* editor);

/**
 * Bake the content tree into the cache the game reads.
 *
 * The one call that runs a bake, so that "baking only ever happens because the
 * user asked for it" is a property one grep can check rather than a convention
 * to remember. Wire it to the Bake action and to nothing else — not to save,
 * not to a timer, not to a file watcher.
 */
int
Editor_Bake(
    struct Editor* editor,
    EditorHost_ProgressFn on_progress,
    void* progress_user_data);

#endif
