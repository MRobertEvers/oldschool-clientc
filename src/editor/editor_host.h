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
 *   - editor_host_local.c   direct calls, same process. This is the desktop
 *                           editor, and it means the "works without a server"
 *                           property is not a claim about configuration — there
 *                           is no server to misconfigure.
 *   - editor_host_remote.c  the same operations as frames over a WebSocket to
 *                           a small native daemon that owns the content tree.
 *
 * The host trades in opaque bytes: parsing and emitting `.jm2` text stays in
 * the frontend, so the wire carries files, not tiles, and the host never needs
 * to know the map format. It also means the host is the only thing that builds
 * a path — the frontend addresses a square by (map_x, map_z) and cannot name a
 * file outside the content tree.
 */

#include <stddef.h>

/** A file's bytes. NUL-terminated for the text the codec parses; `size`
 *  excludes that terminator. Freed with Editor_HostBlobFree. */
struct EditorHost_Blob
{
    char* data;
    size_t size;
};

void
Editor_HostBlobFree(struct EditorHost_Blob* blob);

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
};

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
 *                     `maps/` (e.g. `OSRS-Content/osrs239-content`).
 * @param repo_root    where the bake runs from; NULL disables baking, which is
 *                     what a session that only wants to read should pass.
 */
int
Editor_HostOpenLocal(
    struct EditorHost* host,
    const char* content_dir,
    const char* repo_root);

/** Calls the binding's free_ and clears the host. NULL-tolerant. */
void
Editor_HostClose(struct EditorHost* host);

#endif
