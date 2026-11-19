#ifndef SRC_EDITOR_EDITOR_SHELL_H
#define SRC_EDITOR_EDITOR_SHELL_H

/*
 * The editors' working state inside the client: the loc editor's panel
 * handles and its selection, the Place tool's hover ghost, the camera on the
 * model-view well, and the map editor's command panel.
 *
 * It does not own a chrome instance. Every panel named here is built in the
 * developer overlay's (ui/dev_overlay.h), which the App unit hands to each
 * call that adds, reads or shows one; this record keeps the ids that came
 * back and, in the map editor's panel, the storage its dropdowns borrow.
 *
 * It lives on the heap, in the EDITOR group, and exists for as long as the App
 * does, whether or not a map editor session was opened: EditorShell_Create
 * hands back a zeroed record with the ghost and the preview camera reset, and
 * EditorShell_Free releases it. The map editor panel's own registry is not
 * released here; the App unit frees it when it shuts the editor session down.
 *
 * Nothing here reads a world, input or the environment, builds chrome, or
 * prints. The caller routes the keys, places the ghost and the selection's
 * moves in the scene, and does the panel work through the fields below.
 */

#include "editor/editor_panel.h"
#include "editor/editor_preview_camera.h"
#include "editor/loc_editor_selection.h"
#include "editor/map_editor_ghost.h"

/** The loc editor panel's chrome handles: the panel, its four readout rows and
 *  its eight buttons. Ids the chrome hands back, nothing more. */
struct LocEditorWidgets
{
    int panel;
    int visible;
    int row_target; /* "loc <id> shape <n>" or "no loc selected" */
    int row_pos;    /* "x=.. z=.. level=.." */
    int row_size;   /* "size AxB angle=N" */
    int row_extra;  /* loc name, or "interactive=0/1" when unnamed */
    int item_xplus;
    int item_xminus;
    int item_zplus;
    int item_zminus;
    int item_rotate;
    int item_reselect;
    int item_deselect;
    int item_close;
};

struct EditorShell
{
    /** The loc editor: a TORIRS_CHROME_PANEL_MENU in the developer chrome.
     *  Opened at the loc under the cursor; "Move"/"Rotate" rows re-place it
     *  client-side only, so the readout gives exact scene coords to hand-copy
     *  into a script without a server round trip. */
    struct LocEditorWidgets locedit;
    /** What the panel is pointed at, and what moving it means. Set only by an
     *  explicit Reselect/Deselect: opening or closing the panel never changes
     *  it. @see editor/loc_editor_selection.h */
    struct LocEditorSelection locedit_selection;
    /**
     * The Place-loc tool's hover ghost: a REAL loc placed at the hovered tile
     * through the same seam a commit uses, then made translucent. It EVICTS
     * whatever held that tile's slot, and has to put it back. See
     * editor/map_editor_ghost.h for the bookkeeping; the caller makes the
     * scene placements it asks for.
     */
    struct MapEditorGhost map_ghost;
    /** The camera on the editor's model-view well, shared by the catalog's
     *  preview and the loc editor's. See editor/editor_preview_camera.h. */
    struct EditorPreviewCamera preview_camera;
    /** The map editor's command panel. Its widgets point into its palette
     *  storage, so it must not move once built; the record it sits in is
     *  allocated once and never moves. */
    struct Editor_Panel editor_panel;
};

/** A zeroed shell with the ghost and the preview camera reset, allocated in the
 *  EDITOR group. Never NULL. */
struct EditorShell*
EditorShell_Create(void);

/** Release a shell from EditorShell_Create. NULL is accepted. */
void
EditorShell_Free(struct EditorShell* shell);

#endif
