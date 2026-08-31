#ifndef TORIDRAW_PROJECTION_PREPARED_NEON32_IMPL_H
#define TORIDRAW_PROJECTION_PREPARED_NEON32_IMPL_H
/*
 * The prepared-camera model-yaw family in A32 NEON intrinsics -- the armv7
 * answer to projection.perspective.prepared.aarch64.S, and the third lane of
 * the family described in projection.perspective.prepared.dispatch.h.
 *
 * WHY THIS LANE EXISTS
 *
 * Until now a prepared kernel was aarch64-or-x86: the hand-written assembly
 * (noclip only) or the SSE2 fused-yaw pair. armv7 had neither, so every
 * yaw-only model on the oldest hardware in the tree -- the hardware least able
 * to afford it -- paid the full per-call camera setup that the census
 * (census/proj_census.h) measures at 50-57% of a four-vertex call. Nothing
 * about the prepared block is 64-bit; the gap was that nobody had written the
 * A32 body.
 *
 * BOTH FAMILIES, clip and noclip, unlike the aarch64 lane next door. The
 * near-clip half is eleven vector ops (projection_zdiv_neon_apply_clip), it is
 * already written, and declining it would send every near-clipped model back
 * down the portable ladder for no reason but the absence of an entry point.
 *
 * WHY THIS IS NOT THE SSE2 KERNEL TRANSLATED
 *
 * projection.perspective.prepared.sse2.impl.h FUSES the two yaw rotations into
 * one lookup at (model_yaw + camera_yaw) & 2047 and rounds once where the
 * reference rounds twice. That is a deliberate, documented trade on x86 -- it
 * buys a pmaddwd per vertex and pays |d| <= 4 screen pixels against the
 * portable ladder, plus a residue correction to centre the error.
 *
 * This lane declines the trade, for a reason that is local to it: on ARM the
 * portable ladder IS a NEON kernel (projection.perspective.plain.neon32.u.c),
 * the two-rotation body is already four vmulq_s32 and two vshrq_n_s32 per
 * block, and SSE2's motive for fusing -- that a 32x32 multiply costs six
 * instructions to emulate on that floor -- simply does not apply where VMUL.I32
 * exists. So the arithmetic here is the portable neon32 kernel's, floor for
 * floor, and what `prepared` buys is the part that was always pure overhead:
 *
 *   - six trig table reads and the cot16 ladder, per call, for values the
 *     frame fixed once. They are read out of the prepared block instead, and
 *     each is stored pre-splatted, so a constant is one vld1q_s32 rather than
 *     a load plus a vdupq.
 *   - nineteen stack arguments (this is a 32-bit ABI: four registers, then
 *     memory) cut to nine, most of them pointers the caller already held.
 *   - the screen box, accumulated in-register off the coordinates as they are
 *     born, instead of a second pass that reads every output back.
 *
 * The consequence worth stating outright: this kernel is BIT-EXACT with the
 * portable ladder on this lane, which is what toridraw_projection_kernel_test
 * demands of a prepared kernel and what the aarch64 assembly also delivers.
 * The exactness is not incidental -- it is why the divide below takes exactly
 * ONE Newton step. vrecpeq_f32 is an eight-bit estimate and two vrecpsq_f32
 * steps would be the more accurate answer, but projection.zdiv.neon32.u.c
 * takes one, so one is the answer the portable ladder produces and two would
 * be a divergence dressed up as an improvement. The shared body is included
 * rather than copied so the two cannot drift.
 *
 * THE 4-WIDE BLOCK AND THE TAIL
 *
 * Identical to the portable kernel again: the vector loop takes whole blocks
 * of four and the remainder runs scalar with an integer divide -- not the
 * reciprocal the lanes got. That is the portable lane's choice and this one
 * inherits it; a tail that divided the other way would disagree with its own
 * vector body by a pixel on the last three vertices of an odd-sized model.
 */

#include <arm_neon.h>
#include <assert.h>
#include <limits.h>

#include "graphics/dash_vertexint.h"
#include "graphics/shared_tables.h"
#include "impl/projection/projection.scalar_reference.h"
#include "impl/projection/zdiv/projection.zdiv.neon32.u.c"
#include "toridraw_types.h"

