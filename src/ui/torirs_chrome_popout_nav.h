#ifndef SRC_UI_TORIRS_CHROME_POPOUT_NAV_H
#define SRC_UI_TORIRS_CHROME_POPOUT_NAV_H

/*
 * Plugin navigation inside a lane's own pop-out launcher column.
 *
 * On the OSRS239 lane the gameframe already carries a strip of launcher
 * buttons down the right edge (interface 728 `popout`: XP Tracker, Loot Tools,
 * Hiscores). A second strip beside it -- the plugin rail -- is two right-hand
 * navs. When a profile names that column (`[role:plugin_nav_column]`), the
 * engine appends one button per rail destination below the lane's own buttons
 * and the rail stands down. A lane without the role keeps the rail.
 *
 * This file is the part with no App in it: the badge every engine-added button
 * wears, the picture composed from a plugin's icon, the column geometry and
 * nothing else. The mode names are in ui/torirs_chrome_exec_kind.h, beside the
 * executor names a boot manifest resolves the same way; the tree edits live in
 * plugin/torirs_plugin_popout_nav.u.c.
 */

#include <stdint.h>

/** The lane's own button box and spacing: script 5356 `cc_setsize(30, 30)`
 *  and `$int2 = calc($int2 + 36)`. */
#define TORIRS_POPOUT_NAV_BUTTON 30
#define TORIRS_POPOUT_NAV_PITCH 36
/** The largest icon drawn inside the button before it is scaled down. The
 *  cache's own launcher icons are 23x22, 26x28 and 25x29. */
#define TORIRS_POPOUT_NAV_ICON_MAX 28

/** The badge that marks a button as the client's, not the cache's. */
#define TORIRS_CHROME_TORIFACE_W 12
#define TORIRS_CHROME_TORIFACE_H 15

/** One badge pixel, 0xAARRGGBB; alpha is 0 or 255. */
uint32_t
ToriRSChromeToriFace_Pixel(int column, int row);

/**
 * Where the first engine button goes in a column whose lane buttons end at
 * `native_bottom` (the largest y+h among the shown lane buttons, column-local),
 * or -1 when the lane shows none. One pitch below the last lane button, which
 * is exactly where the cache would have put another of its own.
 */
int
ToriRSPopoutNav_FirstY(int native_bottom);

/** How many engine buttons fit from `first_y` down a column `column_h` tall. */
int
ToriRSPopoutNav_Capacity(int first_y, int column_h);

/**
 * Compose one TORIRS_POPOUT_NAV_BUTTON-square button picture into `out`.
 *
 * `src` (`src_w` x `src_h`, 0xAARRGGBB) is centred, scaled down nearest-
 * neighbour to fit TORIRS_POPOUT_NAV_ICON_MAX when larger. A destination with
 * no icon of its own passes the wrench, as the rail does. The Tori face badge
 * is always drawn over the
 * bottom-right corner, so no engine button can pass for a native one.
 */
void
ToriRSPopoutNav_ComposeButton(
    uint32_t const* src,
    int src_w,
    int src_h,
    uint32_t* out);

#endif
