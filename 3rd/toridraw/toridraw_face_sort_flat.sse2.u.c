#ifndef TORIDRAW_FACE_SORT_FLAT_SSE2_U_C
#define TORIDRAW_FACE_SORT_FLAT_SSE2_U_C

#include "toridraw_face_sort_flat.h"

#include <emmintrin.h>

/*
 * The SSE2 lane of the flat face sort. See toridraw_face_sort_flat.h for the
 * three hooks every lane owes the dispatcher; this is the only lane that
 * answers the terrain-tile one, because it is the only lane the two tile
 * kernels were ever measured on.
 */

/*
 * The SSE2 lane, for the Win32 XP build, written to the Pentium 4 floor.
 * Where the ISA has a hole the fill is named here once:
 *
 *   no unsigned 32-bit min/max (pminud is SSE4.1)
 *       -> bias both by 0x80000000 and use the signed pcmpgtd, then select.
 *   no 64-bit signed multiply or compare (pmuldq/pcmpgtq are SSE4.x)
 *       -> the winding is taken in double precision, two faces per vector.
 *          The deltas are ints and the products stay far below 2^53, so the
 *          two products and their difference are exact, and the sign is the
 *          64-bit integer sign graphics/winding.h computes.
 *   no pmulld
 *       -> mullo_epi32_sse2 from sse2_41compat.h (two pmuludq and a shuffle);
 *          the low 32 bits are the same for signed and unsigned operands,
 *          which is all the C's int multiply keeps.
 *   no pshufb for the left-pack
 *       -> the four keys go to a stack slot and come back through a 16-entry
 *          lane-index table with four unconditional scalar stores. Still no
 *          branch on the winding and no data-dependent store address.
 *   no interleaving store (vst4q)
 *       -> a 4x4 transpose through unpcklps/unpckhps and four plain stores.
 *   no cmple / select
 *       -> cmple is ~cmpgt; select is and/andnot/or.
 */

