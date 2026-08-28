/*
 * The flat face sort: SIMD cull, composite keys, bitonic or radix.
 *
 * WHAT IT REPLACES. bucket_sort_by_average_depth_small walks the faces one
 * at a time: three index loads, six dependent coordinate loads, a branch on
 * the winding, and a scattered write into a per-depth bucket whose counter
 * is a read-modify-write on an unpredictable address. Then a prefix sum over
 * the depth span, a second scatter into CSR order, and a windowed restore.
 * Every one of those is a dependency chain the core cannot run ahead of.
 *
 * WHAT THIS DOES INSTEAD, per model:
 *
 *   1. CULL AND PACK, four faces at a time. The twelve coordinates of four
 *      faces are gathered into vectors by axis (structure of arrays), the
 *      winding is a 64-bit cross product in two vmlsl_s32, the near-clip
 *      exemption and the depth-range test are compares, and the four
 *      results are one accept mask. The key (0xFFFF - depth) << 16 | face
 *      is built for all four lanes unconditionally, then LEFT-PACKED through
 *      a 16-entry byte-shuffle table and stored with one unconditional
 *      vector store; the write cursor advances by the popcount. No branch
 *      on the winding, no scattered write, no counter.
 *
 *      The y-sort permutation the batched kernels want (see sm_face_x4) is
 *      three vector compares and a handful of selects, and the permuted
 *      coordinates go out as two interleaving stores per four faces, so the
 *      stash costs a fraction of what seven scalar stores and a six-way
 *      ladder did.
 *
 *   2. SORT the dense key array. The key puts depth in the high half,
 *      inverted, and the face index in the low half, so an ascending sort of
 *      plain u32 is back-to-front with face order preserved within a depth
 *      -- the same order the bucket walk emitted, so the pixels are the same.
 *
 *      The dispatch is by count, once per model:
 *
 *        <= TORIDRAW_FACE_SORT_BITONIC_MAX (256)    NEON / SSE2 bitonic network
 *        otherwise                                  two-pass 8-bit radix
 *
 *      A bitonic network is branchless and fully unrolled: padded to a power
 *      of two with 0xFFFFFFFF (which sorts to the end and can never be a real
 *      key -- a face index is at most 0x7FFF), strides of four and above are
 *      whole-vector min/max between two loads, and strides two and one are
 *      in-register shuffles. For the typical 200-face model that is a few
 *      dozen passes over 64 vectors, all in L1. Its O(n log^2 n) is the wrong
 *      shape for a 4,000-face tile, which is where the radix takes over:
 *      the face index is already in order in the low half, so a STABLE sort
 *      on the sixteen depth bits alone is enough -- two counting passes over
 *      the upper two bytes, O(n), streaming.
 *
 *   3. EMIT. The draw order is the low sixteen bits of each key; the
 *      priority partition reads (face, depth) pairs off the same array.
 *
 * The two sorts are selected by TORIDRAW_FACE_SORT (see
 * toridraw_face_sort_flat_armed) and by ToriDraw_FaceSortSetFlat for the
 * A/B in the benchmark; the bucket sort stays compiled and is the reference
 * toridraw_face_sort_flat_test.c holds this to, order for order.
 */

#ifndef TORIDRAW_FACE_SORT_FLAT_U_C
#define TORIDRAW_FACE_SORT_FLAT_U_C

#include <stdint.h>
#include <string.h>

#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && !defined(NEON_DISABLED)
#include <arm_neon.h>
#define TORIDRAW_FACE_SORT_NEON 1
#define TORIDRAW_FACE_SORT_SIMD 1
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
/* The Win32 XP lane: -march=pentium4 is SSE2 and nothing above it -- no
 * pshufb (SSSE3), no pcmpgtq / pmulld / pminud (SSE4.x). Everything below
 * is written to that floor; see the SSE2 block for how each gap is filled. */
