#ifndef SRC_RENDER_TORIRS_ARC_H
#define SRC_RENDER_TORIRS_ARC_H

/**
 * Widget type 10 (CC/IF_SETARC): a circular sector, as horizontal runs.
 *
 * The reference draws one with NXTPix2D::DrawCircularArc, which is an ANNULUS
 * sector -- it carries an inner radius as well as an outer one, and a filled
 * arc is just the case where the inner radius is zero. That is why one shape
 * serves both of clientscript 5480's needs: `cc_setfill(true)` gives the whole
 * disc and `cc_setfill(false)` with `cc_setlinewid(1)` a one-pixel band along
 * the arc, with no second primitive and no second code path.
 *
 * Spans rather than a rasteriser: every backend can already fill an
 * axis-aligned run (torirs_polygon.h makes the same argument), so the geometry
 * is solved once here and the frame translator hands each run out as an
 * ordinary FILL_RECT. Nothing below the emit layer learns what an arc is.
 *
 * Rows are asked for one at a time rather than accumulated into a list, because
 * the frame emitter produces exactly one command per step and a cached list
 * would need a cap -- and a cap on a primitive's own geometry is the kind of
 * silent truncation that draws a plausible wrong picture.
 */

struct ToriRS_ArcShape
{
    /** Widget box in absolute screen pixels. The sector is centred in it and
     *  its radius is half the shorter side. */
    int x;
    int y;
    int w;
    int h;
    /** Start and end angle, 65536 to a full turn, 0 straight up, clockwise.
     *  end <= start draws nothing; a sweep of a full turn or more is a disc. */
    int arc_start;
    int arc_end;
    /** 1 = the whole disc; 0 = a `line_width`-pixel band along the arc. */
    int filled;
    int line_width;
};

/** One horizontal run: `w` pixels starting at (x, y). */
struct ToriRS_ArcSpan
{
    int x;
    int y;
    int w;
};

/** Rows the shape spans, i.e. the valid range of `row` below (0 if degenerate). */
int
ToriRS_ArcRowCount(struct ToriRS_ArcShape const* arc);

/**
 * The runs of row `row` (0 = the top row of the circle), into `out`.
 *
 * At most four, and four is a bound rather than a budget: on one scanline the
 * annulus is at most two intervals and the sector at most two (it is a union of
 * half-planes once the sweep passes a half turn), so their intersection cannot
 * be more.
 */
#define TORIRS_ARC_ROW_SPANS_MAX 4

int
ToriRS_ArcRowSpans(
    struct ToriRS_ArcShape const* arc,
    int row,
    struct ToriRS_ArcSpan* out);

#endif
