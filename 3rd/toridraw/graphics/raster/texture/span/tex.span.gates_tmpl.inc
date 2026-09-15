/*
 * One perspective texture scanline, generated once per point of the capability
 * matrix. The includer defines:
 *
 *   TS2_SPAN_FN   scanline entry point name
 *   TS2_GATE      0 = every texel drawn      (texopaque)
 *                 1 = RGB 0 is a colour key  (textrans)
 *                 2 = the texel's own alpha  (texalpha)
 *   TS2_FACEALPHA 1 = additionally weight by the face's alpha
 *   TS2_MODULATE  1 = additionally tint by the face's colour
 *   TS2_ZBUF      1 = depth-test every pixel against the scene z-buffer
 *                 (optional; absent means 0)
 *
 * The uv walk is draw_texture_scanline_*_blend_branching_lerp8_v3_ordered from
 * tex.span.scalar.u.c, unchanged: same span clipping, same carried-forward block
 * endpoints, same linear-fit test, same fallback to a per-pixel exact block.
 * Keeping it identical is the point — it is what lets a face change gate without
 * its texture mapping moving, and it is what the coverage test pins.
 *
 * What differs is only the per-pixel composite, and it is written once in
 * TS2_EMIT below rather than duplicated into the block and remainder paths.
 *
 * These stay scalar. The plain gates call into the per-ISA SIMD spans
 * (tex.span.{scalar,sse2,sse41,avx,neon}.u.c) because they only ever write the
 * frame buffer; every variant here either reads the destination back or does
 * per-pixel work the SIMD chain is not shaped for. The `scanline` family reached
 * the same conclusion for its own facealpha variants.
 *
 * ## The depth dimension (TS2_ZBUF)
 *
 * A depth-tested variant is its plain twin plus two things and nothing else: a
 * compare before the composite, and a store after it. The uv walk above it is
 * untouched — in particular the eight-pixel linear fit STAYS, because the fit
 * decides which texel a pixel samples and depth decides whether that pixel is
 * written. Those are independent, so a face draws the same texels through both
 * variants and only loses the ones something nearer already covered. (The SD
 * `zbuf.screen` walker drops to a per-pixel divide instead; it is one walker for
 * three shading modes and had no fitted block to keep.)
 *
 * Whether a variant WRITES depth is decided here, at compile time, by which
 * point of the matrix it is: a face that can let the destination show through
 * must not occlude what is drawn after it, and that is exactly the `texalpha`
 * gate and the `facealpha` variants. `textrans` still writes — a colour key
 * either draws a pixel opaquely or does not draw it at all, and the store sits
 * behind the same test that decides to draw.
 */

#include "graphics/int_wrap.h"

/* ------------------------------------------------------------- depth */

#ifndef TS2_ZBUF
#define TS2_ZBUF 0
#endif

#if TS2_ZBUF

/* Extra span/helper parameters, appended so the plain variants' argument order
 * is untouched and the two shapes stay diffable. */
#define TS2_ZSPAN_PARAMS , torizdepth_t* RESTRICT zbuffer, float key_edge, float key_step_dx
#define TS2_ZPARAMS , torizdepth_t* RESTRICT zbuffer, float key, float key_step
#define TS2_ZARGS(at_column) , zbuffer, key_edge + key_step_dx * (float)(at_column), key_step

/* Strictly nearer wins. Equal depth keeps what is already there, which is what
 * stops the shared edge between two faces of one model from fighting. */
#define TS2_ZPASS(at) (key > (float)zbuffer[(at)])
#define TS2_ZADVANCE() (key += key_step)

#if TS2_GATE == 2 || TS2_FACEALPHA
/* Translucent: tests, never writes. */
#define TS2_ZWRITE(at) ((void)0)
#else
#define TS2_ZWRITE(at) (zbuffer[(at)] = (torizdepth_t)key)
#endif

#else

