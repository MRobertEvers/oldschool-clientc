#ifndef TORIDRAW_KERNELS_PROJECTION_STOCK_U_C
#define TORIDRAW_KERNELS_PROJECTION_STOCK_U_C

/*
 * The stock projection kernel: model space to screen space, plus the cull.
 *
 * A thin object over ToriDraw_Project. There is only one, because the axes
 * inside it -- rotation shape, textured/untextured, clip/noclip, prepared
 * camera, ISA lane -- are chosen per model from the geometry and the camera,
 * not by the caller. A replacement kernel would have to answer all of them
 * itself and honour the same contract: fill screen_vertices_*, fill
 * orthographic_vertices_* for a textured model, set near_clipped, leave the
 * screen box in scene->aabb, and return a TORIDRAW_CULL_* verdict.
 */

static int
projection_kernel_default(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    (void)user_data;
    return ToriDraw_Project(scene, hnd, position, view_port, camera);
}

static const struct ToriDraw_ProjectionKernel g_stock_projection_kernel = {
    .name = "projection",
    .project = projection_kernel_default,
};

const struct ToriDraw_ProjectionKernel*
ToriDraw_ProjectionKernelGetDefault(void)
{
    return &g_stock_projection_kernel;
}

#endif /* TORIDRAW_KERNELS_PROJECTION_STOCK_U_C */
