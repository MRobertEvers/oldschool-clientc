#ifndef SRC_EDITOR_EDITOR_PANEL_H
#define SRC_EDITOR_EDITOR_PANEL_H

/**
 * The map editor's main window, as the App holds it.
 *
 * The panels themselves are modules (src/editor/panels/, registered in
 * editor/editor_ui.h): menu bar, tool rail, terrain brushes, palette,
 * rectangle select, square picker, locs, spawns, inspector, catalog, output
 * console, the New square dialog and the save guard. This file is what stays
 * App-facing: the struct the App embeds, the per-frame entry the frame loop
 * calls, the SELECT latch the overlay and the minimenu read, and the world
 * actions the minimenu and the loc editor invoke.
 *
 * Built entirely on ToriRSChrome, and that is a hard rule rather than a
 * convenience. The editor exists to repair content, so its chrome must not
 * depend on the content being repairable — it has to open on a half-broken
 * bake, a foreign revision, or a square whose configs are mid-edit.
 * ToriRSChrome has its fonts compiled in and reaches no cache.
 *
 * Every edit is a COMMAND on a document (editor_cmd.h), submitted through the
 * session; the world shows it through the EDIT layout's per-frame flush (or a
 * square rebuild under STATIC). Nothing here edits the scene directly: a loc
 * placed, moved or deleted is a LOC command, never an App_WorldLocChange —
 * that would put a second, default-lit copy beside the one the flush places.
 * The SELECTION is held by document address (Editor_LocRef), not by scene
 * element id: an element dies on every reshape, a document address does not.
 */

#include "editor.h"
#include "editor_ui.h"

#include <stdint.h>

struct ToriRSChrome;
struct App;

/** What the SELECT latch holds. Read by the overlay (app_overlay.c), which
 *  outlines it, and the minimenu (app_minimenu.c), which sets it. */
enum Editor_SelectionKind
{
    EDITOR_SELECTION_NONE = 0,
    EDITOR_SELECTION_TERRAIN,
    EDITOR_SELECTION_LOC,
    /** A spawn (kept for the selection relay's value layout). */
    EDITOR_SELECTION_NPC,
    EDITOR_SELECTION_OBJ
};

struct Editor_Panel
{
    /**
     * The editor session, for publishing shared state. Borrowed from the
     * App, which sets it after Editor_PanelInit; NULL until then.
     */
    struct Editor* editor;
    /** Set while applying a received state fact, so the latching it causes
     *  does not re-publish what the server just said. */
    int applying_shared_state;

    int built;
    int visible;

    /** The panel registry and the shared tool state (editor_ui.h). Owned:
     *  allocated by Editor_PanelInit, released by Editor_PanelFree. */
    struct EditorUi* ui;

    /**
     * The PRIMARY selection, in this connection's SCENE coordinates — what
     * the overlay outlines. A loc selection is the first of ui->tools.locs
     * (document addresses); sel_element_id is a cache the overlay heals by
     * tile and shape when a reshape kills the element.
     */
    enum Editor_SelectionKind sel_kind;
    int sel_scene_x;
    int sel_scene_z;
    int sel_level;
    int sel_element_id;
    int sel_loc_id;
    int sel_shape;
    int sel_angle;

    /** A square the picker asked for. The app drains it on the frame
     *  boundary (app_map_editor_open_pending_square); the panels never call
     *  the world loader themselves. `sq_open_add` keeps what is open. */
    int sq_open_pending;
    int sq_open_x;
    int sq_open_z;
    int sq_open_add;
    char sq_open_storage[EDITOR_DOC_STORAGE_MAX];
    /** Open (not Add) also brings the free camera to the square once its
     *  load has landed: the tile it goes to, pending until the world has it.
     *  Without this an Open of a square away from the camera looked like
     *  nothing at all (owner, 2026-11-02). */
    int sq_goto_pending;
    int sq_goto_x;
    int sq_goto_z;

    /** The brush stroke the world click opened on press (a drag is one undo
     *  step), and the last tile it painted, so a drag paints each tile once
     *  per visit. */
    int stroke_active;
    int stroke_last_x;
    int stroke_last_z;
    int rect_dragging;

    /** With a level pinned, the tile under the pointer ON THAT PLANE (scene
     *  tiles): the pick meets the first surface drawn, which over an empty
     *  upper floor is the ground below, a tile or more away from where the
     *  pinned plane passes under the pointer at a slant. The App re-picks
     *  each frame (app_map_editor_pin_hover) and every editor consumer reads
     *  it through Editor_PanelHoverTile. `pin_hover_level` is the level it
     *  was computed for; -1 = not computed. */
    int pin_hover_level;
    int pin_hover_valid;
    int pin_hover_x;
    int pin_hover_z;
};

