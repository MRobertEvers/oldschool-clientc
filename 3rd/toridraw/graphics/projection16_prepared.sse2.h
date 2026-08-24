#ifndef TORIDRAW_PROJECTION16_PREPARED_SSE2_H
#define TORIDRAW_PROJECTION16_PREPARED_SSE2_H
/*
 * The model-yaw projection family again, this time with the camera hoisted
 * out of the call.
 *
 * WHY A PREPARED CAMERA
 *
 * The census (proj_census.h) puts 1,563,125 of 2,653,519 projection calls at
 * exactly four vertices, and on those calls the per-call setup -- four trig
 * table reads, the cot16 ladder, and a dozen constant splats -- measures at
 * 50-57% of the whole call. The camera that setup is derived from changes
 * once per frame; the calls come two and a half million to the frame. So
 * everything derived from (pitch, yaw, cot16, near) moves into
 * ToriDraw_ProjPrepared, built once, and the call keeps only what really is
 * per-model: one trig pair for the yaw and one rotation of the translate.
 *
 * WHY ONE YAW, NOT TWO
 *
 * Model yaw and camera yaw rotate about the same vertical axis, and rotations
 * about a shared axis commute with the translation between them:
 *
 *     R_cam(R_model v + t)  ==  R_(model+cam) v  +  R_cam t
 *
 * The left side is what projection.u.c does per vertex; the right side does
 * one table lookup at (model_yaw + camera_yaw) & 2047 per call, rotates the
 * scene translate by the camera yaw once per call, and spends one pmaddwd
 * rotation per vertex where the old kernels spend two full rotations. It
 * also erases the model_yaw != 0 special case: yaw zero is just a combined
 * angle that happens to equal the camera's, so one loop serves both and the
 * dispatch branch goes away.
 *
 * The regrouping is not bit-identical to the two-stage arithmetic -- each
 * >> 16 floors, and the fused form floors in different places -- so camera
 * space drifts by a unit or two against projection16_simd.scalar.u.c, and
 * screen x/y by |d| <= 4 at rasterizable depths (the parity harness measures
 * it). screen_z drifts by at most the same couple of units; face sort keys
 * off it, so an exact depth tie can order the other way, which is the same
 * class of tie projection16_fast.sse2.h already concedes.
 *
 * Everything below the rotation is projection16_fast.sse2.h's pipeline: the
 * pitch rotation and fov scale in float (the operands are int32 by then, and
 * mulps is one instruction where SSE2's mullo emulation is six), and the
 * perspective divide as one rcpps that carries the fov scale for free. One
 * refinement falls out: the fused z_scene arrives as an exact integer from
 * the pmaddwd path, so the cvttps round-trip the fast kernel spends to
 * re-floor its float z_scene is simply not needed here.
 */

#include <assert.h>
#include <emmintrin.h>

#include "dash_vertexint.h"
#include "projection.h"

struct ToriDraw_ProjPrepared
{
    /* The camera this was built from, for the caller to compare before
     * reusing across a frame boundary. */
    int camera_pitch;
    int camera_yaw;
    int camera_cot16;
    int near_plane_z;

    /* 16.16 camera yaw pair for the per-call rotate of the translate. */
    int cos_yaw;
    int sin_yaw;

    /* Unit-scale pitch pair and the fov-carrying reciprocal factor. */
    float f_cos_pitch;
    float f_sin_pitch;
    float f_fov;

    /* Integer cot for callers that still want the exact ortho scale. */
    int cot15;
};

#define TORIDRAW_PROJ_PREP_1_OVER_65536 ( 1.0f / 65536.0f )

