#ifndef SRC_UI_TORIRS_CHROME_LANE_LAUNCHER_H
#define SRC_UI_TORIRS_CHROME_LANE_LAUNCHER_H

/*
 * The plugin launcher the engine hangs off a lane's own stone.
 *
 * WHY IT EXISTS. On the OSRS239 MOBILE toplevel (601, `toplevel_osm`) with the
 * lane's own chrome up -- gameframe-layout on `Auto`, which defers to the lane
 * on a CS2 root rather than arranging over it -- nothing offers the plugin
 * window. The engine's other three launchers all stand down there: the rail
 * wants a presenter the Android shell does not give it, the pop-out nav column
 * (ui/torirs_chrome_popout_nav.h) wants interface 728 on screen and a mobile
 * toplevel mounts it hidden, and the dat2 profile authors no
 * `option_action=PLUGIN_PANEL` row because on a desk the column is there. The
 * Stone Drawer grew its own PLUGINS switch for exactly this reason; a player
 * who has NOT replaced the lane's chrome was left with no way in at all.
 *
 * WHAT IT IS. One button, wearing the lane's own stone, one pitch below the
 * "Start chatting" stone in the mobile chat column -- the button this file
 * calls the ANCHOR. The profile names the anchor
 * (`[role:plugin_launcher_anchor]`) and nothing else: every number here is
 * read off that node and its children at runtime, so the engine knows the
 * word "anchor" and not one cache id, one sprite or one stone size.
 *
 * This file is the part with no App and no UITree in it: where the button goes
 * under the column, and the picture it wears. The tree edits are in
 * plugin/torirs_plugin_lane_launcher.u.c.
 */

#include <stdint.h>

/**
 * The largest button this will compose, in pixels.
 *
 * The one it is written for is the OSM stone, 58x40. The cap is what bounds
 * the composition buffers, which are stack arrays on the tick path; a lane
 * naming an anchor larger than this gets no launcher rather than a smashed
 * frame.
 */
#define TORIRS_LANE_LAUNCHER_MAX_W 128
#define TORIRS_LANE_LAUNCHER_MAX_H 128
#define TORIRS_LANE_LAUNCHER_PIXELS_MAX                                                            \
    (TORIRS_LANE_LAUNCHER_MAX_W * TORIRS_LANE_LAUNCHER_MAX_H)

/**
 * The column's pitch: how far one stone's top is below the one above it.
 *
 * Derived rather than assumed, because it is not the stone's height. The OSM
 * column stacks 40-tall stones at y 0, 39 and 78 -- they overlap by the one
 * pixel their art shares -- so a launcher placed a full height below the last
 * stone would sit a pixel proud of the column it belongs to.
 *
 * `previous_y` is the top of the nearest shown stone ABOVE the anchor, or -1
 * when the anchor is the only one; there the stone's own height is the best
 * answer available and is what a single-stone column would use for its second.
 */
int
ToriRSLaneLauncher_Pitch(int anchor_y, int previous_y, int anchor_h);

/**
 * Where the launcher's top goes, parent-local.
 *
 * `bottom` is the lowest edge among the column's SHOWN stones, which is the
 * anchor's own bottom while the anchor is up and the stone above it once the
 * anchor is hidden. That is deliberate: the mobile chat toggle hides the
 * "Start chatting" stone and shrinks the column when the chatbox is put away,
 * and a launcher pinned to the anchor's laid-out box would then float in the
 * gap the stone left behind. Following the column's last SHOWN stone puts the
 * launcher exactly where the anchor used to be instead, and the one way into
 * the plugin window does not move because the chatbox was closed.
 */
int
ToriRSLaneLauncher_Y(int bottom, int anchor_h, int pitch);

/**
 * Compose the launcher's picture: the anchor's own backing, wearing the
 * client's wrench where the anchor wears its own glyph.
 *
 * `backing` (`backing_w` x `backing_h`, 0xAARRGGBB) is the picture the anchor's
 * backing child draws -- the lane's stone -- scaled nearest-neighbour to
 * `out_w` x `out_h`, which is the anchor's own box. Nearest-neighbour because
 * it is pixel art, and because on the lane it was written for the two sizes
 * are equal and every pixel is a copy.
 *
 * `icon` is drawn into (`icon_x`, `icon_y`, `icon_w`, `icon_h`) -- the box the
 * anchor's own glyph child occupies, relative to the anchor -- scaled to it
 * the same way and composited src-over, so a glyph with a soft edge keeps it.
 * Taking the box from the anchor rather than centring in the button is what
 * carries the cache's own one-pixel lift (`cc_setposition(0, -1, centre,
 * centre)` in torirs_osm_chatbox_bind) without this file knowing about it.
 *
 * No badge, unlike a pop-out nav button. A nav button is appended to a strip
 * of the CACHE's buttons and has to be tellable from them; this one is the
 * only button of its kind on the column, and a Tori face over a 24-pixel
 * wrench on a phone is a smudge.
 *
 * `out` holds out_w * out_h pixels. Every parameter is a contract: the caller
 * has already refused a box larger than the cap above.
 */
void
ToriRSLaneLauncher_Compose(
    uint32_t const* backing,
    int backing_w,
    int backing_h,
    uint32_t const* icon,
    int icon_w,
    int icon_h,
    int out_w,
    int out_h,
    int icon_x,
    int icon_y,
    int icon_box_w,
    int icon_box_h,
    uint32_t* out);

#endif /* SRC_UI_TORIRS_CHROME_LANE_LAUNCHER_H */
