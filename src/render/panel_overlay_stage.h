#ifndef SRC_RENDER_PANEL_OVERLAY_STAGE_H
#define SRC_RENDER_PANEL_OVERLAY_STAGE_H

/*
 * What a plugin has drawn into the custom wells of its panel page, and what it
 * is drawing into one of them right now.
 *
 * Two lists. The KEPT list is every well's last complete picture, each
 * primitive tagged with the widget serial that owns it and the panel row that
 * serial sits on. The STAGING list is filled while one well's draw callback
 * runs, already moved into panel space: scaled about the well's origin and cut
 * to the well's clip as it arrives. Nothing staged is seen until a commit
 * swaps it in for that owner's run, all at once, so a redraw that fails
 * halfway leaves the last complete picture standing.
 *
 * A push with no well open is DROPPED, not handed on. That is the isolation
 * render/overlay_stage.h describes from the other side: a panel draw that was
 * never prepared must not fall through into a game list. A push past the
 * bound is dropped too, and remembered, and a well that overflowed refuses to
 * commit -- a run cut short is a picture with pieces missing, and the old
 * picture is the better one to keep. So is an empty one: a well whose
 * callback drew nothing has declined, not erased.
 *
 * Every change to what the kept list would show bumps `revision`, which skips
 * zero so a reader can hold 0 as "never seen". `generation` names the panel
 * selection the kept list was drawn for, and a commit for any other is
 * refused.
 *
 * Nothing here knows the panel's rows, the plugin host, or the clock. Whether
 * a row still holds the serial a run is tagged with, whether that row's well
 * is laid out and visible, the scroll clip a kept item is shown through, when
 * a well may be redrawn, and the selection generation itself are the
 * caller's.
 */

#include "ui/uitree_entity_overlay.h"

#include <stdbool.h>
#include <stdint.h>

enum
{
    /** Primitives across every custom well on the active page, kept and
     *  staged alike. */
    PANEL_OVERLAY_STAGE_MAX = 512,
};

struct PanelOverlayStage
{
    /** Every well's last complete picture, in panel space. */
    struct UITreeEntityOverlay kept[PANEL_OVERLAY_STAGE_MAX];
    /** The widget serial each kept primitive belongs to. */
    uint32_t kept_owner[PANEL_OVERLAY_STAGE_MAX];
    /** The panel row of that serial when it was committed, so a reader finds
     *  its clip without a search. */
    int kept_row[PANEL_OVERLAY_STAGE_MAX];
    int kept_count;
    /** One well's drawing in progress. */
    struct UITreeEntityOverlay staged[PANEL_OVERLAY_STAGE_MAX];
    int staged_count;
    /** A well is open: pushes are kept. */
    int open;
    /** A push was refused because `staged` was full. */
    int overflow;
    /** The open well's placement: its origin and scale in panel space, and
     *  the box every staged primitive is cut to. */
    int origin_x;
    int origin_y;
    int scale;
    int clip_x;
    int clip_y;
    int clip_w;
    int clip_h;
    /** The panel selection the kept list belongs to; 0 for none. */
    uint32_t generation;
    /** Moves whenever what the kept list shows may have changed. Never 0
     *  once moved. */
    uint32_t revision;
};

/**
 * Drop every kept and staged primitive, close the well, and belong to
 * `generation`. The revision moves when anything was kept or the generation
 * changes. Returns whether it moved.
 */
bool
PanelOverlayStage_Reset(
    struct PanelOverlayStage* stage,
    uint32_t generation);

uint32_t
PanelOverlayStage_Generation(struct PanelOverlayStage const* stage);

uint32_t
PanelOverlayStage_Revision(struct PanelOverlayStage const* stage);

/** Move the revision without changing the lists: the caller's view of them
 *  (a clip, a visibility) changed. */
void
PanelOverlayStage_Bump(struct PanelOverlayStage* stage);

/**
 * Open a well: empty the staging list and place every later push at
 * `origin` and `scale`, cut to the clip box.
 */
void
PanelOverlayStage_Begin(
    struct PanelOverlayStage* stage,
    int origin_x,
    int origin_y,
    int scale,
    int clip_x,
    int clip_y,
    int clip_w,
    int clip_h);

/** Close the well and empty the staging list, committed or not. */
void
PanelOverlayStage_End(struct PanelOverlayStage* stage);

/**
 * Stage one well-local primitive.
 *
 * False when it was not kept: no well is open, the list is full (remembered
 * in `overflow`), or the primitive has nothing left inside the clip.
 */
bool
PanelOverlayStage_Push(
    struct PanelOverlayStage* stage,
    struct UITreeEntityOverlay const* item);

/** How many primitives the open well has staged; 0 with no well open. */
int
PanelOverlayStage_StagedCount(struct PanelOverlayStage const* stage);

/**
 * Replace `owner`'s kept run with what is staged, tagged with `row`.
 *
 * Refused, with the kept list untouched, when `generation` or `owner` is 0,
 * `generation` is not the one the list belongs to, the staging overflowed or
 * is empty, or the other owners' runs and the new one do not fit together.
 * Returns whether it was committed; a commit moves the revision.
 */
bool
PanelOverlayStage_Commit(
    struct PanelOverlayStage* stage,
    uint32_t generation,
    uint32_t owner,
    int row);

/** Drop `owner`'s kept run. Returns whether anything was dropped; a drop
 *  moves the revision. Owner 0 owns nothing. */
bool
PanelOverlayStage_Retire(
    struct PanelOverlayStage* stage,
    uint32_t owner);

/** Shift `owner`'s kept run, clips included, by (dx, dy). Returns whether
 *  anything moved; a move moves the revision. */
bool
PanelOverlayStage_Move(
    struct PanelOverlayStage* stage,
    uint32_t owner,
    int dx,
    int dy);

int
PanelOverlayStage_KeptCount(struct PanelOverlayStage const* stage);

/**
 * One kept primitive, its owner and its row.
 *
 * False for an index outside the list, or while the list belongs to no
 * selection.
 */
bool
PanelOverlayStage_Kept(
    struct PanelOverlayStage const* stage,
    int index,
    struct UITreeEntityOverlay* out,
    uint32_t* out_owner,
    int* out_row);

/**
 * Copy `owner`'s kept run into the staging list, shifted so (origin_x,
 * origin_y) becomes (0, 0), and return how many were copied.
 *
 * For rastering one well on its own while no well is open. The staging count
 * is not touched: the copy is read through PanelOverlayStage_Staged.
 */
int
PanelOverlayStage_StageOwnerLocal(
    struct PanelOverlayStage* stage,
    uint32_t owner,
    int origin_x,
    int origin_y);

/** The staging list. Also the surface a panel draw names, so the plugin host
 *  can tell the open well from any other draw target. */
struct UITreeEntityOverlay*
PanelOverlayStage_Staged(struct PanelOverlayStage* stage);

#endif
