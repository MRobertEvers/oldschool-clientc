#ifndef TORIDRAW_GRAPHICS_PROJECTION_ORTHO_NONE_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_ORTHO_NONE_U_C

#include "projection_ortho.h"

/*
 * No vector parallel projection in this build.
 *
 * A real lane and not a gap: it is what wasm and any scalar build compile, and
 * it is also the arrangement the other three lanes are correct RELATIVE TO --
 * every lane's vector body is followed by the same scalar tail, so a lane that
 * declines outright leaves the tail projecting the whole model, and that is the
 * reference answer. All four hooks return 0.
 */

/** Camera-space output, no near-plane test. */
static inline int
toridraw_ortho_lane_fused(
    const struct ToriDraw_OrthoFusedCamera* cam,
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices)
{
    return TORIDRAW_ORTHO_LANE_DECLINE(
        cam,
        orthographic_vertices_x,
        orthographic_vertices_y,
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices);
}

/** Camera-space output; marks a vertex behind near_plane_z for the face test to drop. */
static inline int
toridraw_ortho_lane_fused_clip(
    const struct ToriDraw_OrthoFusedCamera* cam,
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices)
{
    return TORIDRAW_ORTHO_LANE_DECLINE(
        cam,
        orthographic_vertices_x,
        orthographic_vertices_y,
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices);
}

/** Screen output only, no near-plane test. */
static inline int
toridraw_ortho_lane_fused_notex(
    const struct ToriDraw_OrthoFusedCamera* cam,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices)
{
    return TORIDRAW_ORTHO_LANE_DECLINE_NOTEX(
        cam,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices);
}

/** Screen output only; marks a vertex behind near_plane_z for the face test to drop. */
static inline int
toridraw_ortho_lane_fused_notex_clip(
    const struct ToriDraw_OrthoFusedCamera* cam,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices)
{
    return TORIDRAW_ORTHO_LANE_DECLINE_NOTEX(
        cam,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices);
}

#endif /* TORIDRAW_GRAPHICS_PROJECTION_ORTHO_NONE_U_C */
