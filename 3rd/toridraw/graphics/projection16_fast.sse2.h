#ifndef TORIDRAW_PROJECTION16_FAST_SSE2_H
#define TORIDRAW_PROJECTION16_FAST_SSE2_H
/*
 * The model-yaw projection family, rebuilt around the two instructions SSE2
 * actually gives you.
 *
 * WHAT THE OLD KERNEL SPENDS ITS TIME ON
 *
 * projection16_simd.sse2.u.c does every one of its three rotations, and the
 * field-of-view scale, with mullo_epi32_sse. SSE2 has no pmulld -- that is
 * SSE4.1 -- so each of those is emulated as two pmuludq, a psrldq, two pshufd
 * and a punpckldq. The textured model-yaw block issues fourteen of them, and
 * an objdump of the i686 lane (toolchains/mingw32/bin/objdump.exe on
 * src/torirs.exe) shows the result: 171 instructions and 13 reloads of spilled
 * constants for every four vertices.
 *
 * WHAT THIS DOES INSTEAD
 *
 * Model yaw runs on pmaddwd. Its inputs are the raw vertexint_t coordinates,
 * which are int16 already, and pmaddwd is precisely "multiply adjacent int16
 * pairs and add them" -- the shape of x*cos + z*sin. The only obstacle is that
 * a 1.16 sine does not fit in an int16, so each coefficient is split as
 * C == (C >> 8) * 256 + (C & 255), both halves int16, and the two pmaddwd
 * results recombined with a pslld and a paddd. That recombination is exact
 * modulo 2^32, which is also what the original signed multiply was, so this
 * stage is bit-identical -- including on the overflow that a far-flung vertex
 * would produce.
 *
 * The two camera rotations and the fov scale run in float. Their operands are
 * full int32 by then, so pmaddwd cannot reach them, and mulps costs one
 * instruction where mullo_epi32_sse costs six.
 *
 * WHY FLOAT IS ALLOWED HERE
 *
 * Because it already is. projection_zdiv_simd.sse2.u.c divides by z with a
 * bare _mm_rcp_ps and no Newton step -- about 12 bits. Every screen coordinate
 * this file is asked to produce has already been through that, so a rotation
 * carried in 24-bit float is strictly tighter than the error the caller
 * accepts today. What does change is the rounding direction: an arithmetic
 * >> 16 floors, cvttps2dq truncates toward zero, so a negative camera-space
 * coordinate can land one unit off the old value. Face ordering keys off
 * screen_vertices_z, so an exact depth tie can sort the other way; nothing
 * else downstream resolves finely enough to see it.
 *
 * The fov scale folds into the divide. The old path computed
 * (x_scene * cot15) >> 6 in integer and then multiplied by the reciprocal;
 * cot15 / 64.0f is exact for any sane fov, so it multiplies into the
 * reciprocal once per block and rides along for free.
 */

#include <assert.h>
#include <emmintrin.h>

#include "dash_vertexint.h"
#include "projection.h"

/*
 * Pack one pmaddwd coefficient dword. A source vector's dword holds the pair
 * (first, second) as int16 lane 0 and lane 1, so the coefficients go in the
 * same order.
 */
static inline __m128i
toridraw_proj_pair16(int first, int second)
{
    assert(first >= -32768);
    assert(first <= 32767);
    assert(second >= -32768);
    assert(second <= 32767);

    return _mm_set1_epi32(
        (int)( ( (unsigned int)second << 16 ) | ( (unsigned int)first & 0xFFFFu ) ));
}

/*
 * (a * ca + b * cb) for int16 a, b and 1.16 coefficients, exact mod 2^32.
 * `ab` holds the (a, b) pairs; `hi` and `lo` are the >> 8 and & 255 halves.
 */
static inline __m128i
toridraw_proj_madd16(__m128i ab, __m128i hi, __m128i lo)
{
    __m128i const h = _mm_madd_epi16(ab, hi);
    __m128i const l = _mm_madd_epi16(ab, lo);

    return _mm_add_epi32(_mm_slli_epi32(h, 8), l);
}

/* Sign-extend the low four int16 of a vector into four int32. */
static inline __m128i
toridraw_proj_sx16(__m128i w)
{
    return _mm_srai_epi32(_mm_unpacklo_epi16(w, w), 16);
}

#define TORIDRAW_PROJ_1_OVER_65536 ( 1.0f / 65536.0f )

