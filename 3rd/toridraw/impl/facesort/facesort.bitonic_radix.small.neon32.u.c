#ifndef TORIDRAW_FACE_SORT_BITONIC_RADIX_NEON32_U_C
#define TORIDRAW_FACE_SORT_BITONIC_RADIX_NEON32_U_C

#include "impl/facesort/facesort.bitonic_radix.small.dispatch.h"

#include <arm_neon.h>

#include <assert.h>
#include <limits.h>

/*
 * The A32 lane of the bitonic+radix face sort -- the armv7 twin of
 * facesort.bitonic_radix.small.neon64.u.c. See
 * facesort.bitonic_radix.small.dispatch.h for the three hooks every lane owes
 * the dispatcher; this file provides all three. The terrain tile takes the
 * SCALAR tile kernel (as the SSE2 lane does), because on the Moto X a
 * Lumbridge frame's sort is ~60% two-face tiles by call count and the tile
 * kernel is about the per-model cost, not vector width -- see the hook.
 *
 * WHY A SECOND ARM FILE AT ALL. The arithmetic is identical -- the same
 * structure-of-arrays gather, the same 64-bit cross product, the same key, the
 * same bitonic network. What differs is five instructions the A64 file spells
 * with A64-only intrinsics. A32 NEON is a different instruction set, not a
 * subset with a slower encoding, so the fill for each is named here once:
 *
 *   vmull_high_s32 / vmlsl_high_s32  (widening multiply of the top two lanes)
 *       -> vmull_s32 / vmlsl_s32 on vget_high_s32 of each operand. The
 *          widening multiply itself is VMULL.S32 / VMLSL.S32 and exists on
 *          A32; only the "_high" convenience form that takes the quad and
 *          picks its top half is A64. Same instruction, one vget each.
 *
 *   vcgtq_s64 / vcltq_s64            (signed 64-bit vector compare)
 *       -> DOES NOT EXIST ON A32 in any form: there is no 64-bit lane-wise
 *          compare in the A32 encoding. Synthesized from the 32-bit halves,
 *          which is the same gap the SSE2 lane has (no pcmpgtq) and the same
 *          shape of answer. The comparison is against ZERO, which makes it
 *          exact in two compares rather than a general high/low ladder: split
 *          each 64-bit product into its high word (vshrn_n_s64 by 32, an
 *          arithmetic shift, so the high word keeps its sign) and its low word
 *          (vmovn_u64, the low 32 bits, UNSIGNED),
 *
 *              w <  0   iff   high <  0
 *              w >  0   iff   high >  0  ||  (high == 0 && low != 0)
 *
 *          because w = high * 2^32 + low with low unsigned. `low != 0` is
 *          vtstq_u32(low, low). No 32-bit truncation of the product is
 *          involved anywhere, so the sign decisions are bit-for-bit those of
 *          toridraw_winding_2d's long long -- which is the whole point: a
 *          winding that differs by one face changes draw order.
 *
 *   vaddvq_u32                       (horizontal add across four lanes)
 *       -> two pairwise folds. vpadd_u32 of the low and high halves gives
 *          {w0+w1, w2+w3}; vpadd_u32 of that with itself gives the total in
 *          both lanes. VPADD.I32 is A32. Once per four-face block.
 *
 *   vqtbl1q_u8                       (full 16-byte table lookup, the left-pack)
 *       -> NOT DONE IN THE BLOCK AT ALL on this lane. A32 has only the 8-byte
 *          vtbl, and every pack that brought the keys to the ARM side
 *          (register moves, or a store the table lookup reloaded) was measured
 *          as the block's serialisation point -- see block4. The block stores
 *          all four keys with rejected faces as the sentinel, and lane_blocks
 *          compacts the run in one ARM pass before the sort. Same "four lanes
 *          of slack" contract, since every block writes a whole vector.
 *
 * Everything else in the file -- vmulq_n_s32, vcltq_u32, vbslq, vminq_u32/
 * vmaxq_u32, vrev64q_u32, vextq_u32, vst4q_s32 -- is common to both encodings
 * and is written the same way in both files on purpose. What is NOT common is
 * the gather: this file reads interleaved {x,y,z,0} vertices with whole
 * register loads and transposes, because lane inserts serialise on the A32
 * NEON unit (see block4); the A64 file still gathers lane by lane.
 */

/* 3-way select: lane takes a where o == 0, b where o == 1, else c. */
static inline int32x4_t
toridraw_sel3_neon32(
    int32x4_t o,
    int32x4_t a,
    int32x4_t b,
    int32x4_t c)
{
    uint32x4_t is0 = vceqq_s32(o, vdupq_n_s32(0));
    uint32x4_t is1 = vceqq_s32(o, vdupq_n_s32(1));
    return vbslq_s32(is0, a, vbslq_s32(is1, b, c));
}

/*
 * The signed sign test of four 64-bit windings, as one 32-bit lane mask.
 *
 * `w_lo` holds faces 0 and 1, `w_hi` faces 2 and 3; the result is all-ones in
 * lane i where face i is front facing. A32 has no vcgtq_s64, so this is the
 * high-word / low-word synthesis the file header describes.
 */
static inline uint32x4_t
toridraw_winding_front_neon32(
    int64x2_t nw_lo,
    int64x2_t nw_hi)
{
    /*
     * THE CALLER HANDS IN -w, NOT w. A 64-bit value is negative iff its high
     * word is, whatever the low word holds -- so `w < 0` is one compare on
     * the narrowed high words, while `w > 0` needs the high word AND a
     * low-word tie-break for high == 0 (five more vector ops: two vmovn, a
     * vtst, a vceq, an and/or). Multiplying the cross product the other way
     * round costs nothing (the same vmull / vmlsl with the operands swapped)
     * and turns the front test into the cheap direction: front means w > 0,
     * that is -w < 0. |w| < 2^63 always, so negating cannot overflow and the
     * two tests agree bit for bit with graphics/winding.h.
     *
     * vshrn_n_s64(nw, 32) is an arithmetic narrowing shift: the high word
     * arrives signed.
     */
    int32x4_t const high = vcombine_s32(vshrn_n_s64(nw_lo, 32), vshrn_n_s64(nw_hi, 32));
#if TORIDRAW_FLIP_WINDING
    /* front is w < 0, i.e. -w > 0: high word positive, or zero with a
     * non-zero low word. The flipped build takes the five-op form. */
    {
        int32x4_t const zero = vdupq_n_s32(0);
        uint32x4_t const low = vcombine_u32(
            vmovn_u64(vreinterpretq_u64_s64(nw_lo)), vmovn_u64(vreinterpretq_u64_s64(nw_hi)));
        uint32x4_t const low_nonzero = vtstq_u32(low, low);
        return vorrq_u32(vcgtq_s32(high, zero), vandq_u32(vceqq_s32(high, zero), low_nonzero));
    }
#else
    /* front is w > 0, i.e. -w < 0: the sign of the high word, spread. */
    return vreinterpretq_u32_s32(vshrq_n_s32(high, 31));
#endif
}

