#include "torirs_arc.h"

#include <assert.h>
#include <math.h>

/* A full turn in the cache's angle units (CC_SETARC). */
#define ARC_TURN 65536

/* Fixed-point scale for the two boundary directions. Only their SIGN matters
 * once they are crossed with a pixel offset, so this only has to be fine enough
 * that a boundary lands on the right pixel; 14 bits puts the worst-case angular
 * error four orders of magnitude below one pixel on a 25-pixel pie. */
#define ARC_DIR_SCALE 16384

/* Spelled out rather than taken from math.h: M_PI is not in C11 proper, and the
 * Windows lane's headers hide it without _USE_MATH_DEFINES. */
#define ARC_PI 3.14159265358979323846

struct arc_geom
{
    int cx;
    int cy;
    int r;
    int ri;
    /* Boundary directions in screen space (y down): angle 0 is (0, -1) and the
     * sweep runs clockwise, so angle t is (sin t, -cos t). */
    int sx;
    int sy;
    int ex;
    int ey;
    /* A sweep past a half turn is the UNION of the two half-planes rather than
     * their intersection -- the sector is then the larger of the two pieces the
     * boundary rays cut the disc into. */
    int wide;
    int whole;
    int empty;
};

static void
arc_geom_build(
    struct ToriRS_ArcShape const* arc,
    struct arc_geom* g)
{
    int sweep;
    double a;
    double b;

    assert(arc);
    assert(g);

    g->r = (arc->w < arc->h ? arc->w : arc->h) / 2;
    g->cx = arc->x + arc->w / 2;
    g->cy = arc->y + arc->h / 2;

    if( arc->filled )
    {
        g->ri = 0;
    }
    else
    {
        int const width = arc->line_width > 0 ? arc->line_width : 1;
        g->ri = g->r - width;
        if( g->ri < 0 )
            g->ri = 0;
    }

    sweep = arc->arc_end - arc->arc_start;
    g->empty = sweep <= 0 || g->r <= 0;
    g->whole = sweep >= ARC_TURN;
    g->wide = sweep > ARC_TURN / 2;

    a = (double)arc->arc_start * (2.0 * ARC_PI / (double)ARC_TURN);
    b = (double)arc->arc_end * (2.0 * ARC_PI / (double)ARC_TURN);
    g->sx = (int)(sin(a) * ARC_DIR_SCALE);
    g->sy = (int)(-cos(a) * ARC_DIR_SCALE);
    g->ex = (int)(sin(b) * ARC_DIR_SCALE);
    g->ey = (int)(-cos(b) * ARC_DIR_SCALE);
}

/* Is (dx, dy), an offset from the centre, inside the swept sector? */
static int
arc_in_sector(
    struct arc_geom const* g,
    int dx,
    int dy)
{
    long long after_start;
    long long before_end;

    if( g->whole )
        return 1;

    /* Cross products against the two boundary rays. Positive is clockwise-of
     * in screen space, which is the direction the sweep runs. */
    after_start = (long long)g->sx * dy - (long long)g->sy * dx;
    before_end = (long long)dx * g->ey - (long long)dy * g->ex;

    if( g->wide )
        return after_start >= 0 || before_end >= 0;
    return after_start >= 0 && before_end >= 0;
}

int
ToriRS_ArcRowCount(struct ToriRS_ArcShape const* arc)
{
    struct arc_geom g;

    assert(arc);
    arc_geom_build(arc, &g);
    if( g.empty )
        return 0;
    return 2 * g.r + 1;
}

int
ToriRS_ArcRowSpans(
    struct ToriRS_ArcShape const* arc,
    int row,
    struct ToriRS_ArcSpan* out)
{
    struct arc_geom g;
    int dy;
    int count = 0;
    int run_start = 0;
    int in_run = 0;

    assert(arc);
    assert(out);

    arc_geom_build(arc, &g);
    if( g.empty || row < 0 || row > 2 * g.r )
        return 0;

    dy = row - g.r;

    /* One pass across the row, coalescing lit pixels into runs. The per-pixel
     * test is the reference's own shape (DrawCircleHelper tests every pixel of
     * every span against the two boundary rays); at these radii the whole row
     * costs less than working out where the boundaries cross it. */
    for( int dx = -g.r; dx <= g.r + 1; dx++ )
    {
        int lit = 0;
        if( dx <= g.r )
        {
            int const d2 = dx * dx + dy * dy;
            lit = d2 <= g.r * g.r && d2 >= g.ri * g.ri && arc_in_sector(&g, dx, dy);
        }

        if( lit && !in_run )
        {
            in_run = 1;
            run_start = dx;
        }
        else if( !lit && in_run )
        {
            in_run = 0;
            /* Not a cap: four is what the geometry allows (see the header), so
             * a fifth run means the sector test is wrong, and truncating would
             * hide that behind a picture that still looks like a pie. */
            assert(count < TORIRS_ARC_ROW_SPANS_MAX);
            out[count].x = g.cx + run_start;
            out[count].y = g.cy + dy;
            out[count].w = dx - run_start;
            count++;
        }
    }

    return count;
}
