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
 * facesort.bitonic_radix.small.{neon64,neon32,sse2,scalar}.u.c -- reached through the
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
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Above this many accepted keys the radix sorts; at or below it, the lane's
 * bitonic network does.
 *
 * PER LANE, because the crossover is a property of the core, not the
 * algorithm. On x86 the wide network wins to 256. On the A32 NEON lane it
 * does not: the network is 4-wide compare/swaps at an IPC under one, and at
 * N = 256 its 36 stages cost more per face than the radix's two counting
 * passes. Measured on the Moto X (Krait) with the sort bench, keys arm,
 * presort off, ns per input face at 64/200/256/1000 faces:
 *
 *   BITONIC_MAX = 256   47.9  53.2  57.5  44.3     (the x86 default)
 *   BITONIC_MAX = 128   47.7  53.3  47.0  44.4
 *   BITONIC_MAX =  64   47.6  43.9  43.0  44.6
 *   BITONIC_MAX =   0   55.1  44.2  43.2  45.2     (radix for everything)
 *
 * The network earns its keep below ~64 accepted keys (a 64-face model draws
 * ~30) and loses above; 64 is the crossover. neon64 is untested and keeps
 * the x86 value until it is measured. TORIDRAW_SORT_BITONIC_MAX overrides.
 */
#if defined(TORIDRAW_FACE_SORT_LANE_NEON32)
#define TORIDRAW_FACE_SORT_BITONIC_MAX 64
#else
#define TORIDRAW_FACE_SORT_BITONIC_MAX 256
#endif

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

/*
 * TORIDRAW_FACE_SORT_K16=0 turns the A32 lane's eight-face int16 block off
 * (every model then takes the four-face int32 block). The A/B control arm.
 */
static inline int
toridraw_face_sort_k16_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
    {
        const char* v = getenv("TORIDRAW_FACE_SORT_K16");
        armed = (v && v[0] == '0') ? 0 : 1;
    }
    return armed;
}

/* Census for the frame stat lines: models the K16 block took, and models a
 * K16-capable lane sent down the int32 block instead (extent, clip, stash,
 * or fewer than eight faces). */
int g_toridraw_sort_k16_models = 0;
int g_toridraw_sort_k16_declined = 0;
/* ... and of those, models whose last 1..7 faces went through the masked
 * K16 tail block rather than the scalar per-face loop. */
int g_toridraw_sort_k16_tail_models = 0;
/* Terrain tiles the leaf fast path answered without entering the general
 * dispatcher at all (see toridraw_face_sort_bitonic_radix_tile2_fast). */
int g_toridraw_sort_tile_fast_models = 0;

/*
 * THE 2026-09 A/B TOGGLES, one per step of the neon32 lane's second pass.
 * Each is read from the environment once and cached; the default is the NEW
 * behaviour, and NAME=0 selects the control arm -- the code path that stood
 * before the step -- so one binary measures both, interleaved, which is the
 * only A/B that has held up on the phone (see ARMVX_KERNEL_STATE.md).
 */
static inline int
toridraw_face_sort_env_on_unless_zero(const char* name)
{
    const char* v = getenv(name);
    return (v && v[0] == '0') ? 0 : 1;
}

/* TORIDRAW_K16_UZP=0: the K16 block's eight-quad gather goes back to the
 * D-register vtrn.16 + vtrn.32 transposes (which clang lowers as 36 vext.32
 * per block); on, the transposes are four vuzp.16 on Q pairs per corner. */
static inline int
toridraw_face_sort_k16_uzp_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = toridraw_face_sort_env_on_unless_zero("TORIDRAW_K16_UZP");
    return armed;
}

/* TORIDRAW_K16_TAIL=0: the 1..7 faces after a K16 model's last full block
 * take the scalar per-face loop; on, the K16 block runs once more over the
 * overlapping window num_faces - 8 with the already-emitted lanes masked
 * to sentinels. */
static inline int
toridraw_face_sort_k16_tail_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = toridraw_face_sort_env_on_unless_zero("TORIDRAW_K16_TAIL");
    return armed;
}

/* TORIDRAW_SORT_EMIT_VEC=0: the sorted keys are truncated into
 * tmp_face_order one at a time; on, four (eight) per vector store. */
static inline int
toridraw_face_sort_emit_vec_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = toridraw_face_sort_env_on_unless_zero("TORIDRAW_SORT_EMIT_VEC");
    return armed;
}

/* TORIDRAW_SORT_PLD=0: no prefetch of the three face-index streams; on, each
 * block prefetches the line 32 faces (64 bytes of int16) ahead on all three. */