/*
 * Four faces at f..f+3: cull, key, stash. Always writes four keys at *keys --
 * rejected faces as the sentinel -- and returns 4; lane_blocks compacts the
 * run afterwards.
 *
 * THE GATHER READS INTERLEAVED VERTICES. `xyz` is the model's projected
 * vertices as {x, y, z, 0} quads, laid out by lane_blocks once per model. One
 * whole-register load per corner vertex, twelve per block, then a 4x4
 * transpose per corner (two VTRN.32 and the half swaps) puts each axis of the
 * four faces into one vector.
 *
 * WHAT WAS MEASURED, on the Moto X with the sort bench (keys arm, presort off,
 * ns per input face at 64/200/256/1000/2000 faces; parity with the bucket
 * sort held throughout):
 *
 *   lane-by-lane gather + ARM left-pack (the original)   47.6 43.9 43.0 44.6 62.8
 *   + vld1q_lane instead of vgetq_lane for the indices   no change
 *   + pack via register moves instead of a stack slot    no change
 *   + sentinel store, sort over ALL keys (no pack)       53.2 46.9 45.0 47.6 62.8
 *   interleaved gather + transposes, ARM left-pack       51.5 46.7 45.2 48.0 56.7
 *   interleaved gather, sentinel store, COMPACTION PASS  46.6 40.7 40.7 42.1 49.6
 *   + near_clipped / stash folded per model (4 loops)    44.3 39.4 36.1 39.4 46.1
 *   + winding built negated (one-op sign), running
 *     key base (two-op key)                              41.5 37.1 34.8 38.3 44.7
 *   + two blocks per loop trip (independent chains)      44.4 38.9 36.9 40.9 49.0
 *     LOST: two blocks' gathered vectors are 36 q-registers against the
 *     16 A32 has, and the spills cost more than the overlap bought.
 *
 * So neither the gather nor the pack was the cost on its own. What the block
 * could not do was overlap: the pack needed the keys on the ARM side at the
 * end of the chain, and that wait charged every block the chain's full
 * latency (the block ran at an IPC near 0.4). Removing the pack only paid
 * once the sort was kept to the accepted keys -- hence the compaction pass
 * -- and the interleaved gather, a loss on its own, pays with it at large
 * counts. What is left is the core: ~40 ns a face is ~70 cycles at an IPC
 * under one, spent on the winding's 64-bit products, the near-clip and
 * depth compares and the key build.
 */
static inline __attribute__((always_inline)) int
toridraw_face_sort_bitonic_radix_block4_neon32(
    struct ToriDraw_Scene* scene,
    int f,
    int32x4_t near_clip_sentinel, /* -5000 x4; read only when spec_clipped */
    int32x4_t min_depth,
    uint32x4_t depth_levels,
    int const spec_clipped, /* literal at every call: folds the clip test away */
    int const spec_stash,   /* literal at every call: folds the stash body away */
    uint32x4_t key_base,    /* 0xFFFF0000 | (f + lane), advanced by the loop */
    const int* RESTRICT xyz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c,
    uint32_t* keys)
{
    int const a0 = face_a[f], a1 = face_a[f + 1], a2 = face_a[f + 2], a3 = face_a[f + 3];
    int const b0 = face_b[f], b1 = face_b[f + 1], b2 = face_b[f + 2], b3 = face_b[f + 3];
    int const c0 = face_c[f], c1 = face_c[f + 1], c2 = face_c[f + 2], c3 = face_c[f + 3];

    /* vtrnq_s32({x0,y0,z0,_}, {x1,y1,z1,_}) is {x0,x1,z0,z1} and {y0,y1,_,_}:
     * the x's and z's of two vertices in one vector, the y's in the other.
     * Combining the low halves of two such pairs gives the axis across four
     * vertices; the high halves of the first give z. The fourth column is
     * the padding and is dropped. */
    int32x4x2_t const ta01 = vtrnq_s32(vld1q_s32(xyz + a0 * 4), vld1q_s32(xyz + a1 * 4));
    int32x4x2_t const ta23 = vtrnq_s32(vld1q_s32(xyz + a2 * 4), vld1q_s32(xyz + a3 * 4));
    int32x4x2_t const tb01 = vtrnq_s32(vld1q_s32(xyz + b0 * 4), vld1q_s32(xyz + b1 * 4));
    int32x4x2_t const tb23 = vtrnq_s32(vld1q_s32(xyz + b2 * 4), vld1q_s32(xyz + b3 * 4));
    int32x4x2_t const tc01 = vtrnq_s32(vld1q_s32(xyz + c0 * 4), vld1q_s32(xyz + c1 * 4));
    int32x4x2_t const tc23 = vtrnq_s32(vld1q_s32(xyz + c2 * 4), vld1q_s32(xyz + c3 * 4));
    int32x4_t const ax = vcombine_s32(vget_low_s32(ta01.val[0]), vget_low_s32(ta23.val[0]));
    int32x4_t const ay = vcombine_s32(vget_low_s32(ta01.val[1]), vget_low_s32(ta23.val[1]));
    int32x4_t const az = vcombine_s32(vget_high_s32(ta01.val[0]), vget_high_s32(ta23.val[0]));
    int32x4_t const bx = vcombine_s32(vget_low_s32(tb01.val[0]), vget_low_s32(tb23.val[0]));
    int32x4_t const by = vcombine_s32(vget_low_s32(tb01.val[1]), vget_low_s32(tb23.val[1]));
    int32x4_t const bz = vcombine_s32(vget_high_s32(tb01.val[0]), vget_high_s32(tb23.val[0]));
    int32x4_t const cx = vcombine_s32(vget_low_s32(tc01.val[0]), vget_low_s32(tc23.val[0]));
    int32x4_t const cy = vcombine_s32(vget_low_s32(tc01.val[1]), vget_low_s32(tc23.val[1]));
    int32x4_t const cz = vcombine_s32(vget_high_s32(tc01.val[0]), vget_high_s32(tc23.val[0]));

    /* Winding, 64-bit, as graphics/winding.h: the deltas are int, only the
     * product is widened, and a truncating 32-bit product would be wrong
     * past |delta| ~ 32k, which pre-clip coordinates reach. vmull_s32 /
     * vmlsl_s32 on the halves is what A32 has in place of the _high forms. */
    int32x4_t const dx1 = vsubq_s32(ax, bx);
    int32x4_t const dy1 = vsubq_s32(ay, by);
    int32x4_t const dx2 = vsubq_s32(cx, bx);
    int32x4_t const dy2 = vsubq_s32(cy, by);
    /* Built NEGATED -- nw = dy1*dx2 - dx1*dy2 = -(dx1*dy2 - dy1*dx2) -- so
     * the sign test is one instruction; see toridraw_winding_front_neon32. */
    int64x2_t nw_lo = vmull_s32(vget_low_s32(dy1), vget_low_s32(dx2));
    int64x2_t nw_hi = vmull_s32(vget_high_s32(dy1), vget_high_s32(dx2));
    nw_lo = vmlsl_s32(nw_lo, vget_low_s32(dx1), vget_low_s32(dy2));
    nw_hi = vmlsl_s32(nw_hi, vget_high_s32(dx1), vget_high_s32(dy2));
    uint32x4_t const front = toridraw_winding_front_neon32(nw_lo, nw_hi);

    /* A clipped vertex has sentinel x and no screen-space winding yet: the
     * face is kept and the near-plane rebuild decides. Only the clipped
     * variant asks -- `near_clipped` is a per-MODEL fact the projection
     * decided, so the plain variant, which is nearly every model, carries
     * neither the three compares nor the flag. Same split the bucket sort
     * and the projection families make. */
    uint32x4_t const clip = spec_clipped
        ? vorrq_u32(
              vorrq_u32(vceqq_s32(ax, near_clip_sentinel), vceqq_s32(bx, near_clip_sentinel)),
              vceqq_s32(cx, near_clip_sentinel))
        : vdupq_n_u32(0);

    /* depth = (z_sum * 21845) >> 16 + min_depth, as div3_fast_fixedpoint. */
    int32x4_t depth = vaddq_s32(vaddq_s32(az, bz), cz);
    depth = vshrq_n_s32(vmulq_n_s32(depth, 21845), 16);
    depth = vaddq_s32(depth, min_depth);
    uint32x4_t const in_range = vcltq_u32(vreinterpretq_u32_s32(depth), depth_levels);

    uint32x4_t const accept = spec_clipped ? vandq_u32(vorrq_u32(front, clip), in_range)
                                           : vandq_u32(front, in_range);

    /* key = (0xFFFF - depth) << 16 | face, as
     *   key_base - (depth << 16)   with   key_base = 0xFFFF0000 | (f + lane).
     * depth < 0x10000 whenever the face is accepted, so the shifted depth
     * never borrows into the face half; a rejected lane's key is garbage
     * and is replaced by the sentinel below. Two ops, and key_base is a
     * vector the loop advances by four instead of a vdup of f per block. */
    uint32x4_t const key =
        vsubq_u32(key_base, vreinterpretq_u32_s32(vshlq_n_s32(depth, 16)));

    {
        /*
         * NO PACK INSIDE THE BLOCK. The four keys go out as one vector store
         * with the rejected lanes as the sentinel 0xFFFFFFFF; lane_blocks
         * compacts the whole run afterwards in one streaming ARM pass. The
         * pack this replaces needed the mask and the keys on the ARM side
         * -- five NEON-to-ARM moves at the END of the block's chain, each of
         * which stalls the ARM pipeline until the vector work ahead of it
         * retires. Sitting where they did, they charged the whole chain's
         * latency to every block: the loop ran at the chain's latency, not
         * its throughput, and the block measured an IPC of 0.4. With no
         * ARM consumer in the loop the ARM side runs ahead on the next
         * blocks' index loads and addresses while NEON drains this one.
         *
         * (An earlier variant stored the sentinels and sorted them too,
         * which lost: it doubled the sort. The compaction pass is what makes
         * this one pay -- four ARM instructions a face, and the sort sees
         * exactly the accepted keys as before.)
         */
        vst1q_u32(keys, vbslq_u32(accept, key, vdupq_n_u32(0xFFFFFFFFu)));

        if( spec_stash )
        {
            /* The y-sort permutation, all four at once, with the C's `<=` ties:
             *   p = (ya<=yb && ya<=yc) ? (yb<=yc ? 0 : 1)
             *     : (yb<=yc) ? (yc<=ya ? 2 : 3) : (ya<=yb ? 4 : 5)
             * then o0 = p >> 1, o1 = (o0 + 1 + (p & 1)) mod 3, o2 = 3 - o0 - o1. */
            uint32x4_t const A = vcleq_s32(ay, by);
            uint32x4_t const B = vcleq_s32(ay, cy);
            uint32x4_t const C = vcleq_s32(by, cy);
            uint32x4_t const D = vcleq_s32(cy, ay);
            int32x4_t const k0 = vdupq_n_s32(0), k1 = vdupq_n_s32(1), k2 = vdupq_n_s32(2);
            int32x4_t const k3 = vdupq_n_s32(3), k4 = vdupq_n_s32(4), k5 = vdupq_n_s32(5);
            int32x4_t const inner1 = vbslq_s32(C, k0, k1);
            int32x4_t const inner2 = vbslq_s32(D, k2, k3);
            int32x4_t const inner3 = vbslq_s32(A, k4, k5);
            int32x4_t const p = vbslq_s32(vandq_u32(A, B), inner1, vbslq_s32(C, inner2, inner3));
            int32x4_t const o0 = vshrq_n_s32(p, 1);
            int32x4_t v = vaddq_s32(vaddq_s32(o0, k1), vandq_s32(p, k1));
            v = vsubq_s32(v, vandq_s32(vreinterpretq_s32_u32(vcgeq_s32(v, k3)), k3));
            int32x4_t const o1 = v;
            int32x4_t const o2 = vsubq_s32(vsubq_s32(k3, o0), o1);

            int32x4x4_t xq;
            int32x4x4_t yq;
            xq.val[0] = toridraw_sel3_neon32(o0, ax, bx, cx);
            xq.val[1] = toridraw_sel3_neon32(o1, ax, bx, cx);
            xq.val[2] = toridraw_sel3_neon32(o2, ax, bx, cx);
            xq.val[3] = vreinterpretq_s32_u32(vshrq_n_u32(clip, 31));
            yq.val[0] = toridraw_sel3_neon32(o0, ay, by, cy);
            yq.val[1] = toridraw_sel3_neon32(o1, ay, by, cy);
            yq.val[2] = toridraw_sel3_neon32(o2, ay, by, cy);
            yq.val[3] = p;
            /* Interleaving stores: record f gets lane 0 of each, f+1 lane 1...
             * VST4.32 is A32 as well; only the widest A64 forms are not. */
            vst4q_s32(&scene->sm_face_x4[(size_t)f * 4], xq);
            vst4q_s32(&scene->sm_face_y4[(size_t)f * 4], yq);
        }

        return 4;
    }
}


