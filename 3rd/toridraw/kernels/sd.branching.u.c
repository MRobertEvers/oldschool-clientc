#ifndef TORIDRAW_TORIDRAW_C_UNITY
/* Indexed on its own by an editor rather than reached through the unity build.
 * Pull in the translation unit that owns this fragment: it includes this file
 * back at its proper place, with the macro defined, where every name below is
 * in scope. Inert in a real build, where toridraw.c defines the macro first. */
#include "../toridraw.c"
#else

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
};

static const struct ToriDraw_RasterKernelSD g_stock_branching_kernel = {
    /* Staged runs into the assembly kernels, with the per-face walk behind
     * it. On a lane with no presorted-run assembly this name IS the per-face
     * walk (graphics/raster/batch/raster.batch.u.c), so there is nothing to
     * branch on here. */
    .draw_model = toridraw_raster_walk_batched,
    .vtable = &g_stock_branching_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetBranching(void)
{
    return &g_stock_branching_kernel;
}

#endif /* TORIDRAW_KERNELS_SD_BRANCHING_U_C */

#endif /* TORIDRAW_TORIDRAW_C_UNITY */