#define TS2_ZSPAN_PARAMS
#define TS2_ZPARAMS
#define TS2_ZARGS(at_column)
#define TS2_ZPASS(at) 1
#define TS2_ZADVANCE() ((void)0)
#define TS2_ZWRITE(at) ((void)0)

#endif

/* ---------------------------------------------------------- per pixel */

/*
 * Compose one texel into `pixel_buffer[at]`.
 *
 * Takes an INDEX rather than an lvalue because the depth variants index a second
 * buffer at the same offset — the z-buffer is allocated at the frame buffer's
 * stride and rebased with it precisely so one index walks both.
 *
 * Order is load-bearing: the shade multiplies the texel (SD's `texel x
 * lightness` contract), the tint then multiplies that result, and only the
 * *alpha* decides how much of the destination survives. Tinting after shading
 * rather than before is what keeps a lightness-0 face from collapsing to black —
 * the authored lightness already arrived as `shade`, so a tint carrying it too
 * would count it twice.
 *
 * The depth test wraps the whole composite, so a rejected pixel costs one
 * compare: it does not sample, shade, tint or blend. Advancing `key` is the
 * CALLER's job, at every pixel of the run including the ones that never reach
 * here — see TS2_ZADVANCE at each loop below.
 */
#define TS2_EMIT(at, texel_expr, shade)                                                            \
    do                                                                                             \
    {                                                                                              \
        int ts2_at_ = (at);                                                                        \
                                                                                                   \
        if( TS2_ZPASS(ts2_at_) )                                                                   \
        {                                                                                          \
            toritexel_t ts2_texel_ = (texel_expr);                                                 \
            toritexel_t ts2_rgb_ = ts2_texel_ & TORITEXEL_COLOR_MASK;                              \
            int ts2_skip_ = 0;                                                                     \
            int ts2_alpha_ = 0xFF;                                                                 \
                                                                                                   \
            (void)ts2_skip_;                                                                       \
            (void)ts2_alpha_;                                                                      \
                                                                                                   \
            TS2_GATE_APPLY(ts2_texel_, ts2_rgb_, ts2_skip_, ts2_alpha_);                           \
                                                                                                   \
            if( !ts2_skip_ )                                                                       \
            {                                                                                      \
                toritexel_t ts2_out_ = TS2_SHADE_AND_MODULATE(ts2_rgb_, (shade));                  \
                TS2_FACEALPHA_APPLY(ts2_alpha_);                                                   \
                TS2_STORE(pixel_buffer[ts2_at_], ts2_out_, ts2_alpha_);                            \
                TS2_ZWRITE(ts2_at_);                                                               \
            }                                                                                      \
        }                                                                                          \
    } while( 0 )

#if TS2_GATE == 0
/* Every texel drawn, fully opaque. */
#define TS2_GATE_APPLY(texel, rgb, skip, alpha) ((void)0)
#elif TS2_GATE == 1
/* Colour key: RGB 0 is a hole. Tested on RGB, not on the whole word, so a
 * texture that happens to carry a zero alpha byte on a coloured texel is not
 * mistaken for a hole. */
#define TS2_GATE_APPLY(texel, rgb, skip, alpha)                                                    \
    do                                                                                             \
    {                                                                                              \
        if( toritexel_is_key(texel) )                                                              \
            (skip) = 1;                                                                            \
    } while( 0 )
#else
/* The texel's own alpha. A fully transparent texel is skipped outright rather
 * than blended at 0, so it matches the colour key's "left exactly alone" and
 * costs nothing on the large transparent regions these frames have. */
#define TS2_GATE_APPLY(texel, rgb, skip, alpha)                                                    \
    do                                                                                             \
    {                                                                                              \
        (alpha) = toritexel_alpha(texel);                                                          \
        if( (alpha) == 0 )                                                                         \
            (skip) = 1;                                                                            \
    } while( 0 )
#endif