/*
 * THE K16 BLOCK: eight faces at f..f+7 in int16 lanes.
 *
 * WHY IT IS EXACT. lane_blocks admits a model here only when its screen box
 * (scene->projected_box, the raw sweep toridraw_projected_bound made) is
 * under 32767 wide and tall, and it rebuilt the vertices as
 * {x - box_min_x, y - box_min_y, z, 0} int16 quads. Every rebased coordinate
 * is then in [0, 32766], every winding delta in (-32767, 32767) -- an exact
 * int16 -- and each product under 2^30, so the two-product winding fits an
 * int32 with 131 thousand to spare: VMULL.S16 / VMLSL.S16 give the same sign
 * the 64-bit reference does, and the reference's sign is translation
 * invariant, so rebasing changes nothing. z is NOT rebased -- the depth is
 * div3(za + zb + zc) + min_depth on the absolute values -- and is widened
 * to int32 before the sum (VADDL / VADDW), so the depth is the int32 block's
 * bit for bit. The keys are therefore identical, which is what the parity
 * test holds this lane to.
 *
 * WHAT IT SAVES. Twice the faces per gather and per store, 16-bit products
 * in place of the 64-bit VMULL.S32 chain whose latency the four-face block
 * was measured waiting on, and a gather that is four VUZP.16 per corner.
 * The near-clip sentinel and the presort stash are not handled here: a
 * clipped or stashing model takes the int32 block.
 *
 * THE TWO GATHERS. The first shape was D-register transposes: vtrn.16 on
 * quad pairs, then vtrn.32 across pairs of pairs. What clang made of the
 * vtrn.32 half was not vtrn.32 at all -- it had to keep only half of each
 * result, and lowered those halves as 36 vext.32 per block, nearly a fifth
 * of the block's 184 instructions. The unzip shape has no half-used result:
 * two quads in one Q register, VUZP.16 of two such Q's separates the even
 * lanes {x,z} from the odd {y,0}, and a second VUZP.16 across the two
 * four-quad results separates x from z (and y from the padding). Four
 * instructions per corner, each one used whole, in place of eight vtrn plus
 * the extracts. `spec_uzp` is a literal at every call: TORIDRAW_K16_UZP=0
 * keeps the vtrn shape as the control arm.
 */
