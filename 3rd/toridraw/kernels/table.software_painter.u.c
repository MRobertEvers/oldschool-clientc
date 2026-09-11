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
 * The projection and face-sort slots are filled by the getter, not left NULL
 * for a stage to resolve: this table follows TORIDRAW_FACE_SORT and the
 * prepared-camera path exactly as the client did before tables existed, but it
 * says so in the object it hands out.
 */

static struct ToriDraw_Kernel g_kernel_software_painter = {
    .name = "software-painter",
    .raster = &g_stock_branching_kernel,
};

const struct ToriDraw_Kernel*
ToriDraw_KernelGetSoftwarePainter(void)
{
    /* The raster slot too, not just the two front stages: whether this table
     * gets the whole-model door is TORIDRAW_RASTER_BATCH's answer, and it is
     * given HERE, once, where a renderer takes the table -- not per model,
     * inside the sort, as a second opinion about whether to fill the stash. */
    g_kernel_software_painter.raster = toridraw_stock_painter_kernel(false);
    toridraw_kernel_table_publish(&g_kernel_software_painter);
    return &g_kernel_software_painter;
}

#endif /* TORIDRAW_KERNELS_TABLE_SOFTWARE_PAINTER_U_C */
