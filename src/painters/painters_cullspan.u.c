/* PaintersCullSpan: per-frame analytic frustum → convex trapezoid in tile
 * space, stored as [dx_min, dx_max] per eye-relative dz row.
 *
 * Included from painters.c — do not compile as a separate translation unit.
 *
 * Derivation (camera at origin, pitch/yaw applied, perspective screen =
 * (coord * focal) / z with focal = 512 * cot(fov/2)):
 *   u = X cosYaw + Z sinYaw
 *   v = Z cosYaw - X sinYaw
 *   Y sampled in [Y0, Y1] = eye_height + [y_lo, y_hi]
 * Four half-planes bound the visible set; per-row they collapse to a span.
 * Outward rounding + one-tile dilation keeps the result a superset of the
 * discrete 4-corner × Y-sweep test used by the baked cullmap. */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static inline int
pcullspan_row_index(const struct PaintersCullSpan* span, int dz)
{
    return dz - span->dz_min;
}

/** Query one eye-relative row. Returns 0 if the row is empty / out of range. */
static int
painters_cullspan_row(
    const struct PaintersCullSpan* span,
    int dz,
    int* lo_out,
    int* hi_out)
{
    int idx;
    assert(span);
    assert(lo_out);
    assert(hi_out);
    if( span->empty || dz < span->dz_min || dz > span->dz_max )
        return 0;
    idx = pcullspan_row_index(span, dz);
    if( span->row_min[idx] > span->row_max[idx] )
        return 0;
    *lo_out = (int)span->row_min[idx];
    *hi_out = (int)span->row_max[idx];
    return 1;
}