#include <emmintrin.h>
#define TORIDRAW_FACE_SORT_SSE2 1
#define TORIDRAW_FACE_SORT_SIMD 1
#endif

#define TORIDRAW_FACE_SORT_BITONIC_MAX 256

/* Runtime override for the A/B: -1 = the environment decides. */
int g_toridraw_face_sort_flat_override = -1;

void
ToriDraw_FaceSortSetFlat(int enabled)
{
    g_toridraw_face_sort_flat_override = enabled;
}

/*
 * TORIDRAW_FACE_SORT=bucket puts the bucket sort back, in the same binary.
 * Default is the flat sort wherever a SIMD cull is compiled; the scalar
 * flat sort is a fallback for the sake of the test, not a contender.
 */
static inline int
toridraw_face_sort_flat_armed(void)
{
    static int armed = -1;
    if( g_toridraw_face_sort_flat_override >= 0 )
        return g_toridraw_face_sort_flat_override;
    if( armed < 0 )
    {
        const char* v = getenv("TORIDRAW_FACE_SORT");
#ifdef TORIDRAW_FACE_SORT_SIMD
        armed = (v && (v[0] == 'b' || v[0] == '0')) ? 0 : 1;
#else
        armed = (v && (v[0] == 'f' || v[0] == '1')) ? 1 : 0;
#endif
    }
    return armed;
}

static inline int
toridraw_face_sort_bitonic_max(void)
{
    static int max = -1;
    if( max < 0 )
    {
        const char* v = getenv("TORIDRAW_SORT_BITONIC_MAX");
        max = v ? atoi(v) : TORIDRAW_FACE_SORT_BITONIC_MAX;
        if( max < 0 )
            max = 0;
    }
    return max;
}

/* The y-order permutation, as the batched kernels read it (sm_face_y4[3]). */
static const unsigned char g_toridraw_ysort_order[6][3] = {
    { 0, 1, 2 }, { 0, 2, 1 }, { 1, 2, 0 }, { 1, 0, 2 }, { 2, 0, 1 }, { 2, 1, 0 }
};

/*
 * One face, scalar: the tail of the vector loop and the whole of the
 * non-NEON fallback. Same decisions as the bucket sort, same stash.
 */
static inline int
toridraw_face_sort_flat_one(
    struct ToriDraw_Scene* scene,
    int f,
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
    uint32_t* out_key)
{
    const uint32_t a = face_a[f];
    const uint32_t b = face_b[f];
    const uint32_t c = face_c[f];
    bool const clip_candidate =
        near_clipped &&
        (vx[a] == TORIDRAW_SCREEN_X_NEAR_CLIPPED || vx[b] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
         vx[c] == TORIDRAW_SCREEN_X_NEAR_CLIPPED);
    long long winding = 1;
    int depth_avg;

    if( !clip_candidate )
    {
        winding = toridraw_winding_2d(vx[a], vy[a], vx[b], vy[b], vx[c], vy[c]);
        if( flip ? !(winding < 0) : !(winding > 0) )
            return 0;
    }

    depth_avg = div3_fast_fixedpoint(vz[a] + vz[b] + vz[c]) + model_min_depth;
    if( (unsigned int)depth_avg >= (unsigned int)scene->depth_levels )
        return 0;

    if( stash_xy )
    {
        int* const x4 = &scene->sm_face_x4[(size_t)f * 4];
        int* const y4 = &scene->sm_face_y4[(size_t)f * 4];
        x4[3] = clip_candidate ? 1 : 0;
        if( !clip_candidate )
        {
            int const ya = vy[a];
            int const yb = vy[b];
            int const yc = vy[c];
            int const perm = (ya <= yb && ya <= yc) ? ((yb <= yc) ? 0 : 1)
                             : (yb <= yc)           ? ((yc <= ya) ? 2 : 3)
                                                    : ((ya <= yb) ? 4 : 5);
            unsigned char const* const o = g_toridraw_ysort_order[perm];
            int const px[3] = { vx[a], vx[b], vx[c] };
            int const py[3] = { ya, yb, yc };
            x4[0] = px[o[0]];
            x4[1] = px[o[1]];
            x4[2] = px[o[2]];
            y4[0] = py[o[0]];
            y4[1] = py[o[1]];
            y4[2] = py[o[2]];
            y4[3] = perm;
        }
    }

    *out_key = ((uint32_t)(0xFFFF - depth_avg) << 16) | (uint32_t)f;
    return 1;
}