static inline __attribute__((always_inline)) int
toridraw_face_sort_bitonic_radix_block8_k16_neon32(
    int f,
    int32x4_t min_depth,
    uint32x4_t depth_levels,
    uint32x4_t key_base_lo, /* 0xFFFF0000 | (f + lane), lanes 0..3 */
    uint32x4_t key_base_hi, /* lanes 4..7 */
    int const spec_uzp,     /* literal at every call: which gather shape */
    const int16_t* RESTRICT xyz16,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c,
    uint32_t* keys)
{
    int16x8_t ax, ay, az, bx, by, bz, cx, cy, cz;

/*
 * THE EIGHT INDICES OF ONE CORNER: one `ldrsh` each.
 *
 * MEASURED, NOT ASSUMED. The obvious cut is to read them in pairs -- adjacent
 * faces' indices are adjacent int16, so twelve 32-bit loads carry all
 * twenty-four -- and the trip's load count falls from forty-eight to
 * thirty-six. It does not pay: in-launch, arms alternating every 300 frames
 * (`TORIDRAW_K16_IDX32`, kr17), the worker measured 6.07 ms/frame with the
 * `ldrsh` shape against 6.22 with the pairs, four of seven pairs worse.
 *
 * The reason is the shape of the stall. Each gather is a chain
 * `ldrsh -> add address -> vld1`, and the pair form makes it
 * `ldr -> and/shift -> add -> vld1` -- one link longer, twice per load
 * saved. The region is latency-bound on that chain (`kr16`: IPC 0.85 against
 * a 3-wide machine), not up against the load port, so trading chain length
 * for load count is the wrong direction. Anything here has to SHORTEN the
 * chain, and no A32 addressing mode folds a scaled 16-bit index load into
 * the gather.
 */
/* Eight {x,y,z,0} quads -> one vector per axis, the unzip way: see above.
 * vuzpq_s16(a, b).val[0] is the even lanes of a then b, .val[1] the odd. */
#define TORIDRAW_K16_GATHER_UZP(fa_, ox_, oy_, oz_)                                              \
    do                                                                                             \
    {                                                                                              \
        int16x8_t const q01 = vcombine_s16(                                                        \
            vld1_s16(xyz16 + (size_t)(fa_)[f + 0] * 4),                                            \
            vld1_s16(xyz16 + (size_t)(fa_)[f + 1] * 4));                                           \
        int16x8_t const q23 = vcombine_s16(                                                        \
            vld1_s16(xyz16 + (size_t)(fa_)[f + 2] * 4),                                            \
            vld1_s16(xyz16 + (size_t)(fa_)[f + 3] * 4));                                           \
        int16x8_t const q45 = vcombine_s16(                                                        \
            vld1_s16(xyz16 + (size_t)(fa_)[f + 4] * 4),                                            \
            vld1_s16(xyz16 + (size_t)(fa_)[f + 5] * 4));                                           \
        int16x8_t const q67 = vcombine_s16(                                                        \
            vld1_s16(xyz16 + (size_t)(fa_)[f + 6] * 4),                                            \
            vld1_s16(xyz16 + (size_t)(fa_)[f + 7] * 4));                                           \
        /* {x0,z0,x1,z1,x2,z2,x3,z3} and {y0,0,y1,0,y2,0,y3,0}; same for 4..7 */                   \
        int16x8x2_t const u03 = vuzpq_s16(q01, q23);                                               \
        int16x8x2_t const u47 = vuzpq_s16(q45, q67);                                               \
        /* {x0..x7} and {z0..z7}; {y0..y7} and the padding */                                      \
        int16x8x2_t const xz = vuzpq_s16(u03.val[0], u47.val[0]);                                  \
        int16x8x2_t const yw = vuzpq_s16(u03.val[1], u47.val[1]);                                  \
        (ox_) = xz.val[0];                                                                         \
        (oz_) = xz.val[1];                                                                         \
        (oy_) = yw.val[0];                                                                         \
    } while( 0 )

/* The control arm: vtrn_s16 on two quads gives {x0,x1,z0,z1} and
 * {y0,y1,w0,w1}; vtrn_s32 across two such pairs gives {x0..x3} and {z0..z3}
 * from the first, {y0..y3} from the second. */
#define TORIDRAW_K16_GATHER(fa_, ox_, oy_, oz_)                                                  \
    do                                                                                             \
    {                                                                                              \
        int16x4_t const r0 = vld1_s16(xyz16 + (size_t)(fa_)[f + 0] * 4);                        \
        int16x4_t const r1 = vld1_s16(xyz16 + (size_t)(fa_)[f + 1] * 4);                        \
        int16x4_t const r2 = vld1_s16(xyz16 + (size_t)(fa_)[f + 2] * 4);                        \
        int16x4_t const r3 = vld1_s16(xyz16 + (size_t)(fa_)[f + 3] * 4);                        \
        int16x4_t const r4 = vld1_s16(xyz16 + (size_t)(fa_)[f + 4] * 4);                        \
        int16x4_t const r5 = vld1_s16(xyz16 + (size_t)(fa_)[f + 5] * 4);                        \
        int16x4_t const r6 = vld1_s16(xyz16 + (size_t)(fa_)[f + 6] * 4);                        \
        int16x4_t const r7 = vld1_s16(xyz16 + (size_t)(fa_)[f + 7] * 4);                        \
        int16x4x2_t const t01 = vtrn_s16(r0, r1);                                                 \
        int16x4x2_t const t23 = vtrn_s16(r2, r3);                                                 \
        int16x4x2_t const t45 = vtrn_s16(r4, r5);                                                 \
        int16x4x2_t const t67 = vtrn_s16(r6, r7);                                                 \
        int32x2x2_t const xz03 = vtrn_s32(                                                        \
            vreinterpret_s32_s16(t01.val[0]), vreinterpret_s32_s16(t23.val[0]));                \
        int32x2x2_t const yw03 = vtrn_s32(                                                        \
            vreinterpret_s32_s16(t01.val[1]), vreinterpret_s32_s16(t23.val[1]));                \
        int32x2x2_t const xz47 = vtrn_s32(                                                        \
            vreinterpret_s32_s16(t45.val[0]), vreinterpret_s32_s16(t67.val[0]));                \
        int32x2x2_t const yw47 = vtrn_s32(                                                        \
            vreinterpret_s32_s16(t45.val[1]), vreinterpret_s32_s16(t67.val[1]));                \
        (ox_) = vcombine_s16(                                                                     \
            vreinterpret_s16_s32(xz03.val[0]), vreinterpret_s16_s32(xz47.val[0]));              \
        (oz_) = vcombine_s16(                                                                     \
            vreinterpret_s16_s32(xz03.val[1]), vreinterpret_s16_s32(xz47.val[1]));              \
        (oy_) = vcombine_s16(                                                                     \
            vreinterpret_s16_s32(yw03.val[0]), vreinterpret_s16_s32(yw47.val[0]));              \
    } while( 0 )

    if( spec_uzp )
    {
        TORIDRAW_K16_GATHER_UZP(face_a, ax, ay, az);
        TORIDRAW_K16_GATHER_UZP(face_b, bx, by, bz);
        TORIDRAW_K16_GATHER_UZP(face_c, cx, cy, cz);
    }
    else
    {
        TORIDRAW_K16_GATHER(face_a, ax, ay, az);
        TORIDRAW_K16_GATHER(face_b, bx, by, bz);
        TORIDRAW_K16_GATHER(face_c, cx, cy, cz);
    }
#undef TORIDRAW_K16_GATHER
#undef TORIDRAW_K16_GATHER_UZP

    {
        /* nw = dy1*dx2 - dx1*dy2, the negated winding, as the int32 block:
         * front is nw < 0, one arithmetic shift. */
        int16x8_t const dx1 = vsubq_s16(ax, bx);
        int16x8_t const dy1 = vsubq_s16(ay, by);
        int16x8_t const dx2 = vsubq_s16(cx, bx);
        int16x8_t const dy2 = vsubq_s16(cy, by);
        int32x4_t nw_lo = vmull_s16(vget_low_s16(dy1), vget_low_s16(dx2));
        int32x4_t nw_hi = vmull_s16(vget_high_s16(dy1), vget_high_s16(dx2));
        uint32x4_t front_lo;
        uint32x4_t front_hi;
        nw_lo = vmlsl_s16(nw_lo, vget_low_s16(dx1), vget_low_s16(dy2));
        nw_hi = vmlsl_s16(nw_hi, vget_high_s16(dx1), vget_high_s16(dy2));
#if TORIDRAW_FLIP_WINDING
        front_lo = vcgtq_s32(nw_lo, vdupq_n_s32(0));
        front_hi = vcgtq_s32(nw_hi, vdupq_n_s32(0));
#else
        front_lo = vreinterpretq_u32_s32(vshrq_n_s32(nw_lo, 31));
        front_hi = vreinterpretq_u32_s32(vshrq_n_s32(nw_hi, 31));
#endif

        {
            /* depth = (za + zb + zc) * 21845 >> 16 + min_depth, the z's
             * widened first: the int32 block's arithmetic exactly. */
            int32x4_t zs_lo = vaddl_s16(vget_low_s16(az), vget_low_s16(bz));
            int32x4_t zs_hi = vaddl_s16(vget_high_s16(az), vget_high_s16(bz));
            int32x4_t depth_lo;
            int32x4_t depth_hi;
            uint32x4_t accept_lo;
            uint32x4_t accept_hi;
            uint32x4_t const sentinel = vdupq_n_u32(0xFFFFFFFFu);
            zs_lo = vaddw_s16(zs_lo, vget_low_s16(cz));
            zs_hi = vaddw_s16(zs_hi, vget_high_s16(cz));
            depth_lo = vaddq_s32(vshrq_n_s32(vmulq_n_s32(zs_lo, 21845), 16), min_depth);
            depth_hi = vaddq_s32(vshrq_n_s32(vmulq_n_s32(zs_hi, 21845), 16), min_depth);
            accept_lo = vandq_u32(
                front_lo, vcltq_u32(vreinterpretq_u32_s32(depth_lo), depth_levels));
            accept_hi = vandq_u32(
                front_hi, vcltq_u32(vreinterpretq_u32_s32(depth_hi), depth_levels));
            vst1q_u32(
                keys,
                vbslq_u32(
                    accept_lo,
                    vsubq_u32(key_base_lo, vreinterpretq_u32_s32(vshlq_n_s32(depth_lo, 16))),
                    sentinel));
            vst1q_u32(
                keys + 4,
                vbslq_u32(
                    accept_hi,
                    vsubq_u32(key_base_hi, vreinterpretq_u32_s32(vshlq_n_s32(depth_hi, 16))),
                    sentinel));
        }
    }
    return 8;
}