static const uint8_t g_toridraw_pack_idx_sse2[16][4] = {
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
static const uint8_t g_toridraw_popcount4_sse2[16] = { 0, 1, 1, 2, 1, 2, 2, 3,
                                                       1, 2, 2, 3, 2, 3, 3, 4 };

static inline __m128i
toridraw_sse2_select(
    __m128i mask,
    __m128i t,
    __m128i f)
{
    return _mm_or_si128(_mm_and_si128(mask, t), _mm_andnot_si128(mask, f));
}

static inline __m128i
toridraw_sse2_cmple_epi32(
    __m128i a,
    __m128i b)
{
    return _mm_xor_si128(_mm_cmpgt_epi32(a, b), _mm_set1_epi32(-1));
}

/* unsigned a < b, per lane */
static inline __m128i
toridraw_sse2_cmplt_epu32(
    __m128i a,
    __m128i b)
{
    __m128i const bias = _mm_set1_epi32((int)0x80000000u);
    return _mm_cmpgt_epi32(_mm_xor_si128(b, bias), _mm_xor_si128(a, bias));
}

static inline __m128i
toridraw_sse2_mullo_epi32(
    __m128i a,
    __m128i b)
{
    __m128i const lo = _mm_mul_epu32(a, b);
    __m128i const hi = _mm_mul_epu32(_mm_srli_si128(a, 4), _mm_srli_si128(b, 4));
    return _mm_unpacklo_epi32(
        _mm_shuffle_epi32(lo, _MM_SHUFFLE(0, 0, 2, 0)),
        _mm_shuffle_epi32(hi, _MM_SHUFFLE(0, 0, 2, 0)));
}

/* 3-way select: lane takes a where o == 0, b where o == 1, else c. */
static inline __m128i
toridraw_sse2_sel3(
    __m128i o,
    __m128i a,
    __m128i b,
    __m128i c)
{
    __m128i const is0 = _mm_cmpeq_epi32(o, _mm_setzero_si128());
    __m128i const is1 = _mm_cmpeq_epi32(o, _mm_set1_epi32(1));
    return toridraw_sse2_select(is0, a, toridraw_sse2_select(is1, b, c));
}

/* {lo lanes 0,1} x {hi lanes 0,1} of two double-compare masks -> one 4-lane int mask */
static inline __m128i
toridraw_sse2_pack_pd_masks(
    __m128d lo,
    __m128d hi)
{
    return _mm_castps_si128(
        _mm_shuffle_ps(_mm_castpd_ps(lo), _mm_castpd_ps(hi), _MM_SHUFFLE(2, 0, 2, 0)));
}

/*
 * Cull, key, pack and stash four faces whose NINE coordinate vectors are
 * already in registers, lane i holding face f + i.
 *
 * Split out of toridraw_face_sort_flat_block4_sse2 so the terrain-tile kernel below
 * can reach the same arithmetic rather than restate it. Everything that
 * differs between the two is a parameter that is a compile-time constant at
 * both call sites, so nothing here costs the general path anything:
 *
 *   lanes  4 for a full block, 2 for a two-triangle tile. Lanes at or above
 *          it are forced out of the accept mask and are never stashed, which
 *          is what lets the tile kernel leave them holding junk.
 *   f      the face index of lane 0.
 *
 * Writes four keys at *keys unconditionally; returns how many of them count.
 */
static inline int
toridraw_face_sort_flat_pack4_sse2(
    struct ToriDraw_Scene* scene,
    int f,
    int lanes,
    __m128i near_clip_sentinel, /* -5000 x4 when near_clipped, else INT_MIN x4 */
    int flip,
    __m128i min_depth,
    __m128i depth_levels,
    int stash_xy,
    __m128i ax,
    __m128i ay,
    __m128i az,
    __m128i bx,
    __m128i by,
    __m128i bz,
    __m128i cx,
    __m128i cy,
    __m128i cz,
    uint32_t* keys)
{
    /* Winding = dx1*dy2 - dy1*dx2, in doubles: exact, see the block comment. */
    __m128i const dx1 = _mm_sub_epi32(ax, bx);
    __m128i const dy1 = _mm_sub_epi32(ay, by);
    __m128i const dx2 = _mm_sub_epi32(cx, bx);
    __m128i const dy2 = _mm_sub_epi32(cy, by);
    __m128d const w_lo = _mm_sub_pd(
        _mm_mul_pd(_mm_cvtepi32_pd(dx1), _mm_cvtepi32_pd(dy2)),
        _mm_mul_pd(_mm_cvtepi32_pd(dy1), _mm_cvtepi32_pd(dx2)));
    __m128d const w_hi = _mm_sub_pd(
        _mm_mul_pd(
            _mm_cvtepi32_pd(_mm_srli_si128(dx1, 8)), _mm_cvtepi32_pd(_mm_srli_si128(dy2, 8))),
        _mm_mul_pd(
            _mm_cvtepi32_pd(_mm_srli_si128(dy1, 8)), _mm_cvtepi32_pd(_mm_srli_si128(dx2, 8))));
    __m128d const zero = _mm_setzero_pd();
    __m128i const front =
        flip ? toridraw_sse2_pack_pd_masks(_mm_cmplt_pd(w_lo, zero), _mm_cmplt_pd(w_hi, zero))
             : toridraw_sse2_pack_pd_masks(_mm_cmpgt_pd(w_lo, zero), _mm_cmpgt_pd(w_hi, zero));

    /* A clipped vertex has sentinel x and no screen-space winding yet. */
    __m128i const clip = _mm_or_si128(
        _mm_or_si128(
            _mm_cmpeq_epi32(ax, near_clip_sentinel), _mm_cmpeq_epi32(bx, near_clip_sentinel)),
        _mm_cmpeq_epi32(cx, near_clip_sentinel));

    /* depth = (z_sum * 21845) >> 16 + min_depth, as div3_fast_fixedpoint. */
    __m128i depth = _mm_add_epi32(_mm_add_epi32(az, bz), cz);
    depth = _mm_srai_epi32(toridraw_sse2_mullo_epi32(depth, _mm_set1_epi32(21845)), 16);
    depth = _mm_add_epi32(depth, min_depth);
    __m128i const in_range = toridraw_sse2_cmplt_epu32(depth, depth_levels);

    __m128i accept = _mm_and_si128(_mm_or_si128(front, clip), in_range);

    /* A short block's surplus lanes hold whatever the shuffle left there.
     * Dropping them here rather than at the caller means they cannot reach the
     * key array, the popcount, or the stash. */
    if( lanes < 4 )
        accept = _mm_and_si128(accept, _mm_set_epi32(0, 0, -1, -1));

    /* key = (0xFFFF - depth) << 16 | face */
    __m128i key = _mm_slli_epi32(_mm_sub_epi32(_mm_set1_epi32(0xFFFF), depth), 16);
    key = _mm_or_si128(key, _mm_add_epi32(_mm_set1_epi32(f), _mm_set_epi32(3, 2, 1, 0)));

    /* Left-pack through the index table: four unconditional stores. */
    {
        unsigned const m = (unsigned)_mm_movemask_ps(_mm_castsi128_ps(accept));
        const uint8_t* const t = g_toridraw_pack_idx_sse2[m];
        uint32_t lane[4];
        _mm_storeu_si128((__m128i*)lane, key);
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
            __m128i const A = toridraw_sse2_cmple_epi32(ay, by);
            __m128i const B = toridraw_sse2_cmple_epi32(ay, cy);
            __m128i const C = toridraw_sse2_cmple_epi32(by, cy);
            __m128i const D = toridraw_sse2_cmple_epi32(cy, ay);
            __m128i const k1 = _mm_set1_epi32(1), k2 = _mm_set1_epi32(2);
            __m128i const k3 = _mm_set1_epi32(3), k4 = _mm_set1_epi32(4);
            __m128i const k5 = _mm_set1_epi32(5);
            __m128i const inner1 = toridraw_sse2_select(C, _mm_setzero_si128(), k1);
            __m128i const inner2 = toridraw_sse2_select(D, k2, k3);
            __m128i const inner3 = toridraw_sse2_select(A, k4, k5);
            __m128i const p = toridraw_sse2_select(
                _mm_and_si128(A, B), inner1, toridraw_sse2_select(C, inner2, inner3));
            __m128i const o0 = _mm_srai_epi32(p, 1);
            __m128i v = _mm_add_epi32(_mm_add_epi32(o0, k1), _mm_and_si128(p, k1));
            v = _mm_sub_epi32(v, _mm_and_si128(_mm_cmpgt_epi32(v, k2), k3));
            __m128i const o1 = v;
            __m128i const o2 = _mm_sub_epi32(_mm_sub_epi32(k3, o0), o1);

            /* Columns (one per record field), transposed to rows (one per
             * face) through the float unpacks, which are bitwise. */
            __m128 x0 = _mm_castsi128_ps(toridraw_sse2_sel3(o0, ax, bx, cx));
            __m128 x1 = _mm_castsi128_ps(toridraw_sse2_sel3(o1, ax, bx, cx));
            __m128 x2 = _mm_castsi128_ps(toridraw_sse2_sel3(o2, ax, bx, cx));
            __m128 x3 = _mm_castsi128_ps(_mm_srli_epi32(clip, 31));
            __m128 y0 = _mm_castsi128_ps(toridraw_sse2_sel3(o0, ay, by, cy));
            __m128 y1 = _mm_castsi128_ps(toridraw_sse2_sel3(o1, ay, by, cy));
            __m128 y2 = _mm_castsi128_ps(toridraw_sse2_sel3(o2, ay, by, cy));
            __m128 y3 = _mm_castsi128_ps(p);
            int* const x4 = &scene->sm_face_x4[(size_t)f * 4];
            int* const y4 = &scene->sm_face_y4[(size_t)f * 4];
            _MM_TRANSPOSE4_PS(x0, x1, x2, x3);
            _MM_TRANSPOSE4_PS(y0, y1, y2, y3);
            _mm_storeu_ps((float*)(x4 + 0), x0);
            _mm_storeu_ps((float*)(x4 + 4), x1);
            _mm_storeu_ps((float*)(y4 + 0), y0);
            _mm_storeu_ps((float*)(y4 + 4), y1);
            if( lanes == 4 )
            {
                _mm_storeu_ps((float*)(x4 + 8), x2);
                _mm_storeu_ps((float*)(x4 + 12), x3);
                _mm_storeu_ps((float*)(y4 + 8), y2);
                _mm_storeu_ps((float*)(y4 + 12), y3);
            }
        }

        return g_toridraw_popcount4_sse2[m];
    }
}

