#ifndef TORIDRAW_KERNELS_PROJECTION_PREPARED_U_C
#define TORIDRAW_KERNELS_PROJECTION_PREPARED_U_C

/*
 * Projection with the prepared-camera kernels in front. The default.
 *
 * A yaw-only model, drawn under a camera whose prepared block was published
 * with ToriDraw_ScenePrepareProjectionCamera, takes the hand-written
 * prepared-camera kernel: the AArch64 assembly where this lane builds it, the
 * SSE2 fused-yaw pair otherwise. Everything else -- pitched, rolled, a camera
 * with no published block, and every parallel camera -- falls through to the
 * portable ladder, which is why this is a superset of `portable` rather than
 * an alternative to it.
 *
 * The two vtables differ in exactly the two perspective slots; see
 * g_projection_prepared_vtable in toridraw_render.u.c.
 */

static int
projection_kernel_prepared(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    (void)user_data;
    return ToriDraw_ProjectWithVTable(
        scene, hnd, position, view_port, camera, &g_projection_prepared_vtable);
}

static const struct ToriDraw_ProjectionKernel g_projection_prepared_kernel = {
    .name = "prepared",
    .project = projection_kernel_prepared,
    .vtable = &g_projection_prepared_vtable,
};

const struct ToriDraw_ProjectionKernel*
ToriDraw_ProjectionKernelGetPrepared(void)
{
    return &g_projection_prepared_kernel;
}

const struct ToriDraw_ProjectionKernel*
ToriDraw_ProjectionKernelGetDefault(void)
{
    return &g_projection_prepared_kernel;
}

#endif /* TORIDRAW_KERNELS_PROJECTION_PREPARED_U_C */
