#ifndef TORIDRAW_GRAPHICS_PROJECTION_PREPARED_NEON32_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_PREPARED_NEON32_U_C

#include "census/proj_census.h"
#include "impl/projection/projection.perspective.prepared.dispatch.h"
#include "impl/projection/projection.perspective.prepared.neon32.impl.h"
#include "toridraw_model_internal.h"

/*
 * The A32 NEON prepared lane: the intrinsics kernels in
 * projection.perspective.prepared.neon32.impl.h, which is where the arithmetic
 * and the reasoning behind it live.
 *
 * BOTH FAMILIES, unlike the aarch64 lane next door: the near-clip half is
 * eleven vector ops that projection.zdiv.neon32.u.c already provides, so
 * nothing falls through to the portable ladder here except the models
 * toridraw_projection_prepared_eligible already turned away.
 *
 * WHICH BUILDS REACH IT. Every NEON build that is not the Apple aarch64
 * assembly one -- armv7 first and foremost, which had no prepared kernel at
 * all and took the portable ladder for every model in the game, but aarch64
 * Android and Linux too, where projection.perspective.prepared.aarch64.S is
 * not assembled. neon32 in the name is the A32 encoding, not a 32-bit-only
 * restriction: it runs on both (tools/kernel_names.py, ISA_CANON).
 *
 * The kernels read the camera out of the scene's prepared block, so `camera`
 * itself is never touched here -- toridraw_projection_prepared_eligible has
 * already established that the block belongs to it.
 */

/* Nothing measured on this lane. The AArch64 file's always_inline/noinline
 * pair was measured THERE, against that lane's assembly kernel, and copying
 * the attributes over would be copying its measurement with them. Empty until
 * somebody runs the A/B on armv7 hardware. */
#define TORIDRAW_PROJECTION_SLOT_ALWAYS_INLINE
#define TORIDRAW_PROJECTION_SLOT_NEVER_INLINE

static inline bool
toridraw_projection_prepared_clip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_yaw,
    int model_mid_z)
{
    int const num_vertices = model_vertex_count(hnd);

    (void)camera;

    if( model_has_textures(hnd) )
    {
        TORIDRAW_PROJECTION_CENSUS_RECORD(TORIDRAW_PROJECTION_K_YAW_TEX, 1, num_vertices);
        ToriDraw_ProjectionPreparedNeon32Clip(
            scene,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            num_vertices,
            model_yaw,
            model_mid_z,
            position);
    }
    else
    {
        TORIDRAW_PROJECTION_CENSUS_RECORD(TORIDRAW_PROJECTION_K_YAW_NOTEX, 1, num_vertices);
        ToriDraw_ProjectionPreparedNeon32NotexClip(
            scene,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            num_vertices,
            model_yaw,
            model_mid_z,
            position);
    }

    /* The kernel bounded every full block; the sweep takes the tail. Under
     * four vertices no block ran and the accumulators still hold their
     * INT_MAX/INT_MIN seed, so the count is zero and toridraw_projected_bound
     * ignores the block entirely. */
    scene->projection_bound_vertices = num_vertices & ~3;
    return true;
}

static inline bool
toridraw_projection_prepared_noclip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_yaw,
    int model_mid_z)
{
    int const num_vertices = model_vertex_count(hnd);

    (void)camera;

    if( model_has_textures(hnd) )
    {
        TORIDRAW_PROJECTION_CENSUS_RECORD(TORIDRAW_PROJECTION_K_YAW_TEX, 0, num_vertices);
        ToriDraw_ProjectionPreparedNeon32Noclip(
            scene,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            num_vertices,
            model_yaw,
            model_mid_z,
            position);
    }
    else
    {
        TORIDRAW_PROJECTION_CENSUS_RECORD(TORIDRAW_PROJECTION_K_YAW_NOTEX, 0, num_vertices);
        ToriDraw_ProjectionPreparedNeon32NotexNoclip(
            scene,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            num_vertices,
            model_yaw,
            model_mid_z,
            position);
    }

    /* The kernel bounded every full block; the sweep takes the tail. */
    scene->projection_bound_vertices = num_vertices & ~3;
    return true;
}

#endif /* TORIDRAW_GRAPHICS_PROJECTION_PREPARED_NEON32_U_C */
