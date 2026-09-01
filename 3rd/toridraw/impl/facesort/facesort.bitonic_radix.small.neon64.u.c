#ifndef TORIDRAW_FACE_SORT_BITONIC_RADIX_NEON64_U_C
#define TORIDRAW_FACE_SORT_BITONIC_RADIX_NEON64_U_C

#include "impl/facesort/facesort.bitonic_radix.small.dispatch.h"

#include <arm_neon.h>

#include <limits.h>

/*
 * The AArch64 lane of the bitonic+radix face sort. See
 * facesort.bitonic_radix.small.dispatch.h for the three hooks every lane owes
 * the dispatcher; this file provides all
 * three. The terrain tile takes the scalar tile kernel (measured on an M4 Max:
 * 5.84 -> 5.09 ns/face, the same shape as on x86 and A32 -- the saving is the
 * per-model cost of a two-face model, not vector width).
 *
 * WHAT WAS MEASURED HERE (M4 Max, sort bench, keys arm, presort off, ns per
 * input face at 64/200/256/1000/2000 faces, interleaved A/B runs):
 *
 *   near_clipped / stash folded to per-model constants   neutral (the core hid
 *                                                        the compares; kept for
 *                                                        the shape shared with
 *                                                        the bucket sort)
 *   sentinel store + compaction, no vqtbl1q pack         3.33->2.94 at 64,
 *                                                        -3..-9% at 200..2000
 *   interleaved {x,y,z,0} gather + vtrn transposes       +10% at 200..256,
 *                                                        -4% at 1000: NOT taken
 *
 * So this lane stores the vector whole and compacts, like the A32 lane, but
 * keeps the lane-by-lane gather the A32 lane gave up: an out-of-order core
 * overlaps the lane loads on its own and only pays for the transposes.
 */

/* 3-way select: lane takes a where o == 0, b where o == 1, else c. */
static inline int32x4_t
toridraw_sel3_neon(
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
 * Four faces at f..f+3: cull, key, pack, stash. Returns how many keys were
 * appended at *keys; always writes 16 bytes there, so the buffer needs four
 * lanes of slack past the count.
 */
static inline __attribute__((always_inline)) int
toridraw_face_sort_bitonic_radix_block4_neon(
    struct ToriDraw_Scene* scene,
    int f,
    int32x4_t near_clip_sentinel, /* -5000 x4; read only when spec_clipped */
    int32x4_t min_depth,
    uint32x4_t depth_levels,
    int const spec_clipped, /* literal at every call: folds the clip test away */
    int const spec_stash,   /* literal at every call: folds the stash body away */
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
     * past |delta| ~ 32k, which pre-clip coordinates reach. */
    int32x4_t const dx1 = vsubq_s32(ax, bx);
    int32x4_t const dy1 = vsubq_s32(ay, by);
    int32x4_t const dx2 = vsubq_s32(cx, bx);
    int32x4_t const dy2 = vsubq_s32(cy, by);
    int64x2_t w_lo = vmull_s32(vget_low_s32(dx1), vget_low_s32(dy2));
    int64x2_t w_hi = vmull_high_s32(dx1, dy2);
    w_lo = vmlsl_s32(w_lo, vget_low_s32(dy1), vget_low_s32(dx2));
    w_hi = vmlsl_high_s32(w_hi, dy1, dx2);
    uint64x2_t const zero64 = vdupq_n_u64(0);
#if TORIDRAW_FLIP_WINDING
    uint64x2_t const front_lo = vcltq_s64(w_lo, vreinterpretq_s64_u64(zero64));
    uint64x2_t const front_hi = vcltq_s64(w_hi, vreinterpretq_s64_u64(zero64));
#else
    uint64x2_t const front_lo = vcgtq_s64(w_lo, vreinterpretq_s64_u64(zero64));
    uint64x2_t const front_hi = vcgtq_s64(w_hi, vreinterpretq_s64_u64(zero64));
#endif
    uint32x4_t const front = vcombine_u32(vmovn_u64(front_lo), vmovn_u64(front_hi));

    /* A clipped vertex has sentinel x and no screen-space winding yet: the
     * face is kept and the near-plane rebuild decides. Only the clipped
     * variant asks -- near_clipped is a per-MODEL fact, so the plain variant
     * (nearly every model) carries neither the compares nor the flag; the
     * same split the bucket sort and the projection families make. */
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

    /* key = (0xFFFF - depth) << 16 | face */
    uint32x4_t key = vreinterpretq_u32_s32(vsubq_s32(vdupq_n_s32(0xFFFF), depth));
    key = vshlq_n_u32(key, 16);
    {
        uint32x4_t const lane = { 0, 1, 2, 3 };
        key = vorrq_u32(key, vaddq_u32(vdupq_n_u32((uint32_t)f), lane));
    }

    /*
     * NO LEFT-PACK. The four keys go out as one vector store with rejected
     * lanes as the sentinel 0xFFFFFFFF; lane_blocks compacts the run in one
     * scalar pass before the sort. Measured on an M4 Max (keys arm, presort
     * off, interleaved A/B against the vqtbl1q pack): 3.33 -> 2.94 ns/face
     * at 64 faces, -3..-9% across 200..2000 -- the same shape as the A32
     * lane, where the pack's NEON-to-ARM hand-off was the block's
     * serialisation point. The wide core hides less of it than expected.
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
        xq.val[0] = toridraw_sel3_neon(o0, ax, bx, cx);
        xq.val[1] = toridraw_sel3_neon(o1, ax, bx, cx);
        xq.val[2] = toridraw_sel3_neon(o2, ax, bx, cx);
        xq.val[3] = vreinterpretq_s32_u32(vshrq_n_u32(clip, 31));
        yq.val[0] = toridraw_sel3_neon(o0, ay, by, cy);
        yq.val[1] = toridraw_sel3_neon(o1, ay, by, cy);
        yq.val[2] = toridraw_sel3_neon(o2, ay, by, cy);
        yq.val[3] = p;
        /* Interleaving stores: record f gets lane 0 of each, f+1 lane 1... */
        vst4q_s32(&scene->sm_face_x4[(size_t)f * 4], xq);
        vst4q_s32(&scene->sm_face_y4[(size_t)f * 4], yq);
    }

    return 4;
}

