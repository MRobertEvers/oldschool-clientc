#ifndef SRC_RENDER_OVERLAY_STAGE_H
#define SRC_RENDER_OVERLAY_STAGE_H

/*
 * Where an overlay primitive goes.
 *
 * Health bars, hitsplats, overhead chat, the editor's marks and everything a
 * plugin draws all arrive at one function, and it decides which of the frame's
 * lists they land in. Which one is decided by the draw window that is open --
 * and nothing that pushes has to know which that is, or that there is more
 * than one list at all.
 *
 * The isolation is the rule. A plugin drawing into a PANEL well that was never
 * prepared must be DROPPED, not fall through into the world list, because the
 * world list is painted over the game: a panel that failed to stage its
 * drawing would otherwise scribble its contents across the scene, positioned
 * in panel coordinates, which is not a failure anyone would read as "the panel
 * did not open".
 *
 * The two lists are different sizes on purpose. A filled polygon is a
 * begin/point.../end RUN, so one highlighted entity costs a dozen entries
 * rather than one; at 512 the runs starved the outlines that followed them and
 * every later push was dropped, which on screen reads as a broken outline
 * rather than as a full buffer. The canvas list holds a handful of orbs and
 * bars -- nothing in it is per-entity -- and a plugin is held to its own draw
 * budget on top of that.
 *
 * Nothing here draws, projects or clips. The caller has already turned an
 * entity into flat screen-space primitives; this decides where they are kept
 * until the frame consumes them.
 */

#include "ui/uitree_entity_overlay.h"

#include <stdbool.h>

enum
{
    /** Room for one frame's world primitives. Sized for polygon RUNS, not for
     *  one entry per entity -- see the note above. */
    OVERLAY_STAGE_WORLD_MAX = 2048,
    /** Room for one frame's canvas primitives. */
    OVERLAY_STAGE_CANVAS_MAX = 512,
};

/** Which surface a draw window is open on. */
enum OverlaySurface
{
    /**
     * The game world, under the interfaces.
     *
     * The default, and what every built-in overlay uses: they are built with
     * no draw window open at all.
     */
    OVERLAY_SURFACE_WORLD,
    /** Above the interfaces, in canvas space. */
    OVERLAY_SURFACE_CANVAS,
    /**
     * A panel-local well, staged elsewhere.
     *
     * Named here so this module can REFUSE it rather than treating it as the
     * default. It is not one of these lists and must never become one.
     */
    OVERLAY_SURFACE_PANEL
};

struct OverlayStage
{
    struct UITreeEntityOverlay world[OVERLAY_STAGE_WORLD_MAX];
    int world_count;
    struct UITreeEntityOverlay canvas[OVERLAY_STAGE_CANVAS_MAX];
    int canvas_count;
    /**
     * This frame's overlay batch has already been opened.
     *
     * A retained refresh can discover a new role anchor and immediately fall
     * back to a full walk; the second open must reuse the first dispatch
     * rather than spending a plugin's draw budget twice.
     */
    bool batch_started;
    bool canvas_prepared;
};

/** Empty both lists and close the batch. */
void
OverlayStage_Reset(struct OverlayStage* stage);

/** Begin a frame's world list. */
void
OverlayStage_ResetWorld(struct OverlayStage* stage);

/** Begin a frame's canvas list. */
void
OverlayStage_ResetCanvas(struct OverlayStage* stage);

/**
 * Keep one primitive for the surface whose draw window is open.
 *
 * False when it was not kept: the list is full, or the surface is PANEL, which
 * this stage does not hold. A refusal is a dropped primitive and never a
 * primitive on the wrong surface.
 */
bool
OverlayStage_Push(
    struct OverlayStage* stage,
    enum OverlaySurface surface,
    struct UITreeEntityOverlay const* item);

/**
 * How many primitives the open draw window has kept.
 *
 * So a draw verb can report its own cost without knowing which list it landed
 * in. PANEL reports none, because none of it is here.
 */
int
OverlayStage_Count(struct OverlayStage const* stage, enum OverlaySurface surface);

/** The kept primitives for one surface, or NULL for PANEL. */
struct UITreeEntityOverlay const*
OverlayStage_Items(struct OverlayStage const* stage, enum OverlaySurface surface);

#endif
