#ifndef TORIDRAW_GRAPHICS_CONVEX_HULL_H
#define TORIDRAW_GRAPHICS_CONVEX_HULL_H

/**
 * Convex hull of a screen-space point set (Graham scan).
 *
 * What it is for: outlining a thing on screen with ONE closed polygon instead
 * of one box per piece it is made of. A 3x3 loc outlined tile by tile draws
 * nine overlapping quads with every internal edge showing; the hull of the same
 * corners is the silhouette, which is what a highlight is supposed to be.
 *
 * Ported from v0/graphics/convex_hull.u.c — its siblings there (alpha.h,
 * clamp.h, projection) are this folder — with three changes that matter here:
 *
 *   - Integer coordinates. These are screen pixels. Floats made the collinear
 *     test depend on rounding, and a hull that keeps or drops a collinear point
 *     depending on the camera angle flickers along straight edges.
 *   - No allocation. The v0 version malloc'd an index array per call; this runs
 *     per highlighted loc per frame, and it has a hard input cap instead.
 *   - Iterative sort. The v0 quicksort recursed, and its partition could
 *     recurse once per point on an already-sorted input — which axis-aligned
 *     footprints, the exact input here, produce.
 *
 * Degenerate input is not an error and does not abort: fewer than three points,
 * or points that are all collinear, are legitimate on screen (a loc seen
 * edge-on, a footprint one tile wide at a shallow camera pitch). Those come
 * back as the 1- or 2-point "hull" they are, and a caller draws that as a dot
 * or a line. Contract violations — a NULL array, a count past the cap — do
 * abort.
 */

#include <stddef.h>

/**
 * Most points one call accepts.
 *
 * The callers are footprint outlines: four corners per tile, and the largest
 * loc footprints in the cache are a handful of tiles. 256 is far above that and
 * keeps the scratch inside one cache line's worth of indices per 64 points.
 */
#define TORIDRAW_CONVEX_HULL_MAX_POINTS 256

/**
 * Hull of `count` points, counter-clockwise, starting at the lowest-then-
 * leftmost point.
 *
 * `out_x` / `out_y` must hold `count` entries: a point set already in convex
 * position is its own hull, so the output is not smaller in the worst case.
 * Input and output may not overlap.
 *
 * @return number of hull points written (0 when count is 0).
 */
int
ToriDraw_ConvexHull(
    const int* x,
    const int* y,
    int count,
    int* out_x,
    int* out_y);

#endif