/* Four faces at f..f+3: gather by axis, then the shared cull above. */
static inline int
toridraw_face_sort_flat_block4_sse2(
    struct ToriDraw_Scene* scene,
    int f,
    __m128i near_clip_sentinel,
    int flip,
    __m128i min_depth,
    __m128i depth_levels,
    int stash_xy,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c,
    uint32_t* keys)
{
    int const a0 = face_a[f], a1 = face_a[f + 1], a2 = face_a[f + 2], a3 = face_a[f + 3];
    int const b0 = face_b[f], b1 = face_b[f + 1], b2 = face_b[f + 2], b3 = face_b[f + 3];
    int const c0 = face_c[f], c1 = face_c[f + 1], c2 = face_c[f + 2], c3 = face_c[f + 3];

    /* The gather, structure of arrays: _mm_set_epi32 takes lane 3 first. */
    return toridraw_face_sort_flat_pack4_sse2(
        scene,
        f,
        4,
        near_clip_sentinel,
        flip,
        min_depth,
        depth_levels,
        stash_xy,
        _mm_set_epi32(vx[a3], vx[a2], vx[a1], vx[a0]),
        _mm_set_epi32(vy[a3], vy[a2], vy[a1], vy[a0]),
        _mm_set_epi32(vz[a3], vz[a2], vz[a1], vz[a0]),
        _mm_set_epi32(vx[b3], vx[b2], vx[b1], vx[b0]),
        _mm_set_epi32(vy[b3], vy[b2], vy[b1], vy[b0]),
        _mm_set_epi32(vz[b3], vz[b2], vz[b1], vz[b0]),
        _mm_set_epi32(vx[c3], vx[c2], vx[c1], vx[c0]),
        _mm_set_epi32(vy[c3], vy[c2], vy[c1], vy[c0]),
        _mm_set_epi32(vz[c3], vz[c2], vz[c1], vz[c0]),
        keys);
}

