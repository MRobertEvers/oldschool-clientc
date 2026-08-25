#ifndef TORIDRAW_RASTER_FACE_CENSUS_H
#define TORIDRAW_RASTER_FACE_CENSUS_H

/**
 * Which texture raster family does a real frame actually spend its pixels in?
 *
 * This exists to answer one question that had been inferred rather than
 * measured: the handrolled i686 texture span substitutes for exactly one
 * function, tex_span_opaque_lerp8_v3_ordered(), reached from exactly one
 * leaf drawer, ToriDraw_TriangleTextureBlendOpaque. Every other texture leaf
 * -- flat-shaded, transparent, and the four affine variants -- still runs C.
 * Deciding whether porting those to asm is worth the work needs their share
 * of textured pixels, and a wall-clock A/B can only infer it: the covered
 * share came out anywhere from 52% to 87% depending on which reps were kept,
 * because the whole asm saving is ~200 us against a frame that drifts by more
 * than that between runs.
 *
 * A counter does not have that problem. Faces and area are exact, they cost
 * nothing to attribute to the right bucket, and they are gated at compile
 * time -- -DTORIDRAW_FACE_CENSUS=1 -- so a shipping build has no branch here.
 *
 * Area, not faces, is the number to read. 59% of projected models have four
 * vertices and the span census already showed 64.7% of gouraud spans are <= 4
 * px, so face counts are dominated by tiny geometry that costs nothing to
 * fill; a single floor tile outweighs a hundred of them. Area is computed as
 * the screen-space cross product, halved, and clamped to the viewport's total
 * pixel count so one enormous near-clipped triangle cannot swamp the bucket
 * it lands in. It is a proxy: it ignores overdraw and counts backfaces the
 * rasterizer will reject. Read it as "share of fill work", not as a pixel
 * count.
 */

#if defined(TORIDRAW_FACE_CENSUS) && TORIDRAW_FACE_CENSUS

#include <stdio.h>
#include <stdlib.h>

enum
{
    TORIDRAW_FACE_CENSUS_TEX_BLEND_OPAQUE = 0, /**< the asm-covered leaf */
    TORIDRAW_FACE_CENSUS_TEX_FLAT_OPAQUE,
    TORIDRAW_FACE_CENSUS_TEX_BLEND_TRANS,
    TORIDRAW_FACE_CENSUS_TEX_FLAT_TRANS,
    TORIDRAW_FACE_CENSUS_TEX_BLEND_AFFINE_V3,
    TORIDRAW_FACE_CENSUS_TEX_BLEND_AFFINE,
    TORIDRAW_FACE_CENSUS_TEX_FLAT_AFFINE_V3,
    TORIDRAW_FACE_CENSUS_TEX_FLAT_AFFINE,
    TORIDRAW_FACE_CENSUS_GOURAUD,
    TORIDRAW_FACE_CENSUS_FLAT,
    TORIDRAW_FACE_CENSUS_BUCKETS
};

struct ToriDraw_FaceCensus
{
    double faces[TORIDRAW_FACE_CENSUS_BUCKETS];
    double area[TORIDRAW_FACE_CENSUS_BUCKETS];
    /* A dedicated flag, not "is bucket N still zero": the first face of a frame
     * can land in any bucket, and keying off one of them would re-register the
     * atexit handler on every face until the atexit table overflowed. */
    int registered;
};

extern struct ToriDraw_FaceCensus g_toridraw_face_census;

void
ToriDraw_FaceCensusDump(void);

static inline void
toridraw_face_census_record(
    int bucket, int x0, int y0, int x1, int y1, int x2, int y2, int cap)
{
    double cross;
    double area;

    if( !g_toridraw_face_census.registered )
    {
        g_toridraw_face_census.registered = 1;
        atexit(ToriDraw_FaceCensusDump);
    }

    /* Doubles because a near-clipped triangle's coordinates are not bounded by
     * the viewport, and this is measurement code where an overflow would be a
     * silently wrong answer rather than a crash. */
    cross = (double)(x1 - x0) * (double)(y2 - y0) -
            (double)(x2 - x0) * (double)(y1 - y0);
    area = (cross < 0.0 ? -cross : cross) * 0.5;
    if( cap > 0 && area > (double)cap )
        area = (double)cap;

    g_toridraw_face_census.faces[bucket] += 1.0;
    g_toridraw_face_census.area[bucket] += area;
}

#define TORIDRAW_FACE_CENSUS_RECORD(bucket, x0, y0, x1, y1, x2, y2, cap)                  \
    toridraw_face_census_record((bucket), (x0), (y0), (x1), (y1), (x2), (y2), (cap))

#else

#define TORIDRAW_FACE_CENSUS_RECORD(bucket, x0, y0, x1, y1, x2, y2, cap) ((void)0)

#endif

#endif
