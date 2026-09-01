#ifndef TORIDRAW_FACE_SORT_BITONIC_RADIX_SCALAR_U_C
#define TORIDRAW_FACE_SORT_BITONIC_RADIX_SCALAR_U_C

#include "impl/facesort/facesort.bitonic_radix.small.dispatch.h"

/*
 * No vector cull in this build.
 *
 * A real lane and not a gap: it is what wasm, an x86 build with SSE2_DISABLED,
 * and any target with neither NEON nor SSE2 compile. All three hooks decline,
 * so the sort is the scalar per-face loop followed by qsort or the radix --
 * which is why the KERNEL IS NOT CALLED bitonic+radix here. There is no
 * bitonic network without vectors, so what this build runs is the radix half
 * alone, and that is the name kernels/facesort.bitonic_radix.u.c gives it.
 *
 * It is correct, and slower than the bucket sort it replaces, which is why
 * toridraw_face_sort_bitonic_radix_armed defaults the OTHER way here: without
 * TORIDRAW_FACE_SORT_SIMD this sort is opt-in
 * (TORIDRAW_FACE_SORT=bitonic_radix) and exists so
 * toridraw_face_sort_bitonic_radix_test has something to hold the bucket sort
 * against on a host with no SIMD at all.
 */

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
    (void)num_vertices; /* this lane gathers lane by lane from the axis arrays */
    *out_accepted = 0;
    return TORIDRAW_FACE_SORT_BLOCKS_DECLINE(
        scene,
        f_io,
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
    return TORIDRAW_FACE_SORT_TILE2_DECLINE(
        scene, tile2_rot, near_clipped, model_min_depth, stash_xy, vx, vy, vz, keys, out_n);
}

static inline bool
toridraw_face_sort_bitonic_radix_lane_sort(
    uint32_t* keys,
    int n)
{
    /* No bitonic network without vectors; the caller runs qsort. */
    (void)keys;
    (void)n;
    return false;
}

#endif /* TORIDRAW_FACE_SORT_BITONIC_RADIX_SCALAR_U_C */