static inline int
toridraw_face_sort_pld_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = toridraw_face_sort_env_on_unless_zero("TORIDRAW_SORT_PLD");
    return armed;
}

/* TORIDRAW_SORT_BITONIC2=0: the bitonic network's original control loop (a
 * skip test per vector, masks reloaded per vector, a memset pad); on, the
 * nested-loop network with hoisted masks and a vector-store pad. */
static inline int
toridraw_face_sort_bitonic2_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = toridraw_face_sort_env_on_unless_zero("TORIDRAW_SORT_BITONIC2");
    return armed;
}

/* TORIDRAW_TILE_FAST=0: a two-face terrain tile goes through the general
 * dispatcher (its prologue, sort_model_inputs, the tile2 kernel's two
 * outlined per-face calls, the sort and the emit); on, a leaf answers it
 * before any of that is entered. See toridraw_face_sort_bitonic_radix_tile2_fast. */
static inline int
toridraw_face_sort_tile_fast_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = toridraw_face_sort_env_on_unless_zero("TORIDRAW_TILE_FAST");
    return armed;
}

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
 * THE TILE FAST PATH: the two-triangle terrain tile answered as a LEAF, before
 * the general dispatcher's prologue is paid.
 *
 * WHAT A TILE PAID. On the Moto X the general path costs a two-face model
 * about 320 instructions of which the faces are a small part: the
 * dispatcher's prologue (a 760-byte frame and a vpush of d8-d15, since the
 * whole lane is inlined into one function), sort_model_inputs, two OUTLINED
 * calls to one_abc with twelve arguments each (eight on the stack), the
 * two-key sort, and the emit loop. A Lumbridge frame hands the sort ~760 of
 * these tiles, ~60% of its models.
 *
 * WHAT THIS DOES INSTEAD. The same arithmetic as one_abc (the same winding,
 * the same clip exemption, the same depth) for both faces inline -- they
 * share vertices 1 and 3, so eight corner loads and not twelve -- the two
 * keys compared, and the face indices written straight into tmp_face_order.
 * No key buffer, no stash (a presorting call takes the general path), no
 * priorities (a tile has none; the caller checks). Called from
 * toridraw_compute_projected_face_order_small's front, which is kept small
 * enough that the general body is a separate, noinline function: the
 * prologue this saves is the general body's, and it is saved only if this
 * function is reached before it.
 *
 * TORIDRAW_TILE_FAST=0 is the control arm. Note it does NOT consult
 * TORIDRAW_TILE_SORT: that switch chooses between the general path's tile
 * kernels, and the leaf is the step before it.
 */
static inline __attribute__((always_inline)) void
toridraw_face_sort_bitonic_radix_tile2_fast_corners(
    struct ToriDraw_Scene* scene,
    int const c0,
    int const c1,
    int const c2,
    int const c3,
    bool near_clipped,
    int model_min_depth)
{
    const int* RESTRICT const vx = scene->screen_vertices_x;
    const int* RESTRICT const vy = scene->screen_vertices_y;
    const int* RESTRICT const vz = scene->screen_vertices_z;
    int const x1 = vx[c1], y1 = vy[c1], z1 = vz[c1];
    int const x3 = vx[c3], y3 = vy[c3], z3 = vz[c3];
    unsigned int const levels = (unsigned int)scene->depth_levels;
    int* const order = scene->tmp_face_order;
    uint32_t key0 = 0;
    uint32_t key1 = 0;
    int accept0;
    int accept1;
    int n = 0;

    /* Face 0 is (1, 2, 3), face 1 is (0, 1, 3): the tile triples, turned by
     * the caller. Each is one_abc's decision sequence exactly: a face with a
     * near-clipped corner is kept for the near-plane rebuild, otherwise the
     * winding decides, then the depth-range gate. */
    {
        int const x2 = vx[c2], y2 = vy[c2], z2 = vz[c2];
        int const depth = div3_fast_fixedpoint(z1 + z2 + z3) + model_min_depth;
        bool const clip = near_clipped &&
                          (x1 == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                           x2 == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                           x3 == TORIDRAW_SCREEN_X_NEAR_CLIPPED);
        accept0 = (clip || toridraw_winding_2d_front_facing(x1, y1, x2, y2, x3, y3)) &&
                  (unsigned int)depth < levels;
        key0 = ((uint32_t)(0xFFFF - depth) << 16) | 0u;
    }
    {
        int const x0 = vx[c0], y0 = vy[c0], z0 = vz[c0];
        int const depth = div3_fast_fixedpoint(z0 + z1 + z3) + model_min_depth;
        bool const clip = near_clipped &&
                          (x0 == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                           x1 == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                           x3 == TORIDRAW_SCREEN_X_NEAR_CLIPPED);
        accept1 = (clip || toridraw_winding_2d_front_facing(x0, y0, x1, y1, x3, y3)) &&
                  (unsigned int)depth < levels;
        key1 = ((uint32_t)(0xFFFF - depth) << 16) | 1u;
    }

    /* Ascending key order is back to front, face order within a depth: the
     * two-key compare-swap the general path ends with. */
    if( accept0 && accept1 )
    {
        int const first = key0 > key1;
        order[0] = first;
        order[1] = 1 - first;
        n = 2;
    }
    else if( accept0 )
    {
        order[0] = 0;
        n = 1;
    }
    else if( accept1 )
    {
        order[0] = 1;
        n = 1;
    }
    scene->tmp_face_order_count = n;
    /* Nothing was stashed; the general path says the same for a
     * non-presorting call. */
    scene->sm_face_xy_valid = 0;
}

