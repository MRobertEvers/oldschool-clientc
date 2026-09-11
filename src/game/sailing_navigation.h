#ifndef SAILING_NAVIGATION_H
#define SAILING_NAVIGATION_H

#include <assert.h>
#include <math.h>

/* Revision-239 class108: choose the nearest of sixteen 128-unit sectors.
 * Coordinates are root fine units: south=0, west=4, north=8, east=12. */
static inline int SailingNavigation_Heading(double dx, double dz)
{
    assert(dx != 0.0 || dz != 0.0);
    double angle = atan2(-dx, -dz) * (2048.0 / 6.28318530717958647693);
    return ((int)floor((angle + 64.0 + 2048.0) / 128.0)) & 15;
}

#endif
