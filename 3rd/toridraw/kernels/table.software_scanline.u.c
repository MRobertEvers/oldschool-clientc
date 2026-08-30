#ifndef TORIDRAW_KERNELS_TABLE_SOFTWARE_SCANLINE_U_C
#define TORIDRAW_KERNELS_TABLE_SOFTWARE_SCANLINE_U_C

/*
 * The scanline rasteriser.
 *
 * A different rasteriser, not a mode of the branching one: it resolves the
 * trapezoid once per triangle instead of per row, and it has its own vtable.
 * That vtable has no whole-model door and must not grow one -- a presorted run
 * drawing scanline faces would draw the branching family's pixels.
 *
 * Because the door is absent, ToriDraw_KernelScratchNeeds does not ask for the
 * presort stash for this table, so the sort does not pay seven stores per face
 * to fill a buffer this raster would never load.
 */

static struct ToriDraw_Kernel g_kernel_software_scanline = {
    .name = "software-scanline",
    .raster = &g_stock_scanline_kernel,
};

const struct ToriDraw_Kernel*
ToriDraw_KernelGetSoftwareScanline(void)
{
    toridraw_sd_kernel_publish(&g_stock_scanline_kernel);
    toridraw_kernel_table_publish(&g_kernel_software_scanline);
    return &g_kernel_software_scanline;
}

#endif /* TORIDRAW_KERNELS_TABLE_SOFTWARE_SCANLINE_U_C */
