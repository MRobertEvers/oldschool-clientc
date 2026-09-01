/*
 * The bitonic+radix face sort: SIMD cull, composite keys, bitonic or radix.
 *
 * The two halves of the name are step 2 below, and which one runs is decided
 * by the key count. Only the bitonic half needs a vector lane; a build with
 * none has the radix and qsort, and registers a kernel called `radix` instead
 * -- see kernels/facesort.bitonic_radix.u.c.
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
 * THE TERRAIN TILE short-circuits all of it. 94% of a map's tiles are four
 * vertices and two triangles whose index triples are a compile-time fact, and
 * two faces never reach step 1's vector path at all -- so they get a kernel
 * with the triples as literals, and a compare-swap for step 2. It is the
 * scalar one that wins; the SSE2 twin beside it is slower than the general
 * path and is kept only as a switch position. Numbers at
 * toridraw_face_sort_tile2_armed, which is also how the three are A/B'd.
 *
 * The two sorts are selected by TORIDRAW_FACE_SORT (see
 * toridraw_face_sort_bitonic_radix_armed) and by
 * ToriDraw_FaceSortSetBitonicRadix for the A/B in the benchmark; the bucket
 * sort stays compiled and is the reference
 * toridraw_face_sort_bitonic_radix_test.c holds this to, order for order.
 *
 * WHAT IS IN THIS FILE AND WHAT IS NOT. Steps 1 and 2 are written per
 * instruction set and each ISA's copy lives in its own file --
 * facesort.bitonic_radix.small.{neon,sse2,scalar}.u.c -- reached through the
 * three hooks facesort.bitonic_radix.small.dispatch.h names. This file holds
 * only what every build compiles: the scalar per-face cull, the scalar terrain
 * tile, the radix sort, the environment gates, and the one dispatcher that
 * asks the lane for the rest. It contains no `#if` on any architecture, and
 * neither does any caller.
 */

#ifndef TORIDRAW_FACE_SORT_BITONIC_RADIX_U_C
#define TORIDRAW_FACE_SORT_BITONIC_RADIX_U_C

#include "impl/facesort/facesort.bitonic_radix.small.dispatch.h"

#include "graphics/batch_stats.h"
#include "graphics/div3.h"
#include "graphics/winding.h"
#include "graphics/ysort_order.h"
#include "toridraw_model_internal.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define TORIDRAW_FACE_SORT_BITONIC_MAX 256

/* Runtime override for the A/B: -1 = the environment decides. */
int g_toridraw_face_sort_bitonic_radix_override = -1;

void
ToriDraw_FaceSortSetBitonicRadix(int enabled)
{
    g_toridraw_face_sort_bitonic_radix_override = enabled;
}

/*
 * Does TORIDRAW_FACE_SORT name this sort?
 *
 * A FULL COMPARE, and not the first-letter test this replaced: `bucket` and
 * `bitonic_radix` now share their first letter, so `b` alone cannot tell them
 * apart. `0` and `1` are kept as the numeric spellings of the same two
 * choices.
 */
static inline int
toridraw_face_sort_env_named(const char* v, const char* name, char digit)
{
    if( !v )
        return 0;
    return strcmp(v, name) == 0 || (v[0] == digit && v[1] == '\0');
}

/*
 * TORIDRAW_FACE_SORT=bucket puts the bucket sort back, in the same binary.
 * Default is this sort wherever a SIMD cull is compiled; without one it is
 * the radix and qsort with no bitonic network, which is a fallback for the
 * sake of the test and not a contender, so there it is opt-in.
 */
static inline int
toridraw_face_sort_bitonic_radix_armed(void)
{
    static int armed = -1;
    if( g_toridraw_face_sort_bitonic_radix_override >= 0 )
        return g_toridraw_face_sort_bitonic_radix_override;
    if( armed < 0 )
    {
        const char* v = getenv("TORIDRAW_FACE_SORT");
#ifdef TORIDRAW_FACE_SORT_SIMD
        armed = toridraw_face_sort_env_named(v, "bucket", '0') ? 0 : 1;
#else
        armed = toridraw_face_sort_env_named(v, "bitonic_radix", '1') ? 1 : 0;
#endif
    }
    return armed;
}