/** True when the (dx, dz) tile is inside the span (inclusive). */
static int
painters_cullspan_visible(
    const struct PaintersCullSpan* span,
    int dx,
    int dz)
{
    int lo;
    int hi;
    if( !painters_cullspan_row(span, dz, &lo, &hi) )
        return 0;
    return dx >= lo && dx <= hi;
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void
painters_cullspan_set_all_visible(
    struct PaintersCullSpan* span,
    int dz_min,
    int dz_max,
    int dx_radius)
{
    int dz;
    assert(span);
    if( dz_min < -PAINTERS_CULLSPAN_MAX_DZ )
        dz_min = -PAINTERS_CULLSPAN_MAX_DZ;
    if( dz_max > PAINTERS_CULLSPAN_MAX_DZ )
        dz_max = PAINTERS_CULLSPAN_MAX_DZ;
    if( dz_min > dz_max )
    {
        span->empty = 1;
        span->dz_min = 0;
        span->dz_max = -1;
        return;
    }
    span->empty = 0;
    span->dz_min = dz_min;
    span->dz_max = dz_max;
    for( dz = dz_min; dz <= dz_max; dz++ )
    {
        int idx = pcullspan_row_index(span, dz);
        span->row_min[idx] = (int16_t)(-dx_radius);
        span->row_max[idx] = (int16_t)dx_radius;
    }
}

void
painters_cullspan_build(
    struct PaintersCullSpan* span,
    const struct PaintersCullSpanParams* p)
{
    double pitch_rad;
    double yaw_rad;
    double sP;
    double cP;
    double sY;
    double cY;
    double half_fov;
    double focal;
    double a;
    double b;
    double Y0;
    double Y1;
    double vlo;
    double vhi;
    double k;
    double den;
    double hp_A[4];
    double hp_B[4];
    double hp_C[4];
    double pad;
    int dz_min;
    int dz_max;
    int dx_limit;
    int i;
    int dz;

    assert(span);
    assert(p);

    memset(span, 0, sizeof(*span));

    dz_min = p->dz_min;
    dz_max = p->dz_max;
    if( dz_min < -PAINTERS_CULLSPAN_MAX_DZ )
        dz_min = -PAINTERS_CULLSPAN_MAX_DZ;
    if( dz_max > PAINTERS_CULLSPAN_MAX_DZ )
        dz_max = PAINTERS_CULLSPAN_MAX_DZ;
    if( dz_min > dz_max || p->screen_width < 1 || p->screen_height < 1 ||
        p->near_clip < 1 || p->far_clip <= p->near_clip )
    {
        span->empty = 1;
        span->dz_min = 0;
        span->dz_max = -1;
        return;
    }

    /* Generous dx radius for the all-visible fallback / span clamp. */
    dx_limit = PAINTERS_CULLSPAN_MAX_DZ;
    if( p->screen_width > 0 )
    {
        int guess = (p->dz_max - p->dz_min) + 4;
        if( guess > dx_limit )
            dx_limit = guess;
        if( dx_limit > PAINTERS_CULLSPAN_MAX_DZ )
            dx_limit = PAINTERS_CULLSPAN_MAX_DZ;
    }

    pitch_rad = (double)p->pitch * (2.0 * M_PI / 2048.0);
    yaw_rad = (double)p->yaw * (2.0 * M_PI / 2048.0);
    sP = sin(pitch_rad);
    cP = cos(pitch_rad);
    sY = sin(yaw_rad);
    cY = cos(yaw_rad);

    /* Degenerate look-direction (cosP ~ 0): keep the whole box. */
    if( cP <= 1e-6 )
    {
        painters_cullspan_set_all_visible(span, dz_min, dz_max, dx_limit);
        return;
    }

    {
        int fov = p->fov_rpi2048;
        if( fov < 1 )
            fov = 512;
        half_fov = ((double)fov * 0.5) * (2.0 * M_PI / 2048.0);
        /* Match project_perspective_fast_trig: screen = x * (512*cot(half)) / z. */
        if( fabs(tan(half_fov)) < 1e-9 )
        {
            painters_cullspan_set_all_visible(span, dz_min, dz_max, dx_limit);
            return;
        }
        focal = 512.0 / tan(half_fov);
    }

    a = ((double)p->screen_width * 0.5) / focal;
    b = ((double)p->screen_height * 0.5) / focal;

    Y0 = (double)p->eye_height + (double)p->y_lo;
    Y1 = (double)p->eye_height + (double)p->y_hi;
    if( Y1 < Y0 )
    {
        double tmp = Y0;
        Y0 = Y1;
        Y1 = tmp;
    }

    /* Near / far → v bounds (use extremal Y for a conservative outer hull). */
    vlo = ((double)p->near_clip - Y1 * sP) / cP;
    vhi = ((double)p->far_clip - Y0 * sP) / cP;

    /* Bottom screen edge: y_screen = h/2  →  Y cosP - v sinP = b * (Y sinP + v cosP)
     * rearranged to v >= k * Y, take the Y that loosens the bound. */
    k = (cP - b * sP) / (sP + b * cP);
    {
        double y_for_k = (k > 0.0) ? Y0 : Y1;
        double v_edge = k * y_for_k;
        if( v_edge > vlo )
            vlo = v_edge;
    }

    /* Top screen edge. */
    den = sP - b * cP;
    if( den > 1e-9 )
    {
        double coef = cP + b * sP;
        double y_for_top = (coef > 0.0) ? Y1 : Y0;
        double v_top = y_for_top * coef / den;
        if( v_top < vhi )
            vhi = v_top;
    }

    if( vlo > vhi )
    {
        span->empty = 1;
        span->dz_min = 0;
        span->dz_max = -1;
        return;
    }

    /* Half-planes A*u + B*v <= C, then rotate into (X, Z).
     * (0,-1,-vlo):  -v <= -vlo  → v >= vlo
     * (0, 1, vhi):   v <= vhi
     * (1,-a cP, a sP Y1):  u - a cP v <= a sP Y1
     * (-1,-a cP, a sP Y1): -u - a cP v <= a sP Y1
     */
    {
        double Au[4] = { 0.0, 0.0, 1.0, -1.0 };
        double Bu[4] = { -1.0, 1.0, -a * cP, -a * cP };
        double Cu[4] = { -vlo, vhi, a * sP * Y1, a * sP * Y1 };
        for( i = 0; i < 4; i++ )
        {
            /* u = X cY + Z sY ; v = Z cY - X sY
             * A u + B v = X (A cY - B sY) + Z (A sY + B cY) */
            hp_A[i] = Au[i] * cY - Bu[i] * sY;
            hp_B[i] = Au[i] * sY + Bu[i] * cY;
            hp_C[i] = Cu[i];
        }
    }

    /* One-tile dilation in world units (matches bake's 3x3 OR neighborhood). */
    pad = 128.0;

    span->empty = 0;
    span->dz_min = dz_min;
    span->dz_max = dz_max;

    for( dz = dz_min; dz <= dz_max; dz++ )
    {
        double Z = (double)dz * 128.0;
        double lo = -(double)dx_limit * 128.0;
        double hi = (double)dx_limit * 128.0;
        int empty_row = 0;

        for( i = 0; i < 4; i++ )
        {
            double A = hp_A[i];
            double B = hp_B[i];
            double C = hp_C[i];
            /* Dilate: A X + B Z <= C + pad*(|A|+|B|) */
            double r = C + pad * (fabs(A) + fabs(B)) - B * Z;
            if( fabs(A) < 1e-12 )
            {
                if( r < 0.0 )
                {
                    empty_row = 1;
                    break;
                }
            }
            else if( A > 0.0 )
            {
                double bound = r / A;
                if( bound < hi )
                    hi = bound;
            }
            else
            {
                double bound = r / A;
                if( bound > lo )
                    lo = bound;
            }
        }

        {
            int idx = pcullspan_row_index(span, dz);
            if( empty_row || lo > hi )
            {
                span->row_min[idx] = 1;
                span->row_max[idx] = 0; /* empty */
            }
            else
            {
                int ilo = (int)floor(lo / 128.0);
                int ihi = (int)ceil(hi / 128.0);
                if( ilo < -dx_limit )
                    ilo = -dx_limit;
                if( ihi > dx_limit )
                    ihi = dx_limit;
                span->row_min[idx] = (int16_t)ilo;
                span->row_max[idx] = (int16_t)ihi;
            }
        }
    }
}
