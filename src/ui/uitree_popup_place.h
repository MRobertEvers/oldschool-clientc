#ifndef SRC_UI_UITREE_POPUP_PLACE_H
#define SRC_UI_UITREE_POPUP_PLACE_H

/**
 * Where a popup panel goes when it is opened beside the row that opened it.
 *
 * The All Settings colour picker and its number-entry twin both do this, with
 * the same rules and two different numbers. Placement is pure geometry, so it
 * is here where it can be checked, rather than twice in the middle of two
 * chrome-building functions where it cannot.
 *
 * Two things about the rules are not obvious and both come from watching the
 * thing work:
 *
 *   - the popup sits to the LEFT of its anchor. A settings row's swatch or
 *     field is docked on the panel's right edge, and a popup covering it would
 *     hide the one thing the player is watching change.
 *   - the bottom clamp is TIGHTER than the canvas. The colour picker's axis
 *     popup drops BELOW the panel, so the panel needs room under itself as
 *     well as for itself; the number entry needs less, which is why the
 *     fraction is a parameter and not a constant.
 *
 * With no anchor -- the row was destroyed, or the request named no component
 * -- the popup is centred horizontally and a quarter of the way down, which is
 * where a dialogue with nothing to point at belongs.
 */

struct UIPopupPlacement
{
    int x;
    int y;
};

/**
 * `anchor_x`/`anchor_y` are the anchor component's absolute top-left, and
 * `have_anchor` is false when there is none.
 *
 * `gap` is the space left between the popup and its anchor, already scaled.
 * `bottom_limit_numerator`/`_denominator` give the lowest the popup's top may
 * sit as a fraction of the canvas height -- two thirds for the colour picker,
 * three quarters for the number entry.
 */
struct UIPopupPlacement
UITree_PlacePopupBesideAnchor(
    int canvas_w,
    int canvas_h,
    int popup_w,
    int have_anchor,
    int anchor_x,
    int anchor_y,
    int gap,
    int bottom_limit_numerator,
    int bottom_limit_denominator);

#endif /* SRC_UI_UITREE_POPUP_PLACE_H */