#ifdef TORIDRAW_FACE_SORT_NEON

/*
 * Left-pack: for each 4-bit accept mask, the byte shuffle that moves the
 * accepted u32 lanes to the front. Rejected lanes' bytes are don't-care
 * (0xFF selects zero in vqtbl1q, which is as good as anything: they land
 * past the write cursor and are overwritten or padded over).
 */
static const uint8_t g_toridraw_pack_tbl[16][16] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 8, 9, 10, 11, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0, 1, 2, 3, 8, 9, 10, 11, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 4, 5, 6, 7, 8, 9, 10, 11, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0xFF, 0xFF, 0xFF, 0xFF },
    { 12, 13, 14, 15, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0, 1, 2, 3, 12, 13, 14, 15, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 4, 5, 6, 7, 12, 13, 14, 15, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0, 1, 2, 3, 4, 5, 6, 7, 12, 13, 14, 15, 0xFF, 0xFF, 0xFF, 0xFF },
    { 8, 9, 10, 11, 12, 13, 14, 15, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 15, 0xFF, 0xFF, 0xFF, 0xFF },
    { 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
};

/* 3-way select: lane takes a where o == 0, b where o == 1, else c. */
static inline int32x4_t
toridraw_sel3(int32x4_t o, int32x4_t a, int32x4_t b, int32x4_t c)
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
toridraw_face_sort_flat_block4(
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
    unsigned const m = vaddvq_u32(vandq_u32(vshrq_n_u32(accept, 31) , vdupq_n_u32(1)) * weights);
    uint8x16_t const shuffle = vld1q_u8(g_toridraw_pack_tbl[m]);
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
        int32x4_t const p =
            vbslq_s32(vandq_u32(A, B), inner1, vbslq_s32(C, inner2, inner3));
        int32x4_t const o0 = vshrq_n_s32(p, 1);
        int32x4_t v = vaddq_s32(vaddq_s32(o0, k1), vandq_s32(p, k1));
        v = vsubq_s32(v, vandq_s32(vreinterpretq_s32_u32(vcgeq_s32(v, k3)), k3));
        int32x4_t const o1 = v;
        int32x4_t const o2 = vsubq_s32(vsubq_s32(k3, o0), o1);

        int32x4x4_t xq;
        int32x4x4_t yq;
        xq.val[0] = toridraw_sel3(o0, ax, bx, cx);
        xq.val[1] = toridraw_sel3(o1, ax, bx, cx);
        xq.val[2] = toridraw_sel3(o2, ax, bx, cx);
        xq.val[3] = vreinterpretq_s32_u32(vshrq_n_u32(clip, 31));
        yq.val[0] = toridraw_sel3(o0, ay, by, cy);
        yq.val[1] = toridraw_sel3(o1, ay, by, cy);
        yq.val[2] = toridraw_sel3(o2, ay, by, cy);
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
toridraw_bitonic_inner(uint32x4_t v, int k, int asc)
{
    uint32x4_t const m2a = { 0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu }; /* stride 2: lanes 2,3 take max */
    uint32x4_t const m1a = { 0, 0xFFFFFFFFu, 0, 0xFFFFFFFFu }; /* stride 1: lanes 1,3 take max */
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
toridraw_bitonic_sort_u32_simd(uint32_t* a, int N)
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
            vst1q_u32(a + i, toridraw_bitonic_inner(vld1q_u32(a + i), k, (i & k) == 0));
    }
}