/*
 * One projection body, four shapes.
 *
 * want_yaw, want_ortho and want_clip are compile-time constants at every call
 * site and the function is always_inline, so each `if` below folds away and
 * each entry point gets the straight-line body it asked for. Written once and
 * specialized by the compiler, rather than the eight hand-copied loops the
 * portable neon32 file carries for the same eight shapes.
 *
 * want_yaw is a separate body and not "yaw zero is just cos 65536, sin 0":
 * that identity is exact -- (x * 65536) >> 16 == x for every int16 x, and
 * -32768 * 65536 lands on INT_MIN rather than past it -- but it is four
 * multiplies, two shifts and an add per block spent proving it, on the terrain
 * tiles that are most of the call count.
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
static inline void
toridraw_projection_prepared_neon32_core(
    const struct ToriDraw_ProjectionPreparedCamera* prep,
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int want_yaw,
    int want_ortho,
    int want_clip,
    int* bound_out)
{
    assert(prep);
    assert(screen_vertices_x);
    assert(screen_vertices_y);
    assert(screen_vertices_z);
    assert(vertex_x);
    assert(vertex_y);
    assert(vertex_z);
    assert(bound_out);
    assert(model_yaw >= 0);
    assert(model_yaw < 2048);

    /*
     * Read, not derived. Each member is the same value in all four lanes --
     * ToriDraw_ScenePrepareProjectionCamera writes it that way, and
     * projection.perspective.prepared.aarch64.S reads the block with paired
     * loads for the same reason -- so the splat a per-call kernel builds with
     * a table read plus a vdupq is one aligned vector load here.
     *
     * The values are bit-identical to ToriDraw_ReadCosTable(camera->yaw) and
     * friends, which is what the portable kernel calls: the prepared block is
     * built from those same two tables, and cot15 from the same
     * toridraw_projection_cot16() >> 1.
     */
    int32x4_t const c_yaw = vld1q_s32(prep->cos_yaw);
    int32x4_t const s_yaw = vld1q_s32(prep->sin_yaw);
    int32x4_t const c_pitch = vld1q_s32(prep->cos_pitch);
    int32x4_t const s_pitch = vld1q_s32(prep->sin_pitch);
    int32x4_t const cot_v = vld1q_s32(prep->cot15);

    /* g_projection_model_yaw_table is the interleaved {cos, sin} pair table --
     * both halves on one cache line, and the same numbers g_cos_table and
     * g_sin_table hold, refreshed together whenever either is reselected. */
    int const cos_model_yaw = g_projection_model_yaw_table[model_yaw][0];
    int const sin_model_yaw = g_projection_model_yaw_table[model_yaw][1];
    int32x4_t const c_my = vdupq_n_s32(cos_model_yaw);
    int32x4_t const s_my = vdupq_n_s32(sin_model_yaw);

    int32x4_t const v_scene_x = vdupq_n_s32(scene_x);
    int32x4_t const v_scene_y = vdupq_n_s32(scene_y);
    int32x4_t const v_scene_z = vdupq_n_s32(scene_z);
    int32x4_t const v_near = vdupq_n_s32(near_plane_z);
    int32x4_t const v_mid = vdupq_n_s32(model_mid_z);
    int32x4_t const v_neg5000 = vdupq_n_s32(TORIDRAW_SCREEN_X_NEAR_CLIPPED);
    int32x4_t const v_neg5001 = vdupq_n_s32(TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE);

    int const cos_camera_yaw = prep->cos_yaw[0];
    int const sin_camera_yaw = prep->sin_yaw[0];
    int const cos_camera_pitch = prep->cos_pitch[0];
    int const sin_camera_pitch = prep->sin_pitch[0];
    int const cot_fov_half_ish15 = prep->cot15[0];

    /*
     * The screen box, accumulated where the screen coordinates are born --
     * the same four vector ops per block projection.perspective.prepared
     * .aarch64.S keeps in v8-v11. VMIN.S32 and VMAX.S32 are A32 instructions,
     * so unlike the SSE2 lane this needs no compare-and-select emulation.
     * Seeded wide: at least one block runs whenever the caller publishes a
     * count (num_vertices >= 4 is the caller's gate), so the seed is always
     * superseded.
     */
    int32x4_t b_min_x = vdupq_n_s32(INT_MAX);
    int32x4_t b_max_x = vdupq_n_s32(INT_MIN);
    int32x4_t b_min_y = vdupq_n_s32(INT_MAX);
    int32x4_t b_max_y = vdupq_n_s32(INT_MIN);

    int i = 0;

    for( ; i + 4 <= num_vertices; i += 4 )
    {
        int32x4_t const xv = vmovl_s16(vld1_s16(&vertex_x[i]));
        int32x4_t const yv = vmovl_s16(vld1_s16(&vertex_y[i]));
        int32x4_t const zv = vmovl_s16(vld1_s16(&vertex_z[i]));

        int32x4_t x_rotated = xv;
        int32x4_t z_rotated = zv;
        int32x4_t y_rotated;
        int32x4_t x_scene;
        int32x4_t z_scene;
        int32x4_t y_scene;
        int32x4_t z_final;
        int32x4_t x_scaled;
        int32x4_t y_scaled;
        int32x4_t vscreen_z;
        int32x4_t final_x;
        int32x4_t final_y;

        if( want_yaw )
        {
            x_rotated = vshrq_n_s32(
                vaddq_s32(vmulq_s32(xv, c_my), vmulq_s32(zv, s_my)), 16);
            z_rotated = vshrq_n_s32(
                vsubq_s32(vmulq_s32(zv, c_my), vmulq_s32(xv, s_my)), 16);
        }

        x_rotated = vaddq_s32(x_rotated, v_scene_x);
        y_rotated = vaddq_s32(yv, v_scene_y);
        z_rotated = vaddq_s32(z_rotated, v_scene_z);

        x_scene = vshrq_n_s32(
            vaddq_s32(vmulq_s32(x_rotated, c_yaw), vmulq_s32(z_rotated, s_yaw)), 16);
        z_scene = vshrq_n_s32(
            vsubq_s32(vmulq_s32(z_rotated, c_yaw), vmulq_s32(x_rotated, s_yaw)), 16);

        y_scene = vshrq_n_s32(
            vsubq_s32(vmulq_s32(y_rotated, c_pitch), vmulq_s32(z_scene, s_pitch)), 16);
        z_final = vshrq_n_s32(
            vaddq_s32(vmulq_s32(y_rotated, s_pitch), vmulq_s32(z_scene, c_pitch)), 16);

        if( want_ortho )
        {
            vst1q_s32(&orthographic_vertices_x[i], x_scene);
            vst1q_s32(&orthographic_vertices_y[i], y_scene);
            vst1q_s32(&orthographic_vertices_z[i], z_final);
        }

        x_scaled = vshrq_n_s32(vmulq_s32(x_scene, cot_v), 6);
        y_scaled = vshrq_n_s32(vmulq_s32(y_scene, cot_v), 6);

        /* The shared body, included rather than reproduced: the near-plane
         * mask, the reciprocal and its single Newton step, the truncating
         * convert and the -5000/-5001 sentinel all live in
         * projection.zdiv.neon32.u.c, and the portable ladder calls the same
         * two functions. That is what makes the two kernels bit-identical
         * rather than merely intended to be. */
        if( want_clip )
            projection_zdiv_neon_apply_clip(
                z_final, x_scaled, y_scaled, v_near, v_mid, v_neg5000, v_neg5001,
                &vscreen_z, &final_x, &final_y);
        else
            projection_zdiv_neon_apply_noclip(
                z_final, x_scaled, y_scaled, v_near, v_mid, v_neg5000, v_neg5001,
                &vscreen_z, &final_x, &final_y);

        vst1q_s32(&screen_vertices_z[i], vscreen_z);
        vst1q_s32(&screen_vertices_x[i], final_x);
        vst1q_s32(&screen_vertices_y[i], final_y);

        b_min_x = vminq_s32(b_min_x, final_x);
        b_max_x = vmaxq_s32(b_max_x, final_x);
        b_min_y = vminq_s32(b_min_y, final_y);
        b_max_y = vmaxq_s32(b_max_y, final_y);
    }

    vst1q_s32(bound_out + 0, b_min_x);
    vst1q_s32(bound_out + 4, b_max_x);
    vst1q_s32(bound_out + 8, b_min_y);
    vst1q_s32(bound_out + 12, b_max_y);

    /* Four-vertex models are 59% of all projection calls and never reach the
     * tail; give the common case an exit before the tail's own setup. */
    if( i == num_vertices )
        return;

    /*
     * Scalar tail, lane for lane with the vector body above -- except the
     * divide, which is an integer divide here and a reciprocal there. That is
     * the portable neon32 kernel's split and this one copies it deliberately:
     * matching the portable ladder is the contract, and the ladder's own tail
     * divides exactly this way.
     */
    for( ; i < num_vertices; i++ )
    {
        int const x = vertex_x[i];
        int const y = vertex_y[i];
        int const z = vertex_z[i];

        int x_rotated = x;
        int z_rotated = z;
        int y_rotated;
        int x_scene;
        int z_scene;
        int y_scene;
        int z_final_scene;
        int screen_x;
        int screen_y;

        if( want_yaw )
        {
            x_rotated = ( x * cos_model_yaw + z * sin_model_yaw ) >> 16;
            z_rotated = ( z * cos_model_yaw - x * sin_model_yaw ) >> 16;
        }

        x_rotated += scene_x;
        y_rotated = y + scene_y;
        z_rotated += scene_z;

        x_scene = ( x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw ) >> 16;
        z_scene = ( z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw ) >> 16;

        y_scene = ( y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch ) >> 16;
        z_final_scene = ( y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch ) >> 16;

        if( want_ortho )
        {
            orthographic_vertices_x[i] = x_scene;
            orthographic_vertices_y[i] = y_scene;
            orthographic_vertices_z[i] = z_final_scene;
        }

        screen_x = ( x_scene * cot_fov_half_ish15 ) >> 6;
        screen_y = ( y_scene * cot_fov_half_ish15 ) >> 6;

        screen_vertices_z[i] = z_final_scene - model_mid_z;

        if( want_clip && z_final_scene < near_plane_z )
        {
            screen_vertices_x[i] = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
            screen_vertices_y[i] = screen_y;
        }
        else
        {
            screen_vertices_x[i] = screen_x / z_final_scene;
            if( want_clip &&
                screen_vertices_x[i] == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
                screen_vertices_x[i] = TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE;
            screen_vertices_y[i] = screen_y / z_final_scene;
        }
    }
}

