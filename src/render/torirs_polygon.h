#ifndef SRC_RENDER_TORIRS_POLYGON_H
#define SRC_RENDER_TORIRS_POLYGON_H

/**
 * Convex polygon fill, decomposed to horizontal spans.
 *
 * Shared because the alternative is four copies of the same edge walk. Every
 * backend can already fill an axis-aligned run of pixels — that is the one
 * drawing operation all of them have — so the geometry is solved once here and
 * each backend supplies only the span, in whatever way is native to it: a row
 * of the framebuffer in the software rasteriser, a quad in the GL and D3D
 * paths.
 *
 * Convex only, and the caller is the one that knows: this walks a single left
 * and right edge per scanline, which a concave polygon breaks by having more
 * than one span on a row. Every caller here fills a convex hull, so the input
 * is convex by construction rather than by hope.
 */

#include <stdint.h>

/** Points one polygon run may carry. A convex hull of a projected model is a
 *  handful of vertices; 64 is far above anything a highlight produces and keeps
 *  the accumulator small enough to sit inside a renderer's state struct. */
#define TORIRS_POLYGON_MAX_POINTS 64

/**
 * One horizontal run of pixels: `count` pixels starting at (x, y).
 * Already clipped — a backend can write it without further checks.
 */
typedef void (*ToriRS_PolygonSpanFn)(void* user_data, int x, int y, int count);

/**
 * Fill a convex polygon by handing its spans to `span`.
 *
 * The scissor box is applied here rather than by the backend so that every
 * renderer clips identically; a highlight that bleeds past the world viewport
 * in one backend and not another is the kind of difference nobody looks for.
 *
 * Degenerate input draws nothing rather than asserting: fewer than three points
 * is a legitimate on-screen state (a hull seen edge-on), and a zero-area
 * polygon has no pixels by definition.
 */
void
ToriRS_PolygonFillConvex(
    const int* xs,
    const int* ys,
    int count,
    int clip_x,
    int clip_y,
    int clip_w,
    int clip_h,
    ToriRS_PolygonSpanFn span,
    void* user_data);

#endif
