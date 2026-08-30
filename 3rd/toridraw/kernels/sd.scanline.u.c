#ifndef TORIDRAW_KERNELS_SD_SCANLINE_U_C
#define TORIDRAW_KERNELS_SD_SCANLINE_U_C

/*
 * Stock SD painter, `scanline` walk.
 *
 * Same four face classes as the branching kernel, reached through the family
 * that resolves the trapezoid once per triangle instead of per row. Selected by
 * TORIDRAW_RASTER_SCANLINE / ToriDraw_RasterSetScanline before ToriDraw_Init.
 *
 * Never takes a presorted run: it is its own rasteriser, and the run doors
 * belong to the branching family alone.
 */

static const struct ToriDraw_RasterKernelSDVTable g_stock_scanline_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_scanline_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_scanline_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_scanline_textured_gouraud,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_scanline_textured_flat,
    },
};

static const struct ToriDraw_RasterKernelSD g_stock_scanline_kernel = {
    .vtable = &g_stock_scanline_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetScanline(void)
{
    return &g_stock_scanline_kernel;
}

#endif /* TORIDRAW_KERNELS_SD_SCANLINE_U_C */
