#ifndef PROJECTION_H
#define PROJECTION_H

#include "graphics/shared_tables.h"

#include <stdbool.h>

#define UNIT_SCALE_SHIFT (9)
#define SCALE_UNIT(x) ((((long long)x) << UNIT_SCALE_SHIFT))
#define UNIT_SCALE ((1 << UNIT_SCALE_SHIFT))

/**
 * `SCALE_UNIT(v) / z`, without the 64-bit divide when the numerator fits.
 *
 * On i686 a 64/64 divide is a call to a libgcc helper rather than an
 * instruction, and the projection kernels pay one per projected vertex --
 * `objdump` puts ten such call sites in `ToriDraw_Project` alone, on a lane
 * where `render` is 8.2 ms of a 10.0 ms frame.
 *
 * The 64-bit numerator exists only so a coordinate large enough to overflow
 * `v << 9` survives. Inside +/-2^22 it cannot, and the reference client does
 * this multiply in plain 32-bit arithmetic, so the narrow path is the reference
 * spelling rather than a departure from it. C integer division truncates toward
 * zero in both widths, so an in-range value divides to the same result.
 *
 * Spelled as a multiply, not a shift: left-shifting a negative signed value is
 * undefined, and the compiler emits the shift for `* UNIT_SCALE` anyway.
 */
static inline int
ToriDraw_ScaleUnitDiv(int v, int z)
{
    if( v > -(1 << 22) && v < (1 << 22) )
        return (v * UNIT_SCALE) / z;
    return (int)(SCALE_UNIT(v) / z);
}

/**
 * Screen x parked here by the projection kernels (projection16_simd.*.u.c) when
 * a vertex falls behind the near plane; its screen y is left *undivided*, so
 * consumers must reject the whole face rather than read the pair.
 *
 * Only meaningful when the model was projected by the *_clip family — recorded
 * for the last projected model as scene->near_clipped. Only that family emits
 * the sentinel, and only it pays the compare that decides to; the *_noclip
 * family divides unconditionally and can therefore leave a *genuine* -5000 in
 * screen x. Every consumer must consult scene->near_clipped before testing
 * against this value — see ToriDraw_Project for how it is derived, and
 * Client-TS Model.render2:1876 for the same gate in the reference.
 */
#define TORIDRAW_SCREEN_X_NEAR_CLIPPED (-5000)

/**
 * Nudge target for a genuinely projected -5000 in the *_clip family, so the
 * sentinel above stays exact there. The reference does not do this (it relies
 * solely on the per-model gate); we keep it because that family is rare enough
 * for the extra compare to be free.
 */
#define TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE (-5001)

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
 * is live is stated by projection_mode, NOT inferred from a zero sentinel -- two
 * fields racing to define one quantity through "0 means use the other" is how
 * a camera ends up projecting with a value nobody set.
 *
 *   TORIDRAW_PROJECTION_MODE_SCALE  projection_scale is authoritative. Exact, and the only
 *                             way to match a reference projection.
 *   TORIDRAW_PROJECTION_MODE_FOV    fov_rpi2048 is authoritative. An angle, resolved
 *                             through cot(fov/2). Natural for a free camera.
 *
 * The angle is lossy as a way to request a scale, which is why the default mode
 * is SCALE: the conversion opens with `fov >> 1`, so only 1024 of the 2048
 * angles are distinct and the reachable scales step by ~1.7 near scale 190 and
 * ~2.6 near 410. Most integer scales are not on that ladder -- including the
 * 191 the reference lands on at a 503-high viewport (nearest 190.31, 192.09).
 *
 * toridraw_projection_scale_from_fov / toridraw_projection_fov_from_scale convert between
 * them for callers that need to move a value from one spelling to the other.
 */
#define TORIDRAW_PROJECTION_COT16_SHIFT (16 - UNIT_SCALE_SHIFT)

/** The scale the reference defaults to, and what SCALE_UNIT alone produces. */
#define TORIDRAW_PROJECTION_SCALE_DEFAULT UNIT_SCALE

/** The angle whose cotangent is ~1.0, i.e. the fov spelling of scale 512. */
#define TORIDRAW_PROJECTION_FOV_DEFAULT (512)

/* Valid fov domain. cot(fov/2) is only positive while fov/2 < 90 degrees, i.e.
 * fov_half < 512 in these units. Past that the tan table returns a NEGATIVE
 * cotangent and the projection mirrors: fov 1200 resolves to scale -142, which
 * draws the world inside out rather than failing. Angles are clamped into the
 * domain here so no caller can reach that by arithmetic accident. */
#define TORIDRAW_PROJECTION_FOV_MIN (2)
#define TORIDRAW_PROJECTION_FOV_MAX (1022)

