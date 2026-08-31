#ifndef TORIDRAW_KERNELS_SD_BRANCHING_PERFACE_U_C
#define TORIDRAW_KERNELS_SD_BRANCHING_PERFACE_U_C

/*
 * The stock branching kernel with no whole-model door.
 *
 * Same four callbacks, same pixels; the only difference is that this one draws
 * face by face even when the sort left a presorted stash behind. That makes it
 * the A/B baseline for the batched walk, in the same binary and with nothing
 * else different -- which TORIDRAW_RASTER_BATCH=0 could only approximate,
 * because turning the batcher off also stops the sort filling the stash and so
 * moves work out of stage 2 as well.
 *
 * Selecting this measures the raster half alone: the sort still stashes, and
 * the stores are still paid for; only the run doors go unused.
 */

static const struct ToriDraw_RasterKernelSDVTable g_stock_branching_perface_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_branching_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_branching_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_branching_textured_gouraud,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_branching_textured_flat,
    },
};

static struct ToriDraw_RasterKernelSD g_stock_branching_perface_kernel = {
    .name = "branching_perface",
    .draw_model = ToriDraw_RasterWalkPerFace,
    .vtable = &g_stock_branching_perface_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
    /* The same twin the branching kernel names: the depth family draws face by
     * face already, so the A/B difference this kernel exists for disappears
     * the moment the model wants depth. */
    .zbuffered_variant = &g_stock_sorted_zbuffered_kernel,
};

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetBranchingPerFace(void)
{
    return &g_stock_branching_perface_kernel;
}

#endif /* TORIDRAW_KERNELS_SD_BRANCHING_PERFACE_U_C */
