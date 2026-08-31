#ifndef TORIDRAW_KERNELS_HD_BRANCHING_U_C
#define TORIDRAW_KERNELS_HD_BRANCHING_U_C

/*
 * HD painter, `branching` walk.
 *
 * Six face classes, not the SD four: HD splits the textured class by
 * PROJECTION -- plane, cylinder, cube, sphere -- because the projection is
 * what distinguishes them, and a per-pixel branch on a per-triangle property
 * is waste. Texture shading, face alpha, texel gating, modulation and depth
 * testing are inputs to those six, not further vtable axes.
 *
 * Under TORIDRAW_PIXEL16 the whole HD kernel set is stubs: HD draws through
 * the 32-bit texture and blend paths, so every slot is the shared no-op and
 * the getters still resolve, which keeps ToriDraw_RenderHD linkable on a
 * 16-bit target rather than making every caller ifdef.
 */

#ifndef TORIDRAW_PIXEL16

static const struct ToriDraw_RasterKernelHDVTable g_hd_branching_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_HD_GOURAUD] = hd_branching_gouraud,
        [TORIDRAW_RASTER_FACE_HD_FLAT] = hd_branching_flat,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE] = hd_draw_plane_painter,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER] = hd_draw_cylinder,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE] = hd_draw_cube,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE] = hd_draw_sphere,
    },
};

static const struct ToriDraw_RasterKernelHD g_hd_branching_kernel = {
    .name = "branching",
    .vtable = &g_hd_branching_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

#else /* TORIDRAW_PIXEL16 */

static const struct ToriDraw_RasterKernelHD g_hd_branching_kernel = {
    .name = "branching",
    .vtable = &g_hd_pixel16_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

#endif /* TORIDRAW_PIXEL16 */

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetBranching(void)
{
    return &g_hd_branching_kernel;
}

#endif /* TORIDRAW_KERNELS_HD_BRANCHING_U_C */
