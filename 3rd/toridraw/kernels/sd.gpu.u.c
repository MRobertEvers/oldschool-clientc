#ifndef TORIDRAW_KERNELS_SD_GPU_U_C
#define TORIDRAW_KERNELS_SD_GPU_U_C

/*
 * The kernel a GPU renderer holds: projection and face cull+sort only.
 *
 * Its raster vtable is NULL -- the faces go to a vertex buffer, never to a
 * software span -- so it is accepted by the stage-1 and stage-2 entries and
 * refused by every raster entry (the stage-3 table entry asserts
 * on the vtable).
 *
 * NEEDS_FACE_SORTING is set because a GPU renderer still wants the
 * back-to-front order; it reads it out of tmp_face_order and uploads. It never
 * wants the presort stash: nothing downstream of it walks a software span, so
 * filling sm_face_x4 / y4 would be stores into a buffer no one loads. The
 * D3D9, GL and WebGL renderers therefore call the plain sort entry.
 */

static struct ToriDraw_RasterKernelSD g_stock_gpu_kernel = {
    .name = "gpu",
    /* No stage 3 at all: both slots NULL, and every raster entry asserts. */
    .draw_model = NULL,
    .vtable = NULL,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetGpu(void)
{
    return &g_stock_gpu_kernel;
}

#endif /* TORIDRAW_KERNELS_SD_GPU_U_C */