#endif /* TORIDRAW_FACE_SORT_NEON */

#ifdef TORIDRAW_FACE_SORT_SSE2

/*
 * The SSE2 twin of the NEON block above, for the Win32 XP lane, built on
 * the Pentium 4 floor. Where the ISA has a hole the fill is named here once:
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

static const uint8_t g_toridraw_pack_idx[16][4] = {
    { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 0, 1, 0, 0 },
    { 2, 0, 0, 0 }, { 0, 2, 0, 0 }, { 1, 2, 0, 0 }, { 0, 1, 2, 0 },
    { 3, 0, 0, 0 }, { 0, 3, 0, 0 }, { 1, 3, 0, 0 }, { 0, 1, 3, 0 },
    { 2, 3, 0, 0 }, { 0, 2, 3, 0 }, { 1, 2, 3, 0 }, { 0, 1, 2, 3 },
};
static const uint8_t g_toridraw_popcount4[16] = { 0, 1, 1, 2, 1, 2, 2, 3,
                                                  1, 2, 2, 3, 2, 3, 3, 4 };

static inline __m128i
toridraw_sse2_select(__m128i mask, __m128i t, __m128i f)
{
    return _mm_or_si128(_mm_and_si128(mask, t), _mm_andnot_si128(mask, f));
}

static inline __m128i
toridraw_sse2_cmple_epi32(__m128i a, __m128i b)
{
    return _mm_xor_si128(_mm_cmpgt_epi32(a, b), _mm_set1_epi32(-1));
}

/* unsigned a < b, per lane */
static inline __m128i
toridraw_sse2_cmplt_epu32(__m128i a, __m128i b)
{
    __m128i const bias = _mm_set1_epi32((int)0x80000000u);
    return _mm_cmpgt_epi32(_mm_xor_si128(b, bias), _mm_xor_si128(a, bias));
}

static inline __m128i
toridraw_sse2_mullo_epi32(__m128i a, __m128i b)
{
    __m128i const lo = _mm_mul_epu32(a, b);
    __m128i const hi = _mm_mul_epu32(_mm_srli_si128(a, 4), _mm_srli_si128(b, 4));
    return _mm_unpacklo_epi32(
        _mm_shuffle_epi32(lo, _MM_SHUFFLE(0, 0, 2, 0)),
        _mm_shuffle_epi32(hi, _MM_SHUFFLE(0, 0, 2, 0)));
}

/* 3-way select: lane takes a where o == 0, b where o == 1, else c. */
static inline __m128i
toridraw_sse2_sel3(__m128i o, __m128i a, __m128i b, __m128i c)
{
    __m128i const is0 = _mm_cmpeq_epi32(o, _mm_setzero_si128());
    __m128i const is1 = _mm_cmpeq_epi32(o, _mm_set1_epi32(1));
    return toridraw_sse2_select(is0, a, toridraw_sse2_select(is1, b, c));
}

/* {lo lanes 0,1} x {hi lanes 0,1} of two double-compare masks -> one 4-lane int mask */
static inline __m128i
toridraw_sse2_pack_pd_masks(__m128d lo, __m128d hi)
{
    return _mm_castps_si128(
        _mm_shuffle_ps(_mm_castpd_ps(lo), _mm_castpd_ps(hi), _MM_SHUFFLE(2, 0, 2, 0)));
}

/* Four faces at f..f+3: cull, key, pack, stash. Writes four keys at *keys
 * unconditionally; returns how many of them count. */
