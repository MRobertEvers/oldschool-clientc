#ifndef TORIDRAW_GRAPHICS_PROJECTION_PREPARED_SSE2_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_PREPARED_SSE2_U_C

#include "proj_census.h"
#include "projection16_prepared.sse2.h"
#include "projection_prepared.h"
#include "toridraw_model_internal.h"

/*
 * The SSE2 prepared lane: the fused yaw-only family in
 * projection16_prepared.sse2.h, which is where the kernels and the reasoning
 * behind them live.
 *
 * Both families, unlike the AArch64 lane next door: this one has a clip kernel
 * as well as a noclip one, so nothing falls through to the portable ladder
 * except the models toridraw_proj_prepared_eligible already turned away.
 *
 * Reached only from the plain-SSE2 arm of the ladder in
 * projection16_simd.u.c, which is what defines TORIDRAW_SSE2_PREPARED_PROJECTION
 * -- and is therefore what this file is selected by. The SSE4.1 and AVX2 arms
 * above it have their own measured kernel shapes and no prepared family, so
 * those builds take projection_prepared.none.u.c.
 */

/* Nothing measured on this lane; the renderer's slot functions take the
 * compiler's own inlining decisions. */
#define TORIDRAW_PROJ_SLOT_ALWAYS_INLINE
#define TORIDRAW_PROJ_SLOT_NEVER_INLINE

static inline bool
toridraw_proj_prepared_clip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_yaw,
    int model_mid_z)
{
    int const num_vertices = model_vertex_count(hnd);

    if( model_has_textures(hnd) )
    {
        TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_TEX, 1, num_vertices);
        ToriDraw_ProjPreparedClip(
            scene,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            num_vertices,
            camera->yaw,
            model_yaw,
            model_mid_z,
            position);
    }
    else
    {
        TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_NOTEX, 1, num_vertices);
        ToriDraw_ProjPreparedNotexClip(
            scene,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            num_vertices,
            camera->yaw,
            model_yaw,
            model_mid_z,
            position);
    }

    /* The kernel bounded every full block; the sweep takes the tail. */
    scene->projection_bound_vertices = num_vertices & ~3;
    return true;
}

static inline bool
toridraw_proj_prepared_noclip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_yaw,
    int model_mid_z)
{
    int const num_vertices = model_vertex_count(hnd);

    if( model_has_textures(hnd) )
    {
        TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_TEX, 0, num_vertices);
        ToriDraw_ProjPreparedNoclip(
            scene,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            num_vertices,
            camera->yaw,
            model_yaw,
            model_mid_z,
            position);
    }
    else
    {
        TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_NOTEX, 0, num_vertices);
        ToriDraw_ProjPreparedNotexNoclip(
            scene,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            num_vertices,
            camera->yaw,
            model_yaw,
            model_mid_z,
            position);
    }

    /* The kernel bounded every full block; the sweep takes the tail. */
    scene->projection_bound_vertices = num_vertices & ~3;
    return true;
}

#endif /* TORIDRAW_GRAPHICS_PROJECTION_PREPARED_SSE2_U_C */
