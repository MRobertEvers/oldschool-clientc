#ifndef TORIDRAW_KERNELS_TABLE_SOFTWARE_PAINTER_U_C
#define TORIDRAW_KERNELS_TABLE_SOFTWARE_PAINTER_U_C

/*
 * The world painter: the fastest software path, and the only table that can
 * reach the batched walk.
 *
 * Branching raster WITH its whole-model door, so a run of presorted faces
 * goes to the assembly kernels in one call. Three other things have to line up
 * for that -- a sort that stashes, a small scene to stash into, and a build
 * with the presorted-run assembly -- and ToriDraw_KernelValidate reports
 * DEGRADED rather than failing when they do not. The pixels are the same
 * either way; only the speed differs.
 *
 * NULL projection and face_sort mean the stock defaults, so this table follows
 * TORIDRAW_FACE_SORT and the prepared-camera path exactly as the client did
 * before tables existed.
 */

static const struct ToriDraw_Kernel g_kernel_software_painter = {
    .name = "software-painter",
    .projection = NULL,
    .face_sort = NULL,
    .raster = &g_stock_branching_kernel,
};

const struct ToriDraw_Kernel*
ToriDraw_KernelGetSoftwarePainter(void)
{
    return &g_kernel_software_painter;
}

#endif /* TORIDRAW_KERNELS_TABLE_SOFTWARE_PAINTER_U_C */
