#ifndef TORIDRAW_GRAPHICS_PROJECTION_PREPARED_NEON_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_PREPARED_NEON_U_C

#include "proj_census.h"
#include "projection16.aarch64.h"
#include "projection_prepared.h"
#include "toridraw_model_internal.h"

/*
 * The AArch64 prepared lane: the hand-written assembly in
 * projection16.aarch64.S, entered directly.
 *
 * NOCLIP ONLY. The .S implements the fused yaw-only noclip pair -- textured
 * and not -- and nothing else, so `clip` declines here and every near-clipped
 * model on this lane takes the portable ladder. Which means the prepared
 * vtable's PERSPECTIVE_CLIP slot IS the portable kernel on an Apple arm64
 * build. That is worth one line of code to say; before this file it was
 * visible only as the absence of a block in a function that had one for the
 * other family.
 *
 * The kernels read the camera out of the scene's prepared block, so `camera`
 * itself is never touched here -- toridraw_projection_prepared_eligible already
 * established that the block belongs to it.
 */

/* Measured on this lane: the prepared slot folds into ToriDraw_ProjectWithVTable's
 * call site, and the portable fallback stays out of line so its register
 * pressure does not price the hot path. */
#define TORIDRAW_PROJECTION_SLOT_ALWAYS_INLINE __attribute__((always_inline))
#define TORIDRAW_PROJECTION_SLOT_NEVER_INLINE __attribute__((noinline))

static inline bool
toridraw_projection_prepared_clip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_yaw,
    int model_mid_z)
{
    /* No clip entry point exists in projection16.aarch64.S. */
    return TORIDRAW_PROJECTION_PREPARED_DECLINE(
        scene, hnd, position, camera, model_yaw, model_mid_z);
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

    /* Under four vertices there is not one full block for either body to
     * take, so this is the portable ladder's model. */
    if( num_vertices < 4 )
        return false;

    if( model_has_textures(hnd) )
    {
        TORIDRAW_PROJECTION_CENSUS_RECORD(TORIDRAW_PROJECTION_K_YAW_TEX, 0, num_vertices);
        toridraw_project_vertices_fused_neon_noclip_native_prepared_aarch64(
            &scene->screen_vertices_x,
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
        toridraw_project_vertices_fused_neon_notex_noclip_native_prepared_aarch64(
            &scene->screen_vertices_x,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            num_vertices,
            model_yaw,
            model_mid_z,
            position);
    }

    /* The exact-four body (num_vertices == 4) does not touch the bound block
     * -- four outputs are one vector load each to sweep. The generic loop
     * covers every full block and leaves the tail to the sweep. */
    scene->projection_bound_vertices = num_vertices == 4 ? 0 : (num_vertices & ~3);
    return true;
}

#endif /* TORIDRAW_GRAPHICS_PROJECTION_PREPARED_NEON_U_C */
