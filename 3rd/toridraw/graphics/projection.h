#ifndef PROJECTION_H
#define PROJECTION_H

#include "graphics/shared_tables.h"

#define UNIT_SCALE_SHIFT (9)
#define SCALE_UNIT(x) ((((long long)x) << UNIT_SCALE_SHIFT))
#define UNIT_SCALE ((1 << UNIT_SCALE_SHIFT))

/*
 * Projection scale, and the two ways to spell it.
 *
 * The reference client has no field of view. It projects
 *
 *     screen = coord * scale / z
 *
 * with `scale` a plain integer recomputed on every layout from the world
 * viewport HEIGHT (class159.method5357 -> client.field817, read back through
 * client.getScale()). UNIT_SCALE is that same multiplier: SCALE_UNIT(x) / z
 * *is* the reference projection at scale 512, and 512 is where the reference
 * sits for a 334-high viewport at zoom 1.0.
 *
 * The kernels take `camera_cot16`, a 16.16 multiplier on top of UNIT_SCALE:
 *
 *     effective_scale = camera_cot16 * UNIT_SCALE / 65536 = camera_cot16 >> 7
 *
 * Both spellings are configurable and both are stored on the camera. Which one
 * is live is stated by proj_mode, NOT inferred from a zero sentinel -- two
 * fields racing to define one quantity through "0 means use the other" is how
 * a camera ends up projecting with a value nobody set.
 *
 *   TORIDRAW_PROJ_MODE_SCALE  proj_scale is authoritative. Exact, and the only
 *                             way to match a reference projection.
 *   TORIDRAW_PROJ_MODE_FOV    fov_rpi2048 is authoritative. An angle, resolved
 *                             through cot(fov/2). Natural for a free camera.
 *
 * The angle is lossy as a way to request a scale, which is why the default mode
 * is SCALE: the conversion opens with `fov >> 1`, so only 1024 of the 2048
 * angles are distinct and the reachable scales step by ~1.7 near scale 190 and
 * ~2.6 near 410. Most integer scales are not on that ladder -- including the
 * 191 the reference lands on at a 503-high viewport (nearest 190.31, 192.09).
 *
 * toridraw_proj_scale_from_fov / toridraw_proj_fov_from_scale convert between
 * them for callers that need to move a value from one spelling to the other.
 */
#define TORIDRAW_PROJ_COT16_SHIFT (16 - UNIT_SCALE_SHIFT)

/** The scale the reference defaults to, and what SCALE_UNIT alone produces. */
#define TORIDRAW_PROJ_SCALE_DEFAULT UNIT_SCALE

/** The angle whose cotangent is ~1.0, i.e. the fov spelling of scale 512. */
#define TORIDRAW_PROJ_FOV_DEFAULT (512)

/* Valid fov domain. cot(fov/2) is only positive while fov/2 < 90 degrees, i.e.
 * fov_half < 512 in these units. Past that the tan table returns a NEGATIVE
 * cotangent and the projection mirrors: fov 1200 resolves to scale -142, which
 * draws the world inside out rather than failing. Angles are clamped into the
 * domain here so no caller can reach that by arithmetic accident. */
#define TORIDRAW_PROJ_FOV_MIN (2)
#define TORIDRAW_PROJ_FOV_MAX (1022)

enum ToriDraw_ProjMode
{
    /** proj_scale drives the projection. Default. */
    TORIDRAW_PROJ_MODE_SCALE = 0,
    /** fov_rpi2048 drives the projection. */
    TORIDRAW_PROJ_MODE_FOV = 1,
};

/** Exact: cot16 such that the kernels project coord * scale / z. */
static inline int
toridraw_proj_cot16_from_scale(int proj_scale)
{
    return (proj_scale > 0 ? proj_scale : TORIDRAW_PROJ_SCALE_DEFAULT)
           << TORIDRAW_PROJ_COT16_SHIFT;
}

/** Lossy; see the ladder note above. Angle is in units of 2*pi/2048, clamped
 *  to [TORIDRAW_PROJ_FOV_MIN, TORIDRAW_PROJ_FOV_MAX]. */
static inline int
toridraw_proj_cot16_from_fov(int fov_rpi2048)
{
    if( fov_rpi2048 < 1 )
        fov_rpi2048 = TORIDRAW_PROJ_FOV_DEFAULT;
    if( fov_rpi2048 < TORIDRAW_PROJ_FOV_MIN )
        fov_rpi2048 = TORIDRAW_PROJ_FOV_MIN;
    if( fov_rpi2048 > TORIDRAW_PROJ_FOV_MAX )
        fov_rpi2048 = TORIDRAW_PROJ_FOV_MAX;
    return g_tan_table[1536 - (fov_rpi2048 >> 1)];
}

