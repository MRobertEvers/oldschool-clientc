#ifndef SRC_APP_APP_CAM_ARC_H
#define SRC_APP_APP_CAM_ARC_H

/*
 * CAM_MOVETO_ARC's path (deob class561): the circle through the start (x0, z0),
 * a via point and the end (x1, z1), swept from the start's angle to the end's
 * the way that passes the via point. Fine coordinates in, fine coordinates out.
 *
 * The centre is where the perpendicular bisectors of start-via and via-end
 * meet, computed as the deob computes it. Collinear points have no circle; the
 * deob centres it on the end with radius 0, so the path is the end point.
 */

#include <math.h>

struct App_CamArc
{
    double cx, cz, radius, a0, a1;
};

static inline void
App_CamArcFit(
    struct App_CamArc* arc,
    double x0,
    double z0,
    double vx,
    double vz,
    double x1,
    double z1)
{
    if( (vx - x0) * (z1 - vz) == (vz - z0) * (x1 - vx) )
    {
        arc->cx = x1;
        arc->cz = z1;
        arc->radius = 0.0;
        arc->a0 = 0.0;
        arc->a1 = 0.0;
        return;
    }
    {
        double mx0 = (x0 + vx) / 2.0;
        double mz0 = (z0 + vz) / 2.0;
        double mx1 = (x1 + vx) / 2.0;
        double mz1 = (z1 + vz) / 2.0;
        double s0 = (vx - x0) * -1.0 / (vz - z0);
        double s1 = (x1 - vx) * -1.0 / (z1 - vz);
        double av;

        arc->cx = (mx0 * s0 - mx1 * s1 + mz1 - mz0) / (s0 - s1);
        arc->cz = (arc->cx - mx0) * s0 + mz0;
        arc->radius = sqrt(pow(arc->cx - x0, 2.0) + pow(arc->cz - z0, 2.0));
        arc->a0 = atan2(z0 - arc->cz, x0 - arc->cx);
        av = atan2(vz - arc->cz, vx - arc->cx);
        arc->a1 = atan2(z1 - arc->cz, x1 - arc->cx);
        if( !((arc->a0 <= av && av <= arc->a1) || (arc->a1 <= av && av <= arc->a0)) )
            arc->a1 += M_PI * (arc->a0 - arc->a1 > 0.0 ? 2 : -2);
    }
}

/* Java's (int) Math.round(double): floor(v + 0.5) saturated to a long, NaN to
 * 0, then narrowed. A start-to-via or via-to-end chord with no z change makes
 * the deob's slope infinite and its centre NaN; the eye then lands on 0, 0,
 * and this does what the client does rather than what C would. */
static inline int
App_CamArcJavaRound(double v)
{
    double r;

    if( v != v )
        return 0;
    r = floor(v + 0.5);
    if( r >= 9223372036854775807.0 )
        return (int)(long long)9223372036854775807LL;
    if( r <= -9223372036854775808.0 )
        return (int)(long long)(-9223372036854775807LL - 1);
    return (int)(long long)r;
}

/** The point `f` (eased, 0..1) along the arc, rounded as the deob rounds it. */
static inline void
App_CamArcAt(
    struct App_CamArc const* arc,
    double f,
    int* x,
    int* z)
{
    double a = (arc->a1 - arc->a0) * f + arc->a0;

    *x = App_CamArcJavaRound(arc->cx + arc->radius * cos(a));
    *z = App_CamArcJavaRound(arc->cz + arc->radius * sin(a));
}

#endif
