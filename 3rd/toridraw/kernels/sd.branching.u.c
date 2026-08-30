#ifndef TORIDRAW_KERNELS_SD_BRANCHING_U_C
#define TORIDRAW_KERNELS_SD_BRANCHING_U_C

/*
 * Stock SD painter, `branching` walk.
 *
 * The default software kernel, and the only one the batched walk will run
 * behind: toridraw_raster_draw_faces_batched tests for this exact vtable
 * pointer, because the scanline and smooth families are different rasterisers
 * and a run door drawing their faces would draw the wrong pixels.
 *
 * Included from toridraw_raster.u.c once the four stock callbacks exist; the
 * texture pair is a macro that resolves to an unreachable stub under
 * TORIDRAW_PIXEL16.
 */

static const struct ToriDraw_RasterKernelSDVTable g_stock_branching_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_branching_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_branching_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_branching_textured_gouraud,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_branching_textured_flat,
    },
#ifdef TORIDRAW_RASTER_BATCH
    /* The batched walk: runs of presorted faces into the assembly kernels. */
    .draw_model = toridraw_raster_draw_model_batched,
#endif
};

static const struct ToriDraw_RasterKernelSD g_stock_branching_kernel = {
    .vtable = &g_stock_branching_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetBranching(void)
{
    return &g_stock_branching_kernel;
}

#endif /* TORIDRAW_KERNELS_SD_BRANCHING_U_C */
