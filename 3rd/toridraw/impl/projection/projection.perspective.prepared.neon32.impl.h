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
 *
 * ONE FUNCTION PER SHAPE, AND WHY (2026-09-01)
 *
 * The first cut of this file had the four noclip shapes (tex/notex x
 * yaw/noyaw) as `if`s inside one always_inline core, called from one entry
 * point per family. clang did exactly what that asks: it inlined all four
 * bodies into the renderer's slot function and gave them a UNION frame --
 * 440 bytes, push of nine GPRs plus vpush d8-d15, 251 [sp] references, and
 * five constant vectors vdup'd from GPRs, vst1'd to that frame and reloaded
 * four to seven times per block because the four bodies together needed more
 * than the sixteen Q registers. Every model paid that prologue, including the
 * ones the eligibility test then turned away (the test ran AFTER it).
 *
 * So each shape is now its own `noinline` function with its own frame. The
 * slot is a dispatcher: eligibility, one model read, and a direct call to the
 * body it wants -- a declined model tail-calls the portable ladder with no
 * prologue spent. The bodies themselves are the same arithmetic, still
 * generated from one core by the compiler; the difference is where the
 * function boundary sits.
 *
 * THE CONSTANTS ARE READ WHERE THEY ARE USED
 *
 * The prepared block is one L1-hot cache line and a half. Hoisting its five
 * vectors into registers ahead of the loop is what starved the allocator: it
 * then spilled them to the frame and reloaded from there, which is the same
 * load with a store in front of it. With TORIDRAW_PROJ_PREP_POINT_OF_USE (the
 * default) the loop reads each constant from the block at the multiply that
 * consumes it; the stores to the output arrays may alias the block as far as
 * the compiler knows, so it cannot hoist them back. Set it to 0 at compile
 * time to get the hoisted form for an A/B.
 *
 * THE EXACT-FOUR KERNEL
 *
 * 57% of projected models are four-vertex terrain tiles. For those the loop
 * runs once, the bound seed and fold are pure overhead (the block's own
 * final_x / final_y ARE the bound), and the tail's exit test is a branch on a
 * count that was known at the call. toridraw_projection_prepared_neon32_tile4_*
 * is that shape written straight: one block, no loop, no tail, no seed, the
 * four bound vectors written directly from the block's outputs, and few
 * enough live vectors that no callee-saved d8-d15 need saving. Bit-identical
 * to the generic body because it is the same core with num_vertices folded
 * to four. Dispatched by the slot when TORIDRAW_PROJ_TILE4 is not 0.
 */

#include <arm_neon.h>
#include <assert.h>
#include <limits.h>
#include <stdint.h>

#include "graphics/dash_vertexint.h"
#include "graphics/shared_tables.h"
#include "impl/projection/projection.scalar_reference.h"
#include "impl/projection/zdiv/projection.zdiv.neon32.u.c"
#include "toridraw_types.h"

/* Compile-time A/B: 1 (default) reads the prepared block's vectors at the
 * point of use inside the loop; 0 hoists them into locals ahead of it, the
 * form that measured as a 440-byte spill frame on armv7. */
#ifndef TORIDRAW_PROJ_PREP_POINT_OF_USE
#define TORIDRAW_PROJ_PREP_POINT_OF_USE 1
#endif

#if defined(__GNUC__) || defined(__clang__)
#define TORIDRAW_PN32_ALWAYS_INLINE __attribute__((always_inline))
#define TORIDRAW_PN32_NOINLINE __attribute__((noinline))
#define TORIDRAW_PN32_ASSUME_ALIGNED8(p) ((const vertexint_t*)__builtin_assume_aligned((p), 8))
#else
#define TORIDRAW_PN32_ALWAYS_INLINE
#define TORIDRAW_PN32_NOINLINE
#define TORIDRAW_PN32_ASSUME_ALIGNED8(p) (p)
#endif

