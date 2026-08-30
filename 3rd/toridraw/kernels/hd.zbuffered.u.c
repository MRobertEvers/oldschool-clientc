#ifndef TORIDRAW_KERNELS_HD_ZBUFFERED_U_C
#define TORIDRAW_KERNELS_HD_ZBUFFERED_U_C

/*
 * HD depth-tested painter.
 *
 * A depth twin of every one of the six classes. NEEDS_ZBUFFER without
 * NEEDS_FACE_SORTING: the buffer resolves the faces, so no back-to-front order
 * is produced or consumed and stage 2 does not run.
 */

#ifndef TORIDRAW_PIXEL16

static const struct ToriDraw_RasterKernelHDVTable g_hd_z_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_HD_GOURAUD] = hd_draw_gouraud_z,
        [TORIDRAW_RASTER_FACE_HD_FLAT] = hd_draw_flat_z,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE] = hd_draw_plane_z,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER] = hd_draw_cylinder_z,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE] = hd_draw_cube_z,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE] = hd_draw_sphere_z,
    },
};

static const struct ToriDraw_RasterKernelHD g_hd_z_kernel = {
    .vtable = &g_hd_z_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

#else /* TORIDRAW_PIXEL16 */

static const struct ToriDraw_RasterKernelHD g_hd_z_kernel = {
    .vtable = &g_hd_pixel16_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

#endif /* TORIDRAW_PIXEL16 */

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetZBuffered(void)
{
    return &g_hd_z_kernel;
}

#endif /* TORIDRAW_KERNELS_HD_ZBUFFERED_U_C */
