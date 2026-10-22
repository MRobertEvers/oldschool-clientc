#ifndef TORIDRAW_KERNELS_PROJECTION_RSZBUF_U_C
#define TORIDRAW_KERNELS_PROJECTION_RSZBUF_U_C

/*
 * The rev-241 float projection, as a stage-1 kernel.
 *
 * Not a variant of `prepared` / `portable`: those are one integer projection
 * two ways, and this is a different one -- float rotation, float divide,
 * fractional screen positions -- whose output the rszbuf raster reads from the
 * scene's TORIDRAW_SCENE_SCRATCH_RSZBUF group rather than from the integer
 * arrays alone. It has no family vtable to dispatch through; the one
 * implementation is scalar, because the client's arithmetic order is the
 * point. See impl/projection/projection.perspective.rszbuf.scalar.u.c.
 */

#include "impl/projection/projection.perspective.rszbuf.scalar.u.c"

static int
projection_kernel_rszbuf(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    (void)user_data;
    return toridraw_project_rszbuf(scene, hnd, position, view_port, camera);
}

static const struct ToriDraw_ProjectionKernel g_projection_rszbuf_kernel = {
    .name = "rszbuf",
    .project = projection_kernel_rszbuf,
};

const struct ToriDraw_ProjectionKernel*
ToriDraw_ProjectionKernelGetRsZbuf(void)
{
    return &g_projection_rszbuf_kernel;
}

#endif /* TORIDRAW_KERNELS_PROJECTION_RSZBUF_U_C */
