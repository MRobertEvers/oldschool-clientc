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