/** Construct the registry and every tools-panels module over `ui`. Call
 *  once, after the chrome exists. The App sets `panel->editor` after. */
void
Editor_PanelInit(
    struct Editor_Panel* panel,
    struct ToriRSChrome* ui);

/** Release the registry. NULL-safe on an unbuilt panel. */
void
Editor_PanelFree(struct Editor_Panel* panel);

void
Editor_PanelSetVisible(
    struct Editor_Panel* panel,
    struct ToriRSChrome* ui,
    int visible);

/** Per frame: the registry's dispatch and every panel's tick. */
void
Editor_PanelTick(
    struct Editor_Panel* panel,
    struct App* app);

/** The armed tool (shared tool state). */
enum Editor_Tool
Editor_PanelTool(const struct Editor_Panel* panel);

/** Apply the armed tool at a scene tile, as one undo step: a brush
 *  application, a place, a move, a rotate, a delete. */
int
Editor_PanelApplyToolAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

/**
 * Record a loc move or rotate in the DOCUMENT (the loc editor's nudge and
 * rotate). Scene coordinates; a move across a square border is a delete in
 * one square and an add in the other, one undo step. Returns 1 when
 * recorded.
 */
int
Editor_PanelRecordLocEdit(
    struct Editor_Panel* panel,
    struct App* app,
    int from_scene_x,
    int from_scene_z,
    int level,
    int loc_id,
    int shape,
    int from_angle,
    int to_scene_x,
    int to_scene_z,
    int to_angle);

/** Latch a ground tile (a plain click, or the minimenu's "Select terrain").
 *  Clears the loc selection. */
void
Editor_PanelSelectTerrain(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

/** Latch an exact loc by scene element id (the minimenu's "Select wall /
 *  object / decor" rows). With shift held it is added to (or taken out of)
 *  the multi-selection instead. No-op when the element is not live. */
void
Editor_PanelSelectLoc(
    struct Editor_Panel* panel,
    struct App* app,
    int element_id);

/** The level an edit means: the pinned plane, or the pick's on "auto". */
int
Editor_PanelEditLevel(
    struct Editor_Panel const* panel,
    struct App const* app);

/** The scene tile the editor's tools act on under the pointer: the pick's
 *  on Auto, the pinned plane's (pin_hover_*) with a level pinned. 0 when
 *  there is none (no hit, or the pointer is off the pinned plane). */
int
Editor_PanelHoverTile(
    struct Editor_Panel const* panel,
    struct App const* app,
    int* out_scene_x,
    int* out_scene_z);

/** The painter level mask the view cap asks for; 0 = no opinion. */
uint8_t
Editor_PanelVisLevelMask(struct Editor_Panel const* panel);

/** Stamp the catalog's picked loc at a scene tile. 0 when nothing is
 *  picked or the square is not open. */
int
Editor_PanelPlaceLocAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

/** Delete the first loc on a tile, whatever layer it is on. */
int
Editor_PanelDeleteLocAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

/** Delete every loc on a tile, one undo step. Returns how many went. */
int
Editor_PanelClearLocsAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

/** The armed brush applied once at the latched tile (the E key). */
int
Editor_PanelApplyToSelection(
    struct Editor_Panel* panel,
    struct App* app);

/** The pose the Place tool would stamp now, for the hover ghost. */
void
Editor_PanelGhostSpec(
    struct Editor_Panel* panel,
    struct App* app,
    int* out_shape,
    int* out_angle);

/** Delete whatever is selected (every selected loc). */
int
Editor_PanelDeleteSelection(
    struct Editor_Panel* panel,
    struct App* app);

/** Delete one exact loc by scene element id (minimenu "Delete Wall"). */
int
Editor_PanelDeleteLocByElement(
    struct Editor_Panel* panel,
    struct App* app,
    int element_id);

void
Editor_PanelClearSelection(
    struct Editor_Panel* panel,
    struct App* app);

/** A shared-state fact from this connection's Client: the selection relay,
 *  and every other key to the panel modules. */
void
Editor_PanelApplySharedState(
    struct Editor_Panel* panel,
    struct App* app,
    uint32_t key,
    const int32_t* values,
    int count);

#endif
