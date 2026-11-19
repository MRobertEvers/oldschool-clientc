#ifndef SRC_EDITOR_EDITOR_SHELL_H
#define SRC_EDITOR_EDITOR_SHELL_H

/*
 * The map editor's working state inside the client: the Place tool's hover
 * ghost and the map editor's command panel.
 *
 * It does not own a chrome instance. The command panel is built in the
 * developer overlay's (ui/dev_overlay.h), which the App unit hands to each
 * call that adds, reads or shows one; this record keeps the ids that came
 * back and the storage the panel's dropdowns borrow. (The loc editor is a
 * side-app with its own state, app/app_tool_loc_editor.c; the model-view
 * camera the Catalog and the loc editor share is the developer overlay's.)
 *
 * It lives on the heap, in the EDITOR group, and exists for as long as the App
 * does, whether or not a map editor session was opened: EditorShell_Create
 * hands back a zeroed record with the ghost reset, and EditorShell_Free
 * releases it. The map editor panel's own registry is not released here; the
 * App unit frees it when it shuts the editor session down.
 *
 * Nothing here reads a world, input or the environment, builds chrome, or
 * prints. The caller places the ghost in the scene and does the panel work
 * through the fields below.
 */

#include "editor/editor_panel.h"
#include "editor/map_editor_ghost.h"

struct EditorShell
{
    /**
     * The Place-loc tool's hover ghost: a REAL loc placed at the hovered tile
     * through the same seam a commit uses, then made translucent. It EVICTS
     * whatever held that tile's slot, and has to put it back. See
     * editor/map_editor_ghost.h for the bookkeeping; the caller makes the
     * scene placements it asks for.
     */
    struct MapEditorGhost map_ghost;
    /** The map editor's command panel. Its widgets point into its palette
     *  storage, so it must not move once built; the record it sits in is
     *  allocated once and never moves. */
    struct Editor_Panel editor_panel;
};

/** A zeroed shell with the ghost reset, allocated in the
 *  EDITOR group. Never NULL. */
struct EditorShell*
EditorShell_Create(void);

/** Release a shell from EditorShell_Create. NULL is accepted. */
void
EditorShell_Free(struct EditorShell* shell);

#endif
