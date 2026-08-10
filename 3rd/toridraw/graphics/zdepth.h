#ifndef TORIDRAW_ZDEPTH_H
#define TORIDRAW_ZDEPTH_H

#include <stdbool.h>

/*
 * Storage for the per-model depth scratch ("model z-buffer").
 *
 * The buffer resolves a model's faces against EACH OTHER and nothing else: it
 * is cleared before every model that opts in, so a depth written by one model
 * can never reject a pixel of the next. That is the whole scope of it. Sorting
 * BETWEEN models stays the scene's job (painter's order over the element list),
 * which is what keeps the reference's layering rules intact — a z-buffer across
 * the whole scene would quietly overrule them.
 *
 * The stored value is a DEPTH KEY, not a depth: larger is nearer, and the
 * cleared state is the farthest key there is. Two things follow from that.
 *
 *   - Under perspective the key is TORIDRAW_ZDEPTH_PERSP_SCALE / z. The
 *     reciprocal is what makes the key EXACTLY linear in screen space, so a
 *     span can interpolate it with a plain add per pixel and still be
 *     perspective-correct. Interpolating z itself is not; it bows away from the
 *     true surface between vertices and mis-resolves the middle of long
 *     triangles, which on a model the size of the QBD is most of them.
 *
 *   - Under parallel projection there is no divide, so depth is already linear
 *     and the key is a plain negated, scaled z. Negated so "larger is nearer"
 *     holds in both modes and the kernels need no knowledge of which one they
 *     are drawing; only the clear value differs.
 *
 * 16-bit half is used when the toolchain has a real IEEE `_Float16`, halving a
 * buffer that is screen-sized. Its 11-bit mantissa gives ~0.05% relative
 * resolution, which at the QBD's ~3,000-unit viewing distance is ~1.5 world
 * units — far below the separation between the surfaces the sort gets wrong.
 * Precision is RELATIVE, so the scale factors below exist to keep the key
 * inside half's exponent range, not to buy resolution.
 */

#if defined(TORIDRAW_ZDEPTH_FORCE_F32)
#define TORIDRAW_ZDEPTH_HALF 0
#elif defined(__FLT16_MANT_DIG__) &&                                                               \
    (defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__))
/* GCC/Clang define __FLT16_MANT_DIG__ only where _Float16 is real. The target
 * test is belt-and-braces: on 32-bit x86 the type exists but wants SSE2 that
 * the XP lane does not promise, and a soft-float half in the inner loop would
 * cost more than the memory it saves. */
#define TORIDRAW_ZDEPTH_HALF 1
#else
#define TORIDRAW_ZDEPTH_HALF 0
#endif

#if TORIDRAW_ZDEPTH_HALF
typedef _Float16 torizdepth_t;
/** Largest finite magnitude the storage type holds. */
#define TORIDRAW_ZDEPTH_LIMIT 65504.0f
#else
typedef float torizdepth_t;
#define TORIDRAW_ZDEPTH_LIMIT 3.0e38f
#endif

/** Perspective key numerator. Chosen so the key stays inside half's finite
 *  range for every depth a projected vertex can legally have (z >= 1). */
#define TORIDRAW_ZDEPTH_PERSP_SCALE 4096.0f

/** Parallel key divisor, same job at the other end: z is unbounded above under
 *  an orthographic camera, and z/64 keeps a 4-million-unit scene finite. */
#define TORIDRAW_ZDEPTH_PARALLEL_DIVISOR 64.0f

/**
 * Depth -> key. `camera_z` is the camera-space depth of the vertex, i.e. the
 * orthographic z the projection kernels wrote, NOT screen_vertices_z (which
 * carries the model's mid-z subtracted out).
 */
static inline float
toridraw_zdepth_key(int camera_z, bool parallel)
{
    float key;

    if( parallel )
    {
        key = -(float)camera_z * (1.0f / TORIDRAW_ZDEPTH_PARALLEL_DIVISOR);
        if( key > TORIDRAW_ZDEPTH_LIMIT )
            return TORIDRAW_ZDEPTH_LIMIT;
        if( key < -TORIDRAW_ZDEPTH_LIMIT )
            return -TORIDRAW_ZDEPTH_LIMIT;
        return key;
    }

    /* A vertex at or behind the eye has no perspective key; the near clip is
     * supposed to have removed it, so answer "as near as anything can be"
     * rather than divide by zero and poison the buffer with a NaN. */
    if( camera_z < 1 )
        return TORIDRAW_ZDEPTH_LIMIT;
    return TORIDRAW_ZDEPTH_PERSP_SCALE / (float)camera_z;
}

/** The key a cleared buffer holds: farther than every real surface. */
static inline float
toridraw_zdepth_clear_key(bool parallel)
{
    return parallel ? -TORIDRAW_ZDEPTH_LIMIT : 0.0f;
}

#endif
