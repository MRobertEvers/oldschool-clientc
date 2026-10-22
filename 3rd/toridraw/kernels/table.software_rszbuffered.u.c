#ifndef TORIDRAW_KERNELS_TABLE_SOFTWARE_RSZBUFFERED_U_C
#define TORIDRAW_KERNELS_TABLE_SOFTWARE_RSZBUFFERED_U_C

/*
 * The rev-241 client's `::zbuf 1` renderer, as two tables: draw the scene's
 * models through the opaque table, then the same models in the same order
 * through the translucent one, inside one frame-wide depth pass.
 *
 * Stage 1 is the rev-241 projection, not the default: the raster reads its
 * scratch. Stage 2 does not run (neither raster sets NEEDS_FACE_SORTING; the
 * walk orders faces itself), but the slot is filled with a real sort, as every
 * prebaked table's is.
 */

static struct ToriDraw_Kernel g_kernel_software_rszbuffered_opaque = {
    .name = "software-rszbuffered-opaque",
    .raster = &g_rszbuffered_opaque_kernel,
};

static struct ToriDraw_Kernel g_kernel_software_rszbuffered_translucent = {
    .name = "software-rszbuffered-translucent",
    .raster = &g_rszbuffered_translucent_kernel,
};

static const struct ToriDraw_Kernel*
toridraw_kernel_table_publish_rszbuffered(struct ToriDraw_Kernel* table)
{
    table->projection = ToriDraw_ProjectionKernelGetRsZbuf();
    table->face_sort = ToriDraw_FaceCullSortKernelGetDefault();
    return table;
}

const struct ToriDraw_Kernel*
ToriDraw_KernelGetSoftwareRsZBufferedOpaque(void)
{
    return toridraw_kernel_table_publish_rszbuffered(&g_kernel_software_rszbuffered_opaque);
}

const struct ToriDraw_Kernel*
ToriDraw_KernelGetSoftwareRsZBufferedTranslucent(void)
{
    return toridraw_kernel_table_publish_rszbuffered(&g_kernel_software_rszbuffered_translucent);
}

#endif /* TORIDRAW_KERNELS_TABLE_SOFTWARE_RSZBUFFERED_U_C */
