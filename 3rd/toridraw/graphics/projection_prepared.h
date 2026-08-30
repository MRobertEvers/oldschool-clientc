#ifndef TORIDRAW_GRAPHICS_PROJECTION_PREPARED_H
#define TORIDRAW_GRAPHICS_PROJECTION_PREPARED_H

/*
 * The prepared-camera projection lane: which ISA supplies one, and the single
 * question the renderer puts to it.
 *
 * A yaw-only model drawn under a camera whose prepared block was published
 * (ToriDraw_ScenePrepareProjectionCamera) can skip the per-call camera setup
 * entirely -- projection16_prepared.sse2.h carries the census that motivates
 * it. WHICH kernel performs that skip is a property of the build's
 * architecture; WHETHER this model qualifies is a property of the model. Those
 * are two different questions, and keeping them apart is what this family is
 * for -- they used to be one interleaved `#if` inside the two hottest
 * functions in toridraw_render.u.c, with the model half written out once per
 * ISA.
 *
 *   toridraw_projection_prepared_eligible   per model. One copy, no ISA.
 *   toridraw_projection_prepared_clip       per ISA. One file each.
 *   toridraw_projection_prepared_noclip
 *
 *   projection_prepared.u.c        selects one lane
 *   projection_prepared.neon.u.c   AArch64: the assembly in
 *                                  projection16.aarch64.S. NOCLIP ONLY --
 *                                  there is no clip entry point in the .S, so
 *                                  `clip` declines on this lane and those
 *                                  models take the portable ladder.
 *   projection_prepared.sse2.u.c   the SSE2 fused-yaw kernels, both families
 *   projection_prepared.none.u.c   everywhere else -- wasm, scalar, and the
 *                                  SSE4.1/AVX2 builds, which reach neither
 *                                  gate. Both hooks decline, and the
 *                                  `prepared` projection kernel is then the
 *                                  `portable` one under a second name.
 *
 * THE HOOK CONTRACT
 *
 * Each hook returns true when it projected the model: the screen and
 * orthographic vertex arrays are written, and scene->projection_bound_vertices
 * records how much of the screen box the kernel bounded on the way past (see
 * toridraw_projected_bound). False means "not my lane", the caller runs the
 * portable ladder instead, and a lane that declines must not have written
 * anything -- the caller is entitled to project the same model again.
 *
 * No caller tests the architecture. A lane that this build did not compile is
 * `return false`, the conjunction in the caller folds at compile time, and the
 * portable call is all that survives; that is why toridraw_render.u.c carries
 * no preprocessor for any of this.
 *
 * THE INLINING, WHICH IS ALSO A LANE FACT
 *
 * Every lane defines TORIDRAW_PROJECTION_SLOT_ALWAYS_INLINE and
 * TORIDRAW_PROJECTION_SLOT_NEVER_INLINE, which the two renderer slot functions carry
 * -- prepared inlined into its caller, portable kept out of line. Only the
 * AArch64 lane asks for either, and it was measured there; the tuning lives
 * with the lane that measured it rather than as a bare `#if` around an
 * attribute in the renderer.
 */

#include "toridraw_types.h"

#include <stdbool.h>

/*
 * Can any prepared kernel take this model?
 *
 * Yaw-only geometry -- a pitched or rolled model needs the full rotation the
 * prepared block does not carry -- and a block published for THIS camera.
 * That last one is a pointer compare and not a value compare on purpose: the
 * block is derived from one camera object, and a second camera holding the
 * same angles is still a different object whose block was never written.
 * Identity is the only cheap question that cannot be wrong.
 *
 * Asked by the caller, before the lane hook rather than inside it. It was the
 * same three lines in every lane, and the comment in each of them said so.
 */
static inline bool
toridraw_projection_prepared_eligible(
    const struct ToriDraw_Scene* scene,
    const struct ToriDraw_Camera* camera,
    int model_pitch,
    int model_roll,
    int camera_roll)
{
    return model_pitch == 0 && model_roll == 0 && camera_roll == 0 &&
           scene->projection_prepared_camera_source == camera;
}

/*
 * A lane that has no kernel for this family. Names the intent, and consumes
 * the arguments a declining hook never looks at.
 */
#define TORIDRAW_PROJECTION_PREPARED_DECLINE(scene, hnd, position, camera, model_yaw, model_mid_z) \
    ((void)(scene),                                                                          \
     (void)(hnd),                                                                            \
     (void)(position),                                                                       \
     (void)(camera),                                                                         \
     (void)(model_yaw),                                                                      \
     (void)(model_mid_z),                                                                    \
     false)

#endif /* TORIDRAW_GRAPHICS_PROJECTION_PREPARED_H */
