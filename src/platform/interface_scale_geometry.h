#ifndef TORIRS_INTERFACE_SCALE_GEOMETRY_H
#define TORIRS_INTERFACE_SCALE_GEOMETRY_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

/* Fixed frames have historically presented one logical pixel per window
 * point at 100%, including HighDPI displays. Window-following canvases instead
 * use drawable pixels. Preserve each policy's existing 100% presentation and
 * apply the user's percentage to it. */
static inline int
ToriRS_InterfaceScaleWindowPoints(int canvas_px, int percent, int density, bool fixed)
{
    int64_t denominator;

    assert(canvas_px > 0);
    assert(percent > 0);
    assert(density > 0);
    denominator = (int64_t)100 * (fixed ? 1 : density);
    return (int)(((int64_t)canvas_px * percent + denominator - 1) / denominator);
}

#endif
