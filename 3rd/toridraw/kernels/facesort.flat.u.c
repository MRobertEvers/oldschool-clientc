#ifndef TORIDRAW_KERNELS_FACESORT_FLAT_U_C
#define TORIDRAW_KERNELS_FACESORT_FLAT_U_C

/*
 * The flat face cull and sort: SIMD cull, composite keys, bitonic or radix.
 *
 * Emits the same order as the bucket sort, face for face, so which one ran is
 * invisible in the pixels and visible only in the time.
 *
 * ONLY DIFFERS ON A SMALL SCENE. The key arrays it sorts (sm_sort_keys /
 * sm_sort_tmp) and the y-ordered stash it can leave behind (sm_face_x4 / y4)
 * are small-tier scratch; a full-mode scene has neither, so this kernel falls
 * through to the same full bucket sort the bucket kernel runs. That is not a
 * silent failure -- it is the reason ToriDraw_RasterKernelSDScratchNeeds
 * reports FLAT_KEYS and PRESORT_XY, so a caller can ask the scene whether the
 * kernel it selected will actually be the kernel that runs.
 */

static int
face_sort_kernel_flat(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    bool presort)
{
    (void)user_data;
    if( scene->flags & TORIDRAW_SCENE_SMALL )
        toridraw_compute_projected_face_order_small(scene, hnd, presort, 1);
    else
        ToriDraw_ComputeProjectedFaceOrder(scene, hnd, presort);
    return scene->tmp_face_order_count;
}

static const struct ToriDraw_FaceCullSortKernel g_stock_face_sort_flat = {
    .name = "flat",
    .sort = face_sort_kernel_flat,
    /* The key arrays are small-tier scratch, and on a full scene this falls
     * through to the same bucket walk -- so it needs both to be the kernel
     * its name claims. */
    .provides = TORIDRAW_FACESORT_PROVIDES_FACE_ORDER |
                TORIDRAW_FACESORT_PROVIDES_PRESORTED_XY,
    .needs = TORIDRAW_FACESORT_NEEDS_FLAT_KEYS |
             TORIDRAW_FACESORT_NEEDS_SMALL_SCENE,
};

const struct ToriDraw_FaceCullSortKernel*
ToriDraw_FaceCullSortKernelGetFlat(void)
{
    return &g_stock_face_sort_flat;
}

const struct ToriDraw_FaceCullSortKernel*
ToriDraw_FaceCullSortKernelGetDefault(void)
{
    return toridraw_face_sort_flat_armed() ? &g_stock_face_sort_flat
                                           : &g_stock_face_sort_bucket;
}

#endif /* TORIDRAW_KERNELS_FACESORT_FLAT_U_C */