/*
 * Which kernel the two-triangle terrain tile takes, in the SAME binary. The
 * times are ns per input face, from the tile2 row of the bench in
 * toridraw_face_sort_bitonic_radix_test.c (make -C src test-face-sort-bitonic-radix,
 * TORIDRAW_FACE_SORT_BENCH=1), on the dev host -- an x86-64 core running this
 * win32 SSE2 build, which is NOT the Pentium 4 any of it is aimed at:
 *
 *   TORIDRAW_TILE_SORT=0   the general path -- the four-wide block's scalar
 *                          tail, twice, reading the index arrays    8.5-8.8
 *   TORIDRAW_TILE_SORT=1   scalar kernel, triples as literals       6.5-6.6
 *                          (the default)
 *   TORIDRAW_TILE_SORT=2   SSE2 kernel, triples as shuffle          8.9-9.0
 *                          immediates -- SLOWER THAN DOING NOTHING
 *
 * Reproducible to within about 1% run to run, with the bucket sort's column as
 * the control: it sits at 13 ns in all three and must not move. The frame-level
 * sweep cannot see any of this -- ~200 tiles a frame times 2 ns saved is 0.09%
 * of a 0.97 ms frame, against a ~1.7% noise floor -- so this bench is the
 * instrument for a change here, not a wall-clock A/B.
 *
 * That the vector arm loses is the surprise, and the reason is in its own
 * comment: the gather was never what a two-face model paid for. Do not re-try
 * it on this host expecting a different answer; the open question is the P4,
 * which is what the arm is kept for.
 *
 * Two binaries would be the other way to A/B this, and it is the worse way:
 * the arms then differ by a link as well as by a kernel, and telling them
 * apart afterwards means hashing blobs rather than reading a variable. One
 * check per model, cached, alongside the two this sort already does.
 */
#define TORIDRAW_TILE_SORT_OFF 0
#define TORIDRAW_TILE_SORT_SCALAR 1
#define TORIDRAW_TILE_SORT_SIMD 2

