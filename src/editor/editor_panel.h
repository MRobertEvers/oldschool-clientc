#ifndef SRC_EDITOR_EDITOR_PANEL_H
#define SRC_EDITOR_EDITOR_PANEL_H

/**
 * The map editor's panel: tool, brush, palettes, save and bake.
 *
 * Built entirely on ToriDbgUI, and that is a hard rule rather than a
 * convenience. The editor exists to repair content, so its chrome must not
 * depend on the content being repairable — it has to open on a half-broken
 * bake, a foreign revision, or a square whose configs are mid-edit. ToriDbgUI
 * has its fonts compiled in and reaches no cache, which is exactly that
 * property; a panel built from cache interfaces would go dark precisely when
 * it is needed.
 *
 * The dividing line the panel keeps: CHROME IS BAKED, CONTENT IS DRAWN. Row
 * text, boxes and the dropdown arrow come from the overlay's own primitives.
 * The palette ENTRIES are content — underlay and overlay names read out of the
 * cache at runtime — and degrade to plain ids when the configs cannot be read,
 * so the palette still operates on a cache the editor cannot fully decode.
 */

#include "editor.h"

struct ToriDbgUI;
struct App;

/** Which edit the next click makes. */
enum Editor_Tool
{
    /** Read-only: report what is under the cursor. */
    EDITOR_TOOL_SELECT = 0,
    EDITOR_TOOL_HEIGHT,
    EDITOR_TOOL_UNDERLAY,
    EDITOR_TOOL_OVERLAY,
    EDITOR_TOOL_FLAGS,
    EDITOR_TOOL_COUNT
};

/** Palette entries the dropdowns borrow. Sized for the flotype/underlay id
 *  space, which is a byte for underlays and a u16 for overlays. */
#define EDITOR_PALETTE_MAX 512
#define EDITOR_PALETTE_LABEL_MAX 32

/** What EDITOR_TOOL_SELECT is latched onto. Own concept from the loc editor's
 *  locedit_* fields (src/app.c) -- that tool targets a loc to nudge/rotate;
 *  this one targets a tile or loc the OTHER tools (height/underlay/overlay/
 *  flags) act on, and the two panels can be open at once with different
 *  subjects. */
enum Editor_SelectionKind
{
    EDITOR_SELECTION_NONE = 0,
    EDITOR_SELECTION_TERRAIN,
    EDITOR_SELECTION_LOC
};

struct Editor_Panel
{
    int panel;
    int built;

    /* Rows. */
    int row_status;
    int row_square;
    int row_tile;
    int row_authored;
    int dd_tool;
    int dd_underlay;
    int dd_overlay;
    int in_height;
    int dd_shape;
    int dd_rotation;
    int cb_flag_block;
    int cb_flag_bridge;
    int cb_flag_roof;
    int cb_flag_below;
    int item_undo;
    int item_redo;
    int item_save;
    int item_bake;
    int item_close;

    int visible;
    enum Editor_Tool tool;

    /**
     * Palette storage. The dropdowns borrow these, so they must outlive every
     * Build — which is why they live in the panel rather than on a stack.
     */
    char underlay_labels[EDITOR_PALETTE_MAX][EDITOR_PALETTE_LABEL_MAX];
    char const* underlay_options[EDITOR_PALETTE_MAX];
    int underlay_ids[EDITOR_PALETTE_MAX];
    int underlay_count;

    char overlay_labels[EDITOR_PALETTE_MAX][EDITOR_PALETTE_LABEL_MAX];
    char const* overlay_options[EDITOR_PALETTE_MAX];
    int overlay_ids[EDITOR_PALETTE_MAX];
    int overlay_count;

    /** Last tile the panel reported on, so the readout only rebuilds on change. */
    int shown_x;
    int shown_z;
    int shown_level;

    /** EDITOR_TOOL_SELECT's latch: what the other tools' click/readout targets
     *  when it is not just the live hover. sel_scene_x/z/level are valid for
     *  both kinds; sel_element_id/sel_loc_id only for EDITOR_SELECTION_LOC. */
    enum Editor_SelectionKind sel_kind;
    int sel_scene_x;
    int sel_scene_z;
    int sel_level;
    int sel_element_id;
    int sel_loc_id;
};

/** Construct the rows. Call once, after the overlay exists. */
void
Editor_PanelInit(
    struct Editor_Panel* panel,
    struct ToriDbgUI* ui);

void
Editor_PanelSetVisible(
    struct Editor_Panel* panel,
    struct ToriDbgUI* ui,
    int visible);

/**
 * Per-frame: refresh the readout from the hovered tile, and act on whatever
 * the user activated since the last call.
 *
 * Takes the App because acting on a click needs the world (what tile is under
 * the cursor) and the editor session (what to do about it) at once.
 */
void
Editor_PanelTick(
    struct Editor_Panel* panel,
    struct App* app);

/**
 * Apply the current tool to a tile, as one undoable command.
 *
 * Separate from the tick so a brush drag can call it per tile inside one
 * stroke, and so the click path and any scripted path land in the same place.
 */
int
Editor_PanelApplyToolAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

/**
 * Record a loc move or rotate in the DOCUMENT, so it survives a save.
 *
 * The loc editor already re-places the loc in the scene through
 * App_WorldLocChange, which is what makes the move visible. That is a
 * client-side scene edit and nothing more — it never touched the square's
 * `.jl2`, so the move was gone on the next world reload and could never be
 * saved. This is the other half: the same move as an undoable command against
 * the authored loc list.
 *
 * Coordinates are SCENE tiles, as the loc editor holds them; the square and
 * tile they belong to are resolved here.
 *
 * Returns 1 when a command was recorded. 0 means there is no editor session, or
 * the square the loc sits in is not open in the document — a loc can be visible
 * in the scene while its square was never loaded for editing.
 */
/** Latch the SELECT tool onto a ground tile -- either a plain click (the
 *  hovered tile) or the "Select terrain" minimenu row (the exact tile the
 *  row named). Clears any loc latch: one panel, one subject. */
void
Editor_PanelSelectTerrain(
    struct Editor_Panel* panel,
    int scene_x,
    int scene_z,
    int level);

/** Latch the SELECT tool onto an exact loc, by scene element id -- the
 *  "Select wall/object/decor" minimenu rows' handler. Comes from the same
 *  pick/classify/dedup pipeline the minimenu itself uses, so it disambiguates
 *  a tile carrying a wall AND a decor AND a ground loc exactly the way the
 *  row that was clicked named it. No-op (selection unchanged) if the element
 *  is not a live scenery element. */
void
Editor_PanelSelectLoc(
    struct Editor_Panel* panel,
    struct App* app,
    int element_id);

/** Clear the SELECT tool's latch without touching the world. */
void
Editor_PanelClearSelection(
    struct Editor_Panel* panel);

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

#endif
