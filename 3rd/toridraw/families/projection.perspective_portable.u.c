#ifndef TORIDRAW_FAMILIES_PROJECTION_PERSPECTIVE_PORTABLE_U_C
#define TORIDRAW_FAMILIES_PROJECTION_PERSPECTIVE_PORTABLE_U_C

/*
 * The perspective family without the prepared-camera kernels, and the vtable
 * that pairs it with the shared parallel family.
 *
 * Same pixels as the prepared family -- the prepared kernels are an
 * optimisation of the yaw-only case, not a different projection -- so this is
 * the A/B baseline. kernels/projection.portable.u.c wraps it into the
 * ToriDraw_ProjectionKernel a caller selects.
 */
static const struct ToriDraw_ProjectionFamily g_projection_family_perspective_portable = {
    .name = "perspective/portable",
    .near_clip = toridraw_projection_near_clip_perspective,
    .project = {
        [false] = toridraw_project_vertices_noclip_portable,
        [true] = toridraw_project_vertices_clip_portable,
    },
};

static const struct ToriDraw_ProjectionKernelVTable g_projection_portable_vtable = {
    .perspective = &g_projection_family_perspective_portable,
    .parallel = &g_projection_family_parallel,
};

#endif /* TORIDRAW_FAMILIES_PROJECTION_PERSPECTIVE_PORTABLE_U_C */