/*
 * The same tile through SSE2, and the SLOWER of the two -- kept as the arm
 * TORIDRAW_TILE_SORT=2 selects, not as anything's default.
 *
 * The idea was that the gather is what a two-face model pays: with the triples
 * known, the model's four projected vertices are contiguous at vx[0..3], so ONE
 * unaligned vector load per axis has all of them and each of the three lanes is
 * a _mm_shuffle_epi32 with a constant immediate -- three loads and twelve
 * shuffles for both faces at once, against twenty-four dependent scalar loads.
 * The rotation is applied to the loaded vector rather than to the immediates,
 * so rotations 1-3 cost three more shuffles and rotation 0, which is 94% of
 * tiles, costs none.
 *
 * MEASURED, it loses. On the isolated sort bench (make -C src
 * test-face-sort-flat, TORIDRAW_FACE_SORT_BENCH=1) over 256 tiles in the
 * census's rotation mix, per input face:
 *
 *     TORIDRAW_TILE_SORT=0  general path       8.5 - 8.8 ns
 *     TORIDRAW_TILE_SORT=1  scalar kernel      6.5 - 6.6 ns
 *     TORIDRAW_TILE_SORT=2  this               8.9 - 9.0 ns
 *
 * The gather was not the cost. What costs is the rest of pack4 -- the
 * double-precision cross product, the emulated 32-bit multiply and unsigned
 * compare, the left-pack, and the whole y-sort permutation ladder -- which is
 * priced to be amortised over FOUR faces and here is amortised over two. The
 * scalar kernel wins by deleting loads the compiler can prove redundant rather
 * than by widening the arithmetic.
 *
 * Kept because the host it was measured on is not the host it was written for:
 * a Pentium 4 pays ~10 cycles for the 64-bit imul the scalar winding needs and
 * has half this core's load throughput, which is the case that could flip it.
 * That is a hypothesis, and the switch is how the XP box tests it in one
 * binary. SSE2 only; a NEON twin is not written because nothing here is
 * measured on NEON.
 */
