#ifndef TORIDRAW_KERNELS_TABLE_SOFTWARE_ZBUFFERED_U_C
#define TORIDRAW_KERNELS_TABLE_SOFTWARE_ZBUFFERED_U_C

/*
 * Depth-resolved models.
 *
 * STAGE 2 DOES NOT RUN. The kernel does not set NEEDS_FACE_SORTING, so no
 * back-to-front order is produced or consumed: faces draw in the order the
 * model stores them and the depth buffer decides what survives. The face_sort
 * slot is still filled, because a caller may run the stages by hand and the
 * table should name a real sort rather than leave a hole -- it simply is not
 * called for this raster.
 *
 * The z-buffer itself is NOT scene-tier scratch and ToriDraw_KernelEnsureScratch
 * will not provision it; it is sized from the viewport, so call
 * ToriDraw_SceneZBufferResize. Under TORIDRAW_PIXEL16 this table is
 * INCOMPATIBLE and ToriDraw_KernelValidate says so.
 */

static struct ToriDraw_Kernel g_kernel_software_zbuffered = {
    .name = "software-zbuffered",
    .raster = &g_stock_zbuffered_kernel,
};

const struct ToriDraw_Kernel*
ToriDraw_KernelGetSoftwareZBuffered(void)
{
    toridraw_kernel_table_publish(&g_kernel_software_zbuffered);
    return &g_kernel_software_zbuffered;
}

#endif /* TORIDRAW_KERNELS_TABLE_SOFTWARE_ZBUFFERED_U_C */
