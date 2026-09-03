#ifndef TORIRS_PLUGIN_PLACEMENT_H
#define TORIRS_PLUGIN_PLACEMENT_H

/*
 * Exact, engine-independent placement geometry.
 *
 * A safe area is generally not one rectangle: taking a chatbox, rail, or
 * floating panel out of a viewport leaves several disconnected pieces. This
 * module keeps those pieces instead of choosing one and discarding the rest.
 * It owns no engine state and allocates no memory, so a frame builder and the
 * eventual plugin API can use the same arithmetic.
 */

#include <stdbool.h>

/**
 * Rectangles retained by one region.
 *
 * The ceiling makes every operation and its storage cost bounded. An
 * operation whose canonical exact answer needs more rectangles returns
 * TORIRS_PLACEMENT_LIMIT and leaves its output untouched; it never substitutes
 * a lossy largest rectangle.
 */
#define TORIRS_PLACEMENT_REGION_RECTS_MAX 64

struct ToriRS_PlacementRect
{
    int x;
    int y;
    int w;
    int h;
};

/**
 * A canonical union of non-overlapping, positive-area horizontal bands.
 *
 * Treat the fields as private. Use Clear/SetRect/AddRect to create a region
 * and RectCount/RectAt to inspect it. Geometry operations normalize their
 * results into a stable top-to-bottom, left-to-right order independent of
 * input declaration order or rectangle decomposition.
 */
struct ToriRS_PlacementRegion
{
    int _rect_count;
    struct ToriRS_PlacementRect _rects[TORIRS_PLACEMENT_REGION_RECTS_MAX];
};

enum ToriRS_PlacementStatus
{
    TORIRS_PLACEMENT_OK = 0,
    TORIRS_PLACEMENT_LIMIT,
};

/** Nine positions inside a region, in ordinary screen coordinates. */
enum ToriRS_PlacementAnchor
{
    TORIRS_PLACEMENT_ANCHOR_TOP_LEFT = 0,
    TORIRS_PLACEMENT_ANCHOR_TOP,
    TORIRS_PLACEMENT_ANCHOR_TOP_RIGHT,
    TORIRS_PLACEMENT_ANCHOR_LEFT,
    TORIRS_PLACEMENT_ANCHOR_CENTER,
    TORIRS_PLACEMENT_ANCHOR_RIGHT,
    TORIRS_PLACEMENT_ANCHOR_BOTTOM_LEFT,
    TORIRS_PLACEMENT_ANCHOR_BOTTOM,
    TORIRS_PLACEMENT_ANCHOR_BOTTOM_RIGHT,

    TORIRS_PLACEMENT_ANCHOR_COUNT
};

/**
 * Whether a rectangle has non-negative dimensions and a representable right
 * and bottom edge. Zero-area rectangles are valid empty geometry.
 */
bool
ToriRS_PlacementRect_IsValid(struct ToriRS_PlacementRect const* rect);

/** Set `region` to the empty set. */
void
ToriRS_PlacementRegion_Clear(struct ToriRS_PlacementRegion* region);

/** Set `region` to `rect`, or to empty when either dimension is zero. */
void
ToriRS_PlacementRegion_SetRect(
    struct ToriRS_PlacementRegion* region,
    struct ToriRS_PlacementRect const* rect);

/**
 * Add `rect` to the region as an exact union. Overlap is accepted and counted
 * once. On TORIRS_PLACEMENT_LIMIT the region is unchanged.
 */
enum ToriRS_PlacementStatus
ToriRS_PlacementRegion_AddRect(
    struct ToriRS_PlacementRegion* region,
    struct ToriRS_PlacementRect const* rect);

/** The number of fragments in normalized iteration order. */
int
ToriRS_PlacementRegion_RectCount(struct ToriRS_PlacementRegion const* region);

/** Copy fragment `index`; `index` must be in [0, RectCount). */
void
ToriRS_PlacementRegion_RectAt(
    struct ToriRS_PlacementRegion const* region,
    int index,
    struct ToriRS_PlacementRect* out_rect);

/**
 * The exact intersection of `a` and `b`.
 *
 * `out` may alias either input. On TORIRS_PLACEMENT_LIMIT it is unchanged.
 */
enum ToriRS_PlacementStatus
ToriRS_PlacementRegion_Intersect(
    struct ToriRS_PlacementRegion const* a,
    struct ToriRS_PlacementRegion const* b,
    struct ToriRS_PlacementRegion* out);

/**
 * The exact result of taking one rectangle out of `source`.
 *
 * `out` may alias `source`. On TORIRS_PLACEMENT_LIMIT it is unchanged.
 */
enum ToriRS_PlacementStatus
ToriRS_PlacementRegion_SubtractRect(
    struct ToriRS_PlacementRegion const* source,
    struct ToriRS_PlacementRect const* cut,
    struct ToriRS_PlacementRegion* out);

/**
 * The exact result of taking the union of `cuts` out of `source`.
 *
 * The union of `cuts` is removed as one set, so declaration order and
 * decomposition do not change the answer. `out` may alias either input. On
 * TORIRS_PLACEMENT_LIMIT it is unchanged.
 */
enum ToriRS_PlacementStatus
ToriRS_PlacementRegion_Subtract(
    struct ToriRS_PlacementRegion const* source,
    struct ToriRS_PlacementRegion const* cuts,
    struct ToriRS_PlacementRegion* out);

/** True when every point of `rect` is in the region. Empty rectangles fit. */
bool
ToriRS_PlacementRegion_ContainsRect(
    struct ToriRS_PlacementRegion const* region,
    struct ToriRS_PlacementRect const* rect);

/** True when two regions cover exactly the same points. */
bool
ToriRS_PlacementRegion_Equals(
    struct ToriRS_PlacementRegion const* a,
    struct ToriRS_PlacementRegion const* b);

/**
 * Place a positive-size box in one fragment of `region`.
 *
 * `margin` is required on all four sides inside the chosen fragment. When
 * several fragments fit, the one whose requested anchor is nearest the same
 * anchor of the whole region wins; ties use top-to-bottom, left-to-right
 * order. Returns false, leaving `out_rect` untouched, when no fragment fits.
 */
bool
ToriRS_PlacementRegion_Place(
    struct ToriRS_PlacementRegion const* region,
    int anchor,
    int width,
    int height,
    int margin,
    struct ToriRS_PlacementRect* out_rect);

#endif /* TORIRS_PLUGIN_PLACEMENT_H */