static inline int
toridraw_face_sort_flat_block4(
    struct ToriDraw_Scene* scene,
    int f,
    __m128i near_clip_sentinel, /* -5000 x4 when near_clipped, else INT_MIN x4 */
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
    __m128i const ax = _mm_set_epi32(vx[a3], vx[a2], vx[a1], vx[a0]);
    __m128i const bx = _mm_set_epi32(vx[b3], vx[b2], vx[b1], vx[b0]);
    __m128i const cx = _mm_set_epi32(vx[c3], vx[c2], vx[c1], vx[c0]);
    __m128i const ay = _mm_set_epi32(vy[a3], vy[a2], vy[a1], vy[a0]);
    __m128i const by = _mm_set_epi32(vy[b3], vy[b2], vy[b1], vy[b0]);
    __m128i const cy = _mm_set_epi32(vy[c3], vy[c2], vy[c1], vy[c0]);
    __m128i const az = _mm_set_epi32(vz[a3], vz[a2], vz[a1], vz[a0]);
    __m128i const bz = _mm_set_epi32(vz[b3], vz[b2], vz[b1], vz[b0]);
    __m128i const cz = _mm_set_epi32(vz[c3], vz[c2], vz[c1], vz[c0]);

    /* Winding = dx1*dy2 - dy1*dx2, in doubles: exact, see the block comment. */
    __m128i const dx1 = _mm_sub_epi32(ax, bx);
    __m128i const dy1 = _mm_sub_epi32(ay, by);
    __m128i const dx2 = _mm_sub_epi32(cx, bx);
    __m128i const dy2 = _mm_sub_epi32(cy, by);
    __m128d const w_lo = _mm_sub_pd(
        _mm_mul_pd(_mm_cvtepi32_pd(dx1), _mm_cvtepi32_pd(dy2)),
        _mm_mul_pd(_mm_cvtepi32_pd(dy1), _mm_cvtepi32_pd(dx2)));
    __m128d const w_hi = _mm_sub_pd(
        _mm_mul_pd(_mm_cvtepi32_pd(_mm_srli_si128(dx1, 8)),
                   _mm_cvtepi32_pd(_mm_srli_si128(dy2, 8))),
        _mm_mul_pd(_mm_cvtepi32_pd(_mm_srli_si128(dy1, 8)),
                   _mm_cvtepi32_pd(_mm_srli_si128(dx2, 8))));
    __m128d const zero = _mm_setzero_pd();
    __m128i const front = flip ? toridraw_sse2_pack_pd_masks(_mm_cmplt_pd(w_lo, zero),
                                                             _mm_cmplt_pd(w_hi, zero))
                               : toridraw_sse2_pack_pd_masks(_mm_cmpgt_pd(w_lo, zero),
                                                             _mm_cmpgt_pd(w_hi, zero));

    /* A clipped vertex has sentinel x and no screen-space winding yet. */
    __m128i const clip = _mm_or_si128(
        _mm_or_si128(_mm_cmpeq_epi32(ax, near_clip_sentinel),
                     _mm_cmpeq_epi32(bx, near_clip_sentinel)),
        _mm_cmpeq_epi32(cx, near_clip_sentinel));

    /* depth = (z_sum * 21845) >> 16 + min_depth, as div3_fast_fixedpoint. */
    __m128i depth = _mm_add_epi32(_mm_add_epi32(az, bz), cz);
    depth = _mm_srai_epi32(toridraw_sse2_mullo_epi32(depth, _mm_set1_epi32(21845)), 16);
    depth = _mm_add_epi32(depth, min_depth);
    __m128i const in_range = toridraw_sse2_cmplt_epu32(depth, depth_levels);

    __m128i const accept = _mm_and_si128(_mm_or_si128(front, clip), in_range);

    /* key = (0xFFFF - depth) << 16 | face */
    __m128i key = _mm_slli_epi32(_mm_sub_epi32(_mm_set1_epi32(0xFFFF), depth), 16);
    key = _mm_or_si128(key, _mm_add_epi32(_mm_set1_epi32(f), _mm_set_epi32(3, 2, 1, 0)));

    /* Left-pack through the index table: four unconditional stores. */
    {
        unsigned const m = (unsigned)_mm_movemask_ps(_mm_castsi128_ps(accept));
        const uint8_t* const t = g_toridraw_pack_idx[m];
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
            _mm_storeu_ps((float*)(x4 + 8), x2);
            _mm_storeu_ps((float*)(x4 + 12), x3);
            _mm_storeu_ps((float*)(y4 + 0), y0);
            _mm_storeu_ps((float*)(y4 + 4), y1);
            _mm_storeu_ps((float*)(y4 + 8), y2);
            _mm_storeu_ps((float*)(y4 + 12), y3);
        }

        return g_toridraw_popcount4[m];
    }
}