/*
 * Shade, and tint when this variant modulates — as one operation.
 *
 * Not two: shading down to 8 bits and then multiplying back up rounds twice and
 * amplifies the first rounding, which streaks a smooth gradient. See
 * tex_sampler_shade_tint.
 */
#if TS2_MODULATE
#define TS2_SHADE_AND_MODULATE(rgb, shade) tex_sampler_shade_tint(sampler, (rgb), (shade))
#else
#define TS2_SHADE_AND_MODULATE(rgb, shade) toritexel_shade_blend((rgb), (shade))
#endif

#if TS2_FACEALPHA
#define TS2_FACEALPHA_APPLY(alpha)                                                                 \
    do                                                                                             \
    {                                                                                              \
        (alpha) = tex_sampler_mul255((alpha), sampler->face_alpha);                                \
    } while( 0 )
#else
#define TS2_FACEALPHA_APPLY(alpha) ((void)0)
#endif

#if TS2_GATE == 2 || TS2_FACEALPHA
/* An alpha of exactly 0xFF is a plain store, not a blend: alpha_blend rounds a
 * 0xFF channel down to 0xFE, so routing an opaque pixel through it would dim
 * every fully-opaque texel by one count per channel. */
#define TS2_STORE(dst_ref, rgb, alpha)                                                             \
    do                                                                                             \
    {                                                                                              \
        if( (alpha) >= 0xFF )                                                                      \
            (dst_ref) = toritexel_to_pixel(rgb);                                                   \
        else                                                                                       \
            (dst_ref) = alpha_blend((alpha), (dst_ref), toritexel_to_pixel(rgb));                  \
    } while( 0 )
#else
#define TS2_STORE(dst_ref, rgb, alpha)                                                             \
    do                                                                                             \
    {                                                                                              \
        (void)(alpha);                                                                             \
        (dst_ref) = toritexel_to_pixel(rgb);                                                       \
    } while( 0 )
#endif

/** Eight pixels along a fitted uv line. */
static inline void
TS2_CAT(TS2_SPAN_FN, _block8)(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    const struct ToriDraw_TexSampler* RESTRICT sampler,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int shade TS2_ZPARAMS)
{
    for( int i = 0; i < 8; i++ )
    {
        TS2_EMIT(
            offset + i,
            sampler->texels[tex_sampler_index(sampler, u_scan, v_scan)],
            shade);
        u_scan += step_u;
        v_scan += step_v;
        TS2_ZADVANCE();
    }
}

/**
 * The per-pixel path: an exact divide per pixel, the mapping the reference
 * rasterizer uses for every pixel. Only blocks that fail tex_span_lerp8_fits
 * reach it, so this is cold.
 */
static inline void
TS2_CAT(TS2_SPAN_FN, _exact)(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    const struct ToriDraw_TexSampler* RESTRICT sampler,
    int count,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade TS2_ZPARAMS)
{
    for( int i = 0; i < count; i++ )
    {
        int w = cw >> sampler->shift;
        if( w != 0 )
        {
            /*
             * The exact path indexes by row directly, so it applies the address
             * mode to the raw quotients rather than to a pre-shifted scan. Both
             * axes read their mode off the sampler; neither is hard-wired.
             */
            int u_raw = au / w;
            int v_raw = bv / w;
            int u = sampler->clamp_s ? clamp(u_raw, 0, sampler->width - 1)
                                     : (u_raw & (sampler->width - 1));
            int v = sampler->clamp_t ? clamp(v_raw, 0, sampler->width - 1)
                                     : (v_raw & (sampler->width - 1));
            TS2_EMIT(
                offset + i,
                sampler->texels[u + (v << sampler->shift)],
                shade);
        }
        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;
        /* Outside the `w != 0` guard: a pixel the mapping cannot place is still
         * a pixel of the run, and a key that skipped it would run the rest of
         * the span one column short. */
        TS2_ZADVANCE();
    }
}

