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
 * the dispatcher; this file provides two and declines the terrain tile, for the
 * same reason the A64 file does: both tile kernels were measured against the
 * general path on an x86 host and nothing equivalent has been run on ARM.
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
 *   vqtbl1q_u8                       (full 16-byte table lookup)
 *       -> A32 has only the 8-byte vtbl1/vtbl2, which cannot express a
 *          16-byte left-pack in one go. Rather than stitch two halves, this
 *          takes the SSE2 lane's road, which has no pshufb either: the four
 *          keys go to a stack slot and come back through a 16-entry LANE-INDEX
 *          table with four unconditional scalar stores. Still no branch on the
 *          winding and no data-dependent store address, and it writes the same
 *          four slots the vector store did -- so the hook's "four lanes of
 *          slack past the returned count" contract is unchanged.
 *
 * Everything else in the file -- vld1_s16/vmovl_s16, vmulq_n_s32, vcltq_u32,
 * vbslq, vminq_u32/vmaxq_u32, vrev64q_u32, vextq_u32, vst4q_s32 -- is common to
 * both encodings and is written the same way in both files on purpose.
 */

/*
 * Left-pack: for each 4-bit accept mask, which source lane each of the four
 * destination slots takes. Slots past the popcount are don't-care -- they land
 * at or past the write cursor and are overwritten by the next block or padded
 * over by the sort.
 */
static const uint8_t g_toridraw_pack_idx_neon32[16][4] = {
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 1, 0, 0, 0 },
    { 0, 1, 0, 0 },
    { 2, 0, 0, 0 },
    { 0, 2, 0, 0 },
    { 1, 2, 0, 0 },
    { 0, 1, 2, 0 },
    { 3, 0, 0, 0 },
    { 0, 3, 0, 0 },
    { 1, 3, 0, 0 },
    { 0, 1, 3, 0 },
    { 2, 3, 0, 0 },
    { 0, 2, 3, 0 },
    { 1, 2, 3, 0 },
    { 0, 1, 2, 3 },
};
static const uint8_t g_toridraw_popcount4_neon32[16] = { 0, 1, 1, 2, 1, 2, 2, 3,
                                                         1, 2, 2, 3, 2, 3, 3, 4 };

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
 * Four faces at f..f+3: cull, key, pack, stash. Returns how many keys were
 * appended at *keys; always writes four slots there, so the buffer needs four
 * lanes of slack past the count.
 */
static inline int
toridraw_face_sort_bitonic_radix_block4_neon32(
    struct ToriDraw_Scene* scene,
    int f,
    int32x4_t near_clip_sentinel, /* -5000 x4 when near_clipped, else INT_MIN x4 */
    int32x4_t min_depth,
    uint32x4_t depth_levels,
    int stash_xy,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c,
    uint32_t* keys)
{
    int32x4_t const ia = vmovl_s16(vld1_s16(face_a + f));
    int32x4_t const ib = vmovl_s16(vld1_s16(face_b + f));
    int32x4_t const ic = vmovl_s16(vld1_s16(face_c + f));
    int const a0 = vgetq_lane_s32(ia, 0), a1 = vgetq_lane_s32(ia, 1);
    int const a2 = vgetq_lane_s32(ia, 2), a3 = vgetq_lane_s32(ia, 3);
    int const b0 = vgetq_lane_s32(ib, 0), b1 = vgetq_lane_s32(ib, 1);
    int const b2 = vgetq_lane_s32(ib, 2), b3 = vgetq_lane_s32(ib, 3);
    int const c0 = vgetq_lane_s32(ic, 0), c1 = vgetq_lane_s32(ic, 1);
    int const c2 = vgetq_lane_s32(ic, 2), c3 = vgetq_lane_s32(ic, 3);

    /* The gather, into structure-of-arrays vectors: one axis per vector. */
    int32x4_t const ax = { vx[a0], vx[a1], vx[a2], vx[a3] };
    int32x4_t const bx = { vx[b0], vx[b1], vx[b2], vx[b3] };
    int32x4_t const cx = { vx[c0], vx[c1], vx[c2], vx[c3] };
    int32x4_t const ay = { vy[a0], vy[a1], vy[a2], vy[a3] };
    int32x4_t const by = { vy[b0], vy[b1], vy[b2], vy[b3] };
    int32x4_t const cy = { vy[c0], vy[c1], vy[c2], vy[c3] };
    int32x4_t const az = { vz[a0], vz[a1], vz[a2], vz[a3] };
    int32x4_t const bz = { vz[b0], vz[b1], vz[b2], vz[b3] };
    int32x4_t const cz = { vz[c0], vz[c1], vz[c2], vz[c3] };

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
        /* movemask: 0/1 per lane, weighted, then folded pairwise -- A32 has no
         * vaddvq_u32, and two VPADD.I32 are the whole difference. */
        uint32x4_t const weights = { 1, 2, 4, 8 };
        uint32x4_t const bits = vmulq_u32(vshrq_n_u32(accept, 31), weights);
        uint32x2_t fold = vpadd_u32(vget_low_u32(bits), vget_high_u32(bits));
        unsigned m;
        const uint8_t* t;
        uint32_t lane[4];

        fold = vpadd_u32(fold, fold);
        m = vget_lane_u32(fold, 0);

        /* Left-pack through the index table: four unconditional stores, in
         * place of the A64 vqtbl1q_u8 byte shuffle. */
        t = g_toridraw_pack_idx_neon32[m];
        vst1q_u32(lane, key);
        keys[0] = lane[t[0]];
        keys[1] = lane[t[1]];
        keys[2] = lane[t[2]];
        keys[3] = lane[t[3]];

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

        return g_toridraw_popcount4_neon32[m];
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
    bool near_clipped,
    int model_min_depth,
    int stash_xy,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c,
    uint32_t* keys)
{
    /* The three loop-invariant vectors are built here rather than at the
     * caller because they are the lane's own currency: the dispatcher does not
     * know what shape a sentinel has on this ISA, and no longer needs to.
     * They read `scene` and `f_io`, so the asserts come first. */
    int32x4_t sentinel;
    int32x4_t min_depth;
    uint32x4_t levels;
    int f;
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

    sentinel = vdupq_n_s32(near_clipped ? TORIDRAW_SCREEN_X_NEAR_CLIPPED : INT_MIN);
    min_depth = vdupq_n_s32(model_min_depth);
    levels = vdupq_n_u32((uint32_t)scene->depth_levels);
    f = *f_io;

    for( ; f + 4 <= num_faces; f += 4 )
        n += toridraw_face_sort_bitonic_radix_block4_neon32(
            scene,
            f,
            sentinel,
            min_depth,
            levels,
            stash_xy,
            vx,
            vy,
            vz,
            face_a,
            face_b,
            face_c,
            keys + n);

    *f_io = f;
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
    /* No tile kernel is measured on NEON; see the header. */
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
