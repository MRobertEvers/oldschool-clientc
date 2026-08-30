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

static struct ToriDraw_RasterKernelSD g_stock_smooth_branching_kernel = {
    .draw_model = ToriDraw_RasterWalkPerFace,
    .vtable = &g_stock_smooth_branching_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
    /* The SMOOTH twin, which today draws the same pixels as the flat one --
     * the depth family shares one vtable. Naming it anyway is the point of the
     * slot: the day the depth family grows a smooth gouraud callback, this
     * line is already correct and no substitution site has to be taught to
     * tell a smooth painter from a flat one. */
    .zbuffered_variant = &g_stock_smooth_sorted_zbuffered_kernel,
};

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetSmoothBranching(void)
{
    toridraw_sd_kernel_publish(&g_stock_smooth_branching_kernel);
    return &g_stock_smooth_branching_kernel;
}

#endif /* TORIDRAW_KERNELS_SD_SMOOTH_BRANCHING_U_C */