/*
 * Every vertex array this kernel reads was malloc'd (world_decode_tile.c,
 * toridraw_model_transform.c, torirs_model_from_rscache.c, ToriDraw_BufCopy)
 * and so is at least 8-byte aligned on every target this lane builds for --
 * bionic's 32-bit malloc included. The block loads step by four int16s, so
 * every vld1_s16 lands on an 8-byte boundary; telling the compiler so is
 * what stops it legalising the load as ldr, ldr, str, str, vld1 [sp]. The
 * assert is the contract's witness in a debug build.
 */
#define TORIDRAW_PN32_VERTEX_ALIGNED(p) (((uintptr_t)(p) & 7u) == 0)

/*
 * One projection body, four shapes.
 *
 * want_yaw, want_ortho and want_clip are compile-time constants at every call
 * site and the function is always_inline into a noinline entry point, so each
 * `if` below folds away and each entry point gets the straight-line body it
 * asked for -- and ONLY that body, in its own frame.
 *
 * want_yaw is a separate body and not "yaw zero is just cos 65536, sin 0":
 * that identity is exact -- (x * 65536) >> 16 == x for every int16 x, and
 * -32768 * 65536 lands on INT_MIN rather than past it -- but it is four
 * multiplies, two shifts and an add per block spent proving it, on the terrain
 * tiles that are most of the call count.
 *
 * exact_four folds the loop to one block and drops the seed, the fold and the
 * tail; see THE EXACT-FOUR KERNEL above.
 */
