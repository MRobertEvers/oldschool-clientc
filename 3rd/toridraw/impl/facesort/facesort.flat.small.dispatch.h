#ifndef TORIDRAW_FACE_SORT_FLAT_H
#define TORIDRAW_FACE_SORT_FLAT_H

/*
 * The flat face sort's ISA lanes: what a lane provides, and how the one
 * dispatcher asks for it.
 *
 * The sort itself is one algorithm -- cull four faces at a time into composite
 * keys, then order the keys -- and two of its three steps are written per
 * instruction set. WHICH kernel runs is a property of the build; WHAT the sort
 * does with the answer is not. Those used to be the same function: one
 * `toridraw_face_sort_flat` with a NEON block, an SSE2 block and a scalar tail
 * stacked inside it, and a second copy of every helper name under each
 * `#ifdef`. This family keeps them apart.
 *
 *   toridraw_face_sort_flat.u.c        the shared scalar work, the lane
 *                                      selection, and the one dispatcher
 *   toridraw_face_sort_flat.neon.u.c   AArch64: vqtbl1q left-pack, 64-bit
 *                                      winding in vmlsl_s32, vst4q stash,
 *                                      bitonic network. NO tile kernel --
 *                                      nothing here was measured on NEON.
 *   toridraw_face_sort_flat.sse2.u.c   the Win32 XP lane, written to the
 *                                      Pentium 4 floor: no pshufb, no
 *                                      pcmpgtq, no pminud, no pmulld. Carries
 *                                      the two-triangle terrain tile kernel.
 *   toridraw_face_sort_flat.none.u.c   wasm, scalar, and anything that reaches
 *                                      neither gate. All three hooks decline;
 *                                      the sort is then the scalar tail and
 *                                      qsort, which is what the reference test
 *                                      needs and not a contender otherwise.
 *
 * THE HOOK CONTRACT. Each lane defines all three of these, under these exact
 * names -- that is what lets toridraw_face_sort_flat carry no preprocessor of
 * its own:
 *
 *   int toridraw_face_sort_flat_lane_blocks(...)
 *       Runs the vector cull over whole blocks of faces starting at *f_io,
 *       appends the accepted keys at `keys`, advances *f_io past every face it
 *       consumed, and returns how many keys it wrote. A lane with no vector
 *       cull leaves *f_io alone and returns 0; the caller's scalar loop then
 *       covers every face. The caller finishes [*f_io, num_faces) either way,
 *       so a lane may consume any whole number of faces it likes.
 *
 *       Writes up to four keys past the returned count (the left-pack stores a
 *       whole vector), so `keys` needs four lanes of slack.
 *
 *   bool toridraw_face_sort_flat_lane_tile2(...)
 *       The two-triangle terrain tile, whose index triples are a compile-time
 *       fact at rotation `tile2_rot`. True means *out_n holds the accepted
 *       count and the keys are written; false means "not my lane" and the
 *       model goes through the ordinary block-and-tail path. A declining lane
 *       must not have written anything.
 *
 *   bool toridraw_face_sort_flat_lane_sort(uint32_t* keys, int n)
 *       Sorts n keys ascending, stably enough that equal depths keep face
 *       order (ordering the whole u32 gives that for free). True when it did;
 *       false means the caller runs qsort instead. Called only for counts at
 *       or under toridraw_face_sort_bitonic_max().
 *
 * The lane a build gets is decided by the one `#elif` ladder below, which also
 * sets TORIDRAW_FACE_SORT_SIMD when the lane has a vector cull. That is what
 * flips the default in toridraw_face_sort_flat_armed: where a SIMD cull was
 * compiled the flat sort is the default and TORIDRAW_FACE_SORT=bucket opts
 * out, and where it was not the bucket sort is the default and
 * TORIDRAW_FACE_SORT=flat opts in.
 *
 * WHY THE TILE IS A LANE FACT AND NOT A MODEL FACT. Both tile kernels -- the
 * scalar one with the triples as literals and the SSE2 one with them as
 * shuffle immediates -- are reachable only from the SSE2 lane, and that is
 * deliberate rather than an omission: the A/B that chose between them
 * (toridraw_face_sort_tile2_armed) was run on an x86 host against the general
 * path, and no equivalent measurement exists on NEON. Until one does, the
 * NEON lane declines and a terrain tile there is an ordinary two-face model.
 */

#include "graphics/dash_restrict.h"
#include "toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * WHICH LANE THIS BUILD HAS. One `#elif` ladder, here, and nowhere else --
 * the lane files do not test the architecture and neither does the
 * dispatcher. TORIDRAW_FACE_SORT_SIMD says a vector cull was compiled, which
 * is the one thing outside this family that has to know: it flips the default
 * in toridraw_face_sort_flat_armed.
 */
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#define TORIDRAW_FACE_SORT_LANE_NEON 1
#define TORIDRAW_FACE_SORT_SIMD 1
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
/* The Win32 XP lane: -march=pentium4 is SSE2 and nothing above it -- no
 * pshufb (SSSE3), no pcmpgtq / pmulld / pminud (SSE4.x). The lane file is
 * written to that floor and says how each gap is filled. */
#define TORIDRAW_FACE_SORT_LANE_SSE2 1
#define TORIDRAW_FACE_SORT_SIMD 1
#endif

/*
 * A lane with no vector cull. Names the intent, and consumes the arguments a
 * declining hook never looks at.
 */
#define TORIDRAW_FACE_SORT_BLOCKS_DECLINE(                                                         \
    scene, f_io, num_faces, near_clipped, model_min_depth, stash_xy, vx, vy, vz, fa, fb, fc, keys) \
    ((void)(scene),                                                                                \
     (void)(f_io),                                                                                 \
     (void)(num_faces),                                                                            \
     (void)(near_clipped),                                                                         \
     (void)(model_min_depth),                                                                      \
     (void)(stash_xy),                                                                             \
     (void)(vx),                                                                                   \
     (void)(vy),                                                                                   \
     (void)(vz),                                                                                   \
     (void)(fa),                                                                                   \
     (void)(fb),                                                                                   \
     (void)(fc),                                                                                   \
     (void)(keys),                                                                                 \
     0)

/* A lane with no terrain-tile kernel. */
#define TORIDRAW_FACE_SORT_TILE2_DECLINE(                                                          \
    scene, tile2_rot, near_clipped, model_min_depth, stash_xy, vx, vy, vz, keys, out_n)            \
    ((void)(scene),                                                                                \
     (void)(tile2_rot),                                                                            \
     (void)(near_clipped),                                                                         \
     (void)(model_min_depth),                                                                      \
     (void)(stash_xy),                                                                             \
     (void)(vx),                                                                                   \
     (void)(vy),                                                                                   \
     (void)(vz),                                                                                   \
     (void)(keys),                                                                                 \
     (void)(out_n),                                                                                \
     false)

#endif /* TORIDRAW_FACE_SORT_FLAT_H */
