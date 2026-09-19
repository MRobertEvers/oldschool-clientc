#ifndef TORIRS_CHROME_PANEL_DRAW_H
#define TORIRS_CHROME_PANEL_DRAW_H

#include "uitree_debug_overlay.h"
#include "uitree_host.h"

/**
 * Transform one plugin-local logical primitive into a panel-surface item.
 *
 * Every output receives `visible_clip`; a plugin-supplied local clip narrows
 * it but can never enlarge it. Returns zero when the intersection is empty.
 */
int
ToriRSChromePanelDraw_Transform(
    struct UITreeEntityOverlay const* item,
    int origin_x,
    int origin_y,
    int scale,
    struct ToriRSChromeRect visible_clip,
    struct UITreeEntityOverlay* out);

/**
 * Translate one retained panel primitive into a chrome primitive, for the
 * in-canvas fallback -- the path that draws a plugin panel with the client's
 * own chrome renderer when there is no separate window to put it in.
 *
 * Returns zero for a primitive that has nothing to draw, and that is per kind
 * rather than a shared test: a rect or a line always draws, a text with an
 * empty string draws nothing, and a sprite whose scene id is not yet resolved
 * draws nothing YET -- it is an asset that has not arrived. All three would
 * otherwise emit an item the renderer has to make its own decision about, and
 * the empty-text one shows up as a stray background block.
 *
 * A world polygon returns zero too. The panel API exposes rect, line, text and
 * image; a polygon in a panel's list came from somewhere else.
 *
 * `color` is masked to its low 24 bits. The overlay's alpha byte is not the
 * chrome's: chrome carries transparency in `trans`, which is copied
 * separately, so leaving the byte in would multiply one by the other.
 */
int
ToriRSChromePanelDraw_ToChromePrim(
    struct UITreeEntityOverlay const* item,
    struct ToriRSChromePrim* out);

/** How a retained custom surface changed between two layout passes. SIZE is
 * the only bit that requires invoking the plugin again; ORIGIN and CLIP can be
 * applied to its retained primitive run. */
enum ToriRSChromePanelDrawChange
{
    TORIRS_CHROME_PANEL_DRAW_SIZE = 1u << 0,
    TORIRS_CHROME_PANEL_DRAW_ORIGIN = 1u << 1,
    TORIRS_CHROME_PANEL_DRAW_CLIP = 1u << 2,
    TORIRS_CHROME_PANEL_DRAW_HIDDEN = 1u << 3,
};

unsigned
ToriRSChromePanelDraw_Changes(
    int previous_valid,
    struct ToriRSChromeRect previous_region,
    struct ToriRSChromeRect previous_clip,
    int next_valid,
    struct ToriRSChromeRect next_region,
    struct ToriRSChromeRect next_clip);

#endif /* TORIRS_CHROME_PANEL_DRAW_H */