/*
 * The bitonic network over N = 2^k keys, ascending, unsigned.
 *
 * Strides of four and above compare two whole vectors; the two strides
 * inside a vector are a lane rotate (stride 2) and a pair swap (stride 1)
 * with a select mask that says which lanes keep the max. Direction per
 * block of k follows (i & k) == 0, which is constant across a vector once
 * k >= 4; the k == 2 opening stage is the one place it alternates inside
 * the vector, and it has its own mask.
 */
static inline uint32x4_t
toridraw_bitonic_inner_neon(
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
toridraw_bitonic_sort_u32_neon(
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
            vst1q_u32(a + i, toridraw_bitonic_inner_neon(vld1q_u32(a + i), k, (i & k) == 0));
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
        (void)num_vertices; /* this lane gathers lane by lane; see the header for why */
    /* The three loop-invariant vectors are built here rather than at the
     * caller because they are the lane's own currency: the dispatcher does not
     * know what shape a sentinel has on this ISA, and no longer needs to. */
    int32x4_t const sentinel = vdupq_n_s32(near_clipped ? TORIDRAW_SCREEN_X_NEAR_CLIPPED : INT_MIN);
    int32x4_t const min_depth = vdupq_n_s32(model_min_depth);
    uint32x4_t const levels = vdupq_n_u32((uint32_t)scene->depth_levels);
    int f = *f_io;
    int n = 0;

    /* The two questions are asked ONCE, here; each answer picks a loop whose
     * block was compiled with the answers as literals, as
     * bucket_sort_by_average_depth_small dispatches to its four loops. */
#define TORIDRAW_NEON64_BLOCK_LOOP(spec_clipped_, spec_stash_)                                   \
    do                                                                                             \
    {                                                                                              \
        for( ; f + 4 <= num_faces; f += 4 )                                                        \
            n += toridraw_face_sort_bitonic_radix_block4_neon(                                     \
                scene, f, sentinel, min_depth, levels, (spec_clipped_), (spec_stash_), vx, vy, vz, \
                face_a, face_b, face_c, keys + n);                                                 \
    } while( 0 )
    if( near_clipped )
    {
        if( stash_xy )
            TORIDRAW_NEON64_BLOCK_LOOP(1, 1);
        else
            TORIDRAW_NEON64_BLOCK_LOOP(1, 0);
    }
    else
    {
        if( stash_xy )
            TORIDRAW_NEON64_BLOCK_LOOP(0, 1);
        else
            TORIDRAW_NEON64_BLOCK_LOOP(0, 0);
    }
#undef TORIDRAW_NEON64_BLOCK_LOOP

    *f_io = f;
    /* Compact the run: rejected faces were stored as sentinels (see block4). */
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
    *out_accepted = n; /* this lane left-packs: every key written is an accepted face */
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
    /* The scalar tile kernel, as the SSE2 and A32 lanes route it: its saving
     * is the six index loads and the two-face model's trip through a block it
     * cannot fill, not vector width. TORIDRAW_TILE_SORT=0 is the control. */
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

    while( N < n )
        N <<= 1;
    for( i = n; i < N; i++ )
        keys[i] = 0xFFFFFFFFu;
    toridraw_bitonic_sort_u32_neon(keys, N);
    return true;
}

#endif /* TORIDRAW_FACE_SORT_BITONIC_RADIX_NEON64_U_C */