/*
 * The four entry points, matching the SSE2 lane's shape: each reads the six
 * output arrays and the prepared camera straight out of the scene, so the call
 * site hands over pointers it is already holding rather than pushing nineteen
 * values onto a 32-bit stack frame.
 *
 * The caller owns the "is the prepared camera the one I mean?" question --
 * toridraw_projection_prepared_eligible -- so these assert that a block was
 * published rather than re-deriving it.
 *
 * The argument list is a macro because it is the same nineteen values four
 * times over, differing only in the three specialization flags and the near
 * plane. Written out per entry point it was the kind of list where a
 * transposed pair of scene arrays reads as correct.
 *
 * The untextured pair still passes the orthographic arrays: want_ortho == 0
 * means the body never touches them, and handing the real pointers over keeps
 * a NULL out of a parameter that is a contract violation everywhere else.
 */
#define TORIDRAW_PREPARED_NEON32_ARGS(want_yaw_, want_ortho_, want_clip_, near_) \
    &scene->projection_prepared_camera, scene->orthographic_vertices_x,         \
        scene->orthographic_vertices_y, scene->orthographic_vertices_z,         \
        scene->screen_vertices_x, scene->screen_vertices_y,                     \
        scene->screen_vertices_z, vertex_x, vertex_y, vertex_z, num_vertices,   \
        model_yaw, model_mid_z, position->x, position->y, position->z, (near_), \
        (want_yaw_), (want_ortho_), (want_clip_), &scene->projection_bound[0][0]

