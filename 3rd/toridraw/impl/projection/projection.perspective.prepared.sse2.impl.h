#ifndef TORIDRAW_PROJECTION16_PREPARED_SSE2_H
#define TORIDRAW_PROJECTION16_PREPARED_SSE2_H
/*
 * The model-yaw projection family again, this time with the camera hoisted
 * out of the call and the call itself cut down to the arguments that really
 * do change per model.
 *
 * WHY A PREPARED CAMERA
 *
 * The census (proj_census.h) puts 1,563,125 of 2,653,519 projection calls at
 * exactly four vertices, and on those calls the per-call setup -- four trig
 * table reads, the cot16 ladder, and a dozen constant splats -- measures at
 * 50-57% of the whole call. The camera that setup is derived from changes
 * once per frame; the calls come two and a half million to the frame. So
 * everything derived from (pitch, yaw, cot16) is read out of the
 * ToriDraw_ProjectionPreparedCamera the scene already carries -- the same
 * block projection16.aarch64.S loads with two ldp pairs -- and the call keeps
 * only what is genuinely per-model.
 *
 * WHY THE SCENE POINTER AND NOT TWENTY ARGUMENTS
 *
 * projection16.aarch64.S takes the six output arrays as one pointer to the
 * contiguous Scene block and reloads them with `ldp`;
 * benchmarks/projection_neon_scene goes further and passes every parameter in
 * one args struct. Both are answering the same thing: on the hot four-vertex
 * call the argument marshalling is a real fraction of the work. On 32-bit x86
 * that is not a subtlety -- cdecl passes everything on the stack, and
 * project_vertices_array_fused_noclip has twenty parameters, so every
 * four-vertex model costs twenty pushes before a single vertex moves. These
 * entry points take nine, and the scene, the position, and the three vertex
 * arrays are pointers the caller already had. The _Static_asserts in
 * toridraw.c pin the Scene layout this relies on.
 *
 * WHY ONE YAW, NOT TWO
 *
 * Model yaw and camera yaw rotate about the same vertical axis, and rotations
 * about a shared axis commute with the translation between them:
 *
 *     R_cam(R_model v + t)  ==  R_(model+cam) v  +  R_cam t
 *
 * The left side is what projection.u.c does per vertex -- and what both
 * assembly kernels still do, two `mul`/`mla` rotations deep in every vector
 * body. The right side does one lookup at (model_yaw + camera_yaw) & 2047 per
 * call, rotates the scene translate by the camera yaw once per call, and
 * spends one pmaddwd rotation per vertex where the old kernels spend two. It
 * also erases the model_yaw != 0 special case that the C kernels, the Apple
 * kernel, and the benchmark kernel each carry a duplicate loop for: yaw zero
 * is just a combined angle that happens to equal the camera's, so one loop
 * serves both.
 *
 * The lookup uses g_projection_model_yaw_table, the interleaved {cos, sin}
 * pair table the assembly reads with `ld2r` -- on x86 there is no paired
 * splat to gain, but both halves still land on one cache line instead of two.
 *
 * project_vertices_aarch64.S states the cost of the fusion outright: "a
 * precomposed matrix is faster to set up but rounds only once, whereas the
 * renderer rounds after each Euler rotation." Correct, and the reason that
 * benchmark declines the trade is that it exists to compare identical
 * pictures. This kernel is allowed to take it: each >> 16 floors, the fused
 * form floors in different places, so camera space drifts by a unit or two
 * against projection16_simd.scalar.u.c and screen x/y by |d| <= 4 at
 * rasterizable depths (the parity harness measures it, and the residue
 * correction below is what keeps it there). screen_z drifts by at most the
 * same couple of units; face sort keys off it, so an exact depth tie can
 * order the other way -- the same class of tie projection16_fast.sse2.h
 * already concedes.
 *
 * Everything below the rotation is projection16_fast.sse2.h's pipeline: the
 * pitch rotation and fov scale in float (the operands are int32 by then, and
 * mulps is one instruction where SSE2's mullo emulation is six), and the
 * perspective divide as one rcpps that carries the fov scale for free. The
 * assembly needs frecpe plus an frecps Newton step because NEON's estimate is
 * eight bits; rcpps gives twelve, and twelve is a quarter pixel at x = 640,
 * so the step is left out. One further refinement falls out of the fusion:
 * the fused z_scene arrives as an exact integer from the pmaddwd path, so the
 * cvttps round-trip the fast kernel spends to re-floor its float z_scene is
 * simply not needed here.
 */

#include <assert.h>
#include <emmintrin.h>
#include <limits.h>

