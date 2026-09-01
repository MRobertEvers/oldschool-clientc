#ifndef TORIDRAW_FACE_SORT_BITONIC_RADIX_H
#define TORIDRAW_FACE_SORT_BITONIC_RADIX_H

/*
 * The bitonic+radix face sort's ISA lanes: what a lane provides, and how the
 * one dispatcher asks for it.
 *
 * WHAT THE NAME MEANS, AND WHERE IT IS EARNED. The sort culls four faces at a
 * time into composite keys and then orders the keys, and it orders them two
 * ways: a branchless bitonic network up to
 * toridraw_face_sort_bitonic_max() keys, a two-pass 8-bit radix above it. The
 * bitonic half is a vector network and exists only where a vector lane does --
 * NEON and SSE2 below. A build that reaches neither gate has the radix and
 * qsort and no network at all, which is why the kernel it registers is called
 * `radix` rather than `bitonic+radix`; see kernels/facesort.bitonic_radix.u.c.
 *
 * Two of the sort's three steps are written per instruction set. WHICH kernel
 * runs is a property of the build; WHAT the sort does with the answer is not.
 * Those used to be the same function: one `toridraw_face_sort_bitonic_radix`
 * with a NEON block, an SSE2 block and a scalar tail stacked inside it, and a
 * second copy of every helper name under each `#ifdef`. This family keeps them
 * apart.
 *
 *   facesort.bitonic_radix.small.dispatch.u.c  the shared scalar work, the
 *                                              lane selection, the radix, and
 *                                              the one dispatcher
 *   facesort.bitonic_radix.small.neon64.u.c    AArch64: 64-bit winding in
 *                                              vmlsl_s32, vst4q stash, bitonic
 *                                              network, sentinel store and a
 *                                              scalar compaction (measured on
 *                                              an M4 Max: beats the vqtbl1q
 *                                              left-pack). Routes the tile to
 *                                              the scalar tile kernel. Folds
 *                                              near_clipped / stash per model.
 *   facesort.bitonic_radix.small.neon32.u.c    the armv7 twin of it, and the
 *                                              Android armeabi-v7a lane. Same
 *                                              arithmetic; the five A64-only
 *                                              intrinsics are replaced as the
 *                                              ladder below lists. Routes the
 *                                              tile to the scalar tile kernel
 *                                              (measured on the Krait), and
 *                                              gathers from an interleaved
 *                                              copy of the vertices (an
 *                                              in-order core cannot overlap
 *                                              twelve lane loads on its own).
 *   facesort.bitonic_radix.small.sse2.u.c      the Win32 XP lane, written to
 *                                              the Pentium 4 floor: no pshufb,
 *                                              no pcmpgtq, no pminud, no
 *                                              pmulld. Carries the
 *                                              two-triangle terrain tile
 *                                              kernel.
 *   facesort.bitonic_radix.small.scalar.u.c    wasm, scalar, and anything that
 *                                              reaches neither gate. All three
 *                                              hooks decline; the sort is then
 *                                              the scalar tail and qsort,
 *                                              which is what the reference
 *                                              test needs and not a contender
 *                                              otherwise.
 *
 * THE HOOK CONTRACT. Each lane defines all three of these, under these exact
 * names -- that is what lets toridraw_face_sort_bitonic_radix carry no
 * preprocessor of its own:
 *
 *   int toridraw_face_sort_bitonic_radix_lane_blocks(..., int* out_accepted)
 *       Runs the vector cull over whole blocks of faces starting at *f_io,
 *       (num_vertices says how many projected vertices are the model's, for a
 *       lane that re-lays them out per model), 
 *       appends keys at `keys`, advances *f_io past every face it consumed,
 *       returns how many keys it WROTE and stores in *out_accepted how many of
 *       those are accepted faces. The two differ on a lane that does not
 *       left-pack: it may write a rejected face as the sentinel 0xFFFFFFFF in
 *       place, which the sort carries to the end -- the sort runs over the
 *       written count, the caller reports the accepted one, and the sorted
 *       prefix [0, accepted) is the same either way because the key is a
 *       total order. Both NEON lanes store the sentinel and then COMPACT the
 *       block's keys in a scalar pass before returning, so they report the
 *       written count equal to the accepted count; what they gain is the
 *       unconditional vector store in place of a table left-pack. (Sorting
 *       the sentinels instead of compacting was measured on the Krait and
 *       LOST -- the radix over the rejected keys cost more than the pack.)
 *       The SSE2 lane packs. A lane with no vector cull leaves *f_io
 *       alone, returns 0 with *out_accepted = 0; the caller's scalar loop then
 *       covers every face. The caller finishes [*f_io, num_faces) either way,
 *       so a lane may consume any whole number of faces it likes.
 *
 *       Writes up to four keys past the returned count (a packing lane stores
 *       a whole vector), so `keys` needs four lanes of slack.
 *
 *   bool toridraw_face_sort_bitonic_radix_lane_tile2(...)
 *       The two-triangle terrain tile, whose index triples are a compile-time
 *       fact at rotation `tile2_rot`. True means *out_n holds the accepted
 *       count and the keys are written; false means "not my lane" and the
 *       model goes through the ordinary block-and-tail path. A declining lane
 *       must not have written anything.
 *
 *   bool toridraw_face_sort_bitonic_radix_lane_sort(uint32_t* keys, int n)
 *       Sorts n keys ascending, stably enough that equal depths keep face
 *       order (ordering the whole u32 gives that for free). True when it did;
 *       false means the caller runs qsort instead. Called only for counts at
 *       or under toridraw_face_sort_bitonic_max().
 *
 * The lane a build gets is decided by the one `#elif` ladder below, which also
 * sets TORIDRAW_FACE_SORT_SIMD when the lane has a vector cull -- and so, one
 * layer up, whether the kernel is the one the family is named for. That is
 * also what flips the default in toridraw_face_sort_bitonic_radix_armed: where
 * a SIMD cull was compiled this sort is the default and
 * TORIDRAW_FACE_SORT=bucket opts out, and where it was not the bucket sort is
 * the default and TORIDRAW_FACE_SORT=bitonic_radix opts in.
 *
 * WHY THE TILE IS A LANE FACT AND NOT A MODEL FACT. Both tile kernels -- the
 * scalar one with the triples as literals and the SSE2 one with them as
 * shuffle immediates -- are reachable only from the SSE2 lane, and that is
 * deliberate rather than an omission: the A/B that chose between them
 * (toridraw_face_sort_tile2_armed) was run on an x86 host against the general
 * path. The A32 lane has since been measured on the Krait (the scalar kernel
 * wins there too, 117 -> 110 ns/face) and the A64 lane on an M4 Max (5.84 ->
 * 5.09 ns/face); both route the tile. The saving is the same shape on every
 * core: the per-model setup of a two-face model, not vector width.
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
 * in toridraw_face_sort_bitonic_radix_armed.
 */
