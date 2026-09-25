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
 */

#include "../toridraw_raster_kernel.h"


/* The six face callbacks are defined by toridraw_render_hd.u.c, which includes
 * this file once they exist -- this table is a fragment of that file, not a
 * translation unit. Redeclaring them here costs nothing in the unity build (a
 * static declaration after the definition), states what the table is made of,
 * and lets an editor parsing this file on its own resolve the slots. A
 * signature that drifts from the definition fails the build here. */
static void hd_branching_gouraud(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face);
static void hd_branching_flat(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face);
static void hd_draw_plane_painter(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face);
static void hd_draw_cylinder(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face);
static void hd_draw_cube(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face);
static void hd_draw_sphere(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face);

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


const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetBranching(void)
{
    return &g_hd_branching_kernel;
}

#endif /* TORIDRAW_KERNELS_HD_BRANCHING_U_C */
