#ifndef TORIDRAW_KERNELS_FACESORT_BITONIC_RADIX_U_C
#define TORIDRAW_KERNELS_FACESORT_BITONIC_RADIX_U_C

/*
 * The bitonic+radix face cull and sort: SIMD cull, composite keys, then a
 * bitonic network up to toridraw_face_sort_bitonic_max() keys and a two-pass
 * 8-bit radix above it.
 *
 * Emits the same order as the bucket sort, face for face, so which one ran is
 * invisible in the pixels and visible only in the time.
 *
 * THE NAME IS PER LANE, because the bitonic half is. The network is vector
 * code and lives in the NEON and SSE2 lanes; a build that reaches neither
 * gate -- wasm, SSE2_DISABLED, anything else -- has the radix and qsort and no
 * network, so it registers the same kernel under the name `radix`. Both are
 * one algorithm and one entry point; what differs is which half of the sort
 * step this build actually compiled, and the readout at init says which. See
 * facesort.bitonic_radix.small.dispatch.h for the lane ladder that sets
 * TORIDRAW_FACE_SORT_SIMD.
 *
 * ONLY DIFFERS ON A SMALL SCENE. The key arrays it sorts (sm_sort_keys /
 * sm_sort_tmp) and the y-ordered stash it can leave behind (sm_face_x4 / y4)
 * are small-tier scratch; a full-mode scene has neither, so this kernel falls
 * through to the same full bucket sort the bucket kernel runs. That is not a
 * silent failure -- it is the reason ToriDraw_RasterKernelSDScratchNeeds
 * reports BITONIC_RADIX_KEYS and PRESORT_XY, so a caller can ask the scene
 * whether the kernel it selected will actually be the kernel that runs.
 */

/*
 * The key sort where the keys exist, and the BUCKET KERNEL by name where they
 * do not.
 *
 * The composite keys this sorts (sm_sort_keys / sm_sort_tmp) are small-tier
 * scratch. A full-mode scene has none, so on one there is no key sort to run
 * -- and what runs instead is not "a fallback path inside this kernel", it is
 * the other kernel, called through its own entry so that reading this function
 * tells you so.
 *
 * That substitution is REPORTED, not silent: the kernel declares
 * TORIDRAW_FACESORT_NEEDS_SMALL_SCENE, ToriDraw_KernelValidate turns that into
 * DEGRADED on a full scene, and ToriDraw_KernelTake prints it once at init --
 * which is the whole reason the fit enum has three values instead of two.
 */
static int
face_sort_kernel_bitonic_radix(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    bool presort)
{
    (void)user_data;
    if( !(scene->flags & TORIDRAW_SCENE_SMALL) )
        return g_stock_face_sort_bucket.sort(
            g_stock_face_sort_bucket.user_data, scene, hnd, presort);

    toridraw_compute_projected_face_order_small(scene, hnd, presort, 1);
    return scene->tmp_face_order_count;
}

static const struct ToriDraw_FaceCullSortKernel g_stock_face_sort_bitonic_radix = {
#ifdef TORIDRAW_FACE_SORT_SIMD
    .name = "bitonic+radix",
#else
    /* No vector lane in this build, so no bitonic network: small counts go to
     * qsort and large ones to the radix. Naming the network here would put an
     * algorithm this binary does not contain into the init readout. */
    .name = "radix",
#endif
    .sort = face_sort_kernel_bitonic_radix,
    /* The key arrays are small-tier scratch, and on a full scene this falls
     * through to the same bucket walk -- so it needs both to be the kernel
     * its name claims. */
    .provides = TORIDRAW_FACESORT_PROVIDES_FACE_ORDER |
                TORIDRAW_FACESORT_PROVIDES_PRESORTED_XY,
    .needs = TORIDRAW_FACESORT_NEEDS_BITONIC_RADIX_KEYS |
             TORIDRAW_FACESORT_NEEDS_SMALL_SCENE,
};

const struct ToriDraw_FaceCullSortKernel*
ToriDraw_FaceCullSortKernelGetBitonicRadix(void)
{
    return &g_stock_face_sort_bitonic_radix;
}

const struct ToriDraw_FaceCullSortKernel*
ToriDraw_FaceCullSortKernelGetDefault(void)
{
    return toridraw_face_sort_bitonic_radix_armed() ? &g_stock_face_sort_bitonic_radix
                                                    : &g_stock_face_sort_bucket;
}

#endif /* TORIDRAW_KERNELS_FACESORT_BITONIC_RADIX_U_C */
