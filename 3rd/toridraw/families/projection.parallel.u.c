#ifndef TORIDRAW_FAMILIES_PROJECTION_PARALLEL_U_C
#define TORIDRAW_FAMILIES_PROJECTION_PARALLEL_U_C

/*
 * The parallel (orthographic) projection family.
 *
 * One family, shared by both vtables rather than repeated in each. A parallel
 * camera has no prepared variant -- the prepared block is a perspective
 * cotangent and a yaw/pitch pair, and the orthographic kernels use neither --
 * so `prepared` and `portable` differ in the perspective family and in nothing
 * else, which is a fact about the two pointers instead of a comment asking you
 * to compare four slots.
 *
 * First of the three family files, because the other two name this one.
 */
static const struct ToriDraw_ProjectionFamily g_projection_family_parallel = {
    .name = "parallel",
    .near_clip = toridraw_projection_near_clip_parallel,
    .project = {
        [false] = toridraw_project_vertices_parallel_noclip,
        [true] = toridraw_project_vertices_parallel_clip,
    },
};

#endif /* TORIDRAW_FAMILIES_PROJECTION_PARALLEL_U_C */