TORIDRAW_PN32_ALWAYS_INLINE
static inline void
toridraw_projection_prepared_neon32_core(
    struct ToriDraw_Scene* scene,
    const vertexint_t* vertex_x,
    const vertexint_t* vertex_y,
    const vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    const struct ToriDraw_Position* position,
    int want_yaw,
    int want_ortho,
    int want_clip,
    int exact_four)
{
    assert(scene);
    assert(vertex_x);
    assert(vertex_y);
    assert(vertex_z);
    assert(position);
    assert(model_yaw >= 0);
    assert(model_yaw < 2048);
    assert(!exact_four || num_vertices == 4);
    assert(TORIDRAW_PN32_VERTEX_ALIGNED(vertex_x));
    assert(TORIDRAW_PN32_VERTEX_ALIGNED(vertex_y));
    assert(TORIDRAW_PN32_VERTEX_ALIGNED(vertex_z));

    const struct ToriDraw_ProjectionPreparedCamera* const prep = &scene->projection_prepared_camera;
    int* const orthographic_vertices_x = scene->orthographic_vertices_x;
    int* const orthographic_vertices_y = scene->orthographic_vertices_y;
    int* const orthographic_vertices_z = scene->orthographic_vertices_z;
    int* const screen_vertices_x = scene->screen_vertices_x;
    int* const screen_vertices_y = scene->screen_vertices_y;
    int* const screen_vertices_z = scene->screen_vertices_z;
    int* const bound_out = &scene->projection_bound[0][0];
    int const near_plane_z = want_clip ? scene->projection_near_plane_z : 0;

    vertex_x = TORIDRAW_PN32_ASSUME_ALIGNED8(vertex_x);
    vertex_y = TORIDRAW_PN32_ASSUME_ALIGNED8(vertex_y);
    vertex_z = TORIDRAW_PN32_ASSUME_ALIGNED8(vertex_z);

    /* g_projection_model_yaw_table is the interleaved {cos, sin} pair table --
     * both halves on one cache line, and the same numbers g_cos_table and
     * g_sin_table hold, refreshed together whenever either is reselected. */
    const int* const yaw_row = g_projection_model_yaw_table[model_yaw];

    /*
     * The camera constants. Each member of the prepared block is the same
     * value in all four lanes -- ToriDraw_ScenePrepareProjectionCamera writes
     * it that way, and projection.perspective.prepared.aarch64.S reads the
     * block with paired loads for the same reason -- so a splat is one
     * vld1q_s32. The values are bit-identical to ToriDraw_ReadCosTable(
     * camera->yaw) and friends, which is what the portable kernel calls.
     *
     * The per-model entries -- the model's yaw pair and its scene position --
     * are vld1q_dup'd straight from memory the caller already holds, so no
     * GPR is moved into the vector file for them.
     */
#if TORIDRAW_PROJ_PREP_POINT_OF_USE
#define TORIDRAW_PN32_C_YAW vld1q_s32(prep->cos_yaw)
#define TORIDRAW_PN32_S_YAW vld1q_s32(prep->sin_yaw)
#define TORIDRAW_PN32_C_PITCH vld1q_s32(prep->cos_pitch)
#define TORIDRAW_PN32_S_PITCH vld1q_s32(prep->sin_pitch)
#define TORIDRAW_PN32_COT vld1q_s32(prep->cot15)
#define TORIDRAW_PN32_C_MY vld1q_dup_s32(&yaw_row[0])
#define TORIDRAW_PN32_S_MY vld1q_dup_s32(&yaw_row[1])
#define TORIDRAW_PN32_SCENE_X vld1q_dup_s32(&position->x)
#define TORIDRAW_PN32_SCENE_Y vld1q_dup_s32(&position->y)
#define TORIDRAW_PN32_SCENE_Z vld1q_dup_s32(&position->z)
#else
    int32x4_t const c_yaw_h = vld1q_s32(prep->cos_yaw);
    int32x4_t const s_yaw_h = vld1q_s32(prep->sin_yaw);
    int32x4_t const c_pitch_h = vld1q_s32(prep->cos_pitch);
    int32x4_t const s_pitch_h = vld1q_s32(prep->sin_pitch);
    int32x4_t const cot_h = vld1q_s32(prep->cot15);
    int32x4_t const c_my_h = vdupq_n_s32(yaw_row[0]);
    int32x4_t const s_my_h = vdupq_n_s32(yaw_row[1]);
    int32x4_t const scene_x_h = vdupq_n_s32(position->x);
    int32x4_t const scene_y_h = vdupq_n_s32(position->y);
    int32x4_t const scene_z_h = vdupq_n_s32(position->z);
#define TORIDRAW_PN32_C_YAW c_yaw_h
#define TORIDRAW_PN32_S_YAW s_yaw_h
#define TORIDRAW_PN32_C_PITCH c_pitch_h
#define TORIDRAW_PN32_S_PITCH s_pitch_h
#define TORIDRAW_PN32_COT cot_h
#define TORIDRAW_PN32_C_MY c_my_h
#define TORIDRAW_PN32_S_MY s_my_h
#define TORIDRAW_PN32_SCENE_X scene_x_h
#define TORIDRAW_PN32_SCENE_Y scene_y_h
#define TORIDRAW_PN32_SCENE_Z scene_z_h
#endif

    /* The three per-model scalars the divide body wants as vectors. One
     * vdup.32 from a GPR each; the no-clip body ignores the last three. */
    int32x4_t const v_mid = vdupq_n_s32(model_mid_z);
    int32x4_t const v_near = vdupq_n_s32(near_plane_z);
    int32x4_t const v_neg5000 = vdupq_n_s32(TORIDRAW_SCREEN_X_NEAR_CLIPPED);
    int32x4_t const v_neg5001 = vdupq_n_s32(TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE);

    /*
     * The screen box, accumulated where the screen coordinates are born --
     * the same four vector ops per block projection.perspective.prepared
     * .aarch64.S keeps in v8-v11. VMIN.S32 and VMAX.S32 are A32 instructions,
     * so unlike the SSE2 lane this needs no compare-and-select emulation.
     * Seeded wide; under four vertices no block runs and the seed is what
     * the caller sees, with projection_bound_vertices zero to say so.
     *
     * Not seeded at all for the exact-four shape: its one block's outputs
     * are written to the bound slots directly.
     */
    int32x4_t b_min_x = vdupq_n_s32(INT_MAX);
    int32x4_t b_max_x = vdupq_n_s32(INT_MIN);
    int32x4_t b_min_y = vdupq_n_s32(INT_MAX);
    int32x4_t b_max_y = vdupq_n_s32(INT_MIN);

    int i = 0;

    for( ; exact_four ? i == 0 : i + 4 <= num_vertices; i += 4 )
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
                vaddq_s32(
                    vmulq_s32(xv, TORIDRAW_PN32_C_MY), vmulq_s32(zv, TORIDRAW_PN32_S_MY)),
                16);
            z_rotated = vshrq_n_s32(
                vsubq_s32(
                    vmulq_s32(zv, TORIDRAW_PN32_C_MY), vmulq_s32(xv, TORIDRAW_PN32_S_MY)),
                16);
        }

        x_rotated = vaddq_s32(x_rotated, TORIDRAW_PN32_SCENE_X);
        y_rotated = vaddq_s32(yv, TORIDRAW_PN32_SCENE_Y);
        z_rotated = vaddq_s32(z_rotated, TORIDRAW_PN32_SCENE_Z);

        x_scene = vshrq_n_s32(
            vaddq_s32(
                vmulq_s32(x_rotated, TORIDRAW_PN32_C_YAW),
                vmulq_s32(z_rotated, TORIDRAW_PN32_S_YAW)),
            16);
        z_scene = vshrq_n_s32(
            vsubq_s32(
                vmulq_s32(z_rotated, TORIDRAW_PN32_C_YAW),
                vmulq_s32(x_rotated, TORIDRAW_PN32_S_YAW)),
            16);

        y_scene = vshrq_n_s32(
            vsubq_s32(
                vmulq_s32(y_rotated, TORIDRAW_PN32_C_PITCH),
                vmulq_s32(z_scene, TORIDRAW_PN32_S_PITCH)),
            16);
        z_final = vshrq_n_s32(
            vaddq_s32(
                vmulq_s32(y_rotated, TORIDRAW_PN32_S_PITCH),
                vmulq_s32(z_scene, TORIDRAW_PN32_C_PITCH)),
            16);

        if( want_ortho )
        {
            vst1q_s32(&orthographic_vertices_x[i], x_scene);
            vst1q_s32(&orthographic_vertices_y[i], y_scene);
            vst1q_s32(&orthographic_vertices_z[i], z_final);
        }

        x_scaled = vshrq_n_s32(vmulq_s32(x_scene, TORIDRAW_PN32_COT), 6);
        y_scaled = vshrq_n_s32(vmulq_s32(y_scene, TORIDRAW_PN32_COT), 6);

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

        if( exact_four )
        {
            /* The one block IS the bound: min and max over the same four
             * lanes, which the caller's fold reduces exactly as it would
             * reduce a running accumulator. No seed, no vmin/vmax. */
            vst1q_s32(bound_out + 0, final_x);
            vst1q_s32(bound_out + 4, final_x);
            vst1q_s32(bound_out + 8, final_y);
            vst1q_s32(bound_out + 12, final_y);
            return;
        }

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
    {
        int const cos_model_yaw = yaw_row[0];
        int const sin_model_yaw = yaw_row[1];
        int const scene_x = position->x;
        int const scene_y = position->y;
        int const scene_z = position->z;
        int const cos_camera_yaw = prep->cos_yaw[0];
        int const sin_camera_yaw = prep->sin_yaw[0];
        int const cos_camera_pitch = prep->cos_pitch[0];
        int const sin_camera_pitch = prep->sin_pitch[0];
        int const cot_fov_half_ish15 = prep->cot15[0];

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

#undef TORIDRAW_PN32_C_YAW
#undef TORIDRAW_PN32_S_YAW
#undef TORIDRAW_PN32_C_PITCH
#undef TORIDRAW_PN32_S_PITCH
#undef TORIDRAW_PN32_COT
#undef TORIDRAW_PN32_C_MY
#undef TORIDRAW_PN32_S_MY
#undef TORIDRAW_PN32_SCENE_X
#undef TORIDRAW_PN32_SCENE_Y
#undef TORIDRAW_PN32_SCENE_Z
}

