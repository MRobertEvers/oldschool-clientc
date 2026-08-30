#ifndef TORIDRAW_KERNELS_SD_SMOOTH_SCANLINE_U_C
#define TORIDRAW_KERNELS_SD_SMOOTH_SCANLINE_U_C

/*
 * Stock SD painter, `scanline` walk, smooth gouraud.
 *
 * The smooth twin of the scanline kernel, and the same one-slot difference:
 * only the gouraud class changes.
 */

static const struct ToriDraw_RasterKernelSDVTable g_stock_smooth_scanline_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_smooth_scanline_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_scanline_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_scanline_textured_gouraud,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_scanline_textured_flat,
    },
};

static struct ToriDraw_RasterKernelSD g_stock_smooth_scanline_kernel = {
    .draw_model = ToriDraw_RasterWalkPerFace,
    .vtable = &g_stock_smooth_scanline_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
    /* Smooth, for the reason the smooth branching kernel gives. */
    .zbuffered_variant = &g_stock_smooth_sorted_zbuffered_kernel,
};

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetSmoothScanline(void)
{
    toridraw_sd_kernel_publish(&g_stock_smooth_scanline_kernel);
    return &g_stock_smooth_scanline_kernel;
}

#endif /* TORIDRAW_KERNELS_SD_SMOOTH_SCANLINE_U_C */