static inline int
toridraw_face_sort_flat_tile2_sse2(
    struct ToriDraw_Scene* scene,
    int rot, /* quarter turns, 0..3 */
    __m128i near_clip_sentinel,
    int flip,
    __m128i min_depth,
    __m128i depth_levels,
    int stash_xy,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    uint32_t* keys)
{
    __m128i rx = _mm_loadu_si128((const __m128i*)vx);
    __m128i ry = _mm_loadu_si128((const __m128i*)vy);
    __m128i rz = _mm_loadu_si128((const __m128i*)vz);

    /* r[j] = v[(j - rot) & 3], undoing the turn the face indices were built
     * with. Rotation 0 is the overwhelming majority and does nothing. */
    switch( rot )
    {
    case 1:
        rx = _mm_shuffle_epi32(rx, _MM_SHUFFLE(2, 1, 0, 3));
        ry = _mm_shuffle_epi32(ry, _MM_SHUFFLE(2, 1, 0, 3));
        rz = _mm_shuffle_epi32(rz, _MM_SHUFFLE(2, 1, 0, 3));
        break;
    case 2:
        rx = _mm_shuffle_epi32(rx, _MM_SHUFFLE(1, 0, 3, 2));
        ry = _mm_shuffle_epi32(ry, _MM_SHUFFLE(1, 0, 3, 2));
        rz = _mm_shuffle_epi32(rz, _MM_SHUFFLE(1, 0, 3, 2));
        break;
    case 3:
        rx = _mm_shuffle_epi32(rx, _MM_SHUFFLE(0, 3, 2, 1));
        ry = _mm_shuffle_epi32(ry, _MM_SHUFFLE(0, 3, 2, 1));
        rz = _mm_shuffle_epi32(rz, _MM_SHUFFLE(0, 3, 2, 1));
        break;
    default:
        break;
    }

    /* Lane 0 is face 0 = (1,2,3), lane 1 is face 1 = (0,1,3). Lanes 2 and 3
     * are junk and `lanes = 2` is what keeps them from mattering. */
#define TORIDRAW_TILE2_A(v) _mm_shuffle_epi32((v), _MM_SHUFFLE(1, 1, 0, 1))
#define TORIDRAW_TILE2_B(v) _mm_shuffle_epi32((v), _MM_SHUFFLE(2, 2, 1, 2))
#define TORIDRAW_TILE2_C(v) _mm_shuffle_epi32((v), _MM_SHUFFLE(3, 3, 3, 3))
    return toridraw_face_sort_flat_pack4_sse2(
        scene,
        0,
        2,
        near_clip_sentinel,
        flip,
        min_depth,
        depth_levels,
        stash_xy,
        TORIDRAW_TILE2_A(rx),
        TORIDRAW_TILE2_A(ry),
        TORIDRAW_TILE2_A(rz),
        TORIDRAW_TILE2_B(rx),
        TORIDRAW_TILE2_B(ry),
        TORIDRAW_TILE2_B(rz),
        TORIDRAW_TILE2_C(rx),
        TORIDRAW_TILE2_C(ry),
        TORIDRAW_TILE2_C(rz),
        keys);
#undef TORIDRAW_TILE2_A
#undef TORIDRAW_TILE2_B
#undef TORIDRAW_TILE2_C
}

static inline __m128i
toridraw_sse2_min_epu32(
    __m128i a,
    __m128i b)
{
    return toridraw_sse2_select(toridraw_sse2_cmplt_epu32(a, b), a, b);
}

static inline __m128i
toridraw_sse2_max_epu32(
    __m128i a,
    __m128i b)
{
    return toridraw_sse2_select(toridraw_sse2_cmplt_epu32(a, b), b, a);
}

