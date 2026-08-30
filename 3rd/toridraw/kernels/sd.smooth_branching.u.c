#ifndef TORIDRAW_KERNELS_SD_SMOOTH_BRANCHING_U_C
#define TORIDRAW_KERNELS_SD_SMOOTH_BRANCHING_U_C

/*
 * Stock SD painter, `branching` walk, smooth gouraud.
 *
 * Differs from the plain branching kernel in one slot: the gouraud class. Flat
 * and both textured classes are the same callbacks, because smoothing is a
 * property of how a per-vertex colour gradient is interpolated and the other
 * three do not carry one.
 *
 * A separate vtable rather than a flag on the branching kernel, which is what
 * lets the batched walk identify the branching family by pointer.
 */

static const struct ToriDraw_RasterKernelSDVTable g_stock_smooth_branching_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_smooth_branching_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_branching_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_branching_textured_gouraud,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_branching_textured_flat,
    },
};

static const struct ToriDraw_RasterKernelSD g_stock_smooth_branching_kernel = {
    .vtable = &g_stock_smooth_branching_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetSmoothBranching(void)
{
    return &g_stock_smooth_branching_kernel;
}

#endif /* TORIDRAW_KERNELS_SD_SMOOTH_BRANCHING_U_C */