/*
 * The bitonic network over N = 2^k keys, ascending, unsigned.
 *
 * Byte for byte the A64 network: every instruction it uses -- VMIN.U32,
 * VMAX.U32, VREV64.32, VEXT.8, VBSL -- is in the A32 encoding too, so this is a
 * copy rather than a port. It lives here rather than in a shared file because
 * the lane files are the unit a build includes exactly one of.
 *
 * Strides of four and above compare two whole vectors; the two strides
 * inside a vector are a lane rotate (stride 2) and a pair swap (stride 1)
 * with a select mask that says which lanes keep the max. Direction per
 * block of k follows (i & k) == 0, which is constant across a vector once
 * k >= 4; the k == 2 opening stage is the one place it alternates inside
 * the vector, and it has its own mask.
 */
static inline uint32x4_t
toridraw_bitonic_inner_neon32(
    uint32x4_t v,
    int k,
    int asc)
{
    uint32x4_t const m2a = { 0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu };  /* stride 2: lanes 2,3 take max */
    uint32x4_t const m1a = { 0, 0xFFFFFFFFu, 0, 0xFFFFFFFFu };  /* stride 1: lanes 1,3 take max */
    uint32x4_t const m1k2 = { 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0 }; /* k == 2: asc pair, desc pair */
    uint32x4_t p;
    uint32x4_t mn;
    uint32x4_t mx;

    if( k == 2 )
    {
        p = vrev64q_u32(v);
        mn = vminq_u32(v, p);
        mx = vmaxq_u32(v, p);
        return vbslq_u32(m1k2, mx, mn);
    }
    p = vextq_u32(v, v, 2);
    mn = vminq_u32(v, p);
    mx = vmaxq_u32(v, p);
    v = asc ? vbslq_u32(m2a, mx, mn) : vbslq_u32(m2a, mn, mx);
    p = vrev64q_u32(v);
    mn = vminq_u32(v, p);
    mx = vmaxq_u32(v, p);
    return asc ? vbslq_u32(m1a, mx, mn) : vbslq_u32(m1a, mn, mx);
}

static void
toridraw_bitonic_sort_u32_neon32(
    uint32_t* a,
    int N)
{
    int k;
    int j;
    int i;

    for( k = 2; k <= N; k <<= 1 )
    {
        for( j = k >> 1; j >= 4; j >>= 1 )
        {
            for( i = 0; i < N; i += 4 )
            {
                uint32x4_t va;
                uint32x4_t vb;
                uint32x4_t mn;
                uint32x4_t mx;
                if( i & j )
                    continue;
                va = vld1q_u32(a + i);
                vb = vld1q_u32(a + (i ^ j));
                mn = vminq_u32(va, vb);
                mx = vmaxq_u32(va, vb);
                if( (i & k) == 0 )
                {
                    vst1q_u32(a + i, mn);
                    vst1q_u32(a + (i ^ j), mx);
                }
                else
                {
                    vst1q_u32(a + i, mx);
                    vst1q_u32(a + (i ^ j), mn);
                }
            }
        }
        for( i = 0; i < N; i += 4 )
            vst1q_u32(a + i, toridraw_bitonic_inner_neon32(vld1q_u32(a + i), k, (i & k) == 0));
    }
}

/*
 * THE SAME NETWORK, WITH THE CONTROL FLOW TAKEN OUT OF THE VECTOR LOOP.
 * TORIDRAW_SORT_BITONIC2=0 selects the loop above as the control arm.
 *
 * Three things the loop above pays per vector that are not the network:
 *
 *   - `if( i & j ) continue` visits every fourth-lane index and skips half
 *     of them, six instructions a skip. Here the merge is walked as blocks
 *     of 2j: the low half of a block pairs with its high half, so every
 *     trip is a real compare-swap, and the direction ((base & k) == 0) is
 *     one test per block rather than one per vector, since 2j <= k makes it
 *     constant across the block.
 *   - the in-register stages' three select masks were literal constants
 *     inside the inlined helper, which clang materialised from the
 *     constant pool at every use. They are built once here and handed in.
 *   - the pad to a power of two was a scalar loop (an __aeabi_memset8
 *     call): lane_sort now stores it four sentinels a vector.
 *
 * The k == 2 stage is its own loop: its mask encodes both directions, so
 * it has no asc/desc split. For k >= 4 the in-register stage runs
 * ascending over the first k of every 2k keys and descending over the
 * second k -- (i & k) == 0 in the loop above, without asking per vector.
 */
static inline __attribute__((always_inline)) uint32x4_t
toridraw_bitonic_inner2_asc_neon32(
    uint32x4_t v,
    uint32x4_t m2a,
    uint32x4_t m1a)
{
    uint32x4_t p = vextq_u32(v, v, 2);
    v = vbslq_u32(m2a, vmaxq_u32(v, p), vminq_u32(v, p));
    p = vrev64q_u32(v);
    return vbslq_u32(m1a, vmaxq_u32(v, p), vminq_u32(v, p));
}

