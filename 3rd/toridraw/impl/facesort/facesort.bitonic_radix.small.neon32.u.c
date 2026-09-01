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
    int64x2_t w_lo,
    int64x2_t w_hi)
{
    /* vshrn_n_s64(w, 32) is an arithmetic shift, so the high word arrives
     * signed; vmovn_u64(w) is the low 32 bits, which are unsigned. */
    int32x4_t const high = vcombine_s32(vshrn_n_s64(w_lo, 32), vshrn_n_s64(w_hi, 32));
    int32x4_t const zero = vdupq_n_s32(0);
#if TORIDRAW_FLIP_WINDING
    /* w < 0 iff its high word is negative, whatever the low word holds. */
    return vcltq_s32(high, zero);
#else
    {
        uint32x4_t const low = vcombine_u32(
            vmovn_u64(vreinterpretq_u64_s64(w_lo)), vmovn_u64(vreinterpretq_u64_s64(w_hi)));
        /* vtstq_u32(low, low) is `low != 0`. */
        uint32x4_t const low_nonzero = vtstq_u32(low, low);
        return vorrq_u32(vcgtq_s32(high, zero), vandq_u32(vceqq_s32(high, zero), low_nonzero));
    }
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
static inline int
toridraw_face_sort_bitonic_radix_block4_neon32(
    struct ToriDraw_Scene* scene,
    int f,
    int32x4_t near_clip_sentinel, /* -5000 x4 when near_clipped, else INT_MIN x4 */
    int32x4_t min_depth,
    uint32x4_t depth_levels,
    int stash_xy,
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
    int64x2_t w_lo = vmull_s32(vget_low_s32(dx1), vget_low_s32(dy2));
    int64x2_t w_hi = vmull_s32(vget_high_s32(dx1), vget_high_s32(dy2));
    w_lo = vmlsl_s32(w_lo, vget_low_s32(dy1), vget_low_s32(dx2));
    w_hi = vmlsl_s32(w_hi, vget_high_s32(dy1), vget_high_s32(dx2));
    uint32x4_t const front = toridraw_winding_front_neon32(w_lo, w_hi);

    /* A clipped vertex has sentinel x and no screen-space winding yet: the
     * face is kept and the near-plane rebuild decides. With near_clipped
     * false the sentinel vector is INT_MIN and this never matches. */
    uint32x4_t const clip = vorrq_u32(
        vorrq_u32(vceqq_s32(ax, near_clip_sentinel), vceqq_s32(bx, near_clip_sentinel)),
        vceqq_s32(cx, near_clip_sentinel));

    /* depth = (z_sum * 21845) >> 16 + min_depth, as div3_fast_fixedpoint. */
    int32x4_t depth = vaddq_s32(vaddq_s32(az, bz), cz);
    depth = vshrq_n_s32(vmulq_n_s32(depth, 21845), 16);
    depth = vaddq_s32(depth, min_depth);
    uint32x4_t const in_range = vcltq_u32(vreinterpretq_u32_s32(depth), depth_levels);

    uint32x4_t const accept = vandq_u32(vorrq_u32(front, clip), in_range);

    /* key = (0xFFFF - depth) << 16 | face */
    uint32x4_t key = vreinterpretq_u32_s32(vsubq_s32(vdupq_n_s32(0xFFFF), depth));
    key = vshlq_n_u32(key, 16);
    {
        uint32x4_t const lane_id = { 0, 1, 2, 3 };
        key = vorrq_u32(key, vaddq_u32(vdupq_n_u32((uint32_t)f), lane_id));
    }

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

        if( stash_xy )
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
        quad.val[3] = vdupq_n_s32(0);
        for( v = 0; v + 4 <= num_vertices; v += 4 )
        {
            quad.val[0] = vld1q_s32(vx + v);
            quad.val[1] = vld1q_s32(vy + v);
            quad.val[2] = vld1q_s32(vz + v);
            vst4q_s32(xyz + (size_t)v * 4, quad);
        }
        for( ; v < num_vertices; v++ )
        {
            xyz[(size_t)v * 4 + 0] = vx[v];
            xyz[(size_t)v * 4 + 1] = vy[v];
            xyz[(size_t)v * 4 + 2] = vz[v];
            xyz[(size_t)v * 4 + 3] = 0;
        }
    }

    sentinel = vdupq_n_s32(near_clipped ? TORIDRAW_SCREEN_X_NEAR_CLIPPED : INT_MIN);
    min_depth = vdupq_n_s32(model_min_depth);
    levels = vdupq_n_u32((uint32_t)scene->depth_levels);

    for( ; f + 4 <= num_faces; f += 4 )
        n += toridraw_face_sort_bitonic_radix_block4_neon32(
            scene,
            f,
            sentinel,
            min_depth,
            levels,
            stash_xy,
            xyz,
            face_a,
            face_b,
            face_c,
            keys + n);

    *f_io = f;

    /*
     * Compact the run: the blocks wrote four slots each with rejected faces
     * as sentinels (see block4). One pass, ARM only, branch-free -- the
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
    for( i = n; i < N; i++ )
        keys[i] = 0xFFFFFFFFu;
    toridraw_bitonic_sort_u32_neon32(keys, N);
    return true;
}

#endif /* TORIDRAW_FACE_SORT_BITONIC_RADIX_NEON32_U_C */