/*
 * The entry points: one noinline function per shape, each the core above
 * with its three flags folded. The slot picks one and calls it directly, so
 * a model pays the frame of the body it runs and no other.
 *
 * Every entry reads the six output arrays, the prepared camera, the near
 * plane and the bound block straight out of the scene, so the call carries
 * six arguments -- four in registers, two on the stack -- rather than the
 * nineteen the portable kernels take.
 *
 * The caller owns the "is the prepared camera the one I mean?" question --
 * toridraw_projection_prepared_eligible -- so these assert that a block was
 * published rather than re-deriving it.
 */
#define TORIDRAW_PN32_DEFINE_ENTRY(name, want_yaw_, want_ortho_, want_clip_, exact_four_)  \
    TORIDRAW_PN32_NOINLINE                                                                 \
    static void name(                                                                      \
        struct ToriDraw_Scene* scene,                                                      \
        const vertexint_t* vertex_x,                                                       \
        const vertexint_t* vertex_y,                                                       \
        const vertexint_t* vertex_z,                                                       \
        int num_vertices,                                                                  \
        int model_yaw,                                                                     \
        int model_mid_z,                                                                   \
        const struct ToriDraw_Position* position)                                          \
    {                                                                                      \
        assert(scene);                                                                     \
        assert(scene->projection_prepared_camera_source);                                  \
        toridraw_projection_prepared_neon32_core(                                          \
            scene, vertex_x, vertex_y, vertex_z, num_vertices, model_yaw, model_mid_z,     \
            position, (want_yaw_), (want_ortho_), (want_clip_), (exact_four_));            \
    }