/* ---------------------------------------------------------------- span */

static inline void
TS2_SPAN_FN(
    toripixel_t* RESTRICT pixel_buffer,
    int screen_width,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    const struct ToriDraw_TexSampler* RESTRICT sampler TS2_ZSPAN_PARAMS)
{
    int x0 = (screen_x0_ish16 - 1) >> 16;
    if( x0 < 0 )
        x0 = 0;
    int x1 = screen_x1_ish16 >> 16;
    if( x1 >= screen_width )
        x1 = screen_width - 1;
    if( x0 >= x1 )
        return;

    int adjust = x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    int texture_shift = sampler->shift;
    int texture_width = sampler->width;

    int steps = x1 - x0;
    int offset = pixel_offset + x0;
    /* Modular: the reference client relies on int wraparound here, and an edge-on
     * triangle reaches the overflow. See graphics/int_wrap.h. */
    shade8bit_ish8 = toridraw_wrap_add(
        shade8bit_ish8, toridraw_wrap_mul(step_shade8bit_dx_ish8, x0));

#if TS2_ZBUF
    /*
     * The absolute column `offset` currently points at, so each block's starting
     * key is EVALUATED from the plane rather than accumulated across blocks.
     * Accumulating would work too, but the two paths below advance the key at
     * different granularities (per pixel inside a block, per block out here) and
     * a re-evaluation costs one multiply-add per eight pixels to make them
     * agree exactly.
     */
    int zx = x0;
    float const key_step = key_step_dx;
#endif

    int blocks = steps >> 3;
    int remaining = steps & 7;

    int step_au8 = step_au_dx << 3;
    int step_bv8 = step_bv_dx << 3;
    int step_cw8 = step_cw_dx << 3;

    /* Block k's end uv is block k+1's start uv — carried forward rather than
     * divided for twice. Each carried endpoint has already had its own axis's
     * address mode applied, so it is the value the block actually sampled. */
    int cur_u = 0;
    int cur_v = 0;
    int have_cur = 0;

    while( blocks-- )
    {
        int w = cw >> texture_shift;
        if( w != 0 )
        {
            if( !have_cur )
            {
                float inv_w = 1.0f / (float)w;
                cur_u = tex_span_u_quotient_mode(au, w, inv_w, texture_width, sampler->clamp_s);
                cur_v = tex_span_wrapped_quotient(bv, w, inv_w);
            }

            int w_n = (cw + step_cw8) >> texture_shift;
            int nxt_u;
            int nxt_v;
            if( w_n != 0 )
            {
                float inv_w_n = 1.0f / (float)w_n;
                nxt_u = tex_span_u_quotient_mode(
                au + step_au8, w_n, inv_w_n, texture_width, sampler->clamp_s);
                nxt_v = tex_span_wrapped_quotient(bv + step_bv8, w_n, inv_w_n);
            }
            else
            {
                nxt_u = cur_u;
                nxt_v = cur_v;
            }

            /* w_n == 0 leaves nxt_* fabricated from cur_*, so the fit test sees
             * a flat gradient and believes it. The block has no far endpoint;
             * draw it per pixel rather than smearing one row across eight.
             *
             * BOTH axes are tested. A clamped u is bounded into the texture, so
             * it passes by construction and this costs it nothing; a repeating
             * one is as unbounded as v and fails the fit the same way. */
            if( w_n != 0 && tex_span_lerp8_fits(cur_u, nxt_u, texture_width) &&
                tex_span_lerp8_fits(cur_v, nxt_v, texture_width) )
            {
                int s_u = (nxt_u - cur_u) << (texture_shift - 3);
                int s_v = (nxt_v - cur_v) << (texture_shift - 3);

                TS2_CAT(TS2_SPAN_FN, _block8)(
                    pixel_buffer,
                    offset,
                    sampler,
                    tex_span_wrapped_scan_start(cur_u, texture_width, texture_shift),
                    tex_span_wrapped_scan_start(cur_v, texture_width, texture_shift),
                    s_u,
                    s_v,
                    shade8bit_ish8 >> 8 TS2_ZARGS(zx));
            }
            else
            {
                TS2_CAT(TS2_SPAN_FN, _exact)(
                    pixel_buffer,
                    offset,
                    sampler,
                    8,
                    au,
                    bv,
                    cw,
                    step_au_dx,
                    step_bv_dx,
                    step_cw_dx,
                    shade8bit_ish8 >> 8 TS2_ZARGS(zx));
            }

            cur_u = nxt_u;
            cur_v = nxt_v;
            have_cur = (w_n != 0);
        }
        else
        {
            have_cur = 0;
        }
        au += step_au8;
        bv += step_bv8;
        cw += step_cw8;
        offset += 8;
        shade8bit_ish8 =
            toridraw_wrap_add(shade8bit_ish8, toridraw_wrap_shl(step_shade8bit_dx_ish8, 3));
#if TS2_ZBUF
        zx += 8;
#endif
    }

    if( remaining > 0 && (cw >> texture_shift) != 0 )
    {
        int w = cw >> texture_shift;
        if( !have_cur )
        {
            float inv_w = 1.0f / (float)w;
            cur_u = tex_span_u_quotient_mode(au, w, inv_w, texture_width, sampler->clamp_s);
            cur_v = tex_span_wrapped_quotient(bv, w, inv_w);
        }

        int w_n = (cw + step_cw8) >> texture_shift;
        int nxt_u = cur_u;
        int nxt_v = cur_v;
        if( w_n != 0 )
        {
            float inv_w_n = 1.0f / (float)w_n;
            nxt_u = tex_span_u_quotient_mode(
                au + step_au8, w_n, inv_w_n, texture_width, sampler->clamp_s);
            nxt_v = tex_span_wrapped_quotient(bv + step_bv8, w_n, inv_w_n);
        }

        int shade = shade8bit_ish8 >> 8;

        if( w_n == 0 || !tex_span_lerp8_fits(cur_u, nxt_u, texture_width) ||
            !tex_span_lerp8_fits(cur_v, nxt_v, texture_width) )
        {
            TS2_CAT(TS2_SPAN_FN, _exact)(
                pixel_buffer,
                offset,
                sampler,
                remaining,
                au,
                bv,
                cw,
                step_au_dx,
                step_bv_dx,
                step_cw_dx,
                shade TS2_ZARGS(zx));
            return;
        }

        int s_u = (nxt_u - cur_u) << (texture_shift - 3);
        int s_v = (nxt_v - cur_v) << (texture_shift - 3);

        int u_scan = tex_span_wrapped_scan_start(cur_u, texture_width, texture_shift);
        int v_scan = tex_span_wrapped_scan_start(cur_v, texture_width, texture_shift);

#if TS2_ZBUF
        float key = key_edge + key_step_dx * (float)zx;
#endif

        for( int i = 0; i < remaining; i++ )
        {
            TS2_EMIT(
                offset + i,
                sampler->texels[tex_sampler_index(sampler, u_scan, v_scan)],
                shade);
            u_scan += s_u;
            v_scan += s_v;
            TS2_ZADVANCE();
        }
    }
}

#undef TS2_EMIT
#undef TS2_GATE_APPLY
#undef TS2_SHADE_AND_MODULATE
#undef TS2_FACEALPHA_APPLY
#undef TS2_STORE

#undef TS2_ZSPAN_PARAMS
#undef TS2_ZPARAMS
#undef TS2_ZARGS
#undef TS2_ZPASS
#undef TS2_ZADVANCE
#undef TS2_ZWRITE

#undef TS2_SPAN_FN
#undef TS2_GATE
#undef TS2_FACEALPHA
#undef TS2_MODULATE
#undef TS2_ZBUF