static inline void
ToriDraw_ProjPrepare(
    struct ToriDraw_ProjPrepared* prep,
    int camera_pitch,
    int camera_yaw,
    int camera_cot16,
    int near_plane_z)
{
    assert(prep);
    assert(camera_pitch >= 0);
    assert(camera_pitch < 2048);
    assert(camera_yaw >= 0);
    assert(camera_yaw < 2048);

    prep->camera_pitch = camera_pitch;
    prep->camera_yaw = camera_yaw;
    prep->camera_cot16 = camera_cot16;
    prep->near_plane_z = near_plane_z;

    prep->cos_yaw = ToriDraw_ReadCosTable(camera_yaw);
    prep->sin_yaw = ToriDraw_ReadSinTable(camera_yaw);

    prep->f_cos_pitch =
        (float)ToriDraw_ReadCosTable(camera_pitch) * TORIDRAW_PROJ_PREP_1_OVER_65536;
    prep->f_sin_pitch =
        (float)ToriDraw_ReadSinTable(camera_pitch) * TORIDRAW_PROJ_PREP_1_OVER_65536;

    prep->cot15 = camera_cot16 >> 1;
    prep->f_fov = (float)prep->cot15 * ( 1.0f / 64.0f );
}

/* Pack one pmaddwd coefficient dword; see projection16_fast.sse2.h for the
 * split -- C == (C >> 8) * 256 + (C & 255), both halves int16, recombined
 * (h << 8) + l, exact mod 2^32. */
static inline __m128i
toridraw_proj_prep_pair16(int first, int second)
{
    assert(first >= -32768);
    assert(first <= 32767);
    assert(second >= -32768);
    assert(second <= 32767);

    return _mm_set1_epi32(
        (int)( ( (unsigned int)second << 16 ) | ( (unsigned int)first & 0xFFFFu ) ));
}

static inline __m128i
toridraw_proj_prep_madd16(__m128i ab, __m128i hi, __m128i lo)
{
    __m128i const h = _mm_madd_epi16(ab, hi);
    __m128i const l = _mm_madd_epi16(ab, lo);

    return _mm_add_epi32(_mm_slli_epi32(h, 8), l);
}

