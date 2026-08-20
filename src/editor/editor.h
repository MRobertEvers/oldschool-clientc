#ifndef SRC_EDITOR_EDITOR_H
#define SRC_EDITOR_EDITOR_H

/**
 * The editor session: document, history, host, and the seam every edit passes
 * through.
 *
 * One executor, several entry paths. Interactive tools, replayed input, undo
 * and redo all reach the document through Editor_Apply — the editor's
 * equivalent of the net stack having a single door for packets. Nothing else
 * mutates a tile or a placement, which is what keeps "the square is dirty",
 * "the mesh needs rebuilding" and "this is undoable" from being three
 * bookkeeping jobs that drift apart.
 *
 * The session never talks to a game server. It is constructed only in an
 * editor boot, where the client's networking is not built at all, and the
 * squares it shows come from the content tree rather than from a rebuild
 * packet.
 */

#include "editor_cmd.h"
#include "editor_doc.h"
#include "editor_host.h"
#include "editor_types.h"

struct CacheProvider;
struct RSCache;

/** Squares queued for a remesh in one frame. Bounded by the load path's own
 *  ceiling; a single edit dirties at most four. */
#define EDITOR_REBUILD_QUEUE_MAX 64

struct Editor
{
    struct Editor_Doc doc;
    struct Editor_UndoStack undo;
    struct EditorHost host;
    int host_open;

    /** 0 when the session could not take the content-tree lock. The editor
     *  still runs — looking at a tree somebody else is editing is useful — it
     *  just cannot save. */
    int writable;

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
};

/**
 * Open a session over a content tree.
 *
 * @param content_dir  the revision content root holding `maps/`.
 * @param repo_root    where a bake would run; NULL disables baking.
 * @param profile      cache identity, for the terrain codec's tile widths.
 *
 * Takes the content-tree lock if it can. Returns 1 always — a session that
 * could not lock is read-only, not a failure; check `writable`.
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
 * Apply an edit: mutate the document, record it for undo, and queue the
 * squares it invalidates.
 *
 * `open_stroke` groups this command with the ones around it into a single undo
 * step — pass it for every command in a brush drag.
 */
int
Editor_Apply(
    struct Editor* editor,
    const struct Editor_Cmd* command);

int
Editor_Undo(struct Editor* editor);

int
Editor_Redo(struct Editor* editor);

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
 * Write every dirty square back to the content tree as text.
 *
 * Only the halves that changed are written — a terrain edit does not rewrite
 * the square's `.jl2` — and each file lands atomically. Saving does NOT bake:
 * the cache the game reads is updated only by Editor_Bake, which the user asks
 * for explicitly.
 *
 * Returns the number of squares saved, or -1 when the session is read-only.
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