static inline __attribute__((always_inline)) uint32x4_t
toridraw_bitonic_inner2_desc_neon32(
    uint32x4_t v,
    uint32x4_t m2a,
    uint32x4_t m1a)
{
    uint32x4_t p = vextq_u32(v, v, 2);
    v = vbslq_u32(m2a, vminq_u32(v, p), vmaxq_u32(v, p));
    p = vrev64q_u32(v);
    return vbslq_u32(m1a, vminq_u32(v, p), vmaxq_u32(v, p));
}

static void
toridraw_bitonic_sort_u32_neon32_v2(
    uint32_t* a,
    int N)
{
    uint32x4_t const m2a = { 0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu };  /* stride 2: lanes 2,3 take max */
    uint32x4_t const m1a = { 0, 0xFFFFFFFFu, 0, 0xFFFFFFFFu };  /* stride 1: lanes 1,3 take max */
    uint32x4_t const m1k2 = { 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0 }; /* k == 2: asc pair, desc pair */
    int k;
    int j;
    int base;
    int i;

    assert(a);
    assert(N >= 4);
    assert((N & (N - 1)) == 0);

    /* k == 2: one in-register stage, one mask for both directions. */
    for( i = 0; i < N; i += 4 )
    {
        uint32x4_t const v = vld1q_u32(a + i);
        uint32x4_t const p = vrev64q_u32(v);
        vst1q_u32(a + i, vbslq_u32(m1k2, vmaxq_u32(v, p), vminq_u32(v, p)));
    }

    for( k = 4; k <= N; k <<= 1 )
    {
        for( j = k >> 1; j >= 4; j >>= 1 )
        {
            for( base = 0; base < N; base += 2 * j )
            {
                uint32_t* const lo = a + base;
                uint32_t* const hi = lo + j;
                if( (base & k) == 0 )
                {
                    for( i = 0; i < j; i += 4 )
                    {
                        uint32x4_t const va = vld1q_u32(lo + i);
                        uint32x4_t const vb = vld1q_u32(hi + i);
                        vst1q_u32(lo + i, vminq_u32(va, vb));
                        vst1q_u32(hi + i, vmaxq_u32(va, vb));
                    }
                }
                else
                {
                    for( i = 0; i < j; i += 4 )
                    {
                        uint32x4_t const va = vld1q_u32(lo + i);
                        uint32x4_t const vb = vld1q_u32(hi + i);
                        vst1q_u32(lo + i, vmaxq_u32(va, vb));
                        vst1q_u32(hi + i, vminq_u32(va, vb));
                    }
                }
            }
        }
        for( base = 0; base < N; base += 2 * k )
        {
            for( i = base; i < base + k; i += 4 )
                vst1q_u32(a + i, toridraw_bitonic_inner2_asc_neon32(vld1q_u32(a + i), m2a, m1a));
            for( i = base + k; i < base + 2 * k && i < N; i += 4 )
                vst1q_u32(a + i, toridraw_bitonic_inner2_desc_neon32(vld1q_u32(a + i), m2a, m1a));
        }
    }
}

/* ---- the lane hooks --------------------------------------------------- */

