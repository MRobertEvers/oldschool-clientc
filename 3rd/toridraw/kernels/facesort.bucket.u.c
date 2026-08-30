#ifndef TORIDRAW_KERNELS_FACESORT_BUCKET_U_C
#define TORIDRAW_KERNELS_FACESORT_BUCKET_U_C

/*
 * The depth-bucket face cull and sort.
 *
 * One scalar winding test per face, scattered into per-depth lists, prefix
 * summed, walked far to near. The reference the flat sort is held to, order
 * for order (toridraw_face_sort_flat_test.c).
 *
 * SCRATCH. On a full scene this walks the dense depth_levels x depth_stride
 * bucket table; on a small scene it walks the CSR variant sized off max_faces.
 * ToriDraw_RasterKernelSDScratchNeeds reports which, and the scene allocates
 * accordingly -- see ToriDraw_SceneEnsureScratch.
 */

static int
face_sort_kernel_bucket(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    bool presort)
{
    (void)user_data;
    if( scene->flags & TORIDRAW_SCENE_SMALL )
        toridraw_compute_projected_face_order_small(scene, hnd, presort, 0);
    else
        ToriDraw_ComputeProjectedFaceOrder(scene, hnd, presort);
    return scene->tmp_face_order_count;
}

static const struct ToriDraw_FaceCullSortKernel g_stock_face_sort_bucket = {
    .name = "bucket",
    .sort = face_sort_kernel_bucket,
    /* Works on either tier: the dense bucket table on a full scene, the CSR
     * variant on a small one. Can stash on a small scene. */
    .provides = TORIDRAW_FACESORT_PROVIDES_FACE_ORDER |
                TORIDRAW_FACESORT_PROVIDES_PRESORTED_XY,
    .needs = 0,
};

const struct ToriDraw_FaceCullSortKernel*
ToriDraw_FaceCullSortKernelGetBucket(void)
{
    return &g_stock_face_sort_bucket;
}

#endif /* TORIDRAW_KERNELS_FACESORT_BUCKET_U_C */