#include "graphics/dash_vertexint.h"
#include "impl/projection/projection.scalar_reference.h"
#include "graphics/shared_tables.h"
#include "toridraw_types.h"

#define TORIDRAW_PROJECTION_PREP_1_OVER_65536 ( 1.0f / 65536.0f )


/* Pack one pmaddwd coefficient dword; see projection16_fast.sse2.h for the
 * split -- C == (C >> 8) * 256 + (C & 255), both halves int16, recombined
 * (h << 8) + l, exact mod 2^32. */
static inline __m128i
toridraw_projection_prep_pair16(int first, int second)
{
    assert(first >= -32768);
    assert(first <= 32767);
    assert(second >= -32768);
    assert(second <= 32767);

    return _mm_set1_epi32(
        (int)( ( (unsigned int)second << 16 ) | ( (unsigned int)first & 0xFFFFu ) ));
}

static inline __m128i
toridraw_projection_prep_madd16(__m128i ab, __m128i hi, __m128i lo)
{
    __m128i const h = _mm_madd_epi16(ab, hi);
    __m128i const l = _mm_madd_epi16(ab, lo);

    return _mm_add_epi32(_mm_slli_epi32(h, 8), l);
}

static inline __m128i
toridraw_projection_prep_sx16(__m128i w)
{
    return _mm_srai_epi32(_mm_unpacklo_epi16(w, w), 16);
}

/* Lane-wise signed min/max on the SSE2 floor: pminsd/pmaxsd are SSE4.1. */
static inline __m128i
toridraw_projection_prep_min_epi32(__m128i a, __m128i b)
{
    __m128i const a_gt = _mm_cmpgt_epi32(a, b);
    return _mm_or_si128(_mm_and_si128(a_gt, b), _mm_andnot_si128(a_gt, a));
}

