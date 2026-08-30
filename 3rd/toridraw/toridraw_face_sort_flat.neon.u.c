#ifndef TORIDRAW_FACE_SORT_FLAT_NEON_U_C
#define TORIDRAW_FACE_SORT_FLAT_NEON_U_C

#include "toridraw_face_sort_flat.h"

#include <arm_neon.h>

/*
 * The AArch64 lane of the flat face sort. See toridraw_face_sort_flat.h for
 * the three hooks every lane owes the dispatcher; this file provides all
 * three, and declines only the terrain tile -- both tile kernels were measured
 * against the general path on an x86 host and nothing equivalent has been run
 * here, so a tile on this lane stays an ordinary two-face model.
 */

/*
 * Left-pack: for each 4-bit accept mask, the byte shuffle that moves the
 * accepted u32 lanes to the front. Rejected lanes' bytes are don't-care
 * (0xFF selects zero in vqtbl1q, which is as good as anything: they land
 * past the write cursor and are overwritten or padded over).
 */
static const uint8_t g_toridraw_pack_tbl_neon[16][16] = {
    { 0xFF,
     0xFF,      0xFF,
     0xFF,                0xFF,
     0xFF,                            0xFF,
     0xFF,                                        0xFF,
     0xFF,                                                    0xFF,
     0xFF,                                                                0xFF,
     0xFF,                                                                            0xFF,
     0xFF                                                                                        },
    { 0,    1,  2,    3,  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 4,    5,  6,    7,  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0,    1,  2,    3,  4,    5,    6,    7,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 8,    9,  10,   11, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0,    1,  2,    3,  8,    9,    10,   11,   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 4,    5,  6,    7,  8,    9,    10,   11,   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0,    1,  2,    3,  4,    5,    6,    7,    8,    9,    10,   11,   0xFF, 0xFF, 0xFF, 0xFF },
    { 12,   13, 14,   15, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0,    1,  2,    3,  12,   13,   14,   15,   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 4,    5,  6,    7,  12,   13,   14,   15,   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0,    1,  2,    3,  4,    5,    6,    7,    12,   13,   14,   15,   0xFF, 0xFF, 0xFF, 0xFF },
    { 8,    9,  10,   11, 12,   13,   14,   15,   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0,    1,  2,    3,  8,    9,    10,   11,   12,   13,   14,   15,   0xFF, 0xFF, 0xFF, 0xFF },
    { 4,    5,  6,    7,  8,    9,    10,   11,   12,   13,   14,   15,   0xFF, 0xFF, 0xFF, 0xFF },
    { 0,    1,  2,    3,  4,    5,    6,    7,    8,    9,    10,   11,   12,   13,   14,   15   },
};

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
static inline int
toridraw_face_sort_flat_block4_neon(
    struct ToriDraw_Scene* scene,
    int f,
    int32x4_t near_clip_sentinel, /* -5000 x4 when near_clipped, else INT_MIN x4 */
    int flip,
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
    uint64x2_t front_lo;
    uint64x2_t front_hi;
    if( flip )
    {
        front_lo = vcltq_s64(w_lo, vreinterpretq_s64_u64(zero64));
        front_hi = vcltq_s64(w_hi, vreinterpretq_s64_u64(zero64));
    }
    else
    {
        front_lo = vcgtq_s64(w_lo, vreinterpretq_s64_u64(zero64));
        front_hi = vcgtq_s64(w_hi, vreinterpretq_s64_u64(zero64));
    }
    uint32x4_t const front = vcombine_u32(vmovn_u64(front_lo), vmovn_u64(front_hi));

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
        uint32x4_t const lane = { 0, 1, 2, 3 };
        key = vorrq_u32(key, vaddq_u32(vdupq_n_u32((uint32_t)f), lane));
    }

    /* movemask: 0/1 per lane, weighted, summed */
    uint32x4_t const weights = { 1, 2, 4, 8 };
    unsigned const m = vaddvq_u32(vandq_u32(vshrq_n_u32(accept, 31), vdupq_n_u32(1)) * weights);
    uint8x16_t const shuffle = vld1q_u8(g_toridraw_pack_tbl_neon[m]);
    vst1q_u32(keys, vreinterpretq_u32_u8(vqtbl1q_u8(vreinterpretq_u8_u32(key), shuffle)));

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

    return __builtin_popcount(m);
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
toridraw_face_sort_flat_lane_blocks(
    struct ToriDraw_Scene* scene,
    int* f_io,
    int num_faces,
    bool near_clipped,
    int flip,
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
     * know what shape a sentinel has on this ISA, and no longer needs to. */
    int32x4_t const sentinel = vdupq_n_s32(near_clipped ? TORIDRAW_SCREEN_X_NEAR_CLIPPED : INT_MIN);
    int32x4_t const min_depth = vdupq_n_s32(model_min_depth);
    uint32x4_t const levels = vdupq_n_u32((uint32_t)scene->depth_levels);
    int f = *f_io;
    int n = 0;

    for( ; f + 4 <= num_faces; f += 4 )
        n += toridraw_face_sort_flat_block4_neon(
            scene,
            f,
            sentinel,
            flip,
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
toridraw_face_sort_flat_lane_tile2(
    struct ToriDraw_Scene* scene,
    int tile2_rot,
    bool near_clipped,
    int flip,
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
        scene, tile2_rot, near_clipped, flip, model_min_depth, stash_xy, vx, vy, vz, keys, out_n);
}

static inline bool
toridraw_face_sort_flat_lane_sort(
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

#endif /* TORIDRAW_FACE_SORT_FLAT_NEON_U_C */
