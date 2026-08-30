#ifndef TORIDRAW_KERNELS_TABLE_GPU_U_C
#define TORIDRAW_KERNELS_TABLE_GPU_U_C

/*
 * D3D9 / GL / WebGL: stages 1 and 2 only.
 *
 * A NULL raster slot is how a table says it has no software raster stage. The
 * faces go to a vertex upload, so the renderer wants the back-to-front order
 * and nothing else -- it reads tmp_face_order and never walks a span.
 *
 * That is exactly why the stash must not be filled here. A GPU lane paying
 * seven stores and a six-way compare per drawn face into sm_face_x4/y4, for a
 * buffer nothing downstream loads, is the regression the presort split exists
 * to prevent; TORIDRAW_BATCH_STATS reporting a non-zero presort count on a GPU
 * lane is the symptom. With no raster there is no whole-model door, so
 * ToriDraw_KernelScratchNeeds does not ask for it.
 */

static const struct ToriDraw_Kernel g_kernel_gpu = {
    .name = "gpu",
    .projection = NULL,
    .face_sort = NULL,
    .raster = NULL,
};

const struct ToriDraw_Kernel*
ToriDraw_KernelGetGpu(void)
{
    return &g_kernel_gpu;
}

#endif /* TORIDRAW_KERNELS_TABLE_GPU_U_C */
