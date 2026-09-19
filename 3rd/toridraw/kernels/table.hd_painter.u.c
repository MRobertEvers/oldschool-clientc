#ifndef TORIDRAW_KERNELS_TABLE_HD_PAINTER_U_C
#define TORIDRAW_KERNELS_TABLE_HD_PAINTER_U_C

/*
 * The HD painter.
 *
 * Six face classes rather than the SD four -- HD splits the textured class by
 * PROJECTION, because the projection is what distinguishes plane from cylinder
 * from cube from sphere, and a per-pixel branch on a per-triangle property is
 * waste. Texture shading, face alpha, texel gating, modulation and depth
 * testing are inputs to those six, not further vtable axes.
 *
 * The SD raster slot is NULL and the HD one is filled, which is how a table
 * says which pipeline it is for. Before this table existed the HD path reached
 * stages 1 and 2 by CALLING ToriDraw_RenderModel1Project and
 * ToriDraw_RenderModel2SortFaces directly -- so HD always took the prepared
 * projection and always took whichever sort the environment had named, and no
 * caller could hold, name, or swap either. Those were the last two hardwired
 * stage calls in the library.
 *
 * Branching or scanline as TORIDRAW_RASTER_SCANLINE decided, resolved at the
 * getter like the SD stock table. The two differ in the two SOLID classes
 * only: the four mapped classes solve their attributes as planes through the
 * three vertices, and a plane is invariant under vertex permutation, so they
 * never needed the walk the scanline family replaces.
 */

static struct ToriDraw_Kernel g_kernel_hd_painter = {
    .name = "hd-painter",
    .raster = NULL,
    .raster_hd = NULL,
};

const struct ToriDraw_Kernel*
ToriDraw_KernelGetHDPainter(void)
{
    g_kernel_hd_painter.raster_hd = ToriDraw_RasterGetScanline()
                                        ? ToriDraw_RasterKernelHDGetScanline()
                                        : ToriDraw_RasterKernelHDGetBranching();
    toridraw_kernel_table_publish(&g_kernel_hd_painter);
    return &g_kernel_hd_painter;
}

#endif /* TORIDRAW_KERNELS_TABLE_HD_PAINTER_U_C */
