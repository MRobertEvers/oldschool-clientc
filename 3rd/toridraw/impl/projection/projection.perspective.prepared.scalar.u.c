#ifndef TORIDRAW_GRAPHICS_PROJECTION_PREPARED_NONE_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_PREPARED_NONE_U_C

#include "impl/projection/projection.perspective.prepared.dispatch.h"

/*
 * No prepared kernels in this build.
 *
 * A real lane and not a gap: it is what wasm, the scalar fallback, and any
 * x86 build that reaches the SSE4.1 or AVX2 arm of the ladder in
 * projection16_simd.u.c compile. Both hooks decline, every model takes the
 * portable ladder, and ToriDraw_ProjectionKernelGetPrepared() and
 * ...GetPortable() therefore project identically -- one projection under two
 * names.
 *
 * That last part matters to more than curiosity: kernels/projection.portable.u.c
 * exists to be the A/B baseline against `prepared`, and on this lane that A/B
 * has nothing to measure. It will report a clean zero rather than fail, so the
 * arms have to be read knowing which lane produced them.
 */

#define TORIDRAW_PROJECTION_SLOT_ALWAYS_INLINE
#define TORIDRAW_PROJECTION_SLOT_NEVER_INLINE

static inline bool
toridraw_projection_prepared_clip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_yaw,
    int model_mid_z)
{
    return TORIDRAW_PROJECTION_PREPARED_DECLINE(
        scene, hnd, position, camera, model_yaw, model_mid_z);
}

static inline bool
toridraw_projection_prepared_noclip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_yaw,
    int model_mid_z)
{
    return TORIDRAW_PROJECTION_PREPARED_DECLINE(
        scene, hnd, position, camera, model_yaw, model_mid_z);
}

#endif /* TORIDRAW_GRAPHICS_PROJECTION_PREPARED_NONE_U_C */