static inline int
toridraw_face_sort_bitonic_radix_lane_blocks(
    struct ToriDraw_Scene* scene,
    int* f_io,
    int num_faces,
    int num_vertices,
    bool near_clipped,
    int model_min_depth,
    int stash_xy,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c,
    uint32_t* keys,
    int* out_accepted)
{
    /* The three loop-invariant vectors are built here rather than at the
     * caller because they are the lane's own currency: the dispatcher does not
     * know what shape a sentinel has on this ISA, and no longer needs to.
     * They read `scene` and `f_io`, so the asserts come first. */
    int32x4_t sentinel;
    int32x4_t min_depth;
    uint32x4_t levels;
    uint32x4_t key_base;
    uint32x4_t const four = vdupq_n_u32(4);
    /* TORIDRAW_SORT_PLD: prefetch the three index streams a line ahead of
     * every block. A cached read; the test in each loop is one predictable
     * branch per block, which is under a quarter of an instruction per face. */
    int const pld = toridraw_face_sort_pld_armed();
    int* xyz;
    int f;
    int v;
    int n = 0;

    assert(scene);
    assert(f_io);
    assert(vx);
    assert(vy);
    assert(vz);
    assert(face_a);
    assert(face_b);
    assert(face_c);
    assert(keys);
    assert(out_accepted);

    f = *f_io;
    /* Nothing for the vector path to do (a two-face tile that declined its
     * kernel, a three-face model): no interleave either. */
    if( f + 4 > num_faces )
    {
        *out_accepted = 0;
        return 0;
    }

    /*
     * THE K16 QUESTION, asked once per model off the screen box the
     * projection already swept (scene->projected_box): a model under 32767
     * wide and tall, not near-clipped, not stashing, with at least eight
     * faces from here, takes the eight-face int16 block. The vertices are
     * rebuilt as int16 {x - min_x, y - min_y, z, 0} quads in one pass; the
     * pass also checks that z fits, and a model whose z range does not
     * (a radius past 32K units, which nothing in the caches has) falls back
     * to the int32 block below. TORIDRAW_FACE_SORT_K16=0 is the control arm.
     */
    if( !near_clipped && !stash_xy && f + 8 <= num_faces && toridraw_face_sort_k16_armed() )
    {
        int const ext_x = scene->projected_box[1] - scene->projected_box[0];
        int const ext_y = scene->projected_box[3] - scene->projected_box[2];
        if( ext_x >= 0 && ext_x < 32767 && ext_y >= 0 && ext_y < 32767 )
        {
            int16_t* const xyz16 = scene->sm_vertex_xyz16;
            int32x4_t const origin_x = vdupq_n_s32(scene->projected_box[0]);
            int32x4_t const origin_y = vdupq_n_s32(scene->projected_box[2]);
            int32x4_t zmin = vdupq_n_s32(INT_MAX);
            int32x4_t zmax = vdupq_n_s32(INT_MIN);
            int16x4x4_t quad16;
            int z_lo;
            int z_hi;
            assert(xyz16);
            quad16.val[3] = vdup_n_s16(0);
            for( v = 0; v + 4 <= num_vertices; v += 4 )
            {
                int32x4_t const qx = vsubq_s32(vld1q_s32(vx + v), origin_x);
                int32x4_t const qy = vsubq_s32(vld1q_s32(vy + v), origin_y);
                int32x4_t const qz = vld1q_s32(vz + v);
                zmin = vminq_s32(zmin, qz);
                zmax = vmaxq_s32(zmax, qz);
                quad16.val[0] = vmovn_s32(qx);
                quad16.val[1] = vmovn_s32(qy);
                quad16.val[2] = vmovn_s32(qz);
                vst4_s16(xyz16 + (size_t)v * 4, quad16);
            }
            {
                int32x2_t lo2 = vpmin_s32(vget_low_s32(zmin), vget_high_s32(zmin));
                int32x2_t hi2 = vpmax_s32(vget_low_s32(zmax), vget_high_s32(zmax));
                lo2 = vpmin_s32(lo2, lo2);
                hi2 = vpmax_s32(hi2, hi2);
                z_lo = vget_lane_s32(lo2, 0);
                z_hi = vget_lane_s32(hi2, 0);
            }
            for( ; v < num_vertices; v++ )
            {
                int const z = vz[v];
                xyz16[(size_t)v * 4 + 0] = (int16_t)(vx[v] - scene->projected_box[0]);
                xyz16[(size_t)v * 4 + 1] = (int16_t)(vy[v] - scene->projected_box[2]);
                xyz16[(size_t)v * 4 + 2] = (int16_t)z;
                xyz16[(size_t)v * 4 + 3] = 0;
                /* Not `else if`: when the vector loop did not run (a model
                 * under four vertices) z_lo is still INT_MAX and z_hi still
                 * INT_MIN, and the first z must lower BOTH. */
                if( z < z_lo )
                    z_lo = z;
                if( z > z_hi )
                    z_hi = z;
            }
            if( z_lo >= -32767 && z_hi <= 32767 )
            {
                uint32x4_t const eight = vdupq_n_u32(8);
                uint32x4_t key_base_lo;
                uint32x4_t key_base_hi;
                {
                    uint32x4_t const lane_lo = { 0, 1, 2, 3 };
                    uint32x4_t const lane_hi = { 4, 5, 6, 7 };
                    uint32x4_t const base = vdupq_n_u32(0xFFFF0000u | (uint32_t)f);
                    key_base_lo = vaddq_u32(base, lane_lo);
                    key_base_hi = vaddq_u32(base, lane_hi);
                }
                min_depth = vdupq_n_s32(model_min_depth);
                levels = vdupq_n_u32((uint32_t)scene->depth_levels);
                /* The same depth bound the int32 path publishes (see below). */
                scene->sm_sort_depth_lo = div3_fast_fixedpoint(3 * z_lo) + model_min_depth;
                scene->sm_sort_depth_hi = div3_fast_fixedpoint(3 * z_hi) + model_min_depth;
                g_toridraw_sort_k16_models++;
/* The gather shape is asked once per model (TORIDRAW_K16_UZP), and each
 * answer is a loop whose block was compiled with it as a literal. Every
 * instantiation is another ~740 bytes of a function already past the 16 KB
 * L1-I (§3.9), so an arm that loses is deleted rather than left in the
 * ladder -- see the index-load note above. */
#define TORIDRAW_NEON32_K16_LOOP(spec_uzp_)                                         \
    do                                                                                             \
    {                                                                                              \
        for( ; f + 8 <= num_faces; f += 8 )                                                        \
        {                                                                                          \
            if( pld )                                                                              \
            {                                                                                      \
                __builtin_prefetch(face_a + f + 32);                                               \
                __builtin_prefetch(face_b + f + 32);                                               \
                __builtin_prefetch(face_c + f + 32);                                               \
            }                                                                                      \
            n += toridraw_face_sort_bitonic_radix_block8_k16_neon32(                               \
                f, min_depth, levels, key_base_lo, key_base_hi, (spec_uzp_), xyz16, face_a,        \
                face_b, face_c, keys + n);                                                         \
            key_base_lo = vaddq_u32(key_base_lo, eight);                                           \
            key_base_hi = vaddq_u32(key_base_hi, eight);                                           \
        }                                                                                          \
    } while( 0 )
                if( toridraw_face_sort_k16_uzp_armed() )
                    TORIDRAW_NEON32_K16_LOOP(1);
                else
                    TORIDRAW_NEON32_K16_LOOP(0);
#undef TORIDRAW_NEON32_K16_LOOP

                /*
                 * THE K16 TAIL. The 1..7 faces after the last full block
                 * went to the dispatcher's scalar per-face loop, at about a
                 * hundred instructions a face against the block's twenty-
                 * odd. Instead the block runs once more over the window
                 * ending at num_faces -- it starts at num_faces - 8, which
                 * overlaps the last full block by the faces that block has
                 * already emitted -- and those already-emitted lanes are
                 * turned into sentinels afterwards, which the compaction
                 * below removes like any rejected face. The keys the block
                 * writes for the new faces are the ones the scalar loop
                 * would have (the K16 arithmetic is exact; the parity test
                 * holds it there), so the order is unchanged. num_faces is
                 * at least eight here, so the window is in range.
                 * TORIDRAW_K16_TAIL=0 leaves the tail to the scalar loop.
                 */
                if( f < num_faces && toridraw_face_sort_k16_tail_armed() )
                {
                    int const f_tail = num_faces - 8;
                    int const already = f - f_tail; /* 1..7 lanes the last block wrote */
                    int i;
                    assert(f_tail >= 0);
                    assert(already >= 1);
                    assert(already <= 7);
                    {
                        uint32x4_t const lane_lo = { 0, 1, 2, 3 };
                        uint32x4_t const lane_hi = { 4, 5, 6, 7 };
                        uint32x4_t const base = vdupq_n_u32(0xFFFF0000u | (uint32_t)f_tail);
                        key_base_lo = vaddq_u32(base, lane_lo);
                        key_base_hi = vaddq_u32(base, lane_hi);
                    }
                    if( toridraw_face_sort_k16_uzp_armed() )
                        n += toridraw_face_sort_bitonic_radix_block8_k16_neon32(
                            f_tail, min_depth, levels, key_base_lo, key_base_hi, 1, xyz16, face_a,
                            face_b, face_c, keys + n);
                    else
                        n += toridraw_face_sort_bitonic_radix_block8_k16_neon32(
                            f_tail, min_depth, levels, key_base_lo, key_base_hi, 0, xyz16, face_a,
                            face_b, face_c, keys + n);
                    for( i = 0; i < already; i++ )
                        keys[n - 8 + i] = 0xFFFFFFFFu;
                    f = num_faces;
                    g_toridraw_sort_k16_tail_models++;
                }
                goto compact;
            }
        }
        g_toridraw_sort_k16_declined++;
    }
    else
        g_toridraw_sort_k16_declined++;

    /*
     * Interleave the projected vertices once per model: three streaming
     * loads and one VST4 per four vertices, under an instruction per face
     * against the twelve lane gathers a face's corners cost before (see
     * block4). The scratch holds max_vertices + 4 quads; the axis arrays are
     * exactly max_vertices long, so the last partial quad is done one vertex
     * at a time rather than read past their end.
     */
    xyz = scene->sm_vertex_xyz;
    assert(xyz);
    {
        int32x4x4_t quad;
        /* The z range rides along for two ops a quad: it bounds every
         * face's depth, which is what lets a shallow model's radix finish
         * in one pass (see toridraw_radix_sort_depth). */
        int32x4_t zmin = vdupq_n_s32(INT_MAX);
        int32x4_t zmax = vdupq_n_s32(INT_MIN);
        int z_lo;
        int z_hi;
        quad.val[3] = vdupq_n_s32(0);
        for( v = 0; v + 4 <= num_vertices; v += 4 )
        {
            quad.val[0] = vld1q_s32(vx + v);
            quad.val[1] = vld1q_s32(vy + v);
            quad.val[2] = vld1q_s32(vz + v);
            zmin = vminq_s32(zmin, quad.val[2]);
            zmax = vmaxq_s32(zmax, quad.val[2]);
            vst4q_s32(xyz + (size_t)v * 4, quad);
        }
        {
            int32x2_t lo2 = vpmin_s32(vget_low_s32(zmin), vget_high_s32(zmin));
            int32x2_t hi2 = vpmax_s32(vget_low_s32(zmax), vget_high_s32(zmax));
            lo2 = vpmin_s32(lo2, lo2);
            hi2 = vpmax_s32(hi2, hi2);
            z_lo = vget_lane_s32(lo2, 0);
            z_hi = vget_lane_s32(hi2, 0);
        }
        for( ; v < num_vertices; v++ )
        {
            int const z = vz[v];
            xyz[(size_t)v * 4 + 0] = vx[v];
            xyz[(size_t)v * 4 + 1] = vy[v];
            xyz[(size_t)v * 4 + 2] = z;
            xyz[(size_t)v * 4 + 3] = 0;
            /* Not `else if`; see the K16 pass above. */
            if( z < z_lo )
                z_lo = z;
            if( z > z_hi )
                z_hi = z;
        }
        /*
         * depth = div3_fast_fixedpoint(za + zb + zc) + model_min_depth, and
         * div3_fast_fixedpoint is monotone while its product does not wrap
         * (|z_sum| <= 98,304, i.e. |z| <= 32,767): the extreme depths are
         * then those of three z_lo's and three z_hi's. Outside that range
         * the bound is left unknown rather than trusted.
         */
        if( z_lo >= -32767 && z_hi <= 32767 )
        {
            scene->sm_sort_depth_lo = div3_fast_fixedpoint(3 * z_lo) + model_min_depth;
            scene->sm_sort_depth_hi = div3_fast_fixedpoint(3 * z_hi) + model_min_depth;
        }
    }

    sentinel = vdupq_n_s32(near_clipped ? TORIDRAW_SCREEN_X_NEAR_CLIPPED : INT_MIN);
    min_depth = vdupq_n_s32(model_min_depth);
    levels = vdupq_n_u32((uint32_t)scene->depth_levels);
    {
        uint32x4_t const lane_id = { 0, 1, 2, 3 };
        key_base = vaddq_u32(vdupq_n_u32(0xFFFF0000u | (uint32_t)f), lane_id);
    }

    /*
     * The two questions are asked ONCE, here, and each answer picks a loop
     * whose block was compiled with the answers as literals -- plain,
     * clipped, stash, stash+clipped -- exactly as
     * bucket_sort_by_average_depth_small dispatches to its four loops. The
     * block is always_inline with `int const` flags, so each call below is
     * its own copy with the flag tests folded: the plain loop has no clip
     * compares and no stash body in it at all. On the GPU lanes every model
     * is plain.
     */
#define TORIDRAW_NEON32_BLOCK_LOOP(spec_clipped_, spec_stash_)                                   \
    do                                                                                             \
    {                                                                                              \
        for( ; f + 4 <= num_faces; f += 4 )                                                        \
        {                                                                                          \
            if( pld )                                                                              \
            {                                                                                      \
                __builtin_prefetch(face_a + f + 32);                                               \
                __builtin_prefetch(face_b + f + 32);                                               \
                __builtin_prefetch(face_c + f + 32);                                               \
            }                                                                                      \
            n += toridraw_face_sort_bitonic_radix_block4_neon32(                                   \
                scene, f, sentinel, min_depth, levels, (spec_clipped_), (spec_stash_), key_base,  \
                xyz, face_a, face_b, face_c, keys + n);                                            \
            key_base = vaddq_u32(key_base, four);                                                  \
        }                                                                                          \
    } while( 0 )
    if( near_clipped )
    {
        if( stash_xy )
            TORIDRAW_NEON32_BLOCK_LOOP(1, 1);
        else
            TORIDRAW_NEON32_BLOCK_LOOP(1, 0);
    }
    else
    {
        if( stash_xy )
            TORIDRAW_NEON32_BLOCK_LOOP(0, 1);
        else
            TORIDRAW_NEON32_BLOCK_LOOP(0, 0);
    }
#undef TORIDRAW_NEON32_BLOCK_LOOP

compact:
    *f_io = f;

    /*
     * Compact the run: the blocks wrote four (K16: eight) slots each with
     * rejected faces as sentinels (see block4). One pass, ARM only, branch-free -- the
     * store is unconditional and the cursor advances by the comparison --
     * and the keys handed on are exactly the accepted ones in face order,
     * as a packing lane would have written them.
     */
    {
        int written = 0;
        int i;
        for( i = 0; i < n; i++ )
        {
            uint32_t const k = keys[i];
            keys[written] = k;
            written += (k != 0xFFFFFFFFu);
        }
        n = written;
    }
    *out_accepted = n;
    return n;
}

