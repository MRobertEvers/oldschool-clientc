#ifndef TORIDRAW_ZDEPTH_H
#define TORIDRAW_ZDEPTH_H

#include <stdbool.h>

/*
 * Storage for the scene's z-buffer scratch ("model zbuffer").
 *
 * There is ONE buffer per scene, allocated once and reused. It is RESET at the
 * start of each model that opts into depth testing, which is what confines its
 * effect to that model: a depth written while drawing one model can never
 * reject a pixel of the next, because the next opting-in model starts from a
 * cleared buffer and models that do not opt in never read it at all.
 *
 * Sorting BETWEEN models therefore stays the scene's job (painter's order over
 * the element list), which is what keeps the reference's layering rules intact
 * — a z-buffer left standing across the whole scene would quietly overrule
 * them.
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
 * Storage is a 32-bit float on every target. The 16-bit `_Float16` this used
 * to take on x86-64 and aarch64 halved the buffer, but on x86-64 without F16C
 * every load and store in the span became a conversion call, and its 11-bit
 * mantissa left ~1.5 world units of resolution at the QBD's viewing distance.
 * A float's 24 bits put that around 0.0002, and one type means every lane
 * resolves the same pixels the same way.
 */

#include <float.h>
#include <stdbool.h>

typedef float torizdepth_t;

/** Largest finite magnitude the storage type holds. */
#define TORIDRAW_ZDEPTH_LIMIT FLT_MAX

/** Perspective key numerator. Precision is relative, so this buys no
 *  resolution; it only fixes the key's units. */
#define TORIDRAW_ZDEPTH_PERSP_SCALE 4096.0f

/** Parallel key divisor; units only, as above. */
#define TORIDRAW_ZDEPTH_PARALLEL_DIVISOR 64.0f

/**
 * Depth -> key. `camera_z` is the camera-space depth of the vertex, i.e. the
 * orthographic z the projection kernels wrote, NOT screen_vertices_z (which
 * carries the model's mid-z subtracted out).
 */
static inline float
toridraw_zdepth_key(int camera_z, bool parallel)
{
    /* An int z scaled down cannot leave a float's range; no clamp. */
    if( parallel )
        return -(float)camera_z * (1.0f / TORIDRAW_ZDEPTH_PARALLEL_DIVISOR);

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