/* clang-format off */
/*                                                                    yaw ortho clip four */
TORIDRAW_PN32_DEFINE_ENTRY(toridraw_projection_prepared_neon32_tex_yaw_noclip,     1, 1, 0, 0)
TORIDRAW_PN32_DEFINE_ENTRY(toridraw_projection_prepared_neon32_tex_noyaw_noclip,   0, 1, 0, 0)
TORIDRAW_PN32_DEFINE_ENTRY(toridraw_projection_prepared_neon32_notex_yaw_noclip,   1, 0, 0, 0)
TORIDRAW_PN32_DEFINE_ENTRY(toridraw_projection_prepared_neon32_notex_noyaw_noclip, 0, 0, 0, 0)
TORIDRAW_PN32_DEFINE_ENTRY(toridraw_projection_prepared_neon32_tex_yaw_clip,       1, 1, 1, 0)
TORIDRAW_PN32_DEFINE_ENTRY(toridraw_projection_prepared_neon32_tex_noyaw_clip,     0, 1, 1, 0)
TORIDRAW_PN32_DEFINE_ENTRY(toridraw_projection_prepared_neon32_notex_yaw_clip,     1, 0, 1, 0)
TORIDRAW_PN32_DEFINE_ENTRY(toridraw_projection_prepared_neon32_notex_noyaw_clip,   0, 0, 1, 0)

/* The exact-four tile kernels: no-clip only, which is the family every
 * terrain tile at a sane camera distance takes. */
TORIDRAW_PN32_DEFINE_ENTRY(toridraw_projection_prepared_neon32_tile4_tex_yaw,     1, 1, 0, 1)
TORIDRAW_PN32_DEFINE_ENTRY(toridraw_projection_prepared_neon32_tile4_tex_noyaw,   0, 1, 0, 1)
TORIDRAW_PN32_DEFINE_ENTRY(toridraw_projection_prepared_neon32_tile4_notex_yaw,   1, 0, 0, 1)
TORIDRAW_PN32_DEFINE_ENTRY(toridraw_projection_prepared_neon32_tile4_notex_noyaw, 0, 0, 0, 1)
/* clang-format on */

#undef TORIDRAW_PN32_DEFINE_ENTRY

#endif /* TORIDRAW_PROJECTION_PREPARED_NEON32_IMPL_H */