/*
 * THE ARM LANE IS SPLIT BY ENCODING WIDTH, not gated on __aarch64__.
 *
 * A32 and A64 NEON are different instruction sets, and the sort's body reaches
 * five things the A64 file spells with A64-only intrinsics: vmull_high_s32 and
 * vmlsl_high_s32 (the widening high-half multiply), vcgtq_s64 (there is no
 * 64-bit lane-wise compare in A32 NEON at all), vaddvq_u32 (horizontal add) and
 * vqtbl1q_u8 (the full 16-byte table lookup; A32 has only the 8-byte vtbl).
 * armv7 pointed at the A64 file did not merely run slower -- it failed to
 * compile, which is how the Android armeabi-v7a lane found this.
 *
 * All five have an A32 route, and the neon32 file takes it, so armv7 gets the
 * SIMD winding cull and the bitonic network rather than falling through to the
 * scalar lane:
 *
 *   vmull_high_s32 / vmlsl_high_s32  vmull_s32 / vmlsl_s32 of vget_high_s32.
 *                                    The widening multiply is A32; only the
 *                                    form that takes the quad and picks its
 *                                    top half is A64.
 *   vcgtq_s64 / vcltq_s64            synthesized from the 32-bit halves of the
 *                                    product: the sign of a 64-bit value
 *                                    against zero is its high word's sign,
 *                                    with the unsigned low word breaking the
 *                                    high == 0 tie. The SSE2 lane has the same
 *                                    hole (no pcmpgtq) and answers it the same
 *                                    way. No 32-bit truncation, so the sign
 *                                    decisions stay bit-for-bit the scalar
 *                                    reference's.
 *   vaddvq_u32                       two vpadd_u32 folds, once per block.
 *   vqtbl1q_u8                       the SSE2 lane's index-table left-pack:
 *                                    a stack slot and four unconditional
 *                                    scalar stores, no pshufb needed and none
 *                                    available.
 *
 * aarch64 keeps neon64 for the five single instructions. Same split, and the
 * same reason, as the projection bound lane: see the header comment in
 * impl/projection/projection.bound.dispatch.u.c.
 */
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && defined(__aarch64__) &&                     \
    !defined(NEON_DISABLED)
#define TORIDRAW_FACE_SORT_LANE_NEON64 1
#define TORIDRAW_FACE_SORT_SIMD 1
#elif ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(__aarch64__) &&                  \
    !defined(NEON_DISABLED)
#define TORIDRAW_FACE_SORT_LANE_NEON32 1
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

#endif /* TORIDRAW_FACE_SORT_BITONIC_RADIX_H */