/* ------------------------------------------------- textured (six outputs) */

static void
ToriDraw_ProjectionPreparedNeon32Noclip(
    struct ToriDraw_Scene* scene,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    const struct ToriDraw_Position* position)
{
    assert(scene);
    assert(position);
    assert(scene->projection_prepared_camera_source);

    if( model_yaw != 0 )
        toridraw_projection_prepared_neon32_core(
            TORIDRAW_PREPARED_NEON32_ARGS(1, 1, 0, 0));
    else
        toridraw_projection_prepared_neon32_core(
            TORIDRAW_PREPARED_NEON32_ARGS(0, 1, 0, 0));
}

static void
ToriDraw_ProjectionPreparedNeon32Clip(
    struct ToriDraw_Scene* scene,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    const struct ToriDraw_Position* position)
{
    assert(scene);
    assert(position);
    assert(scene->projection_prepared_camera_source);

    if( model_yaw != 0 )
        toridraw_projection_prepared_neon32_core(TORIDRAW_PREPARED_NEON32_ARGS(
            1, 1, 1, scene->projection_near_plane_z));
    else
        toridraw_projection_prepared_neon32_core(TORIDRAW_PREPARED_NEON32_ARGS(
            0, 1, 1, scene->projection_near_plane_z));
}

/* ------------------------------------------------ untextured (screen only) */

static void
ToriDraw_ProjectionPreparedNeon32NotexNoclip(
    struct ToriDraw_Scene* scene,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    const struct ToriDraw_Position* position)
{
    assert(scene);
    assert(position);
    assert(scene->projection_prepared_camera_source);

    if( model_yaw != 0 )
        toridraw_projection_prepared_neon32_core(
            TORIDRAW_PREPARED_NEON32_ARGS(1, 0, 0, 0));
    else
        toridraw_projection_prepared_neon32_core(
            TORIDRAW_PREPARED_NEON32_ARGS(0, 0, 0, 0));
}

static void
ToriDraw_ProjectionPreparedNeon32NotexClip(
    struct ToriDraw_Scene* scene,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    const struct ToriDraw_Position* position)
{
    assert(scene);
    assert(position);
    assert(scene->projection_prepared_camera_source);

    if( model_yaw != 0 )
        toridraw_projection_prepared_neon32_core(TORIDRAW_PREPARED_NEON32_ARGS(
            1, 0, 1, scene->projection_near_plane_z));
    else
        toridraw_projection_prepared_neon32_core(TORIDRAW_PREPARED_NEON32_ARGS(
            0, 0, 1, scene->projection_near_plane_z));
}

#undef TORIDRAW_PREPARED_NEON32_ARGS

#endif /* TORIDRAW_PROJECTION_PREPARED_NEON32_IMPL_H */
