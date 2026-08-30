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

/*
 * Two real implementations of one algorithm, and the scene's TIER picks
 * between them: the dense depth_levels x depth_stride table on a full scene,
 * the CSR variant sized off max_faces on a small one.
 *
 * The tier is fixed at ToriDraw_SceneNew and never changes, so this is a
 * per-model test of a per-SCENE constant -- the honest place for it would be
 * kernel selection. It is not there because the getters that hand out kernels
 * have no scene to ask, and the tables they fill are process-wide mutable
 * statics that the face-sort knob rewrites, so a scene-side resolution cached
 * against a table pointer would go stale the moment that knob moved. Asking
 * once per model, off a flag already in cache, is what that buys instead.
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
