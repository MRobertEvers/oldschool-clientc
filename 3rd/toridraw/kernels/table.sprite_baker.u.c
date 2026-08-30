#ifndef TORIDRAW_KERNELS_TABLE_SPRITE_BAKER_U_C
#define TORIDRAW_KERNELS_TABLE_SPRITE_BAKER_U_C

/*
 * Offline icon and sprite bakes.
 *
 * Software raster, but the PER-FACE branching kernel rather than the one with
 * the whole-model door. The baker draws a handful of small models once, off
 * the render path, and reads tmp_face_order and nothing else; the batched
 * walk's staging would be setup cost against no run length.
 *
 * The absence of the door is also what keeps the sort from stashing for it:
 * ToriDraw_KernelScratchNeeds asks for the presort buffers only when a raster
 * has a door to read them, so this table costs the sort nothing extra.
 */

static struct ToriDraw_Kernel g_kernel_sprite_baker = {
    .name = "sprite-baker",
    .raster = &g_stock_branching_perface_kernel,
};

const struct ToriDraw_Kernel*
ToriDraw_KernelGetSpriteBaker(void)
{
    toridraw_sd_kernel_publish(&g_stock_branching_perface_kernel);
    toridraw_kernel_table_publish(&g_kernel_sprite_baker);
    return &g_kernel_sprite_baker;
}

#endif /* TORIDRAW_KERNELS_TABLE_SPRITE_BAKER_U_C */
