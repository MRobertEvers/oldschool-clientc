#ifndef TORIDRAW_FAMILIES_PROJECTION_PERSPECTIVE_PREPARED_U_C
#define TORIDRAW_FAMILIES_PROJECTION_PERSPECTIVE_PREPARED_U_C

/*
 * The perspective family with the prepared-camera kernels in front, and the
 * vtable that pairs it with the shared parallel family.
 *
 * The vtable lives here rather than in a file of its own because it is what
 * this family is *for*: the only thing it adds is the parallel slot, which
 * every vtable fills with the same pointer. kernels/projection.prepared.u.c
 * wraps it into the ToriDraw_ProjectionKernel a caller selects.
 */
static const struct ToriDraw_ProjectionFamily g_projection_family_perspective_prepared = {
    .name = "perspective/prepared",
    .near_clip = toridraw_projection_near_clip_perspective,
    .project = {
        [false] = toridraw_project_vertices_noclip_prepared,
        [true] = toridraw_project_vertices_clip_prepared,
    },
};

static const struct ToriDraw_ProjectionKernelVTable g_projection_prepared_vtable = {
    .perspective = &g_projection_family_perspective_prepared,
    .parallel = &g_projection_family_parallel,
};

#endif /* TORIDRAW_FAMILIES_PROJECTION_PERSPECTIVE_PREPARED_U_C */
