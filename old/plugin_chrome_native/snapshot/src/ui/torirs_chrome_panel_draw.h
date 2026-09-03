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

#endif /* TORIRS_CHROME_PANEL_DRAW_H */