/* The in-vector stages of the bitonic network; same shape as the NEON one. */
static inline __m128i
toridraw_bitonic_inner_sse2(
    __m128i v,
    int k,
    int asc)
{
    __m128i const m2a = _mm_set_epi32(-1, -1, 0, 0);  /* stride 2: lanes 2,3 take max */
    __m128i const m1a = _mm_set_epi32(-1, 0, -1, 0);  /* stride 1: lanes 1,3 take max */
    __m128i const m1k2 = _mm_set_epi32(0, -1, -1, 0); /* k == 2: asc pair, desc pair */
    __m128i p;
    __m128i mn;
    __m128i mx;

    if( k == 2 )
    {
        p = _mm_shuffle_epi32(v, _MM_SHUFFLE(2, 3, 0, 1));
        mn = toridraw_sse2_min_epu32(v, p);
        mx = toridraw_sse2_max_epu32(v, p);
        return toridraw_sse2_select(m1k2, mx, mn);
    }
    p = _mm_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2));
    mn = toridraw_sse2_min_epu32(v, p);
    mx = toridraw_sse2_max_epu32(v, p);
    v = asc ? toridraw_sse2_select(m2a, mx, mn) : toridraw_sse2_select(m2a, mn, mx);
    p = _mm_shuffle_epi32(v, _MM_SHUFFLE(2, 3, 0, 1));
    mn = toridraw_sse2_min_epu32(v, p);
    mx = toridraw_sse2_max_epu32(v, p);
    return asc ? toridraw_sse2_select(m1a, mx, mn) : toridraw_sse2_select(m1a, mn, mx);
}

static void
toridraw_bitonic_sort_u32_sse2(
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
                __m128i va;
                __m128i vb;
                __m128i mn;
                __m128i mx;
                if( i & j )
                    continue;
                va = _mm_loadu_si128((const __m128i*)(a + i));
                vb = _mm_loadu_si128((const __m128i*)(a + (i ^ j)));
                mn = toridraw_sse2_min_epu32(va, vb);
                mx = toridraw_sse2_max_epu32(va, vb);
                if( (i & k) == 0 )
                {
                    _mm_storeu_si128((__m128i*)(a + i), mn);
                    _mm_storeu_si128((__m128i*)(a + (i ^ j)), mx);
                }
                else
                {
                    _mm_storeu_si128((__m128i*)(a + i), mx);
                    _mm_storeu_si128((__m128i*)(a + (i ^ j)), mn);
                }
            }
        }
        for( i = 0; i < N; i += 4 )
            _mm_storeu_si128(
                (__m128i*)(a + i),
                toridraw_bitonic_inner_sse2(
                    _mm_loadu_si128((const __m128i*)(a + i)), k, (i & k) == 0));
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
    __m128i const sentinel =
        _mm_set1_epi32(near_clipped ? TORIDRAW_SCREEN_X_NEAR_CLIPPED : INT_MIN);
    __m128i const min_depth = _mm_set1_epi32(model_min_depth);
    __m128i const levels = _mm_set1_epi32(scene->depth_levels);
    int f = *f_io;
    int n = 0;

    for( ; f + 4 <= num_faces; f += 4 )
        n += toridraw_face_sort_flat_block4_sse2(
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

/*
 * The terrain tile, at whichever of the three kernels TORIDRAW_TILE_SORT
 * selected. TORIDRAW_TILE_SORT_OFF declines, which puts the tile back on the
 * general block-and-tail path -- that is the A/B's control arm, and the same
 * thing every other lane does unconditionally.
 */
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
    int const armed = toridraw_face_sort_tile2_armed();

    if( armed == TORIDRAW_TILE_SORT_SIMD )
    {
        *out_n = toridraw_face_sort_flat_tile2_sse2(
            scene,
            tile2_rot,
            _mm_set1_epi32(near_clipped ? TORIDRAW_SCREEN_X_NEAR_CLIPPED : INT_MIN),
            flip,
            _mm_set1_epi32(model_min_depth),
            _mm_set1_epi32(scene->depth_levels),
            stash_xy,
            vx,
            vy,
            vz,
            keys);
        return true;
    }

    if( armed == TORIDRAW_TILE_SORT_SCALAR )
    {
        *out_n = toridraw_face_sort_flat_tile2_scalar(
            scene, tile2_rot, near_clipped, flip, model_min_depth, stash_xy, vx, vy, vz, keys);
        return true;
    }

    return false;
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
    toridraw_bitonic_sort_u32_sse2(keys, N);
    return true;
}

#endif /* TORIDRAW_FACE_SORT_FLAT_SSE2_U_C */