static inline __m128i
toridraw_sse2_min_epu32(__m128i a, __m128i b)
{
    return toridraw_sse2_select(toridraw_sse2_cmplt_epu32(a, b), a, b);
}

static inline __m128i
toridraw_sse2_max_epu32(__m128i a, __m128i b)
{
    return toridraw_sse2_select(toridraw_sse2_cmplt_epu32(a, b), b, a);
}

/* The in-vector stages of the bitonic network; same shape as the NEON one. */
static inline __m128i
toridraw_bitonic_inner(__m128i v, int k, int asc)
{
    __m128i const m2a = _mm_set_epi32(-1, -1, 0, 0); /* stride 2: lanes 2,3 take max */
    __m128i const m1a = _mm_set_epi32(-1, 0, -1, 0); /* stride 1: lanes 1,3 take max */
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
toridraw_bitonic_sort_u32_simd(uint32_t* a, int N)
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
                toridraw_bitonic_inner(_mm_loadu_si128((const __m128i*)(a + i)), k, (i & k) == 0));
    }
}

#endif /* TORIDRAW_FACE_SORT_SSE2 */

/*
 * Two-pass LSD counting sort on the depth half of the key. Stable, so the
 * face order the pack wrote survives within a depth. `tmp` is the bounce
 * buffer, as long as `keys`.
 */
static void
toridraw_radix_sort_depth16(uint32_t* RESTRICT keys, uint32_t* RESTRICT tmp, int n)
{
    static int count0[256];
    static int count1[256];
    int i;
    int sum0 = 0;
    int sum1 = 0;

    memset(count0, 0, sizeof(count0));
    memset(count1, 0, sizeof(count1));
    for( i = 0; i < n; i++ )
    {
        uint32_t const k = keys[i];
        count0[(k >> 16) & 0xFF]++;
        count1[k >> 24]++;
    }
    for( i = 0; i < 256; i++ )
    {
        int const c0 = count0[i];
        int const c1 = count1[i];
        count0[i] = sum0;
        count1[i] = sum1;
        sum0 += c0;
        sum1 += c1;
    }
    for( i = 0; i < n; i++ )
    {
        uint32_t const k = keys[i];
        tmp[count0[(k >> 16) & 0xFF]++] = k;
    }
    for( i = 0; i < n; i++ )
    {
        uint32_t const k = tmp[i];
        keys[count1[k >> 24]++] = k;
    }
}

/* Portable fallback for the bitonic slot without a SIMD lane. */
static int
toridraw_key_compare(const void* pa, const void* pb)
{
    uint32_t const a = *(const uint32_t*)pa;
    uint32_t const b = *(const uint32_t*)pb;
    return (a > b) - (a < b);
}

/*
 * Cull, key and sort one model's faces. Returns the accepted count; the
 * sorted keys are in scene->sm_sort_keys.
 */