/*
 * `has_yaw`, `want_ortho` and `want_clip` are literals at every call site, so
 * each wrapper below compiles to one specialised loop with the branches gone
 * -- but only if it really is inlined. Left to itself GCC folds the four
 * wrappers back into a single constprop clone and tests `has_yaw` and
 * `want_ortho` once per four vertices, inside the loop, off the stack frame.
 * always_inline is the whole point of the parameter.
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
static inline void
toridraw_proj_fast_core(
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
    int near_plane_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw,
    int has_yaw,
    int want_ortho,
    int want_clip)
{
    int const cot_fov_half_ish15 = camera_cot16 >> 1;
    int const cos_camera_pitch = ToriDraw_ReadCosTable(camera_pitch);
    int const sin_camera_pitch = ToriDraw_ReadSinTable(camera_pitch);
    int const cos_camera_yaw = ToriDraw_ReadCosTable(camera_yaw);
    int const sin_camera_yaw = ToriDraw_ReadSinTable(camera_yaw);
    int const sin_model_yaw = has_yaw ? ToriDraw_ReadSinTable(model_yaw) : 0;
    int const cos_model_yaw = has_yaw ? ToriDraw_ReadCosTable(model_yaw) : 65536;
    int const neg_sin_model_yaw = -sin_model_yaw;

    /* Model yaw, split into the two int16 halves pmaddwd can take. */
    __m128i const k_xr_hi =
        toridraw_proj_pair16(cos_model_yaw >> 8, sin_model_yaw >> 8);
    __m128i const k_xr_lo =
        toridraw_proj_pair16(cos_model_yaw & 255, sin_model_yaw & 255);
    __m128i const k_zr_hi =
        toridraw_proj_pair16(neg_sin_model_yaw >> 8, cos_model_yaw >> 8);
    __m128i const k_zr_lo =
        toridraw_proj_pair16(neg_sin_model_yaw & 255, cos_model_yaw & 255);

    __m128 const f_scene_x = _mm_set1_ps((float)scene_x);
    __m128 const f_scene_y = _mm_set1_ps((float)scene_y);
    __m128 const f_scene_z = _mm_set1_ps((float)scene_z);
    __m128 const f_ccy = _mm_set1_ps((float)cos_camera_yaw * TORIDRAW_PROJ_1_OVER_65536);
    __m128 const f_scy = _mm_set1_ps((float)sin_camera_yaw * TORIDRAW_PROJ_1_OVER_65536);
    __m128 const f_ccp = _mm_set1_ps((float)cos_camera_pitch * TORIDRAW_PROJ_1_OVER_65536);
    __m128 const f_scp = _mm_set1_ps((float)sin_camera_pitch * TORIDRAW_PROJ_1_OVER_65536);
    __m128 const f_fov = _mm_set1_ps((float)cot_fov_half_ish15 * ( 1.0f / 64.0f ));

    __m128i const v_mid = _mm_set1_epi32(model_mid_z);
    __m128i const v_near = _mm_set1_epi32(near_plane_z);
    __m128i const v_neg5000 = _mm_set1_epi32(TORIDRAW_SCREEN_X_NEAR_CLIPPED);
    __m128i const v_neg5001 = _mm_set1_epi32(TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE);
    __m128i const v_ones = _mm_set1_epi32(-1);

    int i = 0;

    for( ; i + 4 <= num_vertices; i += 4 )
    {
        __m128i const xw = _mm_loadl_epi64((__m128i const*)&vertex_x[i]);
        __m128i const yw = _mm_loadl_epi64((__m128i const*)&vertex_y[i]);
        __m128i const zw = _mm_loadl_epi64((__m128i const*)&vertex_z[i]);

        __m128i xr;
        __m128i zr;
        __m128 fx;
        __m128 fy;
        __m128 fz;
        __m128 fxs;
        __m128 fzs;
        __m128 fys;
        __m128 fzf;
        __m128 rz;
        __m128i izf;
        __m128i final_x;
        __m128i final_y;

        if( has_yaw )
        {
            __m128i const xz = _mm_unpacklo_epi16(xw, zw);

            xr = _mm_srai_epi32(toridraw_proj_madd16(xz, k_xr_hi, k_xr_lo), 16);
            zr = _mm_srai_epi32(toridraw_proj_madd16(xz, k_zr_hi, k_zr_lo), 16);
        }
        else
        {
            xr = toridraw_proj_sx16(xw);
            zr = toridraw_proj_sx16(zw);
        }

        fx = _mm_add_ps(_mm_cvtepi32_ps(xr), f_scene_x);
        fy = _mm_add_ps(_mm_cvtepi32_ps(toridraw_proj_sx16(yw)), f_scene_y);
        fz = _mm_add_ps(_mm_cvtepi32_ps(zr), f_scene_z);

        fxs = _mm_add_ps(_mm_mul_ps(fx, f_ccy), _mm_mul_ps(fz, f_scy));
        fzs = _mm_sub_ps(_mm_mul_ps(fz, f_ccy), _mm_mul_ps(fx, f_scy));

        /*
         * The integer path shifts this intermediate down by 16 and so drops
         * its fraction before the pitch rotation reads it. Every other
         * intermediate can keep its fraction for free -- an extra unit in xs
         * or ys is worth 1/z of a pixel -- but zs is the one that ends up in
         * the denominator, where a unit of drift at z == 200 moves a vertex
         * at the edge of the viewport by four pixels. Two instructions buy
         * the agreement back. Truncation matches the shift's floor for
         * positive depths, which is every depth the caller will draw.
         */
        fzs = _mm_cvtepi32_ps(_mm_cvttps_epi32(fzs));

        fys = _mm_sub_ps(_mm_mul_ps(fy, f_ccp), _mm_mul_ps(fzs, f_scp));
        fzf = _mm_add_ps(_mm_mul_ps(fy, f_scp), _mm_mul_ps(fzs, f_ccp));

        izf = _mm_cvttps_epi32(fzf);

        if( want_ortho )
        {
            _mm_storeu_si128(
                (__m128i*)&orthographic_vertices_x[i], _mm_cvttps_epi32(fxs));
            _mm_storeu_si128(
                (__m128i*)&orthographic_vertices_y[i], _mm_cvttps_epi32(fys));
            _mm_storeu_si128((__m128i*)&orthographic_vertices_z[i], izf);
        }

        _mm_storeu_si128(
            (__m128i*)&screen_vertices_z[i], _mm_sub_epi32(izf, v_mid));

        /*
         * One reciprocal carries both the divide and the fov scale. It takes
         * the truncated z, not the float one: the old kernel divided by an
         * int, so a z that truncates to zero has to keep producing the
         * infinity -- and the INT_MIN out of cvttps2dq -- that it did before.
         * Reciprocating 0.3f instead would hand back a large finite number,
         * and the two kernels would disagree by two billion on that lane.
         */
        rz = _mm_mul_ps(_mm_rcp_ps(_mm_cvtepi32_ps(izf)), f_fov);

        final_x = _mm_cvttps_epi32(_mm_mul_ps(fxs, rz));
        final_y = _mm_cvttps_epi32(_mm_mul_ps(fys, rz));

        if( want_clip )
        {
            __m128i const clipped = _mm_cmplt_epi32(izf, v_near);
            __m128i const not_clipped = _mm_xor_si128(clipped, v_ones);
            __m128i const fix =
                _mm_and_si128(_mm_cmpeq_epi32(final_x, v_neg5000), not_clipped);
            /* The clipped lane keeps the scaled y, undivided. */
            __m128i const y_scaled =
                _mm_cvttps_epi32(_mm_mul_ps(fys, f_fov));

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
     * The census puts 1.67% of vertices here, so it stays scalar and stays
     * the same arithmetic the old kernel used.
     */
    for( ; i < num_vertices; i++ )
    {
        int const x = vertex_x[i];
        int const y = vertex_y[i];
        int const z = vertex_z[i];
        int x_rotated;
        int z_rotated;
        int y_rotated;
        int x_scene;
        int z_scene;
        int y_scene;
        int z_final_scene;
        int screen_x;
        int screen_y;

        if( has_yaw )
        {
            x_rotated = ( x * cos_model_yaw + z * sin_model_yaw ) >> 16;
            z_rotated = ( z * cos_model_yaw - x * sin_model_yaw ) >> 16;
        }
        else
        {
            x_rotated = x;
            z_rotated = z;
        }

        x_rotated += scene_x;
        y_rotated = y + scene_y;
        z_rotated += scene_z;

        x_scene = ( x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw ) >> 16;
        z_scene = ( z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw ) >> 16;
        y_scene = ( y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch ) >> 16;
        z_final_scene =
            ( y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch ) >> 16;

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
            if( want_clip
                && screen_vertices_x[i] == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
                screen_vertices_x[i] = TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE;
            screen_vertices_y[i] = screen_y / z_final_scene;
        }
    }
}

/* -------------------------------------------------- textured (six outputs) */

static inline void
toridraw_proj_fast_noclip(
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
    int camera_cot16,
    int camera_pitch,
    int camera_yaw)
{
    /* clang-format off */
    if( model_yaw != 0 )
        toridraw_proj_fast_core(
            orthographic_vertices_x, orthographic_vertices_y,
            orthographic_vertices_z, screen_vertices_x, screen_vertices_y,
            screen_vertices_z, vertex_x, vertex_y, vertex_z, num_vertices,
            model_yaw, model_mid_z, near_plane_z, scene_x, scene_y, scene_z,
            camera_cot16, camera_pitch, camera_yaw, 1, 1, 0);
    else
        toridraw_proj_fast_core(
            orthographic_vertices_x, orthographic_vertices_y,
            orthographic_vertices_z, screen_vertices_x, screen_vertices_y,
            screen_vertices_z, vertex_x, vertex_y, vertex_z, num_vertices,
            0, model_mid_z, near_plane_z, scene_x, scene_y, scene_z,
            camera_cot16, camera_pitch, camera_yaw, 0, 1, 0);
    /* clang-format on */
}

static inline void
toridraw_proj_fast_clip(
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
    int camera_cot16,
    int camera_pitch,
    int camera_yaw)
{
    /* clang-format off */
    if( model_yaw != 0 )
        toridraw_proj_fast_core(
            orthographic_vertices_x, orthographic_vertices_y,
            orthographic_vertices_z, screen_vertices_x, screen_vertices_y,
            screen_vertices_z, vertex_x, vertex_y, vertex_z, num_vertices,
            model_yaw, model_mid_z, near_plane_z, scene_x, scene_y, scene_z,
            camera_cot16, camera_pitch, camera_yaw, 1, 1, 1);
    else
        toridraw_proj_fast_core(
            orthographic_vertices_x, orthographic_vertices_y,
            orthographic_vertices_z, screen_vertices_x, screen_vertices_y,
            screen_vertices_z, vertex_x, vertex_y, vertex_z, num_vertices,
            0, model_mid_z, near_plane_z, scene_x, scene_y, scene_z,
            camera_cot16, camera_pitch, camera_yaw, 0, 1, 1);
    /* clang-format on */
}

/* ----------------------------------------------- untextured (screen only) */

/*
 * The untextured entries drop the orthographic outputs. Nothing reads them,
 * so the three stores and two cvttps2dq go away; the scratch below only
 * exists to keep one core inline rather than two.
 */
static inline void
toridraw_proj_fast_notex_noclip(
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
    int camera_cot16,
    int camera_pitch,
    int camera_yaw)
{
    /* clang-format off */
    if( model_yaw != 0 )
        toridraw_proj_fast_core(
            NULL, NULL, NULL, screen_vertices_x, screen_vertices_y,
            screen_vertices_z, vertex_x, vertex_y, vertex_z, num_vertices,
            model_yaw, model_mid_z, near_plane_z, scene_x, scene_y, scene_z,
            camera_cot16, camera_pitch, camera_yaw, 1, 0, 0);
    else
        toridraw_proj_fast_core(
            NULL, NULL, NULL, screen_vertices_x, screen_vertices_y,
            screen_vertices_z, vertex_x, vertex_y, vertex_z, num_vertices,
            0, model_mid_z, near_plane_z, scene_x, scene_y, scene_z,
            camera_cot16, camera_pitch, camera_yaw, 0, 0, 0);
    /* clang-format on */
}

static inline void
toridraw_proj_fast_notex_clip(
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
    int camera_cot16,
    int camera_pitch,
    int camera_yaw)
{
    /* clang-format off */
    if( model_yaw != 0 )
        toridraw_proj_fast_core(
            NULL, NULL, NULL, screen_vertices_x, screen_vertices_y,
            screen_vertices_z, vertex_x, vertex_y, vertex_z, num_vertices,
            model_yaw, model_mid_z, near_plane_z, scene_x, scene_y, scene_z,
            camera_cot16, camera_pitch, camera_yaw, 1, 0, 1);
    else
        toridraw_proj_fast_core(
            NULL, NULL, NULL, screen_vertices_x, screen_vertices_y,
            screen_vertices_z, vertex_x, vertex_y, vertex_z, num_vertices,
            0, model_mid_z, near_plane_z, scene_x, scene_y, scene_z,
            camera_cot16, camera_pitch, camera_yaw, 0, 0, 1);
    /* clang-format on */
}

#endif /* TORIDRAW_PROJECTION16_FAST_SSE2_H */