static void
toridraw_face_sort_bitonic_radix_tile2_fast(
    struct ToriDraw_Scene* scene,
    int rot,
    bool near_clipped,
    int model_min_depth)
{
    assert(scene);
    assert(rot >= 0);
    assert(rot <= 3);
    g_toridraw_sort_tile_fast_models++;
    /* Rotation 0 is 94% of tiles and gets the corners as literals; the
     * other three turn them as world_decode_tile turned the triples. */
    if( rot == 0 )
        toridraw_face_sort_bitonic_radix_tile2_fast_corners(
            scene, 0, 1, 2, 3, near_clipped, model_min_depth);
    else
        toridraw_face_sort_bitonic_radix_tile2_fast_corners(
            scene,
            (0 - rot) & 3,
            (1 - rot) & 3,
            (2 - rot) & 3,
            (3 - rot) & 3,
            near_clipped,
            model_min_depth);
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
#if defined(TORIDRAW_FACE_SORT_LANE_NEON64)
#include "impl/facesort/facesort.bitonic_radix.small.neon64.u.c"
#elif defined(TORIDRAW_FACE_SORT_LANE_NEON32)
#include "impl/facesort/facesort.bitonic_radix.small.neon32.u.c"
#elif defined(TORIDRAW_FACE_SORT_LANE_SSE2)
#include "impl/facesort/facesort.bitonic_radix.small.sse2.u.c"
#else
#include "impl/facesort/facesort.bitonic_radix.small.scalar.u.c"
#endif

/*
 * The radix over the depth half of the key. Stable, so the face order the
 * cull wrote survives within a depth. `tmp` is the bounce buffer, as long as
 * `keys`; the return value says which of the two holds the sorted run.
 *
 * TWO SPECIALISATIONS, BOTH ON FACTS THE CALLER ALREADY HOLDS:
 *
 *   The digits are sized to depth_levels, not to the byte. An accepted
 *   depth is < depth_levels <= 2^bits, so the field 0xFFFF - depth has its
 *   top 16 - bits bits all ones on every key, and its low `bits` bits carry
 *   the whole order. Two digits of bits/2 each: at 16K levels (14 bits) the
 *   passes count into 128 buckets, not 256, and the prefix sums and the
 *   histogram clears -- a FIXED cost per model, paid by every model above
 *   the bitonic crossover, most of which have 60..150 keys -- halve.
 *
 *   A SHALLOW model takes ONE pass. When the lane could bound the model's
 *   depths (scene->sm_sort_depth_lo/hi, off the z range of its vertices)
 *   inside 256 levels, the field differs across the model only in its low
 *   eight bits after a rebase to depth_hi, and one counting pass over 256
 *   buckets is the whole sort: half the scatters and no second histogram.
 *   A model a tile or two deep is shallow at any distance.
 */
static inline int
toridraw_ceil_log2(int v)
{
    int bits = 0;
    assert(v > 0);
    while( (1 << bits) < v )
        bits++;
    return bits;
}

/*
 * TORIDRAW_SORT_RADIX_LEGACY=1: the two 8-bit digits and no shallow pass,
 * whatever depth_levels and the lane say. The A/B control arm for the two
 * specialisations above, so one binary measures both.
 */
static inline int
toridraw_face_sort_radix_legacy(void)
{
    static int legacy = -1;
    if( legacy < 0 )
    {
        const char* v = getenv("TORIDRAW_SORT_RADIX_LEGACY");
        legacy = (v && v[0] == '1') ? 1 : 0;
    }
    return legacy;
}

/* Debug census for the frame stat lines: how many models took each radix
 * shape this frame. Read and cleared by the renderer that prints them. */
int g_toridraw_radix_shallow_models = 0;
int g_toridraw_radix_two_pass_models = 0;

static const uint32_t*
toridraw_radix_sort_depth(
    uint32_t* RESTRICT keys,
    uint32_t* RESTRICT tmp,
    int n,
    int depth_levels,
    int depth_lo,
    int depth_hi)
{
    static int count0[256];
    static int count1[256];
    int i;

    assert(keys);
    assert(tmp);
    assert(depth_levels > 0);
    assert(depth_levels <= 0x10000);

    if( toridraw_face_sort_radix_legacy() )
    {
        depth_levels = 0x10000;
        depth_hi = INT_MAX;
    }

    if( depth_hi != INT_MAX && depth_hi - depth_lo < 256 )
    {
        /* Shallow: rebase to depth_hi and one pass over the low byte. */
        int const base = 0xFFFF - depth_hi;
        int sum = 0;
        g_toridraw_radix_shallow_models++;
        memset(count0, 0, sizeof(count0));
        for( i = 0; i < n; i++ )
        {
            int const d = (int)(keys[i] >> 16) - base;
            assert(d >= 0);
            assert(d < 256);
            count0[d]++;
        }
        for( i = 0; i < 256; i++ )
        {
            int const c = count0[i];
            count0[i] = sum;
            sum += c;
        }
        for( i = 0; i < n; i++ )
        {
            uint32_t const k = keys[i];
            tmp[count0[(int)(k >> 16) - base]++] = k;
        }
        return tmp;
    }

    {
        int const bits = toridraw_ceil_log2(depth_levels);
        int const b0 = (bits + 1) >> 1;
        int const b1 = bits - b0;
        int const n0 = 1 << b0;
        int const n1 = 1 << b1;
        uint32_t const m0 = (uint32_t)n0 - 1u;
        uint32_t const m1 = (uint32_t)n1 - 1u;
        int sum0 = 0;
        int sum1 = 0;

        assert(bits >= 2);
        assert(bits <= 16);
        g_toridraw_radix_two_pass_models++;
        memset(count0, 0, (size_t)n0 * sizeof(count0[0]));
        memset(count1, 0, (size_t)n1 * sizeof(count1[0]));
        for( i = 0; i < n; i++ )
        {
            uint32_t const d = keys[i] >> 16;
            count0[d & m0]++;
            count1[(d >> b0) & m1]++;
        }
        for( i = 0; i < n0; i++ )
        {
            int const c0 = count0[i];
            count0[i] = sum0;
            sum0 += c0;
        }
        for( i = 0; i < n1; i++ )
        {
            int const c1 = count1[i];
            count1[i] = sum1;
            sum1 += c1;
        }
        for( i = 0; i < n; i++ )
        {
            uint32_t const k = keys[i];
            tmp[count0[(k >> 16) & m0]++] = k;
        }
        for( i = 0; i < n; i++ )
        {
            uint32_t const k = tmp[i];
            keys[count1[(k >> (16 + b0)) & m1]++] = k;
        }
        return keys;
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
    int num_vertices,
    int tile2_rot,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c,
    const uint32_t** out_keys)
{
    uint32_t* const keys = scene->sm_sort_keys;
    int const stash_xy = presort;
    int tile_n = 0;
    int n = 0;
    int accepted = 0;
    int f = 0;

    assert(scene);
    assert(keys);
    assert(out_keys);

    /* The sorted run is in `keys` unless the radix says otherwise below. */
    *out_keys = keys;
    /* Unknown until a lane narrows it; see the fields' comment. */
    scene->sm_sort_depth_lo = 0;
    scene->sm_sort_depth_hi = INT_MAX;

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
    /* `n` counts keys WRITTEN; `accepted` the faces among them that passed
     * the cull. A lane that stores rejected lanes as sentinels makes the two
     * differ (see the hook contract); the sort runs over n and the sentinels
     * land past `accepted`, which is what the caller is told. */
    n += toridraw_face_sort_bitonic_radix_lane_blocks(
        scene,
        &f,
        num_faces,
        num_vertices,
        near_clipped,
        model_min_depth,
        stash_xy,
        vx,
        vy,
        vz,
        face_a,
        face_b,
        face_c,
        keys,
        &accepted);

    for( ; f < num_faces; f++ )
    {
        int const one = toridraw_face_sort_bitonic_radix_one(
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
        n += one;
        accepted += one;
    }

    if( n <= 2 )
        return toridraw_face_sort_bitonic_radix_sort2(n, keys);

    if( n <= toridraw_face_sort_bitonic_max() )
    {
        if( !toridraw_face_sort_bitonic_radix_lane_sort(keys, n) )
            qsort(keys, (size_t)n, sizeof(*keys), toridraw_key_compare);
    }
    else
        *out_keys = toridraw_radix_sort_depth(
            keys,
            scene->sm_sort_tmp,
            n,
            scene->depth_levels,
            scene->sm_sort_depth_lo,
            scene->sm_sort_depth_hi);

    return accepted;
}

/*
 * Debug census for the frame stat lines, beside the radix pair above: models
 * this frame whose face priorities were all one value (and so emitted in key
 * order, skipping the partition) and models that took the partition.
 */
int g_toridraw_prio_uniform_models = 0;
int g_toridraw_prio_varied_models = 0;

/*
 * True when every one of the model's `face_count` priorities is the same
 * value. THE DEGENERATE CASE OF THE PRIORITY PARTITION: with one band
 * occupied, sort_face_draw_order_small emits that band alone -- the ten
 * fixed bands in band order, or the flexible list in list order -- and
 * either way that is the depth order the keys already hold. So a uniform
 * model's draw order IS its key order, and the partition and the merge
 * (two more passes over every key, the first with a scatter per key) are
 * skipped. Measured on the phone before this existed: those two passes
 * were a fifth of the sort.
 *
 * Answered per sort rather than cached on the model because the model
 * tools rewrite face_priorities in place; a word compare over face_count/2
 * nibble-packed bytes is under a tenth of an instruction per face.
 */
static inline bool
toridraw_face_priorities_uniform(
    const uint8_t* packed,
    int face_count)
{
    uint8_t const first = packed[0] & 0x0Fu;
    uint8_t const both = (uint8_t)(first | (first << 4));
    int const whole = face_count >> 1; /* bytes holding two real faces */
    int i;

    assert(packed);
    assert(face_count > 0);

    for( i = 0; i + 4 <= whole; i += 4 )
    {
        uint32_t word;
        memcpy(&word, packed + i, sizeof(word));
        if( word != (uint32_t)both * 0x01010101u )
            return false;
    }
    for( ; i < whole; i++ )
        if( packed[i] != both )
            return false;
    if( face_count & 1 )
        return (packed[whole] & 0x0Fu) == first;
    return true;
}

/*
 * The priority partition off the sorted keys: the same fold of the old
 * partition and accumulation as the small-mode CSR twin, reading (face,
 * depth) pairs from the key array in the order the buckets emitted them.
 *
 * Trimmed against that twin on the phone's profile: the twelve band bases
 * are computed once, not `prio * max_faces` per key; the nibble comes out
 * with a shift by (index & 1) * 4 rather than a branch; and
 * scene->sm_prio_count, which nothing reads back, is not stored per key.
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
    faceint_t* const prio_faces = scene->sm_prio_faces;
    int* const flex11 = scene->sm_flex_prio11_face_to_depth;
    int* const flex12 = scene->sm_flex_prio12_face_to_depth;
    int base[12];
    int i;

    assert(scene);
    assert(keys);
    assert(priority_depths);
    assert(counts);
    assert(face_priorities);

    for( i = 0; i < 12; i++ )
        base[i] = i * max_faces;
    memset(scene->sm_prio_count, 0, sizeof(scene->sm_prio_count));

    for( i = 0; i < n; i++ )
    {
        uint32_t const k = keys[i];
        int const face_idx = (int)(k & 0xFFFF);
        int const depth = 0xFFFF - (int)(k >> 16);
        int const prio =
            (face_priorities[face_idx >> 1] >> ((face_idx & 1) << 2)) & 0x0F;
        int nn;

        assert(face_idx >= 0 && face_idx < max_faces);
        assert(prio >= 0 && prio < 12 && "face priority indexes counts[12]");

        nn = counts[prio];
        assert(nn >= 0 && nn < max_faces);

        prio_faces[base[prio] + nn] = (faceint_t)face_idx;

        if( prio < 10 )
            priority_depths[prio] += depth;
        else
        {
            assert(depth >= 0 && depth <= 0xFFFF);
            assert(nn < scene->flex_prio_capacity);
            if( prio == 10 )
                flex11[nn] = depth | (face_idx << 16);
            else
                flex12[nn] = depth | (face_idx << 16);
        }

        counts[prio] = nn + 1;
    }
}

#endif /* TORIDRAW_FACE_SORT_BITONIC_RADIX_U_C */
