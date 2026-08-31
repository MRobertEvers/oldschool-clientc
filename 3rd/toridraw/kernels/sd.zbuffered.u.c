#ifndef TORIDRAW_KERNELS_SD_ZBUFFERED_U_C
#define TORIDRAW_KERNELS_SD_ZBUFFERED_U_C

/*
 * Stock SD depth-tested painter.
 *
 * ONE vtable, four kernel objects. The depth-tested family resolves per pixel,
 * so the draw callbacks never change; what varies is whether the caller also
 * wants the face sort run in front of them, and whether the gouraud class is
 * smoothed. Both are flag combinations over the same four callbacks, so they
 * are four objects rather than four vtables:
 *
 *   zbuffered                 no face sort -- faces draw in model order and
 *                             the depth buffer decides. The reason a
 *                             TORIDRAW_MODEL_FLAG_ZBUFFER model skips stage 2
 *                             entirely.
 *   sorted_zbuffered          face sort AND depth test, for a caller that
 *                             wants both (transparency ordering over a
 *                             depth-resolved model).
 *   smooth_*                  the same two, selected when the caller asked
 *                             for smooth shading; the vtable is shared
 *                             because the depth family has no separate
 *                             smooth gouraud callback. They are distinct
 *                             OBJECTS regardless, because the smooth painters
 *                             name them as their zbuffered_variant -- that is
 *                             what carries "the caller wanted smooth" across
 *                             the swap, and what a smooth depth callback would
 *                             only have to be dropped into.
 *
 * Included before the painter kernels (toridraw_raster.u.c), which name these
 * four by address.
 */

static const struct ToriDraw_RasterKernelSDVTable g_stock_zbuffered_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_zbuffered_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_zbuffered_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_zbuffered_textured,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_zbuffered_textured,
    },
};

static struct ToriDraw_RasterKernelSD g_stock_zbuffered_kernel = {
    .name = "zbuffered",
    .draw_model = ToriDraw_RasterWalkPerFace,
    .vtable = &g_stock_zbuffered_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

static struct ToriDraw_RasterKernelSD g_stock_smooth_zbuffered_kernel = {
    .name = "smooth_zbuffered",
    .draw_model = ToriDraw_RasterWalkPerFace,
    .vtable = &g_stock_zbuffered_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

static struct ToriDraw_RasterKernelSD g_stock_sorted_zbuffered_kernel = {
    .name = "sorted_zbuffered",
    .draw_model = ToriDraw_RasterWalkPerFace,
    .vtable = &g_stock_zbuffered_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING |
             TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

static struct ToriDraw_RasterKernelSD g_stock_smooth_sorted_zbuffered_kernel = {
    .name = "smooth_sorted_zbuffered",
    .draw_model = ToriDraw_RasterWalkPerFace,
    .vtable = &g_stock_zbuffered_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING |
             TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

static const struct ToriDraw_RasterKernelSD*
toridraw_stock_zbuffered_kernel(bool smooth, bool sorted)
{
    struct ToriDraw_RasterKernelSD* kernel;

    if( sorted )
        kernel = smooth ? &g_stock_smooth_sorted_zbuffered_kernel
                        : &g_stock_sorted_zbuffered_kernel;
    else
        kernel = smooth ? &g_stock_smooth_zbuffered_kernel : &g_stock_zbuffered_kernel;
    /* Stage 2 does not run for the unsorted pair, but the slots are filled all
     * the same: every stage entry asserts, and a caller running the stages by
     * hand must find a real sort named rather than a hole. */
    return kernel;
}

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetZBuffered(void)
{
    return toridraw_stock_zbuffered_kernel(false, false);
}

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetSmoothZBuffered(void)
{
    return toridraw_stock_zbuffered_kernel(true, false);
}

#endif /* TORIDRAW_KERNELS_SD_ZBUFFERED_U_C */
