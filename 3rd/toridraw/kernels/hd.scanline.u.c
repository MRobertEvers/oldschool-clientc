#ifndef TORIDRAW_KERNELS_HD_SCANLINE_U_C
#define TORIDRAW_KERNELS_HD_SCANLINE_U_C

/*
 * HD painter, `scanline` walk.
 *
 * Differs from the HD branching kernel in the two solid classes only. The four
 * mapped classes are the same callbacks: their attributes are planes through
 * the three vertices, and a plane is invariant under vertex permutation, so
 * they solve once from the unsorted triangle and never needed the walk the
 * scanline family replaces.
 */

#ifndef TORIDRAW_PIXEL16

static const struct ToriDraw_RasterKernelHDVTable g_hd_scanline_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_HD_GOURAUD] = hd_scanline_gouraud,
        [TORIDRAW_RASTER_FACE_HD_FLAT] = hd_scanline_flat,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE] = hd_draw_plane_painter,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER] = hd_draw_cylinder,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE] = hd_draw_cube,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE] = hd_draw_sphere,
    },
};

static const struct ToriDraw_RasterKernelHD g_hd_scanline_kernel = {
    .name = "scanline",
    .vtable = &g_hd_scanline_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

#else /* TORIDRAW_PIXEL16 */

static const struct ToriDraw_RasterKernelHD g_hd_scanline_kernel = {
    .name = "scanline",
    .vtable = &g_hd_pixel16_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

#endif /* TORIDRAW_PIXEL16 */

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetScanline(void)
{
    return &g_hd_scanline_kernel;
}

#endif /* TORIDRAW_KERNELS_HD_SCANLINE_U_C */
