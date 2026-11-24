#ifndef TORIDRAW_KERNELS_TABLE_SOFTWARE_COMPAT_U_C
#define TORIDRAW_KERNELS_TABLE_SOFTWARE_COMPAT_U_C

/*
 * The compatibility software table: the stock front stages, and every face
 * one flat fill (sd.compat_flat.u.c). What ToriDraw_KernelGetStock hands a
 * software renderer while ToriDraw_RasterSetCompat is on.
 */

static struct ToriDraw_Kernel g_kernel_software_compat = {
    .name = "software-compat",
    .raster = &g_compat_flat_kernel,
};

const struct ToriDraw_Kernel*
ToriDraw_KernelGetSoftwareCompat(void)
{
    toridraw_kernel_table_publish(&g_kernel_software_compat);
    return &g_kernel_software_compat;
}

#endif /* TORIDRAW_KERNELS_TABLE_SOFTWARE_COMPAT_U_C */
