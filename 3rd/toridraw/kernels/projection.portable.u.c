#ifndef TORIDRAW_KERNELS_PROJECTION_PORTABLE_U_C
#define TORIDRAW_KERNELS_PROJECTION_PORTABLE_U_C

/*
 * Projection without the prepared-camera kernels.
 *
 * Every model takes the portable SIMD/scalar ladder in
 * graphics/projection16_simd.u.c, whatever the camera has published. Same
 * pixels as `prepared` -- the prepared kernels are an optimisation of the
 * yaw-only case, not a different projection -- so the two are an A/B, and
 * this is the baseline arm.
 *
 * Until now that A/B was a rebuild: the prepared families are compiled in by
 * TORIDRAW_APPLE_NEON_PROJECTION_ASM and TORIDRAW_SSE2_PREPARED_PROJECTION,
 * and the only way to measure without them was to build a second binary whose
 * every other property also differed. Selecting this kernel measures the same
 * binary.
 *
 * Also the honest choice for a caller that never publishes a prepared camera:
 * the prepared kernel would test the camera pointer once per model and fall
 * through every time.
 */

static int
projection_kernel_portable(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    (void)user_data;
    return ToriDraw_ProjectWithVTable(
        scene, hnd, position, view_port, camera, &g_projection_portable_vtable);
}

static const struct ToriDraw_ProjectionKernel g_projection_portable_kernel = {
    .name = "portable",
    .project = projection_kernel_portable,
    .vtable = &g_projection_portable_vtable,
};

const struct ToriDraw_ProjectionKernel*
ToriDraw_ProjectionKernelGetPortable(void)
{
    return &g_projection_portable_kernel;
}

#endif /* TORIDRAW_KERNELS_PROJECTION_PORTABLE_U_C */