static inline int
toridraw_face_sort_tile2_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
    {
        const char* v = getenv("TORIDRAW_TILE_SORT");
        armed = v ? atoi(v) : TORIDRAW_TILE_SORT_SCALAR;
        if( armed < 0 || armed > TORIDRAW_TILE_SORT_SIMD )
            armed = TORIDRAW_TILE_SORT_SCALAR;
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

/*
 * One face, scalar: the tail of whatever the lane's vector cull left, and the
 * whole model on a lane that has no vector cull. Same decisions as the bucket
 * sort, same stash.
 */
static inline int
toridraw_face_sort_bitonic_radix_one_abc(
    struct ToriDraw_Scene* scene,
    int f,
    uint32_t a,
    uint32_t b,
    uint32_t c,
    bool near_clipped,
    int model_min_depth,
    int stash_xy,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    uint32_t* out_key)
{
    bool const clip_candidate = near_clipped && (vx[a] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                                                 vx[b] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                                                 vx[c] == TORIDRAW_SCREEN_X_NEAR_CLIPPED);
    long long winding = 1;
    int depth_avg;

    if( !clip_candidate )
    {
        winding = toridraw_winding_2d(vx[a], vy[a], vx[b], vy[b], vx[c], vy[c]);
        if( !toridraw_winding_front_facing(winding) )
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

/* The same, reading face f's triple out of the index arrays. */
static inline int
toridraw_face_sort_bitonic_radix_one(
    struct ToriDraw_Scene* scene,
    int f,
    bool near_clipped,
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
    return toridraw_face_sort_bitonic_radix_one_abc(
        scene,
        f,
        (uint32_t)face_a[f],
        (uint32_t)face_b[f],
        (uint32_t)face_c[f],
        near_clipped,
        model_min_depth,
        stash_xy,
        vx,
        vy,
        vz,
        out_key);
}

/*
 * The two-triangle terrain tile, with the triangulation resolved at COMPILE
 * time. This is the default (TORIDRAW_TILE_SORT=1).
 *
 * WHY A TILE GETS A KERNEL. A tile is not an arbitrary mesh. Its four vertices
 * and its two index triples come from static tables in world_decode_tile.c,
 * and 94% of the tiles a Lumbridge square builds are one of the three shapes
 * that read four corners and emit faces (1,2,3) and (0,1,3) -- all three the
 * same triples, turned by the tile's rotation. Those tiles are ~65% of the
 * models this sort is handed and a few percent of its faces, so what they cost
 * is not face work: it is that a two-face model never reaches the vector path
 * at all (`f + 4 <= num_faces` is false) and falls to the scalar tail twice.
 *
 * WHAT THE KNOWLEDGE BUYS, and it is not what it first looks like. The obvious
 * prize is the gather -- twenty-four dependent coordinate loads for two
 * triangles -- and spending the constants on SIMD to collapse it is what
 * toridraw_face_sort_bitonic_radix_tile2_sse2 does; measured, that LOSES (its comment
 * has the numbers). What actually pays is smaller and duller: face_indices_a/b/c
 * are not read at all, which is six loads gone, and with the indices constant
 * the two faces provably share vertices 1 and 3, so the compiler folds the
 * duplicated coordinate loads instead of issuing each face's six blind.
 * 6.5 ns per input face against the general path's 8.7, a 25% cut of the sort
 * for a tile, on the bench in toridraw_face_sort_bitonic_radix_test.c.
 *
 * Rotation 0 is 94% of tiles and gets literals. The other three turn the
 * corner indices the way world_decode_tile turned them, which is arithmetic on
 * a register rather than four more copies of the body.
 */
static inline int
toridraw_face_sort_bitonic_radix_tile2_scalar(
    struct ToriDraw_Scene* scene,
    int rot,
    bool near_clipped,
    int model_min_depth,
    int stash_xy,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    uint32_t* keys)
{
    int n = 0;
    if( rot == 0 )
    {
        n += toridraw_face_sort_bitonic_radix_one_abc(
            scene, 0, 1, 2, 3, near_clipped, model_min_depth, stash_xy, vx, vy, vz, keys + n);
        n += toridraw_face_sort_bitonic_radix_one_abc(
            scene, 1, 0, 1, 3, near_clipped, model_min_depth, stash_xy, vx, vy, vz, keys + n);
    }
    else
    {
        uint32_t const r0 = (uint32_t)((0 - rot) & 3);
        uint32_t const r1 = (uint32_t)((1 - rot) & 3);
        uint32_t const r2 = (uint32_t)((2 - rot) & 3);
        uint32_t const r3 = (uint32_t)((3 - rot) & 3);
        n += toridraw_face_sort_bitonic_radix_one_abc(
            scene, 0, r1, r2, r3, near_clipped, model_min_depth, stash_xy, vx, vy, vz, keys + n);
        n += toridraw_face_sort_bitonic_radix_one_abc(
            scene, 1, r0, r1, r3, near_clipped, model_min_depth, stash_xy, vx, vy, vz, keys + n);
    }
    return n;
}

/*
 * ONE LANE, chosen by the ladder in toridraw_face_sort_bitonic_radix.h. Everything
 * above this line is what every build compiles; everything the lane brings is
 * behind the three hooks the header names, so the dispatcher below carries no
 * preprocessor of its own.
 *
 * `#elif` and not two `#if`s: a build has exactly one of these, and the
 * stacked form this replaced -- a NEON block and an SSE2 block inside the same
 * function, one after the other, each redefining the same helper names -- read
 * as though a build could have both and would try each in turn.
 */
#if defined(TORIDRAW_FACE_SORT_LANE_NEON)
#include "impl/facesort/facesort.bitonic_radix.small.neon64.u.c"
#elif defined(TORIDRAW_FACE_SORT_LANE_SSE2)
#include "impl/facesort/facesort.bitonic_radix.small.sse2.u.c"
#else
#include "impl/facesort/facesort.bitonic_radix.small.scalar.u.c"
#endif

/*
 * Two-pass LSD counting sort on the depth half of the key. Stable, so the
 * face order the pack wrote survives within a depth. `tmp` is the bounce
 * buffer, as long as `keys`.
 */
static void
toridraw_radix_sort_depth16(
    uint32_t* RESTRICT keys,
    uint32_t* RESTRICT tmp,
    int n)
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
toridraw_key_compare(
    const void* pa,
    const void* pb)
{
    uint32_t const a = *(const uint32_t*)pa;
    uint32_t const b = *(const uint32_t*)pb;
    return (a > b) - (a < b);
}

/*
 * Two accepted keys, sorted: one compare-swap.
 *
 * The general dispatch below would pad the array to four with 0xFFFFFFFF and
 * run the whole bitonic network to order a pair, and a two-face model is not
 * rare -- the world painter draws hundreds of terrain tiles a frame and each
 * is exactly two triangles. Ordering the full u32 is what the network does
 * too, so equal depths keep face order for the same reason.
 */
static inline int
toridraw_face_sort_bitonic_radix_sort2(
    int n,
    uint32_t* keys)
{
    if( n == 2 && keys[0] > keys[1] )
    {
        uint32_t const t = keys[0];
        keys[0] = keys[1];
        keys[1] = t;
    }
    return n;
}

/*
 * Cull, key and sort one model's faces. Returns the accepted count; the
 * sorted keys are in scene->sm_sort_keys.
 *
 * `tile2_rot` is the terrain-tile fast path: 0..3 says this model is one of
 * the two-triangle tile shapes at that rotation, -1 says it is anything else.
 * The caller answers it off ToriDraw_Model.tile_sort_kernel, which is where the
 * shape is known; see toridraw_face_sort_bitonic_radix_lane_tile2.
 */
static int
toridraw_face_sort_bitonic_radix(
    struct ToriDraw_Scene* scene,
    bool presort,
    bool near_clipped,
    int model_min_depth,
    int num_faces,
    int tile2_rot,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    uint32_t* const keys = scene->sm_sort_keys;
    int const stash_xy = presort;
    int tile_n = 0;
    int n = 0;
    int f = 0;

    assert(scene);
    assert(keys);

    scene->sm_face_xy_valid = stash_xy;
    if( stash_xy )
        TORIDRAW_BATCH_COUNT(g_toridraw_presort_models);

    /* The terrain tile, if this lane has a kernel for one. A lane that has
     * none says so by declining and the tile falls through to the two loops
     * below as the ordinary two-face model it also is. */
    if( tile2_rot >= 0 )
    {
        assert(num_faces == 2);
        if( toridraw_face_sort_bitonic_radix_lane_tile2(
                scene,
                tile2_rot,
                near_clipped,
                model_min_depth,
                stash_xy,
                vx,
                vy,
                vz,
                keys,
                &tile_n) )
            return toridraw_face_sort_bitonic_radix_sort2(tile_n, keys);
    }

    /* The lane's vector cull over whole blocks, then the scalar tail over
     * whatever it left. A lane with no vector cull leaves f at 0 and the tail
     * is the whole model. */
    n += toridraw_face_sort_bitonic_radix_lane_blocks(
        scene,
        &f,
        num_faces,
        near_clipped,
        model_min_depth,
        stash_xy,
        vx,
        vy,
        vz,
        face_a,
        face_b,
        face_c,
        keys);

    for( ; f < num_faces; f++ )
        n += toridraw_face_sort_bitonic_radix_one(
            scene,
            f,
            near_clipped,
            model_min_depth,
            stash_xy,
            vx,
            vy,
            vz,
            face_a,
            face_b,
            face_c,
            keys + n);

    if( n <= 2 )
        return toridraw_face_sort_bitonic_radix_sort2(n, keys);

    if( n <= toridraw_face_sort_bitonic_max() )
    {
        if( !toridraw_face_sort_bitonic_radix_lane_sort(keys, n) )
            qsort(keys, (size_t)n, sizeof(*keys), toridraw_key_compare);
    }
    else
        toridraw_radix_sort_depth16(keys, scene->sm_sort_tmp, n);

    return n;
}

/*
 * The priority partition off the sorted keys: the same fold of the old
 * partition and accumulation as the small-mode CSR twin, reading (face,
 * depth) pairs from the key array in the order the buckets emitted them.
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

#endif /* TORIDRAW_FACE_SORT_BITONIC_RADIX_U_C */
