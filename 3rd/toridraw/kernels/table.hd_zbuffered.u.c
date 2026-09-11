#ifndef TORIDRAW_KERNELS_TABLE_HD_ZBUFFERED_U_C
#define TORIDRAW_KERNELS_TABLE_HD_ZBUFFERED_U_C

/*
 * HD, depth-resolved.
 *
 * STAGE 2 DOES NOT RUN. The kernel sets NEEDS_ZBUFFER without
 * NEEDS_FACE_SORTING, so no back-to-front order is produced or consumed:
 * faces draw in the order the model stores them and the depth buffer decides
 * what survives. A depth twin of every one of the six classes.
 *
 * The face_sort slot is still filled, for the reason the SD depth table gives:
 * a caller running the stages by hand must find a real sort named rather than
 * a hole. It simply is not called for this raster.
 *
 * The z-buffer is NOT scene-tier scratch and ToriDraw_KernelEnsureScratch will
 * not provision it -- it is sized from the viewport. HD sizes it itself, on
 * the first depth-tested model.
 */

static struct ToriDraw_Kernel g_kernel_hd_zbuffered = {
    .name = "hd-zbuffered",
    .raster = NULL,
    .raster_hd = NULL,
};

const struct ToriDraw_Kernel*
ToriDraw_KernelGetHDZBuffered(void)
{
    g_kernel_hd_zbuffered.raster_hd = ToriDraw_RasterKernelHDGetZBuffered();
    toridraw_kernel_table_publish(&g_kernel_hd_zbuffered);
    return &g_kernel_hd_zbuffered;
}

#endif /* TORIDRAW_KERNELS_TABLE_HD_ZBUFFERED_U_C */