enum ToriDraw_ProjectionMode
{
    /** projection_scale drives the projection. Default. */
    TORIDRAW_PROJECTION_MODE_SCALE = 0,
    /** fov_rpi2048 drives the projection. */
    TORIDRAW_PROJECTION_MODE_FOV = 1,
    /**
     * Parallel (orthographic) projection -- no perspective divide at all;
     * parallel_zoom16 drives it. For the map editor. Selects the
     * projection_ortho.u.c kernels, and changes what the bounding-cylinder cull
     * has to compute, because screen extent no longer depends on depth.
     */
    TORIDRAW_PROJECTION_MODE_PARALLEL = 2,
};

/** Fixed-point shift for ToriDraw_Camera.parallel_zoom16. */
#define TORIDRAW_ORTHO_ZOOM_SHIFT 16
/** parallel_zoom16 for 1:1 -- one screen pixel per world unit. */
#define TORIDRAW_ORTHO_ZOOM_UNIT (1 << TORIDRAW_ORTHO_ZOOM_SHIFT)

/** Parallel projection is a different kernel family, not a different scale. */
static inline bool
toridraw_projection_is_parallel(int projection_mode)
{
    return projection_mode == TORIDRAW_PROJECTION_MODE_PARALLEL;
}

/** Exact: cot16 such that the kernels project coord * scale / z. */
static inline int
toridraw_projection_cot16_from_scale(int projection_scale)
{
    return (projection_scale > 0 ? projection_scale : TORIDRAW_PROJECTION_SCALE_DEFAULT)
           << TORIDRAW_PROJECTION_COT16_SHIFT;
}

/** Lossy; see the ladder note above. Angle is in units of 2*pi/2048, clamped
 *  to [TORIDRAW_PROJECTION_FOV_MIN, TORIDRAW_PROJECTION_FOV_MAX]. */
static inline int
toridraw_projection_cot16_from_fov(int fov_rpi2048)
{
    if( fov_rpi2048 < 1 )
        fov_rpi2048 = TORIDRAW_PROJECTION_FOV_DEFAULT;
    if( fov_rpi2048 < TORIDRAW_PROJECTION_FOV_MIN )
        fov_rpi2048 = TORIDRAW_PROJECTION_FOV_MIN;
    if( fov_rpi2048 > TORIDRAW_PROJECTION_FOV_MAX )
        fov_rpi2048 = TORIDRAW_PROJECTION_FOV_MAX;
    return ToriDraw_ReadTanTable(1536 - (fov_rpi2048 >> 1));
}

/**
 * Resolve the camera's projection knobs to the single value the kernels want.
 * Takes ints rather than the camera so this header stays free of toridraw_types.
 * projection_mode selects; the unselected field is ignored, never consulted.
 */
static inline int
toridraw_projection_cot16(int projection_mode, int projection_scale, int fov_rpi2048)
{
    if( projection_mode == TORIDRAW_PROJECTION_MODE_FOV )
        return toridraw_projection_cot16_from_fov(fov_rpi2048);
    return toridraw_projection_cot16_from_scale(projection_scale);
}

/** The linear scale a cot16 realises -- for logging and assertions, so a
 *  reported scale is measured rather than assumed. */
static inline int
toridraw_projection_scale_from_cot16(int cot16)
{
    return cot16 >> TORIDRAW_PROJECTION_COT16_SHIFT;
}

/** Angle -> nearest scale. Round to nearest, not down: truncating turns the tan
 *  table's 65535 for fov 512 into scale 511 rather than the 512 it means. */
static inline int
toridraw_projection_scale_from_fov(int fov_rpi2048)
{
    return (toridraw_projection_cot16_from_fov(fov_rpi2048) +
            (1 << (TORIDRAW_PROJECTION_COT16_SHIFT - 1))) >>
           TORIDRAW_PROJECTION_COT16_SHIFT;
}

/** Scale -> nearest angle on the fov ladder. Lossy in general (that is the
 *  point of the ladder note); exact only where the scale happens to land on a
 *  representable step. cot is monotonically decreasing in the index, so this is
 *  a plain binary search over the same table the projection uses. */
static inline int
toridraw_projection_fov_from_scale(int projection_scale)
{
    int want;
    int lo = TORIDRAW_PROJECTION_FOV_MIN >> 1;
    /* Search only where cot is positive and monotonically decreasing; the table
     * changes sign at fov_half 512 and a search across that finds nonsense. */
    int hi = TORIDRAW_PROJECTION_FOV_MAX >> 1;
    if( projection_scale < 1 )
        projection_scale = TORIDRAW_PROJECTION_SCALE_DEFAULT;
    want = projection_scale << TORIDRAW_PROJECTION_COT16_SHIFT;
    while( lo < hi )
    {
        int mid = (lo + hi) >> 1;
        if( ToriDraw_ReadTanTable(1536 - mid) > want )
            lo = mid + 1;
        else
            hi = mid;
    }
    /* lo is the first half-angle at or below `want`; pick whichever neighbour
     * lands closer so the round trip does not systematically bias one way. */
    if( lo > (TORIDRAW_PROJECTION_FOV_MIN >> 1) )
    {
        int a = ToriDraw_ReadTanTable(1536 - (lo - 1));
        int b = ToriDraw_ReadTanTable(1536 - lo);
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