static inline bool
toridraw_face_sort_bitonic_radix_lane_tile2(
    struct ToriDraw_Scene* scene,
    int tile2_rot,
    bool near_clipped,
    int model_min_depth,
    int stash_xy,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    uint32_t* keys,
    int* out_n)
{
    /*
     * The SCALAR tile kernel, as the SSE2 lane routes it. It is not a vector
     * kernel and needs nothing from this lane: what it saves is the six
     * face-index loads and the two-face model's trip through a four-wide
     * block it can never fill (see toridraw_face_sort_bitonic_radix_tile2_scalar).
     * Measured on the Moto X (Krait, armv7): a Lumbridge frame hands this
     * sort ~1,300 models of which ~760 are terrain tiles, so the tile's
     * per-model cost is most of what the sort costs there. TORIDRAW_TILE_SORT=0
     * is the A/B's control arm (the general path); the SIMD arm was never
     * written for NEON and is not selectable here.
     */
    if( toridraw_face_sort_tile2_armed() != TORIDRAW_TILE_SORT_OFF )
    {
        *out_n = toridraw_face_sort_bitonic_radix_tile2_scalar(
            scene, tile2_rot, near_clipped, model_min_depth, stash_xy, vx, vy, vz, keys);
        return true;
    }
    return TORIDRAW_FACE_SORT_TILE2_DECLINE(
        scene, tile2_rot, near_clipped, model_min_depth, stash_xy, vx, vy, vz, keys, out_n);
}

static inline bool
toridraw_face_sort_bitonic_radix_lane_sort(
    uint32_t* keys,
    int n)
{
    /* The network sorts a power of two. 0xFFFFFFFF sorts to the end and can
     * never be a real key -- a face index is at most 0x7FFF. */
    int N = 4;
    int i;

    assert(keys);

    while( N < n )
        N <<= 1;
    if( toridraw_face_sort_bitonic2_armed() )
    {
        /* The pad, a vector at a time. The stores may run up to three past
         * N; N is at most the power of two the key buffer was sized to, and
         * the buffer has eight lanes of slack past that. */
        uint32x4_t const sentinel = vdupq_n_u32(0xFFFFFFFFu);
        for( i = n; i < N; i += 4 )
            vst1q_u32(keys + i, sentinel);
        toridraw_bitonic_sort_u32_neon32_v2(keys, N);
        return true;
    }
    for( i = n; i < N; i++ )
        keys[i] = 0xFFFFFFFFu;
    toridraw_bitonic_sort_u32_neon32(keys, N);
    return true;
}


/*
 * The emit as vector stores: keys & 0xFFFF, four per vst1q, two vectors a
 * trip. tmp_face_order has no slack, so the last 1..7 go out one at a time.
 * TORIDRAW_SORT_EMIT_VEC=0 is the scalar control arm.
 */
static inline void
toridraw_face_sort_bitonic_radix_lane_emit(
    const uint32_t* RESTRICT keys,
    int n,
    int* RESTRICT out)
{
    int i = 0;

    assert(keys);
    assert(out);

    if( toridraw_face_sort_emit_vec_armed() )
    {
        uint32x4_t const mask = vdupq_n_u32(0xFFFFu);
        for( ; i + 8 <= n; i += 8 )
        {
            vst1q_s32(out + i, vreinterpretq_s32_u32(vandq_u32(vld1q_u32(keys + i), mask)));
            vst1q_s32(
                out + i + 4, vreinterpretq_s32_u32(vandq_u32(vld1q_u32(keys + i + 4), mask)));
        }
        if( i + 4 <= n )
        {
            vst1q_s32(out + i, vreinterpretq_s32_u32(vandq_u32(vld1q_u32(keys + i), mask)));
            i += 4;
        }
    }
    for( ; i < n; i++ )
        out[i] = (int)(keys[i] & 0xFFFFu);
}

#endif /* TORIDRAW_FACE_SORT_BITONIC_RADIX_NEON32_U_C */
