#ifndef SRC_EDITOR_MAP_EDITOR_APP_H
#define SRC_EDITOR_MAP_EDITOR_APP_H

/*
 * The map editor as the client holds it: everything an editor boot keeps,
 * in one record the App points at (app.h `map_editor`).
 *
 * The App holds ONE pointer to it, NULL in every boot that is not an editor
 * boot -- and in an editor boot whose ToriRSMapEd could not be reached, which
 * boots without the editor. App_Init makes it in its editor block, beside the
 * session it opens, and App_Shutdown frees it after closing that session.
 * Shared code never reads it: it asks the App layer's map editor queries
 * (app/app_internal.h, app.h), which test for it once and answer no when it
 * is absent.
 *
 * It owns no chrome. The command panel is built in the developer overlay's
 * chrome (ui/dev_overlay.h), which the App unit hands to each call that adds,
 * reads or shows one; this record keeps the ids that came back and the
 * storage the panel's dropdowns borrow.
 *
 * Nothing here reads a world, input or the environment, builds chrome, or
 * prints. The rules below are the panel-state tests the App layer's queries
 * are made of, each named for what it tests; they are not one test spelled
 * five ways, and they must not be unified.
 */

#include "editor/editor_panel.h"
#include "editor/map_editor_camera.h"
#include "editor/map_editor_config.h"
#include "editor/map_editor_ghost.h"

struct Editor;
struct MapEditorState;

struct MapEditorApp
{
    /** The session (editor/editor.h). Borrowed: App_Init allocates and opens
     *  it, App_Shutdown closes and frees it, then this record. */
    struct Editor* session;
    /** The launch options this boot was given: the App's cfg.map_editor,
     *  which outlives this record. */
    struct MapEditorConfig const* config;
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
    /** The editor camera's latches (app/app_mapeditor_camera.c). */
    struct MapEditorCamera camera;
    /** The App layer's editor units' working state (app/app_mapeditor_state.h):
     *  drags, height tools, files, wells, previews, thumbnails, the view and
     *  the hotkeys. Owned by the App layer, which makes it beside this record
     *  and frees it before; NULL only between the two. */
    struct MapEditorState* state;
};

/** A zeroed record with the ghost reset, holding `session` and `config`,
 *  allocated in the EDITOR group. Never NULL. */
struct MapEditorApp*
MapEditorApp_New(
    struct Editor* session,
    struct MapEditorConfig const* config);

/** Release a record from MapEditorApp_New; not the session it holds, nor the
 *  panel's registry (Editor_PanelFree). NULL is accepted. */
void
MapEditorApp_Free(struct MapEditorApp* app);

/** Is the command panel shown? (the touch, the pick filter, the chat line,
 *  the selection overlay) */
int
MapEditorApp_PanelShown(struct MapEditorApp const* app);

/** Is the command panel built with its registry: the chrome scale, the text
 *  rendering and the window box it remembers are its to answer. */
int
MapEditorApp_PanelLive(struct MapEditorApp const* app);

/** Built, shown and with its registry: the editor's camera scheme steers the
 *  World (app_editor_hotkeys_owns_camera). */
int
MapEditorApp_PanelOwnsCamera(struct MapEditorApp const* app);

#endif