static int
toridraw_face_sort_flat(
    struct ToriDraw_Scene* scene,
    bool presort,
    bool near_clipped,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    uint32_t* const keys = scene->sm_sort_keys;
    int const stash_xy = presort && toridraw_raster_batch_armed();
    int const flip = toridraw_flip_winding();
    int n = 0;
    int f = 0;

    assert(scene);
    assert(keys);

    scene->sm_face_xy_valid = stash_xy;
    if( stash_xy )
        g_toridraw_presort_models++;

#if defined(TORIDRAW_FACE_SORT_NEON)
    {
        int32x4_t const sentinel =
            vdupq_n_s32(near_clipped ? TORIDRAW_SCREEN_X_NEAR_CLIPPED : INT_MIN);
        int32x4_t const min_depth = vdupq_n_s32(model_min_depth);
        uint32x4_t const levels = vdupq_n_u32((uint32_t)scene->depth_levels);

        for( ; f + 4 <= num_faces; f += 4 )
            n += toridraw_face_sort_flat_block4(
                scene, f, sentinel, flip, min_depth, levels, stash_xy, vx, vy, vz, face_a,
                face_b, face_c, keys + n);
    }
#elif defined(TORIDRAW_FACE_SORT_SSE2)
    {
        __m128i const sentinel =
            _mm_set1_epi32(near_clipped ? TORIDRAW_SCREEN_X_NEAR_CLIPPED : INT_MIN);
        __m128i const min_depth = _mm_set1_epi32(model_min_depth);
        __m128i const levels = _mm_set1_epi32(scene->depth_levels);

        for( ; f + 4 <= num_faces; f += 4 )
            n += toridraw_face_sort_flat_block4(
                scene, f, sentinel, flip, min_depth, levels, stash_xy, vx, vy, vz, face_a,
                face_b, face_c, keys + n);
    }
#endif
    for( ; f < num_faces; f++ )
        n += toridraw_face_sort_flat_one(
            scene, f, near_clipped, flip, model_min_depth, stash_xy, vx, vy, vz, face_a,
            face_b, face_c, keys + n);

    if( n <= 1 )
        return n;

    if( n <= toridraw_face_sort_bitonic_max() )
    {
#ifdef TORIDRAW_FACE_SORT_SIMD
        int N = 4;
        while( N < n )
            N <<= 1;
        for( f = n; f < N; f++ )
            keys[f] = 0xFFFFFFFFu;
        toridraw_bitonic_sort_u32_simd(keys, N);
#else
        qsort(keys, (size_t)n, sizeof(*keys), toridraw_key_compare);
#endif
    }
    else
        toridraw_radix_sort_depth16(keys, scene->sm_sort_tmp, n);

    return n;
}

/*
 * The priority partition off the sorted keys: the same fold of the old
 * partition and accumulation as the small-mode CSR twin, reading (face,
 * depth) pairs from the flat array in the order the buckets emitted them.
 */
static inline void
partition_and_accumulate_faces_by_priority_keys(
    struct ToriDraw_Scene* scene,
    const uint32_t* keys,
    int n,
    int* priority_depths,
    int* counts,
    const uint8_t* face_priorities)
{
    const int max_faces = scene->max_faces;
    int i;

    memset(scene->sm_prio_count, 0, sizeof(scene->sm_prio_count));

    for( i = 0; i < n; i++ )
    {
        uint32_t const k = keys[i];
        faceint_t const face_idx = (faceint_t)(k & 0xFFFF);
        int const depth = 0xFFFF - (int)(k >> 16);
        int const prio = faceprio_unpack(face_priorities, face_idx);
        int nn;

        assert(face_idx >= 0 && face_idx < max_faces);
        assert(prio >= 0 && prio < 12 && "face priority indexes counts[12]");

        nn = counts[prio];
        assert(nn >= 0 && nn < max_faces);

        scene->sm_prio_faces[prio * max_faces + nn] = face_idx;

        if( prio < 10 )
            priority_depths[prio] += depth;
        else
        {
            assert(depth >= 0 && depth <= 0xFFFF);
            assert(nn < scene->flex_prio_capacity);
            if( prio == 10 )
                scene->sm_flex_prio11_face_to_depth[nn] = depth | (face_idx << 16);
            else
                scene->sm_flex_prio12_face_to_depth[nn] = depth | (face_idx << 16);
        }

        counts[prio] = nn + 1;
        scene->sm_prio_count[prio] = nn + 1;
    }
}

#endif /* TORIDRAW_FACE_SORT_FLAT_U_C */