static inline __m128i
toridraw_projection_prep_max_epi32(__m128i a, __m128i b)
{
    __m128i const a_gt = _mm_cmpgt_epi32(a, b);
    return _mm_or_si128(_mm_and_si128(a_gt, a), _mm_andnot_si128(a_gt, b));
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
static inline void
toridraw_projection_prepared_core(
    const struct ToriDraw_ProjectionPreparedCamera* prep,
    const struct ToriDraw_ProjectionPreparedCameraFloat* prep_f,
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
    int camera_yaw,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int want_ortho,
    int want_clip,
    int* bound_out)
{
    assert(prep);
    assert(prep_f);
    assert(screen_vertices_x);
    assert(screen_vertices_y);
    assert(screen_vertices_z);
    assert(vertex_x);
    assert(vertex_y);
    assert(vertex_z);
    assert(camera_yaw >= 0);
    assert(camera_yaw < 2048);
    assert(model_yaw >= 0);
    assert(model_yaw < 2048);

    int const combined_yaw = ( model_yaw + camera_yaw ) & 2047;
    int const cos_c = g_projection_model_yaw_table[combined_yaw][0];
    int const sin_c = g_projection_model_yaw_table[combined_yaw][1];
    int const neg_sin_c = -sin_c;

    int const cos_yaw = prep->cos_yaw[0];
    int const sin_yaw = prep->sin_yaw[0];

    /*
     * R_cam t: the only rotation left that runs per call, not per vertex.
     * Kept in 16.16 and added to the vertex product *before* the shift --
     * flooring it separately would cost a unit of camera space, and a unit
     * of camera space is cot15/64/z pixels, about ten of them at the near
     * plane. Range: the vertex term tops out near 1024 * 65536 * 1.41 and
     * the translate near 13312 * 65536 * 1.41, so the sum stays inside
     * int32 across the whole scene -- the same envelope projection.u.c
     * already documents for its own 16.16 products.
     */
    int tx = scene_x * cos_yaw + scene_z * sin_yaw;
    int tz = scene_z * cos_yaw - scene_x * sin_yaw;

    /*
     * Cancel the reference's lost half-unit.
     *
     * projection.u.c floors the model rotation before the camera rotation
     * consumes it, so each coordinate arrives short by a residue a, b in
     * [0, 1), and the camera rotation carries that shortfall forward as
     * (a*cos + b*sin) / 65536 in x and (b*cos - a*sin) / 65536 in z. The
     * fused form never takes that intermediate floor, so it sits above the
     * reference by exactly that much -- up to 1.41 units, which the
     * perspective divide turns into cot15/64/z pixels: invisible out in the
     * scene, but eight pixels at z = 100.
     *
     * a and b are uniform on [0, 1), so the shortfall averages half the
     * coefficient sum. Subtracting that mean centres the error on zero
     * instead of leaning one way, and it costs nothing: it is a constant
     * folded into a translate that was already being added. Only when the
     * model rotation actually ran, though -- at yaw zero the reference takes
     * no intermediate floor at all and there is nothing to cancel.
     */
    if( model_yaw != 0 )
    {
        tx -= ( cos_yaw + sin_yaw ) >> 1;
        tz -= ( cos_yaw - sin_yaw ) >> 1;
    }

    __m128i const k_xr_hi = toridraw_projection_prep_pair16(cos_c >> 8, sin_c >> 8);
    __m128i const k_xr_lo = toridraw_projection_prep_pair16(cos_c & 255, sin_c & 255);
    __m128i const k_zr_hi = toridraw_projection_prep_pair16(neg_sin_c >> 8, cos_c >> 8);
    __m128i const k_zr_lo = toridraw_projection_prep_pair16(neg_sin_c & 255, cos_c & 255);

    __m128i const v_tx = _mm_set1_epi32(tx);
    __m128i const v_tz = _mm_set1_epi32(tz);
    __m128i const v_sy = _mm_set1_epi32(scene_y);
    __m128i const v_mid = _mm_set1_epi32(model_mid_z);
    __m128i const v_near = _mm_set1_epi32(near_plane_z);
    __m128i const v_neg5000 = _mm_set1_epi32(TORIDRAW_SCREEN_X_NEAR_CLIPPED);
    __m128i const v_neg5001 = _mm_set1_epi32(TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE);
    __m128i const v_ones = _mm_set1_epi32(-1);

    /*
     * Read, not derived. These three were a load, a cvtdq2ps and a mulps each,
     * every call, for values the frame fixed once -- see
     * ToriDraw_ProjectionPreparedCameraFloat for why the conversion is exact
     * and why the block is separate.
     *
     * ALIGNED loads, where the helper this replaces used movups on the belief
     * that a 32-bit malloc only promises eight bytes. That stopped being true
     * when td_scene_alloc_aligned arrived: the scene base is aligned to
     * _Alignof(struct ToriDraw_Scene), which the _Alignas(16) members drag to
     * sixteen. The proof is already in the build -- the compiler emits aligned
     * STORES to the sibling int block in ToriDraw_ScenePrepareProjectionCamera
     * on the strength of the same _Alignas, and those would have been faulting
     * all along if the premise were false.
     */
    __m128 const f_ccp = _mm_load_ps(prep_f->cos_pitch);
    __m128 const f_scp = _mm_load_ps(prep_f->sin_pitch);
    __m128 const f_fov = _mm_load_ps(prep_f->cot15);

    int i = 0;

    /*
     * The screen box, accumulated where the screen coordinates are born --
     * the same thing projection16.aarch64.S does in v8-v11. Lane-wise min x,
     * max x, min y, max y over every full block, written to the scene's
     * projection_bound afterwards; the caller records how many vertices
     * that covers (count & ~3) and toridraw_projected_bound sweeps only the
     * tail. Matters most on this lane: SSE2 has no pminsd, so the sweep it
     * replaces was the scalar loop, one compare-and-branch per coordinate.
     * Here a min is a pcmpgtd and three logic ops -- four per block, on
     * registers already in hand.
     */
    __m128i b_min_x = _mm_set1_epi32(INT_MAX);
    __m128i b_max_x = _mm_set1_epi32(INT_MIN);
    __m128i b_min_y = _mm_set1_epi32(INT_MAX);
    __m128i b_max_y = _mm_set1_epi32(INT_MIN);

    for( ; i + 4 <= num_vertices; i += 4 )
    {
        __m128i const xw = _mm_loadl_epi64((__m128i const*)&vertex_x[i]);
        __m128i const yw = _mm_loadl_epi64((__m128i const*)&vertex_y[i]);
        __m128i const zw = _mm_loadl_epi64((__m128i const*)&vertex_z[i]);

        __m128i const xz = _mm_unpacklo_epi16(xw, zw);

        __m128i const xs = _mm_srai_epi32(
            _mm_add_epi32(
                toridraw_projection_prep_madd16(xz, k_xr_hi, k_xr_lo), v_tx),
            16);
        __m128i const zs = _mm_srai_epi32(
            _mm_add_epi32(
                toridraw_projection_prep_madd16(xz, k_zr_hi, k_zr_lo), v_tz),
            16);
        __m128i const yr = _mm_add_epi32(toridraw_projection_prep_sx16(yw), v_sy);

        __m128 const fxs = _mm_cvtepi32_ps(xs);
        __m128 const fzs = _mm_cvtepi32_ps(zs);
        __m128 const fyr = _mm_cvtepi32_ps(yr);

        __m128 const fys =
            _mm_sub_ps(_mm_mul_ps(fyr, f_ccp), _mm_mul_ps(fzs, f_scp));
        __m128 const fzf =
            _mm_add_ps(_mm_mul_ps(fyr, f_scp), _mm_mul_ps(fzs, f_ccp));

        __m128i const izf = _mm_cvttps_epi32(fzf);

        if( want_ortho )
        {
            _mm_storeu_si128((__m128i*)&orthographic_vertices_x[i], xs);
            _mm_storeu_si128(
                (__m128i*)&orthographic_vertices_y[i], _mm_cvttps_epi32(fys));
            _mm_storeu_si128((__m128i*)&orthographic_vertices_z[i], izf);
        }

        _mm_storeu_si128(
            (__m128i*)&screen_vertices_z[i], _mm_sub_epi32(izf, v_mid));

        /* The reciprocal takes the truncated z: an int divide by a z that
         * truncates to zero must keep producing the INT_MIN the divide-based
         * kernel produced. See projection16_fast.sse2.h. */
        __m128 const rz =
            _mm_mul_ps(_mm_rcp_ps(_mm_cvtepi32_ps(izf)), f_fov);

        __m128i final_x = _mm_cvttps_epi32(_mm_mul_ps(fxs, rz));
        __m128i final_y = _mm_cvttps_epi32(_mm_mul_ps(fys, rz));

        if( want_clip )
        {
            __m128i const clipped = _mm_cmplt_epi32(izf, v_near);
            __m128i const not_clipped = _mm_xor_si128(clipped, v_ones);
            __m128i const fix =
                _mm_and_si128(_mm_cmpeq_epi32(final_x, v_neg5000), not_clipped);
            __m128i const y_scaled = _mm_cvttps_epi32(_mm_mul_ps(fys, f_fov));

            final_x = _mm_or_si128(
                _mm_and_si128(fix, v_neg5001), _mm_andnot_si128(fix, final_x));
            final_x = _mm_or_si128(
                _mm_and_si128(clipped, v_neg5000),
                _mm_andnot_si128(clipped, final_x));
            final_y = _mm_or_si128(
                _mm_and_si128(clipped, y_scaled),
                _mm_andnot_si128(clipped, final_y));
        }

        _mm_storeu_si128((__m128i*)&screen_vertices_x[i], final_x);
        _mm_storeu_si128((__m128i*)&screen_vertices_y[i], final_y);

        if( bound_out )
        {
            b_min_x = toridraw_projection_prep_min_epi32(b_min_x, final_x);
            b_max_x = toridraw_projection_prep_max_epi32(b_max_x, final_x);
            b_min_y = toridraw_projection_prep_min_epi32(b_min_y, final_y);
            b_max_y = toridraw_projection_prep_max_epi32(b_max_y, final_y);
        }
    }

    if( bound_out )
    {
        _mm_storeu_si128((__m128i*)(bound_out + 0), b_min_x);
        _mm_storeu_si128((__m128i*)(bound_out + 4), b_max_x);
        _mm_storeu_si128((__m128i*)(bound_out + 8), b_min_y);
        _mm_storeu_si128((__m128i*)(bound_out + 12), b_max_y);
    }

    /* Four-vertex models are 59% of all calls and never reach the tail; give
     * the common case an exit before the tail's own setup. */
    if( i == num_vertices )
        return;

    /*
     * Scalar tail, same arithmetic lane for lane: integer combined rotation
     * (pmaddwd's split recombination is exact mod 2^32, so plain multiplies
     * give the same bits), float pitch in the vector op order, and the same
     * rcpss the vector lanes got -- a tail that divides exactly where the
     * lanes approximate would disagree with them by a pixel.
     */
    float const s_ccp = _mm_cvtss_f32(f_ccp);
    float const s_scp = _mm_cvtss_f32(f_scp);
    float const s_fov = _mm_cvtss_f32(f_fov);

    for( ; i < num_vertices; i++ )
    {
        int const x = vertex_x[i];
        int const y = vertex_y[i];
        int const z = vertex_z[i];

        int const xs = ( x * cos_c + z * sin_c + tx ) >> 16;
        int const zs = ( z * cos_c - x * sin_c + tz ) >> 16;
        int const yr = y + scene_y;

        float const fys = ( (float)yr * s_ccp ) - ( (float)zs * s_scp );
        float const fzf = ( (float)yr * s_scp ) + ( (float)zs * s_ccp );

        int const izf = (int)fzf;

        if( want_ortho )
        {
            orthographic_vertices_x[i] = xs;
            orthographic_vertices_y[i] = (int)fys;
            orthographic_vertices_z[i] = izf;
        }

        screen_vertices_z[i] = izf - model_mid_z;

        float const rz =
            _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss((float)izf))) * s_fov;

        int screen_x = (int)( (float)xs * rz );
        int screen_y = (int)( fys * rz );

        if( want_clip )
        {
            if( izf < near_plane_z )
            {
                screen_x = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
                screen_y = (int)( fys * s_fov );
            }
            else if( screen_x == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
                screen_x = TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE;
        }

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

/*
 * The four entry points. Each reads the six output arrays and the prepared
 * camera straight out of the scene, so the call site hands over pointers it
 * is already holding rather than unpacking twenty values onto the stack.
 *
 * The caller owns the "is the prepared camera the one I mean?" question --
 * scene->projection_prepared_camera_source == camera, exactly the test the
 * Apple dispatch makes -- so these assert that one was published rather than
 * re-deriving it.
 */

/* ------------------------------------------------- textured (six outputs) */

static void
ToriDraw_ProjectionPreparedNoclip(
    struct ToriDraw_Scene* scene,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int camera_yaw,
    int model_yaw,
    int model_mid_z,
    const struct ToriDraw_Position* position)
{
    assert(scene);
    assert(position);
    assert(scene->projection_prepared_camera_source);

    toridraw_projection_prepared_core(
        &scene->projection_prepared_camera,
        &scene->projection_prepared_camera_f,
        scene->orthographic_vertices_x,
        scene->orthographic_vertices_y,
        scene->orthographic_vertices_z,
        scene->screen_vertices_x,
        scene->screen_vertices_y,
        scene->screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices,
        camera_yaw,
        model_yaw,
        model_mid_z,
        position->x,
        position->y,
        position->z,
        0,
        1,
        0,
        &scene->projection_bound[0][0]);
}

static void
ToriDraw_ProjectionPreparedClip(
    struct ToriDraw_Scene* scene,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int camera_yaw,
    int model_yaw,
    int model_mid_z,
    const struct ToriDraw_Position* position)
{
    assert(scene);
    assert(position);
    assert(scene->projection_prepared_camera_source);

    toridraw_projection_prepared_core(
        &scene->projection_prepared_camera,
        &scene->projection_prepared_camera_f,
        scene->orthographic_vertices_x,
        scene->orthographic_vertices_y,
        scene->orthographic_vertices_z,
        scene->screen_vertices_x,
        scene->screen_vertices_y,
        scene->screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices,
        camera_yaw,
        model_yaw,
        model_mid_z,
        position->x,
        position->y,
        position->z,
        scene->projection_near_plane_z,
        1,
        1,
        &scene->projection_bound[0][0]);
}

/* ------------------------------------------------ untextured (screen only) */

static void
ToriDraw_ProjectionPreparedNotexNoclip(
    struct ToriDraw_Scene* scene,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int camera_yaw,
    int model_yaw,
    int model_mid_z,
    const struct ToriDraw_Position* position)
{
    assert(scene);
    assert(position);
    assert(scene->projection_prepared_camera_source);

    toridraw_projection_prepared_core(
        &scene->projection_prepared_camera,
        &scene->projection_prepared_camera_f,
        NULL,
        NULL,
        NULL,
        scene->screen_vertices_x,
        scene->screen_vertices_y,
        scene->screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices,
        camera_yaw,
        model_yaw,
        model_mid_z,
        position->x,
        position->y,
        position->z,
        0,
        0,
        0,
        &scene->projection_bound[0][0]);
}

static void
ToriDraw_ProjectionPreparedNotexClip(
    struct ToriDraw_Scene* scene,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int camera_yaw,
    int model_yaw,
    int model_mid_z,
    const struct ToriDraw_Position* position)
{
    assert(scene);
    assert(position);
    assert(scene->projection_prepared_camera_source);

    toridraw_projection_prepared_core(
        &scene->projection_prepared_camera,
        &scene->projection_prepared_camera_f,
        NULL,
        NULL,
        NULL,
        scene->screen_vertices_x,
        scene->screen_vertices_y,
        scene->screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices,
        camera_yaw,
        model_yaw,
        model_mid_z,
        position->x,
        position->y,
        position->z,
        scene->projection_near_plane_z,
        0,
        1,
        &scene->projection_bound[0][0]);
}


#endif /* TORIDRAW_PROJECTION16_PREPARED_SSE2_H */