static inline __m128i
toridraw_proj_prep_sx16(__m128i w)
{
    return _mm_srai_epi32(_mm_unpacklo_epi16(w, w), 16);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
static inline void
toridraw_proj_prepared_core(
    const struct ToriDraw_ProjPrepared* prep,
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
    int want_ortho,
    int want_clip)
{
    assert(prep);
    assert(model_yaw >= 0);
    assert(model_yaw < 2048);

    int const combined_yaw = ( model_yaw + prep->camera_yaw ) & 2047;
    int const cos_c = ToriDraw_ReadCosTable(combined_yaw);
    int const sin_c = ToriDraw_ReadSinTable(combined_yaw);
    int const neg_sin_c = -sin_c;

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
    int const tx = scene_x * prep->cos_yaw + scene_z * prep->sin_yaw;
    int const tz = scene_z * prep->cos_yaw - scene_x * prep->sin_yaw;

    __m128i const k_xr_hi = toridraw_proj_prep_pair16(cos_c >> 8, sin_c >> 8);
    __m128i const k_xr_lo = toridraw_proj_prep_pair16(cos_c & 255, sin_c & 255);
    __m128i const k_zr_hi = toridraw_proj_prep_pair16(neg_sin_c >> 8, cos_c >> 8);
    __m128i const k_zr_lo = toridraw_proj_prep_pair16(neg_sin_c & 255, cos_c & 255);

    __m128i const v_tx = _mm_set1_epi32(tx);
    __m128i const v_tz = _mm_set1_epi32(tz);
    __m128i const v_sy = _mm_set1_epi32(scene_y);
    __m128i const v_mid = _mm_set1_epi32(model_mid_z);
    __m128i const v_near = _mm_set1_epi32(prep->near_plane_z);
    __m128i const v_neg5000 = _mm_set1_epi32(TORIDRAW_SCREEN_X_NEAR_CLIPPED);
    __m128i const v_neg5001 = _mm_set1_epi32(TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE);
    __m128i const v_ones = _mm_set1_epi32(-1);

    __m128 const f_ccp = _mm_set1_ps(prep->f_cos_pitch);
    __m128 const f_scp = _mm_set1_ps(prep->f_sin_pitch);
    __m128 const f_fov = _mm_set1_ps(prep->f_fov);

    int i = 0;

    for( ; i + 4 <= num_vertices; i += 4 )
    {
        __m128i const xw = _mm_loadl_epi64((__m128i const*)&vertex_x[i]);
        __m128i const yw = _mm_loadl_epi64((__m128i const*)&vertex_y[i]);
        __m128i const zw = _mm_loadl_epi64((__m128i const*)&vertex_z[i]);

        __m128i const xz = _mm_unpacklo_epi16(xw, zw);

        __m128i const xs = _mm_srai_epi32(
            _mm_add_epi32(
                toridraw_proj_prep_madd16(xz, k_xr_hi, k_xr_lo), v_tx),
            16);
        __m128i const zs = _mm_srai_epi32(
            _mm_add_epi32(
                toridraw_proj_prep_madd16(xz, k_zr_hi, k_zr_lo), v_tz),
            16);
        __m128i const yr = _mm_add_epi32(toridraw_proj_prep_sx16(yw), v_sy);

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
    }

    /*
     * Scalar tail, same arithmetic lane for lane: integer combined rotation
     * (pmaddwd's split recombination is exact mod 2^32, so plain multiplies
     * give the same bits), float pitch in the vector op order, and the same
     * rcpss the vector lanes got -- a tail that divides exactly where the
     * lanes approximate would disagree with them by a pixel.
     */
    for( ; i < num_vertices; i++ )
    {
        int const x = vertex_x[i];
        int const y = vertex_y[i];
        int const z = vertex_z[i];

        int const xs = ( x * cos_c + z * sin_c + tx ) >> 16;
        int const zs = ( z * cos_c - x * sin_c + tz ) >> 16;
        int const yr = y + scene_y;

        float const fys =
            ( (float)yr * prep->f_cos_pitch ) - ( (float)zs * prep->f_sin_pitch );
        float const fzf =
            ( (float)yr * prep->f_sin_pitch ) + ( (float)zs * prep->f_cos_pitch );

        int const izf = (int)fzf;

        if( want_ortho )
        {
            orthographic_vertices_x[i] = xs;
            orthographic_vertices_y[i] = (int)fys;
            orthographic_vertices_z[i] = izf;
        }

        screen_vertices_z[i] = izf - model_mid_z;

        float const rz =
            _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss((float)izf))) * prep->f_fov;

        int screen_x = (int)( (float)xs * rz );
        int screen_y = (int)( fys * rz );

        if( want_clip )
        {
            if( izf < prep->near_plane_z )
            {
                screen_x = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
                screen_y = (int)( fys * prep->f_fov );
            }
            else if( screen_x == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
                screen_x = TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE;
        }

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

/* ------------------------------------------------- textured (six outputs) */

static void
toridraw_proj_prepared_noclip(
    const struct ToriDraw_ProjPrepared* prep,
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
    int scene_z)
{
    toridraw_proj_prepared_core(
        prep,
        orthographic_vertices_x,
        orthographic_vertices_y,
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices,
        model_yaw,
        model_mid_z,
        scene_x,
        scene_y,
        scene_z,
        1,
        0);
}

static void
toridraw_proj_prepared_clip(
    const struct ToriDraw_ProjPrepared* prep,
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
    int scene_z)
{
    toridraw_proj_prepared_core(
        prep,
        orthographic_vertices_x,
        orthographic_vertices_y,
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices,
        model_yaw,
        model_mid_z,
        scene_x,
        scene_y,
        scene_z,
        1,
        1);
}

/* ------------------------------------------------ untextured (screen only) */

static void
toridraw_proj_prepared_notex_noclip(
    const struct ToriDraw_ProjPrepared* prep,
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
    int scene_z)
{
    toridraw_proj_prepared_core(
        prep,
        NULL,
        NULL,
        NULL,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices,
        model_yaw,
        model_mid_z,
        scene_x,
        scene_y,
        scene_z,
        0,
        0);
}

static void
toridraw_proj_prepared_notex_clip(
    const struct ToriDraw_ProjPrepared* prep,
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
    int scene_z)
{
    toridraw_proj_prepared_core(
        prep,
        NULL,
        NULL,
        NULL,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices,
        model_yaw,
        model_mid_z,
        scene_x,
        scene_y,
        scene_z,
        0,
        1);
}

#endif /* TORIDRAW_PROJECTION16_PREPARED_SSE2_H */