/**
 * Resolve the camera's projection knobs to the single value the kernels want.
 * Takes ints rather than the camera so this header stays free of toridraw_types.
 * proj_mode selects; the unselected field is ignored, never consulted.
 */
static inline int
toridraw_proj_cot16(int proj_mode, int proj_scale, int fov_rpi2048)
{
    if( proj_mode == TORIDRAW_PROJ_MODE_FOV )
        return toridraw_proj_cot16_from_fov(fov_rpi2048);
    return toridraw_proj_cot16_from_scale(proj_scale);
}

/** The linear scale a cot16 realises -- for logging and assertions, so a
 *  reported scale is measured rather than assumed. */
static inline int
toridraw_proj_scale_from_cot16(int cot16)
{
    return cot16 >> TORIDRAW_PROJ_COT16_SHIFT;
}

/** Angle -> nearest scale. Round to nearest, not down: truncating turns the tan
 *  table's 65535 for fov 512 into scale 511 rather than the 512 it means. */
static inline int
toridraw_proj_scale_from_fov(int fov_rpi2048)
{
    return (toridraw_proj_cot16_from_fov(fov_rpi2048) +
            (1 << (TORIDRAW_PROJ_COT16_SHIFT - 1))) >>
           TORIDRAW_PROJ_COT16_SHIFT;
}

/** Scale -> nearest angle on the fov ladder. Lossy in general (that is the
 *  point of the ladder note); exact only where the scale happens to land on a
 *  representable step. cot is monotonically decreasing in the index, so this is
 *  a plain binary search over the same table the projection uses. */
static inline int
toridraw_proj_fov_from_scale(int proj_scale)
{
    int want;
    int lo = TORIDRAW_PROJ_FOV_MIN >> 1;
    /* Search only where cot is positive and monotonically decreasing; the table
     * changes sign at fov_half 512 and a search across that finds nonsense. */
    int hi = TORIDRAW_PROJ_FOV_MAX >> 1;
    if( proj_scale < 1 )
        proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT;
    want = proj_scale << TORIDRAW_PROJ_COT16_SHIFT;
    while( lo < hi )
    {
        int mid = (lo + hi) >> 1;
        if( g_tan_table[1536 - mid] > want )
            lo = mid + 1;
        else
            hi = mid;
    }
    /* lo is the first half-angle at or below `want`; pick whichever neighbour
     * lands closer so the round trip does not systematically bias one way. */
    if( lo > (TORIDRAW_PROJ_FOV_MIN >> 1) )
    {
        int a = g_tan_table[1536 - (lo - 1)];
        int b = g_tan_table[1536 - lo];
        if( a - want < want - b )
            lo = lo - 1;
    }
    return lo << 1;
}

struct ProjectedVertex
{
    int x;
    int y;

    // z is the distance from the camera to the point
    int z;

    // If the projection is too close or behind the screen, clipped is nonzero.
    int clipped;
};

/** Angle in units of (2π/2048); returns fixed-point trig (typically Q16). */
typedef int (*ToriDrawAngleFn)(int angle_r2pi2048, void* user);

struct ToriDrawTrigFns
{
    ToriDrawAngleFn sin;
    ToriDrawAngleFn cos;
    ToriDrawAngleFn tan;
    void* user;
};

/** Raw 2048-entry RS trig tables; use with ToriDraw_TrigFnsFromTables. */
struct ToriDrawTrigTables
{
    const int* sin;
    const int* cos;
    const int* tan;
};

static inline int
ToriDraw_TrigSinFromTables(
    int angle_r2pi2048,
    void* user)
{
    return ((const struct ToriDrawTrigTables*)user)->sin[angle_r2pi2048];
}

static inline int
ToriDraw_TrigCosFromTables(
    int angle_r2pi2048,
    void* user)
{
    return ((const struct ToriDrawTrigTables*)user)->cos[angle_r2pi2048];
}

static inline int
ToriDraw_TrigTanFromTables(
    int angle_r2pi2048,
    void* user)
{
    return ((const struct ToriDrawTrigTables*)user)->tan[angle_r2pi2048];
}

/** Bind table pointers into trig callbacks (`out->user` = tables). */
static inline void
ToriDraw_TrigFnsFromTables(
    struct ToriDrawTrigFns* out,
    const struct ToriDrawTrigTables* tables)
{
    out->sin = ToriDraw_TrigSinFromTables;
    out->cos = ToriDraw_TrigCosFromTables;
    out->tan = ToriDraw_TrigTanFromTables;
    out->user = (void*)tables;
}

#endif
