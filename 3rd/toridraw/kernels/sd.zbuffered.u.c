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
 *                             smooth gouraud callback.
 *
 * Under TORIDRAW_PIXEL16 every slot is the unsupported stub: the depth family
 * draws through the 32-bit texture and blend paths, and sd_render_with_kernel_z
 * asserts before reaching here anyway.
 */

static const struct ToriDraw_RasterKernelSDVTable g_stock_zbuffered_vtable = {
    .draw = {
#ifdef TORIDRAW_PIXEL16
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_zbuffered_unsupported,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_zbuffered_unsupported,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_zbuffered_unsupported,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_zbuffered_unsupported,
#else
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_zbuffered_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_zbuffered_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_zbuffered_textured,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_zbuffered_textured,
#endif
    },
};

static const struct ToriDraw_RasterKernelSD g_stock_zbuffered_kernel = {
    .vtable = &g_stock_zbuffered_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

static const struct ToriDraw_RasterKernelSD g_stock_smooth_zbuffered_kernel = {
    .vtable = &g_stock_zbuffered_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

static const struct ToriDraw_RasterKernelSD g_stock_sorted_zbuffered_kernel = {
    .vtable = &g_stock_zbuffered_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING |
             TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

static const struct ToriDraw_RasterKernelSD g_stock_smooth_sorted_zbuffered_kernel = {
    .vtable = &g_stock_zbuffered_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING |
             TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

static const struct ToriDraw_RasterKernelSD*
toridraw_stock_zbuffered_kernel(bool smooth, bool sorted)
{
    if( sorted )
        return smooth ? &g_stock_smooth_sorted_zbuffered_kernel
                      : &g_stock_sorted_zbuffered_kernel;
    return smooth ? &g_stock_smooth_zbuffered_kernel : &g_stock_zbuffered_kernel;
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
