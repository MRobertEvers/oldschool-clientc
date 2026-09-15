#include "ui/uitree_popup_place.h"

#include <assert.h>

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
    int bottom_limit_denominator)
{
    struct UIPopupPlacement placement;

    assert(bottom_limit_denominator > 0);

    /* No anchor: centred, a quarter down. */
    placement.x = (canvas_w - popup_w) / 2;
    placement.y = canvas_h / 4;

    if( have_anchor )
    {
        /* LEFT of the anchor -- see the header. */
        placement.x = anchor_x - popup_w - gap;
        placement.y = anchor_y - gap;
    }

    if( placement.x < 0 )
        placement.x = 0;
    if( placement.y < 0 )
        placement.y = 0;
    if( placement.x + popup_w > canvas_w )
        placement.x = canvas_w - popup_w;
    if( placement.y > canvas_h * bottom_limit_numerator / bottom_limit_denominator )
        placement.y = canvas_h * bottom_limit_numerator / bottom_limit_denominator;
    return placement;
}
