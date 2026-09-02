#ifndef TORIDRAW_GRAPHICS_PROJECTION_PREPARED_NEON32_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_PREPARED_NEON32_U_C

#include "census/proj_census.h"
#include "impl/projection/projection.perspective.prepared.dispatch.h"
#include "impl/projection/projection.perspective.prepared.neon32.impl.h"
#include "toridraw_model_internal.h"

#include <stdlib.h>

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
 *
 * THIS FILE IS THE DISPATCHER, AND ONLY THAT. The kernels are one noinline
 * function per shape (see ONE FUNCTION PER SHAPE in the impl header); what
 * happens here is one read of the model and the choice of which to call.
 * The handle is resolved ONCE -- every model kind puts its ToriDraw_Model at
 * offset zero, so ToriDraw_ModelRead is the same pointer whichever kind it
 * is, and the four accessor switches this used to run per model were four
 * ways of asking the same question.
 */

/* The slot is a dispatcher now and cheap to inline into
 * ToriDraw_ProjectWithVTable; the portable fallback stays out of line so its
 * nineteen-argument calls do not price the hot path. */
#define TORIDRAW_PROJECTION_SLOT_ALWAYS_INLINE __attribute__((always_inline))
#define TORIDRAW_PROJECTION_SLOT_NEVER_INLINE __attribute__((noinline))

/*
 * TORIDRAW_PROJ_TILE4: route exactly-four-vertex models (terrain tiles, 57%
 * of everything projected) to the loop-free tile kernels. Default on;
 * TORIDRAW_PROJ_TILE4=0 sends them through the generic body for an A/B.
 * Read once and cached, the same shape as every other knob in the tree.
 */
static inline int
toridraw_projection_tile4_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
    {
        const char* v = getenv("TORIDRAW_PROJ_TILE4");
        armed = (v && v[0] == '0') ? 0 : 1;
    }
    return armed;
}

static inline bool
toridraw_projection_prepared_clip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_yaw,
    int model_mid_z)
{
    const struct ToriDraw_Model* const model = ToriDraw_ModelRead(hnd);
    int const num_vertices = model->vertex_count;
    bool const textured = model->textured_face_count > 0;

    (void)camera;

    if( textured )
    {
        TORIDRAW_PROJECTION_CENSUS_RECORD(TORIDRAW_PROJECTION_K_YAW_TEX, 1, num_vertices);
        if( model_yaw != 0 )
            toridraw_projection_prepared_neon32_tex_yaw_clip(
                scene, model->vertices_x, model->vertices_y, model->vertices_z, num_vertices,
                model_yaw, model_mid_z, position);
        else
            toridraw_projection_prepared_neon32_tex_noyaw_clip(
                scene, model->vertices_x, model->vertices_y, model->vertices_z, num_vertices,
                model_yaw, model_mid_z, position);
    }
    else
    {
        TORIDRAW_PROJECTION_CENSUS_RECORD(TORIDRAW_PROJECTION_K_YAW_NOTEX, 1, num_vertices);
        if( model_yaw != 0 )
            toridraw_projection_prepared_neon32_notex_yaw_clip(
                scene, model->vertices_x, model->vertices_y, model->vertices_z, num_vertices,
                model_yaw, model_mid_z, position);
        else
            toridraw_projection_prepared_neon32_notex_noyaw_clip(
                scene, model->vertices_x, model->vertices_y, model->vertices_z, num_vertices,
                model_yaw, model_mid_z, position);
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
    const struct ToriDraw_Model* const model = ToriDraw_ModelRead(hnd);
    int const num_vertices = model->vertex_count;
    bool const textured = model->textured_face_count > 0;

    (void)camera;

    if( num_vertices == 4 && toridraw_projection_tile4_armed() )
    {
        /* The exact-four kernels: one block, no loop, no tail, and the block's
         * outputs written to the bound slots as both min and max. Every full
         * block -- which is to say the one -- is bounded. */
        if( textured )
        {
            TORIDRAW_PROJECTION_CENSUS_RECORD(TORIDRAW_PROJECTION_K_YAW_TEX, 0, num_vertices);
            if( model_yaw != 0 )
                toridraw_projection_prepared_neon32_tile4_tex_yaw(
                    scene, model->vertices_x, model->vertices_y, model->vertices_z, 4,
                    model_yaw, model_mid_z, position);
            else
                toridraw_projection_prepared_neon32_tile4_tex_noyaw(
                    scene, model->vertices_x, model->vertices_y, model->vertices_z, 4,
                    model_yaw, model_mid_z, position);
        }
        else
        {
            TORIDRAW_PROJECTION_CENSUS_RECORD(TORIDRAW_PROJECTION_K_YAW_NOTEX, 0, num_vertices);
            if( model_yaw != 0 )
                toridraw_projection_prepared_neon32_tile4_notex_yaw(
                    scene, model->vertices_x, model->vertices_y, model->vertices_z, 4,
                    model_yaw, model_mid_z, position);
            else
                toridraw_projection_prepared_neon32_tile4_notex_noyaw(
                    scene, model->vertices_x, model->vertices_y, model->vertices_z, 4,
                    model_yaw, model_mid_z, position);
        }
        scene->projection_bound_vertices = 4;
        return true;
    }

    if( textured )
    {
        TORIDRAW_PROJECTION_CENSUS_RECORD(TORIDRAW_PROJECTION_K_YAW_TEX, 0, num_vertices);
        if( model_yaw != 0 )
            toridraw_projection_prepared_neon32_tex_yaw_noclip(
                scene, model->vertices_x, model->vertices_y, model->vertices_z, num_vertices,
                model_yaw, model_mid_z, position);
        else
            toridraw_projection_prepared_neon32_tex_noyaw_noclip(
                scene, model->vertices_x, model->vertices_y, model->vertices_z, num_vertices,
                model_yaw, model_mid_z, position);
    }
    else
    {
        TORIDRAW_PROJECTION_CENSUS_RECORD(TORIDRAW_PROJECTION_K_YAW_NOTEX, 0, num_vertices);
        if( model_yaw != 0 )
            toridraw_projection_prepared_neon32_notex_yaw_noclip(
                scene, model->vertices_x, model->vertices_y, model->vertices_z, num_vertices,
                model_yaw, model_mid_z, position);
        else
            toridraw_projection_prepared_neon32_notex_noyaw_noclip(
                scene, model->vertices_x, model->vertices_y, model->vertices_z, num_vertices,
                model_yaw, model_mid_z, position);
    }

    /* The kernel bounded every full block; the sweep takes the tail. */
    scene->projection_bound_vertices = num_vertices & ~3;
    return true;
}

#endif /* TORIDRAW_GRAPHICS_PROJECTION_PREPARED_NEON32_U_C */
