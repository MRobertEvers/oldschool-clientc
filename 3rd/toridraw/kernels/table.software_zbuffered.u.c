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
 * ToriDraw_SceneZBufferResize.
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

/*
 * The same depth world with its translucency deferred, as two tables: every
 * model through the OPAQUE table as it arrives, then, after the last one, the
 * models that have face alpha through the TRANSLUCENT one, back to front --
 * all inside one ToriDraw_SceneZBufferBeginPass.
 *
 * Stage 2 runs for the translucent table only, and so only for the models a
 * caller hands it: the painter's face sort, whose order the translucent faces
 * draw in. The opaque table sorts nothing, as software-zbuffered does not.
 */
static struct ToriDraw_Kernel g_kernel_software_zbuffered_opaque = {
    .name = "software-zbuffered-opaque",
    .raster = &g_stock_zbuffered_opaque_kernel,
};

static struct ToriDraw_Kernel g_kernel_software_zbuffered_translucent = {
    .name = "software-zbuffered-translucent",
    .raster = &g_stock_zbuffered_translucent_kernel,
};

const struct ToriDraw_Kernel*
ToriDraw_KernelGetSoftwareZBufferedOpaque(void)
{
    toridraw_kernel_table_publish(&g_kernel_software_zbuffered_opaque);
    return &g_kernel_software_zbuffered_opaque;
}

const struct ToriDraw_Kernel*
ToriDraw_KernelGetSoftwareZBufferedTranslucent(void)
{
    toridraw_kernel_table_publish(&g_kernel_software_zbuffered_translucent);
    return &g_kernel_software_zbuffered_translucent;
}

#endif /* TORIDRAW_KERNELS_TABLE_SOFTWARE_ZBUFFERED_U_C */
